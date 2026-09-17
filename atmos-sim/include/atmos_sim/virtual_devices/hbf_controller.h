#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/resource_pool.h"
#include "atmos_sim/sim_core/bounded_queue.h"
#include "atmos_sim/sim_core/arbiter.h"
#include "atmos_sim/sim_core/dependency_tracker.h"
#include <memory>
#include <vector>
#include <string>

namespace atmos_sim {

/// Configuration for HBF (High Bandwidth Flash) controller.
struct HBFConfig {
    std::string name = "HBF";
    uint32_t channel_count = 8;
    uint64_t capacity_bytes = 128ULL * 1024 * 1024 * 1024;  // 128 GB
    uint64_t bandwidth_per_channel_bytes_sec = 14ULL * 1000 * 1000 * 1000;  // 14 GB/s
    uint32_t queue_depth_per_channel = 32;
    uint32_t max_outstanding = 64;
    uint64_t protocol_overhead_ns = 500;  // 500 ns base protocol delay
    ArbiterPolicy arbitration = ArbiterPolicy::ROUND_ROBIN;
};

/// A memory transaction targeting HBF.
struct MemoryTransaction {
    uint64_t address = 0;
    uint64_t size_bytes = 0;
    bool is_read = true;
    uint32_t channel = 0;
    OpId requestor_op = 0;
};

/// HBF Controller — models the HBF memory subsystem with channel contention.
class HBFController {
public:
    explicit HBFController(ResourceId id, const HBFConfig& config);

    ResourceId id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const HBFConfig& config() const { return config_; }

    /// Check if a transaction can be accepted.
    bool can_accept() const;

    /// Submit a read transaction. Returns estimated completion time.
    SimTime submit_read(uint64_t address, uint64_t size_bytes, OpId requestor);

    /// Submit a write transaction. Returns estimated completion time.
    SimTime submit_write(uint64_t address, uint64_t size_bytes, OpId requestor);

    /// Total bandwidth across all channels.
    uint64_t total_bandwidth() const;

    /// Current outstanding transactions.
    uint32_t outstanding() const { return outstanding_; }

    /// Utilization metric.
    double utilization() const;

    /// Bytes used in the HBF storage.
    uint64_t used_bytes() const { return used_bytes_; }
    bool allocate_storage(uint64_t bytes);
    void release_storage(uint64_t bytes);

    /// Channel access for simulation.
    ResourcePool& channel_pool() { return *channels_; }
    BoundedQueue& request_queue() { return *request_queue_; }

private:
    ResourceId id_;
    HBFConfig config_;
    std::unique_ptr<ResourcePool> channels_;
    std::unique_ptr<BoundedQueue> request_queue_;
    std::unique_ptr<Arbiter> channel_arbiter_;
    uint32_t outstanding_ = 0;
    uint64_t used_bytes_ = 0;
};

} // namespace atmos_sim
