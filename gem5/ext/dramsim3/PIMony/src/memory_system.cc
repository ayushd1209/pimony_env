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
    }

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

  // ===== [CPU PIM (MAC) FLOW · single step] BYPASS =====
  // This is how the CPU drives a PIM op (pim.dispatch lands here).
  // NOTE: it does NOT use request_handler / normal_queue / getNextAccess.
  // It goes STRAIGHT into the DRAM-PIM model (PIMSim::AddTransaction) as a MAC.
  //   NEXT  -> PIMSim::AddTransaction()  (PIMSim.cc) -> JedecDRAMSystem -> PIMController
  bool MemorySystem::AddMACTransaction(uint64_t hex_addr, uint32_t num_macs, uint32_t cpu_token)
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
    req->req_type     = MemoryAccessType::MAC;
    req->request      = true;
    req->pim_last     = true;                // drain treats this as a completion
    req->bankgroup    = (uint32_t)-1;        // channel-level -> sets pim_done[ch]
    req->num_macs     = num_macs;
    req->cpu_token    = cpu_token;           // host token; survives to completion drain
    return dram->_mem->AddTransaction(hex_addr,
        int(MemoryAccessType::MAC), num_macs, req);
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