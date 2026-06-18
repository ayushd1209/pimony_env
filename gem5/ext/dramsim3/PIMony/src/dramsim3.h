#ifndef __MEMORY_SYSTEM__H
#define __MEMORY_SYSTEM__H

#include <functional>
#include <string>

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
    bool AddMACTransaction(uint64_t hex_addr, uint32_t num_macs, uint32_t cpu_token);
    std::function<void(uint32_t)> pim_callback_;
    std::function<void(uint64_t req_id)> read_callback_, write_callback_;
  };

  MemorySystem *GetMemorySystem(const std::string &mem_config, const std::string &model_config,
                                const std::string &log_dir, const std::string &log_level,
                                std::function<void(uint32_t)> pim_callback,
                                std::function<void(uint64_t)> read_callback,
                                std::function<void(uint64_t)> write_callback);

} // namespace pimony
#endif
