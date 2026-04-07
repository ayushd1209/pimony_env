#ifndef __BANKSTATE_H
#define __BANKSTATE_H

#include <vector>

#include "common.h"

namespace dramsim3 {

class BankState {
   public:
    BankState(BankMode bank_mode, int rows_in_subarray);
    /* bank state description */
    /* The row corresponding to OPEN: open_row_ is active */
    /* CLOSED: No row is open, active */
    /* SREF: self-refresh status, waiting for refresh to end.
     * * SREF_EXIT command.  */
    /* PIM: State for PIM computation. (new)  */
    /* PD, SIZE: invalid state. */
    enum class State { OPEN, CLOSED, SREF, PD, SIZE };
    enum class StateDouble {
        OPEN_PIMOPEN = int(State::OPEN) * 10 + int(State::OPEN),
        OPEN_PIMCLOSED = int(State::OPEN) * 10 + int(State::CLOSED),
        CLOSED_PIMOPEN = int(State::CLOSED) * 10 + int(State::OPEN),
        CLOSED_PIMCLOSED = int(State::CLOSED) * 10 + int(State::CLOSED),
    };
    Command GetReadyCommand(const Command &cmd, uint64_t clk);

    // Update the state of the bank resulting after the execution of the command
    void UpdateState(const Command &cmd);

    // Update the existing timing constraints for the command
    void UpdateTiming(const CommandType cmd_type, uint64_t time);
    void UpdateDiffSubarrayTiming(const CommandType cmd_type, uint64_t time);
    void UpdateMACTiming(CommandType cmd_type, uint64_t time);
    void UpdateDiffSubarrayMACTiming(CommandType cmd_type, uint64_t time);
    void UpdatePIMACTdoneTiming(uint64_t time);
    void resetMACTiming(CommandType cmd_type);
    void resetDiffSubarrayMACTiming(CommandType cmd_type);
    bool IsRowOpen() const { return state_ == State::OPEN; }
    bool IsUsed() const { return IsRowOpen(); }
    bool IsPIMUsed() const { return pim_state_ == State::OPEN; }
    int OpenRow() const { return open_row_; }
    int PIMOpenRow() const {
        if (bank_mode_ == BankMode::SINGLE)
            return open_row_;
        else
            return pim_open_row_;
    }
    int RowHitCount() const { return row_hit_count_; }
    std::string StateToString() const {
        switch (state_) {
            case State::OPEN:
                return "OPEN";
                break;
            case State::CLOSED:
                return "CLOSED";
                break;
            case State::SREF:
                return "SREF";
                break;
            case State::PD:
                return "PD";
                break;
            case State::SIZE:
                return "SIZE";
                break;
            default:
                return "UNKNOWN STATE";
                break;
        }
    }
    std::string MemoryStateToString() const {
        switch (state_) {
            case State::OPEN:
                return "OPEN";
                break;
            case State::CLOSED:
                return "CLOSED";
                break;
            case State::SREF:
                return "SREF";
                break;
            case State::PD:
                return "PD";
                break;
            case State::SIZE:
                return "SIZE";
                break;
            default:
                return "UNKNOWN STATE";
                break;
        }
    }

    std::string PIMStateToString() const {
        switch (pim_state_) {
            case State::OPEN:
                return "OPEN";
                break;
            case State::CLOSED:
                return "CLOSED";
                break;
            case State::SREF:
                return "SREF";
                break;
            case State::PD:
                return "PD";
                break;
            case State::SIZE:
                return "SIZE";
                break;
            default:
                return "UNKNOWN STATE";
                break;
        }
    }

   private:
    int Subarray(const int row) const;
    // Current state of the Bank
    // Apriori or instantaneously transitions on a command.
    State state_;      // normal bank state
    State pim_state_;  // pim bank state
    BankMode bank_mode_;

    // Earliest time when the particular Command can be executed in this bank
    std::vector<uint64_t> cmd_timing_;
    std::vector<uint64_t> cmd_timing_mac_;
    std::vector<uint64_t> cmd_timing_diff_subarray;
    std::vector<uint64_t> cmd_timing_mac_diff_subarray;

    uint64_t pim_act_done_timing;

    // DPSA variables
    int rows_in_subarray_;
    int precharging_subarray_;

    // Currently open row
    int open_row_;      // open row of row buffer
    int pim_open_row_;  // open row of pim row buffer

    // consecutive accesses to one row
    int row_hit_count_;

    // PIM Command set count
    int pim_enter_count_;

    void PrintStateAndCommand(Command cmd) const {
        PrintError("cid:", cmd.Channel(), "PIM State:", MemoryStateToString(),
                   ", Command:", cmd.CommandTypeString());
    }

    Command GetReadyCommandSingle(const Command &cmd, uint64_t clk);
    void UpdateStateSingle(const Command &cmd);
};

}  // namespace dramsim3
#endif
