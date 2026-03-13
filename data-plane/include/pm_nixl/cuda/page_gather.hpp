#include "pm_nixl/cuda/page_gather.hpp"

namespace pm_nixl {
namespace cuda {

cudaError_t launch_page_gather(
    void* dst,
    const void* src,
    const uint32_t* page_indices,
    size_t page_size,
    size_t num_pages,
    cudaStream_t stream
) {
    if (num_pages == 0 || page_size == 0) {
        return cudaSuccess;
    }
    
    // Configure kernel launch
    const int threads_per_block = 256;
    const int num_blocks = static_cast<int>(num_pages);
    
    // Launch kernel
    page_gather_kernel<<<num_blocks, threads_per_block, 0, stream>>>(
        dst,
        src,
        page_indices,
        page_size,
        num_pages
    );
    
    return cudaGetLastError();
}

cudaError_t launch_page_copy(
    void* dst,
    const void* src,
    size_t total_size,
    cudaStream_t stream
) {
    if (total_size == 0) {
        return cudaSuccess;
    }
    
    // Configure kernel launch
    const int threads_per_block = 256;
    const int num_blocks = (total_size + threads_per_block - 1) / threads_per_block;
    
    // Limit number of blocks for efficiency
    const int max_blocks = 1024;
    const int actual_blocks = (num_blocks > max_blocks) ? max_blocks : num_blocks;
    
    // Launch kernel
    page_copy_kernel<<<actual_blocks, threads_per_block, 0, stream>>>(
        dst,
        src,
        total_size
    );
    
    return cudaGetLastError();
}

cudaError_t launch_page_gather_strided(
    void* dst,
    const void* src,
    const uint32_t* page_indices,
    size_t page_size,
    size_t stride,
    size_t num_pages,
    cudaStream_t stream
) {
    if (num_pages == 0 || page_size == 0) {
        return cudaSuccess;
    }
    
    // Configure kernel launch
    const int threads_per_block = 256;
    const int num_blocks = static_cast<int>(num_pages);
    
    // Launch kernel
    page_gather_strided_kernel<<<num_blocks, threads_per_block, 0, stream>>>(
        dst,
        src,
        page_indices,
        page_size,
        stride,
        num_pages
    );
    
    return cudaGetLastError();
}

} // namespace cuda
} // namespace pm_nixl
