#include "atmos_sim/virtual_devices/virtual_npu.h"

namespace atmos_sim {

VirtualNPU::VirtualNPU(ResourceId id, const VirtualNPUConfig& config)
    : id_(id), config_(config) {
    command_queue_ = std::make_unique<ResourcePool>("cmd_queue", config.command_queue_depth);
    tensor_engines_ = std::make_unique<ResourcePool>("tensor_engines", config.tensor_engine_count);
    vector_engines_ = std::make_unique<ResourcePool>("vector_engines", config.vector_engine_count);
    load_store_engines_ = std::make_unique<ResourcePool>("ls_engines", config.load_store_engine_count);
    execution_slots_ = std::make_unique<ResourcePool>("exec_slots", config.execution_slots);
}

bool VirtualNPU::can_accept() const {
    return command_queue_->is_available() && execution_slots_->is_available();
}

SimTime VirtualNPU::estimate_compute_time(const ComputeOp& op) const {
    if (config_.peak_flops == 0) return 0;
    // time_ns = (flops * 1e9) / peak_flops
    // Use 128-bit intermediate to avoid overflow.
    __uint128_t num = static_cast<__uint128_t>(op.flops) * 1'000'000'000ULL;
    uint64_t time_ns = static_cast<uint64_t>(num / config_.peak_flops);
    return std::max(time_ns, static_cast<uint64_t>(1));
}

bool VirtualNPU::allocate_sram(uint64_t bytes) {
    if (sram_used_ + bytes > config_.local_sram_bytes) return false;
    sram_used_ += bytes;
    return true;
}

void VirtualNPU::release_sram(uint64_t bytes) {
    if (bytes > sram_used_) {
        sram_used_ = 0;
    } else {
        sram_used_ -= bytes;
    }
}

double VirtualNPU::utilization() const {
    // Weighted average of resource pool utilizations
    double total = 0.0;
    int count = 0;
    total += command_queue_->utilization(); ++count;
    total += tensor_engines_->utilization(); ++count;
    total += execution_slots_->utilization(); ++count;
    return count > 0 ? total / count : 0.0;
}

} // namespace atmos_sim
