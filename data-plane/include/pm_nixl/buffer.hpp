#pragma once

#include "descriptor.hpp"
#include "error.hpp"
#include <memory>

namespace pm_nixl {

/**
 * Buffer class with RAII semantics
 * 
 * Manages buffer lifetime and provides descriptor access.
 * Supports host (pinned/pageable) and device memory.
 */
class Buffer {
public:
    /**
     * Default constructor (empty buffer)
     */
    Buffer();
    
    /**
     * Construct a buffer
     * 
     * @param kind Memory kind (HostPinned, HostPageable, CudaDevice)
     * @param size Size in bytes
     * @param device_id CUDA device ID (for device memory)
     */
    explicit Buffer(MemoryKind kind, size_t size, int device_id = 0);
    
    /**
     * Destructor - frees memory
     */
    ~Buffer();
    
    // Disable copy (unique ownership)
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    
    // Enable move
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    
    /**
     * Get buffer descriptor
     */
    const BufferDescriptor& descriptor() const { return desc_; }
    
    /**
     * Get raw pointer
     */
    void* ptr() const { return desc_.ptr; }
    
    /**
     * Get size in bytes
     */
    size_t size() const { return desc_.size; }
    
    /**
     * Get memory kind
     */
    MemoryKind kind() const { return desc_.kind; }
    
    /**
     * Get device ID
     */
    int device_id() const { return desc_.device_id; }
    
    /**
     * Check if buffer is valid
     */
    bool is_valid() const { return desc_.is_valid(); }
    
    /**
     * Check if buffer is registered
     */
    bool is_registered() const { return desc_.is_registered; }
    
    /**
     * Get registration ID
     */
    uint64_t registration_id() const { return desc_.registration_id; }
    
    /**
     * Reset buffer (free memory)
     */
    void reset();
    
    /**
     * Swap two buffers
     */
    void swap(Buffer& other) noexcept;
    
private:
    BufferDescriptor desc_;
    
    /**
     * Allocate memory for buffer
     */
    void allocate();
    
    /**
     * Free buffer memory
     */
    void deallocate();
    
    /**
     * Register buffer (assign registration ID)
     */
    void register_buffer();
};

/**
 * Create a host pinned buffer
 */
Buffer create_host_pinned_buffer(size_t size);

/**
 * Create a host pageable buffer
 */
Buffer create_host_pageable_buffer(size_t size);

/**
 * Create a device buffer
 */
Buffer create_device_buffer(size_t size, int device_id = 0);

} // namespace pm_nixl
