#include "LLM.h"
namespace pimony
{
  namespace LLM
  {
    LLMGenerator::LLMGenerator(SimulationConfig config) : model_name_(config.model_name),
                                                          params_b_(config.model_params_b),
                                                          block_size_(config.model_block_size),
                                                          vocab_size_(config.model_vocab_size),
                                                          n_layer_(config.model_n_layer),
                                                          n_head_(config.model_n_head),
                                                          d_model_(config.model_d_model),
                                                          d_interm_(config.model_d_interm),
                                                          d_head_(config.model_d_head),
                                                          input_len_(config.input_len),
                                                          output_len_(config.output_len),
                                                          row_major_(config.row_major),
                                                          fixed_columns_(config.fixed_columns_),
                                                          address(config.pim_config_path),
                                                          token_change_flag(false)
    {
      std::ifstream file(config.pim_config_path);
      std::string line;

      if (endsWith(model_name_, "single_layer"))
      {
        n_layer_ = 1;
      }

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

        if (key == "channels")
          channels_ = std::stoi(value);
        if (key == "ranks")
          ranks_ = std::stoi(value);
        if (key == "bankgroups")
          bankgroups_ = std::stoi(value);
        else if (key == "banks_per_group")
          banks_per_group_ = std::stoi(value);
        else if (key == "rows")
          rows_ = std::stoi(value);
        else if (key == "columns")
          columns_ = std::stoi(value);
        else if (key == "device_width")
          device_width_ = std::stoi(value);
        else if (key == "BL")
          burst_length_ = std::stoi(value);
        else if (key == "compute_mode")
          if (value == "ASYNC")
            compute_mode_ = ComputeMode::ASYNC;
          else if (value == "ALL_BANK")
            compute_mode_ = ComputeMode::ALL_BANK;
      }
      page_size_bytes_ = columns_ * device_width_ / 8;
      num_banks_ = ranks_ * bankgroups_ * banks_per_group_;
      elem_bits_ = PRECISION_BITS.at(config.precision);
      elems_per_page_ = (page_size_bytes_ * 8) / elem_bits_;
      burst_read_size_ = burst_length_ * device_width_ / 8;
      comp_per_tile_ = page_size_bytes_ / burst_read_size_;
      elems_per_comp_ = burst_read_size_ / (elem_bits_ / 8);
    }

    bool LLMGenerator::is_all_model_done() const
    {
      if (startsWith(model_name_, "GEMV"))
      {
        return current_layer_ >= n_layer_;
      }
      if (startsWith(model_name_, "GPT"))
      {
        return current_gen_len_ >= output_len_;
      }
    }

    std::vector<TraceEntry> LLMGenerator::getNextTrace()
    {
      if (startsWith(model_name_, "GEMV"))
      {
        if (current_layer_ < n_layer_)
        {
          current_layer_++;

          return getGEMVtrace(d_interm_, d_model_);
        }
      }

      if (startsWith(model_name_, "GPT"))
      {

        if (current_layer_ >= n_layer_)
        {
          current_layer_ = 0;
          current_gen_len_++;
          token_change_flag = true;
        }

        if (current_gen_len_ >= output_len_)
        {
          return {};
        }

        if (current_layer_ < n_layer_)
        {
          switch (current_stage_)
          {
          case LayerStage::QKV:
          {
            std::vector<TraceEntry> trace = getGEMVtrace(d_model_ * 3, d_model_); // Q, K, V projection
            current_stage_ = LayerStage::Q_K_T;
            return trace;
          }
          case LayerStage::Q_K_T:
          {
            std::vector<TraceEntry> trace = getQxKtrace(input_len_ + current_gen_len_);
            current_stage_ = LayerStage::S_V;
            return trace;
          }
          case LayerStage::S_V:
          {
            std::vector<TraceEntry> trace = getSxVtrace(input_len_ + current_gen_len_);
            current_stage_ = LayerStage::OUT_PROJ;
            return trace;
          }
          case LayerStage::OUT_PROJ:
          {
            std::vector<TraceEntry> trace = getGEMVtrace(d_model_, d_model_); // projection
            current_stage_ = LayerStage::FC1;
            return trace;
          }
          case LayerStage::FC1:
          {
            std::vector<TraceEntry> trace = getGEMVtrace(d_interm_, d_model_);
            current_stage_ = LayerStage::FC2;
            return trace;
          }
          case LayerStage::FC2:
          {
            std::vector<TraceEntry> trace = getGEMVtrace(d_model_, d_interm_);
            current_stage_ = LayerStage::QKV;
            current_layer_++;
            return trace;
          }
          }
        }
      }
      return {};
    }

    std::vector<TraceEntry> LLMGenerator::getQxKtrace(uint32_t cur_seq_len)
    {
      std::vector<TraceEntry> trace;

      uint32_t heads_per_page = (elems_per_page_ + d_head_ - 1) / d_head_;
      uint32_t num_macs_per_head = (d_head_ + elems_per_comp_ - 1) / elems_per_comp_;
      uint32_t tiles_x = (n_head_ + heads_per_page - 1) / heads_per_page;
      uint32_t left_head = n_head_ % heads_per_page;
      uint32_t cur_seq_len_per_channel = (cur_seq_len + channels_ - 1) / channels_;
      uint32_t tiles_y = (cur_seq_len_per_channel + num_banks_ - 1) / num_banks_;
      for (uint32_t tx = 0; tx < tiles_x; ++tx)
      {
        for (uint32_t ty = 0; ty < tiles_y; ++ty)
        {
          uint32_t row = rand() % rows_;
          uint32_t num_heads = (tx != tiles_x - 1 || left_head == 0) ? heads_per_page : left_head;

          for (size_t ch = 0; ch < channels_; ch++)
          {
            bool need_h2gwrite = (ty == 0);
            if (need_h2gwrite)
            {
              uint32_t num_macs = (tx != tiles_x - 1 || left_head == 0) ? comp_per_tile_ : left_head * num_macs_per_head;
              trace.push_back({MemoryAccessType::H2GWRITE,
                               0x0,
                               address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0),
                               num_macs});
            }
            if (compute_mode_ == ComputeMode::ALL_BANK)
            {
              for (uint32_t i = 0; i < num_heads; ++i)
              {
                for (uint32_t j = 0; j < num_macs_per_head; j++)
                {
                  uint64_t col_addr = address.ReverseAddressMapping(ch, 0, 0, 0, row, i * num_macs_per_head + j);
                  trace.push_back({MemoryAccessType::COMP,
                                   0,
                                   col_addr,
                                   num_macs_per_head});
                }
                trace.push_back({MemoryAccessType::READRES,
                                 0,
                                 address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0),
                                 0});
              }
            }
            else
            {
              for (uint32_t h = 0; h < num_heads; ++h)
              {
                for (uint32_t r = 0; r < ranks_; ++r)
                {
                  for (uint32_t bg = 0; bg < bankgroups_; ++bg)
                  {
                    uint64_t mac_start_addr = address.ReverseAddressMapping(ch, r, bg, 0, row, 0);

                    trace.push_back({MemoryAccessType::MAC,
                                     0,
                                     mac_start_addr,
                                     num_macs_per_head});
                    trace.push_back({MemoryAccessType::READRES,
                                     0,
                                     mac_start_addr,
                                     0});
                  }
                }
              }
            }
          }
        }
      }

      return trace;
    }

    std::vector<TraceEntry> LLMGenerator::getSxVtrace(uint32_t cur_seq_len)
    {
      std::vector<TraceEntry> trace;

      uint32_t groups_per_row = elems_per_page_ / fixed_columns_;

      uint32_t tiles_x = (n_head_ + groups_per_row - 1) / groups_per_row;
      uint32_t left_head = n_head_ % groups_per_row;

      uint32_t tiles_y = (cur_seq_len + fixed_columns_ - 1) / fixed_columns_;
      uint32_t left_y = (cur_seq_len % fixed_columns_ + elems_per_comp_ - 1) / elems_per_comp_;

      uint32_t d_head_per_channel = (d_head_ + channels_ - 1) / channels_;
      uint32_t tiles_z = (d_head_per_channel + num_banks_ - 1) / num_banks_;
      // there is no left z. because d_head_ is almost mutiples of num_banks_.

      for (uint32_t tx = 0; tx < tiles_x; ++tx)
      {
        for (uint32_t ty = 0; ty < tiles_y; ++ty)
        {
          for (uint32_t tz = 0; tz < tiles_z; ++tz)
          {
            // Allocate a new row
            uint32_t row = rand() % rows_;
            uint32_t num_heads = (tx != tiles_x - 1 || left_head == 0) ? groups_per_row : left_head;
            uint32_t num_macs_per_group = (ty != tiles_y - 1 || left_y == 0) ?: left_y;

            for (size_t ch = 0; ch < channels_; ch++)
            {
              // H2GWRITE once per tile_y
              bool need_h2gwrite = (tz == 0);

              if (need_h2gwrite)
              {
                uint32_t num_macs = (ty != tiles_y - 1 || left_y == 0) ? comp_per_tile_ : left_y * groups_per_row;
                trace.push_back({MemoryAccessType::H2GWRITE,
                                 0x0,
                                 address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0),
                                 num_macs});
              }

              if (compute_mode_ == ComputeMode::ALL_BANK)
              {
                for (uint32_t i = 0; i < num_heads; ++i)
                {
                  for (uint32_t j = 0; j < num_macs_per_group; ++j)
                  {
                    uint64_t col_addr = address.ReverseAddressMapping(ch, 0, 0, 0, row, i * fixed_columns_ / elems_per_comp_ + j);
                    trace.push_back({MemoryAccessType::COMP,
                                     0,
                                     col_addr,
                                     num_macs_per_group});
                  }
                  trace.push_back({MemoryAccessType::READRES,
                                   0,
                                   address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0),
                                   0});
                }
              }
              else // ASYNC mode
              {
                for (uint32_t i = 0; i < num_heads; ++i)
                {
                  for (uint32_t r = 0; r < ranks_; ++r)
                  {
                    for (uint32_t bg = 0; bg < bankgroups_; ++bg)
                    {
                      uint64_t mac_start_addr = address.ReverseAddressMapping(ch, r, bg, 0, row, static_cast<uint64_t>(i * fixed_columns_ / elems_per_comp_));
                      trace.push_back({MemoryAccessType::MAC,
                                       0,
                                       mac_start_addr,
                                       num_macs_per_group});
                      trace.push_back({MemoryAccessType::READRES,
                                       0,
                                       mac_start_addr,
                                       0});
                    }
                  }
                }
              }
            }
          }
        }
      }
      return trace;
    }

    std::vector<TraceEntry> LLMGenerator::getGEMVtrace(uint32_t N, uint32_t K)
    {
      std::vector<TraceEntry> trace_entries;
      uint32_t N_per_channel = (N + channels_ - 1) / channels_;
      uint32_t tiles_y_ = (N_per_channel + num_banks_ - 1) / num_banks_;
      uint32_t tiles_x_ = (K + elems_per_page_ - 1) / elems_per_page_;
      uint32_t left_x_ = (K % elems_per_page_ + elems_per_comp_ - 1) / elems_per_comp_;

      if (row_major_)
      {
        for (uint32_t ty = 0; ty < tiles_y_; ++ty)
        {
          for (uint32_t tx = 0; tx < tiles_x_; ++tx)
          {
            bool need_readres = (tx == tiles_x_ - 1);
            uint32_t num_macs = (tx != tiles_x_ - 1 || left_x_ == 0) ? comp_per_tile_ : left_x_;

            // Random aligned PIM base address

            uint32_t row = rand() % rows_;

            for (size_t ch = 0; ch < channels_; ch++)
            {
              trace_entries.push_back({
                  MemoryAccessType::H2GWRITE,
                  0,                                                // issue_cycle
                  address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0), // dummy address
                  num_macs                                          // num_macs used
              });

              if (compute_mode_ == ComputeMode::ALL_BANK)
              {
                for (uint32_t i = 0; i < num_macs; ++i)
                {
                  uint64_t col_addr = address.ReverseAddressMapping(ch, 0, 0, 0, row, static_cast<uint64_t>(i));
                  trace_entries.push_back({MemoryAccessType::COMP,
                                           0,
                                           col_addr,
                                           num_macs});
                }
                if (need_readres)
                {
                  trace_entries.push_back({MemoryAccessType::READRES,
                                           0,
                                           address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0),
                                           0});
                }
              }
              else
              {
                for (uint32_t r = 0; r < ranks_; ++r)
                {
                  for (uint32_t bg = 0; bg < bankgroups_; ++bg)
                  {
                    uint64_t col_addr = address.ReverseAddressMapping(ch, r, bg, 0, row, 0);
                    trace_entries.push_back({MemoryAccessType::MAC,
                                             0,
                                             col_addr,
                                             num_macs});
                    if (need_readres)
                    {
                      trace_entries.push_back({MemoryAccessType::READRES,
                                               0,
                                               col_addr,
                                               0});
                    }
                  }
                }
              }
            }
          }
        }
      }
      else
      {
        for (uint32_t tx = 0; tx < tiles_x_; ++tx)
        {
          for (uint32_t ty = 0; ty < tiles_y_; ++ty)
          {
            bool need_h2gwrite = (ty == 0);
            uint32_t num_macs = (tx != tiles_x_ - 1 || left_x_ == 0) ? comp_per_tile_ : left_x_;

            // Random aligned PIM base address
            uint32_t row = rand() % rows_;

            for (size_t ch = 0; ch < channels_; ch++)
            {
              if (need_h2gwrite)
              {
                trace_entries.push_back({MemoryAccessType::H2GWRITE,
                                         0,
                                         address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0),
                                         num_macs});
              }

              if (compute_mode_ == ComputeMode::ALL_BANK)
              {
                for (uint32_t i = 0; i < num_macs; ++i)
                {
                  uint64_t col_addr = address.ReverseAddressMapping(ch, 0, 0, 0, row, static_cast<uint64_t>(i));
                  trace_entries.push_back({MemoryAccessType::COMP,
                                           0,
                                           col_addr,
                                           num_macs});
                }
                trace_entries.push_back({MemoryAccessType::READRES,
                                         0,
                                         address.ReverseAddressMapping(ch, 0, 0, 0, 0, 0),
                                         0});
              }
              else
              {
                for (uint32_t r = 0; r < ranks_; ++r)
                {
                  for (uint32_t bg = 0; bg < bankgroups_; ++bg)
                  {
                    uint64_t col_addr = address.ReverseAddressMapping(ch, r, bg, 0, row, 0);

                    trace_entries.push_back({MemoryAccessType::MAC,
                                             0,
                                             col_addr,
                                             num_macs});
                    trace_entries.push_back({MemoryAccessType::READRES,
                                             0,
                                             col_addr,
                                             0});
                  }
                }
              }
            }
          }
        }
      }
      return trace_entries;
    }
  } // namespace LLM
} // namespace pimony