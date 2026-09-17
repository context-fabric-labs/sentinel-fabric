#include <gtest/gtest.h>
#include "atmos_sim/metrics/metrics_collector.h"
#include "atmos_sim/metrics/trace_recorder.h"
#include "atmos_sim/metrics/stall_attribution.h"
#include "atmos_sim/calibration/calibration_database.h"
#include "atmos_sim/calibration/parameter_sweep.h"

using namespace atmos_sim;

// ==================== MetricsCollector Tests ====================

TEST(MetricsCollectorTest, Counters) {
    MetricsCollector mc;
    auto& c = mc.counter("requests_total");
    EXPECT_EQ(c.value, 0u);

    c.increment();
    c.increment(5);
    EXPECT_EQ(c.value, 6u);

    c.reset();
    EXPECT_EQ(c.value, 0u);
}

TEST(MetricsCollectorTest, Gauges) {
    MetricsCollector mc;
    auto& g = mc.gauge("gpu_utilization");
    EXPECT_DOUBLE_EQ(g.value, 0.0);

    g.set(0.75);
    EXPECT_DOUBLE_EQ(g.value, 0.75);

    g.add(0.1);
    EXPECT_DOUBLE_EQ(g.value, 0.85);
}

TEST(MetricsCollectorTest, HistogramPercentiles) {
    MetricsCollector mc;
    std::vector<uint64_t> buckets = {10, 50, 100, 500, 1000};
    auto& h = mc.histogram("latency", buckets);

    // Record values: 5, 15, 55, 105, 505, 1005
    h.record(5);
    h.record(15);
    h.record(55);
    h.record(105);
    h.record(505);
    h.record(1005);

    EXPECT_EQ(h.total_count, 6u);
    EXPECT_GT(h.mean(), 0.0);

    // P50 should be around the median value
    uint64_t p50 = h.percentile(50);
    EXPECT_GT(p50, 0u);

    // P99 should be high
    uint64_t p99 = h.percentile(99);
    EXPECT_GE(p99, p50);
}

TEST(MetricsCollectorTest, RecordLatency) {
    MetricsCollector mc;
    mc.record_latency("ttft", ms_to_sim(10));
    mc.record_latency("ttft", ms_to_sim(20));
    mc.record_latency("ttft", ms_to_sim(30));

    auto& h = mc.histogram("ttft", {});  // Uses default buckets
    EXPECT_EQ(h.total_count, 3u);
}

// ==================== TraceRecorder Tests ====================

TEST(TraceRecorderTest, RecordAndExport) {
    TraceRecorder recorder;
    recorder.record(0, 0, EventType::REQUEST_ARRIVED, 1, "Request A arrived");
    recorder.record(100, 1, EventType::COMPUTE_STARTED, 1, "Compute started");
    recorder.record(500, 2, EventType::COMPUTE_COMPLETED, 1, "Compute done");

    EXPECT_EQ(recorder.count(), 3u);

    std::string trace = recorder.to_string();
    EXPECT_NE(trace.find("REQUEST_ARRIVED"), std::string::npos);
    EXPECT_NE(trace.find("T=100"), std::string::npos);

    std::string json = recorder.to_json();
    EXPECT_NE(json.find("REQUEST_ARRIVED"), std::string::npos);
}

// ==================== StallAttribution Tests ====================

TEST(StallAttributionTest, Recording) {
    StallAttribution sa;
    sa.record_stall(0, 100, StallReason::WAIT_HBF);
    sa.record_stall(100, 200, StallReason::WAIT_COMPUTE);
    sa.record_stall(200, 350, StallReason::WAIT_HBF);

    EXPECT_EQ(sa.records().size(), 3u);

    auto totals = sa.total_by_reason();
    EXPECT_EQ(totals[StallReason::WAIT_HBF], 250u);  // 100 + 150
    EXPECT_EQ(totals[StallReason::WAIT_COMPUTE], 100u);
}

TEST(StallAttributionTest, BreakdownPercent) {
    StallAttribution sa;
    sa.record_stall(0, 100, StallReason::WAIT_HBF);
    sa.record_stall(0, 100, StallReason::WAIT_COMPUTE);

    auto pct = sa.breakdown_percent();
    EXPECT_DOUBLE_EQ(pct[StallReason::WAIT_HBF], 50.0);
    EXPECT_DOUBLE_EQ(pct[StallReason::WAIT_COMPUTE], 50.0);
}

TEST(StallAttributionTest, Summary) {
    StallAttribution sa;
    sa.record_stall(0, 100, StallReason::WAIT_HBF);
    std::string summary = sa.summary();
    EXPECT_NE(summary.find("WAIT_HBF"), std::string::npos);
}

// ==================== CalibrationDatabase Tests ====================

TEST(CalibrationDatabaseTest, SetAndGet) {
    CalibrationDatabase db;
    CalibrationParameter param;
    param.name = "pcie_bw";
    param.nominal = 32e9;
    param.low = 28e9;
    param.high = 35e9;
    param.confidence = Confidence::MEDIUM;
    db.set_parameter(param);

    auto* retrieved = db.get_parameter("pcie_bw");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_DOUBLE_EQ(retrieved->nominal, 32e9);

    EXPECT_DOUBLE_EQ(db.nominal("pcie_bw"), 32e9);
    EXPECT_DOUBLE_EQ(db.nominal("nonexistent", 42.0), 42.0);
}

TEST(CalibrationDatabaseTest, DefaultAtmosParams) {
    auto db = CalibrationDatabase::default_atmos_params();
    auto names = db.parameter_names();
    EXPECT_GT(names.size(), 0u);

    // Should have PCIe bandwidth parameter
    EXPECT_NE(db.get_parameter("pcie_effective_bw"), nullptr);
    EXPECT_NE(db.get_parameter("npu_peak_flops"), nullptr);
    EXPECT_NE(db.get_parameter("hbf_read_bw_per_channel"), nullptr);
}

TEST(CalibrationDatabaseTest, MonteCarloSample) {
    auto db = CalibrationDatabase::default_atmos_params();
    auto sample1 = db.monte_carlo_sample(42);
    auto sample2 = db.monte_carlo_sample(42);
    auto sample3 = db.monte_carlo_sample(43);

    // Same seed → same results
    for (const auto& [name, val] : sample1) {
        EXPECT_DOUBLE_EQ(val, sample2[name]);
    }
    // Different seed → likely different
    bool any_different = false;
    for (const auto& [name, val] : sample1) {
        if (val != sample3[name]) any_different = true;
    }
    EXPECT_TRUE(any_different);
}

TEST(CalibrationDatabaseTest, Serialization) {
    CalibrationDatabase db;
    db.set_parameter({"test_param", "ns", 100.0, 50.0, 150.0,
                      Confidence::HIGH, ParameterSource::MEASURED_DIRECT});

    std::string json = db.to_json();
    EXPECT_NE(json.find("test_param"), std::string::npos);
    EXPECT_NE(json.find("HIGH"), std::string::npos);
}

// ==================== ParameterSweep Tests ====================

TEST(ParameterSweepTest, SingleParameterSweep) {
    auto db = CalibrationDatabase::default_atmos_params();
    ParameterSweep sweep;

    SweepConfig config;
    config.parameter_name = "npu_peak_flops";
    config.start_value = 100e9;
    config.end_value = 1000e9;
    config.num_steps = 5;

    auto sim_fn = [](const std::unordered_map<std::string, double>& params) -> std::unordered_map<std::string, double> {
        auto it = params.find("npu_peak_flops");
        double flops = (it != params.end()) ? it->second : 500e9;
        return {{"throughput", flops / 1e9}};
    };

    auto results = sweep.sweep_single(config, db, sim_fn);
    EXPECT_EQ(results.size(), 5u);
    EXPECT_GT(results[0].output_metrics.at("throughput"), 0.0);
}

TEST(ParameterSweepTest, MonteCarloSweep) {
    auto db = CalibrationDatabase::default_atmos_params();
    ParameterSweep sweep;

    auto sim_fn = [](const std::unordered_map<std::string, double>& params) -> std::unordered_map<std::string, double> {
        double total = 0;
        for (const auto& [_, v] : params) total += v;
        return {{"score", total}};
    };

    auto results = sweep.monte_carlo(db, 50, 42, sim_fn);
    EXPECT_EQ(results.size(), 50u);
}

TEST(ParameterSweepTest, SensitivityAnalysis) {
    ParameterSweep sweep;
    std::vector<SweepResult> results;

    // Create synthetic results where param "a" strongly correlates with output
    for (int i = 0; i < 10; ++i) {
        SweepResult r;
        r.parameter_values["a"] = i * 10.0;
        r.parameter_values["b"] = 50.0;  // Constant
        r.output_metrics["throughput"] = i * 10.0;  // Perfectly correlated with a
        results.push_back(r);
    }

    auto sensitivity = sweep.analyze_sensitivity(results, "throughput");
    ASSERT_GE(sensitivity.size(), 1u);
    EXPECT_EQ(sensitivity[0].parameter_name, "a");
    EXPECT_GT(sensitivity[0].correlation, 0.99);
}
