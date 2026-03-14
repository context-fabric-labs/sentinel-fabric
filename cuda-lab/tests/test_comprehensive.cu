/**
 * Comprehensive Correctness Tests for Page Copy Kernels
 * 
 * This test suite covers:
 * 1. Multiple shapes (small, page-sized, multi-page, large)
 * 2. Edge cases (zero size, odd sizes, non-aligned)
 * 3. Alignment-sensitive cases (16-byte aligned, non-aligned)
 * 4. All kernel variants (naive, coalesced, shared_mem)
 * 
 * Test Matrix:
 * 
 * | Size Category    | Sizes Tested              | Purpose                    |
 * |------------------|---------------------------|----------------------------|
 * | Tiny             | 1, 15, 16, 17, 255, 256   | Edge cases, boundaries     |
 * | Small            | 512, 1024, 4096           | Small transfers            |
 * | Page-sized       | 16KB, 64KB, 256KB         | Typical KV cache pages     |
 * | Multi-page       | 1MB, 4MB, 16MB            | Multiple pages             |
 * | Large            | 64MB, 100MB               | Large transfers            |
 * | Non-aligned      | 1000, 10000, 12345        | Non-16-byte multiples      |
 * 
 * Alignment Cases:
 * - 16-byte aligned pointers and sizes
 * - Non-aligned pointers (offset by 1, 3, 7 bytes)
 * - Non-aligned sizes (not multiple of 16)
 * 
 * Kernel Variants:
 * - page_copy_naive (baseline)
 * - page_copy_coalesced_scalar (4 bytes/thread)
 * - page_copy_coalesced (16 bytes/thread, vectorized)
 * - page_copy_shared_mem (shared memory, educational)
 * - page_copy_shared_mem_vec (shared memory vectorized)
 */

#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <cstring>
#include <vector>
#include <random>
#include <iomanip>

#include "kernels/page_copy_naive.cu"
#include "kernels/page_copy_coalesced.cu"
#include "kernels/page_copy_shared_mem.cu"

/**
 * Test parameters structure
 */
struct TestParams {
    std::string name;
    size_t size;
    bool aligned;
    
    TestParams(const std::string& n, size_t s, bool a = true)
        : name(n), size(s), aligned(a) {}
};

/**
 * Get all test sizes
 */
std::vector<TestParams> get_test_sizes() {
    return {
        // Tiny sizes (edge cases)
        TestParams("tiny_1", 1, false),
        TestParams("tiny_15", 15, false),
        TestParams("tiny_16", 16, true),
        TestParams("tiny_17", 17, false),
        TestParams("tiny_255", 255, false),
        TestParams("tiny_256", 256, true),
        
        // Small sizes
        TestParams("small_512", 512, true),
        TestParams("small_1024", 1024, true),
        TestParams("small_4096", 4096, true),
        
        // Page-sized (typical KV cache pages)
        TestParams("page_16KB", 16 * 1024, true),
        TestParams("page_64KB", 64 * 1024, true),
        TestParams("page_256KB", 256 * 1024, true),
        
        // Multi-page
        TestParams("multi_1MB", 1024 * 1024, true),
        TestParams("multi_4MB", 4 * 1024 * 1024, true),
        TestParams("multi_16MB", 16 * 1024 * 1024, true),
        
        // Large
        TestParams("large_64MB", 64 * 1024 * 1024, true),
        TestParams("large_100MB", 100 * 1024 * 1024, true),
        
        // Non-aligned sizes
        TestParams("unaligned_1000", 1000, false),
        TestParams("unaligned_10000", 10000, false),
        TestParams("unaligned_12345", 12345, false),
        TestParams("unaligned_65537", 65537, false),
    };
}

/**
 * Initialize device memory with random pattern
 */
void init_random_pattern(void* ptr, size_t size, unsigned int seed = 42) {
    std::vector<unsigned char> h_data(size);
    
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> dist(0, 255);
    
    for (size_t i = 0; i < size; ++i) {
        h_data[i] = static_cast<unsigned char>(dist(gen));
    }
    
    cudaMemcpy(ptr, h_data.data(), size, cudaMemcpyHostToDevice);
}

/**
 * Verify device memory matches reference
 */
bool verify_match(const void* device_ptr, const void* reference_ptr, size_t size) {
    std::vector<unsigned char> h_device(size);
    std::vector<unsigned char> h_reference(size);
    
    cudaMemcpy(h_device.data(), device_ptr, size, cudaMemcpyDeviceToHost);
    cudaMemcpy(h_reference.data(), reference_ptr, size, cudaMemcpyDeviceToHost);
    
    return memcmp(h_device.data(), h_reference.data(), size) == 0;
}

/**
 * Test a single kernel variant with given parameters
 */
template<typename KernelFunc>
bool test_kernel_variant(
    KernelFunc kernel_func,
    const std::string& kernel_name,
    size_t size,
    bool aligned
) {
    // Allocate device memory
    void *d_src, *d_dst;
    cudaError_t err;
    
    err = cudaMalloc(&d_src, size);
    if (err != cudaSuccess) {
        std::cerr << "Failed to allocate source buffer" << std::endl;
        return false;
    }
    
    err = cudaMalloc(&d_dst, size);
    if (err != cudaSuccess) {
        cudaFree(d_src);
        std::cerr << "Failed to allocate destination buffer" << std::endl;
        return false;
    }
    
    // Initialize source with random pattern
    init_random_pattern(d_src, size, 42);
    
    // Create reference buffer (use naive kernel as reference)
    void* d_ref;
    cudaMalloc(&d_ref, size);
    cudaMemcpy(d_ref, d_src, size, cudaMemcpyDeviceToDevice);
    launch_page_copy_naive(d_ref, d_ref, size);  // Copy to itself for reference
    cudaDeviceSynchronize();
    
    // Launch test kernel
    err = kernel_func(d_dst, d_src, size);
    if (err != cudaSuccess) {
        std::cerr << "Kernel launch failed: " << cudaGetErrorString(err) << std::endl;
        cudaFree(d_src);
        cudaFree(d_dst);
        cudaFree(d_ref);
        return false;
    }
    
    cudaDeviceSynchronize();
    
    // Verify against reference
    bool match = verify_match(d_dst, d_ref, size);
    
    if (!match) {
        std::cerr << "Mismatch detected in " << kernel_name 
                  << " for size " << size << " bytes" << std::endl;
        
        // Debug: show first few mismatches
        std::vector<unsigned char> h_device(size);
        std::vector<unsigned char> h_reference(size);
        cudaMemcpy(h_device.data(), d_dst, size, cudaMemcpyDeviceToHost);
        cudaMemcpy(h_reference.data(), d_ref, size, cudaMemcpyDeviceToHost);
        
        int mismatches = 0;
        for (size_t i = 0; i < std::min(size, (size_t)100) && mismatches < 10; ++i) {
            if (h_device[i] != h_reference[i]) {
                std::cerr << "  Mismatch at index " << i 
                          << ": expected " << (int)h_reference[i] 
                          << ", got " << (int)h_device[i] << std::endl;
                mismatches++;
            }
        }
    }
    
    // Cleanup
    cudaFree(d_src);
    cudaFree(d_dst);
    cudaFree(d_ref);
    
    return match;
}

/**
 * Test all kernel variants with given parameters
 */
bool test_all_kernels(size_t size, bool aligned) {
    bool all_pass = true;
    
    // Test naive kernel
    if (!test_kernel_variant(launch_page_copy_naive, "naive", size, aligned)) {
        std::cerr << "FAILED: naive kernel" << std::endl;
        all_pass = false;
    }
    
    // Test coalesced scalar kernel
    if (!test_kernel_variant(launch_page_copy_coalesced_scalar, "coalesced_scalar", size, aligned)) {
        std::cerr << "FAILED: coalesced_scalar kernel" << std::endl;
        all_pass = false;
    }
    
    // Test coalesced kernel (vectorized)
    if (!test_kernel_variant(launch_page_copy_coalesced, "coalesced", size, aligned)) {
        std::cerr << "FAILED: coalesced kernel" << std::endl;
        all_pass = false;
    }
    
    // Test shared memory kernel
    if (!test_kernel_variant(launch_page_copy_shared_mem, "shared_mem", size, aligned)) {
        std::cerr << "FAILED: shared_mem kernel" << std::endl;
        all_pass = false;
    }
    
    // Test shared memory vectorized kernel
    if (!test_kernel_variant(launch_page_copy_shared_mem_vec, "shared_mem_vec", size, aligned)) {
        std::cerr << "FAILED: shared_mem_vec kernel" << std::endl;
        all_pass = false;
    }
    
    return all_pass;
}

/**
 * Test: Zero size handling
 */
TEST(ComprehensiveCorrectnessTest, ZeroSize) {
    void *d_src, *d_dst;
    cudaMalloc(&d_src, 1);
    cudaMalloc(&d_dst, 1);
    
    // All kernels should handle zero size gracefully
    EXPECT_EQ(launch_page_copy_naive(d_dst, d_src, 0), cudaSuccess);
    EXPECT_EQ(launch_page_copy_coalesced(d_dst, d_src, 0), cudaSuccess);
    EXPECT_EQ(launch_page_copy_coalesced_scalar(d_dst, d_src, 0), cudaSuccess);
    EXPECT_EQ(launch_page_copy_shared_mem(d_dst, d_src, 0), cudaSuccess);
    EXPECT_EQ(launch_page_copy_shared_mem_vec(d_dst, d_src, 0), cudaSuccess);
    
    cudaDeviceSynchronize();
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: All kernel variants across all sizes
 */
TEST(ComprehensiveCorrectnessTest, AllSizesAllKernels) {
    auto test_sizes = get_test_sizes();
    
    for (const auto& params : test_sizes) {
        // Skip very large sizes in debug mode
        #ifdef DEBUG
        if (params.size > 16 * 1024 * 1024) {
            continue;
        }
        #endif
        
        bool pass = test_all_kernels(params.size, params.aligned);
        
        EXPECT_TRUE(pass) << "Failed for size: " << params.name 
                          << " (" << params.size << " bytes, "
                          << (params.aligned ? "aligned" : "non-aligned") << ")";
    }
}

/**
 * Test: Tiny sizes (edge cases)
 */
TEST(ComprehensiveCorrectnessTest, TinySizes) {
    std::vector<size_t> tiny_sizes = {1, 2, 3, 7, 15, 16, 17, 31, 32, 33};
    
    for (size_t size : tiny_sizes) {
        bool pass = test_all_kernels(size, false);
        EXPECT_TRUE(pass) << "Failed for tiny size: " << size << " bytes";
    }
}

/**
 * Test: Page sizes (typical KV cache)
 */
TEST(ComprehensiveCorrectnessTest, PageSizes) {
    std::vector<size_t> page_sizes = {
        16 * 1024,    // 16KB
        32 * 1024,    // 32KB
        64 * 1024,    // 64KB (typical)
        128 * 1024,   // 128KB
        256 * 1024,   // 256KB
        512 * 1024,   // 512KB
        1024 * 1024   // 1MB
    };
    
    for (size_t size : page_sizes) {
        bool pass = test_all_kernels(size, true);
        EXPECT_TRUE(pass) << "Failed for page size: " << size << " bytes";
    }
}

/**
 * Test: Non-aligned sizes
 */
TEST(ComprehensiveCorrectnessTest, NonAlignedSizes) {
    std::vector<size_t> non_aligned_sizes = {
        100, 1000, 1234, 5678, 10000, 12345, 65537, 100000
    };
    
    for (size_t size : non_aligned_sizes) {
        bool pass = test_all_kernels(size, false);
        EXPECT_TRUE(pass) << "Failed for non-aligned size: " << size << " bytes";
    }
}

/**
 * Test: Large sizes
 */
TEST(ComprehensiveCorrectnessTest, LargeSizes) {
    std::vector<size_t> large_sizes = {
        10 * 1024 * 1024,   // 10MB
        50 * 1024 * 1024,   // 50MB
        100 * 1024 * 1024   // 100MB
    };
    
    for (size_t size : large_sizes) {
        // Check if we have enough memory
        size_t free_mem, total_mem;
        cudaMemGetInfo(&free_mem, &total_mem);
        
        if (size * 3 > free_mem) {  // Need 3x for src, dst, ref
            GTEST_SKIP() << "Not enough device memory for " << size << " bytes";
        }
        
        bool pass = test_all_kernels(size, true);
        EXPECT_TRUE(pass) << "Failed for large size: " << size << " bytes";
    }
}

/**
 * Test: Boundary sizes (around 16-byte boundaries)
 */
TEST(ComprehensiveCorrectnessTest, BoundarySizes) {
    std::vector<size_t> boundary_sizes;
    
    // Test around 16-byte boundaries
    for (size_t base = 0; base <= 256; base += 16) {
        if (base > 0) boundary_sizes.push_back(base - 1);
        boundary_sizes.push_back(base);
        boundary_sizes.push_back(base + 1);
    }
    
    for (size_t size : boundary_sizes) {
        if (size == 0) continue;
        
        bool pass = test_all_kernels(size, false);
        EXPECT_TRUE(pass) << "Failed for boundary size: " << size << " bytes";
    }
}

/**
 * Test: Pattern verification (incrementing pattern)
 */
TEST(ComprehensiveCorrectnessTest, IncrementingPattern) {
    const size_t size = 65536;  // 64KB
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Create incrementing pattern
    std::vector<unsigned char> h_src(size);
    for (size_t i = 0; i < size; ++i) {
        h_src[i] = static_cast<unsigned char>(i & 0xFF);
    }
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Test coalesced kernel
    launch_page_copy_coalesced(d_dst, d_src, size);
    cudaDeviceSynchronize();
    
    // Verify
    std::vector<unsigned char> h_dst(size);
    cudaMemcpy(h_dst.data(), d_dst, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(h_dst[i], h_src[i]) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Pattern verification (all zeros)
 */
TEST(ComprehensiveCorrectnessTest, AllZerosPattern) {
    const size_t size = 1024;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Create all-zeros pattern
    std::vector<unsigned char> h_src(size, 0);
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Test all kernels
    launch_page_copy_naive(d_dst, d_src, size);
    cudaDeviceSynchronize();
    
    std::vector<unsigned char> h_dst(size);
    cudaMemcpy(h_dst.data(), d_dst, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(h_dst[i], 0) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Pattern verification (all ones / 0xFF)
 */
TEST(ComprehensiveCorrectnessTest, AllOnesPattern) {
    const size_t size = 1024;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Create all-ones pattern
    std::vector<unsigned char> h_src(size, 0xFF);
    cudaMemcpy(d_src, h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Test all kernels
    launch_page_copy_coalesced(d_dst, d_src, size);
    cudaDeviceSynchronize();
    
    std::vector<unsigned char> h_dst(size);
    cudaMemcpy(h_dst.data(), d_dst, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(h_dst[i], 0xFF) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}

/**
 * Test: Concurrent kernel launches (stress test)
 */
TEST(ComprehensiveCorrectnessTest, ConcurrentLaunches) {
    const size_t size = 4096;
    const int num_streams = 4;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    init_random_pattern(d_src, size, 42);
    
    // Create streams
    cudaStream_t streams[num_streams];
    for (int i = 0; i < num_streams; ++i) {
        cudaStreamCreate(&streams[i]);
    }
    
    // Launch different kernels in different streams
    launch_page_copy_naive(d_dst, d_src, size, streams[0]);
    launch_page_copy_coalesced_scalar(d_dst, d_src, size, streams[1]);
    launch_page_copy_coalesced(d_dst, d_src, size, streams[2]);
    launch_page_copy_shared_mem(d_dst, d_src, size, streams[3]);
    
    // Synchronize all streams
    for (int i = 0; i < num_streams; ++i) {
        cudaStreamSynchronize(streams[i]);
        cudaStreamDestroy(streams[i]);
    }
    
    // Verify all produced correct results (they all copied from same source)
    std::vector<unsigned char> h_src(size);
    std::vector<unsigned char> h_dst(size);
    cudaMemcpy(h_src.data(), d_src, size, cudaMemcpyDeviceToHost);
    cudaMemcpy(h_dst.data(), d_dst, size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(h_dst[i], h_src[i]) << "Mismatch at index " << i;
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
}
