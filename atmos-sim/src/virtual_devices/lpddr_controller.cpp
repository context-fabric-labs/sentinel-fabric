#include "atmos_sim/virtual_devices/lpddr_controller.h"

namespace atmos_sim {

LPDDRController::LPDDRController(ResourceId id, const LPDDRConfig& config)
    : id_(id), config_(config) {
    channels_ = std::make_unique<ResourcePool>("lpddr_channels", config.channel_count);
    request_queue_ = std::make_unique<BoundedQueue>("lpddr_rq", config.queue_depth_per_channel);
    channel_arbiter_ = std::make_unique<Arbiter>(config.arbitration, config.channel_count);
}

bool LPDDRController::can_accept() const {
    return outstanding_ < config_.max_outstanding && request_queue_->available_slots() > 0;
}

SimTime LPDDRController::submit_read(uint64_t address, uint64_t size_bytes, OpId requestor) {
    if (!can_accept()) return SIM_TIME_MAX;

    uint32_t channel = channel_arbiter_->select();
    ++outstanding_;

    SimTime base = transfer_time_bytes(size_bytes, config_.bandwidth_per_channel_bytes_sec);

    // Add turnaround penalty if last access was a write
    SimTime turnaround = 0;
    if (!last_was_read_) {
        turnaround = ns_to_sim(config_.read_write_turnaround_ns);
    }
    last_was_read_ = true;

    return base + turnaround;
}

SimTime LPDDRController::submit_write(uint64_t address, uint64_t size_bytes, OpId requestor) {
    if (!can_accept()) return SIM_TIME_MAX;

    uint32_t channel = channel_arbiter_->select();
    ++outstanding_;

    SimTime base = transfer_time_bytes(size_bytes, config_.bandwidth_per_channel_bytes_sec);

    SimTime turnaround = 0;
    if (last_was_read_) {
        turnaround = ns_to_sim(config_.read_write_turnaround_ns);
    }
    last_was_read_ = false;

    return base + turnaround;
}

uint64_t LPDDRController::total_bandwidth() const {
    return config_.bandwidth_per_channel_bytes_sec * config_.channel_count;
}

double LPDDRController::utilization() const {
    if (config_.max_outstanding == 0) return 0.0;
    return static_cast<double>(outstanding_) / config_.max_outstanding;
}

bool LPDDRController::allocate_storage(uint64_t bytes) {
    if (used_bytes_ + bytes > config_.capacity_bytes) return false;
    used_bytes_ += bytes;
    return true;
}

void LPDDRController::release_storage(uint64_t bytes) {
    if (bytes > used_bytes_) used_bytes_ = 0;
    else used_bytes_ -= bytes;
}

} // namespace atmos_sim
