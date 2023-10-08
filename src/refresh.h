#ifndef __REFRESH_H
#define __REFRESH_H

#include <vector>
#include "channel_state.h"
#include "common.h"
#include "configuration.h"

namespace dramsim3 {

class Refresh {
   public:
    Refresh(const Config& config, ChannelState& channel_state);
    void ClockTick();

   private:
    uint64_t clk_;
    int refresh_interval_;
    const Config& config_;
    ChannelState& channel_state_;           // 通道状态
    RefreshPolicy refresh_policy_;          // 刷新策略

    int next_rank_, next_bg_, next_bank_;

    void InsertRefresh();

    void IterateNext();
};

}  // namespace dramsim3

#endif