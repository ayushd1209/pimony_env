#pragma once

#include "Common.h"
#include "Dram.h"
#include "spdlog/spdlog.h"
#include "LLM.h"

#include <fstream>
#include <queue>
#include <string>
#include <vector>
namespace pimony
{
  namespace Request
  {
    class TraceRequestHandler
    {
    public:
      TraceRequestHandler(SimulationConfig config, const std::string &normal_path = "");

      MemoryAccess *getNextAccess(int core_id, cycle_type curr_cycle, bool pim_tile_done, std::vector<bool> pim_tile_done_bg);
      void print_state() const;
      void update_latency(int core_id, cycle_type cur_cycle, bool read_done,
                          bool write_done,
                          bool pim_tile_done, uint32_t id);
      void AddNormalTransaction(TraceEntry request);
      bool is_normal_trace_empty() const;
      bool is_pim_trace_empty() const;
      bool is_pim_operation_done() const;
      bool is_pim_trace_done() const;
      LLM::LLMGenerator trace_generator;
      bool slo_violation_flag;

    private:
      std::vector<std::queue<TraceEntry>> normal_queue;

      std::vector<std::vector<std::vector<TraceEntry>>> pim_tiles;
      std::vector<std::vector<std::vector<std::vector<TraceEntry>>>> pim_tiles_bg_sync;
      std::vector<size_t> current_pim_tile_idx;
      std::vector<size_t> current_bg_idx;
      std::vector<size_t> current_pim_op_idx;
      std::vector<std::vector<size_t>> current_pim_tile_idx_bg;
      std::vector<std::vector<size_t>> current_pim_op_idx_bg;

      std::vector<bool> waiting_on_prev_pim_tile;
      std::vector<std::vector<bool>> waiting_on_prev_pim_tile_bg;

      std::unordered_map<uint32_t, cycle_type>
          normal_issue_time_map;

      struct PimStatus
      {
        cycle_type issue_cycle;
        bool done = false;
      };
      std::vector<std::unordered_map<uint32_t, PimStatus>> pim_issue_time_map;
      ;
      uint64_t read_latency_sum = 0;
      uint64_t write_latency_sum = 0;
      uint64_t pim_latency_sum = 0;

      uint64_t read_count = 0;
      uint64_t write_count = 0;
      uint64_t pim_count = 0;

      int columns_;
      int device_width_;
      int channels_;
      int num_bankgroups_;

      // SLO check
      double tCK_;
      int TPOT_slo_;
      int model_n_layer_;
      std::string model_name_;
      int cur_gen_start_clk_;
      // Scheduling
      ComputeMode compute_mode_;

      void reset_pim_layer_state();
      int get_channel_from_address(uint64_t address);
      MemoryAccessType parse_access_type(const std::string &str) const;
      void load_normal_trace(const std::string &path);
      void load_pim_trace(const std::vector<TraceEntry> &trace_entries);
      void print_pim_tiles();

      uint64_t last_done_clk_;
    };
  } // namespace Request
} // namespace pimony