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
                 std::function<void()> pim_callback,
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
    std::function<void()> pim_callback_;
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
  };

  MemorySystem *GetMemorySystem(const std::string &mem_config, const std::string &model_config,
                                const std::string &log_dir, const std::string &log_level,
                                std::function<void(uint64_t)> pim_callback,
                                std::function<void(uint64_t)> read_callback,
                                std::function<void(uint64_t)> write_callback);

} // namespace pimony

#endif
