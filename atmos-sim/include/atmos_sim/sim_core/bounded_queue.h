#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/event.h"
#include <cstdint>
#include <deque>
#include <string>

namespace atmos_sim {

/// Finite-capacity FIFO queue that emits enqueue/dequeue events.
class BoundedQueue {
public:
    BoundedQueue(const std::string& name, uint32_t max_depth);

    const std::string& name() const { return name_; }
    uint32_t max_depth() const { return max_depth_; }
    uint32_t current_depth() const { return static_cast<uint32_t>(queue_.size()); }
    uint32_t available_slots() const { return max_depth_ - current_depth(); }
    bool is_full() const { return current_depth() >= max_depth_; }
    bool is_empty() const { return queue_.empty(); }

    /// Enqueue an item. Returns false if queue is full.
    bool enqueue(EventPayload item);

    /// Dequeue the front item. Undefined if empty.
    EventPayload dequeue();

    /// Peek at the front item without removing.
    const EventPayload& front() const;

    /// Total items enqueued (lifetime counter).
    uint64_t total_enqueued() const { return total_enqueued_; }

    /// Total items dequeued (lifetime counter).
    uint64_t total_dequeued() const { return total_dequeued_; }

    /// Peak depth reached.
    uint32_t peak_depth() const { return peak_depth_; }

private:
    std::string name_;
    uint32_t max_depth_;
    std::deque<EventPayload> queue_;
    uint64_t total_enqueued_ = 0;
    uint64_t total_dequeued_ = 0;
    uint32_t peak_depth_ = 0;
};

} // namespace atmos_sim
