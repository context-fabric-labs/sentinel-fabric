#include "atmos_sim/sim_core/event_queue.h"
#include <algorithm>
#include <stdexcept>

namespace atmos_sim {

EventQueue::EventQueue(size_t initial_capacity) {
    heap_.reserve(initial_capacity);
}

void EventQueue::push(Event event) {
    heap_.push_back(std::move(event));
    sift_up(heap_.size() - 1);
}

Event EventQueue::pop() {
    if (heap_.empty()) {
        throw std::runtime_error("EventQueue::pop() on empty queue");
    }
    Event earliest = heap_.front();
    heap_.front() = heap_.back();
    heap_.pop_back();
    if (!heap_.empty()) {
        sift_down(0);
    }
    return earliest;
}

const Event& EventQueue::top() const {
    if (heap_.empty()) {
        throw std::runtime_error("EventQueue::top() on empty queue");
    }
    return heap_.front();
}

bool EventQueue::empty() const {
    return heap_.empty();
}

size_t EventQueue::size() const {
    return heap_.size();
}

void EventQueue::clear() {
    heap_.clear();
}

void EventQueue::sift_up(size_t idx) {
    while (idx > 0) {
        size_t p = parent(idx);
        if (heap_[idx] < heap_[p]) {
            std::swap(heap_[idx], heap_[p]);
            idx = p;
        } else {
            break;
        }
    }
}

void EventQueue::sift_down(size_t idx) {
    size_t n = heap_.size();
    while (true) {
        size_t smallest = idx;
        size_t l = left(idx);
        size_t r = right(idx);

        if (l < n && heap_[l] < heap_[smallest]) smallest = l;
        if (r < n && heap_[r] < heap_[smallest]) smallest = r;

        if (smallest != idx) {
            std::swap(heap_[idx], heap_[smallest]);
            idx = smallest;
        } else {
            break;
        }
    }
}

} // namespace atmos_sim
