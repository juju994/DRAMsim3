#include "channel_state.h"

namespace dramsim3 {
ChannelState::ChannelState(const Config& config, const Timing& timing)
    : rank_idle_cycles(config.ranks, 0),    // 初始化容器大小, 并同一赋初值. 2个位置都为0
      config_(config),
      timing_(timing),
      rank_is_sref_(config.ranks, false),   // rank级
      four_aw_(config_.ranks, std::vector<uint64_t>()),   // rank级
      thirty_two_aw_(config_.ranks, std::vector<uint64_t>())    // rank级
{
    // reserve用于预分配vector的存储空间, 用于提高性能
    bank_states_.reserve(config_.ranks);    // 第一层先预分配有几个rank
    for (auto i = 0; i < config_.ranks; i++) {
        auto rank_states = std::vector<std::vector<BankState>>();   // 创建一个二维BankState元素向量
        rank_states.reserve(config_.bankgroups);    // 预分配第二层空间, bankgroups数量索引
        for (auto j = 0; j < config_.bankgroups; j++) {
            auto bg_states =
                std::vector<BankState>(config_.banks_per_group, BankState());   // 初始化banks_per_group个BankState结构放入bankgroup结构
            rank_states.push_back(bg_states);   // 推入一个bankgroup结构
        }
        bank_states_.push_back(rank_states);    // 推入一个rank结构
    }
}

/*
    检查Rank内的 所有 Bank是否空闲
    参数: 
        int rank    输入rank号
    返回: 
        true  -> 所有bank都空闲
        false -> 有bank开启
*/
bool ChannelState::IsAllBankIdleInRank(int rank) const {
    for (int j = 0; j < config_.bankgroups; j++) {
        for (int k = 0; k < config_.banks_per_group; k++) {
            if (bank_states_[rank][j][k].IsRowOpen()) {
                return false;
            }
        }
    }
    return true;
}

/*
    检查row的状态是否打开并且是否为第一次读写
    参数: 
        const Command& cmd    传入Command类地址
    返回: 
        true  -> cmd指向的行已经打开, 且为第一次读写
        false -> true的任意条件不满足
*/
bool ChannelState::IsRWPendingOnRef(const Command& cmd) const {
    // 从传入Command类中解析得到rank, bankgroup和bank地址
    int rank = cmd.Rank();
    int bankgroup = cmd.Bankgroup();
    int bank = cmd.Bank();
    // IsRowOpen检查bank是否有行打开
    // RowHitCount检查行是否是第一次打开 (RowHitCount在行关闭的时候也是0, 打开行之后需要进行一次写入才会+1)
    // OpenRow检查cmd指向的bank中打开的行是否和cmd指向的行相等
    // 
    return (IsRowOpen(rank, bankgroup, bank) &&
            RowHitCount(rank, bankgroup, bank) == 0 &&
            bank_states_[rank][bankgroup][bank].OpenRow() == cmd.Row());
}

/*
    Bank级别维护刷新队列 refresh_q_
    参数: 
        int rank        
        int bankgroup   
        int bank        
        bool need       true -> 在刷新队列加入传入的Rank号; false -> 在刷新队列中删除传入的Rank号
    返回: 
    Notes: 没有指定的参数默认填-1
*/
void ChannelState::BankNeedRefresh(int rank, int bankgroup, int bank,
                                   bool need) {
    // 需要刷新时从传入参数得到一个Address类
    if (need) {
        Address addr = Address(-1, rank, bankgroup, bank, -1, -1);
        refresh_q_.emplace_back(CommandType::REFRESH_BANK, addr, -1);   // 在refresh_q_的末尾增加为command类更新命令和地址属性
    } else {
        // 不需要刷新时
        for (auto it = refresh_q_.begin(); it != refresh_q_.end(); it++) {  // 遍历refresh_q_中的元素
            if (it->Rank() == rank && it->Bankgroup() == bankgroup &&
                it->Bank() == bank) {
                refresh_q_.erase(it);   // 传递地址参数与刷新队列中完成了匹配, 删除对应元素, 立即退出
                break;
            }
        }
    }
    return;
}

/*
    Rank级别维护刷新队列 refresh_q_
    参数: 
        int rank        
        bool need       true -> 在刷新队列加入传入的Rank号; false -> 在刷新队列中删除传入的Rank号
    返回: 
    Notes: 没有指定的参数默认填-1
*/
void ChannelState::RankNeedRefresh(int rank, bool need) {
    if (need) {
        Address addr = Address(-1, rank, -1, -1, -1, -1);
        refresh_q_.emplace_back(CommandType::REFRESH, addr, -1);    // CommandType::REFRESH, addr, -1参数对Cmomand类进行初始化
    } else {
        // 在刷新队列中从头开始遍历
        for (auto it = refresh_q_.begin(); it != refresh_q_.end(); it++) {
            // 队列中的元素和传入rank相等
            if (it->Rank() == rank) {
                refresh_q_.erase(it);   // 删除该节点
                break;
            }
        }
    }
    return;
}

/*
    获取就绪命令
    参数: 
        const Command& cmd   传入cmd命令
        uint64_t clk    
    返回: 
        Command     情况1: cmd.cmd_type命令就绪
                    情况2: cmd.cmd_type的先决条件, 地址可能会改变(rank级命令)
                    情况3: 无效命令
    Notes: 具体实现中根据cmd.cmd_type分为rank级命令(REFRESH 或 SREF_ENTER 或 SREF_EXIT)和其他, 两者分开处理
*/
Command ChannelState::GetReadyCommand(const Command& cmd, uint64_t clk) const {
    Command ready_cmd = Command();  // 初始化实例一个Comman类
    // 是否为rank级命令
    if (cmd.IsRankCMD()) {
        int num_ready = 0;
        for (auto j = 0; j < config_.bankgroups; j++) {
            for (auto k = 0; k < config_.banks_per_group; k++) {
                // 遍历每个bank
                ready_cmd =
                    bank_states_[cmd.Rank()][j][k].GetReadyCommand(cmd, clk);
                if (!ready_cmd.IsValid()) {  // Not ready
                    continue;   // 立即停止当前迭代, 开启下一次循环
                }
                if (ready_cmd.cmd_type != cmd.cmd_type) {  // likely PRECHARGE  存在先决命令的情况
                    Address new_addr = Address(-1, cmd.Rank(), j, k, -1, -1);   // 新建一个地址类, 记录当前bank号
                    ready_cmd.addr = new_addr;  // 存入ready_cmd的地址属性中并返回
                    return ready_cmd;
                } else {
                    num_ready++;    // 传入cmd就绪计数自增
                }
            }
        }
        // All bank ready, 对于rank级的命令, 所有的bank对传入cmd已经就绪
        if (num_ready == config_.banks) {
            return ready_cmd;   // 直接返回
        } else {
            return Command();   // 返回无效命令类
        }
    } else {
    // 不是rank级命令
        // 从cmd的地址信息找到选择的bank, 调用GetReadyCommand
        ready_cmd = bank_states_[cmd.Rank()][cmd.Bankgroup()][cmd.Bank()]
                        .GetReadyCommand(cmd, clk);
        if (!ready_cmd.IsValid()) {     // 检查命令是否有效
            return Command();
        }
        if (ready_cmd.cmd_type == CommandType::ACTIVATE) {      // 激活命令需要检查是否满足4窗口限制
            if (!ActivationWindowOk(ready_cmd.Rank(), clk)) {
                return Command();
            }
        }
        return ready_cmd;
    }
}

/*
    给定cmd更新所有bank的状态
    参数: 
        const Command& cmd   传入cmd命令
    返回: 
    Notes: 具体实现中根据cmd.cmd_type分为rank级命令(REFRESH 或 SREF_ENTER 或 SREF_EXIT)和其他, 两者分开处理
           对刷新命令特殊判断以更新刷新和自刷新管理队列
*/
void ChannelState::UpdateState(const Command& cmd) {
    // Rank
    if (cmd.IsRankCMD()) {
        for (auto j = 0; j < config_.bankgroups; j++) {
            for (auto k = 0; k < config_.banks_per_group; k++) {
                bank_states_[cmd.Rank()][j][k].UpdateState(cmd);
            }
        }
        if (cmd.IsRefresh()) {      // 检查cmd是不是refresh
            RankNeedRefresh(cmd.Rank(), false);     // 将当前Rank从刷新队列中移除
        } else if (cmd.cmd_type == CommandType::SREF_ENTER) {       // 检查cmd是不是SREF_ENTER
            rank_is_sref_[cmd.Rank()] = true;       // 记录当前rank的自刷新状态
        } else if (cmd.cmd_type == CommandType::SREF_EXIT) {        // 检查cmd是不是SREF_EXIT
            rank_is_sref_[cmd.Rank()] = false;      // 
        }
    } else {
    // 
        bank_states_[cmd.Rank()][cmd.Bankgroup()][cmd.Bank()].UpdateState(cmd);
        if (cmd.IsRefresh()) {
            // 将当前Bank从刷新队列中移除
            BankNeedRefresh(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), false);
        }
    }
    return;
}

/*
    给定cmd更新时序状态
    参数: 
        const Command& cmd   传入cmd命令
        uint64_t clk
    返回: NULL
    Notes: 
*/
void ChannelState::UpdateTiming(const Command& cmd, uint64_t clk) {
    switch (cmd.cmd_type) {
        case CommandType::ACTIVATE:
            UpdateActivationTimes(cmd.Rank(), clk);     // 更新ACT命令时刻
        case CommandType::READ:
        case CommandType::READ_PRECHARGE:
        case CommandType::WRITE:
        case CommandType::WRITE_PRECHARGE:
        case CommandType::PRECHARGE:
        case CommandType::REFRESH_BANK:
            // TODO - simulator speed? - Speciazlize which of the below
            // functions to call depending on the command type  Same Bank
            // TODO-模拟器速度？-根据命令类型Same Bank，指定要调用以下哪个函数
            UpdateSameBankTiming(
                cmd.addr, timing_.same_bank[static_cast<int>(cmd.cmd_type)],
                clk);

            // Same Bankgroup other banks
            UpdateOtherBanksSameBankgroupTiming(
                cmd.addr,
                timing_
                    .other_banks_same_bankgroup[static_cast<int>(cmd.cmd_type)],
                clk);

            // Other bankgroups
            UpdateOtherBankgroupsSameRankTiming(
                cmd.addr,
                timing_
                    .other_bankgroups_same_rank[static_cast<int>(cmd.cmd_type)],
                clk);

            // Other ranks
            UpdateOtherRanksTiming(
                cmd.addr, timing_.other_ranks[static_cast<int>(cmd.cmd_type)],
                clk);
            break;
        // Rank级刷新命令
        case CommandType::REFRESH:
        case CommandType::SREF_ENTER:
        case CommandType::SREF_EXIT:
            UpdateSameRankTiming(
                cmd.addr, timing_.same_rank[static_cast<int>(cmd.cmd_type)],
                clk);
            break;
        default:        // 错误检查
            AbruptExit(__FILE__, __LINE__);
    }
    return;
}

/*
    更新由addr指向的bank时序
    参数: 
        const Address& addr   传入addr结构体
        const std::vector<std::pair<CommandType, int>>& cmd_timing_list  <命令, int>键值对 向量
        uint64_t clk    clk时钟值
    返回: NULL
    Notes: 
*/
void ChannelState::UpdateSameBankTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto cmd_timing : cmd_timing_list) {
        bank_states_[addr.rank][addr.bankgroup][addr.bank].UpdateTiming(
            cmd_timing.first, clk + cmd_timing.second);
    }
    return;
}

/*
    更新由addr指向的bank组内的其他其他bank时序
    参数: 
        const Address& addr   传入addr结构体
        const std::vector<std::pair<CommandType, int>>& cmd_timing_list  <命令, int>键值对 向量
        uint64_t clk    clk时钟值
    返回: NULL
    Notes: 
*/
void ChannelState::UpdateOtherBanksSameBankgroupTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto k = 0; k < config_.banks_per_group; k++) {
        // 遍历同bank组内的所有bank
        if (k != addr.bank) {   // 滤除addr指向的bank
            for (auto cmd_timing : cmd_timing_list) {
                bank_states_[addr.rank][addr.bankgroup][k].UpdateTiming(
                    cmd_timing.first, clk + cmd_timing.second);
            }
        }
    }
    return;
}

/*
    更新由addr指向的Rank中的Bank组时序
    参数: 
        const Address& addr   传入addr结构体
        const std::vector<std::pair<CommandType, int>>& cmd_timing_list  <命令, int>键值对 向量
        uint64_t clk    clk时钟值
    返回: NULL
    Notes: 
*/
void ChannelState::UpdateOtherBankgroupsSameRankTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto j = 0; j < config_.bankgroups; j++) {
        // 遍历bank组
        if (j != addr.bankgroup) {      // 滤除addr指向的bank组
            for (auto k = 0; k < config_.banks_per_group; k++) {
                // 再遍历每个bank
                for (auto cmd_timing : cmd_timing_list) {
                    bank_states_[addr.rank][j][k].UpdateTiming(
                        cmd_timing.first, clk + cmd_timing.second);
                }
            }
        }
    }
    return;
}

/*
    更新其他Rank的时序
    参数: 
        const Address& addr   传入addr结构体
        const std::vector<std::pair<CommandType, int>>& cmd_timing_list  <命令, int>键值对 向量
        uint64_t clk    clk时钟值
    返回: NULL
    Notes: 
*/
void ChannelState::UpdateOtherRanksTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto i = 0; i < config_.ranks; i++) {
        if (i != addr.rank) {       // 滤除当墙rank
            for (auto j = 0; j < config_.bankgroups; j++) {
                for (auto k = 0; k < config_.banks_per_group; k++) {
                    for (auto cmd_timing : cmd_timing_list) {
                        // 遍历每个bank
                        bank_states_[i][j][k].UpdateTiming(
                            cmd_timing.first, clk + cmd_timing.second);
                    }
                }
            }
        }
    }
    return;
}

/*
    更新addr形参指定的rank中的所有bank的cmd_timing_向量
    参数: 
        const Address& addr    地址结构体
        const std::vector<std::pair<CommandType, int>>& cmd_timing_list  <命令, int>键值对 向量
        uint64_t clk    clk时钟值
    返回: NULL
    Notes: 
*/
void ChannelState::UpdateSameRankTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto j = 0; j < config_.bankgroups; j++) {
        for (auto k = 0; k < config_.banks_per_group; k++) {
            // 遍历每一个bank
            for (auto cmd_timing : cmd_timing_list) {
                // .UpdateTiming()
                bank_states_[addr.rank][j][k].UpdateTiming(
                    cmd_timing.first, clk + cmd_timing.second);
            }
        }
    }
    return;
}

/*
    同时更新状态和时序
    参数: 
        const Command& cmd    命令
        uint64_t clk    clk时钟值
    返回: NULL
    Notes: 先更新状态再更新时序
*/
void ChannelState::UpdateTimingAndStates(const Command& cmd, uint64_t clk) {
    UpdateState(cmd);
    UpdateTiming(cmd, clk);
    return;
}

/*
    检查4激活窗口是否满足
    参数: 
        int rank
        uint64_t curr_time    clk时钟值
    返回: 
        bool    true -> , false -> 
    Notes: 有特判GDDR
*/
bool ChannelState::ActivationWindowOk(int rank, uint64_t curr_time) const {
    bool tfaw_ok = IsFAWReady(rank, curr_time);
    // 特判GDDR设置
    if (config_.IsGDDR()) {
        if (!tfaw_ok)
            return false;
        else
            return Is32AWReady(rank, curr_time);
    }
    return tfaw_ok;
}

/*
    更新激活时间点
    参数: 
        int rank    rank号
        uint64_t curr_time  clk时钟值
    返回: NULL
    Notes: 主要是更新four_aw_[rank]里面的tFAW结束时间
*/
void ChannelState::UpdateActivationTimes(int rank, uint64_t curr_time) {
    // 检查four_aw_[rank]是否为空, 并且传入clk是否大于four_aw_[rank][0]
    // 每次更新前都检查第一个元素, curr_time超出第一个元素记录的4AW宽度后就删除第一个元素
    if (!four_aw_[rank].empty() && curr_time >= four_aw_[rank][0]) {
        four_aw_[rank].erase(four_aw_[rank].begin());   // 删除掉four_aw_[rank]的第一个子元素
    }
    // 在容器的末尾记录当前clk+4个ACT窗口结束的时间
    four_aw_[rank].push_back(curr_time + config_.tFAW);
    // 特判是否为GDDR
    if (config_.IsGDDR()) {
        if (!thirty_two_aw_[rank].empty() &&
            curr_time >= thirty_two_aw_[rank][0]) {
            thirty_two_aw_[rank].erase(thirty_two_aw_[rank].begin());
        }
        thirty_two_aw_[rank].push_back(curr_time + config_.t32AW);
    }
    return;
}

/*
    检查4激活窗口是否满足(供ActivationWindowOk调用)
    参数: 
        int rank
        uint64_t curr_time    clk时钟值
    返回: 
        bool    true -> 满足激活时序, false -> 不满足激活时序要求(一个tFAW窗口内只能有4个激活命令)
    Notes: 
*/
bool ChannelState::IsFAWReady(int rank, uint64_t curr_time) const {
    // 检查传入rank的four_aw_是否为空
    if (!four_aw_[rank].empty()) {
        if (curr_time < four_aw_[rank][0] && four_aw_[rank].size() >= 4) {
            // 检查当前时间戳是否大于了第一个元素记录的tFAW值并且four_aw_的元素已经有了4个
            return false;
        }
    }
    return true;
}

bool ChannelState::Is32AWReady(int rank, uint64_t curr_time) const {
    if (!thirty_two_aw_[rank].empty()) {
        if (curr_time < thirty_two_aw_[rank][0] &&
            thirty_two_aw_[rank].size() >= 32) {
            return false;
        }
    }
    return true;
}

}  // namespace dramsim3
