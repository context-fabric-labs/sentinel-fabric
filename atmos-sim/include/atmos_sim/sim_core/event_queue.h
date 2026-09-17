#pragma once

#include "atmos_sim/sim_core/event.h"
#include <vector>
#include <cstdint>

namespace atmos_sim {

/// Deterministic priority queue for simulation events.
/// Events are ordered by (timestamp, sequence) — the sequence field guarantees
/// that same inputs always produce same ordering, regardless of insertion order.
class EventQueue {
public:
    explicit EventQueue(size_t initial_capacity = 1024);

    /// Push an event into the queue.
    void push(Event event);

    /// Pop the earliest event. Undefined behavior if empty.
    Event pop();

    /// Peek at the earliest event without removing.
    const Event& top() const;

    /// Check if the queue is empty.
    bool empty() const;

    /// Number of events in the queue.
    size_t size() const;

    /// Clear all events.
    void clear();

private:
    std::vector<Event> heap_;

    void sift_up(size_t idx);
    void sift_down(size_t idx);

    static size_t parent(size_t i) { return (i - 1) / 2; }
    static size_t left(size_t i) { return 2 * i + 1; }
    static size_t right(size_t i) { return 2 * i + 2; }
};

} // namespace atmos_sim
