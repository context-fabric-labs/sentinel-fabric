#include <gtest/gtest.h>
#include "atmos_sim/model_graph/sim_op.h"
#include "atmos_sim/model_graph/model_graph.h"
#include "atmos_sim/model_graph/transformer_compiler.h"
#include "atmos_sim/model_graph/request_generator.h"
#include "atmos_sim/model_graph/tokensim_adapter.h"

using namespace atmos_sim;

// ==================== SimOp Tests ====================

TEST(SimOpTest, ComputeClassName) {
    EXPECT_STREQ(compute_class_name(ComputeClass::GEMM), "GEMM");
    EXPECT_STREQ(compute_class_name(ComputeClass::ATTENTION), "ATTENTION");
    EXPECT_STREQ(compute_class_name(ComputeClass::ELEMENTWISE), "ELEMENTWISE");
}

// ==================== ModelGraph Tests ====================

TEST(ModelGraphTest, AddAndGetOp) {
    ModelGraph graph;
    SimOp op1;
    op1.name = "op1";
    op1.compute.flops = 100;
    OpId id1 = graph.add_op(op1);
    EXPECT_EQ(id1, 1u);

    SimOp op2;
    op2.name = "op2";
    op2.dependencies = {id1};
    OpId id2 = graph.add_op(op2);
    EXPECT_EQ(id2, 2u);

    EXPECT_EQ(graph.op_count(), 2u);
    EXPECT_EQ(graph.get_op(id1).name, "op1");
    EXPECT_EQ(graph.get_op(id2).dependencies.size(), 1u);
}

TEST(ModelGraphTest, RootAndLeafOps) {
    ModelGraph graph;
    SimOp op1; op1.name = "root"; auto id1 = graph.add_op(op1);
    SimOp op2; op2.name = "mid"; op2.dependencies = {id1}; auto id2 = graph.add_op(op2);
    SimOp op3; op3.name = "leaf"; op3.dependencies = {id2}; auto id3 = graph.add_op(op3);

    auto roots = graph.root_ops();
    EXPECT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], id1);

    auto leaves = graph.leaf_ops();
    EXPECT_EQ(leaves.size(), 1u);
    EXPECT_EQ(leaves[0], id3);
}

TEST(ModelGraphTest, Serialization) {
    ModelGraph graph;
    graph.meta().name = "test_model";
    graph.meta().num_layers = 2;

    SimOp op1;
    op1.name = "gemm";
    op1.compute.flops = 1000;
    graph.add_op(op1);

    std::string json = graph.to_json();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("test_model"), std::string::npos);
    EXPECT_NE(json.find("gemm"), std::string::npos);
}

// ==================== TransformerCompiler Tests ====================

class TransformerCompilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        TransformerConfig config;
        config.model_name = "test-model";
        config.num_layers = 2;
        config.hidden_size = 512;
        config.num_heads = 8;
        config.num_kv_heads = 2;  // GQA
        config.ffn_size = 1024;
        config.bytes_per_element = 2;
        compiler = std::make_unique<TransformerCompiler>(config);
    }
    std::unique_ptr<TransformerCompiler> compiler;
};

TEST_F(TransformerCompilerTest, CompileSingleLayer) {
    auto graph = compiler->compile_layer(0, 128, 0, 1);
    EXPECT_GT(graph.op_count(), 0u);
    EXPECT_EQ(graph.meta().num_layers, 1u);

    // Verify DAG structure: root ops have no dependencies
    auto roots = graph.root_ops();
    EXPECT_FALSE(roots.empty());

    for (OpId root : roots) {
        EXPECT_TRUE(graph.get_op(root).dependencies.empty());
    }
}

TEST_F(TransformerCompilerTest, CompilePrefill) {
    auto graph = compiler->compile_prefill(256, 1);
    EXPECT_GT(graph.op_count(), 0u);
    EXPECT_EQ(graph.meta().num_layers, 2u);  // 2 layers
}

TEST_F(TransformerCompilerTest, CompileDecode) {
    auto graph = compiler->compile_decode(128, 1);
    EXPECT_GT(graph.op_count(), 0u);
}

TEST_F(TransformerCompilerTest, CompileFullInference) {
    // Small model for testing: 2 layers, short prompt
    auto graph = compiler->compile_inference(64, 4, 1);
    EXPECT_GT(graph.op_count(), 0u);
}

TEST_F(TransformerCompilerTest, MultiModulePlacement) {
    // 2 modules, 2 layers — each layer targets a different module
    auto graph = compiler->compile_layer(0, 128, 0, 2);
    EXPECT_GT(graph.op_count(), 0u);

    // First layer should target module 0
    auto roots = graph.root_ops();
    EXPECT_FALSE(roots.empty());
}

// ==================== RequestGenerator Tests ====================

TEST(RequestGeneratorTest, DeterministicGeneration) {
    RequestGeneratorConfig config;
    config.pattern = ArrivalPattern::POISSON;
    config.arrival_rate_rps = 10.0;
    config.total_requests = 100;
    config.seed = 42;
    config.prompt_tokens_min = 128;
    config.prompt_tokens_max = 1024;
    config.output_tokens_min = 64;
    config.output_tokens_max = 512;

    RequestGenerator gen1(config);
    RequestGenerator gen2(config);

    auto requests1 = gen1.generate();
    auto requests2 = gen2.generate();

    ASSERT_EQ(requests1.size(), requests2.size());
    for (size_t i = 0; i < requests1.size(); ++i) {
        EXPECT_EQ(requests1[i].shape.prompt_tokens, requests2[i].shape.prompt_tokens)
            << "Mismatch at index " << i;
        EXPECT_EQ(requests1[i].shape.max_output_tokens, requests2[i].shape.max_output_tokens)
            << "Mismatch at index " << i;
        EXPECT_EQ(requests1[i].arrival_time, requests2[i].arrival_time)
            << "Mismatch at index " << i;
    }
}

TEST(RequestGeneratorTest, FixedInterval) {
    RequestGeneratorConfig config;
    config.pattern = ArrivalPattern::FIXED_INTERVAL;
    config.interval_ns = ms_to_sim(10);
    config.total_requests = 10;

    RequestGenerator gen(config);
    auto requests = gen.generate();

    EXPECT_EQ(requests.size(), 10u);
    // Fixed interval: requests should be evenly spaced
    for (size_t i = 1; i < requests.size(); ++i) {
        EXPECT_EQ(requests[i].arrival_time - requests[i-1].arrival_time, ms_to_sim(10));
    }
}

TEST(RequestGeneratorTest, BurstPattern) {
    RequestGeneratorConfig config;
    config.pattern = ArrivalPattern::BURST;
    config.total_requests = 20;
    config.seed = 42;

    RequestGenerator gen(config);
    auto requests = gen.generate();

    // First 10 requests should have arrival time 0
    int burst_count = 0;
    for (const auto& req : requests) {
        if (req.arrival_time == 0) ++burst_count;
    }
    EXPECT_EQ(burst_count, 10);
}

// ==================== TokenSim Adapter Tests ====================

TEST(TokenSimAdapterTest, ConvertsRequestArrivalsToTimedWorkload) {
    TokenSimAdapterConfig config;
    config.default_shape.prompt_tokens = 128;
    config.default_shape.max_output_tokens = 32;
    config.default_shape.model_name = "default-model";
    TokenSimAdapter adapter(config);

    std::vector<TokenSimEvent> events = {
        {2, ms_to_sim(5), TokenSimEventType::REQUEST_ARRIVED, 256, 64, 1, "llama-8b"},
        {2, ms_to_sim(6), TokenSimEventType::DECODE_TOKEN_READY, 0, 0, 0, ""},
        {1, ms_to_sim(1), TokenSimEventType::REQUEST_ARRIVED, 0, 0, 0, ""},
    };

    auto workload = adapter.to_workload(events);

    ASSERT_EQ(workload.size(), 2u);
    EXPECT_EQ(workload[0].request_id, 1u);
    EXPECT_EQ(workload[0].arrival_time, ms_to_sim(1));
    EXPECT_EQ(workload[0].shape.prompt_tokens, 128u);
    EXPECT_EQ(workload[0].shape.max_output_tokens, 32u);
    EXPECT_EQ(workload[0].shape.model_name, "default-model");

    EXPECT_EQ(workload[1].request_id, 2u);
    EXPECT_EQ(workload[1].arrival_time, ms_to_sim(5));
    EXPECT_EQ(workload[1].shape.prompt_tokens, 256u);
    EXPECT_EQ(workload[1].shape.max_output_tokens, 64u);
    EXPECT_EQ(workload[1].shape.priority, 1u);
    EXPECT_EQ(workload[1].shape.model_name, "llama-8b");
}

TEST(TokenSimAdapterTest, OrdersSameTimestampByRequestId) {
    TokenSimAdapter adapter;
    std::vector<TokenSimEvent> events = {
        {3, ms_to_sim(1), TokenSimEventType::REQUEST_ARRIVED, 0, 0, 0, ""},
        {1, ms_to_sim(1), TokenSimEventType::REQUEST_ARRIVED, 0, 0, 0, ""},
        {2, ms_to_sim(1), TokenSimEventType::REQUEST_ARRIVED, 0, 0, 0, ""},
    };

    auto workload = adapter.to_workload(events);

    ASSERT_EQ(workload.size(), 3u);
    EXPECT_EQ(workload[0].request_id, 1u);
    EXPECT_EQ(workload[1].request_id, 2u);
    EXPECT_EQ(workload[2].request_id, 3u);
}
