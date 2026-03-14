#pragma once

#include "buffer.hpp"
#include "completion_queue.hpp"
#include <string>
#include <memory>

namespace pm_nixl {

/**
 * Transport capabilities flags
 * 
 * Bitmask of capabilities supported by a transport
 */
enum class TransportCaps : uint32_t {
    None = 0,
    HostToDevice = (1 << 0),
    DeviceToHost = (1 << 1),
    DeviceToDevice = (1 << 2),
    P2P = (1 << 3),
    Async = (1 << 4),
    MultiNode = (1 << 5)
};

inline TransportCaps operator|(TransportCaps a, TransportCaps b) {
    return static_cast<TransportCaps>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline TransportCaps operator&(TransportCaps a, TransportCaps b) {
    return static_cast<TransportCaps>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool has_cap(TransportCaps caps, TransportCaps cap) {
    return (caps & cap) != TransportCaps::None;
}

/**
 * Transport statistics
 */
struct TransportStats {
    size_t bytes_transferred;
    size_t operations_completed;
    size_t operations_failed;
    double avg_latency_ms;
    double peak_throughput_gbps;
    
    TransportStats()
        : bytes_transferred(0)
        , operations_completed(0)
        , operations_failed(0)
        , avg_latency_ms(0.0)
        , peak_throughput_gbps(0.0) {}
};

/**
 * Transport Interface
 * 
 * Minimal abstraction for data transfer operations.
 * This is the seam where future transport backends (UCX, NCCL, etc.) can attach.
 * 
 * DESIGN NOTES:
 * - Keep this interface minimal and stable
 * - Don't over-generalize - focus on common operations
 * - Current implementation: CUDA copy engine
 * - Future implementations: UCX, NCCL, custom fabrics
 * 
 * WHY THIS SEAM IS ENOUGH:
 * 1. Separates transport mechanism from higher-level logic
 * 2. Allows swapping transports without changing callers
 * 3. Clear capability negotiation
 * 4. Statistics for performance monitoring
 * 
 * HOW TO AVOID OVERENGINEERING:
 * - Don't add every possible operation
 * - Don't create complex plugin frameworks
 * - Don't abstract away all differences between transports
 * - Keep implementation details in concrete classes
 * 
 * WHERE UCX WOULD FIT:
 * - Create UcxTransport class implementing this interface
 * - Implement submit() using UCX put/get operations
 * - Handle UCX progress and completion
 * - Map UCX capabilities to TransportCaps
 */
class ITransport {
public:
    virtual ~ITransport() = default;
    
    /**
     * Get transport name (for debugging/logging)
     */
    virtual std::string name() const = 0;
    
    /**
     * Get transport capabilities
     */
    virtual TransportCaps capabilities() const = 0;
    
    /**
     * Initialize transport
     */
    virtual Result<void> initialize() = 0;
    
    /**
     * Shutdown transport
     */
    virtual Result<void> shutdown() = 0;
    
    /**
     * Submit a transfer operation
     * 
     * @param dst Destination buffer
     * @param src Source buffer
     * @param size Size to transfer
     * @return Completion handle
     */
    virtual Result<Completion> submit(
        const BufferDescriptor& dst,
        const BufferDescriptor& src,
        size_t size
    ) = 0;
    
    /**
     * Submit a device-to-device transfer
     * 
     * @param dst Destination buffer
     * @param dst_dev_id Destination device ID
     * @param src Source buffer
     * @param src_dev_id Source device ID
     * @param size Size to transfer
     * @return Completion handle
     */
    virtual Result<Completion> submit_d2d(
        const BufferDescriptor& dst,
        int dst_dev_id,
        const BufferDescriptor& src,
        int src_dev_id,
        size_t size
    ) {
        // Default: not supported
        // Concrete implementations override if they support D2D
        (void)dst; (void)dst_dev_id; (void)src; (void)src_dev_id; (void)size;
        return std::error_code(ErrorCode::InvalidArgument);
    }
    
    /**
     * Synchronize all pending operations
     */
    virtual Result<void> synchronize() = 0;
    
    /**
     * Get transport statistics
     */
    virtual TransportStats get_stats() const = 0;
    
    /**
     * Reset statistics
     */
    virtual void reset_stats() = 0;
};

/**
 * Transport factory function type
 * 
 * Future transport backends can register factory functions
 */
using TransportFactory = std::function<std::unique_ptr<ITransport>()>;

/**
 * Transport registry
 * 
 * Simple registry for available transports.
 * Not a full plugin framework - just a place to register factories.
 */
class TransportRegistry {
public:
    /**
     * Register a transport factory
     * 
     * @param name Transport name
     * @param factory Factory function
     */
    static void register_transport(const std::string& name, TransportFactory factory);
    
    /**
     * Create a transport by name
     * 
     * @param name Transport name
     * @return Transport instance or error
     */
    static Result<std::unique_ptr<ITransport>> create(const std::string& name);
    
    /**
     * Get list of registered transport names
     */
    static std::vector<std::string> get_available_transports();
    
private:
    // Private constructor - singleton pattern
    TransportRegistry() = default;
    
    static TransportRegistry& instance();
    
    std::unordered_map<std::string, TransportFactory> factories_;
};

} // namespace pm_nixl
