#include <pm_nixl/multi_gpu_transfer.hpp>
#include <iostream>
#include <iomanip>

using namespace pm_nixl;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Multi-GPU D2D Benchmark" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help              Show help" << std::endl;
    std::cout << "  -s, --size SIZE         Transfer size in bytes (default: 67108864 = 64MB)" << std::endl;
    std::cout << "  -w, --warmup N          Warmup iterations (default: 10)" << std::endl;
    std::cout << "  -i, --iterations N      Benchmark iterations (default: 100)" << std::endl;
    std::cout << "  -d, --devices DEV1,DEV2 Device IDs (default: 0,1)" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    multi_gpu_bench::D2DBenchmarkConfig config;
    
    // Parse arguments (simplified)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--size") {
            if (i + 1 < argc) {
                config.transfer_size = std::stoul(argv[++i]);
            }
        } else if (arg == "-w" || arg == "--warmup") {
            if (i + 1 < argc) {
                config.warmup_iterations = std::stoul(argv[++i]);
            }
        } else if (arg == "-i" || arg == "--iterations") {
            if (i + 1 < argc) {
                config.benchmark_iterations = std::stoul(argv[++i]);
            }
        } else if (arg == "-d" || arg == "--devices") {
            if (i + 1 < argc) {
                std::string devices = argv[++i];
                size_t comma_pos = devices.find(',');
                if (comma_pos != std::string::npos) {
                    config.src_dev_id = std::stoi(devices.substr(0, comma_pos));
                    config.dst_dev_id = std::stoi(devices.substr(comma_pos + 1));
                }
            }
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Multi-GPU D2D Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Transfer size: " << (config.transfer_size / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  Source device: " << config.src_dev_id << std::endl;
    std::cout << "  Destination device: " << config.dst_dev_id << std::endl;
    std::cout << "  Warmup iterations: " << config.warmup_iterations << std::endl;
    std::cout << "  Benchmark iterations: " << config.benchmark_iterations << std::endl;
    std::cout << std::endl;
    
    try {
        // Initialize multi-GPU engine
        MultiGpuConfig engine_config;
        engine_config.device_ids = {config.src_dev_id, config.dst_dev_id};
        engine_config.enable_p2p = config.use_p2p;
        
        MultiGpuTransferEngine engine(engine_config);
        auto result = engine.initialize();
        
        if (!result.has_value()) {
            std::cerr << "Error: Failed to initialize multi-GPU engine" << std::endl;
            return 1;
        }
        
        // Print topology
        engine.print_topology();
        std::cout << std::endl;
        
        // Check P2P support
        bool p2p_supported = engine.is_p2p_supported(config.src_dev_id, config.dst_dev_id);
        std::cout << "P2P supported: " << (p2p_supported ? "Yes" : "No") << std::endl;
        std::cout << std::endl;
        
        if (!p2p_supported) {
            std::cout << "Warning: P2P not supported between devices " 
                      << config.src_dev_id << " and " << config.dst_dev_id << std::endl;
            std::cout << "Falling back to host-mediated transfer" << std::endl;
            std::cout << std::endl;
        }
        
        // Run benchmark
        std::cout << "Running D2D benchmark..." << std::endl;
        double latency = multi_gpu_bench::run_d2d_benchmark(engine, config);
        
        // Calculate throughput
        double throughput_gbps = (config.transfer_size / 1024.0 / 1024.0 / 1024.0) / (latency / 1000.0);
        
        std::cout << std::endl;
        std::cout << "Results:" << std::endl;
        std::cout << "  Latency: " << std::fixed << std::setprecision(3) << latency << " ms" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << throughput_gbps << " GB/s" << std::endl;
        std::cout << std::endl;
        
        engine.shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
