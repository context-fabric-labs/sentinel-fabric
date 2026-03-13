#pragma once

#include <pm_nixl/buffer.hpp>
#include <pm_nixl/transfer_engine.hpp>
#include <vector>
#include <string>
#include <chrono>

namespace pm_nixl {

/**
 * Benchmark configuration
 */
struct BenchmarkConfig {
    std::vector<size_t> transfer_sizes;  // Sizes to benchmark
    size_t warmup_iterations;             // Warmup runs
    size_t benchmark_iterations;          // Actual benchmark runs
    int device_id;                        // CUDA device
    bool compare_pinned_vs_pageable;      // Compare memory types
    bool output_csv;                      // Output CSV format
    bool output_json;                     // Output JSON format
    std::string output_file;              // Output file path
    
    BenchmarkConfig()
        : transfer_sizes({
            1024,         // 1 KB
            4096,         // 4 KB
            16384,        // 16 KB
            65536,        // 64 KB
            262144,       // 256 KB
            1048576,      // 1 MB
            4194304,      // 4 MB
            16777216,     // 16 MB
            67108864,     // 64 MB
            134217728     // 128 MB
          })
        , warmup_iterations(10)
        , benchmark_iterations(100)
        , device_id(0)
        , compare_pinned_vs_pageable(true)
        , output_csv(false)
        , output_json(false)
        , output_file("") {}
};

/**
 * Benchmark result for a single size
 */
struct BenchmarkResult {
    size_t size_bytes;
    double latency_mean_ms;
    double latency_stddev_ms;
    double latency_p50_ms;
    double latency_p95_ms;
    double latency_p99_ms;
    double throughput_gbps;
    size_t iterations;
    std::string memory_type;
    std::string transfer_type;
    
    BenchmarkResult()
        : size_bytes(0)
        , latency_mean_ms(0.0)
        , latency_stddev_ms(0.0)
        , latency_p50_ms(0.0)
        , latency_p95_ms(0.0)
        , latency_p99_ms(0.0)
        , throughput_gbps(0.0)
        , iterations(0)
        , memory_type("")
        , transfer_type("") {}
};

/**
 * Benchmark harness
 */
class TransferBenchmark {
public:
    /**
     * Construct benchmark harness
     */
    explicit TransferBenchmark(const BenchmarkConfig& config);
    
    /**
     * Run all benchmarks
     */
    std::vector<BenchmarkResult> run();
    
    /**
     * Run H2D benchmarks
     */
    std::vector<BenchmarkResult> run_h2d();
    
    /**
     * Run D2H benchmarks
     */
    std::vector<BenchmarkResult> run_d2h();
    
    /**
     * Print results to console
     */
    void print_results(const std::vector<BenchmarkResult>& results);
    
    /**
     * Save results to CSV
     */
    void save_csv(const std::vector<BenchmarkResult>& results, const std::string& filename);
    
    /**
     * Save results to JSON
     */
    void save_json(const std::vector<BenchmarkResult>& results, const std::string& filename);
    
private:
    BenchmarkConfig config_;
    TransferEngine engine_;
    
    /**
     * Benchmark a single transfer size
     */
    BenchmarkResult benchmark_size(
        size_t size,
        MemoryKind host_kind,
        TransferType transfer_type
    );
    
    /**
     * Calculate percentile
     */
    double percentile(std::vector<double>& data, double p);
    
    /**
     * Calculate mean
     */
    double mean(const std::vector<double>& data);
    
    /**
     * Calculate standard deviation
     */
    double stddev(const std::vector<double>& data, double mean);
    
    /**
     * Format bytes to human-readable string
     */
    std::string format_bytes(size_t bytes);
    
    /**
     * Format throughput to human-readable string
     */
    std::string format_throughput(double gbps);
};

} // namespace pm_nixl
