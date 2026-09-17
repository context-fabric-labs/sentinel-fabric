#include "atmos_sim/sim_core/resource_pool.h"

namespace atmos_sim {

ResourcePool::ResourcePool(const std::string& name, uint32_t total_capacity)
    : name_(name), total_capacity_(total_capacity) {}

bool ResourcePool::acquire() {
    if (occupied_ >= total_capacity_) return false;
    ++occupied_;
    return true;
}

bool ResourcePool::release() {
    if (occupied_ == 0) return false;
    --occupied_;
    return true;
}

double ResourcePool::utilization() const {
    if (total_capacity_ == 0) return 0.0;
    return static_cast<double>(occupied_) / static_cast<double>(total_capacity_);
}

} // namespace atmos_sim
