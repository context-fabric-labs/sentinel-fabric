#include <pm_nixl/page_gather_engine.hpp>
#include <pm_nixl/buffer.hpp>
#include <pm_nixl/transfer_engine.hpp>
#include <gtest/gtest.h>
#include <vector>
#include <cstring>

using namespace pm_nixl;

/**
 * Test: Page gather engine construction
 */
TEST(PageGatherTest, EngineConstruction) {
    TransferEngineConfig config;
    TransferEngine transfer_engine(config);
    
    KvPageGatherEngine gather_engine(transfer_engine);
    
    // Engine constructed successfully
    SUCCEED();
}

/**
 * Test: Page gather with simple order
 * 
 * Note: This test requires CUDA device and is disabled on systems without GPU.
 */
TEST(PageGatherTest, DISABLED_GatherSimpleOrder) {
    TransferEngineConfig config;
    TransferEngine transfer_engine(config);
    transfer_engine.initialize();
    
    KvPageGatherEngine gather_engine(transfer_engine);
    
    // Create source and destination buffers
    const size_t page_size = 65536;  // 64KB
    const size_t num_pages = 10;
    const size_t total_size = num_pages * page_size;
    
    Buffer src(MemoryKind::CudaDevice, total_size);
    Buffer dst(MemoryKind::CudaDevice, total_size);
    
    // Initialize source with pattern
    std::vector<char> h_src(total_size);
    for (size_t i = 0; i < total_size; ++i) {
        h_src[i] = static_cast<char>(i & 0xFF);
    }
    
    // Copy to device (simplified - would use transfer engine in real code)
    cudaMemcpy(src.ptr(), h_src.data(), total_size, cudaMemcpyHostToDevice);
    
    // Create page table with simple order [0, 1, 2, ..., 9]
    KvPageTable page_table(PageSize::Page64KB);
    for (size_t i = 0; i < num_pages; ++i) {
        page_table.add_page(KvPageDesc(
            static_cast<uint32_t>(i),
            i * page_size,
            page_size,
            PageSize::Page64KB
        ));
    }
    
    // Gather
    auto result = gather_engine.gather(dst.descriptor(), src.descriptor(), page_table);
    
    EXPECT_TRUE(result.has_value());
    
    // Verify destination has correct data
    std::vector<char> h_dst(total_size);
    cudaMemcpy(h_dst.data(), dst.ptr(), total_size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < total_size; ++i) {
        EXPECT_EQ(h_dst[i], h_src[i]);
    }
    
    transfer_engine.shutdown();
}

/**
 * Test: Page gather with shuffled order
 */
TEST(PageGatherTest, DISABLED_GatherShuffledOrder) {
    TransferEngineConfig config;
    TransferEngine transfer_engine(config);
    transfer_engine.initialize();
    
    KvPageGatherEngine gather_engine(transfer_engine);
    
    const size_t page_size = 65536;
    const size_t num_pages = 5;
    const size_t total_size = num_pages * page_size;
    
    Buffer src(MemoryKind::CudaDevice, total_size);
    Buffer dst(MemoryKind::CudaDevice, total_size);
    
    // Initialize source with page-specific patterns
    std::vector<char> h_src(total_size);
    for (size_t page = 0; page < num_pages; ++page) {
        for (size_t i = 0; i < page_size; ++i) {
            h_src[page * page_size + i] = static_cast<char>((page + 1) & 0xFF);
        }
    }
    
    cudaMemcpy(src.ptr(), h_src.data(), total_size, cudaMemcpyHostToDevice);
    
    // Create page table
    KvPageTable page_table(PageSize::Page64KB);
    for (size_t i = 0; i < num_pages; ++i) {
        page_table.add_page(KvPageDesc(
            static_cast<uint32_t>(i),
            i * page_size,
            page_size,
            PageSize::Page64KB
        ));
    }
    
    // Set shuffled order: [4, 2, 0, 3, 1]
    std::vector<uint32_t> shuffled_order = {4, 2, 0, 3, 1};
    page_table.set_page_indices(shuffled_order);
    
    // Gather
    auto result = gather_engine.gather(dst.descriptor(), src.descriptor(), page_table);
    EXPECT_TRUE(result.has_value());
    
    // Verify destination has pages in shuffled order
    std::vector<char> h_dst(total_size);
    cudaMemcpy(h_dst.data(), dst.ptr(), total_size, cudaMemcpyDeviceToHost);
    
    for (size_t page = 0; page < num_pages; ++page) {
        uint32_t src_page = shuffled_order[page];
        for (size_t i = 0; i < page_size; ++i) {
            char expected = static_cast<char>((src_page + 1) & 0xFF);
            EXPECT_EQ(h_dst[page * page_size + i], expected);
        }
    }
    
    transfer_engine.shutdown();
}

/**
 * Test: Page copy
 */
TEST(PageGatherTest, DISABLED_PageCopy) {
    TransferEngineConfig config;
    TransferEngine transfer_engine(config);
    transfer_engine.initialize();
    
    KvPageGatherEngine gather_engine(transfer_engine);
    
    const size_t page_size = 65536;
    const size_t num_pages = 10;
    const size_t total_size = num_pages * page_size;
    
    Buffer src(MemoryKind::CudaDevice, total_size);
    Buffer dst(MemoryKind::CudaDevice, total_size);
    
    // Initialize source
    std::vector<char> h_src(total_size);
    for (size_t i = 0; i < total_size; ++i) {
        h_src[i] = static_cast<char>(i & 0xFF);
    }
    
    cudaMemcpy(src.ptr(), h_src.data(), total_size, cudaMemcpyHostToDevice);
    
    // Copy
    auto result = gather_engine.copy(dst.descriptor(), src.descriptor(), total_size);
    EXPECT_TRUE(result.has_value());
    
    // Verify
    std::vector<char> h_dst(total_size);
    cudaMemcpy(h_dst.data(), dst.ptr(), total_size, cudaMemcpyDeviceToHost);
    
    for (size_t i = 0; i < total_size; ++i) {
        EXPECT_EQ(h_dst[i], h_src[i]);
    }
    
    transfer_engine.shutdown();
}

/**
 * Test: Gather with invalid arguments
 */
TEST(PageGatherTest, GatherInvalidArguments) {
    TransferEngineConfig config;
    TransferEngine transfer_engine(config);
    transfer_engine.initialize();
    
    KvPageGatherEngine gather_engine(transfer_engine);
    
    // Empty page table
    KvPageTable empty_table(PageSize::Page64KB);
    Buffer src(MemoryKind::CudaDevice, 65536);
    Buffer dst(MemoryKind::CudaDevice, 65536);
    
    auto result = gather_engine.gather(dst.descriptor(), src.descriptor(), empty_table);
    EXPECT_FALSE(result.has_value());
    
    // Host memory (should fail)
    Buffer host(MemoryKind::HostPinned, 65536);
    KvPageTable table(PageSize::Page64KB);
    table.add_page(KvPageDesc(1, 0, 65536, PageSize::Page64KB));
    
    result = gather_engine.gather(dst.descriptor(), host.descriptor(), table);
    EXPECT_FALSE(result.has_value());
    
    transfer_engine.shutdown();
}

/**
 * Test: Gather with size mismatch
 */
TEST(PageGatherTest, GatherSizeMismatch) {
    TransferEngineConfig config;
    TransferEngine transfer_engine(config);
    transfer_engine.initialize();
    
    KvPageGatherEngine gather_engine(transfer_engine);
    
    // Destination too small
    Buffer src(MemoryKind::CudaDevice, 131072);  // 2 pages
    Buffer dst(MemoryKind::CudaDevice, 65536);   // 1 page
    
    KvPageTable table(PageSize::Page64KB);
    table.add_page(KvPageDesc(1, 0, 65536, PageSize::Page64KB));
    table.add_page(KvPageDesc(2, 65536, 65536, PageSize::Page64KB));
    
    auto result = gather_engine.gather(dst.descriptor(), src.descriptor(), table);
    EXPECT_FALSE(result.has_value());
    
    transfer_engine.shutdown();
}
