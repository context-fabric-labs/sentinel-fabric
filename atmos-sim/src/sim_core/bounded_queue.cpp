#include "atmos_sim/sim_core/bounded_queue.h"
#include <stdexcept>

namespace atmos_sim {

BoundedQueue::BoundedQueue(const std::string& name, uint32_t max_depth)
    : name_(name), max_depth_(max_depth) {}

bool BoundedQueue::enqueue(EventPayload item) {
    if (is_full()) return false;
    queue_.push_back(std::move(item));
    ++total_enqueued_;
    uint32_t depth = current_depth();
    if (depth > peak_depth_) peak_depth_ = depth;
    return true;
}

EventPayload BoundedQueue::dequeue() {
    if (queue_.empty()) {
        throw std::runtime_error("BoundedQueue::dequeue() on empty queue: " + name_);
    }
    EventPayload front = queue_.front();
    queue_.pop_front();
    ++total_dequeued_;
    return front;
}

const EventPayload& BoundedQueue::front() const {
    if (queue_.empty()) {
        throw std::runtime_error("BoundedQueue::front() on empty queue: " + name_);
    }
    return queue_.front();
}

} // namespace atmos_sim
