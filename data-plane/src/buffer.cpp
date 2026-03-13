#include "pm_nixl/buffer.hpp"
#include "pm_nixl/error.hpp"
#include <cuda_runtime.h>
#include <cstring>

namespace pm_nixl {

Buffer::Buffer()
    : desc_() {}

Buffer::Buffer(MemoryKind kind, size_t size, int device_id)
    : desc_(nullptr, size, kind, device_id) {
    allocate();
    register_buffer();
}

Buffer::~Buffer() {
    deallocate();
}

Buffer::Buffer(Buffer&& other) noexcept
    : desc_(other.desc_) {
    other.desc_ = BufferDescriptor();
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        deallocate();
        desc_ = other.desc_;
        other.desc_ = BufferDescriptor();
    }
    return *this;
}

void Buffer::reset() {
    deallocate();
    desc_ = BufferDescriptor();
}

void Buffer::swap(Buffer& other) noexcept {
    std::swap(desc_, other.desc_);
}

void Buffer::allocate() {
    if (desc_.size == 0) {
        return;
    }
    
    cudaError_t cuda_err;
    
    switch (desc_.kind) {
        case MemoryKind::HostPinned: {
            // Allocate page-locked host memory
            cuda_err = cudaHostAlloc(&desc_.ptr, desc_.size, cudaHostAllocDefault);
            if (cuda_err != cudaSuccess) {
                throw PMNIXLError(ErrorCode::OutOfMemory, 
                    "Failed to allocate pinned host memory: " + std::string(cudaGetErrorString(cuda_err)));
            }
            desc_.alignment = BufferDescriptor::get_default_alignment(desc_.kind);
            break;
        }
        
        case MemoryKind::HostPageable: {
            // Allocate regular host memory
            desc_.ptr = std::malloc(desc_.size);
            if (desc_.ptr == nullptr) {
                throw PMNIXLError(ErrorCode::OutOfMemory, "Failed to allocate pageable host memory");
            }
            desc_.alignment = BufferDescriptor::get_default_alignment(desc_.kind);
            break;
        }
        
        case MemoryKind::CudaDevice: {
            // Allocate device memory
            if (desc_.device_id >= 0) {
                cuda_err = cudaSetDevice(desc_.device_id);
                if (cuda_err != cudaSuccess) {
                    throw PMNIXLError(ErrorCode::DeviceError,
                        "Failed to set CUDA device: " + std::string(cudaGetErrorString(cuda_err)));
                }
            }
            
            cuda_err = cudaMalloc(&desc_.ptr, desc_.size);
            if (cuda_err != cudaSuccess) {
                throw PMNIXLError(ErrorCode::OutOfMemory,
                    "Failed to allocate device memory: " + std::string(cudaGetErrorString(cuda_err)));
            }
            desc_.alignment = BufferDescriptor::get_default_alignment(desc_.kind);
            break;
        }
        
        default:
            throw PMNIXLError(ErrorCode::InvalidArgument, "Unknown memory kind");
    }
}

void Buffer::deallocate() {
    if (desc_.ptr == nullptr) {
        return;
    }
    
    switch (desc_.kind) {
        case MemoryKind::HostPinned:
            cudaFreeHost(desc_.ptr);
            break;
        
        case MemoryKind::HostPageable:
            std::free(desc_.ptr);
            break;
        
        case MemoryKind::CudaDevice:
            cudaFree(desc_.ptr);
            break;
        
        default:
            break;
    }
    
    desc_.ptr = nullptr;
}

void Buffer::register_buffer() {
    if (desc_.ptr != nullptr && desc_.size > 0) {
        desc_.registration_id = RegistrationIdGenerator::generate();
        desc_.is_registered = true;
        desc_.version = 1;
    }
}

Buffer create_host_pinned_buffer(size_t size) {
    return Buffer(MemoryKind::HostPinned, size);
}

Buffer create_host_pageable_buffer(size_t size) {
    return Buffer(MemoryKind::HostPageable, size);
}

Buffer create_device_buffer(size_t size, int device_id) {
    return Buffer(MemoryKind::CudaDevice, size, device_id);
}

} // namespace pm_nixl
