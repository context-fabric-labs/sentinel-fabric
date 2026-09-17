#include "atmos_sim/virtual_devices/pcie_endpoint.h"

namespace atmos_sim {

PCIeEndpoint::PCIeEndpoint(ResourceId id, const PCIeEndpointConfig& config)
    : id_(id), config_(config) {}

uint64_t PCIeEndpoint::effective_bandwidth() const {
    return pcie_lane_bandwidth(config_.gen) * config_.lane_count;
}

bool PCIeEndpoint::can_accept() const {
    return credits_used_ < config_.credit_limit;
}

SimTime PCIeEndpoint::transfer_time(uint64_t payload_bytes) const {
    uint64_t bw = effective_bandwidth();
    if (bw == 0) return SIM_TIME_MAX;

    // Packetization: number of TLP packets needed
    uint64_t num_packets = (payload_bytes + config_.max_payload_size - 1) / config_.max_payload_size;
    SimTime packet_overhead = ns_to_sim(config_.packet_overhead_ns) * num_packets;

    // Base transfer time
    SimTime xfer = transfer_time_bytes(payload_bytes, bw);
    return packet_overhead + xfer;
}

bool PCIeEndpoint::consume_credits(uint32_t count) {
    if (credits_used_ + count > config_.credit_limit) return false;
    credits_used_ += count;
    return true;
}

void PCIeEndpoint::return_credits(uint32_t count) {
    if (count > credits_used_) credits_used_ = 0;
    else credits_used_ -= count;
}

double PCIeEndpoint::utilization() const {
    if (config_.credit_limit == 0) return 0.0;
    return static_cast<double>(credits_used_) / config_.credit_limit;
}

} // namespace atmos_sim
