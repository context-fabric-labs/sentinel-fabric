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

/// Configuration for LPDDR controller.
struct LPDDRConfig {
    std::string name = "LPDDR";
    uint32_t channel_count = 4;
    uint64_t capacity_bytes = 32ULL * 1024 * 1024 * 1024;  // 32 GB
    uint64_t bandwidth_per_channel_bytes_sec = 6ULL * 1000 * 1000 * 1000;  // 6 GB/s
    uint32_t queue_depth_per_channel = 16;
    uint32_t max_outstanding = 32;
    uint64_t read_write_turnaround_ns = 15;  // Read/write direction switch penalty
    uint64_t refresh_interval_ns = 7'800;  // 7.8 us refresh interval
    uint64_t refresh_duration_ns = 150;  // 150 ns refresh duration
    ArbiterPolicy arbitration = ArbiterPolicy::ROUND_ROBIN;
};

/// LPDDR Controller — models LPDDR working memory with channel contention,
/// read/write arbitration, and refresh overhead.
class LPDDRController {
public:
    explicit LPDDRController(ResourceId id, const LPDDRConfig& config);

    ResourceId id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const LPDDRConfig& config() const { return config_; }

    bool can_accept() const;

    SimTime submit_read(uint64_t address, uint64_t size_bytes, OpId requestor);
    SimTime submit_write(uint64_t address, uint64_t size_bytes, OpId requestor);

    uint64_t total_bandwidth() const;
    uint32_t outstanding() const { return outstanding_; }
    double utilization() const;

    uint64_t used_bytes() const { return used_bytes_; }
    bool allocate_storage(uint64_t bytes);
    void release_storage(uint64_t bytes);

    ResourcePool& channel_pool() { return *channels_; }
    BoundedQueue& request_queue() { return *request_queue_; }

private:
    ResourceId id_;
    LPDDRConfig config_;
    std::unique_ptr<ResourcePool> channels_;
    std::unique_ptr<BoundedQueue> request_queue_;
    std::unique_ptr<Arbiter> channel_arbiter_;
    uint32_t outstanding_ = 0;
    uint64_t used_bytes_ = 0;
    bool last_was_read_ = true;
    SimTime next_available_ = 0;
};

} // namespace atmos_sim
