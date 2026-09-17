#include "atmos_sim/metrics/trace_recorder.h"
#include <sstream>

namespace atmos_sim {

TraceRecorder::TraceRecorder() = default;

void TraceRecorder::record(SimTime timestamp, uint64_t sequence, EventType type,
                            ResourceId target, const std::string& description,
                            const EventPayload& payload) {
    entries_.push_back({timestamp, sequence, type, target, description, payload});
}

void TraceRecorder::clear() {
    entries_.clear();
}

std::string TraceRecorder::to_string() const {
    std::ostringstream oss;
    oss << "=== Simulation Trace (" << entries_.size() << " entries) ===\n";
    for (const auto& entry : entries_) {
        oss << "T=" << entry.timestamp
            << " [" << entry.sequence << "]"
            << " " << event_type_name(entry.type)
            << " target=" << entry.target
            << " " << entry.description << "\n";
    }
    return oss.str();
}

std::string TraceRecorder::to_json() const {
    std::ostringstream oss;
    oss << "[\n";
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        oss << "  {\"ts\":" << e.timestamp
            << ",\"seq\":" << e.sequence
            << ",\"type\":\"" << event_type_name(e.type) << "\""
            << ",\"target\":" << e.target
            << ",\"desc\":\"" << e.description << "\""
            << "}";
        if (i + 1 < entries_.size()) oss << ",";
        oss << "\n";
    }
    oss << "]\n";
    return oss.str();
}

} // namespace atmos_sim
