#include "Request.h"
#include <sstream>
#include <iomanip>
namespace pimony
{
  namespace Request
  {

    TraceRequestHandler::TraceRequestHandler(SimulationConfig config, const std::string &normal_path)
        : num_bankgroups_(config.dram_bankgroups_per_ch),
          trace_generator(config),
          TPOT_slo_(config.TPOT_slo),
          model_name_(config.model_name),
          model_n_layer_(config.model_n_layer)
    {
      std::ifstream file(config.pim_config_path);
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

        if (key == "columns")
          columns_ = std::stoi(value);
        else if (key == "device_width")
          device_width_ = std::stoi(value);
        else if (key == "channels")
          channels_ = std::stoi(value);
        else if (key == "tCK")
          tCK_ = std::stod(value);
        else if (key == "compute_mode")
          if (value == "ASYNC")
            compute_mode_ = ComputeMode::ASYNC;
          else if (value == "ALL_BANK")
            compute_mode_ = ComputeMode::ALL_BANK;
      }

      normal_queue.resize(channels_);
      pim_tiles.resize(channels_);
      current_pim_tile_idx.resize(channels_, 0);
      current_pim_op_idx.resize(channels_, 0);
      pim_tiles_bg_sync.resize(channels_);
      for (size_t ch = 0; ch < channels_; ch++)
      {
        pim_tiles_bg_sync[ch].resize(num_bankgroups_);
      }
      waiting_on_prev_pim_tile.resize(channels_, false);
      waiting_on_prev_pim_tile_bg.resize(channels_);
      for (size_t ch = 0; ch < channels_; ch++)
        waiting_on_prev_pim_tile_bg[ch].resize(num_bankgroups_, false);
      current_pim_tile_idx_bg.resize(channels_);
      for (size_t ch = 0; ch < channels_; ch++)
        current_pim_tile_idx_bg[ch].resize(num_bankgroups_, 0);
      current_pim_op_idx_bg.resize(channels_);
      for (size_t ch = 0; ch < channels_; ch++)
        current_pim_op_idx_bg[ch].resize(num_bankgroups_, 0);
      current_bg_idx.resize(channels_);
      pim_issue_time_map.resize(channels_);

      if (!normal_path.empty())
      {
        load_normal_trace(normal_path);
      }

      // CPU-driven PIM: LLM auto-run disabled. Do not seed the PIM trace.
      // load_pim_trace(trace_generator.getNextTrace());

      cur_gen_start_clk_ = 0;
      slo_violation_flag = false;
      // print_pim_tiles(); // for debugging
    }

    void TraceRequestHandler::reset_pim_layer_state()
    {
      // Clear previous tiles
      for (size_t ch = 0; ch < channels_; ch++)
      {
        pim_tiles[ch].clear();

        pim_tiles_bg_sync[ch].clear();
        pim_tiles_bg_sync[ch].resize(num_bankgroups_);
        // Reset indices

        current_pim_tile_idx[ch] = 0;
        current_pim_op_idx[ch] = 0;

        current_bg_idx[ch] = 0;

        current_pim_tile_idx_bg[ch].assign(num_bankgroups_, 0);
        current_pim_op_idx_bg[ch].assign(num_bankgroups_, 0);

        // Reset wait flags

        waiting_on_prev_pim_tile[ch] = false;
        waiting_on_prev_pim_tile_bg[ch].assign(num_bankgroups_, false);

        // Reset pim_issue_time_map
        pim_issue_time_map[ch].clear();
      }
    }

    // ===== [CPU R/W FLOW · step 2/4] ENQUEUE =====
    // PREV  <- memory_system.cc  AddTransaction()        [step 1/4]
    // Push the CPU request into this channel's normal_queue. It now waits here
    // until the inject loop pulls it out via getNextAccess().
    //   NEXT  -> getNextAccess() normal_queue block      [step 3/4]
    void TraceRequestHandler::AddNormalTransaction(TraceEntry request)
    {
      int ch = get_channel_from_address(request.address);
      normal_queue[ch].push(request);
    }

    bool TraceRequestHandler::is_normal_trace_empty() const
    {
      bool all_empty = true;
      for (size_t ch = 0; ch < channels_; ch++)
      {
        if (!normal_queue[ch].empty())
          all_empty = false;
      }
      return all_empty;
    }

    int TraceRequestHandler::get_channel_from_address(uint64_t address)
    {
      // Undo final left shift from ReverseAddressMapping
      const uint64_t shifted = address >> trace_generator.address.shift_bits;

      // Extract channel field
      const uint64_t channel = (shifted >> trace_generator.address.ch_pos) & static_cast<uint64_t>(trace_generator.address.ch_mask);

      return static_cast<int>(channel);
    }

    bool TraceRequestHandler::is_pim_trace_empty() const
    {
      bool is_all_channels_done = true;
      if (compute_mode_ == ComputeMode::ALL_BANK)
      {

        for (size_t ch = 0; ch < channels_; ch++)
        {
          if (current_pim_tile_idx[ch] < pim_tiles[ch].size())
            is_all_channels_done = false;
        }
        return is_all_channels_done;
      }
      else if (compute_mode_ == ComputeMode::ASYNC)
      {
        for (size_t ch = 0; ch < channels_; ch++)
        {
          for (size_t bg = 0; bg < num_bankgroups_; ++bg)
          {
            if (current_pim_tile_idx_bg[ch][bg] < pim_tiles_bg_sync[ch][bg].size())
            {
              return false; // still work left in this bankgroup
            }
          }
        }
        return true;
      }
      return true; // fallback
    }

    bool TraceRequestHandler::is_pim_operation_done() const
    {
      return is_pim_trace_done() && trace_generator.is_all_model_done();
    }

    bool TraceRequestHandler::is_pim_trace_done() const
    {

      bool all_pim_done = true;
      for (size_t ch = 0; ch < channels_; ch++)
      {
        for (const auto &entry : pim_issue_time_map[ch])
        {
          if (!entry.second.done)
          {
            all_pim_done = false;
            break;
          }
        }
      }
      return all_pim_done && is_pim_trace_empty();
    }

    void TraceRequestHandler::load_normal_trace(const std::string &path)
    {
      std::ifstream infile(path);
      std::string type_str;
      uint64_t cycle;
      std::string addr_str;

      while (infile >> cycle >> type_str >> addr_str)
      {
        TraceEntry entry;
        entry.issue_cycle = cycle;
        entry.type = parse_access_type(type_str);
        entry.address = std::stoull(addr_str, nullptr, 16);

        int ch = get_channel_from_address(entry.address);
        normal_queue[ch].push(entry);
      }
    }

    void TraceRequestHandler::print_pim_tiles() // for debugging
    {
      for (size_t ch = 0; ch < channels_; ch++)
      {
        std::cout << "(Request.cc) channel :" << ch << std::endl;
        std::cout << "(Request.cc) pim_tiles" << std::endl;
        for (size_t i = 0; i < pim_tiles[ch].size(); ++i)
        {
          std::cout << "  Tile " << i << ":\n";
          for (const auto &entry : pim_tiles[ch][i])
          {
            std::cout << "    type=" << memAccessTypeString(entry.type)
                      << ", addr=0x" << std::hex << entry.address
                      << ", cycle=" << std::dec << entry.issue_cycle << std::endl;
          }
        }
        if (compute_mode_ == ComputeMode::ASYNC)
        {
          std::cout << "(Request.cc) pim_tiles_bh_sync" << std::endl;
          for (size_t bg = 0; bg < pim_tiles_bg_sync[ch].size(); ++bg)
          {
            std::cout << "  BankGroup " << bg << ":\n";
            for (size_t t = 0; t < pim_tiles_bg_sync[ch][bg].size(); ++t)
            {
              std::cout << "    Tile " << t << ":\n";
              for (const auto &entry : pim_tiles_bg_sync[ch][bg][t])
              {
                std::cout << "      type=" << memAccessTypeString(entry.type)
                          << ", addr=0x" << std::hex << entry.address
                          << ", cycle=" << std::dec << entry.issue_cycle << std::endl;
              }
            }
          }
        }
      }
    }

    void TraceRequestHandler::load_pim_trace(const std::vector<TraceEntry> &trace_entries)
    {
      std::vector<MemoryAccessType> past_type;
      std::vector<std::vector<TraceEntry>> tile;
      past_type.resize(channels_);
      tile.resize(channels_);

      if (compute_mode_ == ComputeMode::ALL_BANK)
      {
        for (const auto &entry : trace_entries)
        {
          size_t ch = get_channel_from_address(entry.address);
          if (entry.type == MemoryAccessType::H2GWRITE || past_type[ch] == MemoryAccessType::READRES)
          {
            if (!tile[ch].empty())
            {
              pim_tiles[ch].push_back(tile[ch]);
              tile[ch].clear();
            }
          }

          tile[ch].push_back(entry);
          past_type[ch] = entry.type;
        }

        for (size_t ch = 0; ch < channels_; ch++)
        {
          if (!tile[ch].empty())
          {
            pim_tiles[ch].push_back(tile[ch]);
            tile[ch].clear();
          }
        }
      }
      else if (compute_mode_ == ComputeMode::ASYNC)
      {
        for (const auto &entry : trace_entries)
        {
          size_t ch = get_channel_from_address(entry.address);
          if (entry.type == MemoryAccessType::H2GWRITE)
          {
            if (!tile[ch].empty())
            {
              pim_tiles_bg_sync[ch][current_bg_idx[ch]].push_back(tile[ch]);
              tile[ch].clear();
            }

            tile[ch].push_back(entry);
            pim_tiles[ch].push_back(tile[ch]);
            tile[ch].clear();

            // SYNC to all bankgroups after global buffer write
            for (size_t bg = 0; bg < num_bankgroups_; ++bg)
            {
              TraceEntry sync_entry;
              sync_entry.issue_cycle = 0;
              sync_entry.type = MemoryAccessType::SYNC;
              sync_entry.address = entry.address;
              sync_entry.num_macs = 0;
              pim_tiles_bg_sync[ch][bg].push_back({sync_entry});
            }
            continue;
          }

          if (entry.type == MemoryAccessType::H2GWRITE || past_type[ch] == MemoryAccessType::READRES)
          {
            if (!tile[ch].empty())
            {
              pim_tiles_bg_sync[ch][current_bg_idx[ch]].push_back(tile[ch]);
            }

            if (current_bg_idx[ch] == num_bankgroups_ - 1)
              current_bg_idx[ch] = 0;
            else
              current_bg_idx[ch]++;

            tile[ch].clear();
          }

          tile[ch].push_back(entry);
          past_type[ch] = entry.type;
        }
        for (size_t ch = 0; ch < channels_; ch++)
        {
          if (!tile.empty())
          {
            pim_tiles_bg_sync[ch][current_bg_idx[ch]].push_back(tile[ch]);
            tile[ch].clear();
          }
        }
      }
    }

    MemoryAccessType TraceRequestHandler::parse_access_type(const std::string &str) const
    {
      if (str == "READ")
        return MemoryAccessType::READ;
      if (str == "WRITE")
        return MemoryAccessType::WRITE;
      if (str == "H2GWRITE")
        return MemoryAccessType::H2GWRITE;
      if (str == "GWRITE")
        return MemoryAccessType::D2GWRITE;
      if (str == "COMP")
        return MemoryAccessType::COMP;
      if (str == "MAC")
        return MemoryAccessType::MAC;
      if (str == "SYNC")
        return MemoryAccessType::SYNC;
      if (str == "READRES")
        return MemoryAccessType::READRES;
    }

    MemoryAccess *TraceRequestHandler::getNextAccess(int core_id, cycle_type cur_cycle, bool pim_tile_done, std::vector<bool> pim_tile_done_bg)
    {
      // ----------------------------------------------------------------------
      // CPU-DRIVEN PIM MODE: LLM auto-run is disabled.
      //   - The PIM trace seed (constructor) and regenerate (below) are
      //     commented out, so pim_tiles / pim_tiles_bg_sync stay empty forever.
      //   - As a result, every PIM-injection branch in this function is
      //     UNREACHABLE (is_pim_trace_empty() is always true).
      //   - The SLO check below still sets slo_violation_flag, but its consumer
      //     in MemorySystem::ClockTick is also commented out, so it is inert.
      //   - The ONLY live path is the normal_queue serving block at the bottom
      //     (CPU reads/writes added via AddNormalTransaction).
      //   - CPU PIM ops (MAC) bypass this function entirely via AddMACTransaction.
      // To restore the LLM workload: re-enable the two load_pim_trace() calls.
      // ----------------------------------------------------------------------

      // TPOT SLO viololation check
      if (trace_generator.token_change_flag)
      {
        cur_gen_start_clk_ = cur_cycle;
        trace_generator.token_change_flag = false;
      }
      // 1GHz * TPOT_SLO (ms) / 1000 (ms) / layers / tCK (ns)
      if (endsWith(model_name_, "single_layer"))
      {
        if (cur_cycle - cur_gen_start_clk_ > 1000000000 * TPOT_slo_ / 1000 / model_n_layer_ / tCK_)
        {
          slo_violation_flag = true;
        }
      }
      else
      {
        if (cur_cycle - cur_gen_start_clk_ > 1000000000 * TPOT_slo_ / 1000 / tCK_)
        {
          slo_violation_flag = true;
        }
      }

      MemoryAccess *access = new MemoryAccess{};
      access->request = false;

      // CPU-driven PIM: LLM auto-run disabled. Do not regenerate the PIM trace.
      // if (is_pim_trace_done())
      // {
      //   reset_pim_layer_state();
      //   std::vector<TraceEntry> trace = trace_generator.getNextTrace();
      //   if (trace.empty())
      //   {
      //     return access;
      //   }
      //   else
      //   {
      //     load_pim_trace(trace);
      //   }
      //   // print_pim_tiles(); // for debugging
      // }

      if (!is_pim_trace_empty())
      {
        if (compute_mode_ == ComputeMode::ALL_BANK)
        {
          if (pim_tile_done)
          {
            waiting_on_prev_pim_tile[core_id] = false;
          }

          // Issue PIM tile operations only if previous tile is done

          if (!waiting_on_prev_pim_tile[core_id] && current_pim_tile_idx[core_id] < pim_tiles[core_id].size())
          {

            auto &tile = pim_tiles[core_id][current_pim_tile_idx[core_id]];
            if (current_pim_op_idx[core_id] < tile.size())
            {
              const TraceEntry &entry = tile[current_pim_op_idx[core_id]];
              access->id = current_pim_tile_idx[core_id];
              access->req_type = entry.type;
              access->dram_address = entry.address;
              access->request = true;
              access->core_id = core_id;
              access->start_cycle = cur_cycle;
              access->bankgroup = -1;
              access->num_macs = entry.num_macs;

              if (current_pim_op_idx[core_id] == 0)
              {
                pim_issue_time_map[core_id][access->id] = {access->start_cycle, false}; // log first pim transaction
              }
              current_pim_op_idx[core_id]++;
              if (current_pim_op_idx[core_id] == tile.size())
              {
                current_pim_tile_idx[core_id]++;
                current_pim_op_idx[core_id] = 0;
                waiting_on_prev_pim_tile[core_id] = true;
                access->pim_last = true;
              }
              return access;
            }
          }
        }
        else if (compute_mode_ == ComputeMode::ASYNC)
        {
          for (size_t bg = 0; bg < num_bankgroups_; ++bg)
          {
            if (pim_tile_done_bg[bg])
            {
              waiting_on_prev_pim_tile_bg[core_id][bg] = false;
            }
          }

          // Track the slowest-progressing bankgroup
          int selected_bg = -1;
          size_t min_tile_progress = SIZE_MAX;
          size_t min_op_progress = SIZE_MAX;

          // Step 1: SYNC check across all bankgroups
          bool all_waiting_at_sync = true;
          for (size_t bg = 0; bg < num_bankgroups_; ++bg)
          {
            if (waiting_on_prev_pim_tile_bg[core_id][bg])
            {
              all_waiting_at_sync = false;
              break;
            }

            auto &tiles = pim_tiles_bg_sync[core_id][bg];
            if (current_pim_tile_idx_bg[core_id][bg] >= tiles.size())
              continue;
            auto &tile = tiles[current_pim_tile_idx_bg[core_id][bg]];
            if (current_pim_op_idx_bg[core_id][bg] < tile.size())
            {
              if (tile[current_pim_op_idx_bg[core_id][bg]].type == MemoryAccessType::SYNC)
              {
                continue; // SYNC encountered
              }
            }
            all_waiting_at_sync = false;
            break;
          }

          if (all_waiting_at_sync)
          {
            // Clear SYNC ops from all bankgroups
            for (size_t bg = 0; bg < num_bankgroups_; ++bg)
            {
              auto &tiles = pim_tiles_bg_sync[core_id][bg];
              if (current_pim_tile_idx_bg[core_id][bg] >= tiles.size())
                continue;

              auto &tile = tiles[current_pim_tile_idx_bg[core_id][bg]];
              if (current_pim_op_idx_bg[core_id][bg] < tile.size() &&
                  tile[current_pim_op_idx_bg[core_id][bg]].type == MemoryAccessType::SYNC)
              {
                current_pim_tile_idx_bg[core_id][bg]++;
                current_pim_op_idx_bg[core_id][bg] = 0;
              }
            }

            // After SYNC barrier, unlock global tile
            if (all_waiting_at_sync && current_pim_tile_idx[core_id] < pim_tiles[core_id].size())
            {
              auto &tile = pim_tiles[core_id][current_pim_tile_idx[core_id]];
              if (current_pim_op_idx[core_id] < tile.size())
              {
                const TraceEntry &entry = tile[current_pim_op_idx[core_id]];
                access->id = current_pim_tile_idx[core_id];
                access->req_type = entry.type;
                access->dram_address = entry.address;
                access->request = true;
                access->core_id = core_id;
                access->start_cycle = cur_cycle;
                access->bankgroup = -1;
                access->num_macs = entry.num_macs;

                current_pim_op_idx[core_id]++;
                if (current_pim_op_idx[core_id] == tile.size())
                {
                  current_pim_tile_idx[core_id]++;
                  current_pim_op_idx[core_id] = 0;
                  waiting_on_prev_pim_tile[core_id] = true;
                  access->pim_last = true;
                }
                return access;
              }
            }

            // Return if SYNC just cleared, don't proceed to issuing others this cycle
            return access;
          }

          // Step 2: Select slowest-progressing bg to issue
          for (size_t bg = 0; bg < num_bankgroups_; ++bg)
          {
            if (waiting_on_prev_pim_tile_bg[core_id][bg])
              continue;

            auto &tiles = pim_tiles_bg_sync[core_id][bg];
            if (current_pim_tile_idx_bg[core_id][bg] >= tiles.size())
              continue;

            if (tiles[current_pim_tile_idx_bg[core_id][bg]][current_pim_op_idx_bg[core_id][bg]].type == MemoryAccessType::SYNC)
              continue;

            auto &tile = tiles[current_pim_tile_idx_bg[core_id][bg]];
            if (current_pim_op_idx_bg[core_id][bg] >= tile.size())
              continue;

            const TraceEntry &entry = tile[current_pim_op_idx_bg[core_id][bg]];

            // Prefer slowest bg
            if (current_pim_tile_idx_bg[core_id][bg] < min_tile_progress ||
                (current_pim_tile_idx_bg[core_id][bg] == min_tile_progress && current_pim_op_idx_bg[core_id][bg] < min_op_progress))
            {
              selected_bg = bg;
              min_tile_progress = current_pim_tile_idx_bg[core_id][bg];
              min_op_progress = current_pim_op_idx_bg[core_id][bg];
            }
          }

          // Step 3: Issue request from selected bg
          if (selected_bg != -1)
          {
            auto &tile = pim_tiles_bg_sync[core_id][selected_bg][current_pim_tile_idx_bg[core_id][selected_bg]];
            const TraceEntry &entry = tile[current_pim_op_idx_bg[core_id][selected_bg]];

            access->id = generate_pim_bg_access_id(selected_bg, current_pim_tile_idx_bg[core_id][selected_bg]);
            access->req_type = entry.type;
            access->dram_address = entry.address;
            access->request = true;
            access->core_id = core_id;
            access->start_cycle = cur_cycle;
            access->bankgroup = selected_bg;
            access->num_macs = entry.num_macs;

            if (current_pim_op_idx_bg[core_id][selected_bg] == 0)
            {
              pim_issue_time_map[core_id][access->id] = {access->start_cycle, false};
            }

            current_pim_op_idx_bg[core_id][selected_bg]++;
            if (current_pim_op_idx_bg[core_id][selected_bg] == tile.size())
            {
              current_pim_tile_idx_bg[core_id][selected_bg]++;
              current_pim_op_idx_bg[core_id][selected_bg] = 0;
              waiting_on_prev_pim_tile_bg[core_id][selected_bg] = true;
              access->pim_last = true;
            }
            return access;
          }
        }
      }

      // ===== [CPU R/W FLOW · step 3/4] DEQUEUE (the only live path) =====
      // PREV  <- AddNormalTransaction() filled normal_queue   [step 2/4]
      // If a CPU request is queued for this channel AND its issue_cycle has
      // arrived, copy it into `access`, pop it, and return it.
      //   NEXT  -> memory_system.cc ClockTick() inject loop -> dram->push()  [step 4/4]
      // Check if normal access is ready
      if (!normal_queue[core_id].empty())
      {
        TraceEntry &entry = normal_queue[core_id].front();
        if (cur_cycle >= entry.issue_cycle) // add dep check function
        {
          access->id = generate_mem_access_id();
          access->req_type = entry.type;
          access->dram_address = entry.address;
          access->request = true;
          access->core_id = core_id;
          access->start_cycle = cur_cycle;

          normal_issue_time_map[access->id] = entry.issue_cycle; // log start cycle
          normal_queue[core_id].pop();
          return access;
        }
      }
      return access;
    }

    void TraceRequestHandler::update_latency(int core_id, cycle_type cur_cycle, bool read_done,
                                             bool write_done,
                                             bool pim_tile_done, uint32_t id)
    {
      if (read_done)
      {
        read_latency_sum += (cur_cycle - normal_issue_time_map[id]);
        read_count++;
        normal_issue_time_map.erase(id);
      }
      if (write_done)
      {
        write_latency_sum += (cur_cycle - normal_issue_time_map[id]);
        write_count++;
        normal_issue_time_map.erase(id);
      }
      if (pim_tile_done)
      {
        pim_issue_time_map[core_id][id].done = true;
        pim_latency_sum += (cur_cycle - pim_issue_time_map[core_id][id].issue_cycle);
        pim_count++;
        last_done_clk_ = cur_cycle;
      }
    }

    void TraceRequestHandler::print_state() const
    {
      auto avg = [](uint64_t sum, uint64_t cnt)
      {
        return (cnt == 0) ? 0.0 : static_cast<double>(sum) / cnt;
      };
      spdlog::info("Avg read latency = {:.2f} cycles, Avg write latency = {:.2f} cycles, PIM Tile = {:.2f} cycles",
                   avg(read_latency_sum, read_count), avg(write_latency_sum, write_count), avg(pim_latency_sum, pim_count));
      spdlog::info("GEMV done latency = {} cycles", last_done_clk_);
    }

  } // namespace Request
} // namespace pimony