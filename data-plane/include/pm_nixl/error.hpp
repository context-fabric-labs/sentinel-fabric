#pragma once

#include <string>
#include <system_error>

namespace pm_nixl {

/**
 * Error codes for PM-NIXL operations
 */
enum class ErrorCode {
    Success = 0,
    InvalidArgument,
    OutOfMemory,
    CudaError,
    NotRegistered,
    AlreadyRegistered,
    AlignmentError,
    SizeError,
    DeviceError,
    InternalError
};

/**
 * Error category for PM-NIXL errors
 */
class PMNIXLErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "pm_nixl";
    }
    
    std::string message(int ev) const override {
        switch (static_cast<ErrorCode>(ev)) {
            case ErrorCode::Success:
                return "Success";
            case ErrorCode::InvalidArgument:
                return "Invalid argument";
            case ErrorCode::OutOfMemory:
                return "Out of memory";
            case ErrorCode::CudaError:
                return "CUDA runtime error";
            case ErrorCode::NotRegistered:
                return "Buffer not registered";
            case ErrorCode::AlreadyRegistered:
                return "Buffer already registered";
            case ErrorCode::AlignmentError:
                return "Alignment requirement not met";
            case ErrorCode::SizeError:
                return "Invalid size";
            case ErrorCode::DeviceError:
                return "Device error";
            case ErrorCode::InternalError:
                return "Internal error";
            default:
                return "Unknown error";
        }
    }
};

/**
 * Get the PM-NIXL error category
 */
inline const std::error_category& get_error_category() {
    static PMNIXLErrorCategory category;
    return category;
}

/**
 * Create an error_code from ErrorCode
 */
inline std::error_code make_error_code(ErrorCode e) {
    return std::error_code(static_cast<int>(e), get_error_category());
}

/**
 * Exception class for PM-NIXL errors
 */
class PMNIXLError : public std::runtime_error {
public:
    explicit PMNIXLError(ErrorCode code, const std::string& message = "")
        : std::runtime_error(message.empty() ? get_error_category().message(static_cast<int>(code)) 
                                              : message),
          code_(code) {}
    
    ErrorCode code() const noexcept { return code_; }
    
private:
    ErrorCode code_;
};

/**
 * Check CUDA result and throw if error
 */
void check_cuda_result(int result, const char* operation);

/**
 * Result type for operations that can fail
 */
template<typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)), has_value_(true) {}
    Result(std::error_code ec) : ec_(ec), has_value_(false) {}
    
    bool has_value() const noexcept { return has_value_; }
    operator bool() const noexcept { return has_value_; }
    
    T& value() {
        if (!has_value_) {
            throw PMNIXLError(ErrorCode::InternalError, "Result has no value");
        }
        return value_;
    }
    
    const T& value() const {
        if (!has_value_) {
            throw PMNIXLError(ErrorCode::InternalError, "Result has no value");
        }
        return value_;
    }
    
    std::error_code error() const { return ec_; }
    
private:
    union {
        T value_;
        std::error_code ec_;
    };
    bool has_value_;
};

} // namespace pm_nixl

// Enable std::error_code to work with ErrorCode
namespace std {
template<>
struct is_error_code_enum<pm_nixl::ErrorCode> : true_type {};
}
