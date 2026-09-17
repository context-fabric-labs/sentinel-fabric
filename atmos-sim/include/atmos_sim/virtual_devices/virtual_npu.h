#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/bounded_queue.h"
#include "atmos_sim/sim_core/resource_pool.h"
#include "atmos_sim/sim_core/arbiter.h"
#include <memory>
#include <vector>
#include <string>

namespace atmos_sim {

/// A compute operation to be executed on the NPU.
struct ComputeOp {
    OpId id = 0;
    ComputeClass compute_class = ComputeClass::OTHER;
    uint64_t flops = 0;
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;
    std::vector<OpId> input_deps;
    std::vector<OpId> output_deps;
    ResourceId target_module = INVALID_RESOURCE;
};

/// Configuration for a VirtualNPU.
struct VirtualNPUConfig {
    std::string name = "NPU";
    uint32_t command_queue_depth = 64;
    uint32_t tensor_engine_count = 4;
    uint32_t vector_engine_count = 4;
    uint32_t load_store_engine_count = 2;
    uint64_t local_sram_bytes = 256 * 1024 * 1024;  // 256 MB
    uint32_t execution_slots = 8;
    uint64_t peak_flops = 500'000'000'000ULL;  // 500 GFLOPS
};

/// Virtual NPU — models finite compute resources with queuing.
class VirtualNPU {
public:
    explicit VirtualNPU(ResourceId id, const VirtualNPUConfig& config);

    ResourceId id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const VirtualNPUConfig& config() const { return config_; }

    /// Check if the NPU can accept a new compute op.
    bool can_accept() const;

    /// Submit a compute op. Returns estimated compute time (actual time emerges from simulation).
    SimTime estimate_compute_time(const ComputeOp& op) const;

    /// Get resource pools for external tracking.
    ResourcePool& command_queue() { return *command_queue_; }
    ResourcePool& tensor_engines() { return *tensor_engines_; }
    ResourcePool& vector_engines() { return *vector_engines_; }
    ResourcePool& load_store_engines() { return *load_store_engines_; }
    ResourcePool& execution_slots_pool() { return *execution_slots_; }

    uint64_t available_sram() const { return config_.local_sram_bytes - sram_used_; }
    bool allocate_sram(uint64_t bytes);
    void release_sram(uint64_t bytes);

    double utilization() const;

private:
    ResourceId id_;
    VirtualNPUConfig config_;
    std::unique_ptr<ResourcePool> command_queue_;
    std::unique_ptr<ResourcePool> tensor_engines_;
    std::unique_ptr<ResourcePool> vector_engines_;
    std::unique_ptr<ResourcePool> load_store_engines_;
    std::unique_ptr<ResourcePool> execution_slots_;
    uint64_t sram_used_ = 0;
};

} // namespace atmos_sim
