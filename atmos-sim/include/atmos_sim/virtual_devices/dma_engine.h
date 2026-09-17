#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/resource_pool.h"
#include "atmos_sim/sim_core/bounded_queue.h"
#include "atmos_sim/sim_core/dependency_tracker.h"
#include <memory>
#include <string>

namespace atmos_sim {

/// Configuration for a DMA engine.
struct DMAConfig {
    std::string name = "DMA";
    uint32_t descriptor_queue_depth = 64;
    uint32_t max_outstanding = 16;
    uint32_t channel_count = 2;
    uint64_t setup_overhead_ns = 200;  // Per-descriptor setup cost
    uint64_t bandwidth_bytes_sec = 32ULL * 1000 * 1000 * 1000;  // 32 GB/s
};

/// A DMA transfer descriptor.
struct DMATransfer {
    uint64_t src_address = 0;
    uint64_t dst_address = 0;
    uint64_t size_bytes = 0;
    ResourceId src_resource = INVALID_RESOURCE;
    ResourceId dst_resource = INVALID_RESOURCE;
    OpId requestor_op = 0;
};

/// DMA Engine — models descriptor queuing, setup overhead, and channel scheduling.
class DMAEngine {
public:
    explicit DMAEngine(ResourceId id, const DMAConfig& config);

    ResourceId id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const DMAConfig& config() const { return config_; }

    bool can_accept() const;

    /// Submit a DMA transfer. Returns estimated completion time.
    SimTime submit_transfer(const DMATransfer& transfer);

    uint32_t outstanding() const { return outstanding_; }
    double utilization() const;

    BoundedQueue& descriptor_queue() { return *descriptor_queue_; }
    ResourcePool& channel_pool() { return *channels_; }

private:
    ResourceId id_;
    DMAConfig config_;
    std::unique_ptr<BoundedQueue> descriptor_queue_;
    std::unique_ptr<ResourcePool> channels_;
    uint32_t outstanding_ = 0;
};

} // namespace atmos_sim
