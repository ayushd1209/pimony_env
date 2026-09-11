#ifndef __MEMORY_SYSTEM__H
#define __MEMORY_SYSTEM__H

#include <functional>
#include <string>

#include "helper/CommandLineParser.h"
#include "Common.h"
#include "Dram.h"
#include "Request.h"
#include <unordered_map>

namespace pimony
{
  // This should be the interface class that deals with CPU
  class MemorySystem
  {
  public:
    MemorySystem(const std::string &mem_config, const std::string &model_config,
                 const std::string &log_dir, const std::string &log_level,
                 std::function<void(uint32_t)> pim_callback,
                 std::function<void(uint64_t)> read_callback,
                 std::function<void(uint64_t)> write_callback);
    ~MemorySystem();
    void ClockTick();
    void RegisterCallbacks(std::function<void(uint64_t)> read_callback,
                           std::function<void(uint64_t)> write_callback);
    double GetTCK() const;
    int GetBusBits() const;
    int GetBurstLength() const;
    int GetQueueSize() const;
    void PrintStats() const;
    void ResetStats();

    bool WillAcceptTransaction(uint64_t hex_addr, bool is_write) const;
    bool AddTransaction(uint64_t hex_addr, bool is_write);
    bool AddMACTransaction(uint64_t hex_addr, uint32_t num_macs, uint32_t cpu_token,
                           bool comp);
    // pim.gemv entry point. Records the job and returns; the MACs are issued
    // over the following cycles by IssuePendingGemv(). Returns false if a job
    // is already in flight.
    bool AddGEMVTransaction(uint64_t base, uint64_t v_base,
                            uint32_t num_outputs, uint32_t dot_steps,
                            uint32_t cpu_token);
    std::function<void(uint32_t)> pim_callback_;
    std::function<void(uint64_t req_id)> read_callback_, write_callback_;

  private:
    double tCK;
    int bus_width;
    int BL;
    int trans_queue_size;
    int channels;
    int num_bankgroups;
    int clk_;
    PIM *dram;
    Request::TraceRequestHandler *request_handler;
    MemoryAccess *mem_request;
    MemoryAccess *mem_response;
    std::vector<bool> pim_done;
    std::vector<std::vector<bool>> pim_done_bg;

    std::unordered_map<uint64_t, uint64_t> read_sub2orig_; // sub_addr -> orig_addr
    std::unordered_map<uint64_t, uint64_t> write_sub2orig_;
    std::unordered_map<uint64_t, int> read_remain_; // orig_addr -> remaining count
    std::unordered_map<uint64_t, int> write_remain_;

    // ================= pim.gemv sequencer =================================
    // Expands ONE instruction into many MAC commands, like a DMA engine
    // expands one descriptor into many bus transactions. Emits at most
    // kGemvMacsPerCycle per DRAM cycle, and only when the queue has room, so
    // it can never overflow PIMony's transaction queue.
    //
    // OPERAND CONTRACT (part of the instruction spec, not an implementation
    // detail). Must match bert.c pim_idx() exactly:
    //   output j -> unit j % units_ -> bank = unit % banks, stream = unit / banks
    //               wave j / units_ -> DRAM row
    //   input  i -> step i / 16 (FP16), slot i % 16
    // A MAC command carries no bank field (pim_controller.cc:766 sets
    // addr.bank = -1), so it broadcasts to all banks of its bankgroup: one
    // command computes `banks` DIFFERENT outputs, and one output must live
    // entirely in ONE bank because the result register is per bank with no
    // cross-bank reduction. A row-major matrix physically cannot be processed.
    // bert.c produces this layout at init; precedent is SIMDRAM/gem5-CIMD
    // requiring operands from a custom allocator. Handing pim.gemv an ordinary
    // matrix computes nonsense silently -- the model cannot detect it.
    //
    // A dot must also fit ONE row (<= cols_per_row_ steps). Longer reductions
    // are split by software into partial matmuls whose partial sums the host
    // adds -- see rows_per_wave. That keeps the sequencer free of any
    // assumption about an accumulator surviving a precharge, which PIMony
    // cannot model and the paper does not settle.
    static const uint32_t kGemvMacsPerCycle = 1;   // one command issue port

    // Address geometry, derived from the SAME ini the PIM controller reads, so
    // no stride is hardcoded. Only valid for address_mapping = rorabacobgch
    // (ch/bg are the LOW fields); addr_map_ok_ is false otherwise and the
    // sequencer refuses to run rather than compute wrong addresses.
    int device_width_;
    int ini_ranks_, ini_bankgroups_;   // from the INI. NOT num_bankgroups,
                                       // which is the json's outer-scheduler
                                       // value (8) and a different number.
    int ini_banks_per_group_, ini_columns_;
    int cols_per_row_;                 // columns / BL = 64, the row step cap
    int engines_;                      // channels * ranks * bankgroups = 32
                                       // command STREAMS, not compute units
    int units_;                        // engines_ * banks_per_group = 128
                                       // compute units; one command drives
                                       // banks_per_group of them at once
    uint64_t chunk_bytes_;             // one column step: device_width/8 * BL
    uint64_t ch_stride_, bg_stride_, col_stride_;
    uint64_t bank_stride_, rank_stride_, row_stride_;
    bool addr_map_ok_;

    // Phases of one GEMV.
    //   GWRITE   fill the global buffer with the input vector, once
    //   COMPUTE  MAC and READRES -- these INTERLEAVE PER STREAM, they are not
    //            two global stages. A bankgroup that has read its four results
    //            out is free to start its next dot while other bankgroups are
    //            still multiplying; a global MAC-then-READRES barrier would
    //            synchronise all 32 streams at every wave, which is a
    //            constraint the hardware does not have.
    //   DONE     nothing left to issue, nothing outstanding
    enum class GemvPhase { GWRITE, COMPUTE, DONE };

    // One GEMV in flight at a time: a single GEMV already spans all 32
    // engines, so a second would contend for them and add no throughput.
    struct GemvJob
    {
      GemvPhase phase = GemvPhase::DONE;
      uint64_t base = 0;          // weight base, must be row_stride_-aligned
      uint64_t v_base = 0;          // input-vector base, one copy per channel;
                                    // D2GWRITE's SOURCE row (bank_stride_-aligned)
      uint32_t num_outputs = 0;   // how many dots
      uint32_t dot_steps = 0;     // column steps per dot (FP16: in_dim / 16)
      uint32_t cpu_token = 0;     // whose pim.wait to wake when ALL are done
      uint64_t next_cmd = 0;      // cursor over COMMANDS, not over dots: one
                                  // command covers banks_per_group outputs
      uint32_t outstanding = 0;   // issued but not yet drained
      // One bit per engine: set on issue, cleared when that MAC's response
      // comes back. A MAC sent to an engine that is STILL RUNNING is treated
      // by PIMony as PREEMPTION of the in-flight one, not as continued
      // accumulation -- it takes the continuation branch at
      // pim_controller.cc:604 and aborts with "Not in the return queue".
      // Confirmed minimally: 1 engine + 1 command passes, 1 engine + 2
      // commands aborts, nothing else changed. A real sequencer cannot feed a
      // busy engine either, so gating is faithful, not a workaround.
      uint64_t engine_busy = 0;

      // GWRITE bookkeeping. One fill per CHANNEL, so these count to `channels`,
      // not to engines_. todo = still to issue, out = issued but not drained.
      uint8_t gwrite_todo = 0;
      uint8_t gwrite_out = 0;

      // READRES bookkeeping, per stream. A stream is NOT free when its MAC
      // drains -- it still holds banks_per_group unread accumulators, and it
      // cannot start its next dot until they are out (the single-accumulator
      // assumption; two result registers would pipeline this away). So
      // engine_busy stays set from MAC issue until the last of that stream's
      // readouts drains.
      uint64_t readres_base[64] = {};  // addr of the MAC that just drained
      uint8_t  readres_todo[64] = {};  // still to issue for this stream
      uint8_t  readres_out[64]  = {};  // issued, not yet drained
      uint32_t readres_pending = 0;    // total still to issue, all streams

      bool active = false;
    };
    GemvJob gemv_;

    // One command covers banks_per_group outputs at once and a whole dot's
    // worth of steps, so this is ceil(outputs / banks) -- NOT outputs x steps
    // (rev-1) and NOT waves x streams, which would round UP to a whole wave and
    // issue commands for streams holding no data, reading past the operand.
    // 768 outputs -> 192 commands; 32 outputs -> 8, on streams 0..7 only.
    uint64_t GemvTotalCmds() const
    {
      return ((uint64_t)gemv_.num_outputs + ini_banks_per_group_ - 1)
             / (uint64_t)ini_banks_per_group_;
    }
    bool GemvIssueDone() const { return gemv_.next_cmd >= GemvTotalCmds(); }

    // Generalised PIM entry point. AddMACTransaction is a wrapper on this with
    // type = MAC; D2GWRITE / READRES / COMP were unreachable from gem5 only
    // because that wrapper hardcoded the type. NOT in the dramsim3.h facade:
    // gem5 never calls it, and MemoryAccessType is not visible there.
    bool AddPIMTransaction(MemoryAccessType type, uint64_t hex_addr,
                           uint32_t num_macs, uint32_t cpu_token, bool comp);

    void IssuePendingGemv();                                  // the drip-feed
    uint64_t GemvCmdAddr(uint64_t cmd) const;
    uint32_t GemvCmdRun(uint64_t cmd) const;
    uint32_t GemvCmdStream(uint64_t cmd) const;
    // Which engine owns an address -- GemvCmdAddr run backwards, so the two
    // agree by construction. Offsets are taken from gemv_.base so no carry
    // from the base can leak into the rank field.
    uint32_t GemvEngineOfAddr(uint64_t addr) const;
  };

  MemorySystem *GetMemorySystem(const std::string &mem_config, const std::string &model_config,
                                const std::string &log_dir, const std::string &log_level,
                                std::function<void(uint32_t)> pim_callback,
                                std::function<void(uint64_t)> read_callback,
                                std::function<void(uint64_t)> write_callback);

} // namespace pimony

#endif
