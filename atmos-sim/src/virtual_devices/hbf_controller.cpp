#include "atmos_sim/virtual_devices/hbf_controller.h"

namespace atmos_sim {

HBFController::HBFController(ResourceId id, const HBFConfig& config)
    : id_(id), config_(config) {
    channels_ = std::make_unique<ResourcePool>("hbf_channels", config.channel_count);
    request_queue_ = std::make_unique<BoundedQueue>("hbf_rq", config.queue_depth_per_channel);
    channel_arbiter_ = std::make_unique<Arbiter>(config.arbitration, config.channel_count);
}

bool HBFController::can_accept() const {
    return outstanding_ < config_.max_outstanding && request_queue_->available_slots() > 0;
}

SimTime HBFController::submit_read(uint64_t address, uint64_t size_bytes, OpId requestor) {
    if (!can_accept()) return SIM_TIME_MAX;

    uint32_t channel = channel_arbiter_->select();
    ++outstanding_;

    // Transfer time = protocol_overhead + bytes / bandwidth_per_channel
    SimTime xfer_time = ns_to_sim(config_.protocol_overhead_ns) +
                        transfer_time_bytes(size_bytes, config_.bandwidth_per_channel_bytes_sec);
    return xfer_time;
}

SimTime HBFController::submit_write(uint64_t address, uint64_t size_bytes, OpId requestor) {
    if (!can_accept()) return SIM_TIME_MAX;

    uint32_t channel = channel_arbiter_->select();
    ++outstanding_;

    SimTime xfer_time = ns_to_sim(config_.protocol_overhead_ns) +
                        transfer_time_bytes(size_bytes, config_.bandwidth_per_channel_bytes_sec);
    return xfer_time;
}

uint64_t HBFController::total_bandwidth() const {
    return config_.bandwidth_per_channel_bytes_sec * config_.channel_count;
}

double HBFController::utilization() const {
    if (config_.max_outstanding == 0) return 0.0;
    return static_cast<double>(outstanding_) / config_.max_outstanding;
}

bool HBFController::allocate_storage(uint64_t bytes) {
    if (used_bytes_ + bytes > config_.capacity_bytes) return false;
    used_bytes_ += bytes;
    return true;
}

void HBFController::release_storage(uint64_t bytes) {
    if (bytes > used_bytes_) used_bytes_ = 0;
    else used_bytes_ -= bytes;
}

} // namespace atmos_sim
