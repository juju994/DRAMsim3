#ifndef __BANKSTATE_H
#define __BANKSTATE_H

#include <vector>
#include "common.h"

namespace dramsim3 {

class BankState {
   public:
    // 构造函数
    BankState();

    // 枚举状态类: 打开, 关闭, 自刷新, 掉电
    enum class State { OPEN, CLOSED, SREF, PD, SIZE };
    Command GetReadyCommand(const Command& cmd, uint64_t clk) const;

    // Update the state of the bank resulting after the execution of the command
    // 在执行命令后更新bank的状态
    void UpdateState(const Command& cmd);

    // Update the existing timing constraints for the command
    // 对命令更新现有的时序约束
    void UpdateTiming(const CommandType cmd_type, uint64_t time);

    bool IsRowOpen() const { return state_ == State::OPEN; }
    int OpenRow() const { return open_row_; }
    int RowHitCount() const { return row_hit_count_; }

   private:
    // Current state of the Bank Apriori or instantaneously transitions on a command.
    // bank的当前状态在命令下的Apriori或瞬时转换
    State state_;

    // Earliest time when the particular Command can be executed in this bank
    // 在此bank中执行特定命令的最早时间
    std::vector<uint64_t> cmd_timing_;

    // Currently open row   当前打开行
    int open_row_;

    // consecutive accesses to one row  对一行的连续访问
    int row_hit_count_;
};

}  // namespace dramsim3
#endif
