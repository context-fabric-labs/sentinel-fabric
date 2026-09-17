#pragma once

#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/event.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

namespace atmos_sim {

/// A pool of identical resources with a fixed total capacity.
/// Acquire/release emit events for tracing.
class ResourcePool {
public:
    ResourcePool(const std::string& name, uint32_t total_capacity);

    const std::string& name() const { return name_; }
    uint32_t total_capacity() const { return total_capacity_; }
    uint32_t available() const { return total_capacity_ - occupied_; }
    uint32_t occupied() const { return occupied_; }
    bool is_available() const { return occupied_ < total_capacity_; }

    /// Try to acquire one unit. Returns true if successful.
    bool acquire();

    /// Release one unit. Returns true if was occupied.
    bool release();

    /// Utilization ratio [0.0, 1.0].
    double utilization() const;

private:
    std::string name_;
    uint32_t total_capacity_;
    uint32_t occupied_ = 0;
};

} // namespace atmos_sim
