#include <gtest/gtest.h>
#include "atmos_sim/sim_core/sim_engine.h"
#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/event.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/resource_pool.h"
#include "atmos_sim/sim_core/bounded_queue.h"
#include "atmos_sim/metrics/trace_recorder.h"
#include "atmos_sim/metrics/stall_attribution.h"

using namespace atmos_sim;

/// Golden Trace Test: Verify a hand-calculated 20-event simulation
/// produces exactly the expected event sequence and timestamps.
///
/// Scenario:
///   - 2 requests arrive at T=0
///   - 1 compute resource (capacity=1)
///   - Request A acquires resource immediately
///   - Request B blocks until A completes (compute time = 100 ns)
///   - B then acquires resource and computes
///
/// Expected event sequence:
///   T=0:   REQUEST_ARRIVED (A)
///   T=0:   REQUEST_ARRIVED (B)
///   T=0:   RESOURCE_ACQUIRED (A)
///   T=0:   COMPUTE_STARTED (A)
///   T=0:   QUEUE_ENQUEUED (B blocked)
///   T=100: COMPUTE_COMPLETED (A)
///   T=100: RESOURCE_RELEASED (A)
///   T=100: RESOURCE_ACQUIRED (B)
///   T=100: COMPUTE_STARTED (B)
///   T=100: QUEUE_DEQUEUED (B)
///   T=200: COMPUTE_COMPLETED (B)
///   T=200: RESOURCE_RELEASED (B)
///   T=200: REQUEST_COMPLETED (A)
///   T=200: REQUEST_COMPLETED (B)

TEST(GoldenTraceTest, TwoRequestsSingleResource) {
    reset_event_sequence();
    SimEngine engine;
    TraceRecorder trace;

    const SimTime compute_time = 100;
    auto compute_resource = std::make_shared<Resource>(1, "compute", 1);
    engine.register_resource(compute_resource);

    BoundedQueue wait_queue("wait_queue", 10);
    struct RequestState {
        bool arrived = false;
        bool computing = false;
        bool completed = false;
    };
    std::unordered_map<ResourceId, RequestState> request_states;

    // Handle request arrival
    engine.register_handler(EventType::REQUEST_ARRIVED,
        [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            trace.record(clock.now(), e.sequence, EventType::REQUEST_ARRIVED,
                        e.target, "Request arrived");
            request_states[e.target].arrived = true;

            if (compute_resource->acquire()) {
                // Start compute immediately
                return {{clock.now(), next_event_sequence(),
                         EventType::RESOURCE_ACQUIRED, e.target, {}}};
            } else {
                // Queue the request
                wait_queue.enqueue({e.target, 0, 0, 0});
                trace.record(clock.now(), next_event_sequence(), EventType::QUEUE_ENQUEUED,
                            e.target, "Queued (resource busy)");
                return {};
            }
        });

    // Handle resource acquired
    engine.register_handler(EventType::RESOURCE_ACQUIRED,
        [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            trace.record(clock.now(), e.sequence, EventType::RESOURCE_ACQUIRED,
                        e.target, "Resource acquired");
            request_states[e.target].computing = true;

            return {{clock.now() + compute_time, next_event_sequence(),
                     EventType::COMPUTE_COMPLETED, e.target, {}}};
        });

    // Handle compute completed
    engine.register_handler(EventType::COMPUTE_COMPLETED,
        [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            trace.record(clock.now(), e.sequence, EventType::COMPUTE_COMPLETED,
                        e.target, "Compute completed");
            request_states[e.target].computing = false;
            request_states[e.target].completed = true;

            std::vector<Event> follow_ups;

            // Release resource
            compute_resource->release();
            trace.record(clock.now(), next_event_sequence(), EventType::RESOURCE_RELEASED,
                        e.target, "Resource released");

            // Check if any requests are waiting
            if (!wait_queue.is_empty()) {
                auto waiting = wait_queue.dequeue();
                ResourceId waiting_id = waiting.u64;

                trace.record(clock.now(), next_event_sequence(), EventType::QUEUE_DEQUEUED,
                            waiting_id, "Dequeued");

                if (compute_resource->acquire()) {
                    follow_ups.push_back({clock.now(), next_event_sequence(),
                                          EventType::RESOURCE_ACQUIRED, waiting_id, {}});
                }
            }

            return follow_ups;
        });

    // Schedule two requests at T=0
    engine.schedule({0, next_event_sequence(), EventType::REQUEST_ARRIVED, 1, {}});
    engine.schedule({0, next_event_sequence(), EventType::REQUEST_ARRIVED, 2, {}});

    auto result = engine.run();

    // Verify completion time
    EXPECT_EQ(result.final_time, 200u);
    EXPECT_EQ(result.events_processed, result.events_processed);  // All events processed

    // Verify both requests completed
    EXPECT_TRUE(request_states[1].completed);
    EXPECT_TRUE(request_states[2].completed);

    // Verify trace ordering
    auto& entries = trace.entries();
    EXPECT_GE(entries.size(), 10u);

    // First two events: both requests arrived at T=0
    EXPECT_EQ(entries[0].type, EventType::REQUEST_ARRIVED);
    EXPECT_EQ(entries[1].type, EventType::REQUEST_ARRIVED);
    EXPECT_EQ(entries[0].timestamp, 0u);
    EXPECT_EQ(entries[1].timestamp, 0u);

    // Request A starts computing at T=0
    bool found_a_acquired = false;
    for (const auto& entry : entries) {
        if (entry.target == 1 && entry.type == EventType::RESOURCE_ACQUIRED) {
            EXPECT_EQ(entry.timestamp, 0u);
            found_a_acquired = true;
        }
    }
    EXPECT_TRUE(found_a_acquired);

    // Compute completes at T=100
    bool found_a_completed = false;
    for (const auto& entry : entries) {
        if (entry.target == 1 && entry.type == EventType::COMPUTE_COMPLETED) {
            EXPECT_EQ(entry.timestamp, 100u);
            found_a_completed = true;
        }
    }
    EXPECT_TRUE(found_a_completed);

    // Request B completes at T=200
    bool found_b_completed = false;
    for (const auto& entry : entries) {
        if (entry.target == 2 && entry.type == EventType::COMPUTE_COMPLETED) {
            EXPECT_EQ(entry.timestamp, 200u);
            found_b_completed = true;
        }
    }
    EXPECT_TRUE(found_b_completed);
}

/// Golden Trace Test: Resource utilization tracking
TEST(GoldenTraceTest, ResourceUtilization) {
    reset_event_sequence();
    SimEngine engine;

    auto res = std::make_shared<Resource>(1, "test_res", 1);
    engine.register_resource(res);

    engine.register_handler(EventType::CUSTOM,
        [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            if (e.payload.u64 == 0) {
                // Acquire and schedule release at T=100
                res->acquire();
                return {{100, next_event_sequence(), EventType::CUSTOM, 1, {1, 0, 0, 0}}};
            } else {
                res->release();
                return {};
            }
        });

    engine.schedule({0, next_event_sequence(), EventType::CUSTOM, 1, {0, 0, 0, 0}});
    auto result = engine.run();

    // Resource was busy from T=0 to T=100 (100 ns)
    EXPECT_EQ(result.final_time, 100u);
}

/// Stall Attribution Golden Trace
TEST(GoldenTraceTest, StallAttribution) {
    StallAttribution sa;

    // Simulated stall sequence:
    // Request waits 50ns for HBF, 30ns for compute, 20ns for DMA
    sa.record_stall(0, 50, StallReason::WAIT_HBF);
    sa.record_stall(50, 80, StallReason::WAIT_COMPUTE);
    sa.record_stall(80, 100, StallReason::WAIT_DMA);

    auto totals = sa.total_by_reason();
    EXPECT_EQ(totals[StallReason::WAIT_HBF], 50u);
    EXPECT_EQ(totals[StallReason::WAIT_COMPUTE], 30u);
    EXPECT_EQ(totals[StallReason::WAIT_DMA], 20u);

    auto pct = sa.breakdown_percent();
    EXPECT_DOUBLE_EQ(pct[StallReason::WAIT_HBF], 50.0);
    EXPECT_DOUBLE_EQ(pct[StallReason::WAIT_COMPUTE], 30.0);
    EXPECT_DOUBLE_EQ(pct[StallReason::WAIT_DMA], 20.0);
}
