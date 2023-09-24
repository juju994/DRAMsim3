#ifndef __TIMING_H
#define __TIMING_H

#include <vector>
#include "common.h"
#include "configuration.h"

namespace dramsim3 {

class Timing {
   public:
    // 时序参数构造函数, 由Config对象初始化
    Timing(const Config& config);
    /* 定义一个二维std::ector容器, 内部元素是std::pair类型的向量, std::pair元素包含一个CommandType枚举值和一个int值, 
       最后命名为same_bank代表了DRAM中相同bank的命令序列 */
    std::vector<std::vector<std::pair<CommandType, int> > > same_bank;
    std::vector<std::vector<std::pair<CommandType, int> > >
        other_banks_same_bankgroup;
    std::vector<std::vector<std::pair<CommandType, int> > >
        other_bankgroups_same_rank;
    std::vector<std::vector<std::pair<CommandType, int> > > other_ranks;
    std::vector<std::vector<std::pair<CommandType, int> > > same_rank;
};

}  // namespace dramsim3
#endif
