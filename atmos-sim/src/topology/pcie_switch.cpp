#include "atmos_sim/topology/pcie_switch.h"

namespace atmos_sim {

PCIeSwitch::PCIeSwitch(uint64_t id, const PCIeSwitchConfig& config)
    : id_(id), config_(config) {}

void PCIeSwitch::add_downstream_port(uint64_t node_id) {
    downstream_ports_.push_back(node_id);
}

bool PCIeSwitch::can_route() const {
    return downstream_ports_.size() < config_.port_count;
}

SimTime PCIeSwitch::transit_latency(uint32_t active_ports) const {
    SimTime base = ns_to_sim(config_.switch_latency_ns);
    // Contention: more active ports → more arbitration delay
    if (active_ports > 1) {
        base += ns_to_sim(config_.switch_latency_ns / 2) * (active_ports - 1);
    }
    return base;
}

} // namespace atmos_sim
