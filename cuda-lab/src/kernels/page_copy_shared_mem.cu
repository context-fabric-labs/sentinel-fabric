/**
 * Shared Memory Experiment for Page Copy
 * 
 * IMPORTANT LEARNING POINT:
 * 
 * Shared memory is NOT beneficial for simple page copy operations.
 * 
 * WHY SHARED MEMORY DOESN'T HELP HERE:
 * 
 * 1. NO DATA REUSE
 *    - Each byte is read ONCE from source, written ONCE to destination
 *    - Shared memory helps when data is reused multiple times
 *    - No reuse = no benefit from caching in shared memory
 * 
 * 2. EXTRA COPY OVERHEAD
 *    - Using shared memory requires: global → shared → global
 *    - Direct copy is: global → global
 *    - Extra copy adds latency without benefit
 * 
 * 3. ALREADY COALESCED
 *    - Story 3.2 already achieves coalesced access
 *    - Shared memory for coalescing is unnecessary
 * 
 * 4. MEMORY-BOUND KERNEL
 *    - This kernel is purely memory-bound
 *    - No compute to hide behind memory latency
 *    - Shared memory doesn't increase memory bandwidth
 * 
 * WHEN SHARED MEMORY WOULD HELP:
 * 
 * 1. DATA REUSE PATTERNS
 *    - Multiple threads read same data
 *    - Example: convolution, matrix multiply
 * 
 * 2. REDUCTION OPERATIONS
 *    - Combining values across threads
 *    - Example: sum, max, min reduction
 * 
 * 3. TRANSPOSE/REORDER
 *    - Changing access pattern for better coalescing
 *    - Example: matrix transpose
 * 
 * 4. GATHER/SCATTER WITH REUSE
 *    - If pages are gathered and reused multiple times
 *    - Example: KV cache attention with repeated access
 * 
 * THIS IMPLEMENTATION:
 * 
 * We provide a shared memory version BELOW to demonstrate that it's SLOWER.
 * This is an important learning experience - not all optimizations help!
 * 
 * EXPECTED RESULT: Shared memory version will be 2-5x SLOWER than coalesced.
 */

#include <cuda_runtime.h>
#include <stdio.h>

/**
 * Shared memory page copy kernel (EDUCATIONAL - DO NOT USE IN PRODUCTION)
 * 
 * This kernel uses shared memory even though it doesn't help.
 * Purpose: Demonstrate that shared memory is not always beneficial.
 * 
 * @param dst Destination pointer
 * @param src Source pointer
 * @param size Size in bytes
 */
__global__ void page_copy_shared_mem_kernel(void* dst, const void* src, size_t size) {
    // Declare shared memory
    // Each block will stage data through shared memory
    extern __shared__ char shared_mem[];
    
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    // Each thread copies 16 bytes (same as coalesced version)
    size_t bytes_per_thread = 16;
    size_t thread_start = idx * bytes_per_thread;
    
    if (thread_start < size) {
        // STEP 1: Copy from global to shared memory
        // This is EXTRA work that direct copy doesn't need
        char* src_byte = (const char*)src + thread_start;
        char* shared_byte = shared_mem + (threadIdx.x * bytes_per_thread);
        
        // Check bounds for shared memory copy
        if (thread_start + bytes_per_thread <= size) {
            // Copy 16 bytes to shared memory
            for (int i = 0; i < bytes_per_thread; ++i) {
                shared_byte[i] = src_byte[i];
            }
        } else {
            // Remainder
            size_t remaining = size - thread_start;
            for (int i = 0; i < remaining && i < bytes_per_thread; ++i) {
                shared_byte[i] = src_byte[i];
            }
        }
        
        // STEP 2: Synchronize to ensure all data is in shared memory
        // This adds synchronization overhead
        __syncthreads();
        
        // STEP 3: Copy from shared memory to global memory
        // This is the actual work - everything before was overhead
        char* dst_byte = (char*)dst + thread_start;
        
        if (thread_start + bytes_per_thread <= size) {
            // Copy 16 bytes from shared memory
            for (int i = 0; i < bytes_per_thread; ++i) {
                dst_byte[i] = shared_byte[i];
            }
        } else {
            // Remainder
            size_t remaining = size - thread_start;
            for (int i = 0; i < remaining && i < bytes_per_thread; ++i) {
                dst_byte[i] = shared_byte[i];
            }
        }
    }
}

/**
 * Shared memory page copy kernel with vectorized shared memory access
 * 
 * Slightly better than above but still slower than direct coalesced copy.
 * Uses float4 for shared memory operations.
 * 
 * @param dst Destination pointer
 * @param src Source pointer
 * @param size Size in bytes
 */
__global__ void page_copy_shared_mem_vec_kernel(void* dst, const void* src, size_t size) {
    extern __shared__ char shared_mem[];
    
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    size_t bytes_per_thread = 16;
    size_t thread_start = idx * bytes_per_thread;
    
    if (thread_start < size) {
        // Cast to float4 for vectorized access
        float4* src_vec = (float4*)((const char*)src + thread_start);
        float4* shared_vec = (float4*)(shared_mem + (threadIdx.x * bytes_per_thread));
        float4* dst_vec = (float4*)((char*)dst + thread_start);
        
        if (thread_start + bytes_per_thread <= size) {
            // STEP 1: Global → Shared (extra copy)
            *shared_vec = *src_vec;
            
            // STEP 2: Synchronize (overhead)
            __syncthreads();
            
            // STEP 3: Shared → Global (actual work)
            *dst_vec = *shared_vec;
        } else {
            // Remainder - byte-by-byte
            char* src_byte = (const char*)src + thread_start;
            char* shared_byte = shared_mem + (threadIdx.x * bytes_per_thread);
            char* dst_byte = (char*)dst + thread_start;
            
            size_t remaining = size - thread_start;
            for (size_t i = 0; i < remaining && i < bytes_per_thread; ++i) {
                shared_byte[i] = src_byte[i];
            }
            
            __syncthreads();
            
            for (size_t i = 0; i < remaining && i < bytes_per_thread; ++i) {
                dst_byte[i] = shared_byte[i];
            }
        }
    }
}

/**
 * Launch wrapper for shared memory kernel
 * 
 * @param dst Destination pointer
 * @param src Source pointer
 * @param size Size in bytes
 * @param stream CUDA stream
 * @return cudaSuccess on success
 */
cudaError_t launch_page_copy_shared_mem(
    void* dst,
    const void* src,
    size_t size,
    cudaStream_t stream = 0
) {
    if (size == 0) {
        return cudaSuccess;
    }
    
    const int threads_per_block = 256;
    const size_t bytes_per_thread = 16;
    const size_t total_threads = (size + bytes_per_thread - 1) / bytes_per_thread;
    const int num_blocks = (total_threads + threads_per_block - 1) / threads_per_block;
    
    // Calculate shared memory size
    // Each thread needs 16 bytes of shared memory
    const size_t shared_mem_size = threads_per_block * bytes_per_thread;
    
    const int max_blocks = 65535;
    const int actual_blocks = (num_blocks > max_blocks) ? max_blocks : num_blocks;
    
    // Launch with shared memory
    page_copy_shared_mem_kernel<<<actual_blocks, threads_per_block, shared_mem_size, stream>>>(
        dst, src, size
    );
    
    return cudaGetLastError();
}

/**
 * Launch wrapper for shared memory kernel (vectorized)
 */
cudaError_t launch_page_copy_shared_mem_vec(
    void* dst,
    const void* src,
    size_t size,
    cudaStream_t stream = 0
) {
    if (size == 0) {
        return cudaSuccess;
    }
    
    const int threads_per_block = 256;
    const size_t bytes_per_thread = 16;
    const size_t total_threads = (size + bytes_per_thread - 1) / bytes_per_thread;
    const int num_blocks = (total_threads + threads_per_block - 1) / threads_per_block;
    
    const size_t shared_mem_size = threads_per_block * bytes_per_thread;
    
    const int max_blocks = 65535;
    const int actual_blocks = (num_blocks > max_blocks) ? max_blocks : num_blocks;
    
    page_copy_shared_mem_vec_kernel<<<actual_blocks, threads_per_block, shared_mem_size, stream>>>(
        dst, src, size
    );
    
    return cudaGetLastError();
}

/**
 * Get kernel name
 */
const char* get_shared_mem_kernel_name() {
    return "page_copy_shared_mem";
}

/**
 * Get kernel description
 */
const char* get_shared_mem_kernel_description() {
    return "Shared memory version (EDUCATIONAL - slower than direct coalesced copy)";
}

/**
 * Get vectorized shared memory kernel name
 */
const char* get_shared_mem_vec_kernel_name() {
    return "page_copy_shared_mem_vec";
}

/**
 * Get vectorized shared memory kernel description
 */
const char* get_shared_mem_vec_kernel_description() {
    return "Shared memory with float4 (still slower than direct coalesced)";
}
