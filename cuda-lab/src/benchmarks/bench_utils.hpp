#pragma once

#include <cuda_runtime.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

/**
 * Benchmark utilities
 */

/**
 * Get current timestamp
 */
inline auto get_timestamp() {
    return std::chrono::high_resolution_clock::now();
}

/**
 * Calculate duration in milliseconds
 */
inline double duration_ms(std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

/**
 * Format bytes to human-readable string
 */
inline std::string format_bytes(size_t bytes) {
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

/**
 * Format throughput to human-readable string
 */
inline std::string format_throughput(double gbps) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << gbps << " GB/s";
    return oss.str();
}

/**
 * Check CUDA result and print error if failed
 */
inline bool check_cuda_result(cudaError_t result, const char* operation) {
    if (result != cudaSuccess) {
        std::cerr << "CUDA error: " << operation << " failed: " 
                  << cudaGetErrorString(result) << std::endl;
        return false;
    }
    return true;
}

/**
 * Synchronize CUDA device and check for errors
 */
inline bool sync_cuda() {
    cudaError_t err = cudaDeviceSynchronize();
    return check_cuda_result(err, "cudaDeviceSynchronize");
}

/**
 * Initialize device memory with pattern
 */
inline void init_device_memory(void* ptr, size_t size, unsigned char pattern) {
    std::vector<unsigned char> h_data(size, pattern);
    cudaMemcpy(ptr, h_data.data(), size, cudaMemcpyHostToDevice);
}

/**
 * Verify device memory matches expected pattern
 */
inline bool verify_device_memory(const void* ptr, size_t size, unsigned char expected_pattern) {
    std::vector<unsigned char> h_data(size);
    cudaMemcpy(h_data.data(), ptr, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        if (h_data[i] != expected_pattern) {
            std::cerr << "Verification failed at index " << i 
                      << ": expected " << (int)expected_pattern 
                      << ", got " << (int)h_data[i] << std::endl;
            return false;
        }
    }
    
    return true;
}

/**
 * Benchmark result structure
 */
struct BenchmarkResult {
    std::string kernel_name;
    size_t total_bytes;
    double latency_ms;
    double throughput_gbps;
    size_t warmup_iterations;
    size_t benchmark_iterations;
    
    void print() const {
        std::cout << "Kernel: " << kernel_name << std::endl;
        std::cout << "  Total bytes: " << format_bytes(total_bytes) << std::endl;
        std::cout << "  Latency: " << std::fixed << std::setprecision(3) 
                  << latency_ms << " ms" << std::endl;
        std::cout << "  Throughput: " << format_throughput(throughput_gbps) << std::endl;
        std::cout << "  Iterations: " << benchmark_iterations 
                  << " (warmup: " << warmup_iterations << ")" << std::endl;
    }
    
    void print_json(std::ostream& os) const {
        os << "{" << std::endl;
        os << "  \"kernel_name\": \"" << kernel_name << "\"," << std::endl;
        os << "  \"total_bytes\": " << total_bytes << "," << std::endl;
        os << "  \"latency_ms\": " << latency_ms << "," << std::endl;
        os << "  \"throughput_gbps\": " << throughput_gbps << "," << std::endl;
        os << "  \"warmup_iterations\": " << warmup_iterations << "," << std::endl;
        os << "  \"benchmark_iterations\": " << benchmark_iterations << std::endl;
        os << "}" << std::endl;
    }
};

/**
 * Run benchmark
 * 
 * @param kernel_func Kernel launch function
 * @param dst Destination buffer
 * @param src Source buffer
 * @param size Transfer size
 * @param warmup_iterations Warmup iterations
 * @param benchmark_iterations Benchmark iterations
 * @param kernel_name Kernel name for reporting
 * @return Benchmark result
 */
template<typename KernelFunc>
BenchmarkResult run_benchmark(
    KernelFunc kernel_func,
    void* dst,
    const void* src,
    size_t size,
    size_t warmup_iterations,
    size_t benchmark_iterations,
    const std::string& kernel_name
) {
    BenchmarkResult result;
    result.kernel_name = kernel_name;
    result.total_bytes = size * benchmark_iterations;
    result.warmup_iterations = warmup_iterations;
    result.benchmark_iterations = benchmark_iterations;
    
    // Warmup
    for (size_t i = 0; i < warmup_iterations; ++i) {
        kernel_func(dst, src, size);
    }
    sync_cuda();
    
    // Benchmark
    auto start = get_timestamp();
    
    for (size_t i = 0; i < benchmark_iterations; ++i) {
        kernel_func(dst, src, size);
    }
    
    sync_cuda();
    auto end = get_timestamp();
    
    result.latency_ms = duration_ms(start, end) / benchmark_iterations;
    result.throughput_gbps = (size / 1024.0 / 1024.0 / 1024.0) / (result.latency_ms / 1000.0);
    
    return result;
}
