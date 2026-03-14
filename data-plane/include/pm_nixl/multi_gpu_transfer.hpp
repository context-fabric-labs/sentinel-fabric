#pragma once

#include "buffer.hpp"
#include "transfer_engine.hpp"
#include "completion_queue.hpp"
#include <vector>

namespace pm_nixl {

/**
 * Multi-GPU transfer engine configuration
 */
struct MultiGpuConfig {
    std::vector<int> device_ids;      // List of device IDs
    bool enable_p2p;                   // Enable P2P access
    bool auto_detect_topology;         // Auto-detect device topology
    
    MultiGpuConfig()
        : enable_p2p(true)
        , auto_detect_topology(true) {}
};

/**
 * Device topology information
 */
struct DevicePairTopology {
    int dev_id1;
    int dev_id2;
    bool p2p_supported;
    bool same_bus;
    int performance_rank;
    
    DevicePairTopology()
        : dev_id1(-1)
        , dev_id2(-1)
        , p2p_supported(false)
        , same_bus(false)
        , performance_rank(0) {}
};

/**
 * Multi-GPU Transfer Engine
 * 
 * Extends TransferEngine with multi-GPU support.
 * Provides device-to-device transfers when P2P is available.
 */
class MultiGpuTransferEngine {
public:
    /**
     * Construct multi-GPU transfer engine
     */
    explicit MultiGpuTransferEngine(const MultiGpuConfig& config = MultiGpuConfig());
    
    /**
     * Destructor
     */
    ~MultiGpuTransferEngine();
    
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
     * Get number of devices
     */
    size_t device_count() const { return config_.device_ids.size(); }
    
    /**
     * Get device IDs
     */
    const std::vector<int>& device_ids() const { return config_.device_ids; }
    
    /**
     * Submit device-to-device transfer
     * 
     * @param dst Destination buffer (device memory)
     * @param src Source buffer (device memory)
     * @param size Size to transfer
     * @return Completion handle
     */
    Result<Completion> submit_d2d(
        const BufferDescriptor& dst,
        const BufferDescriptor& src,
        size_t size
    );
    
    /**
     * Submit device-to-device transfer with explicit devices
     * 
     * @param dst Destination buffer
     * @param dst_dev_id Destination device ID
     * @param src Source buffer
     * @param src_dev_id Source device ID
     * @param size Size to transfer
     * @return Completion handle
     */
    Result<Completion> submit_d2d_explicit(
        const BufferDescriptor& dst,
        int dst_dev_id,
        const BufferDescriptor& src,
        int src_dev_id,
        size_t size
    );
    
    /**
     * Check if P2P is supported between two devices
     */
    bool is_p2p_supported(int dev_id1, int dev_id2) const;
    
    /**
     * Get device topology
     */
    const DevicePairTopology* get_topology(int dev_id1, int dev_id2) const;
    
    /**
     * Get all device topologies
     */
    const std::vector<DevicePairTopology>& get_all_topologies() const { return topologies_; }
    
    /**
     * Print topology information
     */
    void print_topology() const;
    
private:
    MultiGpuConfig config_;
    bool initialized_;
    std::vector<DevicePairTopology> topologies_;
    
    /**
     * Detect device topology
     */
    void detect_topology();
    
    /**
     * Enable P2P access between devices
     */
    Result<void> enable_p2p_access();
    
    /**
     * Disable P2P access
     */
    void disable_p2p_access();
};

/**
 * Multi-GPU benchmark utilities
 */
namespace multi_gpu_bench {

/**
 * Benchmark configuration
 */
struct D2DBenchmarkConfig {
    size_t transfer_size;
    size_t warmup_iterations;
    size_t benchmark_iterations;
    int src_dev_id;
    int dst_dev_id;
    bool use_p2p;
    
    D2DBenchmarkConfig()
        : transfer_size(64 * 1024 * 1024)  // 64MB default
        , warmup_iterations(10)
        , benchmark_iterations(100)
        , src_dev_id(0)
        , dst_dev_id(1)
        , use_p2p(true) {}
};

/**
 * Run D2D benchmark
 * 
 * @param engine Multi-GPU transfer engine
 * @param config Benchmark configuration
 * @return Average latency in milliseconds
 */
double run_d2d_benchmark(
    MultiGpuTransferEngine& engine,
    const D2DBenchmarkConfig& config
);

/**
 * Compare P2P vs non-P2P performance
 * 
 * @param engine Multi-GPU transfer engine
 * @param transfer_size Size of transfer
 * @param warmup_iterations Warmup iterations
 * @param benchmark_iterations Benchmark iterations
 */
void compare_p2p_performance(
    MultiGpuTransferEngine& engine,
    size_t transfer_size,
    size_t warmup_iterations,
    size_t benchmark_iterations
);

} // namespace multi_gpu_bench

} // namespace pm_nixl
