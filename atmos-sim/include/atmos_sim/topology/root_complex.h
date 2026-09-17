#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include <cstdint>
#include <string>
#include <vector>

namespace atmos_sim {

/// Configuration for a root complex.
struct RootComplexConfig {
    std::string name = "RootComplex";
    uint64_t aggregate_bandwidth_bytes_sec = 64ULL * 1000 * 1000 * 1000;  // 64 GB/s
    uint32_t max_endpoints = 16;
    uint64_t host_memory_bandwidth_bytes_sec = 100ULL * 1000 * 1000 * 1000;  // 100 GB/s
};

/// Root Complex — models the CPU-side PCIe root with aggregate bandwidth limits.
class RootComplex {
public:
    explicit RootComplex(uint64_t id, const RootComplexConfig& config);

    uint64_t id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const RootComplexConfig& config() const { return config_; }

    /// Add a downstream switch or endpoint.
    void add_downstream(uint64_t node_id);
    const std::vector<uint64_t>& downstream_nodes() const { return downstream_; }

    /// Calculate transfer time given aggregate load.
    SimTime transfer_time(uint64_t payload_bytes, uint32_t active_endpoints) const;

    /// Aggregate bandwidth.
    uint64_t aggregate_bandwidth() const { return config_.aggregate_bandwidth_bytes_sec; }
    uint64_t host_memory_bandwidth() const { return config_.host_memory_bandwidth_bytes_sec; }

    double utilization() const { return utilization_; }
    void set_utilization(double u) { utilization_ = u; }

private:
    uint64_t id_;
    RootComplexConfig config_;
    std::vector<uint64_t> downstream_;
    double utilization_ = 0.0;
};

} // namespace atmos_sim
