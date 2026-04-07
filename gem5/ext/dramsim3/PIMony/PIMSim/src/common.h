#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace dramsim3 {

struct Address {
    Address()
        : channel(-1), rank(-1), bankgroup(-1), bank(-1), row(-1), column(-1) {}
    Address(int channel, int rank, int bankgroup, int bank, int row, int column)
        : channel(channel),
          rank(rank),
          bankgroup(bankgroup),
          bank(bank),
          row(row),
          column(column) {}
    Address(const Address &addr)
        : channel(addr.channel),
          rank(addr.rank),
          bankgroup(addr.bankgroup),
          bank(addr.bank),
          row(addr.row),
          column(addr.column) {}
    int channel;
    int rank;
    int bankgroup;
    int bank;
    int row;
    int sub;
    int column;
};

inline uint32_t ModuloWidth(uint64_t addr, uint32_t bit_width, uint32_t pos) {
    addr >>= pos;
    auto store = addr;
    addr >>= bit_width;
    addr <<= bit_width;
    return static_cast<uint32_t>(store ^ addr);
}

extern uint32_t g_bankgroup_bit_width;
std::string dynamic_bit_string(uint32_t value, uint32_t bit_width);

// extern std::function<Address(uint64_t)> AddressMapping;
int GetBitInPos(uint64_t bits, int pos);
// it's 2017 and c++ std::string still lacks a split function, oh well
std::vector<std::string> StringSplit(const std::string &s, char delim);
template <typename Out>
void StringSplit(const std::string &s, char delim, Out result);

int LogBase2(int power_of_two);
void AbruptExit(const std::string &file, int line);
bool DirExist(std::string dir);
enum class Color { RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, DEFAULT, RESET };

std::string ColorString(Color color);

enum class CommandType {
    READ,
    READ_PRECHARGE,
    WRITE,
    WRITE_PRECHARGE,
    ACTIVATE,
    PRECHARGE,
    REFRESH_BANK,
    REFRESH,
    SREF_ENTER,
    SREF_EXIT,
    D2GWRITE,
    H2GWRITE,
    COMP,
    MAC,
    MACINTR,
    READRES,
    PWRITE,
    SIZE
};

struct Command {
    Command() : cmd_type(CommandType::SIZE), hex_addr(0) {}
    Command(CommandType cmd_type, const Address &addr, uint64_t hex_addr)
        : cmd_type(cmd_type),
          addr(addr),
          hex_addr(hex_addr),
          for_gwrite(false),
          is_first_comps(false),
          num_comps(0),
          num_readres(0),
          num_macs(0) {}
    // for MAC
    Command(CommandType cmd_type, const Address &addr, uint64_t hex_addr,
            bool for_pim, uint32_t num_macs)
        : cmd_type(cmd_type),
          addr(addr),
          hex_addr(hex_addr),
          for_pim(for_pim),
          for_gwrite(false),
          is_first_comps(false),
          num_comps(0),
          num_readres(0),
          num_macs(num_macs) {}
    // for MACINTR
    Command(CommandType cmd_type, const Address &addr, uint64_t hex_addr,
            bool for_pim, uint32_t num_macs, uint8_t bankgroup_mask)
        : cmd_type(cmd_type),
          addr(addr),
          hex_addr(hex_addr),
          for_pim(for_pim),
          for_gwrite(false),
          is_first_comps(false),
          num_comps(0),
          num_readres(0),
          num_macs(num_macs),
          bankgroup_mask(bankgroup_mask) {}

    Command(CommandType cmd_type, const Address &addr, uint64_t hex_addr,
            uint32_t num_macs)
        : cmd_type(cmd_type),
          addr(addr),
          hex_addr(hex_addr),
          for_pim(false),
          for_gwrite(false),
          is_first_comps(false),
          num_comps(0),
          num_readres(0),
          num_macs(num_macs) {}

    Command(const Command &cmd)
        : cmd_type(cmd.cmd_type),
          addr(cmd.addr),
          hex_addr(cmd.hex_addr),
          for_pim(cmd.for_pim),
          for_gwrite(cmd.for_gwrite),
          num_comps(cmd.num_comps),
          num_readres(cmd.num_readres),
          num_macs(cmd.num_macs),
          is_first_comps(cmd.is_first_comps),
          bankgroup_mask(cmd.bankgroup_mask) {}

    Command(CommandType cmd_type, const Address &addr, uint64_t hex_addr,
            bool for_pim, bool is_first, int num_comps, uint32_t num_macs)
        : cmd_type(cmd_type),
          addr(addr),
          hex_addr(hex_addr),
          is_first_comps(is_first),
          for_pim(for_pim),
          for_gwrite(false),
          num_comps(num_comps),
          num_macs(num_macs) {}

    bool IsValid() const { return cmd_type != CommandType::SIZE; }
    bool IsRefresh() const {
        return cmd_type == CommandType::REFRESH ||
               cmd_type == CommandType::REFRESH_BANK;
    }
    bool IsRead() const {
        return cmd_type == CommandType::READ ||
               cmd_type == CommandType ::READ_PRECHARGE;
    }
    bool IsWrite() const {
        return cmd_type == CommandType ::WRITE ||
               cmd_type == CommandType ::WRITE_PRECHARGE;
    }
    bool IsReadWrite() const { return IsRead() || IsWrite(); }
    bool IsRankCMD() const {
        return cmd_type == CommandType::REFRESH ||
               cmd_type == CommandType::SREF_ENTER ||
               cmd_type == CommandType::SREF_EXIT;
    }

    bool IsChannelCMD() const {
        return cmd_type == CommandType::H2GWRITE ||
               cmd_type == CommandType::COMP;
    }
    bool IsPIMCommand() const {
        return cmd_type == CommandType::D2GWRITE ||
               cmd_type == CommandType::H2GWRITE ||
               cmd_type == CommandType::COMP || cmd_type == CommandType::MAC ||
               cmd_type == CommandType::MACINTR ||
               cmd_type == CommandType::READRES;
    }

    bool PIMQCommand() const {
        return cmd_type == CommandType::D2GWRITE ||
               cmd_type == CommandType::H2GWRITE ||
               cmd_type == CommandType::COMP || cmd_type == CommandType::MAC ||
               cmd_type == CommandType::MACINTR ||
               cmd_type == CommandType::READRES;
    }
    bool IsPIMComp() const {
        return cmd_type == CommandType::COMP || cmd_type == CommandType::MAC ||
               cmd_type == CommandType::MACINTR;
    }

    bool IsReadRes() const { return cmd_type == CommandType::READRES; }

    bool IsD2Gwrite() const { return cmd_type == CommandType::D2GWRITE; }
    bool IsH2Gwrite() const { return cmd_type == CommandType::H2GWRITE; }
    bool IsGwrite() const { return IsD2Gwrite() || IsH2Gwrite(); }

    CommandType cmd_type;
    Address addr;
    uint64_t hex_addr;

    // for distinguishing activation for normal or pim
    bool for_pim = false;

    // for pim_header
    bool for_gwrite;  // deprecated
    int num_comps;    // also for comps_readres
    int num_readres;
    uint32_t num_macs;
    bool is_first_comps = false;

    uint8_t bankgroup_mask = 0;

    int Channel() const { return addr.channel; }
    int Rank() const { return addr.rank; }
    int Bankgroup() const { return addr.bankgroup; }
    int Bank() const { return addr.bank; }
    int Row() const { return addr.row; }
    int Column() const { return addr.column; }

    friend std::ostream &operator<<(std::ostream &os, const Command &cmd);

    std::string CommandTypeString() const {
        switch (cmd_type) {
            case CommandType::READ:
                return "READ";
                break;
            case CommandType::READ_PRECHARGE:
                return "READ_PRECHARGE";
                break;
            case CommandType::WRITE:
                return "WRITE";
                break;
            case CommandType::WRITE_PRECHARGE:
                return "WRITE_PRECHARGE";
                break;
            case CommandType::ACTIVATE:
                return "ACTIVATE";
                break;
            case CommandType::PRECHARGE:
                return "PRECHARGE";
                break;
            case CommandType::REFRESH_BANK:
                return "REFRESH_BANK";
                break;
            case CommandType::REFRESH:
                return "REFRESH";
                break;
            case CommandType::SREF_ENTER:
                return "SREF_ENTER";
                break;
            case CommandType::SREF_EXIT:
                return "SREF_EXIT";
                break;
            case CommandType::D2GWRITE:
                return "D2GWRITE";
                break;
            case CommandType::H2GWRITE:
                return "H2GWRITE";
                break;
            case CommandType::COMP:
                return "COMP";
                break;
            case CommandType::MAC:
                return "MAC";
                break;
            case CommandType::MACINTR:
                return "MACINTR";
                break;
            case CommandType::READRES:
                return "READRES";
                break;
            case CommandType::PWRITE:
                return "PWRITE";
                break;
            case CommandType::SIZE:
                return "SIZE";
                break;

            default:
                return "UNKNOWN";
                break;
        }
    }
};

bool IsPIMCommand(CommandType cmd_type);

// MUST be same order in MemoryAccessType
enum class TransactionType {
    READ,
    WRITE,
    D2GWRITE,
    H2GWRITE,
    COMP,
    MAC,
    READRES,
    SIZE,
    SYNC
};

enum class ComputeMode { ALL_BANK, ASYNC };
enum class BankMode { SINGLE, DPSA };

struct MacState {
    bool is_active = false;  // whether MAC operation is ongoing
    int num_macs = 0;
    int remaining_macs = 0;       // number of MACs left to complete
    uint64_t last_mac_cycle = 0;  // track last operation time
    Address addr;
    uint64_t hex_addr = 0;
};

struct Transaction {
    Transaction() {}
    Transaction(uint64_t addr, TransactionType req_type)
        : addr(addr),
          added_cycle(0),
          complete_cycle(0),
          req_type(req_type),
          num_macs(0) {}
    Transaction(uint64_t addr, TransactionType req_type, uint32_t num_macs)
        : addr(addr),
          added_cycle(0),
          complete_cycle(0),
          req_type(req_type),
          num_macs(num_macs) {}
    Transaction(const Transaction &tran)
        : addr(tran.addr),
          added_cycle(tran.added_cycle),
          complete_cycle(tran.complete_cycle),
          req_type(tran.req_type),
          num_macs(tran.num_macs) {}
    uint64_t addr;
    uint64_t added_cycle;
    uint64_t complete_cycle;
    TransactionType req_type;
    uint32_t num_macs;
    bool is_dram_trans() const {
        return req_type == TransactionType::WRITE ||
               req_type == TransactionType::READ;
    }
    bool is_write() const { return req_type == TransactionType::WRITE; }
    bool is_read() const { return req_type == TransactionType::READ; }
    bool is_pim() const {
        return req_type == TransactionType::D2GWRITE ||
               req_type == TransactionType::H2GWRITE ||
               req_type == TransactionType::COMP ||
               req_type == TransactionType::MAC ||
               req_type == TransactionType::READRES;
    }

    friend std::ostream &operator<<(std::ostream &os, const Transaction &trans);
    friend std::istream &operator>>(std::istream &is, Transaction &trans);

    std::string TransactionTypeString() const {
        switch (req_type) {
            case TransactionType::READ:
                return "READ";
                break;
            case TransactionType::WRITE:
                return "WRITE";
                break;
            case TransactionType::D2GWRITE:
                return "D2GWRITE";
                break;
            case TransactionType::H2GWRITE:
                return "H2GWRITE";
                break;
            case TransactionType::COMP:
                return "COMP";
                break;
            case TransactionType::MAC:
                return "MAC";
                break;
            case TransactionType::SYNC:
                return "SYNC";
                break;
            case TransactionType::READRES:
                return "READRES";
                break;
            default:
                return "UNKNOWN";
        }
    }
};
// #ifndef LOGGING_CONFIG_H
// #define LOGGING_CONFIG_H
// ... (your header content)
namespace LOGGING_CONFIG {
extern bool STATUS_CHECK;
extern uint32_t TROUBLE_ADDR;     // for logging only specific addr
extern uint32_t TROUBLE_CHANNEL;  // for logging only specific channel
extern bool PIMSIM_LOGGING;
extern bool PIMSIM_LOGGING_DEBUG;
extern bool LOGGING_ONLY_TROUBLE_ZONE;
}  // namespace LOGGING_CONFIG
// #endif // EXAMPLE_H

template <typename T>
void Print(T t) {
    std::cout << t << "\033[0m" << std::endl;
}

template <typename T, typename... Args>
void Print(T t, Args... args) {
    std::cout << t << " ";
    Print(args...);
}

template <typename... Args>
void PrintColor(Color color, Args... args) {
    if (!LOGGING_CONFIG::PIMSIM_LOGGING_DEBUG) return;
    std::cout << ColorString(color);
    Print(args...);
}

template <typename... Args>
void PrintError(Args... args) {
    std::cout << ColorString(Color::RED);
    Print(args...);
    AbruptExit(__FILE__, __LINE__);
}

template <typename... Args>
void PrintWarning(Args... args) {
    if (!LOGGING_CONFIG::PIMSIM_LOGGING_DEBUG) return;
    std::cout << ColorString(Color::MAGENTA);
    Print(args...);
}

template <typename... Args>
void PrintImportant(Args... args) {
    if (!LOGGING_CONFIG::PIMSIM_LOGGING) return;
    std::cout << ColorString(Color::YELLOW);
    Print(args...);
}

template <typename... Args>
void PrintDebug(Args... args) {
    // when you fix bugs -> do not use this method
    // if you want to leave log, use other method instead
    std::cout << ColorString(Color::BLUE);
    Print(args...);
}

template <typename... Args>
void PrintInfo(Args... args) {
    if (!LOGGING_CONFIG::PIMSIM_LOGGING_DEBUG) return;
    std::cout << ColorString(Color::CYAN);
    Print(args...);
}

template <typename... Args>
void PrintGreen(Args... args) {
    if (!LOGGING_CONFIG::PIMSIM_LOGGING_DEBUG) return;
    std::cout << ColorString(Color::GREEN);
    Print(args...);
}
std::string HexString(uint64_t addr);

void PrintControllerLog(std::string method_name, int channel_id, int clk,
                        const Command &cmd);
void PrintTransactionLog(std::string method_name, int channel_id, int clk,
                         const Transaction &trans);
}  // namespace dramsim3
#endif
