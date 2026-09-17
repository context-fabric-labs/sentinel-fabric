#include "atmos_sim/topology/root_complex.h"

namespace atmos_sim {

RootComplex::RootComplex(uint64_t id, const RootComplexConfig& config)
    : id_(id), config_(config) {}

void RootComplex::add_downstream(uint64_t node_id) {
    downstream_.push_back(node_id);
}

SimTime RootComplex::transfer_time(uint64_t payload_bytes, uint32_t active_endpoints) const {
    uint64_t effective_bw = config_.aggregate_bandwidth_bytes_sec;
    if (active_endpoints > 1) {
        // Bandwidth shared among active endpoints
        effective_bw /= active_endpoints;
    }
    if (effective_bw == 0) return SIM_TIME_MAX;
    return transfer_time_bytes(payload_bytes, effective_bw);
}

} // namespace atmos_sim
