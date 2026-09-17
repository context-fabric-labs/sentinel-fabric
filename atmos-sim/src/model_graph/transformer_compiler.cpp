#include "atmos_sim/model_graph/transformer_compiler.h"
#include <cmath>

namespace atmos_sim {

TransformerCompiler::TransformerCompiler(const TransformerConfig& config)
    : config_(config) {}

ModelGraph TransformerCompiler::compile_layer(uint32_t layer_idx, uint32_t seq_len,
                                               uint32_t kv_cache_len, uint32_t module_count) const {
    ModelGraph graph;
    graph.meta().name = config_.model_name;
    graph.meta().num_layers = 1;
    graph.meta().hidden_size = config_.hidden_size;
    graph.meta().num_heads = config_.num_heads;
    graph.meta().num_kv_heads = config_.num_kv_heads;
    graph.meta().ffn_size = config_.ffn_size;
    graph.meta().bytes_per_element = config_.bytes_per_element;

    uint32_t module_idx = layer_idx % module_count;
    OpId prev_op = 0;
    OpId residual_op = 0;

    // RMSNorm
    add_rmsnorm(graph, layer_idx, config_.hidden_size * config_.bytes_per_element, module_idx, prev_op);
    residual_op = prev_op;

    // QKV Projection
    add_qkv_projection(graph, layer_idx, seq_len, module_idx, prev_op);

    // Attention
    add_attention(graph, layer_idx, seq_len, kv_cache_len, module_idx, prev_op);

    // Output Projection
    add_output_projection(graph, layer_idx, seq_len, module_idx, prev_op);

    // Residual connection
    add_residual(graph, layer_idx, prev_op, residual_op);

    // RMSNorm for MLP
    residual_op = prev_op;
    add_rmsnorm(graph, layer_idx + 1000, config_.hidden_size * config_.bytes_per_element, module_idx, prev_op);

    // MLP
    add_mlp(graph, layer_idx, seq_len, module_idx, prev_op);

    // Residual connection
    add_residual(graph, layer_idx + 1000, prev_op, residual_op);

    return graph;
}

ModelGraph TransformerCompiler::compile_inference(uint32_t prompt_tokens, uint32_t max_output_tokens,
                                                    uint32_t module_count) const {
    ModelGraph full_graph;
    full_graph.meta().name = config_.model_name;
    full_graph.meta().num_layers = config_.num_layers;
    full_graph.meta().hidden_size = config_.hidden_size;
    full_graph.meta().num_heads = config_.num_heads;
    full_graph.meta().num_kv_heads = config_.num_kv_heads;
    full_graph.meta().ffn_size = config_.ffn_size;
    full_graph.meta().bytes_per_element = config_.bytes_per_element;

    // Prefill: process all prompt tokens
    for (uint32_t layer = 0; layer < config_.num_layers; ++layer) {
        ModelGraph layer_graph = compile_layer(layer, prompt_tokens, 0, module_count);
        for (const auto& op : layer_graph.ops()) {
            full_graph.add_op(op);
        }
    }

    // Decode: generate tokens one at a time
    for (uint32_t token = 0; token < max_output_tokens; ++token) {
        uint32_t kv_len = prompt_tokens + token;
        for (uint32_t layer = 0; layer < config_.num_layers; ++layer) {
            ModelGraph layer_graph = compile_layer(layer, 1, kv_len, module_count);
            for (const auto& op : layer_graph.ops()) {
                full_graph.add_op(op);
            }
        }
    }

    return full_graph;
}

ModelGraph TransformerCompiler::compile_prefill(uint32_t prompt_tokens, uint32_t module_count) const {
    ModelGraph full_graph;
    full_graph.meta() = {
        config_.model_name, config_.num_layers, config_.hidden_size,
        config_.num_heads, config_.num_kv_heads, config_.ffn_size,
        config_.vocab_size, 0, config_.bytes_per_element
    };

    for (uint32_t layer = 0; layer < config_.num_layers; ++layer) {
        ModelGraph layer_graph = compile_layer(layer, prompt_tokens, 0, module_count);
        for (const auto& op : layer_graph.ops()) {
            full_graph.add_op(op);
        }
    }
    return full_graph;
}

ModelGraph TransformerCompiler::compile_decode(uint32_t kv_cache_len, uint32_t module_count) const {
    ModelGraph full_graph;
    full_graph.meta() = {
        config_.model_name, config_.num_layers, config_.hidden_size,
        config_.num_heads, config_.num_kv_heads, config_.ffn_size,
        config_.vocab_size, 0, config_.bytes_per_element
    };

    for (uint32_t layer = 0; layer < config_.num_layers; ++layer) {
        ModelGraph layer_graph = compile_layer(layer, 1, kv_cache_len, module_count);
        for (const auto& op : layer_graph.ops()) {
            full_graph.add_op(op);
        }
    }
    return full_graph;
}

void TransformerCompiler::add_rmsnorm(ModelGraph& graph, uint32_t layer_idx,
                                       uint32_t hidden_bytes, uint32_t module_idx,
                                       OpId& prev_op) const {
    SimOp op;
    op.name = "rmsnorm_" + std::to_string(layer_idx);
    op.compute.flops = hidden_bytes / config_.bytes_per_element * 2;  // elementwise ops
    op.compute.compute_class = ComputeClass::ELEMENTWISE;
    op.compute.required_tensor_engines = 0;
    op.compute.required_vector_engines = 1;
    op.reads.push_back({MemoryLocation::HBF, 0, hidden_bytes, true});
    op.writes.push_back({MemoryLocation::NPU_SRAM, 0, hidden_bytes, false});
    op.target_module = module_idx;
    if (prev_op > 0) op.dependencies.push_back(prev_op);
    prev_op = graph.add_op(op);
}

void TransformerCompiler::add_qkv_projection(ModelGraph& graph, uint32_t layer_idx,
                                               uint32_t seq_len, uint32_t module_idx,
                                               OpId& prev_op) const {
    // QKV: 3 matrix multiplications (Q, K, V projections)
    uint64_t hidden = config_.hidden_size;
    uint64_t q_size = hidden;
    uint64_t kv_size = config_.num_kv_heads * (hidden / config_.num_heads);

    SimOp op;
    op.name = "qkv_proj_" + std::to_string(layer_idx);
    op.compute.flops = seq_len * hidden * (q_size + 2 * kv_size) * 2;  // GEMM FLOPs
    op.compute.compute_class = ComputeClass::GEMM;
    op.compute.required_tensor_engines = 2;
    op.compute.sram_bytes = (q_size + 2 * kv_size) * config_.bytes_per_element;
    op.reads.push_back({MemoryLocation::HBF, 0, hidden * config_.bytes_per_element, true});
    op.reads.push_back({MemoryLocation::HBF, 0, (q_size + 2 * kv_size) * hidden * config_.bytes_per_element, true});
    op.writes.push_back({MemoryLocation::NPU_SRAM, 0,
                         (q_size + 2 * kv_size) * seq_len * config_.bytes_per_element, false});
    op.target_module = module_idx;
    op.dependencies.push_back(prev_op);
    prev_op = graph.add_op(op);
}

void TransformerCompiler::add_attention(ModelGraph& graph, uint32_t layer_idx,
                                         uint32_t seq_len, uint32_t kv_cache_len,
                                         uint32_t module_idx, OpId& prev_op) const {
    uint64_t head_dim = config_.hidden_size / config_.num_heads;
    uint64_t kv_total_len = kv_cache_len + seq_len;

    SimOp op;
    op.name = "attention_" + std::to_string(layer_idx);
    op.compute.flops = seq_len * kv_total_len * head_dim * config_.num_heads * 2;
    op.compute.compute_class = ComputeClass::ATTENTION;
    op.compute.required_tensor_engines = 2;
    op.compute.sram_bytes = kv_total_len * head_dim * config_.bytes_per_element;

    // KV cache read
    op.reads.push_back({MemoryLocation::HBF, 0,
                        kv_cache_len * config_.num_kv_heads * head_dim * config_.bytes_per_element, true});
    // Q read from SRAM
    op.reads.push_back({MemoryLocation::NPU_SRAM, 0,
                        seq_len * config_.hidden_size * config_.bytes_per_element, true});

    // Output write
    op.writes.push_back({MemoryLocation::NPU_SRAM, 0,
                         seq_len * config_.hidden_size * config_.bytes_per_element, false});

    op.target_module = module_idx;
    op.dependencies.push_back(prev_op);
    prev_op = graph.add_op(op);
}

void TransformerCompiler::add_output_projection(ModelGraph& graph, uint32_t layer_idx,
                                                  uint32_t seq_len, uint32_t module_idx,
                                                  OpId& prev_op) const {
    SimOp op;
    op.name = "out_proj_" + std::to_string(layer_idx);
    op.compute.flops = seq_len * config_.hidden_size * config_.hidden_size * 2;
    op.compute.compute_class = ComputeClass::GEMM;
    op.compute.required_tensor_engines = 2;
    op.reads.push_back({MemoryLocation::NPU_SRAM, 0,
                        seq_len * config_.hidden_size * config_.bytes_per_element, true});
    op.reads.push_back({MemoryLocation::HBF, 0,
                        config_.hidden_size * config_.hidden_size * config_.bytes_per_element, true});
    op.writes.push_back({MemoryLocation::NPU_SRAM, 0,
                         seq_len * config_.hidden_size * config_.bytes_per_element, false});
    op.target_module = module_idx;
    op.dependencies.push_back(prev_op);
    prev_op = graph.add_op(op);
}

void TransformerCompiler::add_mlp(ModelGraph& graph, uint32_t layer_idx,
                                    uint32_t seq_len, uint32_t module_idx,
                                    OpId& prev_op) const {
    // Gate + Up projections → SiLU → Down projection
    SimOp gate_up;
    gate_up.name = "mlp_gate_up_" + std::to_string(layer_idx);
    gate_up.compute.flops = seq_len * config_.hidden_size * config_.ffn_size * 2 * 2;  // gate + up
    gate_up.compute.compute_class = ComputeClass::GEMM;
    gate_up.compute.required_tensor_engines = 2;
    gate_up.compute.sram_bytes = seq_len * config_.ffn_size * 2 * config_.bytes_per_element;
    gate_up.reads.push_back({MemoryLocation::NPU_SRAM, 0,
                             seq_len * config_.hidden_size * config_.bytes_per_element, true});
    gate_up.reads.push_back({MemoryLocation::HBF, 0,
                             config_.hidden_size * config_.ffn_size * 2 * config_.bytes_per_element, true});
    gate_up.writes.push_back({MemoryLocation::NPU_SRAM, 0,
                              seq_len * config_.ffn_size * 2 * config_.bytes_per_element, false});
    gate_up.target_module = module_idx;
    gate_up.dependencies.push_back(prev_op);
    OpId gate_up_id = graph.add_op(gate_up);

    // Down projection
    SimOp down;
    down.name = "mlp_down_" + std::to_string(layer_idx);
    down.compute.flops = seq_len * config_.ffn_size * config_.hidden_size * 2;
    down.compute.compute_class = ComputeClass::GEMM;
    down.compute.required_tensor_engines = 2;
    down.reads.push_back({MemoryLocation::NPU_SRAM, 0,
                          seq_len * config_.ffn_size * config_.bytes_per_element, true});
    down.reads.push_back({MemoryLocation::HBF, 0,
                          config_.ffn_size * config_.hidden_size * config_.bytes_per_element, true});
    down.writes.push_back({MemoryLocation::NPU_SRAM, 0,
                           seq_len * config_.hidden_size * config_.bytes_per_element, false});
    down.target_module = module_idx;
    down.dependencies.push_back(gate_up_id);
    prev_op = graph.add_op(down);
}

void TransformerCompiler::add_residual(ModelGraph& graph, uint32_t layer_idx,
                                        OpId& prev_op, OpId residual_op) const {
    SimOp op;
    op.name = "residual_" + std::to_string(layer_idx);
    op.compute.flops = config_.hidden_size;
    op.compute.compute_class = ComputeClass::ELEMENTWISE;
    op.compute.required_vector_engines = 1;
    op.dependencies.push_back(prev_op);
    op.dependencies.push_back(residual_op);
    prev_op = graph.add_op(op);
}

} // namespace atmos_sim
