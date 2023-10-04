#include "command_queue.h"

namespace dramsim3 {

CommandQueue::CommandQueue(int channel_id, const Config& config,
                           const ChannelState& channel_state,
                           SimpleStats& simple_stats)
    : rank_q_empty(config.ranks, true),     // 初始化大小, 默认值为true
      config_(config),
      channel_state_(channel_state),
      simple_stats_(simple_stats),
      is_in_ref_(false),
      queue_size_(static_cast<size_t>(config_.cmd_queue_size)),     // 就int转size_t类型
      queue_idx_(0),
      clk_(0) 
      
{
    // 判断传入 config_.queue_structure的类型, 只支持PER_BANK和PER_RANK两种
    if (config_.queue_structure == "PER_BANK") {
        queue_structure_ = QueueStructure::PER_BANK;    // 
        num_queues_ = config_.banks * config_.ranks;    // 计算队列数量, 总队列数等于所有bank的数量
    } else if (config_.queue_structure == "PER_RANK") {
        queue_structure_ = QueueStructure::PER_RANK;
        num_queues_ = config_.ranks;    // 计算队列数量, 总队列数等于所有rank的数量
    } else {
        std::cerr << "Unsupportted queueing structure "
                  << config_.queue_structure << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    // 预申请需要的二维队列尺寸     queues_[num_queues_(bank或rank号)][cmd_queue_size(设定的queue大小)]
    queues_.reserve(num_queues_);       
    for (int i = 0; i < num_queues_; i++) {
        auto cmd_queue = std::vector<Command>();
        cmd_queue.reserve(config_.cmd_queue_size);
        queues_.push_back(cmd_queue);
    }
}

/*
    从命令队列中获取可执行命令
    参数: 
        NULL
    返回: 
        Command     可执行的命令
*/
Command CommandQueue::GetCommandToIssue() {
    for (int i = 0; i < num_queues_; i++) {
        // 遍历每一个queue(每个rank或者bank)
        auto& queue = GetNextQueue();
        // if we're refresing, skip the command queues that are involved
        // 如果我们处于刷新状态, 跳过所设计的命令队列
        if (is_in_ref_) {   // 处于刷新状态
            if (ref_q_indices_.find(queue_idx_) != ref_q_indices_.end()) {
                // find(queue_idx_)查找无序集合中是否有等于queue_idx的元素, 并返回一个迭代器
                // 如果找到该元素, 迭代器指向该元素, 否则等于.end(), 表示没有找到该元素
                continue;   // 跳过这个循环
            }
        }
        auto cmd = GetFirstReadyInQueue(queue);
        // 检查返回命令是否有效
        if (cmd.IsValid()) {
            // 检查是否为读或写命令
            if (cmd.IsReadWrite()) {
                EraseRWCommand(cmd);
            }
            return cmd;
        }
    }
    return Command();   // 返回空cmd类, 表示命令无效
}

/*
    结束刷新???
    参数: 
        NULL
    返回: 
        Command     可执行的命令
*/
Command CommandQueue::FinishRefresh() {
    // we can do something fancy here like clearing the R/Ws
    // that already had ACT on the way but by doing that we
    // significantly pushes back the timing for a refresh
    // so we simply implement an ASAP approach
    // 我们可以在这里进行一些操作, 比如清除以及有ACT的R/Ws, 但是这样做会大大推迟刷新的时间, 所以我们只需要实现ASAP方法
    /* ASAP (As Soon As Possible) 是一种方法论，主要用于优化任务调度和工作流程。
        它的基本原理是尽快完成尽可能多的任务，以最大程度地提高整体效率和满足要求。
        在ASAP方法中，任务被安排在尽可能早的时间点执行，以便尽快交付结果。
        这种方法通常适用于那些没有先后依赖关系或最小化先后依赖关系的情况。
        ASAP方法强调尽早进行任务，以便在任务完成后能够尽早获取结果，并能在后续的任务中使用。 */
    
    auto ref = channel_state_.PendingRefCommand();
    // 命令队列不在刷新状态
    if (!is_in_ref_) {
        GetRefQIndices(ref);
        // 进入刷新状态
        is_in_ref_ = true;
    }

    // either precharge or refresh  预充电或刷新命令
    auto cmd = channel_state_.GetReadyCommand(ref, clk_);

    if (cmd.IsRefresh()) {
        ref_q_indices_.clear();
        is_in_ref_ = false;
    }
    return cmd;
}

/*
    仲裁预充电命令
    参数: 
        const CMDIterator& cmd_it   迭代器参数 (queue中的迭代开始元素)
        const CMDQueue& queue       队列参数 (整个命令队列queue)
    返回: 
        bool    true->需要预充电
                false->不需要预充电
    Notes: 返回true是会同步自增统计量num_ondemand_pres (什么意思?)
*/
bool CommandQueue::ArbitratePrecharge(const CMDIterator& cmd_it,
                                      const CMDQueue& queue) const {
    auto cmd = *cmd_it;
    // 对queue从头开始遍历, 直到到达cmd_it指向的元素
    for (auto prev_itr = queue.begin(); prev_itr != cmd_it; prev_itr++) {
        // 判断在cmd_it前有没有指向同一个bank的命令
        if (prev_itr->Rank() == cmd.Rank() &&
            prev_itr->Bankgroup() == cmd.Bankgroup() &&
            prev_itr->Bank() == cmd.Bank()) {
            return false;
        }
    }
    // 挂起的row_hit是否存在  bool标志
    bool pending_row_hits_exist = false;
    int open_row =
        channel_state_.OpenRow(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    // for循环从传入迭代位置开始遍历整个命令队列
    for (auto pending_itr = cmd_it; pending_itr != queue.end(); pending_itr++) {
        // 后面的命令中有和传入迭代器命令相同的情况就说明有row_hit情况?
        if (pending_itr->Row() == open_row &&
            pending_itr->Bank() == cmd.Bank() &&
            pending_itr->Bankgroup() == cmd.Bankgroup() &&
            pending_itr->Rank() == cmd.Rank()) {
            pending_row_hits_exist = true;
            break;
        }
    }
    // 检查传入迭代器指向的bank发生RowHit的次数, 如果大于等于4就说明到了rowhit上限
    bool rowhit_limit_reached =
        channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(), cmd.Bank()) >=
        4;
    // 后续没有row_hits情况或者rowhit数量达到上限, 返回true
    if (!pending_row_hits_exist || rowhit_limit_reached) {
        simple_stats_.Increment("num_ondemand_pres");
        return true;
    }
    return false;
}

/*
    检查传入地址所对应的queue能够接受命令(即检查队列长度)
    参数: 
        int rank
        int bankgroup
        int bank
    返回: 
        bool    true->queue未满      false->queue已满
*/
bool CommandQueue::WillAcceptCommand(int rank, int bankgroup, int bank) const {
    int q_idx = GetQueueIndex(rank, bankgroup, bank);
    return queues_[q_idx].size() < queue_size_;
}

/*
    检查queues_是否为空
    参数: 
        NULL
    返回: 
        bool    true->queues_为空      false->queues_不为空
*/
bool CommandQueue::QueueEmpty() const {
    for (const auto q : queues_) {
        if (!q.empty()) {
            return false;
        }
    }
    return true;
}

/*
    在queue的尾部添加命令
    参数: 
        Command cmd     传入需要添加的命令
    返回: 
        bool    true->添加成功      false->queue长度过长添加失败
    Notes: 添加命令后会同步改变rank_q_empty[rank]的状态
*/
bool CommandQueue::AddCommand(Command cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    // 检查queue长度
    if (queue.size() < queue_size_) {
        queue.push_back(cmd);
        rank_q_empty[cmd.Rank()] = false;
        return true;
    } else {
        return false;
    }
}

/*
    从queues_中获得下一个queue, 取出的queue由queue_idx_控制
    参数: 
        NULL
    返回: 
        CMDQueue&   queue_idx_指向的queue数组地址
*/
CMDQueue& CommandQueue::GetNextQueue() {
    queue_idx_++;
    if (queue_idx_ == num_queues_) {
        queue_idx_ = 0;
    }
    return queues_[queue_idx_];
}

/*
    维护ref_q_indices_数组
    参数: 
        const Command& ref  刷新队列首个命令
    返回: 
       NULL
    Notes: 传入ref参数要么是REFRESH, 要么是REFRESH_BANK
*/
void CommandQueue::GetRefQIndices(const Command& ref) {
    if (ref.cmd_type == CommandType::REFRESH) {
        // 队列类型
        if (queue_structure_ == QueueStructure::PER_BANK) {
            for (int i = 0; i < num_queues_; i++) {
                // 在PER_BANK型的命令队列中, 对于ref_q_indices_的更新还是以rank为单位的(这个rank里面的所有bank都会插入)
                if (i / config_.banks == ref.Rank()) {
                    ref_q_indices_.insert(i);
                }
            }
        } else {
            ref_q_indices_.insert(ref.Rank());
        }
    } else {  // refb
        int idx = GetQueueIndex(ref.Rank(), ref.Bankgroup(), ref.Bank());
        ref_q_indices_.insert(idx);
    }
    return;
}

/*
    根据传入地址得到该地址的queue索引值, 用于在queues_中寻址
    参数: 
        int rank
        int bankgroup
        int bank
    返回: 
        int     queues_索引值(会判断队列结构不同的情况)
*/
int CommandQueue::GetQueueIndex(int rank, int bankgroup, int bank) const {
    if (queue_structure_ == QueueStructure::PER_RANK) {
        return rank;
    } else {
        return rank * config_.banks + bankgroup * config_.banks_per_group +
               bank;
    }
}

/*
    根据传入地址从queues_中得到该地址的queue结构
    参数: 
        int rank
        int bankgroup
        int bank
    返回: 
        CMDQueue&   单个命令队列queue地址
*/
CMDQueue& CommandQueue::GetQueue(int rank, int bankgroup, int bank) {
    int index = GetQueueIndex(rank, bankgroup, bank);
    return queues_[index];
}

/*
    从queue中获得第一个就绪命令
    参数: 
        CMDQueue& queue     传入的一维命令数组
    返回: 
        Command     返回命令, 会存在无效命令的情况
    Notes:  检查是否存在写前读情况, 如有则跳过循环??
*/
Command CommandQueue::GetFirstReadyInQueue(CMDQueue& queue) const {
    // 从头到尾遍历所有queue元素
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        // 从通道状态中获取就绪命令
        Command cmd = channel_state_.GetReadyCommand(*cmd_it, clk_);
        if (!cmd.IsValid()) {
            // 返回命令无效, 直接进入下次循环
            continue;
        }
        // 返回命令为预充电时
        if (cmd.cmd_type == CommandType::PRECHARGE) {
            // 返回false就表示不需要预充电, 直接跳过这个循环
            if (!ArbitratePrecharge(cmd_it, queue)) {
                continue;
            }
        } else if (cmd.IsWrite()) {     // 命令为写入, 或写后预充电
            // 检查是否存在写前读情况, 如有则跳过循环??
            if (HasRWDependency(cmd_it, queue)) {
                continue;
            }
        }
        return cmd;     // 返回有效命令
    }
    return Command();
}

/*
    从queue中获得第一个就绪命令
    参数: 
        const Command& cmd      传入命令
    返回: 
        NULL
    Notes:  存在错误检查, 没有找到cmd!, 直接exit(1), 并输出报错信息
*/
void CommandQueue::EraseRWCommand(const Command& cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        if (cmd.hex_addr == cmd_it->hex_addr && cmd.cmd_type == cmd_it->cmd_type) {
            queue.erase(cmd_it);
            return;
        }
    }
    // 错误检查, 没有找到cmd!
    std::cerr << "cannot find cmd!" << std::endl;
    exit(1);
}

int CommandQueue::QueueUsage() const {
    int usage = 0;
    for (auto i = queues_.begin(); i != queues_.end(); i++) {
        usage += i->size();
    }
    return usage;
}

/*
    检查命令队列是否存在对同一个地址写后读情况
    参数: 
        const CMDIterator& cmd_it   迭代器参数 (queue中的迭代开始元素, 写类型命令)
        const CMDQueue& queue       队列参数 (整个命令队列queue)
    返回: 
        bool
                true->当前写命令前有读命令      fasle->当前写命令前没有读命令
    Notes:  
*/
bool CommandQueue::HasRWDependency(const CMDIterator& cmd_it,
                                   const CMDQueue& queue) const {
    // Read after write has been checked in controller so we only check write after read here
    // 读后写情况会在控制器中检查, 所以我们在这里只检查写后读的情况

    // for循环从整个queue的开头开始遍历到传入迭代器指向的命令
    for (auto it = queue.begin(); it != cmd_it; it++) {
        if (it->IsRead() &&         // 是不是读命令
            it->Row() == cmd_it->Row() &&
            it->Column() == cmd_it->Column() && it->Bank() == cmd_it->Bank() &&
            it->Bankgroup() == cmd_it->Bankgroup()) {
            return true;
        }
    }
    return false;
}

}  // namespace dramsim3
