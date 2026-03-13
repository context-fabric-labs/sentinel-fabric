#include "pm_nixl/transfer_engine.hpp"
#include "pm_nixl/error.hpp"
#include <cuda_runtime.h>
#include <algorithm>

namespace pm_nixl {

TransferEngine::TransferEngine(const TransferEngineConfig& config)
    : config_(config)
    , initialized_(false)
    , next_operation_id_(1)
    , pending_count_(0) {}

TransferEngine::~TransferEngine() {
    if (initialized_) {
        shutdown();
    }
}

Result<void> TransferEngine::initialize() {
    if (initialized_) {
        return std::error_code(ErrorCode::AlreadyRegistered);
    }
    
    // Set device
    cudaError_t err = cudaSetDevice(config_.device_id);
    if (err != cudaSuccess) {
        return std::error_code(ErrorCode::DeviceError);
    }
    
    // Create streams
    streams_.resize(config_.num_streams);
    for (size_t i = 0; i < config_.num_streams; ++i) {
        err = cudaStreamCreate(&streams_[i]);
        if (err != cudaSuccess) {
            // Cleanup on failure
            for (size_t j = 0; j < i; ++j) {
                cudaStreamDestroy(streams_[j]);
            }
            return std::error_code(ErrorCode::CudaError);
        }
    }
    
    // Create events
    events_.resize(config_.num_streams);
    for (size_t i = 0; i < config_.num_streams; ++i) {
        err = cudaEventCreate(&events_[i]);
        if (err != cudaSuccess) {
            // Cleanup on failure
            for (size_t j = 0; j < config_.num_streams; ++j) {
                cudaStreamDestroy(streams_[j]);
            }
            for (size_t j = 0; j < i; ++j) {
                cudaEventDestroy(events_[j]);
            }
            return std::error_code(ErrorCode::CudaError);
        }
    }
    
    initialized_ = true;
    return Result<void>(std::monostate{});
}

Result<void> TransferEngine::shutdown() {
    if (!initialized_) {
        return std::error_code(ErrorCode::NotRegistered);
    }
    
    // Synchronize all streams
    synchronize();
    
    // Destroy streams
    for (void* stream : streams_) {
        cudaStreamDestroy(static_cast<cudaStream_t>(stream));
    }
    streams_.clear();
    
    // Destroy events
    for (void* event : events_) {
        cudaEventDestroy(static_cast<cudaEvent_t>(event));
    }
    events_.clear();
    
    initialized_ = false;
    return Result<void>(std::monostate{});
}

Result<Completion> TransferEngine::submit(const TransferDescriptor& desc) {
    if (!initialized_) {
        return std::error_code(ErrorCode::NotRegistered);
    }
    
    if (!desc.is_valid()) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    // Get next stream and event
    void* stream = get_next_stream();
    void* event = create_event();
    
    // Create completion handle
    uint64_t op_id = next_operation_id_++;
    Completion completion(op_id);
    completion.cuda_event_ = event;
    
    // Execute transfer
    auto result = execute_transfer(desc, stream, event);
    if (!result.has_value()) {
        destroy_event(event);
        return result.error();
    }
    
    pending_count_++;
    return Result<Completion>(std::move(completion));
}

Result<Completion> TransferEngine::submit_h2d(
    const BufferDescriptor& host,
    const BufferDescriptor& device,
    size_t size,
    size_t offset
) {
    TransferDescriptor desc(host, device, size, TransferType::HostToDevice);
    desc.src_offset = offset;
    desc.dst_offset = offset;
    return submit(desc);
}

Result<Completion> TransferEngine::submit_d2h(
    const BufferDescriptor& device,
    const BufferDescriptor& host,
    size_t size,
    size_t offset
) {
    TransferDescriptor desc(device, host, size, TransferType::DeviceToHost);
    desc.src_offset = offset;
    desc.dst_offset = offset;
    return submit(desc);
}

Result<void> TransferEngine::wait(Completion& completion) {
    if (!completion.is_valid()) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    auto result = completion.wait();
    if (result.has_value()) {
        pending_count_--;
    }
    return result;
}

bool TransferEngine::try_complete(Completion& completion) {
    if (!completion.is_valid()) {
        return false;
    }
    
    if (completion.try_complete()) {
        pending_count_--;
        return true;
    }
    return false;
}

Result<void> TransferEngine::synchronize() {
    if (!initialized_) {
        return std::error_code(ErrorCode::NotRegistered);
    }
    
    for (void* stream : streams_) {
        cudaError_t err = cudaStreamSynchronize(static_cast<cudaStream_t>(stream));
        if (err != cudaSuccess) {
            return std::error_code(ErrorCode::CudaError);
        }
    }
    
    pending_count_ = 0;
    return Result<void>(std::monostate{});
}

size_t TransferEngine::pending_count() const {
    return pending_count_;
}

void* TransferEngine::get_next_stream() {
    static thread_local size_t next_index = 0;
    size_t index = next_index++ % streams_.size();
    return streams_[index];
}

void* TransferEngine::create_event() {
    cudaEvent_t event;
    cudaError_t err = cudaEventCreate(&event);
    if (err != cudaSuccess) {
        return nullptr;
    }
    return event;
}

void TransferEngine::record_event(void* event, void* stream) {
    cudaEventRecord(static_cast<cudaEvent_t>(event), static_cast<cudaStream_t>(stream));
}

bool TransferEngine::is_event_complete(void* event) {
    cudaError_t err = cudaEventQuery(static_cast<cudaEvent_t>(event));
    return err == cudaSuccess;
}

void TransferEngine::destroy_event(void* event) {
    if (event != nullptr) {
        cudaEventDestroy(static_cast<cudaEvent_t>(event));
    }
}

Result<void> TransferEngine::execute_transfer(
    const TransferDescriptor& desc,
    void* stream,
    void* event
) {
    cudaStream_t cuda_stream = static_cast<cudaStream_t>(stream);
    
    void* src_ptr = static_cast<char*>(desc.src.ptr) + desc.src_offset;
    void* dst_ptr = static_cast<char*>(desc.dst.ptr) + desc.dst_offset;
    
    cudaError_t err;
    
    switch (desc.type) {
        case TransferType::HostToDevice:
            err = cudaMemcpyAsync(dst_ptr, src_ptr, desc.size, 
                                  cudaMemcpyHostToDevice, cuda_stream);
            break;
        
        case TransferType::DeviceToHost:
            err = cudaMemcpyAsync(src_ptr, dst_ptr, desc.size,
                                  cudaMemcpyDeviceToHost, cuda_stream);
            break;
        
        case TransferType::DeviceToDevice:
            err = cudaMemcpyAsync(dst_ptr, src_ptr, desc.size,
                                  cudaMemcpyDeviceToDevice, cuda_stream);
            break;
        
        default:
            return std::error_code(ErrorCode::InvalidArgument);
    }
    
    if (err != cudaSuccess) {
        return std::error_code(ErrorCode::CudaError);
    }
    
    // Record event for completion tracking
    record_event(event, stream);
    
    return Result<void>(std::monostate{});
}

} // namespace pm_nixl
