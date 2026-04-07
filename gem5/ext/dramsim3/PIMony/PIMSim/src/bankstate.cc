#include "bankstate.h"

namespace dramsim3 {

BankState::BankState(BankMode bank_mode, int rows_in_subarray)
    : state_(State::CLOSED),
      pim_state_(State::CLOSED),
      bank_mode_(bank_mode),
      pim_open_row_(-1),
      cmd_timing_(static_cast<int>(CommandType::SIZE)),
      cmd_timing_diff_subarray(static_cast<int>(CommandType::SIZE)),
      cmd_timing_mac_(static_cast<int>(CommandType::SIZE)),
      cmd_timing_mac_diff_subarray(static_cast<int>(CommandType::SIZE)),
      rows_in_subarray_(rows_in_subarray),
      precharging_subarray_(-1),
      open_row_(-1),
      row_hit_count_(0),
      pim_enter_count_(0) {
    cmd_timing_[static_cast<int>(CommandType::READ)] = 0;
    cmd_timing_[static_cast<int>(CommandType::READ_PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::WRITE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::WRITE_PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::ACTIVATE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::REFRESH)] = 0;
    cmd_timing_[static_cast<int>(CommandType::SREF_ENTER)] = 0;
    cmd_timing_[static_cast<int>(CommandType::SREF_EXIT)] = 0;
    cmd_timing_[static_cast<int>(CommandType::D2GWRITE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::H2GWRITE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::COMP)] = 0;
    cmd_timing_[static_cast<int>(CommandType::MAC)] = 0;
    cmd_timing_[static_cast<int>(CommandType::MACINTR)] = 0;
    cmd_timing_[static_cast<int>(CommandType::READRES)] = 0;
    cmd_timing_[static_cast<int>(CommandType::PWRITE)] = 0;

    // store seperately for mac operation
    cmd_timing_mac_[static_cast<int>(CommandType::READ)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::READ_PRECHARGE)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::WRITE)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::WRITE_PRECHARGE)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::ACTIVATE)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::PRECHARGE)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::REFRESH)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::SREF_ENTER)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::SREF_EXIT)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::D2GWRITE)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::H2GWRITE)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::COMP)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::MAC)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::MACINTR)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::READRES)] = 0;
    cmd_timing_mac_[static_cast<int>(CommandType::PWRITE)] = 0;

    // store seperately for DPSA mode
    cmd_timing_diff_subarray[static_cast<int>(CommandType::READ)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::READ_PRECHARGE)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::WRITE)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::WRITE_PRECHARGE)] =
        0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::ACTIVATE)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::PRECHARGE)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::REFRESH)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::SREF_ENTER)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::SREF_EXIT)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::D2GWRITE)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::H2GWRITE)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::COMP)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::MAC)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::MACINTR)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::READRES)] = 0;
    cmd_timing_diff_subarray[static_cast<int>(CommandType::PWRITE)] = 0;

    // store seperately for mac operation
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::READ)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(
        CommandType::READ_PRECHARGE)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::WRITE)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(
        CommandType::WRITE_PRECHARGE)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::ACTIVATE)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::PRECHARGE)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::REFRESH)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::SREF_ENTER)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::SREF_EXIT)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::D2GWRITE)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::H2GWRITE)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::COMP)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::MAC)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::MACINTR)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::READRES)] = 0;
    cmd_timing_mac_diff_subarray[static_cast<int>(CommandType::PWRITE)] = 0;

    pim_act_done_timing = 0;
}

int BankState::Subarray(const int row) const {
    if (row == -1) return -1;
    return int(row / rows_in_subarray_);
}

/* Return the cmd required before issuing the cmd in the current state */
Command BankState::GetReadyCommand(const Command &cmd, uint64_t clk) {
    Command ready_cmd = GetReadyCommandSingle(cmd, clk);
    if (ready_cmd.cmd_type == CommandType::PRECHARGE) {
        int precharge_row = -1;
        if (cmd.for_pim)
            precharge_row = pim_open_row_;
        else
            precharge_row = open_row_;

        precharging_subarray_ = Subarray(precharge_row);
    }
    return ready_cmd;
}

void BankState::UpdateState(const Command &cmd) {
    UpdateStateSingle(cmd);
    return;
}

void BankState::UpdateTiming(CommandType cmd_type, uint64_t time) {
    cmd_timing_[static_cast<int>(cmd_type)] =
        std::max(cmd_timing_[static_cast<int>(cmd_type)], time);
    return;
}

void BankState::UpdateDiffSubarrayTiming(CommandType cmd_type, uint64_t time) {
    cmd_timing_diff_subarray[static_cast<int>(cmd_type)] =
        std::max(cmd_timing_diff_subarray[static_cast<int>(cmd_type)], time);
    return;
}

void BankState::UpdateMACTiming(CommandType cmd_type, uint64_t time) {
    cmd_timing_mac_[static_cast<int>(cmd_type)] =
        std::max(cmd_timing_mac_[static_cast<int>(cmd_type)], time);
    return;
}

void BankState::UpdateDiffSubarrayMACTiming(CommandType cmd_type,
                                            uint64_t time) {
    cmd_timing_mac_diff_subarray[static_cast<int>(cmd_type)] = std::max(
        cmd_timing_mac_diff_subarray[static_cast<int>(cmd_type)], time);
    return;
}

void BankState::UpdatePIMACTdoneTiming(uint64_t time) {
    pim_act_done_timing = std::max(pim_act_done_timing, time);
    return;
}

void BankState::resetMACTiming(CommandType cmd_type) {
    cmd_timing_mac_[static_cast<int>(cmd_type)] = 0;
    return;
}

void BankState::resetDiffSubarrayMACTiming(CommandType cmd_type) {
    cmd_timing_mac_diff_subarray[static_cast<int>(cmd_type)] = 0;
    return;
}
/* Return the cmd required before issuing the cmd in the current state */
Command BankState::GetReadyCommandSingle(const Command &cmd, uint64_t clk) {
    CommandType required_type = CommandType::SIZE;
    State state;
    int open_row = -1;

    if (cmd.for_pim) {
        state = pim_state_;
        if (state_ == State::OPEN &&
            Subarray(cmd.Row()) == Subarray(open_row_)) {
            state = state_;
        }
    } else {
        state = state_;

        if (pim_state_ == State::OPEN &&
            Subarray(cmd.Row()) == Subarray(pim_open_row_)) {
            state = pim_state_;
        }
    }
    switch (state) {
        case State::CLOSED:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::WRITE:
                case CommandType::D2GWRITE:
                    required_type = CommandType::ACTIVATE;
                    break;
                case CommandType::REFRESH:
                    if (pim_state_ == State::CLOSED) {
                        required_type = cmd.cmd_type;
                    } else {
                        required_type = CommandType::PRECHARGE;
                        bool for_pim = true;
                        return Command(required_type, cmd.addr, cmd.hex_addr,
                                       for_pim, cmd.num_macs);
                    }
                    break;

                case CommandType::H2GWRITE:
                case CommandType::READRES:
                case CommandType::MACINTR:
                    required_type = cmd.cmd_type;
                    break;
                case CommandType::COMP:
                case CommandType::MAC:
                    required_type = CommandType::ACTIVATE;
                    break;
                default:
                    PrintWarning("(GetReadyCommandSingle) Unknown type! addr: ",
                                 HexString(cmd.hex_addr),
                                 "channel:", cmd.Channel(), "rank:", cmd.Rank(),
                                 "bg:", cmd.Bankgroup(), "bank:", cmd.Bank());
                    PrintStateAndCommand(cmd);
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::OPEN:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::WRITE:
                    if (cmd.Row() == open_row_) {
                        required_type = cmd.cmd_type;
                    } else {
                        required_type = CommandType::PRECHARGE;
                    }
                    break;
                case CommandType::D2GWRITE:
                    if (cmd.Row() == open_row_) {
                        required_type = cmd.cmd_type;
                    } else {
                        required_type = CommandType::PRECHARGE;
                    }
                    break;

                case CommandType::REFRESH:
                    required_type = CommandType::PRECHARGE;
                    break;
                case CommandType::COMP:
                case CommandType::MAC:
                    if (cmd.for_pim)
                        open_row = pim_open_row_;
                    else
                        open_row = open_row_;
                    if (cmd.Row() == open_row) {
                        required_type = cmd.cmd_type;

                    } else {
                        required_type = CommandType::PRECHARGE;
                    }
                    break;
                case CommandType::READRES:
                case CommandType::H2GWRITE:
                case CommandType::MACINTR:
                    required_type = cmd.cmd_type;
                    break;
                default:
                    std::cerr << "(GetReadyCommandSingle) Unknown type!"
                              << std::endl;
                    PrintStateAndCommand(cmd);
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::PD:
        case State::SIZE:
            std::cerr << "In unknown state" << std::endl;
            PrintStateAndCommand(cmd);
            AbruptExit(__FILE__, __LINE__);
            break;
    }
    bool ready = false;
    if (required_type != CommandType::SIZE) {
        // check it refers same subarray between PIM and normal access
        bool same_subarray = false;
        // check it is same row intra PIM or Normal accesses
        bool same_row = false;

        if (cmd.for_pim) {
            if (std::abs(Subarray(cmd.Row()) - Subarray(open_row_)) <= 1)
                same_subarray = true;
            if (cmd.Row() == pim_open_row_) same_row = true;
        } else {
            if (std::abs(Subarray(cmd.Row()) - Subarray(pim_open_row_)) <= 1)
                same_subarray = true;
            if (Subarray(cmd.Row()) == Subarray(open_row_)) same_row = true;
        }

        if (required_type == CommandType::ACTIVATE &&
            precharging_subarray_ == Subarray(cmd.Row()))
            same_row = true;

        if (bank_mode_ == BankMode::DPSA) {
            if (same_subarray && same_row) {
                if (clk >=
                    std::max(
                        cmd_timing_[static_cast<int>(required_type)],
                        cmd_timing_mac_[static_cast<int>(required_type)])) {
                    ready = true;
                }
            } else if (!same_subarray && !same_row) {
                if (clk >=
                    std::max(cmd_timing_diff_subarray[static_cast<int>(
                                 required_type)],
                             cmd_timing_mac_diff_subarray[static_cast<int>(
                                 required_type)])) {
                    ready = true;
                }
            } else if ((cmd.for_pim && !same_subarray && same_row)) {
                if (clk >=
                    std::max(
                        std::max(pim_act_done_timing,
                                 cmd_timing_diff_subarray[static_cast<int>(
                                     required_type)]),
                        cmd_timing_mac_[static_cast<int>(required_type)])) {
                    ready = true;
                }

            } else if ((!cmd.for_pim && same_subarray && !same_row)) {
                if (clk >=
                    std::max(
                        cmd_timing_diff_subarray[static_cast<int>(
                            required_type)],
                        cmd_timing_mac_[static_cast<int>(required_type)])) {
                    ready = true;
                }
            } else {
                if (clk >=
                    std::max(cmd_timing_[static_cast<int>(required_type)],
                             cmd_timing_mac_diff_subarray[static_cast<int>(
                                 required_type)])) {
                    ready = true;
                }
            }

        } else {
            if (clk >=
                std::max(cmd_timing_[static_cast<int>(required_type)],
                         cmd_timing_mac_[static_cast<int>(required_type)])) {
                ready = true;
            }
        }
    }
    if (ready)
        return Command(required_type, cmd.addr, cmd.hex_addr, cmd.for_pim,
                       cmd.is_first_comps, cmd.num_comps, cmd.num_macs);
    else
        return Command();
}

void BankState::UpdateStateSingle(const Command &cmd) {
    State state;
    if (cmd.for_pim)
        state = pim_state_;
    else
        state = state_;

    switch (state) {
        case State::OPEN:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::WRITE:
                    row_hit_count_++;
                    break;
                case CommandType::READRES:
                case CommandType::D2GWRITE:
                case CommandType::H2GWRITE:
                case CommandType::COMP:
                case CommandType::MAC:
                case CommandType::MACINTR:
                    break;
                case CommandType::PRECHARGE:

                    if (cmd.for_pim) {
                        pim_state_ = State::CLOSED;
                        pim_open_row_ = -1;
                    } else {
                        state_ = State::CLOSED;
                        open_row_ = -1;
                        row_hit_count_ = 0;
                    }
                    break;

                case CommandType::ACTIVATE:
                case CommandType::REFRESH:
                    if (pim_open_row_) {
                        PrintError("pim row is still open");
                    }
                default:
                    std::cout
                        << "(UpdateStateSingle) addr: "
                        << HexString(cmd.hex_addr) << " rank:" << cmd.Rank()
                        << " bg:" << cmd.Bankgroup() << " bank:" << cmd.Bank()
                        << " ";
                    PrintStateAndCommand(cmd);
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        case State::CLOSED:
            switch (cmd.cmd_type) {
                case CommandType::REFRESH:
                    break;
                case CommandType::ACTIVATE:
                    if (cmd.for_pim) {
                        pim_state_ = State::OPEN;
                        pim_open_row_ = cmd.Row();
                        break;
                    } else {
                        state_ = State::OPEN;
                        open_row_ = cmd.Row();
                        break;
                    }
                case CommandType::SREF_ENTER:
                    state_ = State::SREF;
                    break;

                case CommandType::H2GWRITE:
                case CommandType::READRES:
                    break;
                case CommandType::PRECHARGE:
                    if (bank_mode_ == BankMode::DPSA) {
                        if (Subarray(cmd.Row()) == Subarray(open_row_)) {
                            state_ = State::CLOSED;
                            open_row_ = -1;
                            row_hit_count_ = 0;
                        }
                        if (Subarray(cmd.Row()) == Subarray(pim_open_row_)) {
                            pim_state_ = State::CLOSED;
                            pim_open_row_ = -1;
                        }
                        break;
                    }
                case CommandType::READ:
                case CommandType::WRITE:
                case CommandType::D2GWRITE:
                case CommandType::COMP:
                case CommandType::MAC:
                case CommandType::MACINTR:
                default:
                    std::cout
                        << "(UpdateStateSingle) addr: "
                        << HexString(cmd.hex_addr) << " rank:" << cmd.Rank()
                        << " bg:" << cmd.Bankgroup() << " bank:" << cmd.Bank()
                        << " ";
                    PrintStateAndCommand(cmd);
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        default:
            std::cout << "(UpdateStateSingle) addr: " << HexString(cmd.hex_addr)
                      << " rank:" << cmd.Rank() << " bg:" << cmd.Bankgroup()
                      << " bank:" << cmd.Bank() << " ";
            PrintStateAndCommand(cmd);
            AbruptExit(__FILE__, __LINE__);
    }
    return;
}

}  // namespace dramsim3
