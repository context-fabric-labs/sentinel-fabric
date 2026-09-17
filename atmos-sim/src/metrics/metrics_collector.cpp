#include "atmos_sim/metrics/metrics_collector.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace atmos_sim {

MetricsCollector::MetricsCollector() {
    // Pre-create standard latency histograms with common bucket boundaries
}

Counter& MetricsCollector::counter(const std::string& name) {
    auto it = counters_.find(name);
    if (it == counters_.end()) {
        counters_[name] = Counter{name, 0};
        return counters_[name];
    }
    return it->second;
}

Gauge& MetricsCollector::gauge(const std::string& name) {
    auto it = gauges_.find(name);
    if (it == gauges_.end()) {
        gauges_[name] = Gauge{name, 0.0};
        return gauges_[name];
    }
    return it->second;
}

Histogram& MetricsCollector::histogram(const std::string& name, const std::vector<uint64_t>& buckets) {
    auto it = histograms_.find(name);
    if (it == histograms_.end()) {
        histograms_.emplace(name, Histogram(name, buckets));
        return histograms_.at(name);
    }
    return it->second;
}

void MetricsCollector::record_latency(const std::string& name, SimTime duration_ns) {
    // Default latency buckets: 1us, 10us, 100us, 1ms, 10ms, 100ms, 1s, 10s
    static const std::vector<uint64_t> default_buckets = {
        1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000, 10000000000ULL
    };

    auto it = histograms_.find(name);
    if (it == histograms_.end()) {
        histograms_.emplace(name, Histogram(name, default_buckets));
        it = histograms_.find(name);
    }
    it->second.record(duration_ns);
}

void MetricsCollector::reset() {
    for (auto& [_, c] : counters_) c.reset();
    for (auto& [_, g] : gauges_) g.value = 0.0;
    for (auto& [_, h] : histograms_) h.reset();
}

// Histogram implementation

Histogram::Histogram(const std::string& name, const std::vector<uint64_t>& bucket_bounds)
    : name(name), buckets(bucket_bounds), counts(bucket_bounds.size() + 1, 0) {}

void Histogram::record(uint64_t value_ns) {
    ++total_count;
    sum += static_cast<double>(value_ns);

    for (size_t i = 0; i < buckets.size(); ++i) {
        if (value_ns <= buckets[i]) {
            ++counts[i];
            return;
        }
    }
    // Overflow bucket
    ++counts.back();
}

double Histogram::mean() const {
    if (total_count == 0) return 0.0;
    return sum / total_count;
}

uint64_t Histogram::percentile(double p) const {
    if (total_count == 0) return 0;
    uint64_t target = static_cast<uint64_t>(std::ceil(p / 100.0 * total_count));
    uint64_t cumulative = 0;

    for (size_t i = 0; i < counts.size(); ++i) {
        cumulative += counts[i];
        if (cumulative >= target) {
            if (i < buckets.size()) return buckets[i];
            return buckets.empty() ? 0 : buckets.back();
        }
    }
    return buckets.empty() ? 0 : buckets.back();
}

void Histogram::reset() {
    std::fill(counts.begin(), counts.end(), 0);
    total_count = 0;
    sum = 0.0;
}

} // namespace atmos_sim
