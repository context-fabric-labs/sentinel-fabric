#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <cstring>
#include <vector>

#include "kernels/page_copy_shared_mem.cu"

/**
 * Test: Shared memory kernel launch
 */
TEST(PageCopySharedMemTest, KernelLaunch) {
    const size_t size = 1024;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Launch kernel
    cudaError_t err = launch_page_copy_shared_mem(d_dst, d_src, size);
    
    EXPECT_EQ(err, cudaSuccess);
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Shared memory kernel correctness (small size)
 */
TEST(PageCopySharedMemTest, CorrectnessSmall) {
    const size_t size = 256;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(size, 0xAB);
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Launch kernel
    launch_page_copy_shared_mem(d_dst, d_src, size);
    cudaDeviceSynchronize();
    
    // Copy back and verify
    std::vector<unsigned char> h_dst(size);
    cudaMemcpy(h_dst.data(), d_dst, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(h_dst[i], 0xAB) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Shared memory kernel correctness (page-sized)
 */
TEST(PageCopySharedMemTest, CorrectnessPageSized) {
    const size_t page_size = 65536;  // 64KB
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, page_size);
    cudaMalloc(&d_dst, page_size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(page_size);
    for (size_t i = 0; i < page_size; ++i) {
        h_src[i] = static_cast<unsigned char>(i & 0xFF);
    }
    cudaMemcpy(d_src, h_src.data(), page_size, cudaMemcpyHostToDevice);
    
    // Launch kernel
    launch_page_copy_shared_mem(d_dst, d_src, page_size);
    cudaDeviceSynchronize();
    
    // Copy back and verify
    std::vector<unsigned char> h_dst(page_size);
    cudaMemcpy(h_dst.data(), d_dst, page_size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < page_size; ++i) {
        EXPECT_EQ(h_dst[i], h_src[i]) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Shared memory kernel correctness (multiple pages)
 */
TEST(PageCopySharedMemTest, CorrectnessMultiplePages) {
    const size_t num_pages = 10;
    const size_t page_size = 65536;
    const size_t total_size = num_pages * page_size;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, total_size);
    cudaMalloc(&d_dst, total_size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(total_size);
    for (size_t i = 0; i < total_size; ++i) {
        h_src[i] = static_cast<unsigned char>((i / page_size) & 0xFF);
    }
    cudaMemcpy(d_src, h_src.data(), total_size, cudaMemcpyHostToDevice);
    
    // Launch kernel
    launch_page_copy_shared_mem(d_dst, d_src, total_size);
    cudaDeviceSynchronize();
    
    // Copy back and verify
    std::vector<unsigned char> h_dst(total_size);
    cudaMemcpy(h_dst.data(), d_dst, total_size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < total_size; ++i) {
        EXPECT_EQ(h_dst[i], h_src[i]) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Shared memory vectorized kernel correctness
 */
TEST(PageCopySharedMemTest, CorrectnessVectorized) {
    const size_t size = 65536;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(size);
    for (size_t i = 0; i < size; ++i) {
        h_src[i] = static_cast<unsigned char>(i & 0xFF);
    }
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Launch vectorized shared memory kernel
    launch_page_copy_shared_mem_vec(d_dst, d_src, size);
    cudaDeviceSynchronize();
    
    // Copy back and verify
    std::vector<unsigned char> h_dst(size);
    cudaMemcpy(h_dst.data(), d_dst, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(h_dst[i], h_src[i]) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Shared memory kernel with non-aligned size
 */
TEST(PageCopySharedMemTest, NonAlignedSize) {
    const size_t size = 1000;  // Not a multiple of 16
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(size, 0xCD);
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Launch kernel (should handle remainder correctly)
    launch_page_copy_shared_mem(d_dst, d_src, size);
    cudaDeviceSynchronize();
    
    // Copy back and verify
    std::vector<unsigned char> h_dst(size);
    cudaMemcpy(h_dst.data(), d_dst, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(h_dst[i], 0xCD) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Kernel info
 */
TEST(PageCopySharedMemTest, KernelInfo) {
    const char* name = get_shared_mem_kernel_name();
    const char* desc = get_shared_mem_kernel_description();
    
    EXPECT_STREQ(name, "page_copy_shared_mem");
    EXPECT_STRNE(desc, nullptr);
    
    std::string desc_str(desc);
    EXPECT_TRUE(desc_str.find("shared") != std::string::npos ||
                desc_str.find("educational") != std::string::npos);
}

/**
 * Test: Large size (50MB)
 */
TEST(PageCopySharedMemTest, LargeSize) {
    const size_t size = 50 * 1024 * 1024;  // 50MB
    
    void *d_src, *d_dst;
    cudaError_t err;
    
    err = cudaMalloc(&d_src, size);
    if (err == cudaSuccess) {
        err = cudaMalloc(&d_dst, size);
        if (err == cudaSuccess) {
            // Launch kernel
            err = launch_page_copy_shared_mem(d_dst, d_src, size);
            EXPECT_EQ(err, cudaSuccess);
            
            cudaDeviceSynchronize();
            
            cudaFree(d_dst);
        }
        cudaFree(d_src);
    } else {
        // Not enough memory - skip test
        GTEST_SKIP() << "Not enough device memory for 50MB test";
    }
}
