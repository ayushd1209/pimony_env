#pragma once

#include <json.hpp>
#include <string>
namespace pimony
{

  using json = nlohmann::json;

  typedef uint64_t cycle_type;

  enum class CoreType
  {
    SYSTOLIC_OS,
    SYSTOLIC_WS
  };

  enum class ComputeMode
  {
    ALL_BANK,
    ASYNC
  };

  struct SimulationConfig
  {
    // gpt model config
    std::string model_name;
    float_t model_params_b;
    uint32_t model_block_size;
    uint32_t model_vocab_size;
    uint32_t model_n_layer;
    uint32_t model_n_head;
    uint32_t model_d_model;
    uint32_t model_d_interm;
    uint32_t model_d_head;
    std::string precision;
    uint32_t input_len;
    uint32_t output_len;
    uint32_t TPOT_slo;

    /* Core config */
    uint32_t num_cores;
    CoreType core_type;
    uint32_t core_freq;
    uint32_t core_width;
    uint32_t core_height;

    uint32_t n_tp;

    uint32_t vector_core_count;
    uint32_t vector_core_width;

    /* DRAM config */
    uint32_t dram_channels;
    uint32_t dram_req_size;

    /* PIM config */
    std::string pim_config_path;
    uint32_t dram_page_size; // DRAM row buffer size (in bytes)
    uint32_t dram_banks_per_ch;
    uint32_t dram_bankgroups_per_ch;
    uint32_t fixed_columns_;
    bool row_major;

    /* Log config */
    std::string operation_log_output_path;
    std::string log_dir;

    /* Client config */
    uint32_t request_input_seq_len;
    uint32_t request_interval;
    uint32_t request_total_cnt;
    std::string request_dataset_path;

    /* Sheduler config */
    std::string scheduler_type;

    std::string log_level;

    // uint64_t align_address(uint64_t addr) { return addr - (addr % dram_req_size); }
  };

  namespace Config
  {
    extern SimulationConfig global_config;
  }
} // namespace pimony