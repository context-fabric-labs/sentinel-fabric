#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace atmos_sim {

/// Reasons why an operation was stalled.
enum class StallReason : uint8_t {
    NONE = 0,
    WAIT_INPUT,         // Waiting for input operand(s)
    WAIT_HBF,          // Waiting for HBF memory access
    WAIT_LPDDR,        // Waiting for LPDDR memory access
    WAIT_DMA,          // Waiting for DMA transfer
    WAIT_PCIE,         // Waiting for PCIe transfer
    WAIT_COMPUTE,      // Waiting for compute engine
    WAIT_DEPENDENCY,   // Waiting for dependency satisfaction
    WAIT_SCHEDULER,    // Waiting for scheduler dispatch
    WAIT_SRAM,         // Waiting for SRAM space
};

const char* stall_reason_name(StallReason reason);

/// Per-operation stall record.
struct StallRecord {
    SimTime start_time = 0;
    SimTime end_time = 0;
    StallReason reason = StallReason::NONE;
    uint64_t op_id = 0;

    SimTime duration() const { return end_time - start_time; }
};

/// Stall attribution — tracks why operations were delayed.
class StallAttribution {
public:
    StallAttribution();

    /// Record a stall event.
    void record_stall(SimTime start, SimTime end, StallReason reason, uint64_t op_id = 0);

    /// Get total stall time by reason.
    std::unordered_map<StallReason, SimTime> total_by_reason() const;

    /// Get all stall records.
    const std::vector<StallRecord>& records() const { return records_; }

    /// Get stall breakdown as percentages.
    std::unordered_map<StallReason, double> breakdown_percent() const;

    /// Human-readable summary.
    std::string summary() const;

    /// Clear all records.
    void reset();

private:
    std::vector<StallRecord> records_;
};

} // namespace atmos_sim
