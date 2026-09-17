#include "atmos_sim/sim_core/dependency_tracker.h"
#include <algorithm>
#include <stack>

namespace atmos_sim {

DependencyTracker::DependencyTracker() = default;

void DependencyTracker::register_op(OpId op_id, const std::vector<OpId>& dependencies) {
    OpInfo info;
    info.deps = dependencies;
    info.unsatisfied_count = static_cast<uint32_t>(dependencies.size());
    info.state = (info.unsatisfied_count == 0) ? DependencyState::READY : DependencyState::PENDING;
    ops_[op_id] = std::move(info);

    // Build reverse edges
    for (OpId dep : dependencies) {
        dependents_[dep].push_back(op_id);
    }
}

std::vector<OpId> DependencyTracker::satisfy(OpId op_id) {
    auto it = ops_.find(op_id);
    if (it == ops_.end()) return {};

    it->second.state = DependencyState::SATISFIED;

    // Check all dependents
    std::vector<OpId> newly_ready;
    auto dep_it = dependents_.find(op_id);
    if (dep_it != dependents_.end()) {
        for (OpId dependent : dep_it->second) {
            auto d_it = ops_.find(dependent);
            if (d_it != ops_.end() && d_it->second.state == DependencyState::PENDING) {
                --d_it->second.unsatisfied_count;
                if (d_it->second.unsatisfied_count == 0) {
                    d_it->second.state = DependencyState::READY;
                    newly_ready.push_back(dependent);
                }
            }
        }
    }
    return newly_ready;
}

bool DependencyTracker::is_ready(OpId op_id) const {
    auto it = ops_.find(op_id);
    return it != ops_.end() && it->second.state == DependencyState::READY;
}

bool DependencyTracker::is_satisfied(OpId op_id) const {
    auto it = ops_.find(op_id);
    return it != ops_.end() && it->second.state == DependencyState::SATISFIED;
}

DependencyState DependencyTracker::state(OpId op_id) const {
    auto it = ops_.find(op_id);
    if (it == ops_.end()) return DependencyState::PENDING;
    return it->second.state;
}

const std::vector<OpId>& DependencyTracker::dependencies(OpId op_id) const {
    static const std::vector<OpId> empty;
    auto it = ops_.find(op_id);
    if (it == ops_.end()) return empty;
    return it->second.deps;
}

std::vector<OpId> DependencyTracker::dependents(OpId op_id) const {
    auto it = dependents_.find(op_id);
    if (it == dependents_.end()) return {};
    return it->second;
}

bool DependencyTracker::validate_dag() const {
    std::unordered_set<OpId> visited;
    std::unordered_set<OpId> in_stack;

    for (const auto& [op_id, _] : ops_) {
        if (!has_cycle_dfs(op_id, visited, in_stack)) {
            return false;  // Cycle detected
        }
    }
    return true;  // No cycles
}

bool DependencyTracker::has_cycle_dfs(OpId node,
                                       std::unordered_set<OpId>& visited,
                                       std::unordered_set<OpId>& in_stack) const {
    if (in_stack.count(node)) return false;  // Cycle
    if (visited.count(node)) return true;     // Already checked

    visited.insert(node);
    in_stack.insert(node);

    auto it = dependents_.find(node);
    if (it != dependents_.end()) {
        for (OpId dep : it->second) {
            if (!has_cycle_dfs(dep, visited, in_stack)) return false;
        }
    }

    in_stack.erase(node);
    return true;
}

void DependencyTracker::reset() {
    ops_.clear();
    dependents_.clear();
}

} // namespace atmos_sim
