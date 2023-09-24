#ifndef __SIMPLE_STATS_
#define __SIMPLE_STATS_

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "configuration.h"
#include "json.hpp"

namespace dramsim3 {

class SimpleStats {
   public:
    SimpleStats(const Config& config, int channel_id);

    // incrementing counter 自增epoch计数器
    void Increment(const std::string name) { epoch_counters_[name] += 1; }

    // incrementing for vec counter 自增epoch矢量化计数器
    void IncrementVec(const std::string name, int pos) {
        epoch_vec_counters_[name][pos] += 1;
    }

    // increment vec counter by number  自增epoch计数器number
    void IncrementVecBy(const std::string name, int pos, int num) {
        epoch_vec_counters_[name][pos] += num;
    }

    // add historgram value     加入历史值
    void AddValue(const std::string name, const int value);

    // return per rank background energy 返回每个rank的背景能量
    double RankBackgroundEnergy(const int r) const;

    // Epoch update
    void PrintEpochStats();

    // Final statas output
    void PrintFinalStats();

    // Reset (usually after one phase of simulation)
    void Reset();

   private:
    // using语法使用VecStat定义一个无序map<字符串, 动态uint64_t数组>类型别名
    using VecStat = std::unordered_map<std::string, std::vector<uint64_t> >;
    using HistoCount = std::unordered_map<int, uint64_t>;
    using Json = nlohmann::json;
    void InitStat(std::string name, std::string stat_type,
                  std::string description);
    void InitVecStat(std::string name, std::string stat_type,
                     std::string description, std::string part_name,
                     int vec_len);
    void InitHistoStat(std::string name, std::string description, int start_val,
                       int end_val, int num_bins);

    void UpdateCounters();
    void UpdateHistoBins();
    void UpdatePrints(bool epoch);
    double GetHistoAvg(const HistoCount& histo_counts) const;
    std::string GetTextHeader(bool is_final) const;
    void UpdateEpochStats();
    void UpdateFinalStats();

    const Config& config_;
    int channel_id_;

    // map names to descriptions  用于描述map名字
    std::unordered_map<std::string, std::string> header_descs_;

    // counter stats, indexed by their name     计数器状态, 由name排序
    std::unordered_map<std::string, uint64_t> counters_;
    std::unordered_map<std::string, uint64_t> epoch_counters_;

    // vectored counter stats, first indexed by name then by index  矢量化计数器状态, 名字字符串+index
    VecStat vec_counters_;
    VecStat epoch_vec_counters_;

    // NOTE: doubles_ vec_doubles_ and calculated_ are basically one time
    // placeholders after each epoch they store the value for that epoch
    // (different from the counters) and in the end updated to the overall value
    // doubles_和vec_doubles_和calculated_基本上是每次epoch后的一次性占位符，它们存储该epoch的值（与计数器不同），并最终更新为总值
    std::unordered_map<std::string, double> doubles_;
    std::unordered_map<std::string, std::vector<double> > vec_doubles_;

    // calculated stats, similar to double, but not the same
    std::unordered_map<std::string, double> calculated_;

    // histogram stats 直方图统计
    std::unordered_map<std::string, std::vector<std::string> > histo_headers_;

    std::unordered_map<std::string, std::pair<int, int> > histo_bounds_;
    std::unordered_map<std::string, int> bin_widths_;
    std::unordered_map<std::string, HistoCount> histo_counts_;
    std::unordered_map<std::string, HistoCount> epoch_histo_counts_;
    VecStat histo_bins_;
    VecStat epoch_histo_bins_;

    // outputs
    Json j_data_;
    std::vector<std::pair<std::string, std::string> > print_pairs_;
};

}  // namespace dramsim3
#endif