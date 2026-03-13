#include <pm_nixl/kv_page.hpp>
#include <pm_nixl/buffer.hpp>
#include <gtest/gtest.h>

using namespace pm_nixl;

/**
 * Test: Page size enum
 */
TEST(KvPageTest, PageSizeEnum) {
    EXPECT_EQ(get_page_size_bytes(PageSize::Page16KB), 16 * 1024);
    EXPECT_EQ(get_page_size_bytes(PageSize::Page64KB), 64 * 1024);
    EXPECT_EQ(get_page_size_bytes(PageSize::Page256KB), 256 * 1024);
    EXPECT_EQ(get_page_size_bytes(PageSize::Page1MB), 1024 * 1024);
}

/**
 * Test: Page size string conversion
 */
TEST(KvPageTest, PageSizeString) {
    EXPECT_STREQ(page_size_to_string(PageSize::Page16KB), "16KB");
    EXPECT_STREQ(page_size_to_string(PageSize::Page64KB), "64KB");
    EXPECT_STREQ(page_size_to_string(PageSize::Page256KB), "256KB");
    EXPECT_STREQ(page_size_to_string(PageSize::Page1MB), "1MB");
}

/**
 * Test: Page descriptor default construction
 */
TEST(KvPageTest, PageDescDefault) {
    KvPageDesc page;
    EXPECT_EQ(page.page_id, 0);
    EXPECT_EQ(page.page_index, 0);
    EXPECT_EQ(page.offset, 0);
    EXPECT_EQ(page.size, 0);
    EXPECT_FALSE(page.is_valid);
}

/**
 * Test: Page descriptor construction
 */
TEST(KvPageTest, PageDescConstruction) {
    KvPageDesc page(42, 1024, 65536, PageSize::Page64KB);
    EXPECT_EQ(page.page_id, 42);
    EXPECT_EQ(page.offset, 1024);
    EXPECT_EQ(page.size, 65536);
    EXPECT_EQ(page.page_size_enum, PageSize::Page64KB);
    EXPECT_FALSE(page.is_valid);  // Not validated yet
}

/**
 * Test: Page descriptor validation
 */
TEST(KvPageTest, PageDescValidation) {
    const size_t buffer_size = 1024 * 1024;  // 1 MB
    
    // Valid page
    KvPageDesc page1(1, 0, 65536, PageSize::Page64KB);
    EXPECT_TRUE(page1.validate(buffer_size, 1));
    EXPECT_TRUE(page1.is_valid);
    
    // Invalid: exceeds buffer
    KvPageDesc page2(2, buffer_size, 65536, PageSize::Page64KB);
    EXPECT_FALSE(page2.validate(buffer_size, 1));
    EXPECT_FALSE(page2.is_valid);
    
    // Invalid: wrong size
    KvPageDesc page3(3, 0, 32768, PageSize::Page64KB);
    EXPECT_FALSE(page3.validate(buffer_size, 1));
    EXPECT_FALSE(page3.is_valid);
    
    // Invalid: zero size
    KvPageDesc page4(4, 0, 0, PageSize::Page64KB);
    EXPECT_FALSE(page4.validate(buffer_size, 1));
    EXPECT_FALSE(page4.is_valid);
}

/**
 * Test: Page alignment check
 */
TEST(KvPageTest, PageAlignment) {
    KvPageDesc page(1, 4096, 65536, PageSize::Page64KB);
    
    EXPECT_TRUE(page.is_aligned(1));
    EXPECT_TRUE(page.is_aligned(4096));
    EXPECT_TRUE(page.is_aligned(8192));
    EXPECT_FALSE(page.is_aligned(65536));  // 4096 % 65536 != 0
    
    // Page-aligned offset
    KvPageDesc page2(2, 65536, 65536, PageSize::Page64KB);
    EXPECT_TRUE(page2.is_aligned(65536));
}

/**
 * Test: Page end offset
 */
TEST(KvPageTest, PageEndOffset) {
    KvPageDesc page(1, 1024, 65536, PageSize::Page64KB);
    EXPECT_EQ(page.end_offset(), 1024 + 65536);
}

/**
 * Test: Page overlap detection
 */
TEST(KvPageTest, PageOverlap) {
    KvPageDesc page1(1, 0, 65536, PageSize::Page64KB);
    KvPageDesc page2(2, 65536, 65536, PageSize::Page64KB);  // Adjacent, no overlap
    KvPageDesc page3(3, 32768, 65536, PageSize::Page64KB);  // Overlaps with page1
    
    EXPECT_FALSE(page1.overlaps_with(page2));
    EXPECT_FALSE(page2.overlaps_with(page1));
    
    EXPECT_TRUE(page1.overlaps_with(page3));
    EXPECT_TRUE(page3.overlaps_with(page1));
}

/**
 * Test: Page table construction
 */
TEST(KvPageTest, PageTableConstruction) {
    KvPageTable table;
    EXPECT_EQ(table.page_size(), PageSize::Page64KB);
    EXPECT_EQ(table.page_count(), 0);
    EXPECT_EQ(table.total_size(), 0);
    
    KvPageTable table2(PageSize::Page256KB);
    EXPECT_EQ(table2.page_size(), PageSize::Page256KB);
    EXPECT_EQ(table2.page_size_bytes(), 256 * 1024);
}

/**
 * Test: Page table add pages
 */
TEST(KvPageTest, PageTableAddPages) {
    KvPageTable table(PageSize::Page64KB);
    
    KvPageDesc page1(1, 0, 65536, PageSize::Page64KB);
    KvPageDesc page2(2, 65536, 65536, PageSize::Page64KB);
    
    auto result1 = table.add_page(page1);
    EXPECT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), 0);
    
    auto result2 = table.add_page(page2);
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), 1);
    
    EXPECT_EQ(table.page_count(), 2);
    EXPECT_EQ(table.total_size(), 2 * 65536);
}

/**
 * Test: Page table duplicate page ID
 */
TEST(KvPageTest, PageTableDuplicateId) {
    KvPageTable table(PageSize::Page64KB);
    
    KvPageDesc page1(1, 0, 65536, PageSize::Page64KB);
    KvPageDesc page2(1, 65536, 65536, PageSize::Page64KB);  // Same ID
    
    table.add_page(page1);
    auto result = table.add_page(page2);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(table.page_count(), 1);
}

/**
 * Test: Page table remove page
 */
TEST(KvPageTest, PageTableRemovePage) {
    KvPageTable table(PageSize::Page64KB);
    
    KvPageDesc page1(1, 0, 65536, PageSize::Page64KB);
    KvPageDesc page2(2, 65536, 65536, PageSize::Page64KB);
    
    table.add_page(page1);
    table.add_page(page2);
    
    EXPECT_EQ(table.page_count(), 2);
    
    auto result = table.remove_page(1);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(table.page_count(), 1);
    
    // Try to remove non-existent page
    auto result2 = table.remove_page(999);
    EXPECT_FALSE(result2.has_value());
}

/**
 * Test: Page table find page
 */
TEST(KvPageTest, PageTableFindPage) {
    KvPageTable table(PageSize::Page64KB);
    
    KvPageDesc page1(42, 0, 65536, PageSize::Page64KB);
    table.add_page(page1);
    
    const KvPageDesc* found = table.find_page(42);
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->page_id, 42);
    
    const KvPageDesc* not_found = table.find_page(999);
    EXPECT_EQ(not_found, nullptr);
}

/**
 * Test: Page table validate all
 */
TEST(KvPageTest, PageTableValidateAll) {
    KvPageTable table(PageSize::Page64KB);
    const size_t buffer_size = 256 * 1024;
    
    KvPageDesc page1(1, 0, 65536, PageSize::Page64KB);
    KvPageDesc page2(2, 65536, 65536, PageSize::Page64KB);
    KvPageDesc page3(3, buffer_size, 65536, PageSize::Page64KB);  // Invalid
    
    table.add_page(page1);
    table.add_page(page2);
    table.add_page(page3);
    
    EXPECT_FALSE(table.validate_all(buffer_size, 1));
    
    // Remove invalid page
    table.remove_page(3);
    EXPECT_TRUE(table.validate_all(buffer_size, 1));
}

/**
 * Test: Page table overlap detection
 */
TEST(KvPageTest, PageTableHasOverlaps) {
    KvPageTable table(PageSize::Page64KB);
    
    KvPageDesc page1(1, 0, 65536, PageSize::Page64KB);
    KvPageDesc page2(2, 65536, 65536, PageSize::Page64KB);  // No overlap
    KvPageDesc page3(3, 32768, 65536, PageSize::Page64KB);  // Overlaps
    
    table.add_page(page1);
    table.add_page(page2);
    
    EXPECT_FALSE(table.has_overlaps());
    
    table.add_page(page3);
    EXPECT_TRUE(table.has_overlaps());
}

/**
 * Test: Page table clear
 */
TEST(KvPageTest, PageTableClear) {
    KvPageTable table(PageSize::Page64KB);
    
    table.add_page(KvPageDesc(1, 0, 65536, PageSize::Page64KB));
    table.add_page(KvPageDesc(2, 65536, 65536, PageSize::Page64KB));
    
    EXPECT_EQ(table.page_count(), 2);
    
    table.clear();
    
    EXPECT_EQ(table.page_count(), 0);
    EXPECT_EQ(table.total_size(), 0);
}

/**
 * Test: Page table page indices
 */
TEST(KvPageTest, PageTableIndices) {
    KvPageTable table(PageSize::Page64KB);
    
    table.add_page(KvPageDesc(10, 0, 65536, PageSize::Page64KB));
    table.add_page(KvPageDesc(20, 65536, 65536, PageSize::Page64KB));
    table.add_page(KvPageDesc(30, 131072, 65536, PageSize::Page64KB));
    
    // Set custom order
    std::vector<uint32_t> order = {2, 0, 1};
    table.set_page_indices(order);
    
    auto ordered = table.get_pages_in_order();
    EXPECT_EQ(ordered.size(), 3);
    EXPECT_EQ(ordered[0]->page_id, 30);
    EXPECT_EQ(ordered[1]->page_id, 10);
    EXPECT_EQ(ordered[2]->page_id, 20);
}

/**
 * Test: Page range validation
 */
TEST(KvPageTest, PageRangeValidation) {
    KvPageTable table(PageSize::Page64KB);
    
    table.add_page(KvPageDesc(10, 0, 65536, PageSize::Page64KB));
    table.add_page(KvPageDesc(11, 65536, 65536, PageSize::Page64KB));
    table.add_page(KvPageDesc(12, 131072, 65536, PageSize::Page64KB));
    
    KvPageRange range(10, 3, 3 * 65536);
    EXPECT_TRUE(range.validate(table));
    
    KvPageRange invalid_range(10, 3, 2 * 65536);  // Wrong size
    EXPECT_FALSE(invalid_range.validate(table));
}

/**
 * Test: Validation helpers
 */
TEST(KvPageTest, ValidationHelpers) {
    using namespace kv_page_validation;
    
    // Page alignment
    EXPECT_TRUE(is_page_aligned(0, PageSize::Page64KB));
    EXPECT_TRUE(is_page_aligned(65536, PageSize::Page64KB));
    EXPECT_FALSE(is_page_aligned(1024, PageSize::Page64KB));
    
    // Valid page size
    EXPECT_TRUE(is_valid_page_size(65536, PageSize::Page64KB));
    EXPECT_FALSE(is_valid_page_size(32768, PageSize::Page64KB));
    
    // Calculate page count
    EXPECT_EQ(calculate_page_count(65536, PageSize::Page64KB), 1);
    EXPECT_EQ(calculate_page_count(65537, PageSize::Page64KB), 2);
    EXPECT_EQ(calculate_page_count(131072, PageSize::Page64KB), 2);
    
    // Calculate offset
    EXPECT_EQ(calculate_offset(0, PageSize::Page64KB), 0);
    EXPECT_EQ(calculate_offset(1, PageSize::Page64KB), 65536);
    EXPECT_EQ(calculate_offset(5, PageSize::Page64KB), 5 * 65536);
    
    // Validate bounds
    KvPageDesc page(1, 0, 65536, PageSize::Page64KB);
    EXPECT_TRUE(validate_bounds(page, 1024 * 1024));
    EXPECT_FALSE(validate_bounds(page, 32768));
}

/**
 * Test: Integration with BufferDescriptor
 */
TEST(KvPageTest, IntegrationWithBuffer) {
    // Create a buffer
    Buffer buffer(MemoryKind::HostPinned, 256 * 1024);
    
    // Create page table
    KvPageTable table(PageSize::Page64KB);
    
    // Add pages that fit in buffer
    for (size_t i = 0; i < 4; ++i) {
        KvPageDesc page(
            static_cast<uint32_t>(i + 1),
            i * 65536,
            65536,
            PageSize::Page64KB
        );
        table.add_page(page);
    }
    
    // Validate all pages against buffer
    EXPECT_TRUE(table.validate_all(buffer.size(), buffer.descriptor().alignment));
    EXPECT_FALSE(table.has_overlaps());
    EXPECT_EQ(table.page_count(), 4);
    EXPECT_EQ(table.total_size(), 4 * 65536);
}
