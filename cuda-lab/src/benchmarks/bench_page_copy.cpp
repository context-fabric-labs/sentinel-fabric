#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

#include <cuda_runtime.h>
#include "kernels/page_copy_naive.cu"
#include "benchmarks/bench_utils.hpp"

/**
 * Parse command line arguments
 */
struct BenchmarkConfig {
    size_t page_size = 65536;  // 64KB default
    size_t num_pages = 100;
    size_t warmup_iterations = 10;
    size_t benchmark_iterations = 100;
    bool output_json = false;
    std::string output_file;
    
    void print_usage(const char* program) {
        std::cout << "Usage: " << program << " [OPTIONS]" << std::endl;
        std::cout << std::endl;
        std::cout << "Page Copy Benchmark (Naive Baseline)" << std::endl;
        std::cout << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  -h, --help              Show help" << std::endl;
        std::cout << "  -p, --page-size SIZE    Page size in bytes (default: 65536)" << std::endl;
        std::cout << "  -n, --num-pages N       Number of pages (default: 100)" << std::endl;
        std::cout << "  -w, --warmup N          Warmup iterations (default: 10)" << std::endl;
        std::cout << "  -i, --iterations N      Benchmark iterations (default: 100)" << std::endl;
        std::cout << "  -j, --json FILE         Output results to JSON file" << std::endl;
        std::cout << std::endl;
    }
    
    bool parse_args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            
            if (arg == "-h" || arg == "--help") {
                print_usage(argv[0]);
                return false;
            } else if (arg == "-p" || arg == "--page-size") {
                if (i + 1 < argc) {
                    page_size = std::stoul(argv[++i]);
                }
            } else if (arg == "-n" || arg == "--num-pages") {
                if (i + 1 < argc) {
                    num_pages = std::stoul(argv[++i]);
                }
            } else if (arg == "-w" || arg == "--warmup") {
                if (i + 1 < argc) {
                    warmup_iterations = std::stoul(argv[++i]);
                }
            } else if (arg == "-i" || arg == "--iterations") {
                if (i + 1 < argc) {
                    benchmark_iterations = std::stoul(argv[++i]);
                }
            } else if (arg == "-j" || arg == "--json") {
                if (i + 1 < argc) {
                    output_json = true;
                    output_file = argv[++i];
                }
            }
        }
        
        return true;
    }
};

/**
 * Run page copy benchmark
 */
BenchmarkResult run_page_copy_benchmark(const BenchmarkConfig& config) {
    size_t total_size = config.page_size * config.num_pages;
    
    std::cout << "Allocating device memory: " << format_bytes(total_size) << std::endl;
    
    // Allocate device memory
    void *d_src, *d_dst;
    cudaError_t err;
    
    err = cudaMalloc(&d_src, total_size);
    check_cuda_result(err, "cudaMalloc d_src");
    
    err = cudaMalloc(&d_dst, total_size);
    check_cuda_result(err, "cudaMalloc d_dst");
    
    // Initialize source with pattern
    std::cout << "Initializing device memory..." << std::endl;
    init_device_memory(d_src, total_size, 0xAB);
    
    // Run benchmark
    std::cout << "Running benchmark..." << std::endl;
    std::cout << "  Kernel: " << get_naive_kernel_name() << std::endl;
    std::cout << "  Description: " << get_naive_kernel_description() << std::endl;
    std::cout << std::endl;
    
    BenchmarkResult result = run_benchmark(
        launch_page_copy_naive,
        d_dst, d_src, total_size,
        config.warmup_iterations,
        config.benchmark_iterations,
        get_naive_kernel_name()
    );
    
    // Verify correctness
    std::cout << "Verifying results..." << std::endl;
    bool correct = verify_device_memory(d_dst, total_size, 0xAB);
    
    if (!correct) {
        std::cerr << "ERROR: Benchmark verification failed!" << std::endl;
    } else {
        std::cout << "Verification: PASSED" << std::endl;
    }
    
    std::cout << std::endl;
    
    // Cleanup
    cudaFree(d_src);
    cudaFree(d_dst);
    
    return result;
}

/**
 * Print benchmark summary
 */
void print_summary(const BenchmarkResult& result) {
    std::cout << "========================================" << std::endl;
    std::cout << "  Benchmark Results" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    result.print();
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Notes" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "This is the NAIVE BASELINE implementation." << std::endl;
    std::cout << "Performance will be VERY LOW intentionally." << std::endl;
    std::cout << std::endl;
    std::cout << "Next stories will optimize:" << std::endl;
    std::cout << "  - Story 3.2: Memory coalescing" << std::endl;
    std::cout << "  - Story 3.3: Occupancy optimization" << std::endl;
    std::cout << "  - Story 3.4: Shared memory/tiling" << std::endl;
    std::cout << "  - Story 3.5: Stream overlap" << std::endl;
    std::cout << std::endl;
}

/**
 * Save results to JSON file
 */
void save_results_json(const BenchmarkResult& result, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return;
    }
    
    result.print_json(file);
    
    file.close();
    std::cout << "Results saved to: " << filename << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << "  CUDA Lab: Page Copy Benchmark" << std::endl;
    std::cout << "  Story 3.1: Baseline Kernel" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Parse arguments
    BenchmarkConfig config;
    if (!config.parse_args(argc, argv)) {
        return 0;
    }
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Page size: " << format_bytes(config.page_size) << std::endl;
    std::cout << "  Num pages: " << config.num_pages << std::endl;
    std::cout << "  Total size: " << format_bytes(config.page_size * config.num_pages) << std::endl;
    std::cout << "  Warmup iterations: " << config.warmup_iterations << std::endl;
    std::cout << "  Benchmark iterations: " << config.benchmark_iterations << std::endl;
    std::cout << std::endl;
    
    // Check CUDA device
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    
    if (err != cudaSuccess || device_count == 0) {
        std::cerr << "Error: No CUDA devices found" << std::endl;
        return 1;
    }
    
    std::cout << "CUDA Device: " << device_count << " device(s) available" << std::endl;
    
    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);
    std::cout << "  Device 0: " << prop.name << std::endl;
    std::cout << "  Compute Capability: " << prop.major << "." << prop.minor << std::endl;
    std::cout << "  Global Memory: " << format_bytes(prop.totalGlobalMem) << std::endl;
    std::cout << std::endl;
    
    try {
        // Run benchmark
        BenchmarkResult result = run_page_copy_benchmark(config);
        
        // Print results
        print_summary(result);
        
        // Save to JSON if requested
        if (config.output_json && !config.output_file.empty()) {
            save_results_json(result, config.output_file);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
