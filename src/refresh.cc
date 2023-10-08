#include "refresh.h"

namespace dramsim3 {
Refresh::Refresh(const Config &config, ChannelState &channel_state)
    : clk_(0),
      config_(config),
      channel_state_(channel_state),
      refresh_policy_(config.refresh_policy),
      next_rank_(0),
      next_bg_(0),
      next_bank_(0) {
    
    /*
        刷新策略        刷新间隔  refresh_interval_
        Rank级同步      tREFI
        Bank级交错      tREFIb
        Rank级交错      config_.tREFI / config_.ranks
    */
    if (refresh_policy_ == RefreshPolicy::RANK_LEVEL_SIMULTANEOUS) {
        refresh_interval_ = config_.tREFI;
    } else if (refresh_policy_ == RefreshPolicy::BANK_LEVEL_STAGGERED) {
        refresh_interval_ = config_.tREFIb;
    } else {  // default refresh scheme: RANK STAGGERED
        refresh_interval_ = config_.tREFI / config_.ranks;
    }
}

void Refresh::ClockTick() {
    // clk_计数达到refresh_interval_的整数倍, 插入一次刷新命令
    if (clk_ % refresh_interval_ == 0 && clk_ > 0) {
        InsertRefresh();
    }
    clk_++;
    return;
}

// 调用并维护channel_state_的刷新队列
void Refresh::InsertRefresh() {
    switch (refresh_policy_) {
        // Simultaneous all rank refresh    同步进行所有rank的刷新
        case RefreshPolicy::RANK_LEVEL_SIMULTANEOUS:
            // 遍历所有rank
            for (auto i = 0; i < config_.ranks; i++) {
                // 判断Rank是否处于自刷新模式
                if (!channel_state_.IsRankSelfRefreshing(i)) {
                    // rank级维护刷新队列
                    channel_state_.RankNeedRefresh(i, true);
                    break;
                }
            }
            break;
        // Staggered all rank refresh   交错进行所有rank的刷新
        case RefreshPolicy::RANK_LEVEL_STAGGERED:
            // 判断下一个Rank是否处于自刷新模式
            if (!channel_state_.IsRankSelfRefreshing(next_rank_)) {
                channel_state_.RankNeedRefresh(next_rank_, true);
            }
            IterateNext();  // 迭代进行下一个
            break;
        // Fully staggered per bank refresh     交错进行每个bank的刷新
        case RefreshPolicy::BANK_LEVEL_STAGGERED:
            // 判断下一个Rank是否处于自刷新模式
            if (!channel_state_.IsRankSelfRefreshing(next_rank_)) {
                channel_state_.BankNeedRefresh(next_rank_, next_bg_, next_bank_,
                                               true);
            }
            IterateNext();  // 迭代进行下一个
            break;
        default:        // 错误检查
            AbruptExit(__FILE__, __LINE__);
            break;
    }
    return;
}

// 迭代进行下个刷新 (仅对交错刷新模式有有效)
void Refresh::IterateNext() {
    switch (refresh_policy_) {
        case RefreshPolicy::RANK_LEVEL_STAGGERED:       // Rank级交错
            next_rank_ = (next_rank_ + 1) % config_.ranks;
            return;
        case RefreshPolicy::BANK_LEVEL_STAGGERED:       // Bank级交错
            // Note - the order issuing bank refresh commands is static and
            // non-configurable as per JEDEC standard
            // Note: 在JEDEC标准中发出的bank刷新命令顺序是固定且不能修改的
            /*
                bank级交错的刷新顺序
                bank = 0 
                    bankgroup = 0~3 (4个bank组) rank = 0
                bank = 1 
                    bankgroup = 0~3 (4个bank组) rank = 0
                bank = 2 
                    bankgroup = 0~3 (4个bank组) rank = 0
                bank = 3 
                    bankgroup = 0~3 (4个bank组) rank = 0
                bank = 0 
                    bankgroup = 0~3 (4个bank组) rank = 1
                bank = 1 
                    bankgroup = 0~3 (4个bank组) rank = 1
                bank = 2 
                    bankgroup = 0~3 (4个bank组) rank = 1
                bank = 3 
                    bankgroup = 0~3 (4个bank组) rank = 1
                bank = 0 
                    bankgroup = 0~3 (4个bank组) rank = 0
                bank = 1 
                    bankgroup = 0~3 (4个bank组) rank = 0
            */
            next_bg_ = (next_bg_ + 1) % config_.bankgroups;
            // 特判bankgroup循环后的第一个bank组
            if (next_bg_ == 0) {
                next_bank_ = (next_bank_ + 1) % config_.banks_per_group;
                if (next_bank_ == 0) {
                    next_rank_ = (next_rank_ + 1) % config_.ranks;
                }
            }
            return;
        default:        // 错误检查
            AbruptExit(__FILE__, __LINE__);
            return;
    }
}

}  // namespace dramsim3
