#include <pm_nixl/page_gather_engine.hpp>
#include <pm_nixl/buffer.hpp>
#include <pm_nixl/transfer_engine.hpp>
#include <iostream>
#include <iomanip>
#include <vector>

using namespace pm_nixl;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "KV Page Gather Benchmark" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help              Show help" << std::endl;
    std::cout << "  -p, --page-size SIZE    Page size in bytes (default: 65536)" << std::endl;
    std::cout << "  -n, --num-pages N       Number of pages (default: 100)" << std::endl;
    std::cout << "  -w, --warmup N          Warmup iterations (default: 10)" << std::endl;
    std::cout << "  -i, --iterations N      Benchmark iterations (default: 100)" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    page_gather_bench::GatherBenchmarkConfig config;
    
    // Parse arguments (simplified)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--page-size") {
            if (i + 1 < argc) {
                config.page_size = std::stoul(argv[++i]);
            }
        } else if (arg == "-n" || arg == "--num-pages") {
            if (i + 1 < argc) {
                config.num_pages = std::stoul(argv[++i]);
            }
        } else if (arg == "-w" || arg == "--warmup") {
            if (i + 1 < argc) {
                config.warmup_iterations = std::stoul(argv[++i]);
            }
        } else if (arg == "-i" || arg == "--iterations") {
            if (i + 1 < argc) {
                config.benchmark_iterations = std::stoul(argv[++i]);
            }
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  KV Page Gather Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Page size: " << config.page_size << " bytes (" 
              << (config.page_size / 1024) << " KB)" << std::endl;
    std::cout << "  Num pages: " << config.num_pages << std::endl;
    std::cout << "  Total size: " << (config.num_pages * config.page_size / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  Warmup iterations: " << config.warmup_iterations << std::endl;
    std::cout << "  Benchmark iterations: " << config.benchmark_iterations << std::endl;
    std::cout << std::endl;
    
    try {
        // Initialize transfer engine
        TransferEngineConfig transfer_config;
        TransferEngine transfer_engine(transfer_config);
        transfer_engine.initialize();
        
        // Create gather engine
        KvPageGatherEngine gather_engine(transfer_engine);
        
        // Create default gather order (sequential)
        config.gather_order.resize(config.num_pages);
        for (size_t i = 0; i < config.num_pages; ++i) {
            config.gather_order[i] = static_cast<uint32_t>(i);
        }
        
        // Run gather benchmark
        std::cout << "Running gather benchmark..." << std::endl;
        double gather_latency = page_gather_bench::run_gather_benchmark(gather_engine, config);
        
        // Calculate throughput
        size_t total_bytes = config.num_pages * config.page_size;
        double throughput_gbps = (total_bytes / 1024.0 / 1024.0 / 1024.0) / (gather_latency / 1000.0);
        
        std::cout << std::endl;
        std::cout << "Results:" << std::endl;
        std::cout << "  Gather latency: " << std::fixed << std::setprecision(3) 
                  << gather_latency << " ms" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                  << throughput_gbps << " GB/s" << std::endl;
        std::cout << std::endl;
        
        // Run copy benchmark for comparison
        std::cout << "Running copy benchmark..." << std::endl;
        double copy_latency = page_gather_bench::run_copy_benchmark(
            gather_engine,
            config.page_size,
            config.num_pages,
            config.warmup_iterations,
            config.benchmark_iterations
        );
        
        double copy_throughput_gbps = (total_bytes / 1024.0 / 1024.0 / 1024.0) / (copy_latency / 1000.0);
        
        std::cout << std::endl;
        std::cout << "Results:" << std::endl;
        std::cout << "  Copy latency: " << std::fixed << std::setprecision(3) 
                  << copy_latency << " ms" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                  << copy_throughput_gbps << " GB/s" << std::endl;
        std::cout << std::endl;
        
        // Summary
        std::cout << "========================================" << std::endl;
        std::cout << "  Summary" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;
        std::cout << "  Operation    Latency (ms)    Throughput (GB/s)" << std::endl;
        std::cout << "  -----------  --------------  -----------------" << std::endl;
        std::cout << "  Gather       " << std::fixed << std::setprecision(3) << std::setw(14) << gather_latency
                  << "  " << std::fixed << std::setprecision(2) << std::setw(17) << throughput_gbps << std::endl;
        std::cout << "  Copy         " << std::fixed << std::setprecision(3) << std::setw(14) << copy_latency
                  << "  " << std::fixed << std::setprecision(2) << std::setw(17) << copy_throughput_gbps << std::endl;
        std::cout << std::endl;
        
        transfer_engine.shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
