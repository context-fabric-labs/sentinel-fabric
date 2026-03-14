#include <pm_nixl/completion_queue.hpp>
#include <gtest/gtest.h>
#include <thread>

using namespace pm_nixl;

/**
 * Test: Status and error code string conversion
 */
TEST(CompletionQueueTest, StatusStringConversion) {
    EXPECT_STREQ(status_to_string(OpStatus::Pending), "Pending");
    EXPECT_STREQ(status_to_string(OpStatus::Running), "Running");
    EXPECT_STREQ(status_to_string(OpStatus::Complete), "Complete");
    EXPECT_STREQ(status_to_string(OpStatus::Error), "Error");
    EXPECT_STREQ(status_to_string(OpStatus::Timeout), "Timeout");
    EXPECT_STREQ(status_to_string(OpStatus::Cancelled), "Cancelled");
}

/**
 * Test: Error code string conversion
 */
TEST(CompletionQueueTest, ErrorCodeStringConversion) {
    EXPECT_STREQ(error_code_to_string(ErrorCode::Success), "Success");
    EXPECT_STREQ(error_code_to_string(ErrorCode::InvalidArgument), "Invalid argument");
    EXPECT_STREQ(error_code_to_string(ErrorCode::Timeout), "Timeout");
    EXPECT_STREQ(error_code_to_string(ErrorCode::Cancelled), "Cancelled");
}

/**
 * Test: OpResult construction
 */
TEST(CompletionQueueTest, OpResultConstruction) {
    OpResult result1;
    EXPECT_TRUE(result1.is_success());
    EXPECT_FALSE(result1.is_error());
    
    OpResult result2(ErrorCode::Timeout, "Test timeout");
    EXPECT_FALSE(result2.is_success());
    EXPECT_TRUE(result2.is_error());
    EXPECT_EQ(result2.error, ErrorCode::Timeout);
    EXPECT_EQ(result2.message, "Test timeout");
}

/**
 * Test: OpDescriptor default construction
 */
TEST(CompletionQueueTest, OpDescriptorDefault) {
    OpDescriptor op;
    EXPECT_EQ(op.op_id, INVALID_OP_ID);
    EXPECT_EQ(op.status, OpStatus::Pending);
    EXPECT_TRUE(op.is_active());
    EXPECT_FALSE(op.is_complete());
}

/**
 * Test: Completion queue construction
 */
TEST(CompletionQueueTest, QueueConstruction) {
    CompletionQueueConfig config;
    config.max_pending_ops = 100;
    config.max_completed_ops = 50;
    
    CompletionQueue queue(config);
    
    EXPECT_EQ(queue.pending_count(), 0);
    EXPECT_EQ(queue.completed_count(), 0);
}

/**
 * Test: Submit operation
 */
TEST(CompletionQueueTest, SubmitOperation) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    EXPECT_NE(op_id, INVALID_OP_ID);
    EXPECT_EQ(queue.pending_count(), 1);
    
    const OpDescriptor* op = queue.get_op(op_id);
    EXPECT_NE(op, nullptr);
    EXPECT_EQ(op->status, OpStatus::Pending);
    EXPECT_EQ(op->size_bytes, 1024);
}

/**
 * Test: Mark operation running
 */
TEST(CompletionQueueTest, MarkRunning) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    auto result = queue.mark_running(op_id);
    EXPECT_TRUE(result.is_success());
    
    EXPECT_EQ(queue.get_status(op_id), OpStatus::Running);
}

/**
 * Test: Mark operation complete
 */
TEST(CompletionQueueTest, MarkComplete) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    queue.mark_running(op_id);
    
    auto result = queue.mark_complete(op_id);
    EXPECT_TRUE(result.is_success());
    
    EXPECT_EQ(queue.get_status(op_id), OpStatus::Complete);
    EXPECT_TRUE(queue.is_complete(op_id));
    EXPECT_EQ(queue.completed_count(), 1);
}

/**
 * Test: Mark operation error
 */
TEST(CompletionQueueTest, MarkError) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    auto result = queue.mark_error(op_id, ErrorCode::CudaError, "Test error");
    EXPECT_TRUE(result.is_success());
    
    EXPECT_EQ(queue.get_status(op_id), OpStatus::Error);
    
    const OpDescriptor* op = queue.get_op(op_id);
    EXPECT_EQ(op->result.error, ErrorCode::CudaError);
    EXPECT_EQ(op->result.message, "Test error");
}

/**
 * Test: Mark operation timeout
 */
TEST(CompletionQueueTest, MarkTimeout) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    auto result = queue.mark_timeout(op_id);
    EXPECT_TRUE(result.is_success());
    
    EXPECT_EQ(queue.get_status(op_id), OpStatus::Timeout);
}

/**
 * Test: Cancel operation
 */
TEST(CompletionQueueTest, CancelOperation) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    auto result = queue.cancel(op_id);
    EXPECT_TRUE(result.is_success());
    
    EXPECT_EQ(queue.get_status(op_id), OpStatus::Cancelled);
}

/**
 * Test: Invalid operation ID
 */
TEST(CompletionQueueTest, InvalidOpId) {
    CompletionQueue queue;
    
    // Try to mark complete with invalid ID
    auto result = queue.mark_complete(INVALID_OP_ID);
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error, ErrorCode::NotFound);
    
    // Try to get status with invalid ID
    EXPECT_EQ(queue.get_status(INVALID_OP_ID), OpStatus::Error);
    
    // Try to cancel with invalid ID
    result = queue.cancel(INVALID_OP_ID);
    EXPECT_FALSE(result.is_success());
}

/**
 * Test: Already complete error
 */
TEST(CompletionQueueTest, AlreadyCompleteError) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    queue.mark_complete(op_id);
    
    // Try to mark complete again
    auto result = queue.mark_complete(op_id);
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error, ErrorCode::AlreadyComplete);
    
    // Try to cancel
    result = queue.cancel(op_id);
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error, ErrorCode::AlreadyComplete);
}

/**
 * Test: Get pending and completed ops
 */
TEST(CompletionQueueTest, GetPendingAndCompletedOps) {
    CompletionQueue queue;
    
    // Submit 5 operations
    std::vector<OpId> op_ids;
    for (int i = 0; i < 5; ++i) {
        op_ids.push_back(queue.submit("test_op", 1024, nullptr));
    }
    
    // Complete 2 operations
    queue.mark_complete(op_ids[0]);
    queue.mark_complete(op_ids[1]);
    
    auto pending = queue.get_pending_ops();
    auto completed = queue.get_completed_ops();
    
    EXPECT_EQ(pending.size(), 3);
    EXPECT_EQ(completed.size(), 2);
}

/**
 * Test: Queue cleanup
 */
TEST(CompletionQueueTest, QueueCleanup) {
    CompletionQueueConfig config;
    config.max_completed_ops = 2;
    config.auto_cleanup = true;
    
    CompletionQueue queue(config);
    
    // Submit and complete 5 operations
    for (int i = 0; i < 5; ++i) {
        OpId op_id = queue.submit("test_op", 1024, nullptr);
        queue.mark_complete(op_id);
    }
    
    // Should have cleaned up old operations
    EXPECT_LE(queue.completed_count(), 2);
}

/**
 * Test: Queue clear
 */
TEST(CompletionQueueTest, QueueClear) {
    CompletionQueue queue;
    
    // Submit 10 operations
    for (int i = 0; i < 10; ++i) {
        queue.submit("test_op", 1024, nullptr);
    }
    
    EXPECT_EQ(queue.pending_count(), 10);
    
    queue.clear();
    
    EXPECT_EQ(queue.pending_count(), 0);
    EXPECT_EQ(queue.completed_count(), 0);
}

/**
 * Test: Poll operation (without CUDA)
 */
TEST(CompletionQueueTest, PollOperation) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    // Poll before complete
    EXPECT_FALSE(queue.poll(op_id));
    
    // Mark complete
    queue.mark_complete(op_id);
    
    // Poll after complete
    EXPECT_TRUE(queue.poll(op_id));
}

/**
 * Test: Wait for operation (non-blocking)
 */
TEST(CompletionQueueTest, WaitNonBlocking) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    // Complete in separate thread
    std::thread t([&queue, op_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        queue.mark_complete(op_id);
    });
    
    // Wait with timeout
    auto result = queue.wait(op_id, 1000);
    EXPECT_TRUE(result.is_success());
    
    t.join();
}

/**
 * Test: Wait for operation with timeout
 */
TEST(CompletionQueueTest, WaitWithTimeout) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    // Wait with short timeout (operation never completes)
    auto result = queue.wait(op_id, 10);
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error, ErrorCode::Timeout);
}

/**
 * Test: Wait any operation
 */
TEST(CompletionQueueTest, WaitAny) {
    CompletionQueue queue;
    
    // Submit 3 operations
    std::vector<OpId> op_ids;
    for (int i = 0; i < 3; ++i) {
        op_ids.push_back(queue.submit("test_op", 1024, nullptr));
    }
    
    // Complete one in separate thread
    std::thread t([&queue, &op_ids]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        queue.mark_complete(op_ids[1]);
    });
    
    // Wait for any
    OpId completed_id = queue.wait_any(1000);
    EXPECT_EQ(completed_id, op_ids[1]);
    
    t.join();
}

/**
 * Test: Completion waiter
 */
TEST(CompletionQueueTest, CompletionWaiter) {
    CompletionQueue queue;
    CompletionWaiter waiter(queue);
    
    // Add 3 operations
    std::vector<OpId> op_ids;
    for (int i = 0; i < 3; ++i) {
        OpId op_id = queue.submit("test_op", 1024, nullptr);
        waiter.add(op_id);
        op_ids.push_back(op_id);
    }
    
    // Check not all complete
    EXPECT_FALSE(waiter.all_complete());
    
    // Complete all
    for (OpId op_id : op_ids) {
        queue.mark_complete(op_id);
    }
    
    // Check all complete
    EXPECT_TRUE(waiter.all_complete());
    
    // Get results
    auto results = waiter.get_results();
    EXPECT_EQ(results.size(), 3);
    
    for (const auto& result : results) {
        EXPECT_TRUE(result.is_success());
    }
}

/**
 * Test: Elapsed time tracking
 */
TEST(CompletionQueueTest, ElapsedTimeTracking) {
    CompletionQueue queue;
    
    OpId op_id = queue.submit("test_op", 1024, nullptr);
    
    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    queue.mark_complete(op_id);
    
    const OpDescriptor* op = queue.get_op(op_id);
    EXPECT_GT(op->elapsed_ms(), 10.0);
}
