#include "pm_nixl/page_gather_engine.hpp"
#include "pm_nixl/cuda/page_gather.hpp"
#include <cuda_runtime.h>
#include <chrono>
#include <numeric>

namespace pm_nixl {

KvPageGatherEngine::KvPageGatherEngine(TransferEngine& transfer_engine)
    : transfer_engine_(transfer_engine)
    , indices_capacity_(0) {}

KvPageGatherEngine::~KvPageGatherEngine() {
    // Device indices buffer is managed by Buffer class
}

Result<Completion> KvPageGatherEngine::gather(
    const BufferDescriptor& dst,
    const BufferDescriptor& src,
    const KvPageTable& page_table,
    cudaStream_t stream
) {
    // Validate buffers are device memory
    if (src.kind != MemoryKind::CudaDevice || dst.kind != MemoryKind::CudaDevice) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    // Get page indices
    const auto& indices = page_table.page_indices();
    if (indices.empty()) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    // Get page size
    size_t page_size = page_table.page_size_bytes();
    
    // Calculate total size
    size_t total_size = indices.size() * page_size;
    
    // Validate destination size
    if (dst.size < total_size) {
        return std::error_code(ErrorCode::SizeError);
    }
    
    // Copy indices to device
    std::vector<uint32_t> h_indices(indices.begin(), indices.end());
    
    // Allocate device buffer for indices if needed
    Buffer indices_buffer(MemoryKind::CudaDevice, h_indices.size() * sizeof(uint32_t));
    
    // Copy indices to device
    auto indices_result = transfer_engine_.submit_h2d(
        BufferDescriptor(h_indices.data(), h_indices.size() * sizeof(uint32_t), MemoryKind::HostPinned),
        indices_buffer.descriptor(),
        h_indices.size() * sizeof(uint32_t),
        0
    );
    
    if (!indices_result.has_value()) {
        return indices_result.error();
    }
    
    transfer_engine_.wait(indices_result.value());
    
    // Launch gather kernel
    cudaError_t err = cuda::launch_page_gather(
        dst.ptr,
        src.ptr,
        static_cast<const uint32_t*>(indices_buffer.ptr()),
        page_size,
        indices.size(),
        stream
    );
    
    if (err != cudaSuccess) {
        return std::error_code(ErrorCode::CudaError);
    }
    
    // Create completion (kernel is async, so we just return success)
    Completion completion(0);
    return Result<Completion>(std::move(completion));
}

Result<Completion> KvPageGatherEngine::gather_with_indices(
    const BufferDescriptor& dst,
    const BufferDescriptor& src,
    size_t page_size,
    const std::vector<uint32_t>& page_indices,
    cudaStream_t stream
) {
    // Validate
    if (page_indices.empty()) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    if (src.kind != MemoryKind::CudaDevice || dst.kind != MemoryKind::CudaDevice) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    // Copy indices to device
    Buffer indices_buffer(MemoryKind::CudaDevice, page_indices.size() * sizeof(uint32_t));
    
    auto indices_result = transfer_engine_.submit_h2d(
        BufferDescriptor(const_cast<uint32_t*>(page_indices.data()), 
                        page_indices.size() * sizeof(uint32_t), 
                        MemoryKind::HostPinned),
        indices_buffer.descriptor(),
        page_indices.size() * sizeof(uint32_t)
    );
    
    if (!indices_result.has_value()) {
        return indices_result.error();
    }
    
    transfer_engine_.wait(indices_result.value());
    
    // Launch gather kernel
    cudaError_t err = cuda::launch_page_gather(
        dst.ptr,
        src.ptr,
        static_cast<const uint32_t*>(indices_buffer.ptr()),
        page_size,
        page_indices.size(),
        stream
    );
    
    if (err != cudaSuccess) {
        return std::error_code(ErrorCode::CudaError);
    }
    
    Completion completion(0);
    return Result<Completion>(std::move(completion));
}

Result<Completion> KvPageGatherEngine::copy(
    const BufferDescriptor& dst,
    const BufferDescriptor& src,
    size_t total_size,
    cudaStream_t stream
) {
    if (src.kind != MemoryKind::CudaDevice || dst.kind != MemoryKind::CudaDevice) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    if (dst.size < total_size || src.size < total_size) {
        return std::error_code(ErrorCode::SizeError);
    }
    
    // Launch copy kernel
    cudaError_t err = cuda::launch_page_copy(
        dst.ptr,
        src.ptr,
        total_size,
        stream
    );
    
    if (err != cudaSuccess) {
        return std::error_code(ErrorCode::CudaError);
    }
    
    Completion completion(0);
    return Result<Completion>(std::move(completion));
}

cudaError_t KvPageGatherEngine::ensure_indices_capacity(size_t capacity) {
    if (capacity <= indices_capacity_) {
        return cudaSuccess;
    }
    
    // Reallocate device buffer
    // (In production, would use cudaMalloc/cudaFree)
    indices_capacity_ = capacity;
    return cudaSuccess;
}

namespace page_gather_bench {

double run_gather_benchmark(
    KvPageGatherEngine& engine,
    const GatherBenchmarkConfig& config
) {
    // Create buffers
    size_t total_size = config.num_pages * config.page_size;
    Buffer src(MemoryKind::CudaDevice, total_size);
    Buffer dst(MemoryKind::CudaDevice, total_size);
    
    // Initialize source with pattern
    std::vector<char> h_src(total_size);
    for (size_t i = 0; i < total_size; ++i) {
        h_src[i] = static_cast<char>(i & 0xFF);
    }
    
    // Copy to device (setup, not timed)
    // (Would use transfer engine in real code)
    
    std::vector<double> latencies;
    latencies.reserve(config.benchmark_iterations);
    
    // Warmup
    for (size_t i = 0; i < config.warmup_iterations; ++i) {
        engine.gather_with_indices(
            dst.descriptor(),
            src.descriptor(),
            config.page_size,
            config.gather_order
        );
        cudaDeviceSynchronize();
    }
    
    // Benchmark
    for (size_t i = 0; i < config.benchmark_iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        engine.gather_with_indices(
            dst.descriptor(),
            src.descriptor(),
            config.page_size,
            config.gather_order
        );
        
        cudaDeviceSynchronize();
        
        auto end = std::chrono::high_resolution_clock::now();
        double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latency_ms);
    }
    
    // Calculate average
    double sum = 0.0;
    for (double lat : latencies) {
        sum += lat;
    }
    
    return sum / latencies.size();
}

double run_copy_benchmark(
    KvPageGatherEngine& engine,
    size_t page_size,
    size_t num_pages,
    size_t warmup_iterations,
    size_t benchmark_iterations
) {
    size_t total_size = num_pages * page_size;
    
    Buffer src(MemoryKind::CudaDevice, total_size);
    Buffer dst(MemoryKind::CudaDevice, total_size);
    
    std::vector<double> latencies;
    latencies.reserve(benchmark_iterations);
    
    // Warmup
    for (size_t i = 0; i < warmup_iterations; ++i) {
        engine.copy(dst.descriptor(), src.descriptor(), total_size);
        cudaDeviceSynchronize();
    }
    
    // Benchmark
    for (size_t i = 0; i < benchmark_iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        engine.copy(dst.descriptor(), src.descriptor(), total_size);
        
        cudaDeviceSynchronize();
        
        auto end = std::chrono::high_resolution_clock::now();
        double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latency_ms);
    }
    
    // Calculate average
    double sum = 0.0;
    for (double lat : latencies) {
        sum += lat;
    }
    
    return sum / latencies.size();
}

} // namespace page_gather_bench

} // namespace pm_nixl
