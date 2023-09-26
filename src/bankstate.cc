#include "bankstate.h"

namespace dramsim3 {

// 构造函数参数列表初始化为bank关闭, open_row=-1, row_hit=0
BankState::BankState()
    : state_(State::CLOSED),
      // static_cast<int>将枚举类转换为int, 将cmd_timing_初始化为一个大小为SIZE的动态数组
      cmd_timing_(static_cast<int>(CommandType::SIZE)),     
      open_row_(-1),
      row_hit_count_(0) {
    cmd_timing_[static_cast<int>(CommandType::READ)] = 0;
    cmd_timing_[static_cast<int>(CommandType::READ_PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::WRITE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::WRITE_PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::ACTIVATE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::REFRESH)] = 0;
    cmd_timing_[static_cast<int>(CommandType::SREF_ENTER)] = 0;
    cmd_timing_[static_cast<int>(CommandType::SREF_EXIT)] = 0;
}

/*
    每个bank检查获取就绪命令
    参数: 
        const Command& cmd   传入cmd命令
        uint64_t clk         clk时序要求
    返回: 
        Command     情况1: 返回空的Comamnd(), 即 cmd.cmd_type == CommandType::SIZE
                    情况2: 返回与传入cmd.cmd_type不一样的cmd_type, 即表示为传入命令的前提条件
                    情况3: 返回与传入cmd.cmd_type相等的cmd_type, 即表示当前命令已经就绪
                    情况4: 命令时序致命错误, 直接报错退出
    Notes: 检查完成的输出命令还需要满足cmd_timing_中的时序要求. 时序条件满足后返回一个Command类输出, 
           其中.cmd_type为就绪命令或前提命令, .addr等信息与传入cmd.addr相同
*/
Command BankState::GetReadyCommand(const Command& cmd, uint64_t clk) const {
    // required_type的默认值为SIZE
    CommandType required_type = CommandType::SIZE;
    switch (state_) {
        case State::CLOSED:             // bank关闭
            switch (cmd.cmd_type) {
                // switch-case结构没有break时, 匹配到case后会因为没有break而执行一个case的逻辑, 直到break关键字
                case CommandType::READ:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE:
                case CommandType::WRITE_PRECHARGE:
                    required_type = CommandType::ACTIVATE;  // 读写命令前需要激活命令
                    break;
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                    required_type = cmd.cmd_type;       // 刷新可以直接开始
                    break;
                default:        // 错误处理
                    std::cerr << "Unknown type!" << std::endl;
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::OPEN:             // bank打开
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE:
                case CommandType::WRITE_PRECHARGE:
                    if (cmd.Row() == open_row_) {       // 检查当前bak的打开行是不是命令指向的行地址
                        required_type = cmd.cmd_type;       // bank打开且选定行打开即可直接读写
                    } else {
                        required_type = CommandType::PRECHARGE;     // 选定行未打开, 需要先预充电关闭活动行
                    }
                    break;
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                    required_type = CommandType::PRECHARGE;     // 想开启刷新操作就需要先预充电关闭bank
                    break;
                default:        // 错误处理
                    std::cerr << "Unknown type!" << std::endl;
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::SREF:             // bank自刷新
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE:
                case CommandType::WRITE_PRECHARGE:
                    required_type = CommandType::SREF_EXIT;     // 在自刷新模式下读写需要先退出自刷新
                    break;
                default:        // 错误处理
                    std::cerr << "Unknown type!" << std::endl;
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::PD:             // bank掉电
        case State::SIZE:           // 错误处理
            std::cerr << "In unknown state" << std::endl;
            AbruptExit(__FILE__, __LINE__);
            break;
    }
    // 检查请求的前提命令是否改变, 不等于SIZE即位上面逻辑匹配成功
    if (required_type != CommandType::SIZE) {
        // 检查clk时序要求, 满足时序要求时返回Command类: 前提命令, 命令地址, 十六进制地址参数
        if (clk >= cmd_timing_[static_cast<int>(required_type)]) {
            return Command(required_type, cmd.addr, cmd.hex_addr);
        }
    }
    // required_type没有更新, 或是时序不满足, 返回required_type = SIZE, 且十六进制地址为0
    return Command();
}

// 根据当前bank状态和输入cmd参数更新bank状态, 如果存在非法命令则直接报错
void BankState::UpdateState(const Command& cmd) {
    switch (state_) {
        case State::OPEN:                   // bank已经打开
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::WRITE:
                    row_hit_count_++;       // bank以及打开且为读写命令就自增row_hit计数器
                    break;
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE_PRECHARGE:
                case CommandType::PRECHARGE:
                    state_ = State::CLOSED; // 状态更新为关闭
                    open_row_ = -1;         // 复位打开行
                    row_hit_count_ = 0;     // 清零row_hit计数器
                    break;
                case CommandType::ACTIVATE:
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                case CommandType::SREF_EXIT:
                default:        // 错误处理
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        case State::CLOSED:                   // bank已经关闭
            switch (cmd.cmd_type) {
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                    break;                          // 刷新和bank级刷新都直接退出 (保持关闭状态?)
                case CommandType::ACTIVATE: 
                    state_ = State::OPEN;           // 激活命令会把bank状态改为打开
                    open_row_ = cmd.Row();          // 更新打开行
                    break;
                case CommandType::SREF_ENTER:
                    state_ = State::SREF;           // 进入自刷新模式
                    break;
                case CommandType::READ:
                case CommandType::WRITE:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE_PRECHARGE:
                case CommandType::PRECHARGE:
                case CommandType::SREF_EXIT:
                default:        // 错误处理
                    std::cout << cmd << std::endl;
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        case State::SREF:                   // 自刷新模式
            switch (cmd.cmd_type) {
                case CommandType::SREF_EXIT:
                    state_ = State::CLOSED;     // 退出自刷新模式回到关闭模式
                    break;
                case CommandType::READ:
                case CommandType::WRITE:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE_PRECHARGE:
                case CommandType::ACTIVATE:
                case CommandType::PRECHARGE:
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                default:        // 错误处理
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        default:                   // 错误处理
            AbruptExit(__FILE__, __LINE__);
    }
    return;
}

/*
    根据传入cmd_type在cmd_timing_中更新对应命令中的可执行时刻点
    参数: 
        CommandType cmd_type
        uint64_t time       传入时刻点
    返回: 
        NULL
    Notes: 对BankState.cmd_timing_[CommandType]<int> 进行操作
*/
void BankState::UpdateTiming(CommandType cmd_type, uint64_t time) {
    cmd_timing_[static_cast<int>(cmd_type)] =       // 先把cmd_type变换为int型, 用于在cmd_timing_中寻址 (每种命令一个index, 对应值放的time)
        std::max(cmd_timing_[static_cast<int>(cmd_type)], time);    // 比较cmd_timing_里的值和传入的time参数, 取最大值更新cmd_timing_对于位置
    return;
}

}  // namespace dramsim3
