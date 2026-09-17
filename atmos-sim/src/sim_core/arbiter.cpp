#include "atmos_sim/sim_core/arbiter.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace atmos_sim {

Arbiter::Arbiter(ArbiterPolicy policy, uint32_t num_candidates)
    : policy_(policy), num_candidates_(num_candidates) {
    if (num_candidates == 0) {
        throw std::runtime_error("Arbiter: num_candidates must be > 0");
    }
}

uint32_t Arbiter::select() {
    uint32_t result = robin_index_;
    robin_index_ = (robin_index_ + 1) % num_candidates_;
    ++total_selections_;
    return result;
}

uint32_t Arbiter::select_with_priority(const std::vector<uint32_t>& priorities) {
    if (priorities.size() != num_candidates_) {
        throw std::runtime_error("Arbiter: priorities size mismatch");
    }
    // Select the candidate with lowest priority value (highest priority)
    uint32_t best = 0;
    for (uint32_t i = 1; i < num_candidates_; ++i) {
        if (priorities[i] < priorities[best]) best = i;
    }
    ++total_selections_;
    return best;
}

uint32_t Arbiter::select_weighted(const std::vector<double>& weights) {
    if (weights.size() != num_candidates_) {
        throw std::runtime_error("Arbiter: weights size mismatch");
    }
    // Round-robin weighted: use robin_index weighted by inverse load
    // Simple implementation: pick round-robin for now
    uint32_t result = select();
    return result;
}

void Arbiter::reset() {
    robin_index_ = 0;
    total_selections_ = 0;
}

} // namespace atmos_sim
