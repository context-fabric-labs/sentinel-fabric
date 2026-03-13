#pragma once

#include "kv_page.hpp"
#include "buffer.hpp"
#include "transfer_engine.hpp"
#include <vector>

namespace pm_nixl {

/**
 * KV Page Gather Engine
 * 
 * High-level interface for page gather operations.
 * Manages device memory for page indices and launches GPU kernels.
 */
class KvPageGatherEngine {
public:
    /**
     * Construct gather engine
     * 
     * @param transfer_engine Transfer engine for memory operations
     */
    explicit KvPageGatherEngine(TransferEngine& transfer_engine);
    
    /**
     * Destructor
     */
    ~KvPageGatherEngine();
    
    /**
     * Gather pages from source to destination
     * 
     * @param dst Destination buffer (device memory)
     * @param src Source buffer (device memory)
     * @param page_table Page table with indices
     * @param stream CUDA stream (optional)
     * @return Result with completion handle
     */
    Result<Completion> gather(
        const BufferDescriptor& dst,
        const BufferDescriptor& src,
        const KvPageTable& page_table,
        cudaStream_t stream = 0
    );
    
    /**
     * Gather pages with custom order
     * 
     * @param dst Destination buffer
     * @param src Source buffer
     * @param page_size Size of each page
     * @param page_indices Indices of pages to gather
     * @param stream CUDA stream
     * @return Result with completion handle
     */
    Result<Completion> gather_with_indices(
        const BufferDescriptor& dst,
        const BufferDescriptor& src,
        size_t page_size,
        const std::vector<uint32_t>& page_indices,
        cudaStream_t stream = 0
    );
    
    /**
     * Copy pages (simple contiguous copy)
     * 
     * @param dst Destination buffer
     * @param src Source buffer
     * @param total_size Total size to copy
     * @param stream CUDA stream
     * @return Result with completion handle
     */
    Result<Completion> copy(
        const BufferDescriptor& dst,
        const BufferDescriptor& src,
        size_t total_size,
        cudaStream_t stream = 0
    );
    
private:
    TransferEngine& transfer_engine_;
    std::vector<uint32_t> d_page_indices_;  // Device buffer for indices
    size_t indices_capacity_;
    
    /**
     * Ensure device buffer is large enough
     */
    cudaError_t ensure_indices_capacity(size_t capacity);
};

/**
 * Page gather benchmark utilities
 */
namespace page_gather_bench {

/**
 * Benchmark configuration
 */
struct GatherBenchmarkConfig {
    size_t page_size;
    size_t num_pages;
    size_t warmup_iterations;
    size_t benchmark_iterations;
    std::vector<uint32_t> gather_order;  // Custom gather order
    
    GatherBenchmarkConfig()
        : page_size(65536)  // 64KB default
        , num_pages(100)
        , warmup_iterations(10)
        , benchmark_iterations(100) {}
};

/**
 * Run gather benchmark
 * 
 * @param engine Gather engine
 * @param config Benchmark configuration
 * @return Average latency in milliseconds
 */
double run_gather_benchmark(
    KvPageGatherEngine& engine,
    const GatherBenchmarkConfig& config
);

/**
 * Run copy benchmark
 * 
 * @param engine Gather engine
 * @param page_size Page size
 * @param num_pages Number of pages
 * @param warmup_iterations Warmup iterations
 * @param benchmark_iterations Benchmark iterations
 * @return Average latency in milliseconds
 */
double run_copy_benchmark(
    KvPageGatherEngine& engine,
    size_t page_size,
    size_t num_pages,
    size_t warmup_iterations,
    size_t benchmark_iterations
);

} // namespace page_gather_bench

} // namespace pm_nixl
