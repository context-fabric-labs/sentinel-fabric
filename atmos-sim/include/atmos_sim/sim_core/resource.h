#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/event.h"
#include <string>
#include <cstdint>
#include <vector>

namespace atmos_sim {

enum class ResourceState : uint8_t {
    IDLE,
    BUSY,
    BLOCKED,
    FAILED,
};

/// A finite-capacity resource in the simulation (NPU engine, memory channel, etc.).
class Resource {
public:
    Resource(ResourceId id, const std::string& name, uint32_t capacity);

    ResourceId id() const { return id_; }
    const std::string& name() const { return name_; }
    uint32_t capacity() const { return capacity_; }
    uint32_t occupancy() const { return occupancy_; }
    uint32_t available() const { return capacity_ - occupancy_; }
    ResourceState state() const { return state_; }
    bool is_available() const { return occupancy_ < capacity_; }

    /// Acquire one unit. Returns true if successful, false if at capacity.
    bool acquire();

    /// Release one unit. Returns true if was occupied.
    bool release();

    /// Set resource state.
    void set_state(ResourceState state);

    /// Total time the resource has been busy (for utilization tracking).
    SimTime busy_time() const { return busy_time_; }
    SimTime idle_time() const { return idle_time_; }

    /// Update time tracking (called by engine on each event).
    void update_time(SimTime current_time);

private:
    ResourceId id_;
    std::string name_;
    uint32_t capacity_;
    uint32_t occupancy_ = 0;
    ResourceState state_ = ResourceState::IDLE;

    SimTime last_update_time_ = 0;
    SimTime busy_time_ = 0;
    SimTime idle_time_ = 0;
};

} // namespace atmos_sim
