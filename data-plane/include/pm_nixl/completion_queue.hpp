#pragma once#pragma once















































































































































































































































































} // namespace pm_nixl};    std::vector<OpId> op_ids_;    CompletionQueue& queue_;private:        bool all_complete() const;     */     * Check if all operations are complete    /**        std::vector<OpResult> get_results() const;     */     * Get results for all operations    /**        OpId wait_any(uint64_t timeout_ms = 0);     */     * @return Completed operation ID or INVALID_OP_ID     * @param timeout_ms Timeout in milliseconds     *      * Wait for any operation to complete    /**        bool wait_all(uint64_t timeout_ms = 0);     */     * @return true if all completed, false if timeout     * @param timeout_ms Timeout in milliseconds (0 = infinite)     *      * Wait for all operations to complete    /**        void add(OpId op_id);     */     * Add operation to wait list    /**        explicit CompletionWaiter(CompletionQueue& queue);     */     * Construct waiter    /**public:class CompletionWaiter { */ * Simplifies waiting for multiple operations *  * Completion waiter helper/**};    void cleanup_internal();     */     * Cleanup old completed operations (internal, no lock)    /**        OpId generate_op_id();     */     * Generate next operation ID    /**        OpId next_op_id_;    std::queue<OpId> completed_queue_;    std::unordered_map<OpId, OpDescriptor> ops_;    std::condition_variable cv_;    mutable std::mutex mutex_;    CompletionQueueConfig config_;private:        void clear();     */     * Clear all operations    /**        void cleanup();     */     * Cleanup old completed operations    /**        size_t completed_count() const;     */     * Get number of completed operations    /**        size_t pending_count() const;     */     * Get number of pending operations    /**        std::vector<OpId> get_completed_ops() const;     */     * Get all completed operations    /**        std::vector<OpId> get_pending_ops() const;     */     * Get all pending operations    /**        OpId wait_any(uint64_t timeout_ms = 0);     */     * @return Completed operation ID or INVALID_OP_ID on timeout     * @param timeout_ms Timeout in milliseconds     *      * Wait for any operation to complete    /**        OpResult wait(OpId op_id, uint64_t timeout_ms = 0);     */     * @return OpResult with status     * @param timeout_ms Timeout in milliseconds (0 = infinite)     * @param op_id Operation ID to wait for     *      * Wait for completion (blocking)    /**        bool poll(OpId op_id);     */     * @return true if complete, false if still pending     * @param op_id Operation ID to poll     *      * Poll for completion (non-blocking)    /**        bool is_complete(OpId op_id) const;     */     * Check if operation is complete    /**        OpStatus get_status(OpId op_id) const;     */     * Get operation status    /**        const OpDescriptor* get_op(OpId op_id) const;     */     * Get operation descriptor    /**        OpResult cancel(OpId op_id);     */     * Cancel an operation    /**        OpResult mark_timeout(OpId op_id);     */     * Mark operation as timeout    /**        OpResult mark_error(OpId op_id, ErrorCode error, const std::string& message);     */     * Mark operation as error    /**        OpResult mark_complete(OpId op_id);     */     * Mark operation as complete    /**        OpResult mark_running(OpId op_id);     */     * Mark operation as running    /**        OpId submit(const std::string& op_type, size_t size_bytes, void* cuda_event);     */     * @return Operation ID or INVALID_OP_ID on failure     * @param cuda_event CUDA event for completion tracking     * @param size_bytes Size of operation in bytes     * @param op_type Type of operation (for debugging)     *      * Submit a new operation    /**        ~CompletionQueue();     */     * Destructor    /**        explicit CompletionQueue(const CompletionQueueConfig& config = CompletionQueueConfig());     */     * Construct completion queue    /**public:class CompletionQueue { */ * Thread-safe for concurrent submit/poll operations. * Manages operation lifecycle and completion tracking. *  * Completion Queue/**};        , auto_cleanup(true) {}        , max_completed_ops(256)        : max_pending_ops(1024)    CompletionQueueConfig()        bool auto_cleanup;           // Automatically cleanup old completed ops    size_t max_completed_ops;    // Maximum completed ops to retain    size_t max_pending_ops;      // Maximum pending operationsstruct CompletionQueueConfig { */ * Completion queue configuration/**};    }        }            return duration_ms(submit_time, now());        } else {            return duration_ms(submit_time, complete_time);        if (is_complete()) {    double elapsed_ms() const {     */     * Get elapsed time in milliseconds    /**        }        return status == OpStatus::Pending || status == OpStatus::Running;    bool is_active() const {     */     * Check if operation is still pending/running    /**        }               status == OpStatus::Cancelled;               status == OpStatus::Timeout ||               status == OpStatus::Error ||        return status == OpStatus::Complete ||     bool is_complete() const {     */     * Check if operation is complete    /**            , size_bytes(0) {}        , cuda_event(nullptr)        , complete_time()        , start_time()        , submit_time(now())        , status(OpStatus::Pending)        : op_id(INVALID_OP_ID)    OpDescriptor()        size_t size_bytes;    std::string operation_type;    void* cuda_event;  // Opaque CUDA event pointer    Timestamp complete_time;    Timestamp start_time;    Timestamp submit_time;    OpResult result;    OpStatus status;    OpId op_id;struct OpDescriptor { */ * Tracks the lifecycle of a single operation *  * Operation descriptor/**namespace pm_nixl {#include <condition_variable>#include <mutex>#include <unordered_map>#include <queue>#include <vector>#include "transfer.hpp"#include "completion_queue.hpp"
#include <cstdint>
#include <chrono>
#include <string>

namespace pm_nixl {

/**
 * Operation status codes
 */
enum class OpStatus : uint8_t {
    Pending = 0,      // Operation submitted, not started
    Running = 1,      // Operation in progress
    Complete = 2,     // Operation completed successfully
    Error = 3,        // Operation completed with error
    Timeout = 4,      // Operation timed out
    Cancelled = 5     // Operation was cancelled
};

/**
 * Error codes for operations
 */
enum class ErrorCode : uint32_t {
    Success = 0,
    InvalidArgument = 1,
    OutOfMemory = 2,
    CudaError = 3,
    Timeout = 4,
    Cancelled = 5,
    QueueFull = 6,
    NotFound = 7,
    AlreadyComplete = 8,
    InternalError = 9
};

/**
 * Get string representation of status
 */
const char* status_to_string(OpStatus status);

/**
 * Get string representation of error code
 */
const char* error_code_to_string(ErrorCode code);

/**
 * Operation result with error information
 */
struct OpResult {
    ErrorCode error;
    std::string message;
    
    constexpr OpResult()
        : error(ErrorCode::Success) {}
    
    explicit OpResult(ErrorCode e, const std::string& msg = "")
        : error(e), message(msg) {}
    
    bool is_success() const { return error == ErrorCode::Success; }
    bool is_error() const { return error != ErrorCode::Success; }
    
    static OpResult success() { return OpResult(); }
    static OpResult error(ErrorCode e, const std::string& msg = "") {
        return OpResult(e, msg);
    }
};

/**
 * Operation ID type
 */
using OpId = uint64_t;

/**
 * Invalid operation ID constant
 */
constexpr OpId INVALID_OP_ID = 0;

/**
 * Timestamp type
 */
using Timestamp = std::chrono::steady_clock::time_point;

/**
 * Get current timestamp
 */
inline Timestamp now() {
    return std::chrono::steady_clock::now();
}

/**
 * Calculate duration in milliseconds
 */
inline double duration_ms(Timestamp start, Timestamp end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace pm_nixl
