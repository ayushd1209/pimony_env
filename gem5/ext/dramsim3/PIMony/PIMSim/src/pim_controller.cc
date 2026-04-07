#include "pim_controller.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>

namespace dramsim3 {

PIMController::PIMController(int channel, const Config &config,
                             const Timing &timing)
    : channel_id_(channel),
      clk_(0),
      config_(config),
      simple_stats_(config_, channel_id_),
      channel_state_(channel, config, timing),
      pim_cmd_queue_(channel_id_, config, channel_state_, simple_stats_),
      refresh_(config, channel_state_, simple_stats_),

      row_buf_policy_(config.row_buf_policy == "CLOSE_PAGE"
                          ? RowBufPolicy::CLOSE_PAGE
                          : RowBufPolicy::OPEN_PAGE),
      pim_first_(config.scheduling_policy == "PIM_FIRST"),
      compute_mode_(config.compute_mode == "ALL_BANK" ? ComputeMode::ALL_BANK
                                                      : ComputeMode::ASYNC),
      bank_mode_(config.bank_mode == "SINGLE" ? BankMode::SINGLE
                                              : BankMode::DPSA),
      last_trans_clk_(0),
      write_draining_(0) {
    read_queue_.reserve(config_.trans_queue_size);
    write_buffer_.reserve(
        config_.trans_queue_size);  // BUG: increase write_buffer size
    if (compute_mode_ == ComputeMode::ALL_BANK) {
        mac_states_.resize(1);
    } else {
        mac_states_.resize(config_.ranks * config_.bankgroups);
    }
    rw_dependency_lock_ = false;
    rw_dependency_addr_ = 0;
}

int PIMController::Subarray(const int row) const {
    if (row == -1) return -1;
    return int(row / config_.rows_in_subarray);
}

// stat for pim utilization
void PIMController::ResetPIMCycle() { pim_cmd_queue_.ResetPIMCycle(); }
uint64_t PIMController::GetPIMCycle() { return pim_cmd_queue_.GetPIMCycle(); }

// - [x] handle pim command
std::pair<uint64_t, TransactionType> PIMController::ReturnDoneTrans(
    uint64_t clk) {
    auto it = return_queue_.begin();
    while (it != return_queue_.end()) {
        if (clk >= it->complete_cycle) {
            TransactionType type = TransactionType::SIZE;

            if (it->is_write()) {
                simple_stats_.Increment("num_writes_done");
            } else if (it->is_read()) {
                PrintTransactionLog("ReturnDoneRead", channel_id_, clk_, *it);
                simple_stats_.Increment("num_reads_done");
                simple_stats_.AddValue("read_latency", clk_ - it->added_cycle);
            } else if (it->req_type == TransactionType::D2GWRITE ||
                       it->req_type == TransactionType::H2GWRITE) {
                pim_cmd_queue_.FinishGwrite();
                PrintInfo("cid:", channel_id_, "GWRITE done, gwrite_latency:",
                          clk_ - it->added_cycle, "clk: ", clk);
                simple_stats_.AddValue("gwrite_latency",
                                       clk_ - it->added_cycle);
            } else if (it->req_type == TransactionType::COMP) {
                PrintInfo("[COMP done], cid:", channel_id_, "clk: ", clk,
                          "addr: ", HexString(it->addr));
            } else if (it->req_type == TransactionType::MAC) {
                PrintInfo("[MAC done], cid:", channel_id_, "clk: ", clk,
                          "addr: ", HexString(it->addr));

            } else if (it->req_type == TransactionType::READRES) {
                PrintInfo("[READRES done], cid:", channel_id_, "clk: ", clk,
                          "addr: ", HexString(it->addr));
                simple_stats_.Increment("num_readres_done");
            }

            auto pair = std::make_pair(it->addr, it->req_type);
            it = return_queue_.erase(it);
            return pair;
        } else {
            ++it;
        }
    }
    return std::make_pair(-1, TransactionType::SIZE);
}

void PIMController::ClockTick() {
    // update refresh counter
    refresh_.ClockTick();
    // tracking mac operation state

    for (size_t bg = 0; bg < mac_states_.size(); ++bg) {
        auto &state = mac_states_[bg];

        if (state.is_active) {
            if (clk_ - state.last_mac_cycle >= config_.tCCD_L) {
                if (state.remaining_macs > 0) {
                    state.remaining_macs--;
                    state.last_mac_cycle = clk_;
                }

                if (state.remaining_macs == 0) {
                    state.is_active = false;
                }
            }
        }
    }
    bool cmd_issued = false;

    Command cmd;
    if (channel_state_.IsRefreshWaiting()) {
        PrintColor(Color::RED, "Refresh Waiting.., clk:", clk_);
        cmd = pim_cmd_queue_.FinishRefresh();
    }

    if (!cmd.IsValid()) {
        std::pair<int, int> refresh_slack = refresh_.GetRefreshSlack();
        cmd = pim_cmd_queue_.GetCommandToIssue(refresh_slack);
    }

    if (cmd.IsValid()) {
        if (cmd.IsRead() || cmd.IsWrite()) {
            if (compute_mode_ == ComputeMode::ASYNC &&
                bank_mode_ == BankMode::DPSA) {
                if (mac_states_[cmd.Rank() * config_.bankgroups +
                                cmd.Bankgroup()]
                        .is_active) {
                    bool same_subarray = false;
                    if (std::abs(
                            Subarray(cmd.Row()) -
                            Subarray(
                                mac_states_[cmd.Rank() * config_.bankgroups +
                                            cmd.Bankgroup()]
                                    .addr.row)) <= 1)
                        same_subarray = true;

                    if (!same_subarray) {
                        bool for_pim = true;
                        // delete existing MACINTR command
                        Command mac_intr_cmd = Command(
                            CommandType::MACINTR,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .addr,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .hex_addr,
                            for_pim, 0);
                        pim_cmd_queue_.ErasePIMCommand(mac_intr_cmd);
                        // add MACINTR command to stop the current MAC
                        // operation.
                        cmd = Command(
                            CommandType::MACINTR,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .addr,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .hex_addr,
                            for_pim,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .remaining_macs);
                        // pim_cmd_queue_.AddCommand(mac_intr_cmd);
                        channel_state_.UpdateTiming(mac_intr_cmd, clk_);
                        mac_states_[cmd.Rank() * config_.bankgroups +
                                    cmd.Bankgroup()]
                            .is_active = false;

                        // add MAC command to complete remaining MAC
                        // operation.
                        int remaining_macs =
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .remaining_macs;
                        if (remaining_macs > 0) {
                            Address addr(
                                mac_states_[cmd.Rank() * config_.bankgroups +
                                            cmd.Bankgroup()]
                                    .addr);
                            addr.column =
                                addr.column +
                                (mac_states_[cmd.Rank() * config_.bankgroups +
                                             cmd.Bankgroup()]
                                     .num_macs -
                                 remaining_macs);

                            // it cannot be used for reply to original request
                            //  uint64_t hex_addr =
                            //      config_.ReverseAddressMapping(addr);

                            Command mac_cmd = Command(
                                CommandType::MAC, addr,
                                mac_states_[cmd.Rank() * config_.bankgroups +
                                            cmd.Bankgroup()]
                                    .hex_addr,
                                for_pim, remaining_macs);
                            pim_cmd_queue_.AddCommand(mac_cmd);
                        }
                    }
                }
            }
            if (cmd.IsReadWrite()) {
                pim_cmd_queue_.EraseRWCommand(cmd);
            }
        }
        IssueCommand(cmd);
        cmd_issued = true;
    }
    // power updates pt 1
    for (int i = 0; i < config_.ranks; i++) {
        if (channel_state_.IsRankSelfRefreshing(i)) {
            simple_stats_.IncrementVec("sref_cycles", i);
        } else {
            bool all_idle = channel_state_.IsAllBankIdleInRank(i);
            if (all_idle) {
                simple_stats_.IncrementVec("all_bank_idle_cycles", i);
                channel_state_.rank_idle_cycles[i] += 1;
            } else {
                simple_stats_.IncrementVec("rank_active_cycles", i);
                // reset
                channel_state_.rank_idle_cycles[i] = 0;
            }
        }
    }

    // power updates pt 2: move idle ranks into self-refresh mode to save
    // power
    if (config_.enable_self_refresh && !cmd_issued) {
        for (auto i = 0; i < config_.ranks; i++) {
            if (channel_state_.IsRankSelfRefreshing(i)) {
                // wake up!
                if (!pim_cmd_queue_.rank_q_empty[i]) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_EXIT, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            } else {
                if (pim_cmd_queue_.rank_q_empty[i] &&
                    channel_state_.rank_idle_cycles[i] >=
                        config_.sref_threshold) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_ENTER, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            }
        }
    }

    ScheduleTransaction();
    clk_++;
    pim_cmd_queue_.ClockTick();
    simple_stats_.Increment("num_cycles");

    int interval = 20;
    // channel_id_ == TROUBLE_CHANNEL
    if (clk_ % interval == 0 && LOGGING_CONFIG::STATUS_CHECK) {
        if (LOGGING_CONFIG::LOGGING_ONLY_TROUBLE_ZONE) {
            if (channel_id_ != LOGGING_CONFIG::TROUBLE_CHANNEL) return;
        }
        PrintDebug("-------PIMSim Status Check (cid:", channel_id_, ")-------");

        bool clean_related_read = read_queue_.empty() &&
                                  pending_rd_q_.empty() &&
                                  pim_cmd_queue_.QueueEmpty();
        bool clean_related_write = write_buffer_.empty() &&
                                   pending_wr_q_.empty() &&
                                   pim_cmd_queue_.QueueEmpty();
        bool clean_related_pim = read_queue_.empty() && pending_rd_q_.empty() &&
                                 pim_cmd_queue_.PIMQueueEmpty();

        if (clean_related_read && clean_related_write && clean_related_pim) {
            PrintDebug("All queue empty!");
            return;
        }
        PrintDebug("clk:", clk_);
        PrintDebug("pim_queue.size:", pim_queue_.size());
        PrintDebug("pim_command_queue size:", pim_cmd_queue_.GetPIMQueueSize());
        PrintDebug("read_queue.size:", read_queue_.size());
        PrintDebug("write_buffer.size:", write_buffer_.size());
        PrintDebug("pending_rd_q_", pending_rd_q_.size());
        PrintDebug("pending_wr_q_", pending_wr_q_.size());
        PrintDebug("pending_pim_q_", pending_pim_q_.size());
        pim_cmd_queue_.PrintAllQueue();
        if (write_buffer_.empty() && !pending_wr_q_.empty() &&
            pim_cmd_queue_.QueueEmpty()) {
            PrintDebug("Something wrong!!");
            PrintDebug("write_buffer is empty, but pending_wr_q_ is not empty");
            // show content:
            // for (auto it = pending_wr_q_.begin(); it !=
            // pending_wr_q_.end();
            // ++it) {
            //     std::cout << "addr:" << (*it).first
            //               << " => type:" <<
            //               (*it).second.TransactionTypeString() << '\n';
            // }
        }
        if (read_queue_.empty() && !pending_rd_q_.empty() &&
            pim_cmd_queue_.QueueEmpty()) {
            PrintDebug("Something wrong!!");
            PrintDebug("read_queue is empty, but pending_rd_q_ is not empty");
        }

        return;
    }
}
bool PIMController::WillAcceptTransaction(uint64_t hex_addr,
                                          TransactionType req_type) {
    bool is_write = req_type == TransactionType::WRITE;
    bool is_read = req_type == TransactionType::READ;

    if (is_write) {
        return write_buffer_.size() < write_buffer_.capacity();
    } else {
        return read_queue_.size() < read_queue_.capacity();
    }
}

bool PIMController::AddTransaction(Transaction trans) {
    trans.added_cycle = clk_;
    simple_stats_.AddValue("interarrival_latency", clk_ - last_trans_clk_);
    last_trans_clk_ = clk_;

    if (trans.is_write()) {
        PrintTransactionLog("AddTransaction(WR)", channel_id_, clk_, trans);
        if (pending_wr_q_.count(trans.addr) == 0) {  // can not merge writes
            pending_wr_q_.insert(std::make_pair(trans.addr, trans));
            write_buffer_.push_back(trans);
        }
        trans.complete_cycle = clk_ + 1;
        return_queue_.push_back(trans);
        return true;
    } else if (trans.is_read()) {
        PrintTransactionLog("AddTransaction(RD)", channel_id_, clk_, trans);
        if (pending_wr_q_.count(trans.addr) > 0) {
            trans.complete_cycle = clk_ + 1;

            return_queue_.push_back(trans);
            return true;
        }

        pending_rd_q_.insert(std::make_pair(trans.addr, trans));
        if (pending_rd_q_.count(trans.addr) == 1) {
            read_queue_.push_back(trans);
        }
        return true;
    } else {
        PrintTransactionLog("AddTransaction(PIM)", channel_id_, clk_, trans);
        pending_pim_q_.insert(std::make_pair(trans.addr, trans));

        read_queue_.push_back(trans);
        return true;
    }
}

void PIMController::PrintTransactionQueue() const {
    auto queue = read_queue_;
    std::string commands = "";
    for (auto it = queue.begin(); it != queue.end(); it++) {
        commands += it->TransactionTypeString() + " ";
    }
    PrintWarning("READ trans_q (cid:", channel_id_, "):", commands);

    commands = "";
    for (auto it = write_buffer_.begin(); it != write_buffer_.end(); it++) {
        commands += it->TransactionTypeString() + " ";
    }
    PrintWarning("WRITE trans_q (cid:", channel_id_, "):", commands);
}

void PIMController::ScheduleTransaction() {
    if (!rw_dependency_lock_ && write_draining_ == 0) {
        // we basically have a upper and lower threshold for write buffer
        if ((write_buffer_.size() >= write_buffer_.capacity()) ||
            (write_buffer_.size() > 8 && pim_cmd_queue_.QueueEmpty())) {
            // write_buffer is full or
            // there are transactions more than 8 and cmd_queue is empty
            write_draining_ = write_buffer_.size();
        }
    }

    enum QueueToSchedule { READ_Q, WRITE_BUFFER, SIZE };
    QueueToSchedule queue_to_schedule = SIZE;
    if (rw_dependency_lock_)
        queue_to_schedule = READ_Q;
    else if (write_draining_ > 0)
        queue_to_schedule = WRITE_BUFFER;
    else
        queue_to_schedule = READ_Q;

    assert(queue_to_schedule != SIZE);

    std::vector<Transaction> &queue =
        queue_to_schedule == READ_Q ? read_queue_ : write_buffer_;

    for (auto it = queue.begin(); it != queue.end(); it++) {
        auto cmd = TransToCommand(*it);

        int rank = cmd.IsGwrite() ? -1 : cmd.Rank();

        if (pim_cmd_queue_.WillAcceptCommand(cmd.cmd_type, rank,
                                             cmd.Bankgroup(), cmd.Bank())) {
            if (cmd.IsWrite()) {
                // Enforce R->W dependency
                if (pending_rd_q_.count(it->addr) > 0) {
                    // if there is read transaction (it->addr),
                    // first push it
                    if (read_queue_.size() > 0) {
                        for (int i = 0; i < read_queue_.size(); i++) {
                            if (read_queue_[i].addr == it->addr) {
                                // PrintDebug("(ScheduleTransaction) R->W
                                // dependency:!", it->addr);
                                rw_dependency_lock_ = true;
                                rw_dependency_addr_ = it->addr;
                                break;
                            }
                        }
                    }
                    write_draining_ = 0;
                    break;
                } else if (pending_pim_q_.count(it->addr) > 0) {
                    auto pim_trans_ = pending_pim_q_.find(cmd.hex_addr);
                    if (pim_trans_->second.added_cycle < it->added_cycle) {
                        write_draining_ = 0;
                        break;
                    }
                }
                write_draining_ -= 1;
            }
            if (cmd.IsRead()) {
                if (rw_dependency_lock_ &&
                    rw_dependency_addr_ == cmd.hex_addr) {
                    // PrintDebug("(ScheduleTransaction) Solve R->W
                    // dependency:!", it->addr);
                    rw_dependency_addr_ = 0;
                    rw_dependency_lock_ = false;
                }
            }

            if (cmd.IsRead() && !pim_first_) {
                int remaining_macs =
                    mac_states_[cmd.Rank() * config_.bankgroups +
                                cmd.Bankgroup()]
                        .remaining_macs;
                if (compute_mode_ == ComputeMode::ASYNC) {
                    if (mac_states_[cmd.Rank() * config_.bankgroups +
                                    cmd.Bankgroup()]
                            .is_active &&
                        remaining_macs <= config_.mem_first_threshold &&
                        remaining_macs != 0) {
                        continue;
                    }
                } else if (compute_mode_ == ComputeMode::ALL_BANK) {
                    int num_waiting_comp = 0;
                    for (auto it = pim_cmd_queue_.pim_queue_.begin();
                         it != pim_cmd_queue_.pim_queue_.end(); it++) {
                        if (it->cmd_type == CommandType::COMP) {
                            num_waiting_comp++;
                            if (num_waiting_comp > config_.mem_first_threshold)
                                break;
                        }
                    }
                    if (num_waiting_comp < config_.mem_first_threshold &&
                        num_waiting_comp != 0)
                        continue;
                }
            }

            pim_cmd_queue_.AddCommand(cmd);
            if (compute_mode_ == ComputeMode::ASYNC && cmd.Bankgroup() != -1 &&
                !cmd.IsPIMCommand()) {
                if (mac_states_[cmd.Rank() * config_.bankgroups +
                                cmd.Bankgroup()]
                        .is_active) {
                    bool same_subarray = false;
                    if (std::abs(
                            Subarray(cmd.Row()) -
                            Subarray(
                                mac_states_[cmd.Rank() * config_.bankgroups +
                                            cmd.Bankgroup()]
                                    .addr.row)) <= 1)
                        same_subarray = true;

                    if (bank_mode_ != BankMode::DPSA || same_subarray) {
                        bool for_pim = false;
                        if (bank_mode_ == BankMode::DPSA) for_pim = true;
                        // delete existing MACINTR command
                        Command mac_intr_cmd = Command(
                            CommandType::MACINTR,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .addr,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .hex_addr,
                            for_pim, 0);
                        pim_cmd_queue_.ErasePIMCommand(mac_intr_cmd);
                        // add MACINTR command to stop the current MAC
                        // operation.
                        mac_intr_cmd = Command(
                            CommandType::MACINTR,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .addr,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .hex_addr,
                            for_pim,
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .remaining_macs);

                        pim_cmd_queue_.AddCommand(mac_intr_cmd);
                        channel_state_.UpdateTiming(mac_intr_cmd, clk_);
                        mac_states_[cmd.Rank() * config_.bankgroups +
                                    cmd.Bankgroup()]
                            .is_active = false;

                        // add MAC command to complete remaining MAC operation.
                        int remaining_macs =
                            mac_states_[cmd.Rank() * config_.bankgroups +
                                        cmd.Bankgroup()]
                                .remaining_macs;
                        if (remaining_macs > 0) {
                            Address addr(
                                mac_states_[cmd.Rank() * config_.bankgroups +
                                            cmd.Bankgroup()]
                                    .addr);
                            addr.column =
                                addr.column +
                                (mac_states_[cmd.Rank() * config_.bankgroups +
                                             cmd.Bankgroup()]
                                     .num_macs -
                                 remaining_macs);

                            Command mac_cmd = Command(
                                CommandType::MAC, addr,
                                mac_states_[cmd.Rank() * config_.bankgroups +
                                            cmd.Bankgroup()]
                                    .hex_addr,
                                for_pim, remaining_macs);

                            pim_cmd_queue_.AddCommand(mac_cmd);
                        }
                    }
                }
            }
            queue.erase(it);
            break;
        }
    }
}

void PIMController::IssueCommand(const Command &cmd) {
    PrintControllerLog("IssueCommand", channel_id_, clk_, cmd);

    last_issue_clk_ = clk_;
    // if read/write, update pending queue and return queue
    if (cmd.IsRead()) {
        auto num_reads = pending_rd_q_.count(cmd.hex_addr);
        if (num_reads == 0) {
            std::cerr << cmd.hex_addr << " not in read queue! " << std::endl;
            exit(1);
        }
        // if there are multiple reads pending return them all
        while (num_reads > 0) {
            auto it = pending_rd_q_.find(cmd.hex_addr);
            it->second.complete_cycle = clk_ + config_.read_delay;
            return_queue_.push_back(it->second);
            pending_rd_q_.erase(it);
            num_reads -= 1;
        }
    } else if (cmd.IsWrite()) {
        // there should be only 1 write to the same location at a time
        auto it = pending_wr_q_.find(cmd.hex_addr);
        if (it == pending_wr_q_.end()) {
            std::cerr << cmd.hex_addr << " not in write queue!" << std::endl;
            exit(1);
        }
        auto wr_lat = clk_ - it->second.added_cycle + config_.write_delay;
        simple_stats_.AddValue("write_latency", wr_lat);
        pending_wr_q_.erase(it);
    } else if ((cmd.IsReadRes() || cmd.IsGwrite() || cmd.IsPIMComp()) &&
               cmd.cmd_type != CommandType::MACINTR) {
        int mac_pipeline = 6;
        if (cmd.cmd_type == CommandType::MAC &&
            mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                    .remaining_macs > 0) {
            mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                .is_active = true;
            mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                .num_macs = cmd.num_macs;
            mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                .remaining_macs = cmd.num_macs;
            mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                .last_mac_cycle = clk_;
            mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                .addr = cmd.addr;
            mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                .hex_addr = cmd.hex_addr;

            bool found = false;

            for (auto &trans : return_queue_) {
                if (trans.req_type == TransactionType::MAC &&
                    trans.addr == cmd.hex_addr) {
                    trans.complete_cycle =
                        clk_ +
                        config_.tCCD_L * (cmd.num_macs + mac_pipeline - 1);
                    found = true;
                    break;
                }
            }

            if (!found) {
                PrintError("cid:", channel_id_, "Not in the return queue",
                           cmd.CommandTypeString(),
                           "addr:", HexString(cmd.hex_addr));
            }
            // when MAC cmd is issued ,MACINTR should be added to
            // pim_cmd_queue_
            Command mac_intr_cmd =
                Command(CommandType::MACINTR, cmd.addr, cmd.hex_addr,
                        cmd.for_pim, cmd.num_macs);
            pim_cmd_queue_.AddCommand(mac_intr_cmd);
        } else {
            auto num_pending_pim_trans = pending_pim_q_.count(cmd.hex_addr);
            if (num_pending_pim_trans == 0) {
                PrintError("cid:", channel_id_, "Not in pending pim queue!",
                           cmd.CommandTypeString(),
                           "addr:", HexString(cmd.hex_addr));
            }

            auto it = pending_pim_q_.find(cmd.hex_addr);
            // readres delay == read delay
            if (cmd.IsReadRes())
                it->second.complete_cycle = clk_ + config_.read_delay;
            if (cmd.IsGwrite())
                it->second.complete_cycle =
                    clk_ + config_.tCCD_S * cmd.num_macs;
            if (cmd.cmd_type == CommandType::COMP)
                it->second.complete_cycle =
                    clk_ + config_.tCCD_L * mac_pipeline;
            if (cmd.cmd_type == CommandType::MAC) {
                mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                    .is_active = true;
                mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                    .num_macs = cmd.num_macs;
                mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                    .remaining_macs = cmd.num_macs;
                mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                    .last_mac_cycle = clk_;
                mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                    .addr = cmd.addr;
                mac_states_[cmd.Rank() * config_.bankgroups + cmd.Bankgroup()]
                    .hex_addr = cmd.hex_addr;
                it->second.complete_cycle =
                    clk_ + config_.tCCD_L * (cmd.num_macs + mac_pipeline - 1);
                // when MAC cmd is issued ,MACINTR should be added to
                // pim_cmd_queue_
                Command mac_intr_cmd =
                    Command(CommandType::MACINTR, cmd.addr, cmd.hex_addr,
                            cmd.for_pim, cmd.num_macs);
                pim_cmd_queue_.AddCommand(mac_intr_cmd);
                // channel_state_.UpdateTiming(mac_intr_cmd, clk_);
            }

            return_queue_.push_back(it->second);
            pending_pim_q_.erase(it);
        }
    } else if (cmd.cmd_type == CommandType::MACINTR) {
        if (cmd.bankgroup_mask == 0) {
            for (auto &trans : return_queue_) {
                if (trans.req_type == TransactionType::MAC &&
                    trans.addr == cmd.hex_addr &&
                    mac_states_[cmd.Rank() * config_.bankgroups +
                                cmd.Bankgroup()]
                            .remaining_macs != 0) {
                    trans.complete_cycle = std::numeric_limits<uint64_t>::max();
                    break;
                }
            }
        } else {
            for (auto &trans : return_queue_) {
                if (trans.req_type == TransactionType::MAC) {
                    Address addr = config_.AddressMapping(trans.addr);
                    int bg = addr.bankgroup;
                    if (bg >= 0 &&
                        (cmd.bankgroup_mask &
                         (1 << config_.AddressMapping(trans.addr).rank *
                                       config_.bankgroups +
                                   bg)) &&
                        mac_states_[config_.AddressMapping(trans.addr).rank *
                                        config_.bankgroups +
                                    bg]
                                .remaining_macs != 0) {
                        trans.complete_cycle =
                            std::numeric_limits<uint64_t>::max();
                    }
                }
            }
        }
    }
    // must update stats before states (for row hits)
    UpdateCommandStats(cmd);
    channel_state_.UpdateTimingAndStates(cmd, clk_);
}

// - [x] translate PIM transaction to command
Command PIMController::TransToCommand(const Transaction &trans) {
    bool for_pim = false;
    uint32_t num_macs = 0;
    auto addr = config_.AddressMapping(trans.addr);
    CommandType cmd_type;
    if (row_buf_policy_ == RowBufPolicy::OPEN_PAGE) {
        switch (trans.req_type) {
            case TransactionType::READ:
                cmd_type = CommandType::READ;
                break;
            case TransactionType::WRITE:
                cmd_type = CommandType::WRITE;
                break;
            case TransactionType::D2GWRITE:
                cmd_type = CommandType::D2GWRITE;
                break;
            case TransactionType::H2GWRITE:
                addr.rank = -1;
                addr.bankgroup = -1;
                addr.bank = -1;
                cmd_type = CommandType::H2GWRITE;
                num_macs = trans.num_macs;

                return Command(cmd_type, addr, trans.addr, for_pim, num_macs);

                break;
            case TransactionType::COMP:
                addr.rank = -1;
                if (compute_mode_ == ComputeMode::ALL_BANK) addr.bankgroup = -1;
                addr.bank = -1;
                cmd_type = CommandType::COMP;
                if (bank_mode_ == BankMode::DPSA) for_pim = true;
                num_macs = trans.num_macs;
                return Command(cmd_type, addr, trans.addr, for_pim, num_macs);

                break;
            case TransactionType::MAC: {
                if (compute_mode_ == ComputeMode::ALL_BANK) addr.bankgroup = -1;
                addr.bank = -1;
                cmd_type = CommandType::MAC;
                if (bank_mode_ == BankMode::DPSA) for_pim = true;
                // num_macs = (config_.columns - addr.column) / config_.BL;
                num_macs = trans.num_macs;
                Command cmd =
                    Command(cmd_type, addr, trans.addr, for_pim, num_macs);
                cmd.is_first_comps = true;
                return cmd;
            }
            case TransactionType::READRES:
                return DecodePIMTransaction(trans);
            default:
                break;
        }
    } else {
        cmd_type = trans.is_write() ? CommandType::WRITE_PRECHARGE
                                    : CommandType::READ_PRECHARGE;
    }

    return Command(cmd_type, addr, trans.addr);
}

Command PIMController::DecodePIMTransaction(const Transaction &trans) {
    assert(trans.req_type == TransactionType::READRES);
    CommandType cmd_type = CommandType::READRES;

    auto addr = config_.AddressMapping(trans.addr);
    int num_comps = 0;

    num_comps += addr.rank * config_.bankgroups * config_.banks_per_group;
    num_comps += addr.bankgroup * config_.banks_per_group;
    num_comps += addr.bank;
    num_comps += 1;
    // we have only 5 bits usable,
    // encode (num_comps-1) to rabgba bit

    if (compute_mode_ == ComputeMode::ALL_BANK) {
        addr.rank = -1;
        addr.bankgroup = -1;
    }
    addr.bank = -1;
    uint32_t num_macs = 0;
    // fix num_readres to 1
    return Command(cmd_type, addr, trans.addr, false, num_comps, num_macs);
}

int PIMController::QueueUsage() const { return pim_cmd_queue_.QueueUsage(); }

void PIMController::PrintEpochStats() {
    simple_stats_.Increment("epoch_num");
    simple_stats_.PrintEpochStats();

    return;
}

void PIMController::PrintFinalStats() {
    simple_stats_.PrintFinalStats();

    return;
}

// - [x] add PIM command stats
void PIMController::UpdateCommandStats(const Command &cmd) {
    switch (cmd.cmd_type) {
        case CommandType::READ:
        case CommandType::READ_PRECHARGE:
            simple_stats_.Increment("num_read_cmds");
            if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_read_row_hits");
            }
            break;
        case CommandType::WRITE:
        case CommandType::WRITE_PRECHARGE:
            simple_stats_.Increment("num_write_cmds");
            if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_write_row_hits");
            }
            break;
        case CommandType::ACTIVATE:
            simple_stats_.Increment("num_act_cmds");
            break;
        case CommandType::PRECHARGE:
            simple_stats_.Increment("num_pre_cmds");
            break;
        case CommandType::REFRESH:
            simple_stats_.Increment("num_ref_cmds");
            break;
        case CommandType::REFRESH_BANK:
            simple_stats_.Increment("num_refb_cmds");
            break;
        case CommandType::SREF_ENTER:
            simple_stats_.Increment("num_srefe_cmds");
            break;
        case CommandType::SREF_EXIT:
            simple_stats_.Increment("num_srefx_cmds");
            break;
        case CommandType::D2GWRITE:
        case CommandType::H2GWRITE:
            simple_stats_.Increment("num_gwrite_cmds");
            break;
        case CommandType::COMP:
            simple_stats_.Increment("num_comp_cmds");
            break;
        case CommandType::MAC:
            if (cmd.is_first_comps) {
                for (size_t i = 0; i < cmd.num_macs; i++)
                    simple_stats_.Increment("num_comp_cmds");
            }
            break;
        case CommandType::READRES:
            simple_stats_.Increment("num_readres_cmds");
            break;
        case CommandType::MACINTR:
            break;
        case CommandType::PWRITE:
            // simple_stats_.Increment("num_pim_precharge_cmds");
            break;
        default:
            PrintError(cmd.CommandTypeString());
            AbruptExit(__FILE__, __LINE__);
    }
}

}  // namespace dramsim3
