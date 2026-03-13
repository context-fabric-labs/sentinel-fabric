#pragma once

#include "transfer.hpp"
#include "buffer.hpp"
#include <vector>
#include <memory>

namespace pm_nixl {

/**
 * Transfer engine configuration
 */
struct TransferEngineConfig {
    int device_id;              // CUDA device ID
    size_t num_streams;         // Number of CUDA streams
    bool enable_p2p;            // Enable P2P access (for D2D)
    
    constexpr TransferEngineConfig()
        : device_id(0)
        , num_streams(4)
        , enable_p2p(false) {}
};

/**
 * Async transfer engine
 * 
 * Manages CUDA streams and submits async transfer operations.
 */
class TransferEngine {
public:
    /**
     * Construct a transfer engine
     */
    explicit TransferEngine(const TransferEngineConfig& config = TransferEngineConfig());
    
    /**
     * Destructor
     */
    ~TransferEngine();
    
    // Disable copy
    TransferEngine(const TransferEngine&) = delete;
    TransferEngine& operator=(const TransferEngine&) = delete;
    
    /**
     * Initialize the engine
     */
    Result<void> initialize();
    
    /**
     * Shutdown the engine
     */
    Result<void> shutdown();
    
    /**
     * Check if engine is initialized
     */
    bool is_initialized() const { return initialized_; }
    
    /**
     * Submit an async transfer
     * 
     * @param desc Transfer descriptor
     * @return Completion handle
     */
    Result<Completion> submit(const TransferDescriptor& desc);
    
    /**
     * Submit host-to-device transfer
     */
    Result<Completion> submit_h2d(
        const BufferDescriptor& host,
        const BufferDescriptor& device,
        size_t size,
        size_t offset = 0
    );
    
    /**
     * Submit device-to-host transfer
     */
    Result<Completion> submit_d2h(
        const BufferDescriptor& device,
        const BufferDescriptor& host,
        size_t size,
        size_t offset = 0
    );
    
    /**
     * Wait for a completion
     */
    Result<void> wait(Completion& completion);
    
    /**
     * Try to complete a pending operation (non-blocking)
     */
    bool try_complete(Completion& completion);
    
    /**
     * Synchronize all streams (blocking)
     */
    Result<void> synchronize();
    
    /**
     * Get number of pending operations
     */
    size_t pending_count() const;
    
    /**
     * Get device ID
     */
    int device_id() const { return config_.device_id; }
    
    /**
     * Get number of streams
     */
    size_t num_streams() const { return config_.num_streams; }
    
private:
    TransferEngineConfig config_;
    bool initialized_;
    std::vector<void*> streams_;  // Opaque CUDA stream pointers
    std::vector<void*> events_;   // Opaque CUDA event pointers
    uint64_t next_operation_id_;
    size_t pending_count_;
    
    /**
     * Get next stream (round-robin)
     */
    void* get_next_stream();
    
    /**
     * Create CUDA event
     */
    void* create_event();
    
    /**
     * Record event on stream
     */
    void record_event(void* event, void* stream);
    
    /**
     * Check if event is complete
     */
    bool is_event_complete(void* event);
    
    /**
     * Destroy event
     */
    void destroy_event(void* event);
    
    /**
     * Execute transfer on stream
     */
    Result<void> execute_transfer(
        const TransferDescriptor& desc,
        void* stream,
        void* event
    );
};

} // namespace pm_nixl
