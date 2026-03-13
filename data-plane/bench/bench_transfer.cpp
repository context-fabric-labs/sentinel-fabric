#include <pm_nixl/benchmark.hpp>
#include <iostream>
#include <cstring>
#include <getopt.h>

using namespace pm_nixl;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Transfer benchmark harness for PM-NIXL" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help              Show this help message" << std::endl;
    std::cout << "  -s, --sizes SIZES       Comma-separated list of sizes (e.g., 1024,4096,16384)" << std::endl;
    std::cout << "  -w, --warmup N          Number of warmup iterations (default: 10)" << std::endl;
    std::cout << "  -i, --iterations N      Number of benchmark iterations (default: 100)" << std::endl;
    std::cout << "  -d, --device N          CUDA device ID (default: 0)" << std::endl;
    std::cout << "  -n, --no-compare        Disable pinned vs pageable comparison" << std::endl;
    std::cout << "  -c, --csv FILE          Save results to CSV file" << std::endl;
    std::cout << "  -j, --json FILE         Save results to JSON file" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << std::endl;
    std::cout << "  " << program << " -s 1024,4096,16384 -i 50" << std::endl;
    std::cout << "  " << program << " -w 20 -i 200 -c results.csv" << std::endl;
    std::cout << "  " << program << " -n -j results.json" << std::endl;
    std::cout << std::endl;
}

std::vector<size_t> parse_sizes(const std::string& sizes_str) {
    std::vector<size_t> sizes;
    std::stringstream ss(sizes_str);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        try {
            size_t size = std::stoull(item);
            if (size > 0) {
                sizes.push_back(size);
            }
        } catch (...) {
            std::cerr << "Warning: Invalid size '" << item << "' ignored" << std::endl;
        }
    }
    
    return sizes;
}

int main(int argc, char** argv) {
    BenchmarkConfig config;
    
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"sizes", required_argument, 0, 's'},
        {"warmup", required_argument, 0, 'w'},
        {"iterations", required_argument, 0, 'i'},
        {"device", required_argument, 0, 'd'},
        {"no-compare", no_argument, 0, 'n'},
        {"csv", required_argument, 0, 'c'},
        {"json", required_argument, 0, 'j'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "hs:w:i:d:nc:j:", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
                
            case 's':
                config.transfer_sizes = parse_sizes(optarg);
                if (config.transfer_sizes.empty()) {
                    std::cerr << "Error: No valid sizes provided" << std::endl;
                    return 1;
                }
                break;
                
            case 'w':
                config.warmup_iterations = std::stoul(optarg);
                break;
                
            case 'i':
                config.benchmark_iterations = std::stoul(optarg);
                break;
                
            case 'd':
                config.device_id = std::stoi(optarg);
                break;
                
            case 'n':
                config.compare_pinned_vs_pageable = false;
                break;
                
            case 'c':
                config.output_csv = true;
                config.output_file = optarg;
                break;
                
            case 'j':
                config.output_json = true;
                config.output_file = optarg;
                break;
                
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    std::cout << "PM-NIXL Transfer Benchmark" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Transfer sizes: " << config.transfer_sizes.size() << " sizes" << std::endl;
    std::cout << "  Warmup iterations: " << config.warmup_iterations << std::endl;
    std::cout << "  Benchmark iterations: " << config.benchmark_iterations << std::endl;
    std::cout << "  Device: " << config.device_id << std::endl;
    std::cout << "  Compare pinned vs pageable: " << (config.compare_pinned_vs_pageable ? "yes" : "no") << std::endl;
    std::cout << std::endl;
    
    try {
        // Run benchmarks
        TransferBenchmark benchmark(config);
        auto results = benchmark.run();
        
        // Print results
        benchmark.print_results(results);
        
        // Save to file if requested
        if (config.output_csv && !config.output_file.empty()) {
            benchmark.save_csv(results, config.output_file);
        }
        
        if (config.output_json && !config.output_file.empty()) {
            benchmark.save_json(results, config.output_file);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
