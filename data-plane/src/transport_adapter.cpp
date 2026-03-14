#include "pm_nixl/cuda_transport.hpp"
#include "pm_nixl/transport_interface.hpp"
#include <chrono>

namespace pm_nixl {

CudaTransport::CudaTransport(const TransferEngineConfig& config)
    : config_(config)
    , engine_(nullptr)
    , multi_gpu_engine_(nullptr) {}

CudaTransport::~CudaTransport() {
    if (engine_) {
        engine_->shutdown();
    }
    if (multi_gpu_engine_) {
        multi_gpu_engine_->shutdown();
    }
}

TransportCaps CudaTransport::capabilities() const {
    TransportCaps caps = TransportCaps::HostToDevice | 
                         TransportCaps::DeviceToHost | 
                         TransportCaps::Async;
    
    // Check if multi-GPU is available
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count > 1) {
        caps = caps | TransportCaps::DeviceToDevice;
        
        // Check P2P support
        if (device_count >= 2) {
            int can_access = 0;
            cudaDeviceCanAccessPeer(&can_access, 0, 1);
            if (can_access) {
                caps = caps | TransportCaps::P2P;
            }
        }
    }
    
    return caps;
}

Result<void> CudaTransport::initialize() {
    // Create transfer engine
    engine_ = std::make_unique<TransferEngine>(config_);
    auto result = engine_->initialize();
    
    if (!result.has_value()) {
        return result;
    }
    
    // Create multi-GPU engine if multiple devices available
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count > 1) {
        MultiGpuConfig multi_config;
        for (int i = 0; i < device_count && i < 8; ++i) {
            multi_config.device_ids.push_back(i);
        }
        multi_config.enable_p2p = true;
        
        multi_gpu_engine_ = std::make_unique<MultiGpuTransferEngine>(multi_config);
        auto multi_result = multi_gpu_engine_->initialize();
        
        if (!multi_result.has_value()) {
            // Non-fatal - continue without multi-GPU
            multi_gpu_engine_.reset();
        }
    }
    
    reset_stats();
    
    return Result<void>(std::monostate{});
}

Result<void> CudaTransport::shutdown() {
    if (multi_gpu_engine_) {
        multi_gpu_engine_->shutdown();
        multi_gpu_engine_.reset();
    }
    
    if (engine_) {
        engine_->shutdown();
        engine_.reset();
    }
    
    return Result<void>(std::monostate{});
}

Result<Completion> CudaTransport::submit(
    const BufferDescriptor& dst,
    const BufferDescriptor& src,
    size_t size
) {
    if (!engine_) {
        return std::error_code(ErrorCode::NotRegistered);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    Result<Completion> result;
    
    // Determine transfer type and submit
    if (src.kind == MemoryKind::CudaDevice && dst.kind == MemoryKind::CudaDevice) {
        // Device-to-device
        if (multi_gpu_engine_ && src.device_id != dst.device_id) {
            result = multi_gpu_engine_->submit_d2d(dst, src, size);
        } else {
            // Same device - use regular transfer engine
            // (Would need D2D support in TransferEngine)
            return std::error_code(ErrorCode::InvalidArgument);
        }
    } else if (src.kind == MemoryKind::CudaDevice && dst.kind == MemoryKind::HostPinned) {
        // Device-to-host
        result = engine_->submit_d2h(src, dst, size);
    } else if (src.kind == MemoryKind::HostPinned && dst.kind == MemoryKind::CudaDevice) {
        // Host-to-device
        result = engine_->submit_h2d(src, dst, size);
    } else {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    if (result.has_value()) {
        update_stats(size, latency_ms, true);
    } else {
        update_stats(0, latency_ms, false);
    }
    
    return result;
}

Result<Completion> CudaTransport::submit_d2d(
    const BufferDescriptor& dst,
    int dst_dev_id,
    const BufferDescriptor& src,
    int src_dev_id,
    size_t size
) {
    if (!multi_gpu_engine_) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    auto result = multi_gpu_engine_->submit_d2d_explicit(dst, dst_dev_id, src, src_dev_id, size);
    
    auto end = std::chrono::high_resolution_clock::now();
    double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    if (result.has_value()) {
        update_stats(size, latency_ms, true);
    } else {
        update_stats(0, latency_ms, false);
    }
    
    return result;
}

Result<void> CudaTransport::synchronize() {
    if (engine_) {
        return engine_->synchronize();
    }
    return Result<void>(std::monostate{});
}

TransportStats CudaTransport::get_stats() const {
    std::unique_lock<std::mutex> lock(stats_mutex_);
    return stats_;
}

void CudaTransport::reset_stats() {
    std::unique_lock<std::mutex> lock(stats_mutex_);
    stats_ = TransportStats();
}

void CudaTransport::update_stats(size_t bytes, double latency_ms, bool success) {
    std::unique_lock<std::mutex> lock(stats_mutex_);
    
    if (success) {
        stats_.bytes_transferred += bytes;
        stats_.operations_completed++;
        
        // Update average latency (simple moving average)
        double n = static_cast<double>(stats_.operations_completed);
        stats_.avg_latency_ms = ((n - 1) * stats_.avg_latency_ms + latency_ms) / n;
        
        // Update peak throughput
        double throughput_gbps = (bytes / 1024.0 / 1024.0 / 1024.0) / (latency_ms / 1000.0);
        if (throughput_gbps > stats_.peak_throughput_gbps) {
            stats_.peak_throughput_gbps = throughput_gbps;
        }
    } else {
        stats_.operations_failed++;
    }
}

std::unique_ptr<ITransport> create_cuda_transport() {
    TransferEngineConfig config;
    return std::make_unique<CudaTransport>(config);
}

// TransportRegistry implementation

void TransportRegistry::register_transport(const std::string& name, TransportFactory factory) {
    instance().factories_[name] = factory;
}

Result<std::unique_ptr<ITransport>> TransportRegistry::create(const std::string& name) {
    auto& registry = instance();
    
    auto it = registry.factories_.find(name);
    if (it == registry.factories_.end()) {
        return std::error_code(ErrorCode::NotFound);
    }
    
    return Result<std::unique_ptr<ITransport>>(it->second());
}

std::vector<std::string> TransportRegistry::get_available_transports() {
    std::vector<std::string> names;
    for (const auto& pair : instance().factories_) {
        names.push_back(pair.first);
    }
    return names;
}

TransportRegistry& TransportRegistry::instance() {
    static TransportRegistry registry;
    return registry;
}

} // namespace pm_nixl
