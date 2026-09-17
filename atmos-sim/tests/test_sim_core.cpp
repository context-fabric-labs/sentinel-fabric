#include <gtest/gtest.h>
#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/event.h"
#include "atmos_sim/sim_core/event_queue.h"
#include "atmos_sim/sim_core/sim_clock.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/resource_pool.h"
#include "atmos_sim/sim_core/bounded_queue.h"
#include "atmos_sim/sim_core/arbiter.h"
#include "atmos_sim/sim_core/dependency_tracker.h"
#include "atmos_sim/sim_core/sim_engine.h"

using namespace atmos_sim;

// ==================== SimTime Tests ====================

TEST(SimTimeTest, UnitConversions) {
    EXPECT_EQ(ns_to_sim(1000), 1000u);
    EXPECT_EQ(us_to_sim(1), 1000u);
    EXPECT_EQ(ms_to_sim(1), 1'000'000u);
    EXPECT_EQ(s_to_sim(1), 1'000'000'000u);
}

TEST(SimTimeTest, TransferTimeBytes) {
    // 1000 bytes at 1000 bytes/sec = 1 second
    EXPECT_EQ(transfer_time_bytes(1000, 1000), 1u);
    // Zero bandwidth returns MAX
    EXPECT_EQ(transfer_time_bytes(1000, 0), SIM_TIME_MAX);
    // Ceiling division: 1001 bytes at 1000 B/s = 2 seconds
    EXPECT_EQ(transfer_time_bytes(1001, 1000), 2u);
}

TEST(SimTimeTest, SequenceCounter) {
    reset_event_sequence();
    EXPECT_EQ(next_event_sequence(), 0u);
    EXPECT_EQ(next_event_sequence(), 1u);
    EXPECT_EQ(next_event_sequence(), 2u);
    reset_event_sequence();
    EXPECT_EQ(next_event_sequence(), 0u);
}

// ==================== Event Tests ====================

TEST(EventTest, Ordering) {
    reset_event_sequence();
    Event a{100, next_event_sequence(), EventType::RESOURCE_ACQUIRED, 1, {}};
    Event b{200, next_event_sequence(), EventType::RESOURCE_RELEASED, 1, {}};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(EventTest, SameTimestampDeterministicOrdering) {
    reset_event_sequence();
    Event a{100, next_event_sequence(), EventType::RESOURCE_ACQUIRED, 1, {}};
    Event b{100, next_event_sequence(), EventType::RESOURCE_RELEASED, 2, {}};
    EXPECT_TRUE(a < b);  // Lower sequence wins
    EXPECT_FALSE(b < a);
}

TEST(EventTest, TypeName) {
    EXPECT_STREQ(event_type_name(EventType::RESOURCE_ACQUIRED), "RESOURCE_ACQUIRED");
    EXPECT_STREQ(event_type_name(EventType::DMA_COMPLETED), "DMA_COMPLETED");
}

// ==================== EventQueue Tests ====================

TEST(EventQueueTest, PushPop) {
    reset_event_sequence();
    EventQueue queue;
    EXPECT_TRUE(queue.empty());

    queue.push({200, next_event_sequence(), EventType::RESOURCE_RELEASED, 1, {}});
    queue.push({100, next_event_sequence(), EventType::RESOURCE_ACQUIRED, 1, {}});
    queue.push({150, next_event_sequence(), EventType::COMPUTE_STARTED, 2, {}});

    EXPECT_EQ(queue.size(), 3u);

    auto e1 = queue.pop();
    EXPECT_EQ(e1.timestamp, 100u);

    auto e2 = queue.pop();
    EXPECT_EQ(e2.timestamp, 150u);

    auto e3 = queue.pop();
    EXPECT_EQ(e3.timestamp, 200u);

    EXPECT_TRUE(queue.empty());
}

TEST(EventQueueTest, DeterministicOrdering) {
    reset_event_sequence();
    EventQueue queue;

    // Same timestamp, different sequences
    queue.push({100, 5, EventType::COMPUTE_STARTED, 1, {}});
    queue.push({100, 3, EventType::COMPUTE_STARTED, 2, {}});
    queue.push({100, 7, EventType::COMPUTE_STARTED, 3, {}});

    EXPECT_EQ(queue.pop().sequence, 3u);
    EXPECT_EQ(queue.pop().sequence, 5u);
    EXPECT_EQ(queue.pop().sequence, 7u);
}

TEST(EventQueueTest, LargeQueue) {
    reset_event_sequence();
    EventQueue queue;

    for (uint64_t i = 0; i < 10000; ++i) {
        queue.push({10000 - i, next_event_sequence(), EventType::CUSTOM, i, {}});
    }

    uint64_t prev_ts = 0;
    while (!queue.empty()) {
        auto e = queue.pop();
        EXPECT_GE(e.timestamp, prev_ts);
        prev_ts = e.timestamp;
    }
}

// ==================== SimClock Tests ====================

TEST(SimClockTest, InitialState) {
    SimClock clock;
    EXPECT_EQ(clock.now(), SIM_TIME_ZERO);
}

TEST(SimClockTest, AdvanceForward) {
    SimClock clock;
    clock.advance_to(100);
    EXPECT_EQ(clock.now(), 100u);
    clock.advance_to(500);
    EXPECT_EQ(clock.now(), 500u);
}

TEST(SimClockTest, CannotGoBackward) {
    SimClock clock;
    clock.advance_to(100);
    EXPECT_THROW(clock.advance_to(50), std::runtime_error);
}

TEST(SimClockTest, Reset) {
    SimClock clock;
    clock.advance_to(1000);
    clock.reset();
    EXPECT_EQ(clock.now(), SIM_TIME_ZERO);
}

// ==================== Resource Tests ====================

TEST(ResourceTest, AcquireRelease) {
    Resource res(1, "test", 3);
    EXPECT_EQ(res.capacity(), 3u);
    EXPECT_EQ(res.available(), 3u);

    EXPECT_TRUE(res.acquire());
    EXPECT_EQ(res.occupancy(), 1u);
    EXPECT_TRUE(res.acquire());
    EXPECT_TRUE(res.acquire());
    EXPECT_FALSE(res.acquire());  // At capacity
    EXPECT_EQ(res.available(), 0u);

    EXPECT_TRUE(res.release());
    EXPECT_EQ(res.available(), 1u);
}

TEST(ResourceTest, StateTransitions) {
    Resource res(1, "test", 1);
    EXPECT_EQ(res.state(), ResourceState::IDLE);

    res.acquire();
    EXPECT_EQ(res.state(), ResourceState::BUSY);

    res.release();
    EXPECT_EQ(res.state(), ResourceState::IDLE);
}

// ==================== ResourcePool Tests ====================

TEST(ResourcePoolTest, BasicOperations) {
    ResourcePool pool("test_pool", 4);
    EXPECT_EQ(pool.total_capacity(), 4u);
    EXPECT_TRUE(pool.is_available());

    EXPECT_TRUE(pool.acquire());
    EXPECT_TRUE(pool.acquire());
    EXPECT_EQ(pool.occupied(), 2u);
    EXPECT_DOUBLE_EQ(pool.utilization(), 0.5);

    EXPECT_TRUE(pool.release());
    EXPECT_EQ(pool.occupied(), 1u);
}

// ==================== BoundedQueue Tests ====================

TEST(BoundedQueueTest, EnqueueDequeue) {
    BoundedQueue q("test_q", 3);
    EXPECT_TRUE(q.is_empty());
    EXPECT_EQ(q.max_depth(), 3u);

    EventPayload p1{100, 0, 0, 0};
    EventPayload p2{200, 0, 0, 0};
    EventPayload p3{300, 0, 0, 0};

    EXPECT_TRUE(q.enqueue(p1));
    EXPECT_TRUE(q.enqueue(p2));
    EXPECT_TRUE(q.enqueue(p3));
    EXPECT_TRUE(q.is_full());
    EXPECT_FALSE(q.enqueue({400, 0, 0, 0}));  // Full

    EXPECT_EQ(q.dequeue().u64, 100u);
    EXPECT_EQ(q.dequeue().u64, 200u);
    EXPECT_EQ(q.dequeue().u64, 300u);
    EXPECT_TRUE(q.is_empty());

    EXPECT_EQ(q.total_enqueued(), 3u);
    EXPECT_EQ(q.peak_depth(), 3u);
}

// ==================== Arbiter Tests ====================

TEST(ArbiterTest, RoundRobin) {
    Arbiter arb(ArbiterPolicy::ROUND_ROBIN, 3);
    EXPECT_EQ(arb.select(), 0u);
    EXPECT_EQ(arb.select(), 1u);
    EXPECT_EQ(arb.select(), 2u);
    EXPECT_EQ(arb.select(), 0u);  // Wraps around
}

TEST(ArbiterTest, Priority) {
    Arbiter arb(ArbiterPolicy::PRIORITY, 3);
    // Lower priority value = higher priority
    std::vector<uint32_t> priorities = {5, 1, 3};
    EXPECT_EQ(arb.select_with_priority(priorities), 1u);
}

// ==================== DependencyTracker Tests ====================

TEST(DependencyTrackerTest, LinearChain) {
    DependencyTracker tracker;
    tracker.register_op(1, {});           // Root
    tracker.register_op(2, {1});          // Depends on 1
    tracker.register_op(3, {2});          // Depends on 2

    EXPECT_TRUE(tracker.is_ready(1));
    EXPECT_FALSE(tracker.is_ready(2));
    EXPECT_FALSE(tracker.is_ready(3));

    auto ready = tracker.satisfy(1);
    EXPECT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0], 2u);
    EXPECT_TRUE(tracker.is_ready(2));

    ready = tracker.satisfy(2);
    EXPECT_EQ(ready[0], 3u);
    EXPECT_TRUE(tracker.is_ready(3));
}

TEST(DependencyTrackerTest, DiamondDependency) {
    DependencyTracker tracker;
    tracker.register_op(1, {});
    tracker.register_op(2, {1});
    tracker.register_op(3, {1});
    tracker.register_op(4, {2, 3});  // Diamond merge

    EXPECT_FALSE(tracker.is_ready(4));
    tracker.satisfy(1);

    // Op 4 should not be ready yet — only satisfied op 2 and 3
    EXPECT_FALSE(tracker.is_ready(4));

    tracker.satisfy(2);
    EXPECT_FALSE(tracker.is_ready(4));  // Still waiting for 3

    auto ready = tracker.satisfy(3);
    EXPECT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0], 4u);
    EXPECT_TRUE(tracker.is_ready(4));
}

TEST(DependencyTrackerTest, DAGValidation) {
    DependencyTracker tracker;
    tracker.register_op(1, {});
    tracker.register_op(2, {1});
    tracker.register_op(3, {2});
    EXPECT_TRUE(tracker.validate_dag());
}

// ==================== SimEngine Tests ====================

TEST(SimEngineTest, BasicEventProcessing) {
    reset_event_sequence();
    SimEngine engine;

    bool handler_called = false;
    engine.register_handler(EventType::CUSTOM, [&](const Event& e, SimClock& clock) -> std::vector<Event> {
        handler_called = true;
        return {};
    });

    engine.schedule({0, next_event_sequence(), EventType::CUSTOM, 1, {}});
    auto result = engine.run();

    EXPECT_TRUE(handler_called);
    EXPECT_EQ(result.events_processed, 1u);
    EXPECT_EQ(result.final_time, 0u);
}

TEST(SimEngineTest, EventChaining) {
    reset_event_sequence();
    SimEngine engine;

    int call_count = 0;
    engine.register_handler(EventType::CUSTOM, [&](const Event& e, SimClock& clock) -> std::vector<Event> {
        ++call_count;
        if (call_count == 1) {
            // Schedule a follow-up event
            return {{100, next_event_sequence(), EventType::CUSTOM, 2, {}}};
        }
        return {};
    });

    engine.schedule({0, next_event_sequence(), EventType::CUSTOM, 1, {}});
    auto result = engine.run();

    EXPECT_EQ(call_count, 2);
    EXPECT_EQ(result.events_processed, 2u);
    EXPECT_EQ(result.final_time, 100u);
}

TEST(SimEngineTest, RunUntil) {
    reset_event_sequence();
    SimEngine engine;

    int processed = 0;
    engine.register_handler(EventType::CUSTOM, [&](const Event& e, SimClock& clock) -> std::vector<Event> {
        ++processed;
        return {{e.timestamp + 10, next_event_sequence(), EventType::CUSTOM, e.target, {}}};
    });

    engine.schedule({0, next_event_sequence(), EventType::CUSTOM, 1, {}});
    auto result = engine.run_until(50);

    EXPECT_EQ(result.final_time, 50u);
    // Events at T=0, 10, 20, 30, 40, 50 should be processed
    EXPECT_EQ(processed, 6);
}

TEST(SimEngineTest, DeterministicReplay) {
    auto run_simulation = []() {
        reset_event_sequence();
        SimEngine engine;

        std::vector<SimTime> timestamps;
        engine.register_handler(EventType::CUSTOM, [&](const Event& e, SimClock& clock) -> std::vector<Event> {
            timestamps.push_back(clock.now());
            if (clock.now() < 100) {
                return {{clock.now() + 10, next_event_sequence(), EventType::CUSTOM, 1, {}}};
            }
            return {};
        });

        engine.schedule({0, next_event_sequence(), EventType::CUSTOM, 1, {}});
        engine.run();
        return timestamps;
    };

    auto run1 = run_simulation();
    auto run2 = run_simulation();

    ASSERT_EQ(run1.size(), run2.size());
    for (size_t i = 0; i < run1.size(); ++i) {
        EXPECT_EQ(run1[i], run2[i]) << "Mismatch at index " << i;
    }
}
