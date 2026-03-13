#pragma once

#include <cstdint>
#include <cstddef>

namespace pm_nixl {

/**
 * Memory kind classification
 * 
 * Distinguishes between different types of memory:
 * - HostPinned: Page-locked host memory (fastest H2D/D2H)
 * - HostPageable: Regular host memory (slower H2D/D2H)
 * - CudaDevice: Device memory (GPU)
 */
enum class MemoryKind : uint8_t {
    HostPinned = 0,
    HostPageable = 1,
    CudaDevice = 2
};

/**
 * Get string representation of MemoryKind
 */
const char* memory_kind_to_string(MemoryKind kind);

/**
 * Check if memory kind is host memory
 */
constexpr bool is_host_memory(MemoryKind kind) {
    return kind == MemoryKind::HostPinned || kind == MemoryKind::HostPageable;
}

/**
 * Check if memory kind is device memory
 */
constexpr bool is_device_memory(MemoryKind kind) {
    return kind == MemoryKind::CudaDevice;
}

/**
 * Buffer descriptor
 * 
 * Contains metadata about a buffer without owning it.
 * Used to describe buffers for transfer operations.
 */
struct BufferDescriptor {
    void* ptr;                    // Pointer to buffer
    size_t size;                  // Size in bytes
    MemoryKind kind;              // Memory type
    int device_id;                // CUDA device ID (-1 for host)
    size_t alignment;             // Alignment in bytes
    uint64_t registration_id;     // Unique registration ID
    bool is_registered;           // Registration status
    uint64_t version;             // Version for ABA detection
    
    /**
     * Default constructor (invalid descriptor)
     */
    constexpr BufferDescriptor()
        : ptr(nullptr)
        , size(0)
        , kind(MemoryKind::HostPageable)
        , device_id(-1)
        , alignment(1)
        , registration_id(0)
        , is_registered(false)
        , version(0) {}
    
    /**
     * Construct a descriptor
     */
    constexpr BufferDescriptor(void* p, size_t s, MemoryKind k, int dev_id = -1)
        : ptr(p)
        , size(s)
        , kind(k)
        , device_id(dev_id)
        , alignment(get_default_alignment(k))
        , registration_id(0)
        , is_registered(false)
        , version(1) {}
    
    /**
     * Check if descriptor is valid
     */
    constexpr bool is_valid() const {
        return ptr != nullptr && size > 0 && is_registered;
    }
    
    /**
     * Check if pointer is aligned
     */
    constexpr bool is_aligned() const {
        return reinterpret_cast<uintptr_t>(ptr) % alignment == 0;
    }
    
    /**
     * Get default alignment for memory kind
     */
    static constexpr size_t get_default_alignment(MemoryKind kind) {
        switch (kind) {
            case MemoryKind::HostPinned:
                return 4096;  // Page alignment for pinned memory
            case MemoryKind::CudaDevice:
                return 512;   // Typical GPU memory alignment
            case MemoryKind::HostPageable:
            default:
                return 1;     // No special alignment
        }
    }
};

/**
 * Registration ID generator
 * 
 * Generates unique IDs for buffer registration
 */
class RegistrationIdGenerator {
public:
    /**
     * Generate a unique registration ID
     */
    static uint64_t generate();
    
    /**
     * Reset the generator (for testing)
     */
    static void reset();
    
private:
    static uint64_t next_id_;
};

/**
 * Validate buffer descriptor
 * 
 * Returns true if descriptor is valid, false otherwise
 */
bool validate_descriptor(const BufferDescriptor& desc);

/**
 * Validate descriptor for specific operation
 */
bool validate_descriptor_for_transfer(const BufferDescriptor& desc, bool is_source);

} // namespace pm_nixl
