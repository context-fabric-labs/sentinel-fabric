#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/event.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace atmos_sim {

enum class DependencyState : uint8_t {
    PENDING,      // Not all dependencies satisfied
    READY,        // All dependencies satisfied, ready to execute
    SATISFIED,    // Already processed
};

// OpId is now defined in sim_time.h

/// Tracks dependencies between operations in a DAG.
/// When all dependencies of an operation are satisfied, it becomes READY.
class DependencyTracker {
public:
    DependencyTracker();

    /// Register an operation with its dependencies.
    void register_op(OpId op_id, const std::vector<OpId>& dependencies);

    /// Mark an operation as satisfied. Returns list of ops that became READY.
    std::vector<OpId> satisfy(OpId op_id);

    /// Check if an operation is READY (all dependencies satisfied).
    bool is_ready(OpId op_id) const;

    /// Check if an operation is SATISFIED (already processed).
    bool is_satisfied(OpId op_id) const;

    /// Get dependency state.
    DependencyState state(OpId op_id) const;

    /// Get dependencies of an operation.
    const std::vector<OpId>& dependencies(OpId op_id) const;

    /// Get operations that depend on the given op.
    std::vector<OpId> dependents(OpId op_id) const;

    /// Check if the graph has cycles. Returns true if acyclic (valid).
    bool validate_dag() const;

    /// Reset all state.
    void reset();

    /// Number of registered operations.
    size_t op_count() const { return ops_.size(); }

private:
    struct OpInfo {
        std::vector<OpId> deps;
        DependencyState state = DependencyState::PENDING;
        uint32_t unsatisfied_count = 0;
    };

    std::unordered_map<OpId, OpInfo> ops_;
    std::unordered_map<OpId, std::vector<OpId>> dependents_; // reverse edges

    bool has_cycle_dfs(OpId node,
                       std::unordered_set<OpId>& visited,
                       std::unordered_set<OpId>& in_stack) const;
};

} // namespace atmos_sim
