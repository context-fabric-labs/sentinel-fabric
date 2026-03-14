#include "pm_nixl/completion_queue.hpp"
#include <cuda_runtime.h>
#include <algorithm>

namespace pm_nixl {

const char* status_to_string(OpStatus status) {
    switch (status) {
        case OpStatus::Pending:
            return "Pending";
        case OpStatus::Running:
            return "Running";
        case OpStatus::Complete:
            return "Complete";
        case OpStatus::Error:
            return "Error";
        case OpStatus::Timeout:
            return "Timeout";
        case OpStatus::Cancelled:
            return "Cancelled";
        default:
            return "Unknown";
    }
}

const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::Success:
            return "Success";
        case ErrorCode::InvalidArgument:
            return "Invalid argument";
        case ErrorCode::OutOfMemory:
            return "Out of memory";
        case ErrorCode::CudaError:
            return "CUDA error";
        case ErrorCode::Timeout:
            return "Timeout";
        case ErrorCode::Cancelled:
            return "Cancelled";
        case ErrorCode::QueueFull:
            return "Queue full";
        case ErrorCode::NotFound:
            return "Not found";
        case ErrorCode::AlreadyComplete:
            return "Already complete";
        case ErrorCode::InternalError:
            return "Internal error";
        default:
            return "Unknown error";
    }
}

// CompletionQueue implementation

CompletionQueue::CompletionQueue(const CompletionQueueConfig& config)
    : config_(config)
    , next_op_id_(1) {}

CompletionQueue::~CompletionQueue() {
    clear();
}

OpId CompletionQueue::generate_op_id() {
    return next_op_id_++;
}

OpId CompletionQueue::submit(const std::string& op_type, size_t size_bytes, void* cuda_event) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Check queue capacity
    if (ops_.size() >= config_.max_pending_ops) {
        if (config_.auto_cleanup) {
            cleanup_internal();
        }
        
        // Still full after cleanup
        if (ops_.size() >= config_.max_pending_ops) {
            return INVALID_OP_ID;
        }
    }
    
    // Create operation descriptor
    OpDescriptor op;
    op.op_id = generate_op_id();
    op.status = OpStatus::Pending;
    op.submit_time = now();
    op.cuda_event = cuda_event;
    op.operation_type = op_type;
    op.size_bytes = size_bytes;
    
    // Store operation
    ops_[op.op_id] = op;
    
    return op.op_id;
}

OpResult CompletionQueue::mark_running(OpId op_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return OpResult::error(ErrorCode::NotFound, "Operation not found");
    }
    
    if (it->second.is_complete()) {
        return OpResult::error(ErrorCode::AlreadyComplete, "Operation already complete");
    }
    
    it->second.status = OpStatus::Running;
    it->second.start_time = now();
    
    return OpResult::success();
}

OpResult CompletionQueue::mark_complete(OpId op_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return OpResult::error(ErrorCode::NotFound, "Operation not found");
    }
    
    if (it->second.is_complete()) {
        return OpResult::error(ErrorCode::AlreadyComplete, "Operation already complete");
    }
    
    it->second.status = OpStatus::Complete;
    it->second.complete_time = now();
    it->second.result = OpResult::success();
    
    // Add to completed queue
    completed_queue_.push(op_id);
    
    // Notify waiters
    cv_.notify_all();
    
    // Cleanup if needed
    if (config_.auto_cleanup) {
        cleanup_internal();
    }
    
    return OpResult::success();
}

OpResult CompletionQueue::mark_error(OpId op_id, ErrorCode error, const std::string& message) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return OpResult::error(ErrorCode::NotFound, "Operation not found");
    }
    
    if (it->second.is_complete()) {
        return OpResult::error(ErrorCode::AlreadyComplete, "Operation already complete");
    }
    
    it->second.status = OpStatus::Error;
    it->second.complete_time = now();
    it->second.result = OpResult::error(error, message);
    
    // Add to completed queue
    completed_queue_.push(op_id);
    
    // Notify waiters
    cv_.notify_all();
    
    // Cleanup if needed
    if (config_.auto_cleanup) {
        cleanup_internal();
    }
    
    return OpResult::success();
}

OpResult CompletionQueue::mark_timeout(OpId op_id) {
    return mark_error(op_id, ErrorCode::Timeout, "Operation timed out");
}

OpResult CompletionQueue::cancel(OpId op_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return OpResult::error(ErrorCode::NotFound, "Operation not found");
    }
    
    if (it->second.is_complete()) {
        return OpResult::error(ErrorCode::AlreadyComplete, "Operation already complete");
    }
    
    it->second.status = OpStatus::Cancelled;
    it->second.complete_time = now();
    it->second.result = OpResult::error(ErrorCode::Cancelled, "Operation cancelled");
    
    // Add to completed queue
    completed_queue_.push(op_id);
    
    // Notify waiters
    cv_.notify_all();
    
    // Cleanup if needed
    if (config_.auto_cleanup) {
        cleanup_internal();
    }
    
    return OpResult::success();
}

const OpDescriptor* CompletionQueue::get_op(OpId op_id) const {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

OpStatus CompletionQueue::get_status(OpId op_id) const {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return OpStatus::Error;
    }
    
    return it->second.status;
}

bool CompletionQueue::is_complete(OpId op_id) const {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return false;
    }
    
    return it->second.is_complete();
}

bool CompletionQueue::poll(OpId op_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return false;
    }
    
    // Check CUDA event if available
    if (it->second.cuda_event != nullptr) {
        cudaEvent_t event = static_cast<cudaEvent_t>(it->second.cuda_event);
        cudaError_t err = cudaEventQuery(event);
        
        if (err == cudaSuccess) {
            it->second.status = OpStatus::Complete;
            it->second.complete_time = now();
            it->second.result = OpResult::success();
            completed_queue_.push(op_id);
            cv_.notify_all();
            return true;
        } else if (err == cudaErrorNotReady) {
            return false;
        } else {
            it->second.status = OpStatus::Error;
            it->second.complete_time = now();
            it->second.result = OpResult::error(ErrorCode::CudaError, cudaGetErrorString(err));
            completed_queue_.push(op_id);
            cv_.notify_all();
            return true;
        }
    }
    
    return it->second.is_complete();
}

OpResult CompletionQueue::wait(OpId op_id, uint64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = ops_.find(op_id);
    if (it == ops_.end()) {
        return OpResult::error(ErrorCode::NotFound, "Operation not found");
    }
    
    // Wait for completion
    if (timeout_ms == 0) {
        // Infinite wait
        cv_.wait(lock, [this, op_id]() {
            auto it = ops_.find(op_id);
            return it == ops_.end() || it->second.is_complete();
        });
    } else {
        // Wait with timeout
        bool completed = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this, op_id]() {
            auto it = ops_.find(op_id);
            return it == ops_.end() || it->second.is_complete();
        });
        
        if (!completed) {
            mark_timeout(op_id);
            return OpResult::error(ErrorCode::Timeout, "Wait timed out");
        }
    }
    
    // Return result
    it = ops_.find(op_id);
    if (it == ops_.end()) {
        return OpResult::error(ErrorCode::NotFound, "Operation not found");
    }
    
    return it->second.result;
}

OpId CompletionQueue::wait_any(uint64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto start = now();
    
    while (true) {
        // Check if any completed
        while (!completed_queue_.empty()) {
            OpId op_id = completed_queue_.front();
            auto it = ops_.find(op_id);
            
            if (it != ops_.end() && it->second.is_complete()) {
                return op_id;
            }
            
            completed_queue_.pop();
        }
        
        // Check timeout
        if (timeout_ms > 0) {
            auto elapsed = duration_ms(start, now());
            if (elapsed >= timeout_ms) {
                return INVALID_OP_ID;
            }
        }
        
        // Wait for notification
        if (timeout_ms == 0) {
            cv_.wait(lock);
        } else {
            auto remaining = timeout_ms - static_cast<uint64_t>(duration_ms(start, now()));
            if (remaining == 0) {
                return INVALID_OP_ID;
            }
            cv_.wait_for(lock, std::chrono::milliseconds(remaining));
        }
    }
}

std::vector<OpId> CompletionQueue::get_pending_ops() const {
    std::unique_lock<std::mutex> lock(mutex_);
    
    std::vector<OpId> pending;
    for (const auto& pair : ops_) {
        if (pair.second.is_active()) {
            pending.push_back(pair.first);
        }
    }
    
    return pending;
}

std::vector<OpId> CompletionQueue::get_completed_ops() const {
    std::unique_lock<std::mutex> lock(mutex_);
    
    std::vector<OpId> completed;
    for (const auto& pair : ops_) {
        if (pair.second.is_complete()) {
            completed.push_back(pair.first);
        }
    }
    
    return completed;
}

size_t CompletionQueue::pending_count() const {
    std::unique_lock<std::mutex> lock(mutex_);
    
    size_t count = 0;
    for (const auto& pair : ops_) {
        if (pair.second.is_active()) {
            count++;
        }
    }
    
    return count;
}

size_t CompletionQueue::completed_count() const {
    std::unique_lock<std::mutex> lock(mutex_);
    
    size_t count = 0;
    for (const auto& pair : ops_) {
        if (pair.second.is_complete()) {
            count++;
        }
    }
    
    return count;
}

void CompletionQueue::cleanup() {
    std::unique_lock<std::mutex> lock(mutex_);
    cleanup_internal();
}

void CompletionQueue::cleanup_internal() {
    // Remove old completed operations if over limit
    while (completed_queue_.size() > config_.max_completed_ops) {
        OpId op_id = completed_queue_.front();
        completed_queue_.pop();
        ops_.erase(op_id);
    }
}

void CompletionQueue::clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    ops_.clear();
    
    // Destroy CUDA events
    while (!completed_queue_.empty()) {
        OpId op_id = completed_queue_.front();
        completed_queue_.pop();
        
        auto it = ops_.find(op_id);
        if (it != ops_.end() && it->second.cuda_event != nullptr) {
            cudaEventDestroy(static_cast<cudaEvent_t>(it->second.cuda_event));
        }
    }
}

// CompletionWaiter implementation

CompletionWaiter::CompletionWaiter(CompletionQueue& queue)
    : queue_(queue) {}

void CompletionWaiter::add(OpId op_id) {
    op_ids_.push_back(op_id);
}

bool CompletionWaiter::wait_all(uint64_t timeout_ms) {
    auto start = now();
    
    for (OpId op_id : op_ids_) {
        // Calculate remaining timeout
        uint64_t remaining = 0;
        if (timeout_ms > 0) {
            auto elapsed = duration_ms(start, now());
            if (elapsed >= timeout_ms) {
                return false;
            }
            remaining = timeout_ms - static_cast<uint64_t>(elapsed);
        }
        
        auto result = queue_.wait(op_id, remaining);
        if (!result.is_success()) {
            return false;
        }
    }
    
    return true;
}

OpId CompletionWaiter::wait_any(uint64_t timeout_ms) {
    return queue_.wait_any(timeout_ms);
}

std::vector<OpResult> CompletionWaiter::get_results() const {
    std::vector<OpResult> results;
    
    for (OpId op_id : op_ids_) {
        const OpDescriptor* op = queue_.get_op(op_id);
        if (op != nullptr) {
            results.push_back(op->result);
        } else {
            results.push_back(OpResult::error(ErrorCode::NotFound, "Operation not found"));
        }
    }
    
    return results;
}

bool CompletionWaiter::all_complete() const {
    for (OpId op_id : op_ids_) {
        if (!queue_.is_complete(op_id)) {
            return false;
        }
    }
    
    return true;
}

} // namespace pm_nixl
