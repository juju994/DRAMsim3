#ifndef __CHANNEL_STATE_H
#define __CHANNEL_STATE_H

#include <vector>
#include "bankstate.h"
#include "common.h"
#include "configuration.h"
#include "timing.h"

namespace dramsim3 {

class ChannelState {
   public:
    ChannelState(const Config& config, const Timing& timing);

    Command GetReadyCommand(const Command& cmd, uint64_t clk) const;
    void UpdateState(const Command& cmd);
    void UpdateTiming(const Command& cmd, uint64_t clk);
    void UpdateTimingAndStates(const Command& cmd, uint64_t clk);
    bool ActivationWindowOk(int rank, uint64_t curr_time) const;
    void UpdateActivationTimes(int rank, uint64_t curr_time);
    // 对bankstate中IsRowOpen再次封装
    bool IsRowOpen(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].IsRowOpen();
    }
    bool IsAllBankIdleInRank(int rank) const;
    bool IsRankSelfRefreshing(int rank) const { return rank_is_sref_[rank]; }
    bool IsRefreshWaiting() const { return !refresh_q_.empty(); }
    bool IsRWPendingOnRef(const Command& cmd) const;
    const Command& PendingRefCommand() const {return refresh_q_.front(); }
    void BankNeedRefresh(int rank, int bankgroup, int bank, bool need);
    void RankNeedRefresh(int rank, bool need);
    int OpenRow(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].OpenRow();
    }

    int RowHitCount(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].RowHitCount();
    };

    std::vector<int> rank_idle_cycles;

   private:
    const Config& config_;
    const Timing& timing_;

    std::vector<bool> rank_is_sref_;    // Rank级是否自刷新状态管理
    // std::vector<> 一个可变长容器
    // std::vector<std::vector<BankState>> 一个二维vector容器, 用于存储一维std::vector<BankState>容器的集合
    // 总体来说bank_states_是一个三维容器
    std::vector<std::vector<std::vector<BankState> > > bank_states_;
    std::vector<Command> refresh_q_;    // 刷新队列

    // rank级参数, 一个rank里面的所有device命令都是相同的
    // 二维可变数组four_aw_[rank][第几个激活命令时刻+tFAW], 即每个激活命令对应tFAW失效时间
    std::vector<std::vector<uint64_t> > four_aw_;                   
    std::vector<std::vector<uint64_t> > thirty_two_aw_;             // 32个激活窗口 rank级参数
    bool IsFAWReady(int rank, uint64_t curr_time) const;            // 
    bool Is32AWReady(int rank, uint64_t curr_time) const;           // 
    // Update timing of the bank the command corresponds to
    // 更新命令对于的bank时序
    void UpdateSameBankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of the other banks in the same bankgroup as the command
    // 与命令一起更新 同一bank组 中 其他bank 的时序
    void UpdateOtherBanksSameBankgroupTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of banks in the same rank but different bankgroup as the
    // command
    // 与命令一起更新位于 同一rank 中的 不同bank组 中的 bank
    void UpdateOtherBankgroupsSameRankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of banks in a different rank as the command
    // 与命令一起更新 不同rank 中的bank
    void UpdateOtherRanksTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of the entire rank (for rank level commands)
    // 更新整个rank的时序 (对于rank级别的命令)
    void UpdateSameRankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);
};

}  // namespace dramsim3
#endif
