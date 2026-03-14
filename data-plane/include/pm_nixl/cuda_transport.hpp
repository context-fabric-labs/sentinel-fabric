#pragma once

#include "transport_interface.hpp"
#include "transfer_engine.hpp"
#include "multi_gpu_transfer.hpp"

namespace pm_nixl {

/**
 * CUDA Transport Implementation
 * 
 * Implements ITransport using CUDA copy operations.
 * This is the primary transport for single-node GPU systems.
 * 
 * FUTURE EXTENSIONS:
 * - UcxTransport: Implement ITransport using UCX for multi-node
 * - NcclTransport: Implement ITransport using NCCL for collective ops
 * - CustomTransport: Implement ITransport for custom fabrics
 */
class CudaTransport : public ITransport {
public:
    /**
     * Construct CUDA transport
     * 
     * @param config Transfer engine configuration
     */
    explicit CudaTransport(const TransferEngineConfig& config = TransferEngineConfig());
    
    /**
     * Destructor
     */
    ~CudaTransport() override;
    
    /**
     * Get transport name
     */
    std::string name() const override { return "CUDA"; }
    
    /**
     * Get transport capabilities
     */
    TransportCaps capabilities() const override;
    
    /**
     * Initialize transport
     */
    Result<void> initialize() override;
    
    /**
     * Shutdown transport
     */
    Result<void> shutdown() override;
    
    /**
     * Submit a transfer operation
     */
    Result<Completion> submit(
        const BufferDescriptor& dst,
        const BufferDescriptor& src,
        size_t size
    ) override;
    
    /**
     * Submit a device-to-device transfer
     */
    Result<Completion> submit_d2d(
        const BufferDescriptor& dst,
        int dst_dev_id,
        const BufferDescriptor& src,
        int src_dev_id,
        size_t size
    ) override;
    
    /**
     * Synchronize all pending operations
     */
    Result<void> synchronize() override;
    
    /**
     * Get transport statistics
     */
    TransportStats get_stats() const override;
    
    /**
     * Reset statistics
     */
    void reset_stats() override;
    
private:
    TransferEngineConfig config_;
    std::unique_ptr<TransferEngine> engine_;
    std::unique_ptr<MultiGpuTransferEngine> multi_gpu_engine_;
    TransportStats stats_;
    mutable std::mutex stats_mutex_;
    
    /**
     * Update statistics after operation
     */
    void update_stats(size_t bytes, double latency_ms, bool success);
};

/**
 * Create CUDA transport factory
 * 
 * Usage:
 *   TransportRegistry::register_transport("cuda", create_cuda_transport);
 */
std::unique_ptr<ITransport> create_cuda_transport();

} // namespace pm_nixl
