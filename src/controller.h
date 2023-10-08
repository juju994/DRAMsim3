#ifndef __CONTROLLER_H
#define __CONTROLLER_H

#include <fstream>
#include <map>
#include <unordered_set>
#include <vector>
#include "channel_state.h"
#include "command_queue.h"
#include "common.h"
#include "refresh.h"
#include "simple_stats.h"

#ifdef THERMAL
#include "thermal.h"
#endif  // THERMAL

namespace dramsim3 {

// RowBuf策略: 打开页, 关闭页
enum class RowBufPolicy { OPEN_PAGE, CLOSE_PAGE, SIZE };

class Controller {
   public:
#ifdef THERMAL
    Controller(int channel, const Config &config, const Timing &timing,
               ThermalCalculator &thermalcalc);
#else
    Controller(int channel, const Config &config, const Timing &timing);
#endif  // THERMAL
    void ClockTick();
    bool WillAcceptTransaction(uint64_t hex_addr, bool is_write) const;
    bool AddTransaction(Transaction trans);
    int QueueUsage() const;
    // Stats output     统计输出
    void PrintEpochStats();
    void PrintFinalStats();
    void ResetStats() { simple_stats_.Reset(); }
    std::pair<uint64_t, int> ReturnDoneTrans(uint64_t clock);

    int channel_id_;

   private:
    uint64_t clk_;          // 控制器当前时钟值
    const Config &config_;
    SimpleStats simple_stats_;
    ChannelState channel_state_;
    CommandQueue cmd_queue_;
    Refresh refresh_;

#ifdef THERMAL
    ThermalCalculator &thermal_calc_;
#endif  // THERMAL

    // queue that takes transactions from CPU side  用于区分读写事件是否统一放在unified_queue_队列中
    // true->读写统一队列   false->读写队列分离
    bool is_unified_queue_;     
    
    /* std::vector<Transaction> 表示动态数组的容器模板 */ 

    std::vector<Transaction> unified_queue_;    // 读写事件统一队列     尺寸由config_.trans_queue_size限制
    std::vector<Transaction> read_queue_;       // 读事件队列     尺寸由config_.trans_queue_size限制
    std::vector<Transaction> write_buffer_;     // 写事件队列     尺寸由config_.trans_queue_size限制

    /* std::multimap 是一个关联容器, 用于存储类型为Transaction的对象, 关联的键类型为uint64_t
        std::multimap容器使用键-值存储数据, 允许多个相同键的值存在, 即一个键可以对应多个值. 
        需要注意的是, std::multimap会根据键的值自动排序, 即pending_rd_q_中的元素以uint64_t键的升序方式排列. 
     */
    // transactions that are not completed, use map for convenience     事件还未完成, 为了方便使用map
    std::multimap<uint64_t, Transaction> pending_rd_q_;     // 挂起的读队列
    std::multimap<uint64_t, Transaction> pending_wr_q_;     // 挂起的写队列

    // completed transactions   完成的事件
    std::vector<Transaction> return_queue_;

    // row buffer policy    行缓冲策略
    RowBufPolicy row_buf_policy_;

#ifdef CMD_TRACE
    std::ofstream cmd_trace_;
#endif  // CMD_TRACE

    // used to calculate inter-arrival latency  用于计算内部到达延迟
    uint64_t last_trans_clk_;

    // transaction queueing     事件队列
    int write_draining_;
    void ScheduleTransaction();
    void IssueCommand(const Command &tmp_cmd);
    Command TransToCommand(const Transaction &trans);
    void UpdateCommandStats(const Command &cmd);
};
}  // namespace dramsim3
#endif
