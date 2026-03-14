/**
 * PyTorch C++ Extension for Page Copy Kernel
 * 
 * This extension exposes the optimized page copy kernel to PyTorch.
 * It demonstrates crossing the ML framework boundary with custom CUDA kernels.
 * 
 * Usage:
 *   import page_copy_ext
 *   dst = page_copy_ext.page_copy(src)
 * 
 * Tradeoffs:
 * - Pros: Direct kernel access, no Python overhead, custom optimizations
 * - Cons: Build complexity, platform-specific, maintenance burden
 * - When to use: Specialized operations not covered by PyTorch
 * - When NOT to use: Simple operations already in PyTorch (e.g., torch.clone)
 */

#include <torch/extension.h>
#include <cuda_runtime.h>
#include <vector>
#include <string>

// CUDA kernel declarations from page_copy_coalesced.cu
cudaError_t launch_page_copy_coalesced(
    void* dst,
    const void* src,
    size_t size,
    cudaStream_t stream = 0
);

/**
 * Check CUDA result and throw exception if failed
 */
void check_cuda(cudaError_t result, const char* operation) {
    if (result != cudaSuccess) {
        throw std::runtime_error(
            std::string("CUDA error: ") + operation + " failed: " + 
            cudaGetErrorString(result)
        );
    }
}

/**
 * Page copy operation
 * 
 * Copies tensor data using optimized CUDA kernel.
 * 
 * Args:
 *     src: Source tensor (CUDA device, contiguous)
 * 
 * Returns:
 *     dst: Destination tensor (copy of src)
 * 
 * Raises:
 *     RuntimeError: If CUDA operation fails
 *     TypeError: If tensor is not on CUDA device
 *     ValueError: If tensor is not contiguous
 */
at::Tensor page_copy(at::Tensor src) {
    // Input validation
    TORCH_CHECK(src.is_cuda(), "Input tensor must be on CUDA device");
    TORCH_CHECK(src.is_contiguous(), "Input tensor must be contiguous");
    
    // Get tensor properties
    auto num_elements = src.numel();
    auto element_size = src.element_size();
    auto total_bytes = num_elements * element_size;
    
    // Create output tensor
    auto dst = torch::empty_like(src);
    
    // Get data pointers
    const void* src_ptr = src.data_ptr();
    void* dst_ptr = dst.data_ptr();
    
    // Launch kernel
    check_cuda(
        launch_page_copy_coalesced(dst_ptr, src_ptr, total_bytes, 0),
        "page_copy_coalesced"
    );
    
    // Synchronize to ensure completion (optional - can be async for performance)
    check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
    
    return dst;
}

/**
 * In-place page copy operation
 * 
 * Copies data from src to dst (both must be same shape).
 * 
 * Args:
 *     dst: Destination tensor (CUDA device, contiguous)
 *     src: Source tensor (CUDA device, contiguous, same shape as dst)
 * 
 * Returns:
 *     dst: Modified in-place
 */
at::Tensor& page_copy_(at::Tensor& dst, const at::Tensor& src) {
    // Input validation
    TORCH_CHECK(dst.is_cuda(), "Destination tensor must be on CUDA device");
    TORCH_CHECK(src.is_cuda(), "Source tensor must be on CUDA device");
    TORCH_CHECK(dst.is_contiguous(), "Destination tensor must be contiguous");
    TORCH_CHECK(src.is_contiguous(), "Source tensor must be contiguous");
    TORCH_CHECK(
        dst.sizes() == src.sizes(),
        "Destination and source must have same shape"
    );
    TORCH_CHECK(
        dst.scalar_type() == src.scalar_type(),
        "Destination and source must have same dtype"
    );
    
    // Get tensor properties
    auto num_elements = src.numel();
    auto element_size = src.element_size();
    auto total_bytes = num_elements * element_size;
    
    // Get data pointers
    const void* src_ptr = src.data_ptr();
    void* dst_ptr = dst.data_ptr();
    
    // Launch kernel
    check_cuda(
        launch_page_copy_coalesced(dst_ptr, src_ptr, total_bytes, 0),
        "page_copy_coalesced_inplace"
    );
    
    // Synchronize
    check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
    
    return dst;
}

/**
 * Get extension info
 */
std::string get_extension_info() {
    return "Page Copy Extension v1.0\n"
           "Optimized CUDA page copy kernel for PyTorch\n"
           "Features:\n"
           "  - Coalesced memory access\n"
           "  - Vectorized 16-byte copies (float4)\n"
           "  - Support for all dtypes\n"
           "  - Support for arbitrary tensor shapes";
}

/**
 * Module definition
 */
PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    m.doc() = "PyTorch C++ Extension for Optimized Page Copy Kernel";
    
    // Page copy operations
    m.def("page_copy", &page_copy, "Copy tensor using optimized CUDA kernel");
    m.def("page_copy_", &page_copy_, "In-place tensor copy using optimized CUDA kernel");
    
    // Info function
    m.def("info", &get_extension_info, "Get extension information");
    
    // Version info
    m.attr("__version__") = "1.0.0";
}
