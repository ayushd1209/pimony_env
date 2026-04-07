#include "PIMSim.h"

#include "common.h"
#include "configuration.h"
#include "dram_system.h"

namespace dramsim3 {

PIMSim::PIMSim(const std::string &config_file, const std::string &output_dir,
               const std::string log_level)
    : config_(new Config(config_file, output_dir)) {
    std::function<void(uint64_t)> read_callback = [&](uint64_t addr) {
        PrintInfo("(PIMSim) read_callback");
        int channel = GetChannel(addr);
        assert(pending_read_q_.count(addr) > 0);
        if (pending_read_q_.count(addr) == 0) exit(1);

        auto it = pending_read_q_.find(addr);        // search
        response_queues_[channel].push(it->second);  // push
        pending_read_q_.erase(it);                   // pop
    };
    std::function<void(uint64_t)> pim_callback = [&](uint64_t addr) {
        PrintInfo("(PIMSim) pim_callback");
        int channel = GetChannel(addr);
        if (pending_pim_q_.count(addr) == 0) {
            std::cerr << "[PIMSim] pim_callback: addr 0x" << std::hex << addr
                      << " not found in pending_pim_q_" << std::endl;
            exit(1);
        }

        auto it = pending_pim_q_.find(addr);         // search
        response_queues_[channel].push(it->second);  // push
        pending_pim_q_.erase(it);                    // pop
    };
    std::function<void(uint64_t)> write_callback = [&](uint64_t addr) {
        PrintInfo("(PIMSim) write_callback");
        int channel = GetChannel(addr);
        if (pending_write_q_.count(addr) == 0) {
            std::cerr << "[PIMSim] write_callback: addr 0x" << std::hex << addr
                      << " not found in pending_write_q_" << std::endl;
            exit(1);
        }

        auto it = pending_write_q_.find(addr);       // search
        response_queues_[channel].push(it->second);  // push
        pending_write_q_.erase(it);                  // pop
    };

    if (log_level == "debug") {
        LOGGING_CONFIG::PIMSIM_LOGGING_DEBUG = true;
    } else {
        LOGGING_CONFIG::PIMSIM_LOGGING_DEBUG = false;
    }

    if (log_level == "off") {
        LOGGING_CONFIG::PIMSIM_LOGGING_DEBUG = false;
        LOGGING_CONFIG::PIMSIM_LOGGING = false;
    }

    dram_system_ = new JedecDRAMSystem(*config_, output_dir, read_callback,
                                       pim_callback, write_callback);

    int res_q_size;

    res_q_size = config_->trans_queue_size * 3;
    // res_q_size = 4352;

    for (int ch = 0; ch < config_->channels; ++ch) {
        response_queues_.push_back(ResponseQueue(res_q_size));
    }

    g_bankgroup_bit_width = config_->ranks * config_->bankgroups;
}

uint64_t PIMSim::GetAvgPIMCycles() { return dram_system_->GetAvgPIMCycles(); }
void PIMSim::ResetPIMCycle() { dram_system_->ResetPIMCycle(); }

PIMSim::~PIMSim() {
    // std::cout << "PIMSim delete" << std::endl;
    delete (dram_system_);
    delete (config_);
}

void PIMSim::ClockTick() { dram_system_->ClockTick(); }

double PIMSim::GetTCK() const { return config_->tCK; }

int PIMSim::GetBusBits() const { return config_->bus_width; }

int PIMSim::GetDeviceBits() const { return config_->device_width; }

int PIMSim::GetBurstLength() const { return config_->BL; }

int PIMSim::GetQueueSize() const {
    exit(-1);
    // unused method
    return config_->trans_queue_size;
}

int PIMSim::GetChannel(uint64_t hex_addr) const {
    return dram_system_->GetChannel(hex_addr);
};

uint64_t PIMSim::MakeAddress(int channel, int rank, int bankgroup, int bank,
                             int row, int col) {
    return config_->MakeAddress(channel, rank, bankgroup, bank, row, col);
}

bool PIMSim::WillAcceptTransaction(uint64_t hex_addr, int req_type) const {
    TransactionType type = static_cast<TransactionType>(req_type);
    // check response queue size
    int channel = GetChannel(hex_addr);
    bool available = response_queues_[channel].isAvailable(1);

    return available && dram_system_->WillAcceptTransaction(hex_addr, type);
}

bool PIMSim::AddTransaction(uint64_t hex_addr, int req_type, uint32_t num_macs,
                            void *original_req) {
    TransactionType type = static_cast<TransactionType>(req_type);
    int channel = GetChannel(hex_addr);

    response_queues_[channel].reserve();
    PushToPendingQueue(hex_addr, type, original_req);
    income_req_cnt_++;

    if (req_type == int(TransactionType::MAC) ||
        req_type == int(TransactionType::H2GWRITE)) {
        return dram_system_->AddTransaction(hex_addr, type, num_macs);
    } else {
        return dram_system_->AddTransaction(hex_addr, type);
    }
}

void PIMSim::PushToPendingQueue(uint64_t addr, TransactionType req_type,
                                void *original_req) {
    switch (req_type) {
        case TransactionType::D2GWRITE:
        case TransactionType::H2GWRITE:
        case TransactionType::COMP:
        case TransactionType::MAC:
        case TransactionType::READRES:
            pending_pim_q_.insert(std::make_pair(addr, original_req));
            break;
        case TransactionType::READ:
            pending_read_q_.insert(std::make_pair(addr, original_req));
            break;
        case TransactionType::WRITE:
            pending_write_q_.insert(std::make_pair(addr, original_req));
            break;
        default:
            break;
    }
    return;
}

bool PIMSim::IsEmpty(uint32_t channel) const {
    bool empty = response_queues_[channel].isEmpty();
    // std::cout << "PIMSim::IsEmpty(" << channel
    //           << ") : " << std::to_string(empty) << std::endl;
    return empty;
};
void *PIMSim::Top(uint32_t channel) const {
    // printf("TOP channel= %d\n", channel);
    return response_queues_[channel].top();
};
void PIMSim::Pop(uint32_t channel) {
    response_queues_[channel].pop();
    outcome_req_cnt_++;
};

void PIMSim::PrintStats() const { dram_system_->PrintStats(); }

void PIMSim::ResetStats() { dram_system_->ResetStats(); }

PIMSim::ResponseQueue::ResponseQueue(int Size) : Size(Size), NumReserved(0) {}

bool PIMSim::ResponseQueue::isAvailable() const {
    return NumReserved + OutputQueue.size() < Size;
}

bool PIMSim::ResponseQueue::isAvailable(uint32_t count) const {
    return NumReserved + OutputQueue.size() + count - 1 < Size;
}

void PIMSim::ResponseQueue::reserve() {
    assert(NumReserved < Size);
    NumReserved++;
}

void PIMSim::ResponseQueue::push(void *original_req) {
    // std::cout << "ResponseQueue::push" << std::endl;
    OutputQueue.push_back(original_req);
    assert(NumReserved > 0);
    NumReserved--;
}

bool PIMSim::ResponseQueue::isEmpty() const { return OutputQueue.empty(); }

void PIMSim::ResponseQueue::pop() {
    auto it = OutputQueue.begin();
    // delete (void *)(*it);
    OutputQueue.erase(it);
}
void *PIMSim::ResponseQueue::top() const { return OutputQueue.front(); }

}  // namespace dramsim3
