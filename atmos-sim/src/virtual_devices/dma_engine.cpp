#include "atmos_sim/virtual_devices/dma_engine.h"

namespace atmos_sim {

DMAEngine::DMAEngine(ResourceId id, const DMAConfig& config)
    : id_(id), config_(config) {
    descriptor_queue_ = std::make_unique<BoundedQueue>("dma_desc", config.descriptor_queue_depth);
    channels_ = std::make_unique<ResourcePool>("dma_channels", config.channel_count);
}

bool DMAEngine::can_accept() const {
    return outstanding_ < config_.max_outstanding &&
           descriptor_queue_->available_slots() > 0;
}

SimTime DMAEngine::submit_transfer(const DMATransfer& transfer) {
    if (!can_accept()) return SIM_TIME_MAX;

    ++outstanding_;

    // Setup overhead + transfer time
    SimTime setup = ns_to_sim(config_.setup_overhead_ns);
    SimTime xfer = transfer_time_bytes(transfer.size_bytes, config_.bandwidth_bytes_sec);
    return setup + xfer;
}

double DMAEngine::utilization() const {
    if (config_.max_outstanding == 0) return 0.0;
    return static_cast<double>(outstanding_) / config_.max_outstanding;
}

} // namespace atmos_sim
