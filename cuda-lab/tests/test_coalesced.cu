#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <cstring>
#include <vector>

#include "kernels/page_copy_coalesced.cu"

/**
 * Test: Coalesced kernel launch
 */
TEST(PageCopyCoalescedTest, KernelLaunch) {
    const size_t size = 1024;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Launch kernel
    cudaError_t err = launch_page_copy_coalesced(d_dst, d_src, size);
    
    EXPECT_EQ(err, cudaSuccess);
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Coalesced kernel correctness (small size)
 */
TEST(PageCopyCoalescedTest, CorrectnessSmall) {
    const size_t size = 256;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(size, 0xAB);
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Launch kernel
    launch_page_copy_coalesced(d_dst, d_src, size);
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
 * Test: Coalesced kernel correctness (page-sized, 16-byte aligned)
 */
TEST(PageCopyCoalescedTest, CorrectnessPageSizedAligned) {
    const size_t page_size = 65536;  // 64KB (multiple of 16)
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, page_size);
    cudaMalloc(&d_dst, page_size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(page_size);
    for (size_t i = 0; i < page_size; ++i) {
        h_src[i] = static_cast<unsigned char>(i & 0xFF);
    }
    cudaMemcpy(d_src, h_src.data(), page_size, cudaMemcpyHostToDevice);
    
    // Verify alignment
    EXPECT_TRUE(is_16byte_aligned(d_src));
    EXPECT_TRUE(is_16byte_aligned(d_dst));
    EXPECT_TRUE(is_size_multiple_of_16(page_size));
    
    // Launch kernel
    launch_page_copy_coalesced(d_dst, d_src, page_size);
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
 * Test: Coalesced kernel correctness (multiple pages)
 */
TEST(PageCopyCoalescedTest, CorrectnessMultiplePages) {
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
    launch_page_copy_coalesced(d_dst, d_src, total_size);
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
 * Test: Coalesced kernel with non-16-byte size
 */
TEST(PageCopyCoalescedTest, NonAlignedSize) {
    const size_t size = 1000;  // Not a multiple of 16
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Initialize source with pattern
    std::vector<unsigned char> h_src(size, 0xCD);
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Launch kernel (should handle remainder correctly)
    launch_page_copy_coalesced(d_dst, d_src, size);
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
 * Test: Coalesced scalar kernel correctness
 */
TEST(PageCopyCoalescedTest, CorrectnessScalar) {
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
    
    // Launch scalar kernel
    launch_page_copy_coalesced_scalar(d_dst, d_src, size);
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
 * Test: Alignment check functions
 */
TEST(PageCopyCoalescedTest, AlignmentChecks) {
    // Test 16-byte alignment
    EXPECT_TRUE(is_16byte_aligned((void*)0x1000));
    EXPECT_TRUE(is_16byte_aligned((void*)0x1010));
    EXPECT_FALSE(is_16byte_aligned((void*)0x1001));
    EXPECT_FALSE(is_16byte_aligned((void*)0x100F));
    
    // Test size multiple of 16
    EXPECT_TRUE(is_size_multiple_of_16(16));
    EXPECT_TRUE(is_size_multiple_of_16(65536));
    EXPECT_FALSE(is_size_multiple_of_16(15));
    EXPECT_FALSE(is_size_multiple_of_16(1000));
}

/**
 * Test: Large size (100MB)
 */
TEST(PageCopyCoalescedTest, LargeSize) {
    const size_t size = 100 * 1024 * 1024;  // 100MB
    
    void *d_src, *d_dst;
    cudaError_t err;
    
    err = cudaMalloc(&d_src, size);
    if (err == cudaSuccess) {
        err = cudaMalloc(&d_dst, size);
        if (err == cudaSuccess) {
            // Launch kernel
            err = launch_page_copy_coalesced(d_dst, d_src, size);
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
 * Test: Kernel info
 */
TEST(PageCopyCoalescedTest, KernelInfo) {
    const char* name = get_coalesced_kernel_name();
    const char* desc = get_coalesced_kernel_description();
    
    EXPECT_STREQ(name, "page_copy_coalesced");
    EXPECT_STRNE(desc, nullptr);
    
    std::string desc_str(desc);
    EXPECT_TRUE(desc_str.find("coalesced") != std::string::npos ||
                desc_str.find("vectorized") != std::string::npos);
}
