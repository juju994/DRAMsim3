#include "controller.h"
#include <iomanip>
#include <iostream>
#include <limits>

namespace dramsim3 {

#ifdef THERMAL
Controller::Controller(int channel, const Config &config, const Timing &timing,
                       ThermalCalculator &thermal_calc)
#else
Controller::Controller(int channel, const Config &config, const Timing &timing)
#endif  // THERMAL
    : channel_id_(channel),     // 从0开始
      clk_(0),
      config_(config),
      simple_stats_(config_, channel_id_),
      channel_state_(config, timing),   // config不用config_?
      cmd_queue_(channel_id_, config, channel_state_, simple_stats_),   // config不用config_?
      refresh_(config, channel_state_), // config不用config_?
#ifdef THERMAL
      thermal_calc_(thermal_calc),
#endif  // THERMAL
      is_unified_queue_(config.unified_queue),
    //   根据config.row_buf_policy配置值, 将row_buf_policy_类型转换为RowBufPolicy枚举类
      row_buf_policy_(config.row_buf_policy == "CLOSE_PAGE"
                          ? RowBufPolicy::CLOSE_PAGE
                          : RowBufPolicy::OPEN_PAGE),
      last_trans_clk_(0),
      write_draining_(0) 
      
{
    // 读写事件统一队列
    if (is_unified_queue_) {
        unified_queue_.reserve(config_.trans_queue_size);
    } else {
        // 读写事件分离队列
        read_queue_.reserve(config_.trans_queue_size);
        write_buffer_.reserve(config_.trans_queue_size);
    }

#ifdef CMD_TRACE
    std::string trace_file_name = config_.output_prefix + "ch_" +
                                  std::to_string(channel_id_) + "cmd.trace";
    std::cout << "Command Trace write to " << trace_file_name << std::endl;
    cmd_trace_.open(trace_file_name, std::ofstream::out);
#endif  // CMD_TRACE
}

/*
    返回已经完成的事件
    参数: 
        uint64_t clk        传入时钟值
    返回: 
        std::pair<uint64_t, int>    
            <uint64_t, int>键值对, uint64_t表示完成的事件地址, int(可以放bool值, 会隐式转换为int型)  true->写命令, fasle->读命令
    Notes: 输入clk后会遍历整个return_queue队列元素的complete_cycle是否小于clk, 直到返回第一个满足条件的事件
            当没有元素满足条件时返回<-1, -1>键值对
*/
std::pair<uint64_t, int> Controller::ReturnDoneTrans(uint64_t clk) {
    // 从返回队列头读取一个元素
    auto it = return_queue_.begin();
    while (it != return_queue_.end()) {             // 从头遍历到尾
        // 检查当前clk是否大于返回队列中记录的完成周期
        if (clk >= it->complete_cycle) {
            // 这个it是写命令
            if (it->is_write) {
                simple_stats_.Increment("num_writes_done");     // 写入事件完成 +1
            } else {
                // 这个it是读命令
                simple_stats_.Increment("num_reads_done");      // 读取事件完成 +1
                simple_stats_.AddValue("read_latency", clk_ - it->added_cycle);     // 读取延迟 clk_??
            }
            auto pair = std::make_pair(it->addr, it->is_write);
            it = return_queue_.erase(it);       // 从返回队列中删掉这个元素
            return pair;
        } else {
            ++it;   // 下一项值
        }
    }
    return std::make_pair(-1, -1);
}

void Controller::ClockTick() {
    // update refresh counter
    refresh_.ClockTick();

    bool cmd_issued = false;
    Command cmd;
    if (channel_state_.IsRefreshWaiting()) {
        cmd = cmd_queue_.FinishRefresh();
    }

    // cannot find a refresh related command or there's no refresh
    // 找不到与刷新相关的命令或没有刷新 (cmd由上面函数返回得到), 就是说表示状态空闲不需要刷新的意思吗?
    if (!cmd.IsValid()) {
        cmd = cmd_queue_.GetCommandToIssue();
    }

    if (cmd.IsValid()) {
        IssueCommand(cmd);
        cmd_issued = true;

        if (config_.enable_hbm_dual_cmd) {
            auto second_cmd = cmd_queue_.GetCommandToIssue();
            if (second_cmd.IsValid()) {
                if (second_cmd.IsReadWrite() != cmd.IsReadWrite()) {
                    IssueCommand(second_cmd);
                    simple_stats_.Increment("hbm_dual_cmds");
                }
            }
        }
    }

    // power updates pt 1
    for (int i = 0; i < config_.ranks; i++) {       // 遍历所有rank
        if (channel_state_.IsRankSelfRefreshing(i)) {       // 检查该rank是否处于自刷新状态
            simple_stats_.IncrementVec("sref_cycles", i);   // 自刷新周期计数 +1
        } else {
            bool all_idle = channel_state_.IsAllBankIdleInRank(i);
            // 所有bank都空闲
            if (all_idle) {     
                simple_stats_.IncrementVec("all_bank_idle_cycles", i);  // 所有bank空闲(rank空闲)周期计数 +1
                channel_state_.rank_idle_cycles[i] += 1;    // 对空闲rank的周期进行计数, 用于与自刷新阈值相比较以进入自刷新模式
            } else {    // 有bank都活动
                simple_stats_.IncrementVec("rank_active_cycles", i);    // rank激活周期计数 +1
                // reset
                channel_state_.rank_idle_cycles[i] = 0;     // 空闲rnak计数清零
            }
        }
    }

    // power updates pt 2: move idle ranks into self-refresh mode to save power
    if (config_.enable_self_refresh && !cmd_issued) {       // 使能自刷新默认并且没有触发命令
        // 遍历所有Rank
        for (auto i = 0; i < config_.ranks; i++) {
            // 检查当前rank是否处于自刷新模式
            if (channel_state_.IsRankSelfRefreshing(i)) {
                // wake up! 唤醒(退出自刷新模式)
                if (!cmd_queue_.rank_q_empty[i]) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_EXIT, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            } else {
                // rank空闲周期大于进入预刷新的周期阈值
                if (cmd_queue_.rank_q_empty[i] &&
                    channel_state_.rank_idle_cycles[i] >=
                        config_.sref_threshold) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_ENTER, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            }
        }
    }

    ScheduleTransaction();
    clk_++;
    cmd_queue_.ClockTick();
    simple_stats_.Increment("num_cycles");
    return;
}

/*
    检查读写(均一)缓冲队列是否还有空间
    参数: 
        uint64_t hex_addr       传入地址
        bool is_write           是否为写入 true->写入   false->读取
    返回: 
        bool    true->有空间  false->没有空间
*/
bool Controller::WillAcceptTransaction(uint64_t hex_addr, bool is_write) const {
    // 均一模式
    if (is_unified_queue_) {
        return unified_queue_.size() < unified_queue_.capacity();
    } else if (!is_write) {
        return read_queue_.size() < read_queue_.capacity();
    } else {
        return write_buffer_.size() < write_buffer_.capacity();
    }
}

/*
    插入事件
    参数: 
        Transaction trans   事件类
    返回: 
        bool    true
    Notes: 内部延迟时间 interarrival_latency = 事件加入事件 - 上一个事件加入的时间
*/
bool Controller::AddTransaction(Transaction trans) {
    trans.added_cycle = clk_;       // 记录事件插入时间
    simple_stats_.AddValue("interarrival_latency", clk_ - last_trans_clk_);     // 记录内部延迟事件
    last_trans_clk_ = clk_;

    // 写事件
    if (trans.is_write) {
        // 检查在挂起写入队列中有没有存在相同的地址的元素, 如果没有就直接插入一个元素
        // 如果已经有了相同地址的元素, 就会直接更新返回队列的信息
        if (pending_wr_q_.count(trans.addr) == 0) {  // can not merge writes    无法合并写入??
            pending_wr_q_.insert(std::make_pair(trans.addr, trans));
            // 同步写入写队列
            if (is_unified_queue_) {
                unified_queue_.push_back(trans);
            } else {
                write_buffer_.push_back(trans);
            }
        }
        trans.complete_cycle = clk_ + 1;    // 写事件结束时间 = 开始事件 + 1
        return_queue_.push_back(trans);     // 直接推入结束事件队列中??
        return true;
    } else {  // read   读事件
        // if in write buffer, use the write buffer value
        // 如果在写缓冲中, 使用写缓冲的数值??
        if (pending_wr_q_.count(trans.addr) > 0) {
            trans.complete_cycle = clk_ + 1;
            return_queue_.push_back(trans);     // 直接推入结束事件队列中??
            return true;
        }
        // 如果挂起写缓冲中没有元素具有相同的地址, 在挂起读队列中插入当前元素
        pending_rd_q_.insert(std::make_pair(trans.addr, trans));
        if (pending_rd_q_.count(trans.addr) == 1) {
            // 同步写入读队列
            if (is_unified_queue_) {
                unified_queue_.push_back(trans);
            } else {
                read_queue_.push_back(trans);
            }
        }
        return true;
    }
}

/*
    调度事件
    参数: 
        NULL
    返回: 
        NULL
    Notes: 从读写buffer中拿出事件元素, 转换成命令后加入到命令队列cmd_queue_中
    函数优先调度写入事件, 对于写入事件还会检查挂起读队列中有没有相同地址的, 如果有会进行读后写合并. 
*/
void Controller::ScheduleTransaction() {
    // determine whether to schedule read or write
    // 决定是调度读取还是写入?

    //  写缓冲数量为0 并且 不是读写均一队列
    if (write_draining_ == 0 && !is_unified_queue_) {
        // we basically have a upper and lower threshold for write buffer
        // 对于写入缓冲有一个基本的上限阈值和下限阈值
        if ((write_buffer_.size() >= write_buffer_.capacity()) ||       // buffer内的元素数量比容量还多的情况 或者
            (write_buffer_.size() > 8 && cmd_queue_.QueueEmpty())) {    // buffer内的元素数量>8并且命令队列cmd_queue为空
            write_draining_ = write_buffer_.size();     // 设置write_draining_
        }
    }

    // 根据2个3目运算符, 实现queue地址的选择. is_unified_queue_如果为true, 就将unified_queue_的地址赋给queue
    // 否则就再判定write_draining_是否>0, >0就将write_buffer_赋给queue, 否则就赋值read_queue_
    std::vector<Transaction> &queue =
        is_unified_queue_ ? unified_queue_
                          : write_draining_ > 0 ? write_buffer_ : read_queue_;
    // 遍历整个queue队列
    for (auto it = queue.begin(); it != queue.end(); it++) {
        // 从事件中获取命令
        auto cmd = TransToCommand(*it);
        // 检查对应bank的命令队列是否还有空间
        if (cmd_queue_.WillAcceptCommand(cmd.Rank(), cmd.Bankgroup(),
                                         cmd.Bank())) {
            // 非均一队列 并且 写命令
            if (!is_unified_queue_ && cmd.IsWrite()) {
                // Enforce R->W dependency  强制读后写依赖??
                if (pending_rd_q_.count(it->addr) > 0) {
                    write_draining_ = 0;
                    break;
                }
                write_draining_ -= 1;
            }
            // 在命令队列中添加命令
            cmd_queue_.AddCommand(cmd);     
            queue.erase(it);        // 删除读写缓冲中的当前元素
            break;
        }
        // bank命令队列已满, 检查队列中下一个元素
    }
}

/*
    处理命令
    参数: 
        const Command &cmd      传入的命令
    返回: 
        NULL
    Notes: 函数优先调度写入事件, 对于写入事件还会检查挂起读队列中有没有相同地址的, 如果有会进行读后写合并. 
*/
void Controller::IssueCommand(const Command &cmd) {
#ifdef CMD_TRACE
    cmd_trace_ << std::left << std::setw(18) << clk_ << " " << cmd << std::endl;
#endif  // CMD_TRACE
#ifdef THERMAL
    // add channel in, only needed by thermal module
    thermal_calc_.UpdateCMDPower(channel_id_, cmd, clk_);
#endif  // THERMAL
    // if read/write, update pending queue and return queue
    // 如果为读写命令, 更新挂起队列和返回队列

    // 读
    if (cmd.IsRead()) {
        auto num_reads = pending_rd_q_.count(cmd.hex_addr);     // 获得传入地址在读挂起队列中的数量
        if (num_reads == 0) {       // 传入地址在读挂起队列中没有对应元素, 报错退出
            std::cerr << cmd.hex_addr << " not in read queue! " << std::endl;
            exit(1);
        }
        // if there are multiple reads pending return them all
        // 如果有多个读挂起, 则将其全部返回(同一地址)
        while (num_reads > 0) {
            auto it = pending_rd_q_.find(cmd.hex_addr);
            it->second.complete_cycle = clk_ + config_.read_delay;  // 读命令最后的结束时间 = 命令处理时间+读延时
            return_queue_.push_back(it->second);        // 又放到了return_queue中?
            pending_rd_q_.erase(it);        // 从挂起队列中删除
            num_reads -= 1;
        }
    } 
    // 写
    else if (cmd.IsWrite()) {
        // there should be only 1 write to the same location at a time
        // 同一时刻对同一位置只能有一个写入命令
        auto it = pending_wr_q_.find(cmd.hex_addr);
        if (it == pending_wr_q_.end()) {        // 返回end即表示没找到
            std::cerr << cmd.hex_addr << " not in write queue!" << std::endl;
            exit(1);
        }
        auto wr_lat = clk_ - it->second.added_cycle + config_.write_delay;  // 写延迟 = 当前时刻 - 时间添加时刻 + 写入延迟
        simple_stats_.AddValue("write_latency", wr_lat);
        pending_wr_q_.erase(it);        // 从挂起队列删除元素
    }
    // must update stats before states (for row hits)
    // 必须在更新状态前更新统计量 (为了row hits的计算)
    UpdateCommandStats(cmd);
    channel_state_.UpdateTimingAndStates(cmd, clk_);
}

/*
    从事件获取命令
    参数: 
        const Transaction &trans    传入需要处理的事件
    返回: 
        Command     根据命令类型, 传入地址返回一个Command类
    Notes: row_buf_policy_可选打开页或者关闭页, 主要区别就是打开页使用普通读写, 关闭页使用读写后预充电
*/
Command Controller::TransToCommand(const Transaction &trans) {
    auto addr = config_.AddressMapping(trans.addr);     // 转换一个Address类
    CommandType cmd_type;
    // 行缓冲策略: 打开页
    if (row_buf_policy_ == RowBufPolicy::OPEN_PAGE) {
        cmd_type = trans.is_write ? CommandType::WRITE : CommandType::READ;
    } else {
    // 行缓冲策略: 关闭页
        cmd_type = trans.is_write ? CommandType::WRITE_PRECHARGE
                                  : CommandType::READ_PRECHARGE;
    }
    return Command(cmd_type, addr, trans.addr);     // 返回一个命令类
}

// 调用cmd_queue_中的同名接口
int Controller::QueueUsage() const { return cmd_queue_.QueueUsage(); }

// 打印周期统计量
void Controller::PrintEpochStats() {
    simple_stats_.Increment("epoch_num");
    simple_stats_.PrintEpochStats();
#ifdef THERMAL
    for (int r = 0; r < config_.ranks; r++) {
        double bg_energy = simple_stats_.RankBackgroundEnergy(r);
        thermal_calc_.UpdateBackgroundEnergy(channel_id_, r, bg_energy);
    }
#endif  // THERMAL
    return;
}

/*
    打印最终统计量
    参数: 
        NULL
    返回: 
        NULL
    Notes: 调用simple_stats_类中接口
*/
void Controller::PrintFinalStats() {
    simple_stats_.PrintFinalStats();

#ifdef THERMAL
    for (int r = 0; r < config_.ranks; r++) {
        double bg_energy = simple_stats_.RankBackgroundEnergy(r);
        thermal_calc_.UpdateBackgroundEnergy(channel_id_, r, bg_energy);
    }
#endif  // THERMAL
    return;
}

/*
    更新命令统计量
    参数: 
        const Command &cmd
    返回: 
        NULL
    Notes: 
*/
void Controller::UpdateCommandStats(const Command &cmd) {
    switch (cmd.cmd_type) {
        case CommandType::READ:
        case CommandType::READ_PRECHARGE:
            simple_stats_.Increment("num_read_cmds");
            // 先判断不为0时为了滤除第一次命中的情况
            if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_read_row_hits");       // 读row_hits计数+1
            }
            break;
        case CommandType::WRITE:
        case CommandType::WRITE_PRECHARGE:
            simple_stats_.Increment("num_write_cmds");
            if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_write_row_hits");      // 写row_hits计数+1
            }
            break;
        case CommandType::ACTIVATE:
            simple_stats_.Increment("num_act_cmds");
            break;
        case CommandType::PRECHARGE:
            simple_stats_.Increment("num_pre_cmds");
            break;
        case CommandType::REFRESH:
            simple_stats_.Increment("num_ref_cmds");
            break;
        case CommandType::REFRESH_BANK:
            simple_stats_.Increment("num_refb_cmds");
            break;
        case CommandType::SREF_ENTER:
            simple_stats_.Increment("num_srefe_cmds");
            break;
        case CommandType::SREF_EXIT:
            simple_stats_.Increment("num_srefx_cmds");
            break;
        default:
            AbruptExit(__FILE__, __LINE__);
    }
}

}  // namespace dramsim3
