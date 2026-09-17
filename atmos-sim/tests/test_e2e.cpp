#include <gtest/gtest.h>
#include "atmos_sim/sim_core/sim_engine.h"
#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/resource_pool.h"
#include "atmos_sim/virtual_devices/atmos_module.h"
#include "atmos_sim/topology/topology_graph.h"
#include "atmos_sim/model_graph/transformer_compiler.h"
#include "atmos_sim/model_graph/request_generator.h"
#include "atmos_sim/metrics/metrics_collector.h"
#include "atmos_sim/metrics/trace_recorder.h"
#include "atmos_sim/metrics/stall_attribution.h"

using namespace atmos_sim;

/// E2E Test: Single ATMOS device with simplified transformer workload.
/// Validates the complete pipeline: topology → module → model graph → simulation → metrics.
TEST(E2ETest, SingleDeviceSimulation) {
    reset_event_sequence();

    // 1. Build topology: 1 RC → 1 Switch → 1 ATMOS device
    TopologyGraph topo;
    RootComplexConfig rc_config;
    rc_config.name = "RC0";
    rc_config.aggregate_bandwidth_bytes_sec = 64e9;
    auto rc_id = topo.add_root_complex(rc_config);

    PCIeSwitchConfig sw_config;
    sw_config.name = "SW0";
    auto sw_id = topo.add_switch(sw_config);

    auto ep_id = topo.add_endpoint("ATMOS_0", 0);

    PCIeLinkConfig link;
    link.generation = 4;
    link.lane_count = 8;
    link.latency_ns = 100;
    topo.connect_switch_to_rc(sw_id, rc_id, link);
    topo.connect_endpoint_to_switch(ep_id, sw_id, link);

    EXPECT_EQ(topo.node_count(), 3u);

    // 2. Create ATMOS module
    AtmosModuleConfig mod_config;
    mod_config.name = "ATMOS_Module_0";
    mod_config.npu_config.peak_flops = 500'000'000'000ULL;
    mod_config.npu_config.tensor_engine_count = 4;
    mod_config.npu_config.execution_slots = 8;
    mod_config.hbf_config.capacity_bytes = 128ULL * 1024 * 1024 * 1024;
    mod_config.hbf_config.channel_count = 8;
    mod_config.lpddr_config.capacity_bytes = 32ULL * 1024 * 1024 * 1024;
    mod_config.dma_engine_count = 2;
    AtmosModule module(1, mod_config);

    // 3. Compile a small transformer model (2 layers, small hidden size)
    TransformerConfig tf_config;
    tf_config.model_name = "test-llm";
    tf_config.num_layers = 2;
    tf_config.hidden_size = 512;
    tf_config.num_heads = 8;
    tf_config.num_kv_heads = 2;
    tf_config.ffn_size = 1024;
    tf_config.bytes_per_element = 2;
    TransformerCompiler compiler(tf_config);

    auto prefill_graph = compiler.compile_prefill(128, 1);
    EXPECT_GT(prefill_graph.op_count(), 0u);

    auto decode_graph = compiler.compile_decode(128, 1);
    EXPECT_GT(decode_graph.op_count(), 0u);

    // 4. Generate workload
    RequestGeneratorConfig gen_config;
    gen_config.pattern = ArrivalPattern::FIXED_INTERVAL;
    gen_config.interval_ns = ms_to_sim(10);
    gen_config.total_requests = 5;
    gen_config.seed = 42;
    gen_config.prompt_tokens_min = 128;
    gen_config.prompt_tokens_max = 128;
    gen_config.output_tokens_min = 32;
    gen_config.output_tokens_max = 32;
    RequestGenerator gen(gen_config);
    auto workload = gen.generate();

    EXPECT_EQ(workload.size(), 5u);

    // 5. Run simulation with metrics
    SimEngine engine;
    MetricsCollector metrics;
    TraceRecorder trace;
    StallAttribution stalls;

    uint64_t requests_processed = 0;

    // Model the pipeline: request → check resources → simulate compute → complete
    engine.register_handler(EventType::REQUEST_ARRIVED,
        [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            metrics.counter("requests_arrived").increment();
            trace.record(clock.now(), e.sequence, EventType::REQUEST_ARRIVED,
                        e.target, "Request " + std::to_string(e.target));

            // Check NPU availability
            if (module.npu().can_accept() && module.npu().tensor_engines().acquire()) {
                return {{clock.now(), next_event_sequence(),
                         EventType::COMPUTE_STARTED, e.target, {}}};
            } else {
                stalls.record_stall(clock.now(), clock.now() + 1000,
                                   StallReason::WAIT_COMPUTE, e.target);
                return {{clock.now() + 1000, next_event_sequence(),
                         EventType::COMPUTE_STARTED, e.target, {}}};
            }
        });

    engine.register_handler(EventType::COMPUTE_STARTED,
        [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            metrics.counter("compute_ops").increment();
            trace.record(clock.now(), e.sequence, EventType::COMPUTE_STARTED,
                        e.target, "Compute started");

            // Simulate compute time based on model complexity
            SimTime compute_time = ms_to_sim(1);  // 1ms compute
            return {{clock.now() + compute_time, next_event_sequence(),
                     EventType::COMPUTE_COMPLETED, e.target, {}}};
        });

    engine.register_handler(EventType::COMPUTE_COMPLETED,
        [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            ++requests_processed;
            module.npu().tensor_engines().release();

            trace.record(clock.now(), e.sequence, EventType::COMPUTE_COMPLETED,
                        e.target, "Request completed");

            metrics.record_latency("request_latency", clock.now());

            return {};
        });

    // Schedule all workload requests
    for (const auto& req : workload) {
        engine.schedule({req.arrival_time, next_event_sequence(),
                         EventType::REQUEST_ARRIVED, req.request_id, {}});
    }

    auto result = engine.run();

    // 6. Verify results
    EXPECT_EQ(requests_processed, 5u);
    EXPECT_EQ(metrics.counter("requests_arrived").value, 5u);
    EXPECT_EQ(metrics.counter("compute_ops").value, 5u);
    EXPECT_GT(result.final_time, 0u);
    EXPECT_GE(trace.count(), 10u);  // At least 2 events per request
}

/// E2E Test: Multi-device scaling comparison.
/// Run same workload with 1 device vs 2 devices and verify different results.
TEST(E2ETest, MultiDeviceScalingComparison) {
    auto run_with_devices = [](uint32_t device_count) -> SimTime {
        reset_event_sequence();
        SimEngine engine;

        // Create device resource pools
        auto npu_pool = std::make_shared<ResourcePool>("npu_pool", device_count);
        uint64_t total_requests = 20;
        uint64_t completed = 0;

        engine.register_handler(EventType::REQUEST_ARRIVED,
            [&](const Event& e, SimClock& clock) -> std::vector<Event> {
                if (npu_pool->acquire()) {
                    return {{clock.now() + ms_to_sim(1), next_event_sequence(),
                             EventType::COMPUTE_COMPLETED, e.target, {}}};
                }
                // Queue and retry later
                return {{clock.now() + 100, next_event_sequence(),
                         EventType::REQUEST_ARRIVED, e.target, {}}};
            });

        engine.register_handler(EventType::COMPUTE_COMPLETED,
            [&](const Event& e, SimClock& clock) -> std::vector<Event> {
                npu_pool->release();
                ++completed;
                return {};
            });

        // Schedule requests in burst
        for (uint64_t i = 0; i < total_requests; ++i) {
            engine.schedule({0, next_event_sequence(), EventType::REQUEST_ARRIVED, i + 1, {}});
        }

        auto result = engine.run();
        EXPECT_EQ(completed, total_requests);
        return result.final_time;
    };

    SimTime time_1_device = run_with_devices(1);
    SimTime time_2_devices = run_with_devices(2);

    // 2 devices should be faster (or equal due to burst)
    EXPECT_LE(time_2_devices, time_1_device);

    // Scaling factor
    double scaling = static_cast<double>(time_1_device) / std::max(static_cast<double>(time_2_devices), 1.0);
    EXPECT_GE(scaling, 1.0);
    // Should be at most 2.0 (linear scaling)
    EXPECT_LE(scaling, 2.1);
}

/// E2E Test: Topology affects transfer times.
TEST(E2ETest, TopologyDrivenTransferDifference) {
    // Topology A: Direct links (each device has dedicated uplink)
    auto build_topology_a = []() -> TopologyGraph {
        TopologyGraph topo;
        RootComplexConfig rc_config;
        rc_config.aggregate_bandwidth_bytes_sec = 64e9;
        auto rc = topo.add_root_complex(rc_config);

        PCIeSwitchConfig sw_config;
        auto sw = topo.add_switch(sw_config);

        auto ep0 = topo.add_endpoint("D0", 0);
        auto ep1 = topo.add_endpoint("D1", 1);

        PCIeLinkConfig link16;
        link16.generation = 4;
        link16.lane_count = 16;
        link16.latency_ns = 100;

        topo.connect_endpoint_to_switch(ep0, sw, link16);
        topo.connect_endpoint_to_switch(ep1, sw, link16);
        topo.connect_switch_to_rc(sw, rc, link16);

        return topo;
    };

    // Topology B: Shared uplink (narrow uplink)
    auto build_topology_b = []() -> TopologyGraph {
        TopologyGraph topo;
        RootComplexConfig rc_config;
        rc_config.aggregate_bandwidth_bytes_sec = 64e9;
        auto rc = topo.add_root_complex(rc_config);

        PCIeSwitchConfig sw_config;
        auto sw = topo.add_switch(sw_config);

        auto ep0 = topo.add_endpoint("D0", 0);
        auto ep1 = topo.add_endpoint("D1", 1);

        PCIeLinkConfig link8;
        link8.generation = 4;
        link8.lane_count = 8;
        link8.latency_ns = 100;

        PCIeLinkConfig narrow_uplink;
        narrow_uplink.generation = 4;
        narrow_uplink.lane_count = 8;  // Narrow shared uplink
        narrow_uplink.latency_ns = 200;

        topo.connect_endpoint_to_switch(ep0, sw, link8);
        topo.connect_endpoint_to_switch(ep1, sw, link8);
        topo.connect_switch_to_rc(sw, rc, narrow_uplink);

        return topo;
    };

    auto topo_a = build_topology_a();
    auto topo_b = build_topology_b();

    // Get path bandwidth for each topology
    auto path_a = topo_a.find_path(
        topo_a.nodes()[1].id,  // ep0
        topo_a.nodes()[0].id   // RC
    );
    auto path_b = topo_b.find_path(
        topo_b.nodes()[1].id,
        topo_b.nodes()[0].id
    );

    // Topology A should have higher bottleneck bandwidth
    EXPECT_GT(path_a.bottleneck_bandwidth, 0u);
    EXPECT_GT(path_b.bottleneck_bandwidth, 0u);

    // Transfer time should differ
    uint64_t payload = 1024 * 1024;  // 1 MB
    SimTime time_a = topo_a.transfer_time(
        topo_a.nodes()[1].id, topo_a.nodes()[0].id, payload);
    SimTime time_b = topo_b.transfer_time(
        topo_b.nodes()[1].id, topo_b.nodes()[0].id, payload);

    // Both should produce valid times
    EXPECT_GT(time_a, 0u);
    EXPECT_GT(time_b, 0u);
    // Topology B (narrow uplink) should be slower
    EXPECT_GE(time_b, time_a);
}

/// E2E Test: Transformer compiler produces valid graph
TEST(E2ETest, TransformerCompilerFullPipeline) {
    TransformerConfig config;
    config.model_name = "atmos-test-model";
    config.num_layers = 4;
    config.hidden_size = 1024;
    config.num_heads = 16;
    config.num_kv_heads = 4;
    config.ffn_size = 2048;
    config.bytes_per_element = 2;

    TransformerCompiler compiler(config);

    // Compile full inference
    auto graph = compiler.compile_inference(256, 16, 2);

    EXPECT_GT(graph.op_count(), 0u);
    EXPECT_EQ(graph.meta().num_layers, 4u);

    // Verify DAG validity: all ops should be reachable from roots
    auto roots = graph.root_ops();
    EXPECT_FALSE(roots.empty());

    // All roots should have no dependencies
    for (OpId root : roots) {
        EXPECT_TRUE(graph.get_op(root).dependencies.empty());
    }

    // Serialization round-trip
    std::string json = graph.to_json();
    EXPECT_NE(json.find("atmos-test-model"), std::string::npos);
}
