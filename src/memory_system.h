#ifndef __MEMORY_SYSTEM__H
#define __MEMORY_SYSTEM__H

#include <functional>
#include <string>

#include "configuration.h"
#include "dram_system.h"
#include "hmc.h"

// dram_system和hmc的包装器

namespace dramsim3 {

// This should be the interface class that deals with CPU
class MemorySystem {
   public:
    // 构造
    MemorySystem(const std::string &config_file, const std::string &output_dir,
                // 这两个回调函数在CPU.h里面绑定好了
                 std::function<void(uint64_t)> read_callback,
                 std::function<void(uint64_t)> write_callback);
    // 析构
    ~MemorySystem();
    void ClockTick();               // 调用BaseDRAMSystem类的接口
    void RegisterCallbacks(std::function<void(uint64_t)> read_callback,
                           std::function<void(uint64_t)> write_callback);   // 调用BaseDRAMSystem类的接口
    double GetTCK() const;          // const关键字表示常量成员函数，函数不会修改config_内的值
    int GetBusBits() const;
    int GetBurstLength() const;
    int GetQueueSize() const;

    void PrintStats() const;        // 调用BaseDRAMSystem类的接口
    void ResetStats();              // 调用BaseDRAMSystem类的接口

    bool WillAcceptTransaction(uint64_t hex_addr, bool is_write) const;     // 调用BaseDRAMSystem类的接口
    bool AddTransaction(uint64_t hex_addr, bool is_write);                  // 调用BaseDRAMSystem类的接口

   private:
    // These have to be pointers because Gem5 will try to push this object
    // into container which will invoke a copy constructor, using pointers
    // here is safe
    Config *config_;
    BaseDRAMSystem *dram_system_;
};

// 表示返回类型为MemorySystem对象指针的函数，后面两个表示函数参数，uint64_t类型参数并返回void
MemorySystem* GetMemorySystem(const std::string &config_file, const std::string &output_dir,
                 std::function<void(uint64_t)> read_callback,
                 std::function<void(uint64_t)> write_callback);

}  // namespace dramsim3

#endif
