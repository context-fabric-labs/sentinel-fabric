#include "pm_nixl/kv_page.hpp"
#include <algorithm>
#include <cstring>

namespace pm_nixl {

const char* page_size_to_string(PageSize size) {
    switch (size) {
        case PageSize::Page16KB:
            return "16KB";
        case PageSize::Page64KB:
            return "64KB";
        case PageSize::Page256KB:
            return "256KB";
        case PageSize::Page1MB:
            return "1MB";
        default:
            return "Unknown";
    }
}

bool KvPageDesc::validate(size_t buffer_size, size_t alignment) {
    // Check size is non-zero
    if (size == 0) {
        is_valid = false;
        return false;
    }
    
    // Check offset doesn't exceed buffer
    if (offset >= buffer_size) {
        is_valid = false;
        return false;
    }
    
    // Check page doesn't exceed buffer bounds
    if (offset + size > buffer_size) {
        is_valid = false;
        return false;
    }
    
    // Check alignment
    if (!is_aligned(alignment)) {
        is_valid = false;
        return false;
    }
    
    // Check size matches page size enum
    if (size != get_page_size_bytes(page_size_enum)) {
        is_valid = false;
        return false;
    }
    
    is_valid = true;
    return true;
}

bool KvPageDesc::is_aligned(size_t alignment) const {
    if (alignment == 0 || alignment == 1) {
        return true;
    }
    return (offset % alignment) == 0;
}

// KvPageTable implementation

KvPageTable::KvPageTable()
    : page_size_(PageSize::Page64KB)
    , next_page_id_(1) {}

KvPageTable::KvPageTable(PageSize page_size)
    : page_size_(page_size)
    , next_page_id_(1) {}

size_t KvPageTable::page_size_bytes() const {
    return get_page_size_bytes(page_size_);
}

const KvPageDesc& KvPageTable::page_at(size_t index) const {
    return pages_.at(index);
}

const KvPageDesc* KvPageTable::find_page(uint32_t page_id) const {
    for (const auto& page : pages_) {
        if (page.page_id == page_id) {
            return &page;
        }
    }
    return nullptr;
}

Result<size_t> KvPageTable::add_page(const KvPageDesc& page) {
    // Check for duplicate page ID
    if (find_page(page.page_id) != nullptr) {
        return std::error_code(ErrorCode::AlreadyRegistered);
    }
    
    KvPageDesc new_page = page;
    new_page.page_index = static_cast<uint32_t>(pages_.size());
    new_page.version = 1;
    
    pages_.push_back(new_page);
    page_indices_.push_back(new_page.page_index);
    
    return Result<size_t>(pages_.size() - 1);
}

Result<void> KvPageTable::remove_page(uint32_t page_id) {
    auto it = std::find_if(pages_.begin(), pages_.end(),
        [page_id](const KvPageDesc& p) { return p.page_id == page_id; });
    
    if (it == pages_.end()) {
        return std::error_code(ErrorCode::NotRegistered);
    }
    
    pages_.erase(it);
    assign_indices();
    
    return Result<void>(std::monostate{});
}

void KvPageTable::clear() {
    pages_.clear();
    page_indices_.clear();
    next_page_id_ = 1;
}

bool KvPageTable::validate_all(size_t buffer_size, size_t alignment) {
    bool all_valid = true;
    
    for (auto& page : pages_) {
        if (!page.validate(buffer_size, alignment)) {
            all_valid = false;
        }
    }
    
    return all_valid;
}

bool KvPageTable::has_overlaps() const {
    for (size_t i = 0; i < pages_.size(); ++i) {
        for (size_t j = i + 1; j < pages_.size(); ++j) {
            if (pages_[i].overlaps_with(pages_[j])) {
                return true;
            }
        }
    }
    return false;
}

size_t KvPageTable::total_size() const {
    size_t total = 0;
    for (const auto& page : pages_) {
        total += page.size;
    }
    return total;
}

void KvPageTable::set_page_indices(const std::vector<uint32_t>& indices) {
    if (indices.size() != pages_.size()) {
        return;  // Mismatched size
    }
    page_indices_ = indices;
}

std::vector<const KvPageDesc*> KvPageTable::get_pages_in_order() const {
    std::vector<const KvPageDesc*> ordered_pages;
    ordered_pages.reserve(pages_.size());
    
    for (uint32_t index : page_indices_) {
        if (index < pages_.size()) {
            ordered_pages.push_back(&pages_[index]);
        }
    }
    
    return ordered_pages;
}

void KvPageTable::assign_indices() {
    for (size_t i = 0; i < pages_.size(); ++i) {
        pages_[i].page_index = static_cast<uint32_t>(i);
    }
}

// KvPageRange implementation

bool KvPageRange::validate(const KvPageTable& table) const {
    if (page_count == 0) {
        return false;
    }
    
    // Check all pages exist
    for (size_t i = 0; i < page_count; ++i) {
        uint32_t page_id = start_page_id + static_cast<uint32_t>(i);
        if (table.find_page(page_id) == nullptr) {
            return false;
        }
    }
    
    // Check total size matches
    size_t expected_size = page_count * table.page_size_bytes();
    if (total_size != expected_size) {
        return false;
    }
    
    return true;
}

// Validation helpers implementation

namespace kv_page_validation {

bool is_page_aligned(size_t offset, PageSize page_size) {
    size_t ps = get_page_size_bytes(page_size);
    return (offset % ps) == 0;
}

bool is_valid_page_size(size_t size, PageSize page_size) {
    return size == get_page_size_bytes(page_size);
}

size_t calculate_page_count(size_t total_size, PageSize page_size) {
    size_t ps = get_page_size_bytes(page_size);
    return (total_size + ps - 1) / ps;  // Ceiling division
}

size_t calculate_offset(size_t page_index, PageSize page_size) {
    return page_index * get_page_size_bytes(page_size);
}

bool validate_bounds(const KvPageDesc& page, size_t buffer_size) {
    return (page.offset + page.size) <= buffer_size;
}

bool validate_alignment(const KvPageDesc& page, size_t alignment) {
    return page.is_aligned(alignment);
}

} // namespace kv_page_validation

} // namespace pm_nixl
