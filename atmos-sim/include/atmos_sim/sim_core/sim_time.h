#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace atmos_sim {

/// Simulation time in nanoseconds — no floating point to guarantee determinism.
using SimTime = uint64_t;

constexpr SimTime SIM_TIME_ZERO = 0;
constexpr SimTime SIM_TIME_MAX = std::numeric_limits<SimTime>::max();

/// Convert between units (all integer arithmetic).
constexpr SimTime ns_to_sim(uint64_t ns) { return ns; }
constexpr SimTime us_to_sim(uint64_t us) { return us * 1000; }
constexpr SimTime ms_to_sim(uint64_t ms) { return ms * 1'000'000; }
constexpr SimTime s_to_sim(uint64_t s) { return s * 1'000'000'000; }

/// Bandwidth helper: bytes transferred in given time at given bytes/sec rate.
constexpr SimTime transfer_time_bytes(uint64_t bytes, uint64_t bytes_per_sec) {
    if (bytes_per_sec == 0) return SIM_TIME_MAX;
    // Ceiling division to avoid zero-time transfers
    return (bytes + bytes_per_sec - 1) / bytes_per_sec;
}

/// Operation identifier for dependency tracking.
using OpId = uint64_t;
constexpr OpId INVALID_OP = 0;

/// Compute operation classification.
enum class ComputeClass : uint8_t {
    GEMM,
    ATTENTION,
    ELEMENTWISE,
    REDUCE,
    KV_READ,
    KV_WRITE,
    OTHER,
};

const char* compute_class_name(ComputeClass cls);

/// Unique event sequence counter (global, monotonic).
uint64_t next_event_sequence();

/// Reset sequence counter (for deterministic test replay).
void reset_event_sequence();

} // namespace atmos_sim
