#pragma once

#include "atmos_sim/model_graph/sim_op.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace atmos_sim {

/// Model metadata.
struct ModelMeta {
    std::string name;
    uint32_t num_layers = 0;
    uint32_t hidden_size = 0;
    uint32_t num_heads = 0;
    uint32_t num_kv_heads = 0;
    uint32_t ffn_size = 0;
    uint32_t vocab_size = 0;
    uint64_t total_params = 0;
    uint32_t bytes_per_element = 2;  // FP16 default
};

/// A model graph — DAG of SimOps representing the computation of one request.
class ModelGraph {
public:
    ModelGraph();

    /// Add an operation to the graph. Returns the assigned OpId.
    OpId add_op(const SimOp& op);

    /// Get an operation by ID.
    const SimOp& get_op(OpId id) const;

    /// All operations in topological order.
    const std::vector<SimOp>& ops() const { return ops_; }

    /// Number of operations.
    size_t op_count() const { return ops_.size(); }

    /// Get root ops (no dependencies).
    std::vector<OpId> root_ops() const;

    /// Get leaf ops (no dependents).
    std::vector<OpId> leaf_ops() const;

    /// Model metadata.
    ModelMeta& meta() { return meta_; }
    const ModelMeta& meta() const { return meta_; }

    /// Serialize to JSON string.
    std::string to_json() const;

    /// Deserialize from JSON string.
    static ModelGraph from_json(const std::string& json);

private:
    std::vector<SimOp> ops_;
    std::unordered_map<OpId, size_t> id_to_index_;
    ModelMeta meta_;
    OpId next_id_ = 1;
};

} // namespace atmos_sim
