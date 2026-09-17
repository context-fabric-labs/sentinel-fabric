#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/event.h"
#include <vector>
#include <string>
#include <cstdint>

namespace atmos_sim {

/// A single entry in the simulation trace.
struct TraceEntry {
    SimTime timestamp;
    uint64_t sequence;
    EventType type;
    ResourceId target;
    std::string description;
    EventPayload payload;
};

/// Trace recorder — captures a timestamped log of all simulation events
/// for debugging, golden-trace validation, and post-simulation analysis.
class TraceRecorder {
public:
    TraceRecorder();

    /// Record an event.
    void record(SimTime timestamp, uint64_t sequence, EventType type,
                ResourceId target, const std::string& description,
                const EventPayload& payload = {});

    /// Get all recorded entries.
    const std::vector<TraceEntry>& entries() const { return entries_; }

    /// Number of entries.
    size_t count() const { return entries_.size(); }

    /// Clear all entries.
    void clear();

    /// Export trace as human-readable text.
    std::string to_string() const;

    /// Export trace as JSON.
    std::string to_json() const;

private:
    std::vector<TraceEntry> entries_;
};

} // namespace atmos_sim
