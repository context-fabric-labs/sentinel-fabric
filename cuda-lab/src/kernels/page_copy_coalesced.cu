/**
 * Coalesced Page Copy Kernel
 * 
 * OPTIMIZATION: Memory Coalescing
 * 
 * This kernel improves on the naive baseline by:
 * 1. Having each thread copy multiple bytes (better work per thread)
 * 2. Ensuring consecutive threads access consecutive memory (coalescing)
 * 3. Using vectorized loads/stores when alignment permits (float4 = 16 bytes)
 * 
 * WHY THIS IS FASTER:
 * - Naive: 1 thread = 1 byte (256 bytes per block with 256 threads)
 * - Coalesced: 1 thread = 16 bytes (4096 bytes per block with 256 threads)
 * - 16x more work per thread launch
 * - Better memory coalescing → fewer memory transactions
 * - Vectorized access (float4) → 128-bit memory operations
 * 
 * EXPECTED IMPROVEMENT: 10-50x over naive baseline
 */

#include <cuda_runtime.h>
#include <stdio.h>

/**
 * Coalesced page copy kernel with vectorized access
 * 
 * Each thread copies 16 bytes (using float4 vector type).
 * Threads are organized to ensure coalesced memory access.
 * 
 * @param dst Destination pointer (device memory, should be 16-byte aligned)
 * @param src Source pointer (device memory, should be 16-byte aligned)
 * @param size Size in bytes (should be multiple of 16 for best performance)
 */
__global__ void page_copy_coalesced_kernel(void* dst, const void* src, size_t size) {
    // Calculate global thread index
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    // Each thread copies 16 bytes (float4)
    // This gives us 16x more work per thread than the naive version
    size_t bytes_per_thread = 16;
    size_t thread_start = idx * bytes_per_thread;
    
    // Bounds check
    if (thread_start < size) {
        // Cast to float4* for vectorized access
        // float4 is 16 bytes and can be loaded/stored in a single instruction
        float4* dst_vec = (float4*)((char*)dst + thread_start);
        const float4* src_vec = (const float4*)((const char*)src + thread_start);
        
        // Check if we have a full 16 bytes to copy
        if (thread_start + bytes_per_thread <= size) {
            // Vectorized copy - 16 bytes in one operation
            *dst_vec = *src_vec;
        } else {
            // Handle remainder (less than 16 bytes remaining)
            // Copy byte-by-byte for the remainder
            char* dst_byte = (char*)dst + thread_start;
            const char* src_byte = (const char*)src + thread_start;
            
            size_t remaining = size - thread_start;
            for (size_t i = 0; i < remaining && i < bytes_per_thread; ++i) {
                dst_byte[i] = src_byte[i];
            }
        }
    }
}

/**
 * Coalesced page copy kernel (scalar version)
 * 
 * Same as above but uses scalar loads/stores instead of vectorized.
 * Useful for comparison to measure vectorization benefit.
 * 
 * @param dst Destination pointer
 * @param src Source pointer
 * @param size Size in bytes
 */
__global__ void page_copy_coalesced_scalar_kernel(void* dst, const void* src, size_t size) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    // Each thread copies 4 bytes (float)
    // Still coalesced, but not vectorized
    size_t bytes_per_thread = 4;
    size_t thread_start = idx * bytes_per_thread;
    
    if (thread_start < size) {
        if (thread_start + bytes_per_thread <= size) {
            // 4-byte copy
            float* dst_f = (float*)((char*)dst + thread_start);
            const float* src_f = (const float*)((const char*)src + thread_start);
            *dst_f = *src_f;
        } else {
            // Remainder
            char* dst_byte = (char*)dst + thread_start;
            const char* src_byte = (const char*)src + thread_start;
            
            size_t remaining = size - thread_start;
            for (size_t i = 0; i < remaining && i < bytes_per_thread; ++i) {
                dst_byte[i] = src_byte[i];
            }
        }
    }
}

/**
 * Launch wrapper for coalesced page copy kernel (vectorized)
 * 
 * @param dst Destination pointer (should be 16-byte aligned)
 * @param src Source pointer (should be 16-byte aligned)
 * @param size Size in bytes (should be multiple of 16 for best performance)
 * @param stream CUDA stream (optional)
 * @return cudaSuccess on success
 */
cudaError_t launch_page_copy_coalesced(
    void* dst,
    const void* src,
    size_t size,
    cudaStream_t stream = 0
) {
    if (size == 0) {
        return cudaSuccess;
    }
    
    // Configure kernel launch
    const int threads_per_block = 256;
    
    // Each thread copies 16 bytes, so we need fewer blocks
    const size_t bytes_per_thread = 16;
    const size_t total_threads = (size + bytes_per_thread - 1) / bytes_per_thread;
    const int num_blocks = (total_threads + threads_per_block - 1) / threads_per_block;
    
    // Limit number of blocks
    const int max_blocks = 65535;
    const int actual_blocks = (num_blocks > max_blocks) ? max_blocks : num_blocks;
    
    // Launch kernel
    page_copy_coalesced_kernel<<<actual_blocks, threads_per_block, 0, stream>>>(
        dst, src, size
    );
    
    return cudaGetLastError();
}

/**
 * Launch wrapper for coalesced page copy kernel (scalar version)
 * 
 * @param dst Destination pointer
 * @param src Source pointer
 * @param size Size in bytes
 * @param stream CUDA stream (optional)
 * @return cudaSuccess on success
 */
cudaError_t launch_page_copy_coalesced_scalar(
    void* dst,
    const void* src,
    size_t size,
    cudaStream_t stream = 0
) {
    if (size == 0) {
        return cudaSuccess;
    }
    
    const int threads_per_block = 256;
    
    // Each thread copies 4 bytes
    const size_t bytes_per_thread = 4;
    const size_t total_threads = (size + bytes_per_thread - 1) / bytes_per_thread;
    const int num_blocks = (total_threads + threads_per_block - 1) / threads_per_block;
    
    const int max_blocks = 65535;
    const int actual_blocks = (num_blocks > max_blocks) ? max_blocks : num_blocks;
    
    page_copy_coalesced_scalar_kernel<<<actual_blocks, threads_per_block, 0, stream>>>(
        dst, src, size
    );
    
    return cudaGetLastError();
}

/**
 * Get kernel name (vectorized)
 */
const char* get_coalesced_kernel_name() {
    return "page_copy_coalesced";
}

/**
 * Get kernel description (vectorized)
 */
const char* get_coalesced_kernel_description() {
    return "Coalesced: each thread copies 16 bytes with vectorized float4 access";
}

/**
 * Get kernel name (scalar)
 */
const char* get_coalesced_scalar_kernel_name() {
    return "page_copy_coalesced_scalar";
}

/**
 * Get kernel description (scalar)
 */
const char* get_coalesced_scalar_kernel_description() {
    return "Coalesced scalar: each thread copies 4 bytes without vectorization";
}

/**
 * Check if pointer is aligned to 16 bytes
 */
__host__ __device__ inline bool is_16byte_aligned(const void* ptr) {
    return ((size_t)ptr % 16) == 0;
}

/**
 * Check if size is multiple of 16
 */
__host__ __device__ inline bool is_size_multiple_of_16(size_t size) {
    return (size % 16) == 0;
}
