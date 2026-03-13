#include "pm_nixl/error.hpp"
#include <cuda_runtime.h>

namespace pm_nixl {

void check_cuda_result(int result, const char* operation) {
    if (result != cudaSuccess) {
        throw PMNIXLError(ErrorCode::CudaError, 
            std::string(operation) + ": " + cudaGetErrorString(static_cast<cudaError_t>(result)));
    }
}

} // namespace pm_nixl
