/**
 * Naive Page Copy Kernel
 * 
 * This is the BASELINE implementation - intentionally naive to establish
 * a performance baseline for comparison with optimized versions.
 * 
 * KERNEL CHOICE: Page Copy
 * 
 * Why page copy?
 * 1. Simple to understand - copying data is intuitive
 * 2. Memory-bound - perfect for learning memory optimization
 * 3. KV-relevant - directly applicable to KV cache operations
 * 4. Scalable complexity - can add gather/strided access gradually
 * 5. Easy to verify - correctness is obvious
 * 
 * NAIVE IMPLEMENTATION CHARACTERISTICS:
 * - One thread copies one byte (terrible occupancy)
 * - No attention to memory coalescing
 * - No shared memory usage
 * - No optimization whatsoever
 * 
 * This is intentionally bad so we have room to improve!
 */

#include <cuda_runtime.h>
#include <stdio.h>

/**
 * Naive page copy kernel
 * 
 * Each thread copies ONE BYTE.
 * This is intentionally inefficient for baseline measurement.
 * 
 * @param dst Destination pointer (device memory)
 * @param src Source pointer (device memory)
 * @param size Size in bytes
 */
__global__ void page_copy_naive_kernel(void* dst, const void* src, size_t size) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (idx < size) {
        // One thread copies one byte - very inefficient!
        ((char*)dst)[idx] = ((const char*)src)[idx];
    }
}

/**
 * Launch wrapper for naive page copy kernel
 * 
 * @param dst Destination pointer
 * @param src Source pointer
 * @param size Size in bytes
 * @param stream CUDA stream (optional)
 * @return cudaSuccess on success
 */
cudaError_t launch_page_copy_naive(
    void* dst,
    const void* src,
    size_t size,
    cudaStream_t stream = 0
) {
    if (size == 0) {
        return cudaSuccess;
    }
    
    // Configure kernel launch
    // Using 256 threads per block (arbitrary choice)
    const int threads_per_block = 256;
    
    // One thread per byte - this is part of what makes it naive
    // A better implementation would have each thread copy multiple bytes
    const int num_blocks = (size + threads_per_block - 1) / threads_per_block;
    
    // Limit number of blocks for very large transfers
    const int max_blocks = 65535;
    const int actual_blocks = (num_blocks > max_blocks) ? max_blocks : num_blocks;
    
    // Launch kernel
    page_copy_naive_kernel<<<actual_blocks, threads_per_block, 0, stream>>>(
        dst, src, size
    );
    
    return cudaGetLastError();
}

/**
 * Get kernel name
 */
const char* get_naive_kernel_name() {
    return "page_copy_naive";
}

/**
 * Get kernel description
 */
const char* get_naive_kernel_description() {
    return "Naive baseline: one thread copies one byte, no optimization";
}

#ifdef BENCHMARK_MODE
/**
 * Benchmark configuration for naive kernel
 */
struct NaiveKernelConfig {
    size_t page_size;
    size_t num_pages;
    size_t warmup_iterations;
    size_t benchmark_iterations;
    
    NaiveKernelConfig()
        : page_size(65536)  // 64KB default page size
        , num_pages(100)
        , warmup_iterations(10)
        , benchmark_iterations(100) {}
};
#endif
