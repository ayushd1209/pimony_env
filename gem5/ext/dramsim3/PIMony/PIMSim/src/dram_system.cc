#include "dram_system.h"

#include <assert.h>
#include <chrono>

namespace dramsim3 {

// alternative way is to assign the id in constructor but this is less
// destructive
int BaseDRAMSystem::total_channels_ = 0;

BaseDRAMSystem::BaseDRAMSystem(Config &config, const std::string &output_dir,
                               std::function<void(uint64_t)> read_callback,
                               std::function<void(uint64_t)> pim_callback,
                               std::function<void(uint64_t)> write_callback)
    : read_callback_(read_callback),
      pim_callback_(pim_callback),
      write_callback_(write_callback),
      last_req_clk_(0),
      config_(config),
      timing_(config_),
      clk_(0) {
    total_channels_ += config_.channels;

#ifdef ADDR_TRACE
    std::string addr_trace_name = config_.output_prefix + "addr.trace";
    address_trace_.open(addr_trace_name);
#endif
}

int BaseDRAMSystem::GetChannel(uint64_t hex_addr) const {
    hex_addr >>= config_.shift_bits;
    return (hex_addr >> config_.ch_pos) & config_.ch_mask;
}

void BaseDRAMSystem::PrintEpochStats() {
    // first epoch, print bracket
    if (clk_ - config_.epoch_period == 0) {
        std::ofstream epoch_out(config_.json_epoch_name, std::ofstream::out);
        epoch_out << "[";
    }
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->PrintEpochStats();
        std::ofstream epoch_out(config_.json_epoch_name, std::ofstream::app);
        epoch_out << "," << std::endl;
    }
    return;
}

void BaseDRAMSystem::PrintStats() {
    // Finish epoch output, remove last comma and append ]
    std::ofstream epoch_out(config_.json_epoch_name, std::ios_base::in |
                                                         std::ios_base::out |
                                                         std::ios_base::ate);
    epoch_out.seekp(-2, std::ios_base::cur);
    epoch_out.write("]", 1);
    epoch_out.close();

    std::ofstream json_out(config_.json_stats_name, std::ofstream::out);
    json_out << "{";

    // close it now so that each channel can handle it
    json_out.close();
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->PrintFinalStats();
        if (i != ctrls_.size() - 1) {
            std::ofstream chan_out(config_.json_stats_name, std::ofstream::app);
            chan_out << "," << std::endl;
        }
    }
    json_out.open(config_.json_stats_name, std::ofstream::app);
    json_out << "}";
}

void BaseDRAMSystem::ResetStats() {
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->ResetStats();
    }
}

void BaseDRAMSystem::RegisterCallbacks(
    std::function<void(uint64_t)> read_callback,
    std::function<void(uint64_t)> pim_callback,
    std::function<void(uint64_t)> write_callback) {
    // this should be propagated to controllers
    read_callback_ = read_callback;
    pim_callback_ = pim_callback;
    write_callback_ = write_callback;
}

JedecDRAMSystem::JedecDRAMSystem(Config &config, const std::string &output_dir,
                                 std::function<void(uint64_t)> read_callback,
                                 std::function<void(uint64_t)> pim_callback,
                                 std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config, output_dir, read_callback, pim_callback,
                     write_callback) {
    ctrls_.reserve(config_.channels);
    for (auto i = 0; i < config_.channels; i++) {
        Controller *ctrl;
        ctrl = new PIMController(i, config_, timing_);

        ctrls_.push_back(ctrl);
    }
}

uint64_t JedecDRAMSystem::GetAvgPIMCycles() {
    uint64_t total_pim_cycles = 0;
    for (auto i = 0; i < config_.channels; i++) {
        total_pim_cycles += ctrls_[i]->GetPIMCycle();
    }

    return total_pim_cycles / config_.channels;
}
void JedecDRAMSystem::ResetPIMCycle() {
    for (auto i = 0; i < config_.channels; i++) {
        ctrls_[i]->ResetPIMCycle();
    }
}

JedecDRAMSystem::~JedecDRAMSystem() {
    for (auto it = ctrls_.begin(); it != ctrls_.end(); it++) {
        delete (*it);
    }
}

bool JedecDRAMSystem::WillAcceptTransaction(uint64_t hex_addr,
                                            TransactionType req_type) const {
    int channel = GetChannel(hex_addr);
    return ctrls_[channel]->WillAcceptTransaction(hex_addr, req_type);
}

bool JedecDRAMSystem::AddTransaction(uint64_t hex_addr,
                                     TransactionType req_type) {
// Record trace - Record address trace for debugging or other purposes
#ifdef ADDR_TRACE
    address_trace_ << std::hex << hex_addr << std::dec << " "
                   << (is_write ? "WRITE " : "READ ") << clk_ << std::endl;
#endif

    int channel = GetChannel(hex_addr);
    bool ok = ctrls_[channel]->WillAcceptTransaction(hex_addr, req_type);

    assert(ok);
    if (ok) {
        Transaction trans = Transaction(hex_addr, req_type);
        ctrls_[channel]->AddTransaction(trans);
    }
    last_req_clk_ = clk_;
    return ok;
}

bool JedecDRAMSystem::AddTransaction(uint64_t hex_addr,
                                     TransactionType req_type,
                                     uint32_t num_macs) {
// Record trace - Record address trace for debugging or other purposes
#ifdef ADDR_TRACE
    address_trace_ << std::hex << hex_addr << std::dec << " "
                   << (is_write ? "WRITE " : "READ ") << clk_ << std::endl;
#endif

    int channel = GetChannel(hex_addr);
    bool ok = ctrls_[channel]->WillAcceptTransaction(hex_addr, req_type);

    assert(ok);
    if (ok) {
        Transaction trans = Transaction(hex_addr, req_type, num_macs);
        ctrls_[channel]->AddTransaction(trans);
    }
    last_req_clk_ = clk_;
    return ok;
}

void JedecDRAMSystem::ClockTick() {
    for (size_t i = 0; i < ctrls_.size(); i++) {
        // look ahead and return earlier
        while (true) {
            auto pair = ctrls_[i]->ReturnDoneTrans(clk_);
            TransactionType trans_type = pair.second;

            if (trans_type == TransactionType::WRITE) {
                write_callback_(pair.first);
            } else if (trans_type == TransactionType::READRES ||
                       trans_type == TransactionType::H2GWRITE ||
                       trans_type == TransactionType::D2GWRITE ||
                       trans_type == TransactionType::COMP ||
                       trans_type == TransactionType::MAC) {
                pim_callback_(pair.first);
            } else if (trans_type == TransactionType::READ) {
                read_callback_(pair.first);

            } else {
                break;
            }
        }
    }

    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->ClockTick();
    }
    clk_++;

    if (clk_ % config_.epoch_period == 0) {
        PrintEpochStats();
    }
    return;
}

IdealDRAMSystem::IdealDRAMSystem(Config &config, const std::string &output_dir,
                                 std::function<void(uint64_t)> read_callback,
                                 std::function<void(uint64_t)> pim_callback,
                                 std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config, output_dir, read_callback, pim_callback,
                     write_callback),
      latency_(config_.ideal_memory_latency) {}

IdealDRAMSystem::~IdealDRAMSystem() {}

bool IdealDRAMSystem::AddTransaction(uint64_t hex_addr,
                                     TransactionType req_type) {
    auto trans = Transaction(hex_addr, req_type);
    trans.added_cycle = clk_;
    infinite_buffer_q_.push_back(trans);
    return true;
}

void IdealDRAMSystem::ClockTick() {
    for (auto trans_it = infinite_buffer_q_.begin();
         trans_it != infinite_buffer_q_.end();) {
        if (clk_ - trans_it->added_cycle >= static_cast<uint64_t>(latency_)) {
            if (trans_it->is_write()) {
                write_callback_(trans_it->addr);
            } else if (trans_it->is_read()) {
                read_callback_(trans_it->addr);
            } else {
                pim_callback_(trans_it->addr);
            }
            trans_it = infinite_buffer_q_.erase(trans_it++);
        }
        if (trans_it != infinite_buffer_q_.end()) {
            ++trans_it;
        }
    }

    clk_++;
    return;
}

}  // namespace dramsim3
