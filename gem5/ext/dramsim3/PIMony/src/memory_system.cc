#include "memory_system.h"

namespace pimony
{
  MemorySystem::MemorySystem(const std::string &mem_config, const std::string &model_config,
                             const std::string &log_dir, const std::string &log_level,
                             std::function<void(uint32_t)> pim_callback,
                             std::function<void(uint64_t)> read_callback,
                             std::function<void(uint64_t)> write_callback)
      : pim_callback_(pim_callback),
        read_callback_(read_callback),
        write_callback_(write_callback)
  {
    CommandLineParser cmd_parser = CommandLineParser();

    if (log_level == "trace")
      spdlog::set_level(spdlog::level::trace);
    else if (log_level == "debug")
      spdlog::set_level(spdlog::level::debug);
    else if (log_level == "info")
      spdlog::set_level(spdlog::level::info);

    Config::global_config.log_level = log_level;
    Config::global_config.log_dir = log_dir;

    initialize_memory_config(mem_config);
    initialize_model_config(model_config);

    addr_map_ok_ = false;   // set true only if the ini says rorabacobgch

    std::ifstream file(Config::global_config.pim_config_path);
    std::string line;

    while (std::getline(file, line))
    {
      // Remove comments
      size_t comment_pos = line.find(';');
      if (comment_pos != std::string::npos)
        line = line.substr(0, comment_pos);

      // Trim whitespace
      line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

      // Skip empty lines and section headers
      if (line.empty() || line[0] == '[')
        continue;

      // Parse key = value
      size_t eq = line.find('=');
      if (eq == std::string::npos)
        continue;

      std::string key = line.substr(0, eq);
      std::string value = line.substr(eq + 1);

      if (key == "tCK")
        tCK = std::stod(value);
      else if (key == "bus_width")
        bus_width = std::stoi(value);
      else if (key == "BL")
        BL = std::stoi(value);
      else if (key == "trans_queue_size")
        trans_queue_size = std::stoi(value);
      else if (key == "channels")
        channels = std::stoi(value);
      // --- sequencer address geometry (same keys the PIM controller uses) ---
      else if (key == "device_width")
        device_width_ = std::stoi(value);
      else if (key == "ranks")
        ini_ranks_ = std::stoi(value);
      else if (key == "bankgroups")
        ini_bankgroups_ = std::stoi(value);
      else if (key == "banks_per_group")
        ini_banks_per_group_ = std::stoi(value);
      else if (key == "columns")
        ini_columns_ = std::stoi(value);
      else if (key == "address_mapping")
        addr_map_ok_ = (value == "rorabacobgch");
    }

    // Field strides, low to high, exactly as address_mapping = rorabacobgch
    // orders them. Each level's stride is the previous one times its count.
    chunk_bytes_ = (uint64_t)(device_width_ / 8) * BL;   // 32 B column step
    ch_stride_   = chunk_bytes_;                         // channel is LOWEST
    bg_stride_   = ch_stride_ * channels;
    col_stride_  = bg_stride_ * ini_bankgroups_;
    cols_per_row_ = ini_columns_ / BL;                   // 64 -> the row cap
    bank_stride_ = col_stride_ * cols_per_row_;
    rank_stride_ = bank_stride_ * ini_banks_per_group_;
    row_stride_  = rank_stride_ * ini_ranks_;
    engines_     = channels * ini_ranks_ * ini_bankgroups_;   // 32 streams
    units_       = engines_ * ini_banks_per_group_;           // 128 MAC units

    num_bankgroups = Config::global_config.dram_bankgroups_per_ch;

    dram = new PIM(Config::global_config);
    request_handler =
        new Request::TraceRequestHandler(Config::global_config, "");

    pim_done.resize(channels, true);
    pim_done_bg.resize(channels);
    for (size_t ch = 0; ch < channels; ch++)
      pim_done_bg[ch].resize(num_bankgroups, true);

    clk_ = 0;
  }

  MemorySystem::~MemorySystem()
  {
    delete (dram);
    delete (request_handler);
  }

  void MemorySystem::ClockTick()
  {
    // CPU-driven PIM: LLM workload completion signals disabled.
    // The CPU now owns PIM completion (via pim.waitcompletion / interrupt).
    // if (request_handler->slo_violation_flag)
    // {
    //   std::cout << "(memory_system) SLO violation" << std::endl;
    //   // request_handler->print_state();
    //   pim_callback_();
    // }

    // if (request_handler->is_pim_operation_done())
    // {
    //   std::cout << "(memory_system) all PIM operation is done" << std::endl;
    //   request_handler->print_state();
    //   // dram->print_stat();
    //   pim_callback_();
    // }

    for (int ch = 0; ch < channels; ch++)
    {
      // ===== [CPU R/W FLOW · step 4/4] INJECT =====
      // PREV  <- Request.cc getNextAccess() returned a CPU request   [step 3/4]
      // Pull one request for this channel; if real (request==true), push it
      // into the DRAM model. Empty marker (request==false) is just deleted.
      //   DONE: request is now inside the DRAM timing model (PIMSim).
      mem_request = request_handler->getNextAccess(ch, clk_, pim_done[ch], pim_done_bg[ch]);
      if (mem_request->request != false)
      {
        dram->push(ch, mem_request);
      }
      else
      {
        delete mem_request;
      }

      if (pim_done[ch])
      {
        pim_done[ch] = false;
      }

      for (size_t bg = 0; bg < num_bankgroups; bg++)
      {
        if (pim_done_bg[ch][bg])
        {
          pim_done_bg[ch][bg] = false;
        }
      }
    }

    // SEQUENCER: turn a bit more of the pending pim.gemv into MAC commands.
    // Before dram->cycle() so anything injected now is seen this same cycle.
    IssuePendingGemv();

    // TICK: advance the DRAM timing model one cycle. Requests that have
    // finished their latency now become visible on the response side below.
    dram->cycle();

    // ===== [CPU R/W RESPONSE · step 1/3] DRAIN =====
    // After the tick, pull every finished response off each channel.
    //   dram->top() = peek a done response,  dram->pop() = remove it.
    // Each response is one HALF of a CPU access (recall the partner-channel
    // split in AddTransaction, step 1/4 of the request trail).
    for (int ch = 0; ch < channels; ch++)
    {
      while (!dram->is_empty(ch))
      {
        mem_response = dram->top(ch);
        if (mem_response->req_type == MemoryAccessType::READ)
        {
          request_handler->update_latency(ch, clk_, true, false, false, mem_response->id);
          // ===== [CPU R/W RESPONSE · step 2/3] REASSEMBLE =====
          // sub2orig maps this half's address back to the original CPU address.
          // remain_ counts down from 2 -> 0 as the two halves return.
          if (read_remain_[read_sub2orig_[mem_response->dram_address]] == 2)
          {
            // First half back: 2 -> 1. Not done yet, do NOT notify gem5.
            read_remain_[read_sub2orig_[mem_response->dram_address]]--;
            read_sub2orig_.erase(mem_response->dram_address);
          }
          else
          {
            // Second (last) half back: both halves done -> the access is complete.
            // ===== [CPU R/W RESPONSE · step 3/3] NOTIFY =====
            // Fire read_callback_ (the function gem5 gave us) with the ORIGINAL
            // address -> gem5 wakes the CPU: "your load data is ready."
            read_remain_.erase(read_sub2orig_[mem_response->dram_address]);
            read_callback_(read_sub2orig_[mem_response->dram_address]);
            read_sub2orig_.erase(mem_response->dram_address);
          }
        }
        else if (mem_response->req_type == MemoryAccessType::WRITE)
        {
          // Same 3-step pattern as READ above (REASSEMBLE -> NOTIFY), but fires
          // write_callback_ instead. Two halves -> one "write done" to gem5.
          request_handler->update_latency(ch, clk_, false, true, false, mem_response->id);
          if (write_remain_[write_sub2orig_[mem_response->dram_address]] == 2)
          {
            write_remain_[write_sub2orig_[mem_response->dram_address]]--;
            write_sub2orig_.erase(mem_response->dram_address);
          }
          else
          {
            write_remain_.erase(write_sub2orig_[mem_response->dram_address]);
            write_callback_(write_sub2orig_[mem_response->dram_address]);
            write_sub2orig_.erase(mem_response->dram_address);
          }
        }
        else
        {
          // GEMV completion. Every MAC of the job lands here even though
          // comp=0 keeps it silent, so counting drains is what tells us the
          // job is over -- issue order is not completion order across 32
          // engines. Fire only once ALL are back AND nothing is left to issue.
          // A buffer has been filled. When the last channel reports in, the
          // MACs may start -- that transition is the whole point of the phase.
          if (gemv_.active &&
              mem_response->req_type == MemoryAccessType::D2GWRITE &&
              mem_response->cpu_token == gemv_.cpu_token &&
              gemv_.gwrite_out > 0)
          {
            gemv_.gwrite_out--;
            if (gemv_.gwrite_out == 0 && gemv_.gwrite_todo == 0)
              gemv_.phase = GemvPhase::COMPUTE;
          }

          if (gemv_.active &&
              (mem_response->req_type == MemoryAccessType::MAC ||
               mem_response->req_type == MemoryAccessType::READRES) &&
              mem_response->cpu_token == gemv_.cpu_token &&
              gemv_.outstanding > 0)
          {
            gemv_.outstanding--;
            uint32_t s = GemvEngineOfAddr(mem_response->dram_address);

            if (mem_response->req_type == MemoryAccessType::MAC)
            {
              // The multiply is done, but the stream is NOT free: it now holds
              // banks_per_group finished accumulators, one per bank, and
              // cannot begin its next dot until they are read out. So
              // engine_busy deliberately STAYS set; the readouts clear it.
              // The response arrives after the MAC pipeline has drained
              // (complete_cycle adds mac_pipeline-1 extra tCCD_L), which is
              // exactly when the results are readable.
              gemv_.readres_base[s] = mem_response->dram_address;
              gemv_.readres_todo[s] = (uint8_t)ini_banks_per_group_;
              gemv_.readres_pending += (uint32_t)ini_banks_per_group_;
            }
            else
            {
              gemv_.readres_out[s]--;
              if (gemv_.readres_todo[s] == 0 && gemv_.readres_out[s] == 0)
                gemv_.engine_busy &= ~(1ULL << s);   // now genuinely idle
            }

            // Done only when every MAC is issued, every readout is issued, and
            // nothing is in flight. outstanding can hit 0 transiently between a
            // MAC draining and its readouts issuing, so it is not sufficient on
            // its own -- and engine_busy must NOT be force-cleared there.
            if (gemv_.outstanding == 0 && gemv_.readres_pending == 0 &&
                GemvIssueDone())
            {
              gemv_.engine_busy = 0;
              gemv_.phase = GemvPhase::DONE;
              gemv_.active = false;
              pim_callback_(gemv_.cpu_token);
            }
          }
          if (mem_response->pim_last)
          {
            request_handler->update_latency(ch, clk_, false, false, true, mem_response->id);
            if (mem_response->bankgroup == -1)
              pim_done[ch] = true;
            else
            {
              pim_done_bg[ch][mem_response->bankgroup] = true;
            }
            // MAC done -> tell gem5: pimComplete() posts the interrupt that
            // wakes the hart quiesced in pim.wait. (MVP: pimNotified latches
            // this once; re-arm when multi-dispatch is added.)
            pim_callback_(mem_response->cpu_token);
          }
        }
        // Response consumed: free it and remove it from the channel's queue.
        delete mem_response;
        dram->pop(ch);   // [CPU R/W RESPONSE] end of one response
      }
    }

    clk_++;
  }

  void MemorySystem::RegisterCallbacks(
      std::function<void(uint64_t)> read_callback,
      std::function<void(uint64_t)> write_callback)
  {
    read_callback_ = read_callback;
    write_callback_ = write_callback;
  }
  double MemorySystem::GetTCK() const { return tCK; }

  int MemorySystem::GetBusBits() const { return bus_width; }

  int MemorySystem::GetBurstLength() const { return BL; }

  int MemorySystem::GetQueueSize() const { return trans_queue_size; }

  void MemorySystem::PrintStats() const
  {
    dram->print_stat();
  }

  void MemorySystem::ResetStats()
  {
    dram->ResetStats();
  }

  bool MemorySystem::WillAcceptTransaction(uint64_t hex_addr,
                                           bool is_write) const
  {
    MemoryAccessType req_type = MemoryAccessType::READ;
    if (is_write)
    {
      req_type = MemoryAccessType::WRITE;
    }
    return dram->_mem->WillAcceptTransaction(hex_addr, int(req_type));
  }

  // ===== [CPU R/W FLOW · step 1/4] ENTRY =====
  // gem5 calls this for a CPU read/write. It builds a TraceEntry and hands it
  // to the request_handler's queue.
  //   NEXT  -> Request.cc  AddNormalTransaction()   [step 2/4]
  bool MemorySystem::AddTransaction(uint64_t hex_addr, bool is_write)
  {
    TraceEntry request;
    request.type = is_write ? MemoryAccessType::WRITE : MemoryAccessType::READ;

    request.address = hex_addr;
    request.issue_cycle = clk_;
    request_handler->AddNormalTransaction(request);

    const auto &Addr = request_handler->trace_generator.address;
    const uint64_t shifted = hex_addr >> Addr.shift_bits;

    const int ch = static_cast<int>((shifted >> Addr.ch_pos) & Addr.ch_mask);
    const int ra = static_cast<int>((shifted >> Addr.ra_pos) & Addr.ra_mask);
    const int bg = static_cast<int>((shifted >> Addr.bg_pos) & Addr.bg_mask);
    const int ba = static_cast<int>((shifted >> Addr.ba_pos) & Addr.ba_mask);
    const int ro = static_cast<int>((shifted >> Addr.ro_pos) & Addr.ro_mask);
    const int co = static_cast<int>((shifted >> Addr.co_pos) & Addr.co_mask);

    int partner_ch = (ch % 2 == 0) ? (ch + 1) : (ch - 1);

    uint64_t partner_addr = Addr.ReverseAddressMapping(
        partner_ch, ra, bg, ba, ro, co);
    if (is_write)
    {
      write_remain_[hex_addr] = 2;
      write_sub2orig_[hex_addr] = hex_addr;
      write_sub2orig_[partner_addr] = hex_addr;
    }
    else
    {
      read_remain_[hex_addr] = 2;
      read_sub2orig_[hex_addr] = hex_addr;
      read_sub2orig_[partner_addr] = hex_addr;
    }
    request.address = partner_addr;
    request_handler->AddNormalTransaction(request);
    return true;
  }

  // ===== [pim.gemv · 4a] ENTRY =====
  // gem5 hands over a WHOLE GEMV. This records it and returns immediately --
  // not one MAC is issued here, so the instruction costs the CPU no more than
  // a store. The expansion into MAC commands happens in IssuePendingGemv()
  // over the following cycles, like a DMA engine working through a descriptor.
  bool MemorySystem::AddGEMVTransaction(uint64_t base, uint64_t v_base,
                                        uint32_t num_outputs,
                                        uint32_t dot_steps, uint32_t cpu_token)
  {
    // Returning false means ONE thing only -- busy -- so that the caller can
    // always safely retry. The permanent failures below are contract
    // violations that no amount of retrying fixes, so they stop the run
    // instead of spinning forever.
    if (gemv_.active) return false;                     // busy: retry is correct

    if (!addr_map_ok_)
    {
      spdlog::error("pim.gemv needs address_mapping = rorabacobgch; the "
                    "sequencer's stride derivation is not valid otherwise");
      std::exit(1);
    }
    if (num_outputs == 0 || dot_steps == 0)
    {
      spdlog::error("pim.gemv shape is degenerate: outputs={} dot_steps={}",
                    num_outputs, dot_steps);
      std::exit(1);
    }
    // A dot must fit ONE row. Longer reductions are split by software into
    // partial matmuls whose partials the host sums; folding that in here would
    // require an accumulator to survive a precharge, which PIMony cannot model
    // and the paper does not settle.
    if (dot_steps > (uint32_t)cols_per_row_)
    {
      spdlog::error("pim.gemv dot_steps={} exceeds one row ({} steps). Split "
                    "the reduction into partial matmuls and sum on the host.",
                    dot_steps, cols_per_row_);
      std::exit(1);
    }
    // base must contribute WHOLE ROWS and nothing else: row is now the top
    // field a dot touches, so anything finer carries into rank/bank/column
    // partway through the matrix and splits dots across units. Verified with
    // layoutcheck.cc: bases 0 / 256 KB / 20 MB pass; 128 KB (rank), 32 KB
    // (bank), 512 B (step) all fail. STRICTER than the old rank_stride_ check.
    if (base % row_stride_ != 0)
    {
      spdlog::error("pim.gemv base 0x{:x} is not a multiple of the row stride "
                    "{} -- dots would split across units. Use PIM_ALIGN "
                    "(1<<18) on the weight array.", base, row_stride_);
      std::exit(1);
    }

    // The vector's requirement is WEAKER than the weights': its layout only
    // ever sets the channel and column fields, so a base that shifts bank /
    // rank / row shifts all four copies alike and is harmless. What it must
    // NOT do is arrive with column bits already set -- the last steps would
    // then spill past column 63 into the next bank, which is a different
    // whiteboard's shelf. bank_stride_ is the first stride above column, so
    // it is exactly that condition, DERIVED from the ini (32 KB here, but the
    // number never appears).
    if (v_base % bank_stride_ != 0)
    {
      spdlog::error("pim.gemv v_base 0x{:x} is not a multiple of the bank "
                    "stride {} -- the vector would spill past the end of its "
                    "row. Use PIM_ALIGN on the vector array.", v_base,
                    bank_stride_);
      std::exit(1);
    }

    gemv_.base = base;
    gemv_.v_base = v_base;
    gemv_.num_outputs = num_outputs;
    gemv_.dot_steps = dot_steps;
    gemv_.cpu_token = cpu_token;
    gemv_.next_cmd = 0;
    gemv_.outstanding = 0;
    gemv_.readres_pending = 0;
    gemv_.engine_busy = 0;
    for (int s = 0; s < 64; s++)
    { gemv_.readres_todo[s] = 0; gemv_.readres_out[s] = 0; }
    gemv_.gwrite_todo = (uint8_t)channels;
    gemv_.gwrite_out = 0;
    gemv_.phase = GemvPhase::GWRITE;
    gemv_.active = true;
    return true;
  }

  // Which of the 32 streams command `cmd` targets. Consecutive commands go to
  // DIFFERENT streams (breadth-first): the whole machine is filled before any
  // stream is revisited. Depth-first -- all of one stream's commands, then the
  // next -- leaves 31 streams idle behind the busy gate, which measured 4.1%
  // utilisation. The order is a sequencer scheduling policy, NOT part of the
  // ISA: the same instruction runs correctly either way, just slower.
  uint32_t MemorySystem::GemvCmdStream(uint64_t cmd) const
  {
    return (uint32_t)(cmd % (uint64_t)engines_);
  }

  // Address a command starts from -- the same field arithmetic bert.c's
  // pim_idx() used to PLACE the weight, run backwards to FIND it.
  //   cmd -> stream (channel, rank, bankgroup)  and  wave (DRAM row)
  // Bank and column are 0: the command carries no bank field (it broadcasts to
  // all banks_per_group of them, which is exactly the 4 outputs it computes),
  // and every dot starts at column 0 of its row.
  uint64_t MemorySystem::GemvCmdAddr(uint64_t cmd) const
  {
    uint64_t stream = GemvCmdStream(cmd);
    uint64_t ch     = stream % (uint64_t)channels;
    uint64_t bg     = (stream / channels) % (uint64_t)ini_bankgroups_;
    uint64_t rank   = stream / ((uint64_t)channels * ini_bankgroups_);
    uint64_t wave   = cmd / (uint64_t)engines_;

    return gemv_.base + ch * ch_stride_ + bg * bg_stride_
           + rank * rank_stride_ + wave * row_stride_;
  }

  // Steps in one command. A dot fits one row by contract (checked at entry),
  // so this is the whole dot -- but keep it a function: it is the thing that
  // changes if the row cap is ever relaxed.
  uint32_t MemorySystem::GemvCmdRun(uint64_t cmd) const
  {
    (void)cmd;
    return gemv_.dot_steps;
  }

  uint32_t MemorySystem::GemvEngineOfAddr(uint64_t addr) const
  {
    uint64_t off  = (addr >= gemv_.base) ? (addr - gemv_.base) : 0;
    uint64_t ch   = (off / ch_stride_)   % (uint64_t)channels;
    uint64_t bg   = (off / bg_stride_)   % (uint64_t)ini_bankgroups_;
    uint64_t rank = (off / rank_stride_) % (uint64_t)ini_ranks_;
    // same packing GemvCmdAddr uses: slot = ch + bg*channels + rank*ch*bg
    return (uint32_t)(ch + bg * channels
                      + rank * (uint64_t)channels * ini_bankgroups_);
  }

  // ===== [pim.gemv · 4b] EXPAND =====
  // Once per DRAM cycle, before dram->cycle(), so a MAC injected now is
  // visible to the controller this same cycle. Emits at most
  // kGemvMacsPerCycle and only while the queue will take one -- that check IS
  // the backpressure. PIMony offers none of its own: AddMACTransaction returns
  // true unconditionally and its internal asserts are compiled out in its
  // Release build, so an overflow drops the MAC silently and the token would
  // never complete.
  void MemorySystem::IssuePendingGemv()
  {
    if (!gemv_.active) return;

    // ONE issue port, shared by every command type: at most one command goes
    // out per DRAM cycle, whatever kind it is.
    uint32_t budget = kGemvMacsPerCycle;

    // ===== [pim.gemv . GWRITE] FILL THE GLOBAL BUFFERS =====
    // The global buffer is CHANNEL-scoped: a D2GWRITE broadcasts one DRAM row
    // to all banks of its channel, and a channel cannot be fed from another
    // channel's arrays. So the vector is loaded once PER CHANNEL -- four
    // commands, not one, and not one per bankgroup.
    //
    // The address is the SOURCE row to copy FROM, not a destination: a channel
    // has exactly one buffer, so there is nothing to select, and the channel is
    // already carried by the address (lowest field, ch_stride_ apart). Hence
    // v_base + ch * ch_stride_ -- the four per-channel copies bert.c wrote.
    //
    // NO MAC ISSUES UNTIL ALL FOUR HAVE DRAINED. A MAC multiplies the row it
    // names by the vector in the buffer, so a MAC on an unfilled buffer is
    // reading a blank. The wait is cheap by construction: the D2GWRITE
    // transaction itself completes in zero cycles (TransToCommand drops
    // num_macs for it), while the real cost -- the row activation, and
    // gwrite_latency blocking whatever follows -- is charged by the DRAM
    // timing model either way.
    if (gemv_.phase == GemvPhase::GWRITE)
    {
      while (gemv_.gwrite_todo > 0 && budget > 0)
      {
        uint32_t ch   = (uint32_t)channels - gemv_.gwrite_todo;
        uint64_t addr = gemv_.v_base + (uint64_t)ch * ch_stride_;

        if (!WillAcceptTransaction(addr, false)) break;   // backpressure

        AddPIMTransaction(MemoryAccessType::D2GWRITE, addr, 0, gemv_.cpu_token,
                          false);
        gemv_.gwrite_todo--;
        gemv_.gwrite_out++;
        budget--;
      }
      return;                        // COMPUTE is entered by the last drain
    }

    if (gemv_.phase != GemvPhase::COMPUTE) return;

    // READRES goes FIRST. A stream whose MAC has drained is holding
    // banks_per_group finished accumulators and cannot start its next dot
    // until they are read, so draining it promptly is what frees it. Issuing
    // MACs ahead of pending readouts would starve the streams that are
    // furthest along.
    for (uint32_t s = 0; s < (uint32_t)engines_ && budget > 0; s++)
    {
      if (gemv_.readres_todo[s] == 0) continue;

      uint32_t bank = (uint32_t)ini_banks_per_group_ - gemv_.readres_todo[s];
      uint64_t addr = gemv_.readres_base[s] + (uint64_t)bank * bank_stride_;

      if (!WillAcceptTransaction(addr, false)) break;

      // READRES names ONE unit: DecodePIMTransaction folds (rank, bankgroup,
      // bank) into a 5-bit index before nulling addr.bank, so unlike MAC the
      // bank field here is load-bearing. num_macs is unused on this path.
      AddPIMTransaction(MemoryAccessType::READRES, addr, 0, gemv_.cpu_token,
                        false);
      gemv_.readres_todo[s]--;
      gemv_.readres_out[s]++;
      gemv_.readres_pending--;
      gemv_.outstanding++;
      budget--;
    }

    for (uint32_t n = 0; n < budget && !GemvIssueDone(); n++)
    {
      uint64_t addr = GemvCmdAddr(gemv_.next_cmd);
      uint32_t run  = GemvCmdRun(gemv_.next_cmd);
      uint32_t eng  = GemvCmdStream(gemv_.next_cmd);

      // Stream still running its previous MAC: sending another now would be a
      // PREEMPTION, not accumulation (see GemvJob::engine_busy). Wait.
      // Issue stays IN ORDER -- a single issue port that skips ahead would
      // need a full scoreboard, and breadth-first ordering already means the
      // next command is on a different stream, so this rarely blocks until
      // all 32 are in flight.
      if (gemv_.engine_busy & (1ULL << eng))
        break;

      // A MAC is neither a read nor a write, so the controller's accept check
      // falls through to the read-queue test -- passing is_write=false asks
      // exactly the right question.
      if (!WillAcceptTransaction(addr, false))
        break;                                   // no room; retry next cycle

      // comp=0 on EVERY MAC. Completion is decided by counting drains, not by
      // the last MAC issued: the 32 engines drain independently, so issue
      // order is not completion order and the last-issued MAC may finish
      // first. The drain in ClockTick fires pim_callback_ instead.
      AddMACTransaction(addr, run, gemv_.cpu_token, false);
      gemv_.engine_busy |= (1ULL << eng);
      gemv_.outstanding++;
      gemv_.next_cmd++;
    }
  }

  // ===== [CPU PIM (MAC) FLOW · single step] BYPASS =====
  // This is how the CPU drives a PIM op (pim.dispatch lands here).
  // NOTE: it does NOT use request_handler / normal_queue / getNextAccess.
  // It goes STRAIGHT into the DRAM-PIM model (PIMSim::AddTransaction) as a MAC.
  //   NEXT  -> PIMSim::AddTransaction()  (PIMSim.cc) -> JedecDRAMSystem -> PIMController
  // MAC is one of five PIM command types PIMSim understands; the other four
  // were unreachable from gem5 only because this entry point hardcoded MAC.
  // MemoryAccessType (gem5, src/Common.h) and TransactionType (PIMony,
  // PIMSim/src/common.h) are declared member-for-member in the same order, so
  // the int cast below is valid for every type, not just MAC. Verified.
  bool MemorySystem::AddMACTransaction(uint64_t hex_addr, uint32_t num_macs, uint32_t cpu_token,
                                       bool comp)
  {
    return AddPIMTransaction(MemoryAccessType::MAC, hex_addr, num_macs,
                             cpu_token, comp);
  }

  bool MemorySystem::AddPIMTransaction(MemoryAccessType type, uint64_t hex_addr,
                                       uint32_t num_macs, uint32_t cpu_token,
                                       bool comp)
  {
    // Build a real MemoryAccess so the drain phase has a valid object to read.
    // (Previously passed nullptr -> mem_response->req_type dereferenced null
    //  on completion -> segfault.) This struct is the "claim ticket": PIMSim
    // holds it opaquely and hands it back when the MAC completes -- exactly
    // what the normal R/W path does (getNextAccess also `new`s a MemoryAccess).
    //
    // Host token rides in `cpu_token` (NOT `id`: the scheduler overwrites `id`
    // with a tile index at issue, Request.cc:450). cpu_token is never touched
    // internally, so it survives to the completion drain.
    //
    // num_macs is the count the programmer sets in rs2: the number of column-step
    // MACs to sweep (the PIM controller decrements one per tCCD_L). Passed through
    // as-is by design — no byte->element conversion. NOTE: one column step pulls a
    // full burst (elems_per_comp elements), so 1 num_macs != 1 scalar multiply.
    MemoryAccess *req = new MemoryAccess{};
    req->id           = generate_mem_access_id();
    req->dram_address = hex_addr;            // MVP token
    req->req_type     = type;
    req->request      = true;
    // GROUP SCOPING: only a completing MAC is 'last'. A mid-group MAC (comp=0)
    // still executes and is charged timing, but the drain skips pim_done /
    // pim_callback_ entirely (memory_system.cc ~192) -- the accumulator keeps
    // building and the CPU is never interrupted. Was hardcoded true, which made
    // every single MAC announce itself as a finished computation.
    req->pim_last     = comp;
    req->bankgroup    = (uint32_t)-1;        // channel-level -> sets pim_done[ch]
    req->num_macs     = num_macs;
    req->cpu_token    = cpu_token;           // host token; survives to completion drain
    return dram->_mem->AddTransaction(hex_addr, int(type), num_macs, req);
  }

  MemorySystem *GetMemorySystem(const std::string &mem_config, const std::string &model_config,
                                const std::string &log_dir, const std::string &log_level,
                                std::function<void(uint32_t)> pim_callback,
                                std::function<void(uint64_t)> read_callback,
                                std::function<void(uint64_t)> write_callback)
  {
    return new MemorySystem(mem_config, model_config, log_dir, log_level, pim_callback, read_callback, write_callback);
  }

  // This function can be used by autoconf AC_CHECK_LIB since
  // apparently it can't detect C++ functions.
  // Basically just an entry in the symbol table
  extern "C"
  {
    void libdramsim3_is_present(void) { ; }
  }
} // namespace pimony