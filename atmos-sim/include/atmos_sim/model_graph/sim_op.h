#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/dependency_tracker.h"
#include <cstdint>
#include <vector>
#include <string>

namespace atmos_sim {

/// Memory location identifiers for simulation.
enum class MemoryLocation : uint8_t {
    HBF,
    LPDDR,
    NPU_SRAM,
    HOST_MEMORY,
};

/// Compute requirement for a simulation operation.
struct ComputeRequirement {
    uint64_t flops = 0;
    ComputeClass compute_class = ComputeClass::OTHER;
    uint32_t required_tensor_engines = 1;
    uint32_t required_vector_engines = 0;
    uint64_t sram_bytes = 0;
};

/// Memory access descriptor.
struct MemoryAccess {
    MemoryLocation location = MemoryLocation::HBF;
    uint64_t address = 0;
    uint64_t size_bytes = 0;
    bool is_read = true;
};

/// A single operation in the simulation IR.
struct SimOp {
    OpId id = 0;
    std::string name;
    ComputeRequirement compute;
    std::vector<MemoryAccess> reads;
    std::vector<MemoryAccess> writes;
    std::vector<OpId> dependencies;
    uint32_t target_module = 0;  // Which ATMOS module this op targets
};

} // namespace atmos_sim
