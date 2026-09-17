#include "atmos_sim/sim_core/resource.h"

namespace atmos_sim {

Resource::Resource(ResourceId id, const std::string& name, uint32_t capacity)
    : id_(id), name_(name), capacity_(capacity) {}

bool Resource::acquire() {
    if (occupancy_ >= capacity_) return false;
    ++occupancy_;
    if (occupancy_ >= capacity_) state_ = ResourceState::BUSY;
    return true;
}

bool Resource::release() {
    if (occupancy_ == 0) return false;
    --occupancy_;
    if (occupancy_ < capacity_) state_ = ResourceState::IDLE;
    return true;
}

void Resource::set_state(ResourceState state) {
    state_ = state;
}

void Resource::update_time(SimTime current_time) {
    if (current_time <= last_update_time_) return;
    SimTime delta = current_time - last_update_time_;
    if (occupancy_ > 0) {
        busy_time_ += delta;
    } else {
        idle_time_ += delta;
    }
    last_update_time_ = current_time;
}

} // namespace atmos_sim
