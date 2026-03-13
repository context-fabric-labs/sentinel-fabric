#include "pm_nixl/benchmark.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace pm_nixl {

TransferBenchmark::TransferBenchmark(const BenchmarkConfig& config)
    : config_(config)
    , engine_(TransferEngineConfig{config.device_id, 4, false}) {
    engine_.initialize();
}

std::vector<BenchmarkResult> TransferBenchmark::run() {
    std::vector<BenchmarkResult> all_results;
    
    // Run H2D benchmarks
    auto h2d_results = run_h2d();
    all_results.insert(all_results.end(), h2d_results.begin(), h2d_results.end());
    
    // Run D2H benchmarks
    auto d2h_results = run_d2h();
    all_results.insert(all_results.end(), d2h_results.begin(), d2h_results.end());
    
    return all_results;
}

std::vector<BenchmarkResult> TransferBenchmark::run_h2d() {
    std::vector<BenchmarkResult> results;
    
    // Pinned memory H2D
    for (size_t size : config_.transfer_sizes) {
        auto result = benchmark_size(size, MemoryKind::HostPinned, TransferType::HostToDevice);
        result.transfer_type = "H2D";
        result.memory_type = "Pinned";
        results.push_back(result);
    }
    
    // Pageable memory H2D (if configured)
    if (config_.compare_pinned_vs_pageable) {
        for (size_t size : config_.transfer_sizes) {
            auto result = benchmark_size(size, MemoryKind::HostPageable, TransferType::HostToDevice);
            result.transfer_type = "H2D";
            result.memory_type = "Pageable";
            results.push_back(result);
        }
    }
    
    return results;
}

std::vector<BenchmarkResult> TransferBenchmark::run_d2h() {
    std::vector<BenchmarkResult> results;
    
    // Pinned memory D2H
    for (size_t size : config_.transfer_sizes) {
        auto result = benchmark_size(size, MemoryKind::HostPinned, TransferType::DeviceToHost);
        result.transfer_type = "D2H";
        result.memory_type = "Pinned";
        results.push_back(result);
    }
    
    // Pageable memory D2H (if configured)
    if (config_.compare_pinned_vs_pageable) {
        for (size_t size : config_.transfer_sizes) {
            auto result = benchmark_size(size, MemoryKind::HostPageable, TransferType::DeviceToHost);
            result.transfer_type = "D2H";
            result.memory_type = "Pageable";
            results.push_back(result);
        }
    }
    
    return results;
}

BenchmarkResult TransferBenchmark::benchmark_size(
    size_t size,
    MemoryKind host_kind,
    TransferType transfer_type
) {
    BenchmarkResult result;
    result.size_bytes = size;
    result.memory_type = (host_kind == MemoryKind::HostPinned) ? "Pinned" : "Pageable";
    result.transfer_type = transfer_type_to_string(transfer_type);
    
    // Create buffers
    Buffer host(host_kind, size);
    Buffer device(MemoryKind::CudaDevice, size, config_.device_id);
    
    // Initialize host buffer with pattern
    std::memset(host.ptr(), 0xAB, size);
    
    // Warmup iterations
    for (size_t i = 0; i < config_.warmup_iterations; ++i) {
        if (transfer_type == TransferType::HostToDevice) {
            auto completion = engine_.submit_h2d(host.descriptor(), device.descriptor(), size);
            if (completion.has_value()) {
                engine_.wait(completion.value());
            }
        } else {
            auto completion = engine_.submit_d2h(device.descriptor(), host.descriptor(), size);
            if (completion.has_value()) {
                engine_.wait(completion.value());
            }
        }
    }
    
    // Benchmark iterations
    std::vector<double> latencies;
    latencies.reserve(config_.benchmark_iterations);
    
    for (size_t i = 0; i < config_.benchmark_iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        if (transfer_type == TransferType::HostToDevice) {
            auto completion = engine_.submit_h2d(host.descriptor(), device.descriptor(), size);
            if (completion.has_value()) {
                engine_.wait(completion.value());
            }
        } else {
            auto completion = engine_.submit_d2h(device.descriptor(), host.descriptor(), size);
            if (completion.has_value()) {
                engine_.wait(completion.value());
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latency_ms);
    }
    
    // Sort latencies for percentile calculation
    std::sort(latencies.begin(), latencies.end());
    
    // Calculate statistics
    result.iterations = config_.benchmark_iterations;
    result.latency_mean_ms = mean(latencies);
    result.latency_stddev_ms = stddev(latencies, result.latency_mean_ms);
    result.latency_p50_ms = percentile(latencies, 50.0);
    result.latency_p95_ms = percentile(latencies, 95.0);
    result.latency_p99_ms = percentile(latencies, 99.0);
    
    // Calculate throughput (GB/s)
    double size_gb = static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0);
    double latency_s = result.latency_mean_ms / 1000.0;
    result.throughput_gbps = size_gb / latency_s;
    
    return result;
}

void TransferBenchmark::print_results(const std::vector<BenchmarkResult>& results) {
    std::cout << std::endl;
    std::cout << "=================================================================" << std::endl;
    std::cout << "                    Transfer Benchmark Results                   " << std::endl;
    std::cout << "=================================================================" << std::endl;
    std::cout << std::endl;
    
    // Group by transfer type and memory type
    std::cout << std::setw(12) << "Size" 
              << std::setw(12) << "Type"
              << std::setw(12) << "Memory"
              << std::setw(12) << "Mean(ms)"
              << std::setw(12) << "P95(ms)"
              << std::setw(12) << "P99(ms)"
              << std::setw(14) << "Throughput"
              << std::endl;
    
    std::cout << std::string(94, '-') << std::endl;
    
    for (const auto& result : results) {
        std::cout << std::setw(12) << format_bytes(result.size_bytes)
                  << std::setw(12) << result.transfer_type
                  << std::setw(12) << result.memory_type
                  << std::setw(12) << std::fixed << std::setprecision(3) << result.latency_mean_ms
                  << std::setw(12) << std::fixed << std::setprecision(3) << result.latency_p95_ms
                  << std::setw(12) << std::fixed << std::setprecision(3) << result.latency_p99_ms
                  << std::setw(14) << format_throughput(result.throughput_gbps)
                  << std::endl;
    }
    
    std::cout << "=================================================================" << std::endl;
    std::cout << std::endl;
    
    // Summary statistics
    std::cout << "Summary:" << std::endl;
    std::cout << "  Total benchmarks: " << results.size() << std::endl;
    std::cout << "  Iterations per size: " << config_.benchmark_iterations << std::endl;
    std::cout << "  Warmup iterations: " << config_.warmup_iterations << std::endl;
    std::cout << "  Device: CUDA " << config_.device_id << std::endl;
    std::cout << std::endl;
}

void TransferBenchmark::save_csv(const std::vector<BenchmarkResult>& results, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return;
    }
    
    // Header
    file << "size_bytes,size_human,transfer_type,memory_type,"
         << "latency_mean_ms,latency_stddev_ms,latency_p50_ms,"
         << "latency_p95_ms,latency_p99_ms,throughput_gbps,iterations" << std::endl;
    
    // Data
    for (const auto& result : results) {
        file << result.size_bytes << ","
             << format_bytes(result.size_bytes) << ","
             << result.transfer_type << ","
             << result.memory_type << ","
             << result.latency_mean_ms << ","
             << result.latency_stddev_ms << ","
             << result.latency_p50_ms << ","
             << result.latency_p95_ms << ","
             << result.latency_p99_ms << ","
             << result.throughput_gbps << ","
             << result.iterations << std::endl;
    }
    
    file.close();
    std::cout << "Results saved to: " << filename << std::endl;
}

void TransferBenchmark::save_json(const std::vector<BenchmarkResult>& results, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return;
    }
    
    file << "{" << std::endl;
    file << "  \"config\": {" << std::endl;
    file << "    \"warmup_iterations\": " << config_.warmup_iterations << "," << std::endl;
    file << "    \"benchmark_iterations\": " << config_.benchmark_iterations << "," << std::endl;
    file << "    \"device_id\": " << config_.device_id << "," << std::endl;
    file << "    \"compare_pinned_vs_pageable\": " << (config_.compare_pinned_vs_pageable ? "true" : "false") << std::endl;
    file << "  }," << std::endl;
    file << "  \"results\": [" << std::endl;
    
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        file << "    {" << std::endl;
        file << "      \"size_bytes\": " << result.size_bytes << "," << std::endl;
        file << "      \"size_human\": \"" << format_bytes(result.size_bytes) << "\"," << std::endl;
        file << "      \"transfer_type\": \"" << result.transfer_type << "\"," << std::endl;
        file << "      \"memory_type\": \"" << result.memory_type << "\"," << std::endl;
        file << "      \"latency_mean_ms\": " << result.latency_mean_ms << "," << std::endl;
        file << "      \"latency_stddev_ms\": " << result.latency_stddev_ms << "," << std::endl;
        file << "      \"latency_p50_ms\": " << result.latency_p50_ms << "," << std::endl;
        file << "      \"latency_p95_ms\": " << result.latency_p95_ms << "," << std::endl;
        file << "      \"latency_p99_ms\": " << result.latency_p99_ms << "," << std::endl;
        file << "      \"throughput_gbps\": " << result.throughput_gbps << "," << std::endl;
        file << "      \"iterations\": " << result.iterations << std::endl;
        file << "    }";
        if (i < results.size() - 1) {
            file << ",";
        }
        file << std::endl;
    }
    
    file << "  ]" << std::endl;
    file << "}" << std::endl;
    
    file.close();
    std::cout << "Results saved to: " << filename << std::endl;
}

double TransferBenchmark::percentile(std::vector<double>& data, double p) {
    if (data.empty()) {
        return 0.0;
    }
    
    size_t index = static_cast<size_t>(std::ceil(p / 100.0 * data.size())) - 1;
    return data[index];
}

double TransferBenchmark::mean(const std::vector<double>& data) {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / data.size();
}

double TransferBenchmark::stddev(const std::vector<double>& data, double mean) {
    if (data.size() < 2) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (const auto& value : data) {
        double diff = value - mean;
        sum += diff * diff;
    }
    
    return std::sqrt(sum / (data.size() - 1));
}

std::string TransferBenchmark::format_bytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_index < 3) {
        size /= 1024.0;
        unit_index++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
    return oss.str();
}

std::string TransferBenchmark::format_throughput(double gbps) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << gbps << " GB/s";
    return oss.str();
}

} // namespace pm_nixl
