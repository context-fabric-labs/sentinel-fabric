#include <cuda_runtime.h>
#include <device_launch_parameters.h>

namespace pm_nixl {
namespace cuda {

/**
 * CUDA kernel for page gather
 * 
 * Gathers pages from source buffer to destination buffer according to page indices.
 * Each thread block handles one page.
 * 
 * @param dst Destination buffer
 * @param src Source buffer
 * @param page_indices Array of page indices to gather
 * @param page_size Size of each page in bytes
 * @param num_pages Number of pages to gather
 */
__global__ void page_gather_kernel(
    void* dst,
    const void* src,
    const uint32_t* page_indices,
    size_t page_size,
    size_t num_pages
) {
    // Each block handles one page
    size_t page_idx = blockIdx.x;
    
    if (page_idx >= num_pages) {
        return;
    }
    
    // Get the source page index
    uint32_t src_page_idx = page_indices[page_idx];
    
    // Calculate offsets
    const char* src_page = static_cast<const char*>(src) + (src_page_idx * page_size);
    char* dst_page = static_cast<char*>(dst) + (page_idx * page_size);
    
    // Copy page with multiple threads
    // Use 256 threads per block, process multiple elements per thread
    size_t tid = threadIdx.x;
    size_t threads_per_block = blockDim.x;
    
    for (size_t i = tid; i < page_size; i += threads_per_block) {
        dst_page[i] = src_page[i];
    }
}

/**
 * CUDA kernel for page copy (simple contiguous copy)
 * 
 * Copies pages from source to destination.
 * Uses memcpy-style copy with better coalescing.
 * 
 * @param dst Destination buffer
 * @param src Source buffer
 * @param total_size Total size to copy in bytes
 */
__global__ void page_copy_kernel(
    void* dst,
    const void* src,
    size_t total_size
) {
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    size_t stride = blockDim.x * gridDim.x;
    
    const char* src_bytes = static_cast<const char*>(src);
    char* dst_bytes = static_cast<char*>(dst);
    
    for (size_t i = tid; i < total_size; i += stride) {
        dst_bytes[i] = src_bytes[i];
    }
}

/**
 * CUDA kernel for strided page gather
 * 
 * Gathers pages with a stride between source pages.
 * Useful for gathering every Nth page.
 * 
 * @param dst Destination buffer (contiguous)
 * @param src Source buffer (strided)
 * @param page_indices Array of page indices
 * @param page_size Size of each page
 * @param stride Stride between source pages
 * @param num_pages Number of pages to gather
 */
__global__ void page_gather_strided_kernel(
    void* dst,
    const void* src,
    const uint32_t* page_indices,
    size_t page_size,
    size_t stride,
    size_t num_pages
) {
    size_t page_idx = blockIdx.x;
    
    if (page_idx >= num_pages) {
        return;
    }
    
    uint32_t src_page_idx = page_indices[page_idx];
    
    // Strided source offset
    const char* src_page = static_cast<const char*>(src) + (src_page_idx * stride);
    char* dst_page = static_cast<char*>(dst) + (page_idx * page_size);
    
    size_t tid = threadIdx.x;
    size_t threads_per_block = blockDim.x;
    
    for (size_t i = tid; i < page_size; i += threads_per_block) {
        dst_page[i] = src_page[i];
    }
}

/**
 * Launch page gather kernel
 * 
 * @param dst Destination device pointer
 * @param src Source device pointer
 * @param page_indices Device pointer to page indices array
 * @param page_size Size of each page in bytes
 * @param num_pages Number of pages to gather
 * @param stream CUDA stream for async execution
 * @return cudaSuccess on success
 */
cudaError_t launch_page_gather(
    void* dst,
    const void* src,
    const uint32_t* page_indices,
    size_t page_size,
    size_t num_pages,
    cudaStream_t stream = 0
);

/**
 * Launch page copy kernel
 * 
 * @param dst Destination device pointer
 * @param src Source device pointer
 * @param total_size Total size to copy in bytes
 * @param stream CUDA stream for async execution
 * @return cudaSuccess on success
 */
cudaError_t launch_page_copy(
    void* dst,
    const void* src,
    size_t total_size,
    cudaStream_t stream = 0
);

/**
 * Launch strided page gather kernel
 * 
 * @param dst Destination device pointer
 * @param src Source device pointer
 * @param page_indices Device pointer to page indices array
 * @param page_size Size of each page in bytes
 * @param stride Stride between source pages
 * @param num_pages Number of pages to gather
 * @param stream CUDA stream for async execution
 * @return cudaSuccess on success
 */
cudaError_t launch_page_gather_strided(
    void* dst,
    const void* src,
    const uint32_t* page_indices,
    size_t page_size,
    size_t stride,
    size_t num_pages,
    cudaStream_t stream = 0
);

} // namespace cuda
} // namespace pm_nixl
