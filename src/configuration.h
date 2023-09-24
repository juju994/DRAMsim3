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

        例如: DDR4_8Gb_x8_3200.ini配置文件中    rochrababgco
        地址映射区域    具体指代                      field_widths    field_pos             掩码
            ro      rows = 65536 = 2^16                16        ro_pos = 12(6)     ro_mask = 0000 1111 1111 1111 1111
            ch      channels = 1                        0        ch_pos = 12(5)     ch_mask = 0000 0000 0000 0000 0000
            ra      ranks = 2                           1        ra_pos = 11(4)     ra_mask = 0000 0000 0000 0000 0001
            ba      banks_per_group = 4                 2        ba_pos = 9(3)      ba_mask = 0000 0000 0000 0000 0011
            bg      bankgroups = 4                      2        bg_pos = 7(2)      bg_mask = 0000 0000 0000 0000 0011
            co      actual_col_bits = 7 (10-3)          7        co_pos = 0(1)      co_mask = 0000 0000 0000 0111 1111
                                                    一共28位, 加上3位突发就是31位地址
            最后减掉的3位对应每rank里面的die数量(8个, 突发长度=8)
    */ 
    int shift_bits;                                                         // 请求位数, 请求字节数 = 总线宽度/8*突发长度
    int ch_pos, ra_pos, bg_pos, ba_pos, ro_pos, co_pos;                     // 地址区域位置(即区域开始的位数)
    uint64_t ch_mask, ra_mask, bg_mask, ba_mask, ro_mask, co_mask;          // 地址区域掩码

    // Generic DRAM timing parameters   DRAM时序参数
    double tCK;         // Minimum Clock Cycle Time(DLL off mode)   最小时钟周期, 内存频率周期为时钟周期的两倍    default: 0.63 
    int burst_cycle;    // seperate BL with timing since for GDDRx it's not BL/2 GDDRx的burst_cycle时序参数不是BL/2
    int AL;             // CAS Additive Latency 附加延时     default: 0
    int CL;             // CAS(column address select Latency)延迟 地址读命令信号激活后到第一位数据输出的等待周期    default: 22
    int CWL;            // CAS Write Latency    CAS写命令信号激活后到第一位数据输入的等待周期   default: 16
    int RL;             // RL = AL + CL     ??必须>tRCD??   default: 22
    int WL;             // WL = AL + CWL        default: 16
    int tCCD_L;         // CAS_n-to-CAS_n delay (long) (列选通到列选通间隔)适用于同一 bank组 之间的连续CAS_n(读到读或者写到写都有)         P91    default: 8
    int tCCD_S;         // CAS_n-to-CAS_n delay (short) (列选通到列选通间隔)适用于不同 bank组 之间的连续CAS_n时间(读到读或者写到写都有)     P91    default: 4
    int tRTRS;          // 读到读或写命令附加延时, 需要满足数据线突发输出时序和数据选通前导码的时序要求      ???? default: 1
    int tRTP;           // 读到预充电间隔   Internal READ Command to PRECHARGE Command delay default: 12
    int tWTR_L;         // 写命令到读命令的间隔(同一bank组)   P93     default: 12
    int tWTR_S;         // 写命令到读命令的间隔(不同bank组)   P93     default: 4
    int tWR;            // Write Recovery   写恢复时间, 从写入数据的最后一位开始算之后下个命令的间隔时间      default: 24
    int tRP;            // RAS Precharge Time   行预充电时间(使用precharge关闭一行后需要等待tRP才能寻址另一行)    default: 22
    int tRRD_L;         // (行激活间隔)row to row ACTIVATE to ACTIVATE Command period (long)  适用于 同一bank组但不同bank 的连续RAS     P92    default: 8
    int tRRD_S;         // (行激活间隔)row to row ACTIVATE to ACTIVATE Command period (short)  适用于 不同bank组 的连续RAS             P92    default: 4
    int tRAS;           // minimum ACT to PRE timing    default: 52
    int tRCD;           // RAS-to-CAS Delay 行寻址至列寻址延迟时间,这个延时是为了满足命令激活到读/写命令的切换  default: 22
    int tRFC;           // REFRESH命令和下一个有效命令(也可以是下一个REFRESH)之间的延迟    default: 560     构造初始: 74
    int tRC;            // ACT to ACT or REF command period tRC = tRAS + tRP    default: NULL
    // tCKSRE and tCKSRX are only useful for changing clock freq after entering SRE mode we are not doing that, so tCKESR is sufficient
    int tCKESR;         // Minimum CKE low width for Self refresh entry to exit timing保持自刷新模式的最短时间 default: 9
    int tXS;            // Exit Self Refresh to commands not requiring a locked DLL     default: 576
    
    int tCKE;           // CKE minimum pulse width      default: 8  P162
    int tXP;            // DLL打开情况下, 退出掉电模式到下一个有效命令      default: 10 P162
    
    int tRFCb;          // default: NULL 构造初始: 20
    int tREFI;          // REFRESH命令的平均间隔    default: 12480  构造初始: 7800
    int tREFIb;         // default: NULL 构造初始: 1950
    int tFAW;           // Four activate window 4个激活窗口的间隔(一个tFAW里面只能有4个ACT命令) = 4*tRRD  P93 default: 34
    int tRPRE;          // read preamble are important (读前延时 前导码)   default: 1
    int tWPRE;          // write preamble are important (写前延时 前导码)      default: 1
    int read_delay;     // 读延时(计算得到) read_delay = RL + burst_cycle
    int write_delay;    // 写延时(计算得到) write_delay = WL + burst_cycle

    // LPDDR4 and GDDR5
    int tPPD;           // 
    // GDDR5
    int t32AW;          // 
    int tRCDRD;         // 
    int tRCDWR;         // 

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


    int epoch_period;                           // epoch周期
    int output_level;                           // 输出级别
    std::string output_dir;                     // 输出路径, 默认输出到根目录
    std::string output_prefix;                  // 输出前缀,可配置
    std::string json_stats_name;                // output_prefix.json   
    std::string json_epoch_name;                // output_prefixepoch.json   
    std::string txt_stats_name;                 // output_prefix.txt   

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
    INIReader* reader_;     //.INI文件解析器
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
