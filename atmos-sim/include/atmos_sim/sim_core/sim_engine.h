#pragma once

#include "atmos_sim/sim_core/sim_clock.h"
#include "atmos_sim/sim_core/event_queue.h"
#include "atmos_sim/sim_core/event.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/dependency_tracker.h"

#include <functional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>

namespace atmos_sim {

/// Handler function for events — returns follow-up events to schedule.
using EventHandler = std::function<std::vector<Event>(const Event&, SimClock&)>;

/// Result of a simulation run.
struct SimResult {
    uint64_t events_processed = 0;
    SimTime final_time = 0;
    bool completed_normally = true;

    /// Per-resource utilization data.
    struct ResourceUtilization {
        std::string name;
        double utilization = 0.0;
        SimTime busy_time = 0;
        SimTime idle_time = 0;
    };
    std::vector<ResourceUtilization> resource_utilizations;
};

/// The main simulation engine — orchestrates event processing, resource
/// management, and time advancement. No wall-clock dependency.
class SimEngine {
public:
    SimEngine();

    /// Register an event handler for a specific event type.
    void register_handler(EventType type, EventHandler handler);

    /// Register a resource for utilization tracking.
    void register_resource(std::shared_ptr<Resource> resource);

    /// Schedule an event at a specific time.
    void schedule(Event event);

    /// Schedule an event relative to current time.
    void schedule_after(SimTime delay, Event event);

    /// Run simulation until the event queue is empty.
    SimResult run();

    /// Run simulation until the given time limit.
    SimResult run_until(SimTime time_limit);

    /// Run simulation until a condition is met.
    SimResult run_until_condition(std::function<bool()> condition);

    /// Current simulation time.
    SimTime now() const { return clock_.now(); }

    /// Access the clock.
    const SimClock& clock() const { return clock_; }

    /// Access the dependency tracker.
    DependencyTracker& deps() { return deps_; }

    /// Access a registered resource.
    std::shared_ptr<Resource> get_resource(ResourceId id) const;

    /// Reset engine state for replay.
    void reset();

    /// Number of events currently in the queue.
    size_t pending_events() const { return queue_.size(); }

    /// Total events processed.
    uint64_t events_processed() const { return events_processed_; }

private:
    SimClock clock_;
    EventQueue queue_;
    DependencyTracker deps_;

    std::unordered_map<EventType, std::vector<EventHandler>> handlers_;
    std::unordered_map<ResourceId, std::shared_ptr<Resource>> resources_;

    uint64_t events_processed_ = 0;

    void process_event(const Event& event);
    void update_resource_times(SimTime time);
};

} // namespace atmos_sim
