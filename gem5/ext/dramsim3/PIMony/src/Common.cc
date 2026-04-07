#include "Common.h"
namespace pimony
{
  uint32_t generate_id()
  {
    static uint32_t id_counter{0};
    return id_counter++;
  }

  uint32_t generate_mem_access_id()
  {
    static uint32_t id_counter{0};
    return id_counter++;
  }

  uint32_t generate_pim_bg_access_id(int bankgroup, int tile_index)
  {
    return (static_cast<uint32_t>(bankgroup) << 16) | (tile_index & 0xFFFF);
  }

  uint32_t generate_pim_id()
  {
    static uint32_t id_counter{0};
    return id_counter++;
  }
  namespace AddressConfig
  {
    addr_type alignment = Config::global_config.dram_req_size; // BL * dev width / 8 bytes
    addr_type channel_mask;                                    // not used
    addr_type channel_offset;                                  // not used
  } // namespace AddressConfig

  int MemoryAccess::req_count = 0;
  int MemoryAccess::pre_req_count = 0;

  uint32_t AddressConfig::mask_channel(addr_type address)
  {
    const int col_bits = 4;
    const int offset = 6;

    int ch = (address >> (col_bits + offset)) & channel_mask;
    return ch;
  }

  addr_type AddressConfig::switch_co_ch(addr_type addr)
  {
    const int num_col_bits = 4;
    const int num_ch_bits = 5;
    const int num_offset = 6;

    const addr_type ch_mask = ((1 << num_ch_bits) - 1) << (num_col_bits + num_offset);
    const addr_type col_mask = ((1 << num_col_bits) - 1) << num_offset;

    const addr_type mask = ch_mask | col_mask;

    addr_type new_col_bits = (addr & (col_mask << num_ch_bits)) >> num_ch_bits;
    addr_type new_ch_bits = (addr & (ch_mask >> num_col_bits)) << num_col_bits;

    addr = addr & (~mask);
    addr = addr | new_col_bits;
    addr = addr | new_ch_bits;

    return addr;
  }

  // used in NPU-only
  // this is creating dram address.
  // align cachline size to 4B
  // ex) allocate 31 bytes => align to 32 bytes
  addr_type AddressConfig::allocate_address(uint32_t size)
  {
    static addr_type base_addr{0};

    addr_type result = base_addr;
    base_addr += size;
    if (base_addr & (alignment - 1))
    {
      base_addr += alignment - (base_addr & (alignment - 1));
    }

    return result;
  }

  addr_type AddressConfig::align(addr_type addr)
  {
    addr_type aligned_addr = addr - (addr & (alignment - 1));
    // spdlog::info("align address!! {}", fmt::format("{:#X} to {:#X}", addr, aligned_addr));

    return aligned_addr;
  }

  void PrintColor(Color color, std::string str)
  {
    return;
    std::string color_code;
    switch (color)
    {
    case Color::RED:
      color_code = "\033[1;31m";
      break;
    case Color::GREEN:
      color_code = "\033[1;32m";
      break;
    case Color::YELLOW:
      color_code = "\033[1;33m";
      break;
    case Color::BLUE:
      color_code = "\033[1;34m";
      break;
    case Color::MAGENTA:
      color_code = "\033[1;35m";
      break;
    case Color::CYAN:
      color_code = "\033[1;36m";
      break;
    default:
      color_code = "";
    }
    std::cout << color_code << str << "\033[0m" << std::endl;
  }

  SimulationConfig Config::global_config;

  void initialize_memory_config(std::string mem_config_path)
  {
    json mem_config = load_config(mem_config_path);

    Config::global_config.dram_channels = mem_config["dram_channels"];
    if (mem_config.contains("dram_req_size"))
      Config::global_config.dram_req_size = mem_config["dram_req_size"];

    /* PIM config */
    if (mem_config.contains("pim_config_path"))
    {
      Config::global_config.pim_config_path = mem_config["pim_config_path"];
      // DRAM row buffer size (in bytes)
      Config::global_config.dram_page_size = mem_config["dram_page_size"];
      Config::global_config.dram_banks_per_ch = mem_config["dram_banks_per_ch"];
      Config::global_config.dram_bankgroups_per_ch = mem_config["dram_bankgroups_per_ch"];

      // # params per PIM_COMP command
      Config::global_config.fixed_columns_ = mem_config["fixed_columns_"];

      Config::global_config.row_major = (mem_config["row_major"] == "true") ? true : false;
    }

    // Config::global_config.HBM_size = (uint64_t)(mem_config["HBM_size"])GB;
    // Config::global_config.HBM_act_buf_size = (uint64_t)(mem_config["HBM_act_buf_size"])MB;
  }

  void initialize_model_config(std::string model_config_path)
  {
    json model_config = load_config(model_config_path);
    /* GPT configs */
    Config::global_config.model_name = model_config["model_name"];
    Config::global_config.model_params_b = model_config["model_params_b"];
    Config::global_config.model_vocab_size = model_config["model_vocab_size"];
    Config::global_config.model_n_layer = model_config["model_n_layer"];
    Config::global_config.model_n_head = model_config["model_n_head"];
    Config::global_config.model_d_model = model_config["model_d_model"];
    Config::global_config.model_d_interm = model_config["model_d_interm"];
    Config::global_config.model_d_head = model_config["model_d_head"];
    Config::global_config.precision = model_config["precision"];
    Config::global_config.input_len = model_config["input_len"];
    Config::global_config.output_len = model_config["output_len"];
    Config::global_config.TPOT_slo = model_config["TPOT_slo"];
  }

  void initialize_client_config(std::string cli_config_path)
  {
    Config::global_config.request_dataset_path = cli_config_path;

    // json cli_config = load_config(cli_config_path);
    // /* Client config */
    // Config::global_config.request_dataset_path = cli_config["request_dataset_path"];
    // Config::global_config.request_input_seq_len = cli_config["request_input_seq_len"];
    // Config::global_config.request_interval = cli_config["request_interval"];
    // Config::global_config.request_total_cnt = cli_config["request_total_cnt"];
  }

  json load_config(std::string config_path)
  {
    json config_json;
    std::ifstream config_file(config_path);
    config_file >> config_json;
    config_file.close();
    return config_json;
  }

  std::string memAccessTypeString(MemoryAccessType type)
  {
    switch (type)
    {
    case (MemoryAccessType::READ):
      return "READ";
    case (MemoryAccessType::WRITE):
      return "WRITE";
    case (MemoryAccessType::D2GWRITE):
      return "D2GWRITE";
    case (MemoryAccessType::H2GWRITE):
      return "H2GWRITE";
    case (MemoryAccessType::COMP):
      return "COMP";
    case (MemoryAccessType::MAC):
      return "MAC";
    case (MemoryAccessType::SYNC):
      return "SYNC";
    case (MemoryAccessType::READRES):
      return "READRES";
    default:
      assert(0);
    }
    return "Unknown";
  }

  std::string opcodeTypeString(Opcode opcode)
  {
    switch (opcode)
    {
    case (Opcode::MOVIN):
      return "MOVIN";
    case (Opcode::MOVOUT):
      return "MOVOUT";
    case (Opcode::PIM_HEADER):
      return "PIM_HEADER";
    case (Opcode::PIM_COMP):
      return "PIM_COMP";
    case (Opcode::PIM_MAC):
      return "PIM_MAC";
    case (Opcode::PIM_D2GWRITE):
      return "PIM_GWRITE_D2G";
    case (Opcode::PIM_H2GWRITE):
      return "PIM_GWRITE_H2G";

    default:
      return "Unknown";
    }
  }

  std::string to_hex(uint32_t input)
  {
    std::stringstream addr_as_hex;
    addr_as_hex << std::hex << input;
    return addr_as_hex.str();
  }

  void print_backtrace()
  {
    void *array[10];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // print out all the frames to stderr
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    std::raise(SIGINT);
    exit(1);
  }

  void ast(bool cond)
  {
    if (cond)
      return;

    print_backtrace();
    spdlog::error("assertion failed");
    std::raise(SIGSEGV);
    // exit(-1);
  }

  MemoryAccess *TransToMemoryAccess(Opcode opcode, uint32_t size, addr_type dram_addr, uint32_t core_id,
                                    cycle_type start_cycle)
  {
    MemoryAccessType req_type;
    switch (opcode)
    {
    case Opcode::PIM_D2GWRITE:
      req_type = MemoryAccessType::D2GWRITE;
      break;
    case Opcode::PIM_H2GWRITE:
      req_type = MemoryAccessType::H2GWRITE;
      break;
    case Opcode::PIM_COMP:
      req_type = MemoryAccessType::COMP;
      break;
    case Opcode::PIM_MAC:
      req_type = MemoryAccessType::MAC;
      break;
    case Opcode::PIM_READRES:
      req_type = MemoryAccessType::READRES;
      break;
    case Opcode::MOVIN:
      req_type = MemoryAccessType::READ;
      break;
    case Opcode::MOVOUT:
      req_type = MemoryAccessType::WRITE;
      break;
    default:
      req_type = MemoryAccessType::SIZE;
      spdlog::error("Fail to translate unknown Instruction to MemoryAccessType");
      break;
    }

    MemoryAccess *mem_request = new MemoryAccess{
        .id = generate_mem_access_id(),
        .dram_address = dram_addr,
        .req_type = req_type, //
        .request = true,
        .core_id = core_id,
        .start_cycle = start_cycle};
    return mem_request;
  }

  int LogBase2(int power_of_two)
  {
    int i = 0;
    while (power_of_two > 1)
    {
      power_of_two /= 2;
      i++;
    }
    return i;
  }

  // used for sub-batch interleaving
  std::string stageToString(Stage stage)
  {
    static const std::map<Stage, std::string> stageMap = {
        {Stage::A, "A"},
        {Stage::B, "B"},
        {Stage::C, "C"},
        {Stage::D, "D"},
        {Stage::E, "E"},
        {Stage::F, "F"},
        {Stage::Finish, "Finish"},
    };

    auto it = stageMap.find(stage);
    return (it != stageMap.end()) ? it->second : "unknown";
  }

  std::string stagePlatformToString(StagePlatform sp)
  {
    static const std::map<StagePlatform, std::string> spMap = {
        {StagePlatform::SA, "SA"},
        {StagePlatform::PIM, "PIM"},
    };

    auto it = spMap.find(sp);
    return (it != spMap.end()) ? it->second : "unknown";
  }

  bool startsWith(const std::string &str, const std::string &prefix)
  {
    return str.size() >= prefix.size() &&
           str.compare(0, prefix.size(), prefix) == 0;
  }

  bool endsWith(const std::string &str, const std::string &suffix)
  {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

} // namespace pimony