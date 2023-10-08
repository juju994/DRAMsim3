#ifndef __COMMAND_QUEUE_H
#define __COMMAND_QUEUE_H

#include <unordered_set>
#include <vector>
#include "channel_state.h"
#include "common.h"
#include "configuration.h"
#include "simple_stats.h"

namespace dramsim3 {

using CMDIterator = std::vector<Command>::iterator;         // iterator表示vector容器的迭代器类型, 用于遍历容器中的元素
using CMDQueue = std::vector<Command>;                      // CMDQueue为一个可变长Command类数组
enum class QueueStructure { PER_RANK, PER_BANK, SIZE };     // 队列结构枚举类 (每rank / 每bank)

class CommandQueue {
   public:
    CommandQueue(int channel_id, const Config& config,
                 const ChannelState& channel_state, SimpleStats& simple_stats);
    Command GetCommandToIssue();
    Command FinishRefresh();
    void ClockTick() { clk_ += 1; };
    bool WillAcceptCommand(int rank, int bankgroup, int bank) const;
    bool AddCommand(Command cmd);
    bool QueueEmpty() const;
    int QueueUsage() const;
    std::vector<bool> rank_q_empty;     // rank队列是否为空数组

   private:
    bool ArbitratePrecharge(const CMDIterator& cmd_it,
                            const CMDQueue& queue) const;
    bool HasRWDependency(const CMDIterator& cmd_it,
                         const CMDQueue& queue) const;
    Command GetFirstReadyInQueue(CMDQueue& queue) const;
    int GetQueueIndex(int rank, int bankgroup, int bank) const;
    CMDQueue& GetQueue(int rank, int bankgroup, int bank);
    CMDQueue& GetNextQueue();
    void GetRefQIndices(const Command& ref);
    void EraseRWCommand(const Command& cmd);
    // 这个函数未定义
    Command PrepRefCmd(const CMDIterator& it, const Command& ref) const;

    QueueStructure queue_structure_;
    const Config& config_;
    const ChannelState& channel_state_;
    SimpleStats& simple_stats_;

    std::vector<CMDQueue> queues_;      // 二维数组 queues_[num_queues_(bank或rank号)][cmd_queue_size(设定的queue大小)]

    // Refresh related data structures  刷新相关数据结构体
    std::unordered_set<int> ref_q_indices_;     // 无序集合, 用于存储int型元素(不是键值对, 就是数组) 记录有哪些bank或者rank处于刷新状态
    bool is_in_ref_;        // 是否在刷新状态

    int num_queues_;        // 维护的命令队列数量(与命令队列结构有关)
    size_t queue_size_;     // 麦格命令队列的大小 size_t = unsigned long
    int queue_idx_;         // 全局队列计数
    uint64_t clk_;
};

}  // namespace dramsim3
#endif
