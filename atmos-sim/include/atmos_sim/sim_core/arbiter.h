#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace atmos_sim {

enum class ArbiterPolicy : uint8_t {
    ROUND_ROBIN,
    WEIGHTED,
    PRIORITY,
    FIRST_AVAILABLE,
};

/// Arbiter selects among multiple candidates based on policy.
/// Used for memory channel selection, DMA engine selection, PCIe port selection.
class Arbiter {
public:
    Arbiter(ArbiterPolicy policy, uint32_t num_candidates);

    ArbiterPolicy policy() const { return policy_; }
    uint32_t num_candidates() const { return num_candidates_; }

    /// Select next candidate index.
    uint32_t select();

    /// Select with priority (for PRIORITY policy).
    uint32_t select_with_priority(const std::vector<uint32_t>& priorities);

    /// Select weighted (for WEIGHTED policy).
    uint32_t select_weighted(const std::vector<double>& weights);

    /// Reset arbiter state.
    void reset();

private:
    ArbiterPolicy policy_;
    uint32_t num_candidates_;
    uint32_t robin_index_ = 0;
    uint64_t total_selections_ = 0;
};

} // namespace atmos_sim
