#include "atmos_sim/sim_core/sim_engine.h"
#include <algorithm>

namespace atmos_sim {

SimEngine::SimEngine() = default;

void SimEngine::register_handler(EventType type, EventHandler handler) {
    handlers_[type].push_back(std::move(handler));
}

void SimEngine::register_resource(std::shared_ptr<Resource> resource) {
    resources_[resource->id()] = std::move(resource);
}

void SimEngine::schedule(Event event) {
    queue_.push(std::move(event));
}

void SimEngine::schedule_after(SimTime delay, Event event) {
    event.timestamp = clock_.now() + delay;
    queue_.push(std::move(event));
}

SimResult SimEngine::run() {
    while (!queue_.empty()) {
        Event event = queue_.pop();
        clock_.advance_to(event.timestamp);
        process_event(event);
        ++events_processed_;
    }

    // Build result with utilization data
    SimResult result;
    result.events_processed = events_processed_;
    result.final_time = clock_.now();
    result.completed_normally = true;

    for (const auto& [id, res] : resources_) {
        result.resource_utilizations.push_back({
            res->name(),
            res->capacity() > 0 ? static_cast<double>(res->occupancy()) / res->capacity() : 0.0,
            res->busy_time(),
            res->idle_time(),
        });
    }
    return result;
}

SimResult SimEngine::run_until(SimTime time_limit) {
    while (!queue_.empty()) {
        const Event& next = queue_.top();
        if (next.timestamp > time_limit) break;

        Event event = queue_.pop();
        clock_.advance_to(event.timestamp);
        process_event(event);
        ++events_processed_;
    }

    SimResult result;
    result.events_processed = events_processed_;
    result.final_time = clock_.now();
    result.completed_normally = queue_.empty();

    for (const auto& [id, res] : resources_) {
        result.resource_utilizations.push_back({
            res->name(),
            res->capacity() > 0 ? static_cast<double>(res->occupancy()) / res->capacity() : 0.0,
            res->busy_time(),
            res->idle_time(),
        });
    }
    return result;
}

SimResult SimEngine::run_until_condition(std::function<bool()> condition) {
    while (!queue_.empty() && !condition()) {
        Event event = queue_.pop();
        clock_.advance_to(event.timestamp);
        process_event(event);
        ++events_processed_;
    }

    SimResult result;
    result.events_processed = events_processed_;
    result.final_time = clock_.now();
    result.completed_normally = condition();
    return result;
}

std::shared_ptr<Resource> SimEngine::get_resource(ResourceId id) const {
    auto it = resources_.find(id);
    if (it == resources_.end()) return nullptr;
    return it->second;
}

void SimEngine::reset() {
    clock_.reset();
    queue_.clear();
    deps_.reset();
    events_processed_ = 0;
    for (auto& [_, res] : resources_) {
        res->set_state(ResourceState::IDLE);
    }
}

void SimEngine::process_event(const Event& event) {
    // Update resource time tracking
    update_resource_times(event.timestamp);

    // Find and invoke handlers
    auto it = handlers_.find(event.type);
    if (it != handlers_.end()) {
        for (auto& handler : it->second) {
            std::vector<Event> follow_ups = handler(event, clock_);
            for (auto& follow_up : follow_ups) {
                queue_.push(std::move(follow_up));
            }
        }
    }
}

void SimEngine::update_resource_times(SimTime time) {
    for (auto& [_, res] : resources_) {
        res->update_time(time);
    }
}

} // namespace atmos_sim
