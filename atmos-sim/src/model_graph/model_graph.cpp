#include "atmos_sim/model_graph/model_graph.h"
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace atmos_sim {

ModelGraph::ModelGraph() = default;

OpId ModelGraph::add_op(const SimOp& op) {
    SimOp copy = op;
    if (copy.id == 0) {
        copy.id = next_id_++;
    } else {
        if (copy.id >= next_id_) next_id_ = copy.id + 1;
    }
    id_to_index_[copy.id] = ops_.size();
    ops_.push_back(std::move(copy));
    return ops_.back().id;
}

const SimOp& ModelGraph::get_op(OpId id) const {
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) {
        throw std::runtime_error("ModelGraph: op not found");
    }
    return ops_[it->second];
}

std::vector<OpId> ModelGraph::root_ops() const {
    std::vector<OpId> roots;
    for (const auto& op : ops_) {
        if (op.dependencies.empty()) {
            roots.push_back(op.id);
        }
    }
    return roots;
}

std::vector<OpId> ModelGraph::leaf_ops() const {
    // Find ops that no other op depends on
    std::unordered_set<OpId> all_deps;
    for (const auto& op : ops_) {
        for (OpId dep : op.dependencies) {
            all_deps.insert(dep);
        }
    }
    std::vector<OpId> leaves;
    for (const auto& op : ops_) {
        if (!all_deps.count(op.id)) {
            leaves.push_back(op.id);
        }
    }
    return leaves;
}

std::string ModelGraph::to_json() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"model\": \"" << meta_.name << "\",\n";
    oss << "  \"num_layers\": " << meta_.num_layers << ",\n";
    oss << "  \"ops\": [\n";
    for (size_t i = 0; i < ops_.size(); ++i) {
        const auto& op = ops_[i];
        oss << "    {\"id\": " << op.id
            << ", \"name\": \"" << op.name << "\""
            << ", \"flops\": " << op.compute.flops
            << ", \"deps\": [";
        for (size_t j = 0; j < op.dependencies.size(); ++j) {
            if (j > 0) oss << ", ";
            oss << op.dependencies[j];
        }
        oss << "]}";
        if (i + 1 < ops_.size()) oss << ",";
        oss << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

ModelGraph ModelGraph::from_json(const std::string& /*json*/) {
    // Placeholder for JSON deserialization
    // Full implementation would use a JSON library
    return ModelGraph();
}

} // namespace atmos_sim
