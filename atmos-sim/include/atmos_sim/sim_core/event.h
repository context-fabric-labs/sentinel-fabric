#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include <cstdint>
#include <string>
#include <variant>
#include <unordered_map>

namespace atmos_sim {

/// Event types for the discrete-event simulation.
enum class EventType : uint8_t {
    // Generic
    RESOURCE_ACQUIRED = 0,
    RESOURCE_RELEASED,
    QUEUE_ENQUEUED,
    QUEUE_DEQUEUED,
    TRANSFER_STARTED,
    TRANSFER_COMPLETED,
    COMPUTE_STARTED,
    COMPUTE_COMPLETED,
    DEPENDENCY_SATISFIED,
    MEMORY_READ_STARTED,
    MEMORY_READ_COMPLETED,
    MEMORY_WRITE_STARTED,
    MEMORY_WRITE_COMPLETED,
    DMA_STARTED,
    DMA_COMPLETED,
    PCIE_TRANSFER_STARTED,
    PCIE_TRANSFER_COMPLETED,
    REQUEST_ARRIVED,
    REQUEST_COMPLETED,
    SCHEDULER_DISPATCHED,
    CUSTOM,
};

/// Human-readable event type name.
const char* event_type_name(EventType type);

/// Resource or entity identifier.
using ResourceId = uint64_t;
constexpr ResourceId INVALID_RESOURCE = 0;

/// Event payload — small variant for zero-allocation common cases.
struct EventPayload {
    uint64_t u64 = 0;
    uint64_t u64_extra = 0;
    ResourceId resource_id = INVALID_RESOURCE;
    ResourceId resource_id_extra = INVALID_RESOURCE;
};

/// A single simulation event.
struct Event {
    SimTime timestamp;
    uint64_t sequence;     // For deterministic ordering when timestamps match
    EventType type;
    ResourceId target;     // Which resource/entity this event targets
    EventPayload payload;

    /// Ordering: timestamp first, then sequence for determinism.
    bool operator<(const Event& other) const;
    bool operator>(const Event& other) const;
    bool operator==(const Event& other) const;
};

} // namespace atmos_sim
