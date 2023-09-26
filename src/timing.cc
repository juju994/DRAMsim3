#include "timing.h"
#include <algorithm>
#include <utility>

namespace dramsim3 {

Timing::Timing(const Config& config)
    : 
    // static_cast<int>(xxx)    编译时静态执行类型转换,  用于将CommandType枚举类的SIZE值转换为int类型
    // 指定了二维ComandType, int容器的尺寸
      same_bank(static_cast<int>(CommandType::SIZE)),                           // 相同bank
      other_banks_same_bankgroup(static_cast<int>(CommandType::SIZE)),          // 相同bank group中不同bank
      other_bankgroups_same_rank(static_cast<int>(CommandType::SIZE)),          // 相同rank中不同bank group
      other_ranks(static_cast<int>(CommandType::SIZE)),                         // 其他rank
      same_rank(static_cast<int>(CommandType::SIZE))                            // 相同rank
    // 参数化列表结束
    {
        int read_to_read_l = std::max(config.burst_cycle, config.tCCD_L);       // 相同bank组中bank列选通
        int read_to_read_s = std::max(config.burst_cycle, config.tCCD_S);       // 不同bank组中bank列选通
        int read_to_read_o = config.burst_cycle + config.tRTRS;                 // 其他rank中bank列选通
        
        int read_to_write = config.RL + config.burst_cycle - config.WL +        // 读后写命令间隔
                            config.tRTRS;
        int read_to_write_o = config.read_delay + config.burst_cycle +          // 其他rank中读后写命令间隔
                            config.tRTRS - config.write_delay;
        int read_to_precharge = config.AL + config.tRTP;                        // 读后预充电间隔, P217
        int readp_to_act =                                                      // 读后自动预充电到激活间隔, P219                                            
            config.AL + config.burst_cycle + config.tRTP + config.tRP;

        int write_to_read_l = config.write_delay + config.tWTR_L;               // 相同bank组写后读间隔, P241
        int write_to_read_s = config.write_delay + config.tWTR_S;               // 不同bank组写后读间隔, P240
        int write_to_read_o = config.write_delay + config.burst_cycle +         // 其他rank中写后读间隔
                            config.tRTRS - config.read_delay;

        int write_to_write_l = std::max(config.burst_cycle, config.tCCD_L);     // 相同bank组写后写间隔, P236
        int write_to_write_s = std::max(config.burst_cycle, config.tCCD_S);     // 不同bank组写后写间隔, P236
        int write_to_write_o = config.burst_cycle;                              // 其他rank中写后写间隔

        int write_to_precharge = config.WL + config.burst_cycle + config.tWR;   // 写后预充电间隔, P244

        int precharge_to_activate = config.tRP;                                 // 预充电到激活间隔
        int precharge_to_precharge = config.tPPD;                               // 
        int read_to_activate = read_to_precharge + precharge_to_activate;       // 读到激活间隔
        int write_to_activate = write_to_precharge + precharge_to_activate;     // 写到激活间隔

        int activate_to_activate = config.tRC;                                  // 激活到激活间隔
        int activate_to_activate_l = config.tRRD_L;                             // 相同bank激活到激活间隔, P145
        int activate_to_activate_s = config.tRRD_S;                             // 不同bank激活到激活间隔, P145
        int activate_to_precharge = config.tRAS;                                // 激活到预充电间隔

        int activate_to_read, activate_to_write;                                // 激活到写间隔, RAS和CAS延时减去附加延时
        if (config.IsGDDR() || config.IsHBM()) {    // GDDR或是HBM
            activate_to_read = config.tRCDRD;
            activate_to_write = config.tRCDWR;
        } else {                                    // DDR?
            activate_to_read = config.tRCD - config.AL;
            activate_to_write = config.tRCD - config.AL;
        }
        int activate_to_refresh =   // 激活到刷新间隔, 因为刷新前需要预充电, 所以即为激活->预充电->刷新, 就等于tRC
            config.tRC;  // need to precharge before ref, so it's tRC

        // TODO: deal with different refresh rate
        int refresh_to_refresh =                                                    // 平均刷新到刷新间隔 (每个rank级别)
            config.tREFI;  // refresh intervals (per rank level)
        int refresh_to_activate = config.tRFC;  // tRFC is defined as ref to act    // 刷新到下一个有效命令间隔 (激活或是刷新)
        int refresh_to_activate_bank = config.tRFCb;                                // 刷新到下一个有效命令间隔 (bank级别)

        int self_refresh_entry_to_exit = config.tCKESR;     // 保持自刷新最短时间
        int self_refresh_exit = config.tXS;                 // 退出自刷新最短时间
        // int powerdown_to_exit = config.tCKE;
        // int powerdown_exit = config.tXP;

        if (config.bankgroups == 1) {
            // for a bankgroup can be disabled, in that case
            // the value of tXXX_S should be used instead of tXXX_L
            // (because now the device is running at a lower freq)
            // we overwrite the following values so that we don't have
            // to change the assignement of the vectors
            read_to_read_l = std::max(config.burst_cycle, config.tCCD_S);
            write_to_read_l = config.write_delay + config.tWTR_S;
            write_to_write_l = std::max(config.burst_cycle, config.tCCD_S);
            activate_to_activate_l = config.tRRD_S;
        }

        // command READ
        same_bank[static_cast<int>(CommandType::READ)] =
        // std::vector<std::pair<CommandType, int> >{}  对括号内的
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, read_to_read_l},
                {CommandType::WRITE, read_to_write},
                {CommandType::READ_PRECHARGE, read_to_read_l},
                {CommandType::WRITE_PRECHARGE, read_to_write},
                {CommandType::PRECHARGE, read_to_precharge}};                   // 只有相同bank才能预充电
        other_banks_same_bankgroup[static_cast<int>(CommandType::READ)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, read_to_read_l},
                {CommandType::WRITE, read_to_write},
                {CommandType::READ_PRECHARGE, read_to_read_l},
                {CommandType::WRITE_PRECHARGE, read_to_write}};
        other_bankgroups_same_rank[static_cast<int>(CommandType::READ)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, read_to_read_s},
                {CommandType::WRITE, read_to_write},
                {CommandType::READ_PRECHARGE, read_to_read_s},
                {CommandType::WRITE_PRECHARGE, read_to_write}};
        other_ranks[static_cast<int>(CommandType::READ)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, read_to_read_o},
                {CommandType::WRITE, read_to_write_o},
                {CommandType::READ_PRECHARGE, read_to_read_o},
                {CommandType::WRITE_PRECHARGE, read_to_write_o}};

        // command WRITE
        same_bank[static_cast<int>(CommandType::WRITE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, write_to_read_l},
                {CommandType::WRITE, write_to_write_l},
                {CommandType::READ_PRECHARGE, write_to_read_l},
                {CommandType::WRITE_PRECHARGE, write_to_write_l},
                {CommandType::PRECHARGE, write_to_precharge}};                  // 只有相同bank才能预充电
        other_banks_same_bankgroup[static_cast<int>(CommandType::WRITE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, write_to_read_l},
                {CommandType::WRITE, write_to_write_l},
                {CommandType::READ_PRECHARGE, write_to_read_l},
                {CommandType::WRITE_PRECHARGE, write_to_write_l}};
        other_bankgroups_same_rank[static_cast<int>(CommandType::WRITE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, write_to_read_s},
                {CommandType::WRITE, write_to_write_s},
                {CommandType::READ_PRECHARGE, write_to_read_s},
                {CommandType::WRITE_PRECHARGE, write_to_write_s}};
        other_ranks[static_cast<int>(CommandType::WRITE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, write_to_read_o},
                {CommandType::WRITE, write_to_write_o},
                {CommandType::READ_PRECHARGE, write_to_read_o},
                {CommandType::WRITE_PRECHARGE, write_to_write_o}};

        // command READ_PRECHARGE
        same_bank[static_cast<int>(CommandType::READ_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, readp_to_act},
                {CommandType::REFRESH, read_to_activate},
                {CommandType::REFRESH_BANK, read_to_activate},
                {CommandType::SREF_ENTER, read_to_activate}};
        other_banks_same_bankgroup[static_cast<int>(CommandType::READ_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, read_to_read_l},
                {CommandType::WRITE, read_to_write},
                {CommandType::READ_PRECHARGE, read_to_read_l},
                {CommandType::WRITE_PRECHARGE, read_to_write}};
        other_bankgroups_same_rank[static_cast<int>(CommandType::READ_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, read_to_read_s},
                {CommandType::WRITE, read_to_write},
                {CommandType::READ_PRECHARGE, read_to_read_s},
                {CommandType::WRITE_PRECHARGE, read_to_write}};
        other_ranks[static_cast<int>(CommandType::READ_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, read_to_read_o},
                {CommandType::WRITE, read_to_write_o},
                {CommandType::READ_PRECHARGE, read_to_read_o},
                {CommandType::WRITE_PRECHARGE, read_to_write_o}};

        // command WRITE_PRECHARGE
        same_bank[static_cast<int>(CommandType::WRITE_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, write_to_activate},
                {CommandType::REFRESH, write_to_activate},
                {CommandType::REFRESH_BANK, write_to_activate},
                {CommandType::SREF_ENTER, write_to_activate}};
        other_banks_same_bankgroup[static_cast<int>(CommandType::WRITE_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, write_to_read_l},
                {CommandType::WRITE, write_to_write_l},
                {CommandType::READ_PRECHARGE, write_to_read_l},
                {CommandType::WRITE_PRECHARGE, write_to_write_l}};
        other_bankgroups_same_rank[static_cast<int>(CommandType::WRITE_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, write_to_read_s},
                {CommandType::WRITE, write_to_write_s},
                {CommandType::READ_PRECHARGE, write_to_read_s},
                {CommandType::WRITE_PRECHARGE, write_to_write_s}};
        other_ranks[static_cast<int>(CommandType::WRITE_PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::READ, write_to_read_o},
                {CommandType::WRITE, write_to_write_o},
                {CommandType::READ_PRECHARGE, write_to_read_o},
                {CommandType::WRITE_PRECHARGE, write_to_write_o}};

        // command ACTIVATE !!!激活!!!
        same_bank[static_cast<int>(CommandType::ACTIVATE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, activate_to_activate},
                {CommandType::READ, activate_to_read},
                {CommandType::WRITE, activate_to_write},
                {CommandType::READ_PRECHARGE, activate_to_read},
                {CommandType::WRITE_PRECHARGE, activate_to_write},
                {CommandType::PRECHARGE, activate_to_precharge},
            };
        other_banks_same_bankgroup[static_cast<int>(CommandType::ACTIVATE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, activate_to_activate_l},
                {CommandType::REFRESH_BANK, activate_to_refresh}};
        other_bankgroups_same_rank[static_cast<int>(CommandType::ACTIVATE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, activate_to_activate_s},
                {CommandType::REFRESH_BANK, activate_to_refresh}};

        // command PRECHARGE
        same_bank[static_cast<int>(CommandType::PRECHARGE)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, precharge_to_activate},
                {CommandType::REFRESH, precharge_to_activate},
                {CommandType::REFRESH_BANK, precharge_to_activate},
                {CommandType::SREF_ENTER, precharge_to_activate}};
        // for those who need tPPD
        if (config.IsGDDR() || config.protocol == DRAMProtocol::LPDDR4) {
            other_banks_same_bankgroup[static_cast<int>(CommandType::PRECHARGE)] =
                std::vector<std::pair<CommandType, int> >{
                    {CommandType::PRECHARGE, precharge_to_precharge},
                };
            other_bankgroups_same_rank[static_cast<int>(CommandType::PRECHARGE)] =
                std::vector<std::pair<CommandType, int> >{
                    {CommandType::PRECHARGE, precharge_to_precharge},
                };
        }

        // command REFRESH_BANK
        same_rank[static_cast<int>(CommandType::REFRESH_BANK)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, refresh_to_activate_bank},
                {CommandType::REFRESH, refresh_to_activate_bank},
                {CommandType::REFRESH_BANK, refresh_to_activate_bank},
                {CommandType::SREF_ENTER, refresh_to_activate_bank}};
        other_banks_same_bankgroup[static_cast<int>(CommandType::REFRESH_BANK)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, refresh_to_activate},
                {CommandType::REFRESH_BANK, refresh_to_refresh},
            };
        other_bankgroups_same_rank[static_cast<int>(CommandType::REFRESH_BANK)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, refresh_to_activate},
                {CommandType::REFRESH_BANK, refresh_to_refresh},
            };
        // REFRESH, SREF_ENTER and SREF_EXIT are isued to the entire
        // rank  command REFRESH
        same_rank[static_cast<int>(CommandType::REFRESH)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, refresh_to_activate},
                {CommandType::REFRESH, refresh_to_activate},
                {CommandType::SREF_ENTER, refresh_to_activate}};

        // command SREF_ENTER
        // TODO: add power down commands
        same_rank[static_cast<int>(CommandType::SREF_ENTER)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::SREF_EXIT, self_refresh_entry_to_exit}};

        // command SREF_EXIT
        same_rank[static_cast<int>(CommandType::SREF_EXIT)] =
            std::vector<std::pair<CommandType, int> >{
                {CommandType::ACTIVATE, self_refresh_exit},
                {CommandType::REFRESH, self_refresh_exit},
                {CommandType::REFRESH_BANK, self_refresh_exit},
                {CommandType::SREF_ENTER, self_refresh_exit}};
    }

}  // namespace dramsim3
