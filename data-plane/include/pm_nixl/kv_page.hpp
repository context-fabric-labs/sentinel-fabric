#pragma once

#include "descriptor.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace pm_nixl {

/**
 * Standard KV page sizes
 */
enum class PageSize : uint32_t {
    Page16KB = 16 * 1024,
    Page64KB = 64 * 1024,
    Page256KB = 256 * 1024,
    Page1MB = 1024 * 1024
};

/**
 * Get page size in bytes
 */
constexpr size_t get_page_size_bytes(PageSize size) {
    return static_cast<size_t>(size);
}

/**
 * Get string representation of page size
 */
const char* page_size_to_string(PageSize size);

/**
 * KV Page Descriptor
 * 
 * Describes a single KV cache page/block.
 * Does not own memory - references a buffer region.
 */
struct KvPageDesc {
    uint32_t page_id;         // Unique page identifier
    uint32_t page_index;      // Index in page table
    size_t offset;            // Offset within buffer
    size_t size;              // Page size in bytes
    PageSize page_size_enum;  // Page size enum
    bool is_valid;            // Validation status
    uint64_t version;         // Version for ABA detection
    
    /**
     * Default constructor
     */
    constexpr KvPageDesc()
        : page_id(0)
        , page_index(0)
        , offset(0)
        , size(0)
        , page_size_enum(PageSize::Page64KB)
        , is_valid(false)
        , version(1) {}
    
    /**
     * Construct a page descriptor
     */
    constexpr KvPageDesc(
        uint32_t id,
        size_t off,
        size_t sz,
        PageSize ps = PageSize::Page64KB
    )
        : page_id(id)
        , page_index(0)
        , offset(off)
        , size(sz)
        , page_size_enum(ps)
        , is_valid(false)
        , version(1) {}
    
    /**
     * Validate page descriptor
     */
    bool validate(size_t buffer_size, size_t alignment = 1);
    
    /**
     * Check if page is aligned
     */
    bool is_aligned(size_t alignment) const;
    
    /**
     * Get end offset
     */
    constexpr size_t end_offset() const {
        return offset + size;
    }
    
    /**
     * Check if page overlaps with another
     */
    constexpr bool overlaps_with(const KvPageDesc& other) const {
        return !(end_offset() <= other.offset || other.end_offset() <= offset);
    }
};

/**
 * KV Page Table
 * 
 * Collection of page descriptors with metadata.
 */
class KvPageTable {
public:
    /**
     * Default constructor
     */
    KvPageTable();
    
    /**
     * Construct with specific page size
     */
    explicit KvPageTable(PageSize page_size);
    
    /**
     * Get page size
     */
    PageSize page_size() const { return page_size_; }
    
    /**
     * Get page size in bytes
     */
    size_t page_size_bytes() const;
    
    /**
     * Get number of pages
     */
    size_t page_count() const { return pages_.size(); }
    
    /**
     * Get page by index
     */
    const KvPageDesc& page_at(size_t index) const;
    
    /**
     * Get page by ID
     */
    const KvPageDesc* find_page(uint32_t page_id) const;
    
    /**
     * Add a page
     */
    Result<size_t> add_page(const KvPageDesc& page);
    
    /**
     * Remove a page by ID
     */
    Result<void> remove_page(uint32_t page_id);
    
    /**
     * Clear all pages
     */
    void clear();
    
    /**
     * Validate all pages against buffer
     */
    bool validate_all(size_t buffer_size, size_t alignment = 1);
    
    /**
     * Check for overlapping pages
     */
    bool has_overlaps() const;
    
    /**
     * Get total size of all pages
     */
    size_t total_size() const;
    
    /**
     * Get page indices
     */
    const std::vector<uint32_t>& page_indices() const { return page_indices_; }
    
    /**
     * Set page indices (for gather/scatter operations)
     */
    void set_page_indices(const std::vector<uint32_t>& indices);
    
    /**
     * Get pages in index order
     */
    std::vector<const KvPageDesc*> get_pages_in_order() const;
    
private:
    PageSize page_size_;
    std::vector<KvPageDesc> pages_;
    std::vector<uint32_t> page_indices_;  // For gather/scatter order
    uint32_t next_page_id_;
    
    /**
     * Assign page indices
     */
    void assign_indices();
};

/**
 * KV Page Range
 * 
 * Describes a contiguous range of pages.
 */
struct KvPageRange {
    uint32_t start_page_id;
    size_t page_count;
    size_t total_size;
    
    constexpr KvPageRange()
        : start_page_id(0)
        , page_count(0)
        , total_size(0) {}
    
    constexpr KvPageRange(uint32_t start, size_t count, size_t size)
        : start_page_id(start)
        , page_count(count)
        , total_size(size) {}
    
    /**
     * Validate range
     */
    bool validate(const KvPageTable& table) const;
};

/**
 * Validation helpers
 */
namespace kv_page_validation {

/**
 * Check if offset is aligned to page size
 */
bool is_page_aligned(size_t offset, PageSize page_size);

/**
 * Check if size is valid for page size
 */
bool is_valid_page_size(size_t size, PageSize page_size);

/**
 * Calculate number of pages needed for size
 */
size_t calculate_page_count(size_t total_size, PageSize page_size);

/**
 * Calculate offset for page index
 */
size_t calculate_offset(size_t page_index, PageSize page_size);

/**
 * Validate page doesn't exceed buffer bounds
 */
bool validate_bounds(const KvPageDesc& page, size_t buffer_size);

/**
 * Validate page alignment
 */
bool validate_alignment(const KvPageDesc& page, size_t alignment);

} // namespace kv_page_validation

} // namespace pm_nixl
