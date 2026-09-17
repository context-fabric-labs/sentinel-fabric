#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace atmos_sim {

/// Counter metric (monotonically increasing).
struct Counter {
    std::string name;
    uint64_t value = 0;

    void increment(uint64_t delta = 1) { value += delta; }
    void reset() { value = 0; }
};

/// Gauge metric (can go up or down).
struct Gauge {
    std::string name;
    double value = 0.0;

    void set(double v) { value = v; }
    void add(double delta) { value += delta; }
};

/// Histogram metric (tracks distribution of values).
struct Histogram {
    std::string name;
    std::vector<uint64_t> buckets;  // Bucket upper bounds (ns)
    std::vector<uint64_t> counts;   // Count per bucket
    uint64_t total_count = 0;
    double sum = 0.0;

    explicit Histogram(const std::string& name,
                       const std::vector<uint64_t>& bucket_bounds);

    void record(uint64_t value_ns);
    double mean() const;
    uint64_t percentile(double p) const;
    void reset();
};

/// Metrics collector for simulation results.
class MetricsCollector {
public:
    MetricsCollector();

    /// Create or get a counter.
    Counter& counter(const std::string& name);

    /// Create or get a gauge.
    Gauge& gauge(const std::string& name);

    /// Create or get a histogram.
    Histogram& histogram(const std::string& name, const std::vector<uint64_t>& buckets);

    /// Record a latency sample.
    void record_latency(const std::string& name, SimTime duration_ns);

    /// Get all counters.
    const std::unordered_map<std::string, Counter>& counters() const { return counters_; }

    /// Get all gauges.
    const std::unordered_map<std::string, Gauge>& gauges() const { return gauges_; }

    /// Get all histograms.
    const std::unordered_map<std::string, Histogram>& histograms() const { return histograms_; }

    /// Reset all metrics.
    void reset();

private:
    std::unordered_map<std::string, Counter> counters_;
    std::unordered_map<std::string, Gauge> gauges_;
    std::unordered_map<std::string, Histogram> histograms_;
};

} // namespace atmos_sim
