#pragma once

#include "atmos_sim/model_graph/model_graph.h"
#include <cstdint>
#include <string>

namespace atmos_sim {

/// Configuration for the transformer graph compiler.
struct TransformerConfig {
    std::string model_name = "llama-70b";
    uint32_t num_layers = 80;
    uint32_t hidden_size = 8192;
    uint32_t num_heads = 64;
    uint32_t num_kv_heads = 8;  // GQA
    uint32_t ffn_size = 28672;
    uint32_t vocab_size = 128256;
    uint32_t bytes_per_element = 2;  // FP16
    uint32_t max_seq_len = 4096;
    uint32_t tensor_parallel = 1;
};

/// Compiles a transformer architecture into an executable ModelGraph (SimIR).
class TransformerCompiler {
public:
    explicit TransformerCompiler(const TransformerConfig& config);

    /// Compile a single transformer layer into graph ops.
    ModelGraph compile_layer(uint32_t layer_idx, uint32_t seq_len,
                              uint32_t kv_cache_len, uint32_t module_count) const;

    /// Compile a full model inference (all layers).
    ModelGraph compile_inference(uint32_t prompt_tokens, uint32_t max_output_tokens,
                                  uint32_t module_count) const;

    /// Compile just prefill phase.
    ModelGraph compile_prefill(uint32_t prompt_tokens, uint32_t module_count) const;

    /// Compile just decode phase (one token).
    ModelGraph compile_decode(uint32_t kv_cache_len, uint32_t module_count) const;

    const TransformerConfig& config() const { return config_; }

private:
    TransformerConfig config_;

    void add_rmsnorm(ModelGraph& graph, uint32_t layer_idx, uint32_t hidden_bytes,
                     uint32_t module_idx, OpId& prev_op) const;
    void add_qkv_projection(ModelGraph& graph, uint32_t layer_idx, uint32_t seq_len,
                            uint32_t module_idx, OpId& prev_op) const;
    void add_attention(ModelGraph& graph, uint32_t layer_idx, uint32_t seq_len,
                      uint32_t kv_cache_len, uint32_t module_idx, OpId& prev_op) const;
    void add_output_projection(ModelGraph& graph, uint32_t layer_idx,
                              uint32_t seq_len, uint32_t module_idx, OpId& prev_op) const;
    void add_mlp(ModelGraph& graph, uint32_t layer_idx, uint32_t seq_len,
                uint32_t module_idx, OpId& prev_op) const;
    void add_residual(ModelGraph& graph, uint32_t layer_idx, OpId& prev_op,
                     OpId residual_op) const;
};

} // namespace atmos_sim
