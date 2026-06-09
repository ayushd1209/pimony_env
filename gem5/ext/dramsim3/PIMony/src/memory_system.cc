#include "memory_system.h"

namespace pimony
{
  MemorySystem::MemorySystem(const std::string &mem_config, const std::string &model_config,
                             const std::string &log_dir, const std::string &log_level,
                             std::function<void()> pim_callback,
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
    if (request_handler->slo_violation_flag)
    {
      std::cout << "(memory_system) SLO violation" << std::endl;
      // request_handler->print_state();
      pim_callback_();
    }

    if (request_handler->is_pim_operation_done())
    {
      std::cout << "(memory_system) all PIM operation is done" << std::endl;
      request_handler->print_state();
      // dram->print_stat();
      pim_callback_();
    }

    for (int ch = 0; ch < channels; ch++)
    {
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

    dram->cycle();

    for (int ch = 0; ch < channels; ch++)
    {
      while (!dram->is_empty(ch))
      {
        mem_response = dram->top(ch);
        if (mem_response->req_type == MemoryAccessType::READ)
        {
          request_handler->update_latency(ch, clk_, true, false, false, mem_response->id);
          if (read_remain_[read_sub2orig_[mem_response->dram_address]] == 2)
          {
            read_remain_[read_sub2orig_[mem_response->dram_address]]--;
            read_sub2orig_.erase(mem_response->dram_address);
          }
          else
          {
            read_remain_.erase(read_sub2orig_[mem_response->dram_address]);
            read_callback_(read_sub2orig_[mem_response->dram_address]);
            read_sub2orig_.erase(mem_response->dram_address);
          }
        }
        else if (mem_response->req_type == MemoryAccessType::WRITE)
        {
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
          }
        }
        delete mem_response;
        dram->pop(ch);
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

  bool MemorySystem::AddMACTransaction(uint64_t hex_addr, uint32_t num_macs)
  {
    return dram->_mem->AddTransaction(hex_addr,
        int(MemoryAccessType::MAC), num_macs, nullptr);
  }

  MemorySystem *GetMemorySystem(const std::string &mem_config, const std::string &model_config,
                                const std::string &log_dir, const std::string &log_level,
                                std::function<void()> pim_callback,
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