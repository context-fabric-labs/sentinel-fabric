#include "pm_nixl/transfer.hpp"
#include <cuda_runtime.h>

namespace pm_nixl {

const char* transfer_type_to_string(TransferType type) {
    switch (type) {
        case TransferType::HostToDevice:
            return "HostToDevice";
        case TransferType::DeviceToHost:
            return "DeviceToHost";
        case TransferType::DeviceToDevice:
            return "DeviceToDevice";
        default:
            return "Unknown";
    }
}

const char* TransferDescriptor::type_to_string() const {
    return transfer_type_to_string(type);
}

TransferType infer_transfer_type(const BufferDescriptor& src, const BufferDescriptor& dst) {
    if (is_host_memory(src.kind) && is_device_memory(dst.kind)) {
        return TransferType::HostToDevice;
    } else if (is_device_memory(src.kind) && is_host_memory(dst.kind)) {
        return TransferType::DeviceToHost;
    } else if (is_device_memory(src.kind) && is_device_memory(dst.kind)) {
        return TransferType::DeviceToDevice;
    } else {
        return TransferType::HostToDevice;  // Default
    }
}

bool TransferDescriptor::is_valid() const {
    if (!validate_descriptor(src)) {
        return false;
    }
    
    if (!validate_descriptor(dst)) {
        return false;
    }
    
    if (size == 0) {
        return false;
    }
    
    if (src_offset + size > src.size) {
        return false;
    }
    
    if (dst_offset + size > dst.size) {
        return false;
    }
    
    // Check transfer type matches buffer kinds
    TransferType inferred = infer_transfer_type(src, dst);
    if (inferred != type) {
        return false;
    }
    
    return true;
}

// Completion implementation

Completion::Completion()
    : operation_id_(0)
    , status_(CompletionStatus::Pending)
    , error_message_(nullptr)
    , cuda_event_(nullptr) {}

Completion::Completion(uint64_t op_id)
    : operation_id_(op_id)
    , status_(CompletionStatus::Pending)
    , error_message_(nullptr)
    , cuda_event_(nullptr) {}

Completion::~Completion() {
    // Event is managed by TransferEngine
}

Completion::Completion(Completion&& other) noexcept
    : operation_id_(other.operation_id_)
    , status_(other.status_)
    , error_message_(other.error_message_)
    , cuda_event_(other.cuda_event_) {
    other.operation_id_ = 0;
    other.status_ = CompletionStatus::Pending;
    other.cuda_event_ = nullptr;
}

Completion& Completion::operator=(Completion&& other) noexcept {
    if (this != &other) {
        operation_id_ = other.operation_id_;
        status_ = other.status_;
        error_message_ = other.error_message_;
        cuda_event_ = other.cuda_event_;
        
        other.operation_id_ = 0;
        other.status_ = CompletionStatus::Pending;
        other.cuda_event_ = nullptr;
    }
    return *this;
}

bool Completion::is_complete() const {
    return status_ != CompletionStatus::Pending;
}

CompletionStatus Completion::status() const {
    return status_;
}

Result<void> Completion::wait() {
    if (cuda_event_ == nullptr) {
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    cudaEvent_t event = static_cast<cudaEvent_t>(cuda_event_);
    cudaError_t err = cudaEventSynchronize(event);
    
    if (err != cudaSuccess) {
        set_error(cudaGetErrorString(err));
        return std::error_code(ErrorCode::CudaError);
    }
    
    status_ = CompletionStatus::Complete;
    return Result<void>(std::monostate{});
}

bool Completion::try_complete() {
    if (cuda_event_ == nullptr) {
        return false;
    }
    
    cudaEvent_t event = static_cast<cudaEvent_t>(cuda_event_);
    cudaError_t err = cudaEventQuery(event);
    
    if (err == cudaSuccess) {
        status_ = CompletionStatus::Complete;
        return true;
    } else if (err == cudaErrorNotReady) {
        return false;
    } else {
        set_error(cudaGetErrorString(err));
        status_ = CompletionStatus::Error;
        return true;  // Complete with error
    }
}

void Completion::set_error(const char* msg) {
    status_ = CompletionStatus::Error;
    error_message_ = msg;
}

} // namespace pm_nixl
