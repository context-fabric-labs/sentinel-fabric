#include "atmos_sim/topology/pcie_link.h"

namespace atmos_sim {

PCIeLink::PCIeLink(uint64_t id, const PCIeLinkConfig& config)
    : id_(id), config_(config) {
    if (config.bandwidth_bytes_sec > 0) {
        bandwidth_ = config.bandwidth_bytes_sec;
    } else {
        bandwidth_ = pcie_lane_bandwidth(static_cast<PCIeGen>(config.generation)) * config.lane_count;
    }
}

uint64_t PCIeLink::effective_bandwidth() const {
    return bandwidth_;
}

SimTime PCIeLink::transfer_time(uint64_t payload_bytes) const {
    if (bandwidth_ == 0) return SIM_TIME_MAX;
    SimTime base = transfer_time_bytes(payload_bytes, bandwidth_);
    return base + ns_to_sim(config_.latency_ns);
}

} // namespace atmos_sim
