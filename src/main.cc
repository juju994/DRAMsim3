#include <iostream>
#include "./../ext/headers/args.hxx"
#include "cpu.h"

using namespace dramsim3;

int main(int argc, const char **argv) {
    
    // arg[0]默认为可执行文件的路径
    for (int i = 0; i < argc; i++)
    {
        std::cout << "argc num: " << i << ". " << argv[i] << std::endl;
    }

    // 参数解析器实例对象 parser（用于解析命令行参数），创建一个命令行参数解析器，定义命令行参数
    args::ArgumentParser parser(
        "DRAM Simulator.",
        "Examples: \n."
        "./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -c 100 -t "
        "sample_trace.txt\n"
        "./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -s random -c 100");
    // Helpflag实例对象help （类型组，特殊字符串，帮助信息，匹配器）
    args::HelpFlag help(parser, "help", "Display the help menu", {'h', "help"});
    // 最后一个参数为默认参数
    args::ValueFlag<uint64_t> num_cycles_arg(parser, "num_cycles",
                                             "Number of cycles to simulate",
                                             {'c', "cycles"}, 100000);
    
    args::ValueFlag<std::string> output_dir_arg(
        parser, "output_dir", "Output directory for stats files",
        {'o', "output-dir"}, ".");

    // 流模式配置：地质流生成-随机，流
    args::ValueFlag<std::string> stream_arg(
        parser, "stream_type", "address stream generator - (random), stream",
        {'s', "stream"}, "");

    // trace文件，配置后将会忽略 -s option
    args::ValueFlag<std::string> trace_file_arg(
        parser, "trace",
        "Trace file, setting this option will ignore -s option",
        {'t', "trace"});
        
    args::Positional<std::string> config_arg(
        parser, "config", "The config file name (mandatory)");

    try {
        parser.ParseCLI(argc, argv);    //对入口参数进行解析
    } catch (args::Help) {      // 解析中触发了Help异常，表示帮助标志已经匹配
        std::cout << parser;
        return 0;
    } catch (args::ParseError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    std::string config_file = args::get(config_arg);
    if (config_file.empty()) {
        std::cerr << parser;
        return 1;
    }

    // ./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini --stream random -c 100000 

    uint64_t cycles = args::get(num_cycles_arg);
    std::string output_dir = args::get(output_dir_arg);
    std::string trace_file = args::get(trace_file_arg);
    std::string stream_type = args::get(stream_arg);

    std::cout << "------------------------"<< std::endl;
    std::cout << "printf debug parameter: "<< std::endl;
    std::cout << "clcles: "<< cycles << std::endl;
    std::cout << "output_dir: "<< output_dir << std::endl;
    std::cout << "trace_file: "<< trace_file << std::endl;
    std::cout << "stream_type: "<< stream_type << std::endl;

    CPU *cpu;
    if (!trace_file.empty()) {  // 如果trace_file字符串不是空的
        cpu = new TraceBasedCPU(config_file, output_dir, trace_file);
    } else {                    // trace_file为空
        // 流模式
        if (stream_type == "stream" || stream_type == "s") {
            cpu = new StreamCPU(config_file, output_dir);
        } else {    //随机模式
            cpu = new RandomCPU(config_file, output_dir);
        }
    }

    for (uint64_t clk = 0; clk < cycles; clk++) {
        cpu->ClockTick();
    }
    cpu->PrintStats();
    
    delete cpu;

 
    

    return 0;
}
