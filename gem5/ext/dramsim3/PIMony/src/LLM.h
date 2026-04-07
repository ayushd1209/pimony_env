#pragma once

#include "Common.h"
#include "Address.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <queue>
#include <string>
#include <vector>
namespace pimony
{

  enum class LayerStage
  {
    QKV,
    Q_K_T,
    S_V,
    OUT_PROJ,
    FC1,
    FC2
  };

  namespace LLM
  {

    class LLMGenerator
    {
    public:
      ADDRESS::Address address;

      LLMGenerator(SimulationConfig config);
      bool is_all_model_done() const;
      std::vector<TraceEntry> getNextTrace();
      bool token_change_flag;

    private:
      std::vector<TraceEntry> getGEMVtrace(uint32_t N, uint32_t K);
      std::vector<TraceEntry> getQxKtrace(uint32_t cur_seq_len);
      std::vector<TraceEntry> getSxVtrace(uint32_t cur_seq_len);

      std::string model_name_;
      float_t params_b_;
      uint32_t block_size_;
      uint32_t vocab_size_;
      uint32_t n_layer_;
      uint32_t n_head_;
      uint32_t d_model_;
      uint32_t d_interm_;
      uint32_t d_head_;
      uint32_t input_len_;
      uint32_t output_len_;
      uint32_t TPOT_slo_;

      uint32_t current_layer_ = 0;
      uint32_t current_gen_len_ = 0;
      LayerStage current_stage_ = LayerStage::QKV;

      uint32_t channels_;
      uint32_t ranks_;
      uint32_t bankgroups_;
      uint32_t banks_per_group_;
      uint32_t rows_;
      uint32_t columns_;
      uint32_t device_width_;
      uint32_t burst_length_;
      ComputeMode compute_mode_;

      uint32_t page_size_bytes_;
      uint32_t num_banks_;
      uint32_t elem_bits_;
      uint32_t elems_per_page_;
      uint32_t burst_read_size_;
      uint32_t comp_per_tile_;
      uint32_t elems_per_comp_;
      uint32_t fixed_columns_;

      bool row_major_;
    };
  } // namespace LLM
} // namespace pimony