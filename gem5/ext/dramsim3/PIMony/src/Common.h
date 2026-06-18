#pragma once

#include <robin_hood.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#undef NDEBUG
// #include <robin_hood.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// For backtrace
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>

#include <csignal>
#include <fstream>

#include "SimulationConfig.h"
#include "Stat.h"
#include "helper/HelperFunctions.h"
#include "json.hpp"
namespace pimony
{
#define SPAD_BASE 0x10000000
#define ACCUM_SPAD_BASE 0x20000000
#define GARBAGE_ADDR 0xFFFFFFFFFFFFFFF
#define KB *1024

#define PAGE_SIZE 4096

#define ADDR_ALIGN 256

  using json = nlohmann::json;
  template <typename T>
  using Ptr = std::shared_ptr<T>;

  typedef uint64_t addr_type;
  typedef uint64_t cycle_type;

  namespace AddressConfig
  {
    extern addr_type alignment;
    extern addr_type channel_mask;
    extern addr_type channel_offset;

    uint32_t mask_channel(addr_type address);
    addr_type allocate_address(uint32_t size);
    addr_type align(addr_type addr);

    // uint64_t make_address(int channel, int rank, int bankgroup, int bank, int row, int col);
    uint64_t encode_pim_header(int channel, int row, bool for_gwrite, int num_comps, int num_readres);
    uint64_t encode_pim_comps_readres(int ch, int row, int num_comps, bool last_cmd);

    addr_type switch_co_ch(addr_type addr);
  } // namespace AddressConfig

  enum class MemoryAccessType
  {
    READ,
    WRITE,
    D2GWRITE,
    H2GWRITE,
    COMP,
    MAC,
    READRES,
    SIZE,
    SYNC,
  };

  struct TraceEntry
  {
    MemoryAccessType type;
    cycle_type issue_cycle; // used only for normal access
    addr_type address;
    uint32_t num_macs; // used only for PIM(MAC and GWRITE)
  };

  enum class Color
  {
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    DEFAULT
  };

  enum class Opcode
  {
    MOVIN,
    MOVOUT,
    COMP,
    PIM_HEADER,
    PIM_D2GWRITE,
    PIM_H2GWRITE,
    PIM_COMP,
    PIM_READRES,
    PIM_MAC,
    DUMMY,
    SIZE
  };

  const std::unordered_map<std::string, uint32_t> PRECISION_BITS = {
      {"FP16", 16}, {"INT8", 8}, {"FP32", 32}, {"INT4", 4}};

  enum class StagePlatform;

  std::string memAccessTypeString(MemoryAccessType type);

  typedef struct MemoryAccess
  {
    static int req_count;
    static int pre_req_count;

    uint32_t id;
    addr_type dram_address;
    MemoryAccessType req_type;
    bool request;
    uint32_t core_id;
    cycle_type start_cycle;
    cycle_type dram_enter_cycle;
    cycle_type dram_finish_cycle;
    bool pim_last = false;
    uint32_t bankgroup;
    uint32_t num_macs = 0;
    uint32_t cpu_token = 0;  // host MAC token; rides through untouched, returned on completion

    static void log_count()
    {
      spdlog::info("total pre req count {} / memory request count {}", pre_req_count, req_count);
    }

  } MemoryAccess;

  uint32_t generate_id();
  uint32_t generate_mem_access_id();
  uint32_t generate_pim_bg_access_id(int bankgroup, int tile_index);
  uint32_t generate_pim_id();
  json load_config(std::string config_path);
  void initialize_memory_config(std::string mem_config_path);
  void initialize_client_config(std::string cli_config_path);
  void initialize_model_config(std::string model_config_path);
  void initialize_system_config(std::string sys_config_path);

  std::string to_hex(uint32_t input);
  template <typename... Args>
  std::string name_gen(Args... args)
  {
    std::vector<std::string> strs = {args...};
    assert(!strs.empty());
    std::string ret = "";
    for (auto &str : strs)
    {
      ret += str + ".";
    }
    ret.resize(ret.size() - 1);
    return ret;
  }

  class BTensor;
  typedef struct
  {
    // client to scheduler.
    uint32_t id;
    uint32_t arrival_cycle;   // time spend on client == arrival time to scheduler
    uint32_t completed_cycle; // return time to client

    // request demand
    uint32_t input_size;  // input sequence length
    uint32_t output_size; // # tokens to generate

    // request status
    bool is_initiated;  // whether initialization phase is done
    uint32_t generated; // # tokens generated
    // mapped channel
    int channel;

    std::vector<Ptr<BTensor>> K_cache;
    std::vector<Ptr<BTensor>> V_cache;

  } InferRequest;

  void print_backtrace();
  void ast(bool cond);
  template <typename T>
  std::vector<T> slice(std::vector<T> &inp, int start, int end)
  {
    if (end <= -1)
      end = inp.size() + (end + 1);
    return std::vector<T>(inp.begin() + start, inp.begin() + end);
  }

  template <typename T>
  class Singleton
  {
  protected:
    static T *instance;

  public:
    static T *GetInstance()
    {
      if (instance == nullptr)
        instance = new T();

      return instance;
    }
    static void Delete() { delete instance; }
  };
  template <typename T>
  T *Singleton<T>::instance = nullptr;

  MemoryAccess *TransToMemoryAccess(Opcode, uint32_t size, addr_type dram_addr, uint32_t core_id,
                                    cycle_type start_cycle);
  int LogBase2(int power_of_two);

  // for Sub-batch interleaving
  enum class Stage
  {
    A,
    B,
    C,
    D,
    E,
    F,
    Finish
  };
  enum class StagePlatform
  {
    SA,
    PIM,
    SIZE
  };
  std::string stageToString(Stage stage);
  std::string stagePlatformToString(StagePlatform sp);
  bool startsWith(const std::string &str, const std::string &prefix);
  bool endsWith(const std::string &str, const std::string &suffix);
} // namespace pimony