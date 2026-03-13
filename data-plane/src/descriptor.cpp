#include "pm_nixl/descriptor.hpp"
#include <atomic>

namespace pm_nixl {

// Static initialization
uint64_t RegistrationIdGenerator::next_id_ = 1;

const char* memory_kind_to_string(MemoryKind kind) {
    switch (kind) {
        case MemoryKind::HostPinned:
            return "HostPinned";
        case MemoryKind::HostPageable:
            return "HostPageable";
        case MemoryKind::CudaDevice:
            return "CudaDevice";
        default:
            return "Unknown";
    }
}

uint64_t RegistrationIdGenerator::generate() {
    return next_id_++;
}

void RegistrationIdGenerator::reset() {
    next_id_ = 1;
}

bool validate_descriptor(const BufferDescriptor& desc) {
    // Check basic validity
    if (desc.ptr == nullptr) {
        return false;
    }
    
    if (desc.size == 0) {
        return false;
    }
    
    if (!desc.is_registered) {
        return false;
    }
    
    // Check alignment
    if (!desc.is_aligned()) {
        return false;
    }
    
    // Check device ID for device memory
    if (desc.kind == MemoryKind::CudaDevice && desc.device_id < 0) {
        return false;
    }
    
    return true;
}

bool validate_descriptor_for_transfer(const BufferDescriptor& desc, bool is_source) {
    if (!validate_descriptor(desc)) {
        return false;
    }
    
    // Additional checks for transfer operations
    if (desc.size > (1ULL << 40)) {  // 1TB limit
        return false;
    }
    
    return true;
}

} // namespace pm_nixl
