#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/arbiter.h"
#include <cstdint>
#include <string>
#include <vector>

namespace atmos_sim {

enum class PCIeGen : uint8_t {
    GEN3 = 3,
    GEN4 = 4,
    GEN5 = 5,
};

/// Per-lane bandwidth in bytes/sec for each PCIe generation.
constexpr uint64_t pcie_lane_bandwidth(PCIeGen gen) {
    switch (gen) {
        case PCIeGen::GEN3: return 984'600'000ULL;
        case PCIeGen::GEN4: return 1'969'000'000ULL;
        case PCIeGen::GEN5: return 3'938'000'000ULL;
    }
    return 0;
}

/// Configuration for a PCIe link.
struct PCIeLinkConfig {
    std::string name = "PCIeLink";
    uint32_t lane_count = 8;
    uint32_t generation = 4;
    uint64_t bandwidth_bytes_sec = 0;  // 0 = auto-calculate from gen × lanes
    uint32_t max_outstanding = 32;
    uint32_t queue_depth = 16;
    ArbiterPolicy arbitration = ArbiterPolicy::ROUND_ROBIN;
    uint64_t latency_ns = 100;  // Base link latency
};

/// A PCIe link connecting two topology nodes.
class PCIeLink {
public:
    explicit PCIeLink(uint64_t id, const PCIeLinkConfig& config);

    uint64_t id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const PCIeLinkConfig& config() const { return config_; }

    /// Effective bandwidth (auto-calculated if config.bandwidth_bytes_sec == 0).
    uint64_t effective_bandwidth() const;

    /// Transfer time for a given payload.
    SimTime transfer_time(uint64_t payload_bytes) const;

    /// Current utilization.
    double utilization() const { return utilization_; }
    void set_utilization(double u) { utilization_ = u; }

    /// Endpoints of this link (topology node IDs).
    uint64_t endpoint_a() const { return endpoint_a_; }
    uint64_t endpoint_b() const { return endpoint_b_; }
    void set_endpoints(uint64_t a, uint64_t b) { endpoint_a_ = a; endpoint_b_ = b; }

private:
    uint64_t id_;
    PCIeLinkConfig config_;
    uint64_t bandwidth_;  // Actual computed bandwidth
    double utilization_ = 0.0;
    uint64_t endpoint_a_ = 0;
    uint64_t endpoint_b_ = 0;
};

} // namespace atmos_sim
