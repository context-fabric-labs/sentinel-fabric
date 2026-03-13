#pragma once

#include "descriptor.hpp"
#include "error.hpp"
#include <cstdint>

namespace pm_nixl {

/**
 * Transfer type enumeration
 */
enum class TransferType : uint8_t {
    HostToDevice = 0,
    DeviceToHost = 1,
    DeviceToDevice = 2
};

/**
 * Transfer descriptor
 * 
 * Describes a single transfer operation without executing it.
 */
struct TransferDescriptor {
    BufferDescriptor src;
    BufferDescriptor dst;
    size_t size;
    size_t src_offset;
    size_t dst_offset;
    TransferType type;
    uint64_t operation_id;
    
    /**
     * Default constructor
     */
    constexpr TransferDescriptor()
        : src()
        , dst()
        , size(0)
        , src_offset(0)
        , dst_offset(0)
        , type(TransferType::HostToDevice)
        , operation_id(0) {}
    
    /**
     * Construct a transfer descriptor
     */
    TransferDescriptor(
        const BufferDescriptor& s,
        const BufferDescriptor& d,
        size_t sz,
        TransferType t = TransferType::HostToDevice
    )
        : src(s)
        , dst(d)
        , size(sz)
        , src_offset(0)
        , dst_offset(0)
        , type(t)
        , operation_id(0) {}
    
    /**
     * Validate transfer descriptor
     */
    bool is_valid() const;
    
    /**
     * Get transfer type string
     */
    const char* type_to_string() const;
};

/**
 * Completion status
 */
enum class CompletionStatus : uint8_t {
    Pending = 0,
    Complete = 1,
    Error = 2
};

/**
 * Completion handle
 * 
 * Tracks the status of an async operation
 */
class Completion {
public:
    /**
     * Default constructor (invalid completion)
     */
    Completion();
    
    /**
     * Construct a completion handle
     */
    explicit Completion(uint64_t op_id);
    
    /**
     * Destructor
     */
    ~Completion();
    
    // Disable copy
    Completion(const Completion&) = delete;
    Completion& operator=(const Completion&) = delete;
    
    // Enable move
    Completion(Completion&& other) noexcept;
    Completion& operator=(Completion&& other) noexcept;
    
    /**
     * Get operation ID
     */
    uint64_t operation_id() const { return operation_id_; }
    
    /**
     * Check if completion is valid
     */
    bool is_valid() const { return operation_id_ != 0; }
    
    /**
     * Check if operation is complete
     */
    bool is_complete() const;
    
    /**
     * Get completion status
     */
    CompletionStatus status() const;
    
    /**
     * Wait for completion (blocking)
     */
    Result<void> wait();
    
    /**
     * Try to complete (non-blocking)
     */
    bool try_complete();
    
    /**
     * Get error message if failed
     */
    const char* error_message() const { return error_message_; }
    
private:
    uint64_t operation_id_;
    CompletionStatus status_;
    const char* error_message_;
    void* cuda_event_;  // Opaque CUDA event pointer
    
    /**
     * Set error status
     */
    void set_error(const char* msg);
};

/**
 * Get string representation of TransferType
 */
const char* transfer_type_to_string(TransferType type);

/**
 * Determine transfer type from buffer descriptors
 */
TransferType infer_transfer_type(const BufferDescriptor& src, const BufferDescriptor& dst);

} // namespace pm_nixl
