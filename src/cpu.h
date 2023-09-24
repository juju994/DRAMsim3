#ifndef __CPU_H
#define __CPU_H

#include <fstream>
#include <functional>
#include <random>
#include <string>
#include "memory_system.h"

namespace dramsim3 {

// 实现了3种简单CPU

class CPU {
   public:
    // 构造函数, 使用参数初始化列表(:)初始化构造函数中的成员变量, 把:前面的变量用于后面的初始化
    CPU(const std::string& config_file, const std::string& output_dir)
        : memory_system_(
                config_file, 
                output_dir,
                // std::bind用于将成员函数绑定到特定的参数, 参数1表示要绑定的成员函数的指针, this表示要绑定函数的对象指针, 即指向当前对象的指针, 最后一个表示1位占位符
                // 返回一个可调用对象,即函数对象或函数指针
                std::bind(&CPU::ReadCallBack, this, std::placeholders::_1),     
                std::bind(&CPU::WriteCallBack, this, std::placeholders::_1)     // 创建了对CPU类中回调函数的绑定, 使memory_system_获得了对这两个回调函数的引用
                        ),  //调用memory_system_属性的构造函数
          clk_(0) 
    {}
    // 可以不显式声明析构函数, 编译器会隐式生成一个默认的析构函数, 但是当类中有需要主动清理的资源时(如动态分配的内存, 打开的文件句柄等), 
    // 通常需要定义显示声明析构函数以进行相应的清理操作. 
    virtual void ClockTick() = 0;       // 纯虚函数, 没有实现, 在派生类中必须被重写实现

    // 回调函数好像没有实现任何逻辑,只是占位函数?
    void ReadCallBack(uint64_t addr) { return; }
    void WriteCallBack(uint64_t addr) { return; }

    void PrintStats() { memory_system_.PrintStats(); }  // 调用memory_system_类的PrintStats函数

    // protected关键词: 用于控制类成员的访问级别,
    // 受projected保护的成员在类内部和派生类中是可以访问的,在类外部是不可以直接访问的(即可以是属性也可以是方法)
   protected:
    MemorySystem memory_system_;
    uint64_t clk_;
};

// 随机CPU, 继承自CPU
class RandomCPU : public CPU {
   public:
    using CPU::CPU;             // 使用声明, 使得基类CPU中的构造函数对于派生类RandomCPU可见, 并且可以直接调用, 
    // 使得我们可以在RandomCPU对象的构造函数中使用与基类构造函数相同的参数列表来调用基类构造函数, 并获得相同的行为
    //对基类CPU中纯虚函数的重写, override关键词明确表示为对基类的重写
    
    // 以全速产生CPU请求，这对于利用DRAM协议的并行性非常有用并且不受地址映射和调度策略的影响
    void ClockTick() override;  
    

   private:
    uint64_t last_addr_;        // 上一次访问的地址
    bool last_write_ = false;   // 上一次是否为写入
    std::mt19937_64 gen;        // 用于实现随机数生成
    bool get_next_ = true;      // 是否获得下一个随机数
};

// 流CPU
class StreamCPU : public CPU {
   public:
    using CPU::CPU;
    // 流相加, 读取2个数组, 将它们相加到第三个数组. 这是一个非常简单的近似值, 但应该能够产生足够的缓冲区命中率
    void ClockTick() override;

   private:
    uint64_t addr_a_, addr_b_, addr_c_, offset_ = 0;    // 存储地址与偏移量
    std::mt19937_64 gen;        // 用于实现随机数生成
    bool inserted_a_ = false;   
    bool inserted_b_ = false;   
    bool inserted_c_ = false;   
    const uint64_t array_size_ = 2 << 20;  // elements in array 阵列中的元素数量
    const int stride_ = 64;                // stride in bytes   间隔64字节
};

// 基于追踪的CPU
class TraceBasedCPU : public CPU {
   public:
    TraceBasedCPU(const std::string& config_file, const std::string& output_dir,
                  const std::string& trace_file);
    // 析构函数, 关闭打开的trace文件
    ~TraceBasedCPU() { trace_file_.close(); }
    void ClockTick() override;

   private:
    std::ifstream trace_file_;
    Transaction trans_;
    bool get_next_ = true;
};

}  // namespace dramsim3
#endif
