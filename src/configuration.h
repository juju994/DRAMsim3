#ifndef __CONFIG_H
#define __CONFIG_H

#include <fstream>
#include <string>
#include "common.h"

#include "INIReader.h"

// 初始化并管理系统和DRAM参数，包括协议，DRAM时序，地址映射策略和功耗管理

namespace dramsim3 {

// DRAM协议
enum class DRAMProtocol {
    DDR3,
    DDR4,
    GDDR5,
    GDDR5X,
    GDDR6,
    LPDDR,
    LPDDR3,
    LPDDR4,
    HBM,
    HBM2,
    HMC,
    SIZE
};

// 刷新策略 RANK级同步/RANK级交错/BANK级交错
enum class RefreshPolicy {
    RANK_LEVEL_SIMULTANEOUS,  // impractical due to high power requirement (由于功耗太好不太现实)
    RANK_LEVEL_STAGGERED,
    BANK_LEVEL_STAGGERED,
    SIZE 
};


class Config {
   public:
    Config(std::string config_file, std::string out_dir);       // 构造函数
    Address AddressMapping(uint64_t hex_addr) const;            // 地址映射
    // DRAM physical structure DRAM物理层
    DRAMProtocol protocol;              // 协议                       
    int channel_size;                   // 通道大小(MB) 整个系统的channel尺寸(也可理解为总容量)
    int channels;                       // 通道数量
    int ranks;                          // rank的数量 = channel_size / 每rank容量(MB) 计算得到
    int banks;                          // bank总数量 = bankgroups * banks_per_group 计算得到
    int bankgroups;                     // bank组
    int banks_per_group;                // 每组bank数量
    int rows;                           // 行数
    int columns;                        // 列数     为避免歧义, 配置文件中colums特指列的物理宽度
    int device_width;                   // die位宽
    int bus_width;                      // 总线宽度(即每个rank的数据位数)
    int devices_per_rank;               // 每rank的die个数 = bus_width / device_width  计算得到
    int BL;                             // 突发长度

    // Address mapping numbers  地址映射参数
    /*
        地址映射区域    具体指代
            ch        channels
            ra        ranks
            bg        bankgroups
            ba        banks_per_group
            ro        rows
            co        actual_col_bits = 列位数 - 列地址低位的位数(受突发长度交错影响)
        例如: DDR4_8Gb_x8_3200.ini配置文件中
        具体指代                        field_widths
        channels = 1                    
        ranks = 2                       
        bankgroups = 4                  
        banks_per_group = 4
        rows = 2^16
        actual_col_bits = 7 (10-3)
    */ 
    int shift_bits;                                                         // 请求位数, 请求字节数 = 总线宽度/8*突发长度
    int ch_pos, ra_pos, bg_pos, ba_pos, ro_pos, co_pos;                     // 地址区域位置
    uint64_t ch_mask, ra_mask, bg_mask, ba_mask, ro_mask, co_mask;          // 地址区域掩码

    // Generic DRAM timing parameters   DRAM时序参数
    double tCK;
    int burst_cycle;  // seperate BL with timing since for GDDRx it's not BL/2 GDDRx的burst_cycle时序参数不是BL/2
    int AL;
    int CL;
    int CWL;
    int RL;             // RL = AL + CL
    int WL;             // WL = AL + CWL
    int tCCD_L;
    int tCCD_S;
    int tRTRS;
    int tRTP;
    int tWTR_L;
    int tWTR_S;
    int tWR;
    int tRP;
    int tRRD_L;
    int tRRD_S;
    int tRAS;
    int tRCD;
    int tRFC;
    int tRC;
    // tCKSRE and tCKSRX are only useful for changing clock freq after entering
    // SRE mode we are not doing that, so tCKESR is sufficient
    int tCKE;
    int tCKESR;
    int tXS;
    int tXP;
    int tRFCb;
    int tREFI;
    int tREFIb;
    int tFAW;
    int tRPRE;  // read preamble and write preamble are important
    int tWPRE;
    int read_delay;     // 读延时(计算得到) read_delay = RL + burst_cycle
    int write_delay;    // 写延时(计算得到) write_delay = WL + burst_cycle

    // LPDDR4 and GDDR5
    int tPPD;
    // GDDR5
    int t32AW;
    int tRCDRD;
    int tRCDWR;

    // pre calculated power parameters  计算得到每个指令的功耗参数
    double act_energy_inc;
    double pre_energy_inc;
    double read_energy_inc;
    double write_energy_inc;
    double ref_energy_inc;
    double refb_energy_inc;
    double act_stb_energy_inc;
    double pre_stb_energy_inc;
    double pre_pd_energy_inc;
    double sref_energy_inc;

    // HMC
    int num_links;
    int num_dies;                               // die的数量
    int link_width;
    int link_speed;
    int num_vaults;
    int block_size;  // block size in bytes
    int xbar_queue_depth;

    // System
    std::string address_mapping;                // 地址映射(一定要12个字符)
    std::string queue_structure;                // 队列结构        
    std::string row_buf_policy;                 // 行缓存器策略
    RefreshPolicy refresh_policy;               // 刷新策略
    int cmd_queue_size;                         // 命令队列尺寸
    bool unified_queue;                         // 统一队列？
    int trans_queue_size;                       // ？队列尺寸
    int write_buf_size;                         // 写buf尺寸
    bool enable_self_refresh;                   // 使能自刷新
    int sref_threshold;                         // 自刷新阈值？
    bool aggressive_precharging_enabled;        // 严格预充电使能
    bool enable_hbm_dual_cmd;                   // 是能HBM双指令？


    int epoch_period;                           // epoch周期？
    int output_level;                           // 输出级别
    std::string output_dir;                     // 输出路径
    std::string output_prefix;                  // 输出前缀
    std::string json_stats_name;                // 
    std::string json_epoch_name;                // 
    std::string txt_stats_name;                 // 

    // Computed parameters
    int request_size_bytes;

    bool IsGDDR() const {
        return (protocol == DRAMProtocol::GDDR5 ||
                protocol == DRAMProtocol::GDDR5X ||
                protocol == DRAMProtocol::GDDR6);
    }
    bool IsHBM() const {
        return (protocol == DRAMProtocol::HBM ||
                protocol == DRAMProtocol::HBM2);
    }
    bool IsHMC() const { return (protocol == DRAMProtocol::HMC); }  // 如果protocol和DRAMProtocol::HMC就是true(是HMC), 不相等就是false(不是HMC)
    // yzy: add another function
    bool IsDDR4() const { return (protocol == DRAMProtocol::DDR4); }

    int ideal_memory_latency;

#ifdef THERMAL
    std::string loc_mapping;
    int num_row_refresh;       // number of rows to be refreshed for one time
    double amb_temp;         // the ambient temperature in [C]
    double const_logic_power;

    double chip_dim_x;
    double chip_dim_y;
    int num_x_grids;
    int num_y_grids;
    int mat_dim_x;
    int mat_dim_y;
    // 0: x-direction priority, 1: y-direction priority
    int bank_order;
    // 0; low-layer priority, 1: high-layer priority
    int bank_layer_order;
    int row_tile;
    int tile_row_num;
    double bank_asr;  // the aspect ratio of a bank: #row_bits / #col_bits
#endif  // THERMAL

    // 私有参数!
   private:
    INIReader* reader_;
    void CalculateSize();
    DRAMProtocol GetDRAMProtocol(std::string protocol_str);
    int GetInteger(const std::string& sec, const std::string& opt,
                   int default_val) const;
    void InitDRAMParams();
    void InitOtherParams();
    void InitPowerParams();
    void InitSystemParams();
#ifdef THERMAL
    void InitThermalParams();
#endif  // THERMAL
    void InitTimingParams();
    void SetAddressMapping();
};

}  // namespace dramsim3
#endif
