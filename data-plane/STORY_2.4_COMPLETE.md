# Story 2.4: KV Page Abstraction - COMPLETE

## ✅ Story Status: COMPLETE (Code Complete, Ready for Integration)

**Story:** KV page abstraction  
**Date:** March 12, 2026  
**Epic:** 2 - Poor-man's NIXL  
**Builds on:** Stories 2.1-2.3 (Buffer + Transfer Engine + Benchmarks)

---

## What Was Implemented

### 1. KV Page Descriptor ✅

**File:** `include/pm_nixl/kv_page.hpp`

**Key Types:**

#### PageSize Enum
```cpp
enum class PageSize : uint32_t {
    Page16KB = 16 * 1024,
    Page64KB = 64 * 1024,
    Page256KB = 256 * 1024,
    Page1MB = 1024 * 1024
};
```

#### KvPageDesc Struct
```cpp
struct KvPageDesc {
    uint32_t page_id;         // Unique page identifier
    uint32_t page_index;      // Index in page table
    size_t offset;            // Offset within buffer
    size_t size;              // Page size in bytes
    PageSize page_size_enum;  // Page size enum
    bool is_valid;            // Validation status
    uint64_t version;         // Version for ABA detection
};
```

### 2. KV Page Table ✅

**Class:** `KvPageTable`

**Features:**
- Collection of page descriptors
- Page size configuration
- Add/remove pages
- Validation against buffer
- Overlap detection
- Page indices for gather/scatter ordering
- Total size calculation

**API:**
```cpp
KvPageTable table(PageSize::Page64KB);

// Add pages
table.add_page(KvPageDesc(1, 0, 65536, PageSize::Page64KB));
table.add_page(KvPageDesc(2, 65536, 65536, PageSize::Page64KB));

// Validate
table.validate_all(buffer_size, alignment);

// Check for overlaps
if (table.has_overlaps()) {
    // Handle error
}

// Get pages in custom order (for gather/scatter)
std::vector<uint32_t> order = {1, 0};
table.set_page_indices(order);
auto ordered = table.get_pages_in_order();
```

### 3. KV Page Range ✅

**Struct:** `KvPageRange`

**Purpose:** Describe contiguous page ranges

```cpp
struct KvPageRange {
    uint32_t start_page_id;
    size_t page_count;
    size_t total_size;
};
```

### 4. Validation Helpers ✅

**Namespace:** `kv_page_validation`

**Functions:**
- `is_page_aligned()` - Check page alignment
- `is_valid_page_size()` - Validate size matches page size
- `calculate_page_count()` - Pages needed for size
- `calculate_offset()` - Offset for page index
- `validate_bounds()` - Check page within buffer
- `validate_alignment()` - Check alignment

---

## Design Decisions

### 1. Descriptor-Only Approach

**Decision:** Descriptors don't own memory, they reference buffer regions.

**Rationale:**
- Separation of concerns (buffer manages memory, page describes regions)
- Lightweight descriptors (can be copied freely)
- Matches CUDA memory model

**Example:**
```cpp
Buffer buffer(MemoryKind::CudaDevice, 1024 * 1024);

KvPageDesc page(1, 0, 65536, PageSize::Page64KB);
page.validate(buffer.size(), buffer.descriptor().alignment);
```

### 2. Fixed Page Sizes

**Decision:** Enum-based page sizes (16KB, 64KB, 256KB, 1MB).

**Rationale:**
- Matches common KV cache page sizes
- Type-safe (can't use arbitrary sizes)
- Easy validation

**Common sizes:**
- 16KB: Fine-grained paging
- 64KB: Typical vLLM default
- 256KB: Larger blocks for throughput
- 1MB: Huge pages for minimal overhead

### 3. Page Indices for Ordering

**Decision:** Separate `page_indices` vector for gather/scatter order.

**Rationale:**
- Pages stored in creation order
- Indices define access order
- Efficient reordering without moving pages
- Essential for gather/scatter operations

**Example:**
```cpp
// Pages added in order: 0, 1, 2, 3
table.add_page(page0);
table.add_page(page1);
table.add_page(page2);
table.add_page(page3);

// Access in different order: 3, 1, 0, 2
table.set_page_indices({3, 1, 0, 2});
auto ordered = table.get_pages_in_order();
```

### 4. Version Tracking

**Decision:** Each page has a version field.

**Rationale:**
- Detect ABA problems
- Track page updates
- Debugging aid

**Usage:**
```cpp
page.version++;  // Increment on modification
```

### 5. Comprehensive Validation

**Decision:** Multiple validation layers.

**Rationale:**
- Catch errors early
- Clear error reporting
- Safe for GPU operations

**Validation checks:**
- Size matches page size enum
- Offset within buffer bounds
- Page doesn't exceed buffer
- Alignment requirements met
- No overlapping pages
- Unique page IDs

---

## API Examples

### Create Page Table

```cpp
#include <pm_nixl/kv_page.hpp>

using namespace pm_nixl;

// Create page table with 64KB pages
KvPageTable table(PageSize::Page64KB);

// Add pages
for (size_t i = 0; i < 10; ++i) {
    KvPageDesc page(
        static_cast<uint32_t>(i + 1),  // page_id
        i * 65536,                      // offset
        65536,                          // size
        PageSize::Page64KB              // page_size
    );
    table.add_page(page);
}

// Validate all pages
const size_t buffer_size = 10 * 65536;
if (!table.validate_all(buffer_size, 512)) {
    // Handle validation error
}

// Check for overlaps
if (table.has_overlaps()) {
    // Handle overlap error
}
```

### Page Gather Preparation

```cpp
// Create page table
KvPageTable table(PageSize::Page64KB);

// Add 100 pages
for (size_t i = 0; i < 100; ++i) {
    table.add_page(KvPageDesc(
        static_cast<uint32_t>(i),
        i * 65536,
        65536,
        PageSize::Page64KB
    ));
}

// Define gather order (e.g., for attention)
std::vector<uint32_t> gather_order = {0, 5, 10, 15, 20};
table.set_page_indices(gather_order);

// Get pages in gather order
auto ordered_pages = table.get_pages_in_order();
// ordered_pages[0] = page 0
// ordered_pages[1] = page 5
// ordered_pages[2] = page 10
// etc.
```

### Page Range Operations

```cpp
// Create contiguous range
KvPageRange range(
    10,     // start_page_id
    5,      // page_count
    5 * 65536  // total_size
);

// Validate range exists in table
if (range.validate(table)) {
    // Range is valid
}
```

### Validation Helpers

```cpp
using namespace kv_page_validation;

// Check alignment
bool aligned = is_page_aligned(65536, PageSize::Page64KB);  // true

// Calculate pages needed
size_t pages = calculate_page_count(100000, PageSize::Page64KB);  // 2 pages

// Calculate offset
size_t offset = calculate_offset(5, PageSize::Page64KB);  // 327680

// Validate page
KvPageDesc page(1, 0, 65536, PageSize::Page64KB);
if (!validate_bounds(page, buffer_size)) {
    // Page exceeds buffer
}
```

---

## File Structure

```
data-plane/
├── include/pm_nixl/
│   └── kv_page.hpp               # KV page types (200 lines)
├── src/
│   └── kv_page.cpp               # Implementation (200 lines)
└── tests/
    └── test_kv_page.cpp          # Unit tests (300 lines)
```

**Total:** ~700 lines of new C++ code

---

## Test Coverage

### Test Categories (18 tests)

1. **Page Size Tests** (2 tests)
   - PageSize enum values
   - String conversion

2. **Page Descriptor Tests** (6 tests)
   - Default construction
   - Construction with parameters
   - Validation
   - Alignment check
   - End offset
   - Overlap detection

3. **Page Table Tests** (7 tests)
   - Construction
   - Add pages
   - Duplicate ID detection
   - Remove page
   - Find page
   - Validate all
   - Overlap detection
   - Clear
   - Page indices

4. **Page Range Tests** (1 test)
   - Range validation

5. **Validation Helper Tests** (1 test)
   - All helper functions

6. **Integration Tests** (1 test)
   - Integration with BufferDescriptor

### Running Tests

```bash
cd build
ctest --output-on-failure

# Run specific tests
./test_buffer --gtest_filter=KvPageTest.*
```

---

## Connection to Buffer Descriptors

### Integration Pattern

```cpp
// 1. Create buffer
Buffer buffer(MemoryKind::CudaDevice, 1024 * 1024);

// 2. Get buffer properties
auto desc = buffer.descriptor();
size_t buffer_size = desc.size;
size_t alignment = desc.alignment;

// 3. Create page table
KvPageTable table(PageSize::Page64KB);

// 4. Add pages that fit in buffer
for (size_t i = 0; i < 16; ++i) {  // 16 pages × 64KB = 1MB
    KvPageDesc page(
        static_cast<uint32_t>(i + 1),
        i * 65536,
        65536,
        PageSize::Page64KB
    );
    
    // Validate against buffer
    if (!page.validate(buffer_size, alignment)) {
        // Handle error
    }
    
    table.add_page(page);
}

// 5. Validate entire page table
table.validate_all(buffer_size, alignment);
```

### Memory Layout

```
Buffer (1MB)
├── Page 1 (offset: 0, size: 64KB)
├── Page 2 (offset: 64KB, size: 64KB)
├── Page 3 (offset: 128KB, size: 64KB)
├── ...
└── Page 16 (offset: 960KB, size: 64KB)
```

---

## Acceptance Criteria

✅ KvPageDesc struct defined  
✅ PageSize enum (16KB, 64KB, 256KB, 1MB)  
✅ KvPageTable class with full API  
✅ Page add/remove operations  
✅ Validation against buffer bounds  
✅ Overlap detection  
✅ Page indices for gather/scatter  
✅ KvPageRange struct  
✅ Validation helpers namespace  
✅ Integration with BufferDescriptor  
✅ 18 unit tests passing  
✅ Documentation complete  

---

## Use Cases

### Use Case 1: KV Cache Management

```cpp
// vLLM-style KV cache with 64KB pages
KvPageTable kv_cache(PageSize::Page64KB);

// Allocate pages for sequence
for (size_t i = 0; i < num_blocks; ++i) {
    kv_cache.add_page(KvPageDesc(
        next_page_id++,
        i * 65536,
        65536,
        PageSize::Page64KB
    ));
}
```

### Use Case 2: Page Gather for Attention

```cpp
// Gather pages for attention computation
std::vector<uint32_t> attention_order = {0, 2, 4, 6, 8};
kv_cache.set_page_indices(attention_order);

// Pass to gather kernel (Story 2.5)
// gather_kernel(kv_cache.get_pages_in_order(), ...);
```

### Use Case 3: Page Compaction

```cpp
// Identify fragmented pages
KvPageTable fragmented(PageSize::Page64KB);

// ... add non-contiguous pages ...

// Compact to contiguous region
KvPageTable compacted(PageSize::Page64KB);
size_t new_offset = 0;
for (const auto& page : fragmented.get_pages_in_order()) {
    KvPageDesc new_page = *page;
    new_page.offset = new_offset;
    compacted.add_page(new_page);
    new_offset += page->size;
}
```

---

## Known Limitations

### 1. No GPU Operations Yet

**Issue:** Descriptors only, no actual page movement.

**Impact:** Cannot perform gather/copy/compaction yet.

**Future:** Story 2.5+ will add GPU kernels.

### 2. No Page Allocation

**Issue:** Pages must be manually created.

**Impact:** No automatic page management.

**Future:** Add page allocator if needed.

### 3. No Persistence

**Issue:** Page tables exist only in memory.

**Impact:** Cannot save/restore page state.

**Future:** Add serialization if needed.

---

## Next Story (2.5)

**Story 2.5: Page Gather Primitives**

Will add:
- `page_gather()` kernel
- Scatter-gather operations
- Benchmarks for gather throughput
- Integration with transfer engine

**No changes needed to:**
- KvPageDesc (already complete)
- KvPageTable (already complete)
- Validation (already complete)

---

## Interview Talking Points

### Descriptor Design

> "I designed a descriptor-only model where pages reference buffer regions without owning memory. This separation allows lightweight page descriptors that can be copied and rearranged freely."

### Page Tables

> "The page table maintains both storage order and access order. Pages are stored in creation order, but page_indices define the access pattern for gather/scatter operations."

### Validation

> "I implemented multi-layer validation: size matching, bounds checking, alignment validation, and overlap detection. This catches errors before they reach GPU kernels."

### Integration

> "Pages integrate seamlessly with buffers - they validate against buffer descriptors and respect alignment requirements. This ensures safe GPU operations."

---

## Summary

Story 2.4 adds **KV page abstractions**:
- ✅ KvPageDesc with full metadata
- ✅ PageSize enum (16KB-1MB)
- ✅ KvPageTable with add/remove/validate
- ✅ Page indices for gather/scatter ordering
- ✅ KvPageRange for contiguous ranges
- ✅ Validation helpers
- ✅ 18 unit tests
- ✅ ~700 lines of C++ code

**Code is complete and ready for GPU kernel integration in Story 2.5!**

Next: Story 2.5 (Page Gather Primitives)
