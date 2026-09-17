#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/bounded_queue.h"
#include "atmos_sim/sim_core/arbiter.h"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace atmos_sim {

/// Configuration for a PCIe switch.
struct PCIeSwitchConfig {
    std::string name = "PCIeSwitch";
    uint32_t port_count = 8;
    uint32_t ingress_queue_depth = 32;
    uint64_t switch_latency_ns = 200;
    ArbiterPolicy uplink_arbitration = ArbiterPolicy::ROUND_ROBIN;
};

/// A PCIe switch with multiple downstream ports and a shared uplink.
class PCIeSwitch {
public:
    explicit PCIeSwitch(uint64_t id, const PCIeSwitchConfig& config);

    uint64_t id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const PCIeSwitchConfig& config() const { return config_; }

    /// Add a downstream port (topology node ID).
    void add_downstream_port(uint64_t node_id);

    /// Set the upstream port (toward root complex).
    void set_upstream_port(uint64_t node_id) { upstream_port_ = node_id; }

    /// Downstream port IDs.
    const std::vector<uint64_t>& downstream_ports() const { return downstream_ports_; }
    uint64_t upstream_port() const { return upstream_port_; }

    /// Check if a transfer through this switch can proceed.
    bool can_route() const;

    /// Calculate transit latency including contention.
    SimTime transit_latency(uint32_t active_ports) const;

    /// Utilization of the uplink.
    double uplink_utilization() const { return uplink_utilization_; }
    void set_uplink_utilization(double u) { uplink_utilization_ = u; }

private:
    uint64_t id_;
    PCIeSwitchConfig config_;
    std::vector<uint64_t> downstream_ports_;
    uint64_t upstream_port_ = 0;
    double uplink_utilization_ = 0.0;
};

} // namespace atmos_sim
