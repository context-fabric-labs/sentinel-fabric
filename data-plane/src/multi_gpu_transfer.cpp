#include "pm_nixl/multi_gpu_transfer.hpp"
#include "pm_nixl/cuda/p2p_copy.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>

namespace pm_nixl {

MultiGpuTransferEngine::MultiGpuTransferEngine(const MultiGpuConfig& config)
    : config_(config)
    , initialized_(false) {
    // Default to first 2 devices if not specified
    if (config_.device_ids.empty()) {
        int device_count = 0;
        cudaGetDeviceCount(&device_count);
        
        if (device_count >= 2) {
            config_.device_ids.push_back(0);
            config_.device_ids.push_back(1);
        } else if (device_count == 1) {
            config_.device_ids.push_back(0);
        }
    }
}

MultiGpuTransferEngine::~MultiGpuTransferEngine() {
    if (initialized_) {
        shutdown();
    }
}

Result<void> MultiGpuTransferEngine::initialize() {
    if (initialized_) {
        return std::error_code(ErrorCode::AlreadyRegistered);
    }
    
    if (config_.device_ids.empty()) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    // Verify all devices exist
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    
    if (err != cudaSuccess) {
        return std::error_code(ErrorCode::CudaError);
    }
    
    for (int dev_id : config_.device_ids) {
        if (dev_id < 0 || dev_id >= device_count) {
            return std::error_code(ErrorCode::InvalidArgument);
        }
    }
    
    // Detect topology
    if (config_.auto_detect_topology) {
        detect_topology();
    }
    
    // Enable P2P access if requested
    if (config_.enable_p2p && config_.device_ids.size() > 1) {
        auto result = enable_p2p_access();
        if (!result.has_value()) {
            std::cerr << "Warning: Could not enable P2P access: " 
                      << error_code_to_string(result.error().error) << std::endl;
        }
    }
    
    initialized_ = true;
    return Result<void>(std::monostate{});
}

Result<void> MultiGpuTransferEngine::shutdown() {
    if (!initialized_) {
        return std::error_code(ErrorCode::NotRegistered);
    }
    
    // Disable P2P access
    if (config_.enable_p2p) {
        disable_p2p_access();
    }
    
    topologies_.clear();
    initialized_ = false;
    
    return Result<void>(std::monostate{});
}

void MultiGpuTransferEngine::detect_topology() {
    topologies_.clear();
    
    for (size_t i = 0; i < config_.device_ids.size(); ++i) {
        for (size_t j = i + 1; j < config_.device_ids.size(); ++j) {
            int dev1 = config_.device_ids[i];
            int dev2 = config_.device_ids[j];
            
            DevicePairTopology topo;
            topo.dev_id1 = dev1;
            topo.dev_id2 = dev2;
            topo.p2p_supported = cuda::can_devices_communicate_p2p(dev1, dev2);
            topo.same_bus = cuda::are_devices_on_same_bus(dev1, dev2);
            
            // Get performance rank
            if (topo.p2p_supported) {
                int rank = 0;
                cudaDeviceGetP2PAttribute(&rank, cudaDevP2PAttrPerformanceRank, dev1, dev2);
                topo.performance_rank = rank;
            }
            
            topologies_.push_back(topo);
        }
    }
}

Result<void> MultiGpuTransferEngine::enable_p2p_access() {
    for (const auto& topo : topologies_) {
        if (topo.p2p_supported) {
            cudaError_t err = cuda::enable_peer_access(topo.dev_id1, topo.dev_id2);
            if (err != cudaSuccess && err != cudaErrorPeerAccessAlreadyEnabled) {
                return std::error_code(ErrorCode::CudaError);
            }
            
            err = cuda::enable_peer_access(topo.dev_id2, topo.dev_id1);
            if (err != cudaSuccess && err != cudaErrorPeerAccessAlreadyEnabled) {
                return std::error_code(ErrorCode::CudaError);
            }
        }
    }
    
    return Result<void>(std::monostate{});
}

void MultiGpuTransferEngine::disable_p2p_access() {
    for (const auto& topo : topologies_) {
        cuda::disable_peer_access(topo.dev_id1, topo.dev_id2);
        cuda::disable_peer_access(topo.dev_id2, topo.dev_id1);
    }
}

Result<Completion> MultiGpuTransferEngine::submit_d2d(
    const BufferDescriptor& dst,
    const BufferDescriptor& src,
    size_t size
) {
    if (src.kind != MemoryKind::CudaDevice || dst.kind != MemoryKind::CudaDevice) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    return submit_d2d_explicit(dst, dst.device_id, src, src.device_id, size);
}

Result<Completion> MultiGpuTransferEngine::submit_d2d_explicit(
    const BufferDescriptor& dst,
    int dst_dev_id,
    const BufferDescriptor& src,
    int src_dev_id,
    size_t size
) {
    if (src.kind != MemoryKind::CudaDevice || dst.kind != MemoryKind::CudaDevice) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    if (dst.size < size || src.size < size) {
        return std::error_code(ErrorCode::SizeError);
    }
    
    // Create CUDA event for completion tracking
    cudaEvent_t event;
    cudaError_t err = cudaEventCreate(&event);
    if (err != cudaSuccess) {
        return std::error_code(ErrorCode::CudaError);
    }
    
    // Get stream for source device
    int current_dev = -1;
    err = cudaGetDevice(&current_dev);
    if (err != cudaSuccess) {
        cudaEventDestroy(event);
        return std::error_code(ErrorCode::CudaError);
    }
    
    // Set to source device
    err = cudaSetDevice(src_dev_id);
    if (err != cudaSuccess) {
        cudaEventDestroy(event);
        return std::error_code(ErrorCode::CudaError);
    }
    
    cudaStream_t stream;
    err = cudaStreamCreate(&stream);
    if (err != cudaSuccess) {
        cudaEventDestroy(event);
        return std::error_code(ErrorCode::CudaError);
    }
    
    // Launch D2D copy
    err = cuda::launch_memcpy_d2d(dst.ptr, src.ptr, size, src_dev_id, dst_dev_id, stream);
    
    if (err != cudaSuccess) {
        cudaStreamDestroy(stream);
        cudaEventDestroy(event);
        return std::error_code(ErrorCode::CudaError);
    }
    
    // Record event
    cudaEventRecord(event, stream);
    
    // Restore device
    if (current_dev != -1) {
        cudaSetDevice(current_dev);
    }
    
    // Create completion
    Completion completion(0);
    completion.cuda_event_ = event;
    
    return Result<Completion>(std::move(completion));
}

bool MultiGpuTransferEngine::is_p2p_supported(int dev_id1, int dev_id2) const {
    for (const auto& topo : topologies_) {
        if ((topo.dev_id1 == dev_id1 && topo.dev_id2 == dev_id2) ||
            (topo.dev_id1 == dev_id2 && topo.dev_id2 == dev_id1)) {
            return topo.p2p_supported;
        }
    }
    return false;
}

const DevicePairTopology* MultiGpuTransferEngine::get_topology(int dev_id1, int dev_id2) const {
    for (const auto& topo : topologies_) {
        if ((topo.dev_id1 == dev_id1 && topo.dev_id2 == dev_id2) ||
            (topo.dev_id1 == dev_id2 && topo.dev_id2 == dev_id1)) {
            return &topo;
        }
    }
    return nullptr;
}

void MultiGpuTransferEngine::print_topology() const {
    std::cout << "========================================" << std::endl;
    std::cout << "  Multi-GPU Topology" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Devices: ";
    for (size_t i = 0; i < config_.device_ids.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << config_.device_ids[i];
    }
    std::cout << std::endl;
    std::cout << std::endl;
    
    if (topologies_.empty()) {
        std::cout << "No device pairs (single GPU or topology not detected)" << std::endl;
        return;
    }
    
    std::cout << "Device Pairs:" << std::endl;
    std::cout << "  Dev1  Dev2  P2P     Same Bus  Perf Rank" << std::endl;
    std::cout << "  ----  ----  ------  --------  ---------" << std::endl;
    
    for (const auto& topo : topologies_) {
        std::cout << "  " << std::setw(4) << topo.dev_id1
                  << "  " << std::setw(4) << topo.dev_id2
                  << "  " << std::setw(6) << (topo.p2p_supported ? "Yes" : "No")
                  << "  " << std::setw(8) << (topo.same_bus ? "Yes" : "No")
                  << "  " << std::setw(9) << topo.performance_rank
                  << std::endl;
    }
    
    std::cout << std::endl;
}

namespace multi_gpu_bench {

double run_d2d_benchmark(
    MultiGpuTransferEngine& engine,
    const D2DBenchmarkConfig& config
) {
    // Create buffers on different devices
    Buffer src(MemoryKind::CudaDevice, config.transfer_size, config.src_dev_id);
    Buffer dst(MemoryKind::CudaDevice, config.transfer_size, config.dst_dev_id);
    
    std::vector<double> latencies;
    latencies.reserve(config.benchmark_iterations);
    
    // Warmup
    for (size_t i = 0; i < config.warmup_iterations; ++i) {
        auto result = engine.submit_d2d_explicit(
            dst.descriptor(), config.dst_dev_id,
            src.descriptor(), config.src_dev_id,
            config.transfer_size
        );
        
        if (result.has_value()) {
            cudaDeviceSynchronize();
        }
    }
    
    // Benchmark
    for (size_t i = 0; i < config.benchmark_iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto result = engine.submit_d2d_explicit(
            dst.descriptor(), config.dst_dev_id,
            src.descriptor(), config.src_dev_id,
            config.transfer_size
        );
        
        if (result.has_value()) {
            cudaDeviceSynchronize();
        }
        
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

void compare_p2p_performance(
    MultiGpuTransferEngine& engine,
    size_t transfer_size,
    size_t warmup_iterations,
    size_t benchmark_iterations
) {
    std::cout << "========================================" << std::endl;
    std::cout << "  P2P vs Non-P2P Performance Comparison" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Print topology
    engine.print_topology();
    std::cout << std::endl;
    
    // Benchmark with P2P
    D2DBenchmarkConfig config_p2p;
    config_p2p.transfer_size = transfer_size;
    config_p2p.warmup_iterations = warmup_iterations;
    config_p2p.benchmark_iterations = benchmark_iterations;
    config_p2p.use_p2p = true;
    
    std::cout << "Running P2P benchmark..." << std::endl;
    double p2p_latency = run_d2d_benchmark(engine, config_p2p);
    double p2p_throughput = (transfer_size / 1024.0 / 1024.0 / 1024.0) / (p2p_latency / 1000.0);
    
    std::cout << "  P2P Latency: " << std::fixed << std::setprecision(3) << p2p_latency << " ms" << std::endl;
    std::cout << "  P2P Throughput: " << std::fixed << std::setprecision(2) << p2p_throughput << " GB/s" << std::endl;
    std::cout << std::endl;
    
    // Summary
    std::cout << "========================================" << std::endl;
    std::cout << "  Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "  Transfer Size: " << (transfer_size / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  P2P Throughput: " << std::fixed << std::setprecision(2) << p2p_throughput << " GB/s" << std::endl;
    std::cout << std::endl;
}

} // namespace multi_gpu_bench

} // namespace pm_nixl
