#include "atmos_sim/metrics/stall_attribution.h"
#include <sstream>
#include <iomanip>
#include <map>

namespace atmos_sim {

const char* stall_reason_name(StallReason reason) {
    switch (reason) {
        case StallReason::NONE:            return "NONE";
        case StallReason::WAIT_INPUT:      return "WAIT_INPUT";
        case StallReason::WAIT_HBF:        return "WAIT_HBF";
        case StallReason::WAIT_LPDDR:      return "WAIT_LPDDR";
        case StallReason::WAIT_DMA:        return "WAIT_DMA";
        case StallReason::WAIT_PCIE:       return "WAIT_PCIE";
        case StallReason::WAIT_COMPUTE:    return "WAIT_COMPUTE";
        case StallReason::WAIT_DEPENDENCY: return "WAIT_DEPENDENCY";
        case StallReason::WAIT_SCHEDULER:  return "WAIT_SCHEDULER";
        case StallReason::WAIT_SRAM:       return "WAIT_SRAM";
    }
    return "UNKNOWN";
}

StallAttribution::StallAttribution() = default;

void StallAttribution::record_stall(SimTime start, SimTime end, StallReason reason, uint64_t op_id) {
    records_.push_back({start, end, reason, op_id});
}

std::unordered_map<StallReason, SimTime> StallAttribution::total_by_reason() const {
    std::unordered_map<StallReason, SimTime> totals;
    for (const auto& record : records_) {
        totals[record.reason] += record.duration();
    }
    return totals;
}

std::unordered_map<StallReason, double> StallAttribution::breakdown_percent() const {
    auto totals = total_by_reason();
    SimTime total_time = 0;
    for (const auto& [_, t] : totals) total_time += t;

    std::unordered_map<StallReason, double> percents;
    if (total_time == 0) return percents;

    for (const auto& [reason, time] : totals) {
        percents[reason] = 100.0 * static_cast<double>(time) / total_time;
    }
    return percents;
}

std::string StallAttribution::summary() const {
    auto percents = breakdown_percent();
    auto totals = total_by_reason();

    std::ostringstream oss;
    oss << "=== Stall Attribution ===\n";
    oss << std::fixed << std::setprecision(1);

    // Sort by percentage descending
    std::map<double, StallReason, std::greater<double>> sorted;
    for (const auto& [reason, pct] : percents) {
        sorted[pct] = reason;
    }

    for (const auto& [pct, reason] : sorted) {
        SimTime time = totals[reason];
        oss << "  " << stall_reason_name(reason)
            << ": " << pct << "%"
            << " (" << time << " ns)\n";
    }

    oss << "  Total stall records: " << records_.size() << "\n";
    return oss.str();
}

void StallAttribution::reset() {
    records_.clear();
}

} // namespace atmos_sim
