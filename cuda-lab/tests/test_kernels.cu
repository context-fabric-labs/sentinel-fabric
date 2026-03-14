#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <cstring>
#include <vector>

#include "kernels/page_copy_naive.cu"

/**
 * Test: Naive kernel launch
 */
TEST(PageCopyNaiveTest, KernelLaunch) {
    const size_t size = 1024;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Launch kernel
    cudaError_t err = launch_page_copy_naive(d_dst, d_src, size);
    
    EXPECT_EQ(err, cudaSuccess);
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Naive kernel correctness (small size)
 */
TEST(PageCopyNaiveTest, CorrectnessSmall) {
    const size_t size = 256;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(size, 0xAB);
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Launch kernel
    launch_page_copy_naive(d_dst, d_src, size);
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
 * Test: Naive kernel correctness (page-sized)
 */
TEST(PageCopyNaiveTest, CorrectnessPageSized) {
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
    launch_page_copy_naive(d_dst, d_src, page_size);
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
 * Test: Naive kernel correctness (multiple pages)
 */
TEST(PageCopyNaiveTest, CorrectnessMultiplePages) {
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
    launch_page_copy_naive(d_dst, d_src, total_size);
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
 * Test: Naive kernel with zero size
 */
TEST(PageCopyNaiveTest, ZeroSize) {
    void *d_src, *d_dst;
    cudaMalloc(&d_src, 1);
    cudaMalloc(&d_dst, 1);
    
    // Launch with zero size - should succeed
    cudaError_t err = launch_page_copy_naive(d_dst, d_src, 0);
    
    EXPECT_EQ(err, cudaSuccess);
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Naive kernel with large size
 */
TEST(PageCopyNaiveTest, LargeSize) {
    const size_t size = 100 * 1024 * 1024;  // 100MB
    
    void *d_src, *d_dst;
    cudaError_t err;
    
    err = cudaMalloc(&d_src, size);
    if (err == cudaSuccess) {
        err = cudaMalloc(&d_dst, size);
        if (err == cudaSuccess) {
            // Launch kernel
            err = launch_page_copy_naive(d_dst, d_src, size);
            EXPECT_EQ(err, cudaSuccess);
            
            cudaDeviceSynchronize();
            
            cudaFree(d_dst);
        }
        cudaFree(d_src);
    } else {
        // Not enough memory - skip test
        GTEST_SKIP() << "Not enough device memory for 100MB test";
    }
}

/**
 * Test: Kernel name and description
 */
TEST(PageCopyNaiveTest, KernelInfo) {
    const char* name = get_naive_kernel_name();
    const char* desc = get_naive_kernel_description();
    
    EXPECT_STREQ(name, "page_copy_naive");
    EXPECT_STRNE(desc, nullptr);
    
    std::string desc_str(desc);
    EXPECT_TRUE(desc_str.find("naive") != std::string::npos ||
                desc_str.find("baseline") != std::string::npos);
}

/**
 * Test: Concurrent kernel launches (different streams)
 */
TEST(PageCopyNaiveTest, ConcurrentLaunches) {
    const size_t size = 1024;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Create streams
    cudaStream_t streams[4];
    for (int i = 0; i < 4; ++i) {
        cudaStreamCreate(&streams[i]);
    }
    
    // Launch kernel in each stream
    for (int i = 0; i < 4; ++i) {
        cudaError_t err = launch_page_copy_naive(d_dst, d_src, size, streams[i]);
        EXPECT_EQ(err, cudaSuccess);
    }
    
    // Synchronize streams
    for (int i = 0; i < 4; ++i) {
        cudaStreamSynchronize(streams[i]);
        cudaStreamDestroy(streams[i]);
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}
