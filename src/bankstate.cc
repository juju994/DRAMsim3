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

void BankState::UpdateState(const Command& cmd) {
    switch (state_) {
        case State::OPEN:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::WRITE:
                    row_hit_count_++;
                    break;
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE_PRECHARGE:
                case CommandType::PRECHARGE:
                    state_ = State::CLOSED;
                    open_row_ = -1;
                    row_hit_count_ = 0;
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
        case State::CLOSED:
            switch (cmd.cmd_type) {
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                    break;
                case CommandType::ACTIVATE:
                    state_ = State::OPEN;
                    open_row_ = cmd.Row();
                    break;
                case CommandType::SREF_ENTER:
                    state_ = State::SREF;
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
        case State::SREF:
            switch (cmd.cmd_type) {
                case CommandType::SREF_EXIT:
                    state_ = State::CLOSED;
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
        default:
            AbruptExit(__FILE__, __LINE__);
    }
    return;
}

void BankState::UpdateTiming(CommandType cmd_type, uint64_t time) {
    cmd_timing_[static_cast<int>(cmd_type)] =
        std::max(cmd_timing_[static_cast<int>(cmd_type)], time);
    return;
}

}  // namespace dramsim3
