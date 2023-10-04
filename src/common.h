#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>
#include <iostream>
#include <vector>

namespace dramsim3 {
 
//  地址结构体,由channel, rank, bank group, bank, row, column构成
struct Address {
    // 默认构造函数
    Address()
        : channel(-1), rank(-1), bankgroup(-1), bank(-1), row(-1), column(-1) {}
    // 参数列表构造
    Address(int channel, int rank, int bankgroup, int bank, int row, int column)
        : channel(channel),
          rank(rank),
          bankgroup(bankgroup),
          bank(bank),
          row(row),
          column(column) {}
    // 结构体地址构造
    Address(const Address& addr)
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
    int column;
};

// inline关键字, 请求编译器将函数内联展开, 以提升执行效率
inline uint32_t ModuloWidth(uint64_t addr, uint32_t bit_width, uint32_t pos) {
    addr >>= pos;
    auto store = addr;
    addr >>= bit_width;
    addr <<= bit_width;
    return static_cast<uint32_t>(store ^ addr);
}

// extern std::function<Address(uint64_t)> AddressMapping;
int GetBitInPos(uint64_t bits, int pos);
// it's 2017 and c++ std::string still lacks a split function, oh well
std::vector<std::string> StringSplit(const std::string& s, char delim);
template <typename Out>
void StringSplit(const std::string& s, char delim, Out result);

int LogBase2(int power_of_two);
void AbruptExit(const std::string& file, int line);
bool DirExist(std::string dir);

// 命令枚举, 枚举值默认从0开始加
enum class CommandType {
    READ,                   // 读
    READ_PRECHARGE,         // 读后自动预充电
    WRITE,                  // 写
    WRITE_PRECHARGE,        // 写后自动预充电
    ACTIVATE,               // 激活
    PRECHARGE,              // 预充电
    REFRESH_BANK,           // 刷新bank
    REFRESH,                // 刷新(rank级)
    SREF_ENTER,             // 进入自刷新(rank级)
    SREF_EXIT,              // 退出自刷新(rank级)
    SIZE                    // 枚举元素数量, 用于判断命令是否无效
};

// Command结构体
struct Command {
    Command() : cmd_type(CommandType::SIZE), hex_addr(0) {}
    Command(CommandType cmd_type, const Address& addr, uint64_t hex_addr)
        : cmd_type(cmd_type), addr(addr), hex_addr(hex_addr) {}
    // Command(const Command& cmd) {}

    // 检查cmd_type是否等于CommandType最大数量, true表示命令有效
    bool IsValid() const { return cmd_type != CommandType::SIZE; }
    // 检查cmd_type是否为REFRESH或REFRESH_BANK
    bool IsRefresh() const {
        return cmd_type == CommandType::REFRESH ||
               cmd_type == CommandType::REFRESH_BANK;
    }
    // 检查cmd_type是否为READ或READ_PRECHARGE
    bool IsRead() const {
        return cmd_type == CommandType::READ ||
               cmd_type == CommandType ::READ_PRECHARGE;
    }
    // 检查cmd_type是否为WRITE或WRITE_PRECHARGE
    bool IsWrite() const {
        return cmd_type == CommandType ::WRITE ||
               cmd_type == CommandType ::WRITE_PRECHARGE;
    }
    // 检查cmd_type是否为读写命令   true->读或写命令    fasle->不是
    bool IsReadWrite() const { return IsRead() || IsWrite(); }
    // 检查为Rank级命令: REFRESH 或 SREF_ENTER 或 SREF_EXIT
    bool IsRankCMD() const {
        return cmd_type == CommandType::REFRESH ||
               cmd_type == CommandType::SREF_ENTER ||
               cmd_type == CommandType::SREF_EXIT;
    }
    CommandType cmd_type;       // 构造函数自动赋值
    Address addr;               // 
    uint64_t hex_addr;          // 

    int Channel() const { return addr.channel; }
    int Rank() const { return addr.rank; }
    int Bankgroup() const { return addr.bankgroup; }
    int Bank() const { return addr.bank; }
    int Row() const { return addr.row; }
    int Column() const { return addr.column; }

    // 友元函数重载输出运算符<<
    friend std::ostream& operator<<(std::ostream& os, const Command& cmd);
};

// 
struct Transaction {
    Transaction() {}
    Transaction(uint64_t addr, bool is_write)
        : addr(addr),
          added_cycle(0),
          complete_cycle(0),
          is_write(is_write) {}
    Transaction(const Transaction& tran)
        : addr(tran.addr),
          added_cycle(tran.added_cycle),
          complete_cycle(tran.complete_cycle),
          is_write(tran.is_write) {}
    uint64_t addr;
    uint64_t added_cycle;
    uint64_t complete_cycle;
    bool is_write;

    friend std::ostream& operator<<(std::ostream& os, const Transaction& trans);
    friend std::istream& operator>>(std::istream& is, Transaction& trans);
};

}  // namespace dramsim3
#endif
