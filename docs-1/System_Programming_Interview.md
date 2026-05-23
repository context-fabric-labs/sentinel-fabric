# Systems Programming Deep Dive: C++ & Rust for HPC Fraud Detection

**Context:** CapitalOne Three-Tier Agentic AI Fraud Detection Platform  
**Target:** Sub-5ms p99 at 24,500 TPS using zero-copy, lock-free, multi-threaded solutions  
**Approach:** Top-down — mental model first, then implementation anchored to the real system

---

# TABLE OF CONTENTS

1. [Mental Model: C++](#part-1-mental-model-c)
2. [Mental Model: Rust](#part-2-mental-model-rust)
3. [Implementation: CapitalOne Fraud Detection Data Plane (C++)](#part-3-implementation-c-data-plane)
4. [Implementation: CapitalOne Fraud Detection Control Plane (Rust)](#part-4-implementation-rust-control-plane)
5. [Integration Patterns: C++ ↔ Rust ↔ GPU](#part-5-integration-patterns)
6. [Interview Quick Reference](#part-6-interview-quick-reference)

---

# PART 1: MENTAL MODEL — C++

> **The C++ mental model for HPC:** You own every byte. The compiler trusts you. You control layout, alignment, allocation, and hardware interaction. The cost is manual lifecycle management — but with modern C++17/20, RAII and smart pointers handle most of it safely.

## 1.1 Memory: The Foundation of Everything

### How Memory Works (Hardware → Language)

```
┌────────────────────────────────────────────────────────────────────┐
│ HARDWARE MEMORY HIERARCHY                                          │
│                                                                    │
│  Registers (~0 cycles)                                             │
│      ↓                                                             │
│  L1 Cache (4 cycles, 32-64KB per core, 64-byte cache lines)       │
│      ↓                                                             │
│  L2 Cache (12 cycles, 256KB-1MB per core)                          │
│      ↓                                                             │
│  L3 Cache (40 cycles, shared across cores on same socket)          │
│      ↓                                                             │
│  DRAM (200+ cycles, NUMA: local vs remote socket)                  │
│      ↓                                                             │
│  NVMe/Disk (100,000+ cycles)                                       │
└────────────────────────────────────────────────────────────────────┘

C++ MAPPING:
  • Stack variables → registers/L1 (fast, automatic lifetime)
  • Heap (malloc/new) → DRAM (slow allocation, manual/smart-ptr lifetime)
  • mmap → DRAM pages (OS-managed, can be shared across processes)
  • Pinned memory (cudaMallocHost) → DRAM, page-locked (enables GPU DMA)
```

### Stack vs Heap vs Arena

| Allocation | Speed | Lifetime | Use Case (Fraud Platform) |
|-----------|-------|----------|---------------------------|
| **Stack** | ~0 ns (pointer bump) | Automatic (scope) | Local variables, small structs, loop counters |
| **Heap** (`new`/`malloc`) | 50-500 ns | Manual or smart-ptr | Long-lived objects, variable-size containers |
| **Arena** (bump allocator) | ~0 ns (pointer bump) | Bulk reset | Per-request allocations in hot path — **OUR PRIMARY PATTERN** |
| **Pool** (free list) | ~20 ns (pop from list) | Recycle | Fixed-size objects like ring buffer descriptors |

### malloc/free → Why We Avoid Them

```cpp
// PROBLEM: malloc has hidden costs
void* ptr = malloc(size);
// 1. May call mmap() for large allocations → syscall (~1000 ns)
// 2. Searches free-list for suitable block → variable latency
// 3. May contend with other threads on global heap lock
// 4. Fragmentation over time → unpredictable allocation times
free(ptr);
// 5. May call munmap() → another syscall
// 6. Deferred coalescing → latency spikes

// AT 24,500 TPS: even 100ns × 340 allocs/request = 34µs of allocation overhead!
```

### Memory Arena — The HPC Solution

```cpp
// SOLUTION: Arena allocator — allocate once, bump pointer, bulk reset
// This is the CORE PATTERN for our fraud scoring hot path

class ArenaAllocator {
    uint8_t* base_;      // Start of pre-allocated region
    size_t   capacity_;  // Total size (e.g., 64KB per core)
    size_t   offset_;    // Current write position (the "bump pointer")

public:
    // Allocate = just advance the pointer (essentially FREE — ~0 ns)
    void* allocate(size_t size, size_t align = 64) {
        // Round up offset to alignment boundary
        offset_ = (offset_ + align - 1) & ~(align - 1);
        void* ptr = base_ + offset_;
        offset_ += size;
        return ptr;  // No free-list search, no lock, no syscall
    }

    // Reset = set offset back to 0 (frees EVERYTHING at once — ~0 ns)
    void reset() { offset_ = 0; }
    
    // No individual free() — objects live until arena is reset
    // This is SAFE because arena lifetime = request lifetime
};
```

**Mental model:** Think of arena as a notepad — you write forward, never erase individual words, and tear off the whole page when the request is done.

---

## 1.2 RAII — Resource Acquisition Is Initialization

> **The Rule:** Every resource (memory, file, lock, socket, GPU buffer) is tied to an object's lifetime. Constructor acquires, destructor releases. No manual cleanup code scattered around.

```cpp
// RAII Pattern: constructor acquires, destructor releases
class PinnedBuffer {
    void* ptr_;
    size_t size_;
public:
    PinnedBuffer(size_t size) : size_(size) {
        cudaMallocHost(&ptr_, size);  // Acquire: allocate pinned memory
    }
    ~PinnedBuffer() {
        cudaFreeHost(ptr_);  // Release: guaranteed cleanup
    }
    // Disable copy (resource has unique ownership)
    PinnedBuffer(const PinnedBuffer&) = delete;
    PinnedBuffer& operator=(const PinnedBuffer&) = delete;
    // Enable move (transfer ownership)
    PinnedBuffer(PinnedBuffer&& other) noexcept 
        : ptr_(other.ptr_), size_(other.size_) {
        other.ptr_ = nullptr;
        other.size_ = 0;
    }
};
// When PinnedBuffer goes out of scope → ALWAYS freed, even on exceptions
```

### Smart Pointers (Modern C++ RAII for heap objects)

| Smart Pointer | Ownership | Overhead | Use in Fraud Platform |
|--------------|-----------|----------|----------------------|
| `std::unique_ptr<T>` | Single owner | Zero (raw pointer size) | Model handles, CUDA graph objects |
| `std::shared_ptr<T>` | Shared (ref-counted) | +16 bytes, atomic refcount | Shared model weights across threads |
| `std::weak_ptr<T>` | Observer (no ownership) | Same as shared | Cache entries that may be evicted |
| Raw `T*` | Non-owning view | Zero | Function parameters, arena pointers |

```cpp
// Our fraud platform uses:
auto model = std::make_unique<XGBoostModel>("fraud_v3.bin");  // Unique ownership
auto weights = std::make_shared<ModelWeights>(load_weights()); // Shared across scorers
float* arena_ptr = arena.allocate(512);  // Raw ptr OK — arena manages lifetime
```

---

## 1.3 Cache Lines and Alignment

> **The 64-Byte Rule:** CPU loads memory in 64-byte chunks (cache lines). If your data structure straddles two cache lines, you pay for two memory fetches instead of one. If two threads write to the same cache line (false sharing), cache coherency protocol bounces the line between cores (~100 cycles each time).

```cpp
// BAD: FeatureBlock may straddle cache lines, adjacent fields cause false sharing
struct BadFeatureBlock {
    float amount;           // offset 0
    float velocity_1h;      // offset 4
    // ... could end up with producer/consumer fields on same cache line
};

// GOOD: Aligned to cache line boundary, padded to prevent false sharing
struct alignas(64) FeatureBlock {
    // --- Cache line 1: core features (written by feature extractor) ---
    float amount_normalized;
    float velocity_1h;
    float velocity_24h;
    float merchant_risk;
    float device_risk;
    float geo_risk;
    float time_risk;
    float padding_[9];      // Fill to 64 bytes
    
    // --- Cache line 2: embeddings (written by embedding lookup) ---
    float merchant_embedding[16];
    
    // --- Cache line 3+: model scores (written by each scorer independently) ---
    float xgboost_score;
    float gbdt_score;
    float mlp_score;
    float rule_score;
    float ensemble_score;
};
static_assert(alignof(FeatureBlock) == 64);
```

**Why `alignas(64)` matters:** In our fraud platform, the `FeatureBlock` is the single shared data structure that ALL models read from. Aligning it ensures:
1. Single cache-line fetch for hot fields
2. No false sharing between producer (feature extractor) and consumers (models)
3. Predictable memory access patterns → stable p99 latency

### `#pragma pack` vs `alignas`

```cpp
#pragma pack(1)        // Pack tightly — minimize size (for network protocols)
struct NetworkPacket { /* fields with no padding */ };

alignas(64)            // Align to boundary — maximize cache performance
struct HotPathStruct { /* fields with controlled padding */ };

// In fraud platform: we use alignas(64) for hot-path structs
// and #pragma pack for wire-format parsing (zero-copy network parse)
```

---

## 1.4 NUMA-Aware Programming

> **NUMA = Non-Uniform Memory Access.** In multi-socket servers, each CPU socket has its own local DRAM. Accessing remote DRAM (other socket) costs ~2× the latency. Our fraud nodes have 2 sockets × 96 cores = 192 cores. Every core must access local memory.

```
┌──────────────────────────────────────────────────────────┐
│ DUAL-SOCKET SERVER (Our Fraud Scoring Node)              │
│                                                          │
│  Socket 0 (NUMA Node 0)    Socket 1 (NUMA Node 1)       │
│  ┌──────────────────┐     ┌──────────────────┐          │
│  │ 96 CPU cores     │     │ 96 CPU cores     │          │
│  │ 256 GB DDR5      │     │ 256 GB DDR5      │          │
│  │ GPU 0 (L40S)     │     │ GPU 1 (optional) │          │
│  │ NIC mlx5_0       │     │ NIC mlx5_1       │          │
│  └────────┬─────────┘     └────────┬─────────┘          │
│           │                        │                     │
│           └──── QPI/UPI ───────────┘                     │
│                 (~80ns penalty for cross-socket access)   │
└──────────────────────────────────────────────────────────┘
```

```cpp
#include <numa.h>

// Allocate memory on a specific NUMA node
void* numa_local_alloc(size_t size, int node) {
    void* ptr = numa_alloc_onnode(size, node);
    // Pre-fault pages so they're actually mapped to local node
    memset(ptr, 0, size);
    return ptr;
}

// Pin current thread to a specific core (and thus its NUMA node)
void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

// Our pattern: each fraud scoring worker is pinned to a core,
// its arena is allocated on the same NUMA node as that core.
class PerCoreWorker {
    int core_id_;
    int numa_node_;
    ArenaAllocator arena_;  // Allocated on local NUMA node
    
public:
    PerCoreWorker(int core_id) : core_id_(core_id) {
        numa_node_ = numa_node_of_cpu(core_id);
        pin_to_core(core_id);
        void* mem = numa_alloc_onnode(ARENA_SIZE, numa_node_);
        arena_ = ArenaAllocator(mem, ARENA_SIZE);
    }
};
```

**TODO:** Study `numactl --hardware`, `lstopo`, and how K8s Topology Manager enforces NUMA locality for pods.

---

## 1.5 POSIX Shared Memory / mmap

> **Shared memory** allows multiple processes to access the same physical memory pages without copying. In our fraud platform, the Rust gateway and C++ scoring engine share the FeatureBlock through shared memory — zero serialization.

```cpp
#include <sys/mman.h>
#include <fcntl.h>

// CREATE shared memory region (C++ scoring engine startup)
int fd = shm_open("/sentinel_hotpath", O_CREAT | O_RDWR, 0660);
ftruncate(fd, REGION_SIZE);  // Set size

void* ptr = mmap(
    nullptr,                    // Let OS choose address
    REGION_SIZE,                // Size of mapping
    PROT_READ | PROT_WRITE,    // Read + Write access
    MAP_SHARED,                // Visible to other processes mapping same fd
    fd,                        // File descriptor from shm_open
    0                          // Offset
);

// Enable huge pages for TLB efficiency
madvise(ptr, REGION_SIZE, MADV_HUGEPAGE);

// Pre-fault all pages (avoid page faults during hot path)
memset(ptr, 0, REGION_SIZE);

// Now: Rust gateway can also shm_open("/sentinel_hotpath") and get the SAME pages
// Both processes read/write the same FeatureBlock without any copy or serialization!
```

### Key mmap Flags

| Flag | Purpose | Our Usage |
|------|---------|-----------|
| `MAP_SHARED` | Changes visible to other processes | Gateway ↔ Engine IPC |
| `MAP_PRIVATE` | Copy-on-write, process-local | Model file loading |
| `MAP_ANONYMOUS` | Not backed by file | Arena memory |
| `MAP_HUGETLB` | Use huge pages (2MB/1GB) | Large arenas for TLB reduction |
| `MAP_POPULATE` | Pre-fault all pages | Model loading (avoid runtime faults) |
| `MADV_HUGEPAGE` | Hint: try transparent huge pages | Hot-path shared regions |
| `MADV_SEQUENTIAL` | Hint: sequential access | Model weight loading |

---

## 1.6 SIMD Zero-Copy Parsing

> **SIMD (Single Instruction, Multiple Data):** Process 16-64 bytes in one CPU instruction instead of one byte at a time. We use it to find delimiters in transaction messages without copying the data.

```cpp
#include <immintrin.h>  // AVX2 intrinsics

// Find the position of a delimiter in a buffer — processes 32 bytes per iteration
inline size_t simd_find_char(const char* buf, size_t len, char target) {
    const __m256i target_vec = _mm256_set1_epi8(target);  // Broadcast target to all 32 lanes
    
    size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        // Load 32 bytes from buffer (no copy — reads in-place)
        __m256i chunk = _mm256_loadu_si256((__m256i*)(buf + i));
        // Compare each byte against target
        __m256i cmp = _mm256_cmpeq_epi8(chunk, target_vec);
        // Collapse comparison result to 32-bit mask (one bit per byte)
        uint32_t mask = _mm256_movemask_epi8(cmp);
        if (mask != 0) {
            return i + __builtin_ctz(mask);  // Position of first match
        }
    }
    // Scalar fallback for remaining bytes
    for (; i < len; i++) {
        if (buf[i] == target) return i;
    }
    return len;  // Not found
}

// Zero-copy transaction parser: returns views (pointers) into the original buffer
struct TxnFieldView {
    const char* data;  // Points INTO the receive buffer — no allocation!
    uint32_t length;
};

struct TxnView {
    TxnFieldView card_bin;
    TxnFieldView amount;
    TxnFieldView merchant_id;
    TxnFieldView device_id;
    // ... all fields are pointers into the original network buffer
};

TxnView parse_transaction(const char* buf, size_t len) {
    TxnView view;
    size_t pos = 0;
    
    // Find first delimiter using SIMD
    size_t delim = simd_find_char(buf + pos, len - pos, '|');
    view.card_bin = {buf + pos, (uint32_t)delim};
    pos += delim + 1;
    
    delim = simd_find_char(buf + pos, len - pos, '|');
    view.amount = {buf + pos, (uint32_t)delim};
    pos += delim + 1;
    
    // ... remaining fields
    return view;  // All fields point into original buffer — ZERO COPIES
}
```

**Key SIMD instruction families:**

| Family | Width | Intrinsic Prefix | Use Case |
|--------|-------|-----------------|----------|
| SSE4.2 | 128-bit (16 bytes) | `_mm_` | String search, CRC32 |
| AVX2 | 256-bit (32 bytes) | `_mm256_` | Our parsing, feature extraction |
| AVX-512 | 512-bit (64 bytes) | `_mm512_` | Tree model traversal (Broadcom) |
| NEON | 128-bit | `v*q_*` | Apple Silicon audio features |

**TODO:** Practice writing a SIMD `memchr` equivalent, and understand `_mm256_loadu_si256` (unaligned) vs `_mm256_load_si256` (aligned — faster but requires 32-byte alignment).

---

## 1.7 Lock-Free Data Structures: SPSC Queue

> **Lock-free = no mutexes.** Threads coordinate through atomic operations and memory ordering. The simplest lock-free pattern is SPSC (Single Producer, Single Consumer) — one thread writes, one thread reads, no contention.

### Atomic Memory Ordering (Essential to understand lock-free code)

| Ordering | Guarantee | Cost | When to Use |
|----------|----------|------|-------------|
| `Relaxed` | None (compiler/CPU can reorder) | Cheapest | Counters, statistics |
| `Acquire` | All reads AFTER this see writes that happened BEFORE the paired Release | Medium | Consumer: load head/tail |
| `Release` | All writes BEFORE this are visible to threads that Acquire | Medium | Producer: store head/tail |
| `AcqRel` | Both Acquire + Release | Medium | Read-modify-write (CAS) |
| `SeqCst` | Total global order across all threads | Expensive | Rarely needed |

**The SPSC Contract:**
- Producer only writes to `tail`, reads `head` with Acquire
- Consumer only writes to `head`, reads `tail` with Acquire
- Data is visible because: Producer writes data → Release-stores tail → Consumer Acquire-loads tail → reads data

```cpp
template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Must be power of 2");
    
    alignas(64) std::atomic<size_t> head_{0};  // Consumer advances this
    alignas(64) std::atomic<size_t> tail_{0};  // Producer advances this
    alignas(64) T slots_[Capacity];

public:
    // PRODUCER SIDE
    bool try_push(const T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) & (Capacity - 1);
        
        // Check if full: compare next write position against consumer's read position
        if (next == head_.load(std::memory_order_acquire)) {
            return false;  // Queue is full — backpressure
        }
        
        slots_[tail] = item;  // Write data BEFORE advancing tail
        tail_.store(next, std::memory_order_release);  // Publish: "data is ready"
        return true;
    }
    
    // CONSUMER SIDE
    bool try_pop(T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        
        // Check if empty: compare read position against producer's write position
        if (head == tail_.load(std::memory_order_acquire)) {
            return None;  // Queue is empty — no work available
        }
        
        item = slots_[head];  // Read data BEFORE advancing head
        head_.store((head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
};
```

**Why `alignas(64)` on head/tail?** Without it, `head_` and `tail_` could share a cache line. When producer writes `tail_` and consumer writes `head_`, the cache line bounces between cores (false sharing) — adding ~100 cycles per operation.

**In our fraud platform:** The SPSC ring carries 32-byte descriptors (not payloads). Payloads stay in the arena. This means the ring only moves metadata — keeping it cache-friendly.

---

## 1.8 Templates

> Templates generate specialized code at compile time. One generic implementation → N concrete versions for different types.

```cpp
// Generic arena-backed pool for any fixed-size object
template <typename T, size_t PoolSize = 1024>
class ObjectPool {
    alignas(64) T storage_[PoolSize];
    std::atomic<size_t> free_head_{0};
    size_t free_list_[PoolSize];
    
public:
    ObjectPool() {
        for (size_t i = 0; i < PoolSize; i++) free_list_[i] = i;
    }
    
    T* acquire() {
        size_t idx = free_head_.fetch_add(1, std::memory_order_relaxed);
        return &storage_[free_list_[idx % PoolSize]];
    }
    
    void release(T* obj) {
        size_t idx = (obj - storage_);
        // ... return to free list (simplified)
    }
};

// Usage: pool of FeatureBlocks, pool of Descriptors, etc.
ObjectPool<FeatureBlock, 4096> feature_pool;
ObjectPool<Descriptor, 8192> descriptor_pool;
```

**`static_assert` and `constexpr` with templates:**
```cpp
template <size_t N>
class RingBuffer {
    static_assert((N & (N-1)) == 0, "Capacity must be power of 2");
    // Compiler ERROR if you try RingBuffer<100> — catches bugs at compile time
};

constexpr size_t cache_line_size = 64;
constexpr size_t align_up(size_t n) { return (n + cache_line_size - 1) & ~(cache_line_size - 1); }
```

---

## 1.9 Threading: std::thread and Beyond

```cpp
// Modern C++ threading (RAII-safe, unlike raw pthreads)
#include <thread>
#include <atomic>

class ScoringWorker {
    std::thread thread_;
    std::atomic<bool> running_{true};
    int core_id_;
    
public:
    ScoringWorker(int core_id) : core_id_(core_id) {
        thread_ = std::thread(&ScoringWorker::run, this);
    }
    
    ~ScoringWorker() {
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) thread_.join();  // RAII: clean shutdown
    }
    
private:
    void run() {
        pin_to_core(core_id_);  // Pin thread to specific core
        
        PerCoreArena arena(core_id_);
        SPSCQueue<Descriptor>& input_ring = get_ring(core_id_);
        
        while (running_.load(std::memory_order_relaxed)) {
            Descriptor desc;
            if (input_ring.try_pop(desc)) {
                process(desc, arena);
                arena.reset();  // Bulk free after each request
            } else {
                _mm_pause();  // CPU hint: "I'm spinning, save power"
            }
        }
    }
};
```

**Thread vs Process:**

| Aspect | Thread | Process |
|--------|--------|---------|
| Memory | Shared address space | Separate (need IPC/shm) |
| Creation cost | ~10 µs | ~1 ms |
| Communication | Direct pointer access | shm/pipe/socket |
| Fault isolation | None (crash kills all) | Full (crash is isolated) |
| Our usage | Scoring workers | Gateway ↔ Engine (shm) |

---

## 1.10 Integration: Arrow, ONNX, FFI

### Apache Arrow IPC (Zero-Copy Cross-Process/Cross-Language)

```cpp
#include <arrow/api.h>
#include <arrow/ipc/api.h>

// Wrap existing memory as Arrow array — NO COPY
std::shared_ptr<arrow::RecordBatch> wrap_scores(
    float* scores, int64_t* decisions, int64_t count) {
    
    // Arrow Buffer wraps existing pointer (does not copy!)
    auto score_buf = arrow::Buffer::Wrap(scores, count * sizeof(float));
    auto decision_buf = arrow::Buffer::Wrap(decisions, count * sizeof(int64_t));
    
    auto score_array = std::make_shared<arrow::FloatArray>(count, score_buf);
    auto decision_array = std::make_shared<arrow::Int64Array>(count, decision_buf);
    
    auto schema = arrow::schema({
        arrow::field("score", arrow::float32()),
        arrow::field("decision", arrow::int64())
    });
    
    return arrow::RecordBatch::Make(schema, count, {score_array, decision_array});
}

// Export via C Data Interface (for Rust/Python consumption)
#include <arrow/c/bridge.h>
void export_to_ffi(std::shared_ptr<arrow::RecordBatch> batch,
                   ArrowArray* out_array, ArrowSchema* out_schema) {
    arrow::ExportRecordBatch(*batch, out_array, out_schema);
}
```

### ONNX Runtime (GPU Inference)

```cpp
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

class ONNXScorer {
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "fraud"};
    std::unique_ptr<Ort::Session> session_;
    
public:
    ONNXScorer(const char* model_path, int gpu_id) {
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        
        OrtCUDAProviderOptions cuda_opts{};
        cuda_opts.device_id = gpu_id;
        opts.AppendExecutionProvider_CUDA(cuda_opts);
        
        session_ = std::make_unique<Ort::Session>(env_, model_path, opts);
    }
    
    // Input already on GPU (from arena/pinned buffer) → Output stays on GPU
    void score(const float* d_input, float* d_output, int batch_size) {
        Ort::MemoryInfo gpu_mem("Cuda", OrtDeviceAllocator, 0, OrtMemTypeDefault);
        int64_t shape[] = {batch_size, FEATURE_DIM};
        
        auto input_tensor = Ort::Value::CreateTensor(
            gpu_mem, const_cast<float*>(d_input),
            batch_size * FEATURE_DIM, shape, 2);
        
        const char* input_names[] = {"features"};
        const char* output_names[] = {"score"};
        
        session_->Run(Ort::RunOptions{}, input_names, &input_tensor, 1,
                      output_names, 1);
    }
};
```

### FFI (C ABI Boundary for Rust ↔ C++)

```cpp
// The C ABI boundary: Rust calls into C++ hot path
// Must be extern "C" for stable ABI (no name mangling)
extern "C" {
    
// Rust gateway sends raw transaction bytes, gets back scores
int sentinel_process_batch(
    const uint8_t* input_buf,   // Points into shared memory
    size_t input_len,
    float* scores_out,          // Points into shared memory
    int8_t* decisions_out,      // Points into shared memory
    size_t max_transactions
);

// Rust asks for health status
int sentinel_get_health(
    float* gpu_utilization,
    float* arena_usage_pct,
    uint64_t* requests_processed
);

}  // extern "C"
```

**FFI rules:**
1. Only pass C-compatible types (`int`, `float`, pointers, fixed-size arrays)
2. No `std::string`, `std::vector`, or any C++ objects across the boundary
3. Use `repr(C)` on Rust side to match C struct layout
4. Ownership must be clear: who allocates, who frees?

---

## 1.11 OOP & Polymorphism (Used Sparingly in HPC)

```cpp
// Virtual dispatch adds ~5ns per call (vtable lookup)
// In hot path: AVOID virtual functions. Use templates or function pointers instead.

// COLD PATH (model loading, configuration): virtual is fine
class ModelBackend {
public:
    virtual ~ModelBackend() = default;
    virtual float score(const FeatureBlock& features) = 0;
    virtual void reload(const std::string& path) = 0;
};

class XGBoostBackend : public ModelBackend { /* ... */ };
class GBDTBackend : public ModelBackend { /* ... */ };

// HOT PATH: use compile-time polymorphism (templates) or direct calls
template <typename Model>
float score_with(Model& model, const FeatureBlock& features) {
    return model.score(features);  // No vtable lookup — inlined at compile time
}
```

---

## 1.12 Macros and Preprocessor (Minimal Use)

```cpp
// Useful macros for HPC:
#define CACHE_ALIGNED alignas(64)
#define likely(x)   __builtin_expect(!!(x), 1)  // Branch prediction hint
#define unlikely(x) __builtin_expect(!!(x), 0)

// Hot path: use likely/unlikely for branch prediction
if (likely(ring.try_push(desc))) {
    // Fast path: 99.9% of the time
} else {
    // Slow path: overflow handling
    handle_backpressure();
}
```

---

# PART 2: MENTAL MODEL — RUST

> **The Rust mental model for HPC:** You own every byte, AND the compiler proves at compile time that no data races, use-after-free, or double-free can happen. Zero-cost abstractions mean the compiler generates code as fast as hand-written C — but with safety guarantees.

## 2.1 Ownership and Borrowing — The Core Innovation

### The Three Rules

```rust
// RULE 1: Every value has exactly ONE owner
let data = vec![1, 2, 3];  // `data` owns the Vec
let moved = data;           // Ownership MOVES to `moved`
// println!("{:?}", data);  // COMPILE ERROR: data was moved!

// RULE 2: You can have EITHER:
//   - One mutable reference (&mut T)
//   - OR any number of immutable references (&T)
//   ...but never both at the same time.

let mut buffer = vec![0u8; 1024];
let slice = &buffer[..];     // Immutable borrow
// buffer.push(42);           // COMPILE ERROR: can't mutate while borrowed!

// RULE 3: References must never outlive the data they point to (lifetimes)
fn get_ref<'a>(data: &'a [u8]) -> &'a [u8] {
    &data[0..10]  // Returned reference lives as long as input — compiler checks this
}
```

### How This Maps to Our Fraud Platform

```rust
// Zero-copy transaction parsing — borrowing from network buffer
struct TxnView<'buf> {
    card_bin: &'buf [u8],      // Borrows from network buffer
    amount: &'buf [u8],        // Borrows from network buffer
    merchant_id: &'buf [u8],   // Borrows from network buffer
}

impl<'buf> TxnView<'buf> {
    fn parse(raw: &'buf [u8]) -> Self {
        // All fields are REFERENCES into `raw` — zero allocation
        // Lifetime 'buf guarantees: TxnView can't outlive the network buffer
        TxnView {
            card_bin: &raw[0..16],
            amount: &raw[16..24],
            merchant_id: &raw[24..40],
        }
    }
}
// When network buffer is recycled, all TxnViews are invalidated — at COMPILE TIME
```

---

## 2.2 Structs, Enums, and repr(C)

```rust
// Struct — like C struct but with safety guarantees
#[repr(C, align(64))]  // C-compatible layout for FFI + cacheline aligned
pub struct FeatureBlock {
    pub amount_normalized: f32,
    pub velocity_1h: f32,
    pub velocity_24h: f32,
    pub merchant_risk: f32,
    pub device_risk: f32,
    pub geo_risk: f32,
    pub time_risk: f32,
    pub _padding: [f32; 9],
    pub merchant_embedding: [f32; 16],
}

// Enum — tagged union (compiler ensures you handle all cases)
pub enum ScoringDecision {
    Approve { score: f32 },
    Decline { score: f32, reason: DeclineReason },
    Review { score: f32, risk_factors: Vec<RiskFactor> },
}

// Pattern matching — exhaustive (compiler error if you miss a case)
fn handle_decision(decision: ScoringDecision) -> HttpResponse {
    match decision {
        ScoringDecision::Approve { score } => respond_200(score),
        ScoringDecision::Decline { score, reason } => respond_403(score, reason),
        ScoringDecision::Review { score, .. } => queue_for_review(score),
    }
}
```

---

## 2.3 Error Handling: Result and Option

```rust
// Rust has NO exceptions. Errors are values (Result<T, E>).
// Option<T> = value may be absent (replaces null pointers)

pub enum Result<T, E> {
    Ok(T),   // Success with value
    Err(E),  // Failure with error
}

pub enum Option<T> {
    Some(T),  // Value present
    None,     // Value absent
}

// The ? operator: propagate errors without boilerplate
async fn route_request(req: Request) -> Result<Response, GatewayError> {
    let backend = find_backend(&req)?;           // Returns Err early if no backend
    let permit = admission.check(&req)?;         // Returns Err if rejected
    let response = backend.send(req).await?;     // Returns Err on timeout/failure
    Ok(response)                                  // Success path
}

// In hot path: avoid Result allocation. Use raw return codes or Option.
fn try_pop(ring: &SPSCRing) -> Option<Descriptor> {
    // Option is zero-cost (same size as T for most types due to niche optimization)
}
```

---

## 2.4 Traits — Rust's Interface System

```rust
// Trait = interface definition (like C++ abstract class but zero-cost)
pub trait Backend: Send + Sync {
    fn health(&self) -> HealthStatus;
    fn route(&self, key: &str) -> Option<&str>;
    fn send(&self, req: Request) -> impl Future<Output = Result<Response, Error>>;
}

// Implementation for specific backend types
pub struct VLLMBackend { /* ... */ }
impl Backend for VLLMBackend {
    fn health(&self) -> HealthStatus { /* ... */ }
    fn route(&self, key: &str) -> Option<&str> { /* ... */ }
    async fn send(&self, req: Request) -> Result<Response, Error> { /* ... */ }
}

// Static dispatch (zero-cost, monomorphized at compile time):
fn score<B: Backend>(backend: &B, req: Request) -> Result<Response, Error> {
    backend.send(req)  // Compiler generates specialized code for each B
}

// Dynamic dispatch (1 vtable lookup, used when type not known at compile time):
fn route_dynamic(backends: &[Box<dyn Backend>], req: Request) {
    for b in backends {
        if b.health().is_healthy() { /* ... */ }
    }
}
```

---

## 2.5 Smart Pointers in Rust

| Type | Ownership | Thread Safety | Use in Fraud Platform |
|------|-----------|---------------|----------------------|
| `Box<T>` | Single owner, heap | Not shared | Large objects, trait objects |
| `Rc<T>` | Shared, ref-counted | Single-thread only | Config shared within one task |
| `Arc<T>` | Shared, ref-counted | Thread-safe (atomic) | Backend registry, shared state |
| `Arc<RwLock<T>>` | Shared + mutable | Thread-safe | Pod registry, session map |
| `&T` / `&mut T` | Borrowed (no ownership) | Lifetime-checked | Hot-path function params |

```rust
// Our gateway's shared state pattern:
pub struct AppState {
    pub backends: Arc<RwLock<Vec<BackendInfo>>>,     // Updated by health checker
    pub sessions: Arc<DashMap<String, SessionInfo>>,  // Lock-free concurrent map
    pub config: Arc<GatewayConfig>,                   // Immutable after init
}
```

---

## 2.6 Async/Await: Tokio Runtime

> **Tokio** is Rust's async runtime. Each "task" is like a lightweight thread (~8KB stack vs ~8MB for OS thread). Perfect for handling 50,000+ concurrent connections in our gateway.

```rust
use tokio::net::TcpListener;
use axum::{Router, routing::post};

#[tokio::main]
async fn main() {
    let app = Router::new()
        .route("/v1/score", post(handle_score))
        .route("/health", get(health_check));
    
    // Bind to all interfaces, port 8080
    let listener = TcpListener::bind("0.0.0.0:8080").await.unwrap();
    axum::serve(listener, app).await.unwrap();
}

async fn handle_score(body: Bytes) -> impl IntoResponse {
    // This function can handle 50K+ concurrent requests
    // because each is a cheap Tokio task, not an OS thread
    
    let backend = select_backend(&body).await;
    
    // tokio::select! — race multiple futures, cancel losers
    tokio::select! {
        response = backend.score(&body) => response,
        _ = tokio::time::sleep(Duration::from_millis(5)) => {
            StatusCode::GATEWAY_TIMEOUT  // SLA enforcement!
        }
    }
}
```

### Tokio Key Concepts

| Concept | What It Does | Our Usage |
|---------|-------------|-----------|
| `spawn` | Create a new async task | Background health checker |
| `select!` | Race multiple futures | Timeout enforcement, cancellation |
| `JoinHandle` | Wait for task completion | Graceful shutdown |
| `mpsc` channel | Multi-producer, single-consumer | Work distribution |
| `watch` channel | Broadcast latest value | Config updates |
| `Semaphore` | Limit concurrency | GPU admission control |

---

## 2.7 Channels: MPSC and Beyond

```rust
use tokio::sync::mpsc;

// MPSC: Multiple producers send work to one consumer
let (tx, mut rx) = mpsc::channel::<ScoringRequest>(1024);

// Multiple request handlers send to the channel
let tx_clone = tx.clone();
tokio::spawn(async move {
    tx_clone.send(request).await.unwrap();
});

// Single consumer processes work
tokio::spawn(async move {
    while let Some(req) = rx.recv().await {
        process(req).await;
    }
});

// For our SPSC ring buffer: we use raw atomics (not channels)
// because Tokio channels have async overhead (~200ns) that we can't afford in the hot path
```

---

## 2.8 Shared Memory in Rust (memmap2)

```rust
use memmap2::{MmapMut, MmapOptions};
use std::fs::OpenOptions;

// Open the same shared memory region created by C++
pub struct SharedRegion {
    mmap: MmapMut,
}

impl SharedRegion {
    pub fn open(name: &str, size: usize) -> std::io::Result<Self> {
        let path = format!("/dev/shm/{}", name);
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)?;
        
        let mmap = unsafe { MmapOptions::new().len(size).map_mut(&file)? };
        Ok(Self { mmap })
    }
    
    // Read a C-compatible struct from shared memory
    pub fn read_feature_block(&self, offset: usize) -> &FeatureBlock {
        unsafe {
            &*(self.mmap.as_ptr().add(offset) as *const FeatureBlock)
        }
    }
    
    // Write into shared memory
    pub fn write_request(&mut self, offset: usize, data: &[u8]) {
        self.mmap[offset..offset + data.len()].copy_from_slice(data);
    }
}
```

---

## 2.9 Iterators — Zero-Cost Abstraction

```rust
// Rust iterators compile to the SAME assembly as hand-written C loops
// The compiler eliminates all iterator overhead (inlining + loop fusion)

let total_risk: f32 = features.iter()
    .filter(|f| f.risk_score > 0.5)
    .map(|f| f.risk_score * f.weight)
    .sum();

// This compiles to a single tight loop — no allocations, no function call overhead
// Equivalent to manually writing:
// float total = 0;
// for (int i = 0; i < n; i++) {
//     if (features[i].risk_score > 0.5)
//         total += features[i].risk_score * features[i].weight;
// }
```

---

## 2.10 Unsafe Rust — When You Need Raw Control

```rust
// unsafe = "I'm asserting invariants the compiler can't verify"
// Used for: FFI, raw pointers, SIMD, shared memory, lock-free structures

// Our arena allocator MUST use unsafe (raw pointer arithmetic)
pub struct Arena {
    base: *mut u8,
    offset: AtomicUsize,
    capacity: usize,
}

impl Arena {
    pub fn allocate(&self, size: usize, align: usize) -> *mut u8 {
        let offset = self.offset.fetch_add(
            align_up(size, align), 
            Ordering::Relaxed
        );
        unsafe { self.base.add(offset) }  // unsafe: raw pointer arithmetic
    }
}

// RULES for unsafe:
// 1. Minimize unsafe blocks (wrap in safe API)
// 2. Document why it's safe (invariants you maintain)
// 3. Use #[deny(unsafe_op_in_unsafe_fn)] to require explicit unsafe inside unsafe fns
```

---

## 2.11 Axum Web Framework

```rust
use axum::{Router, routing::{get, post}, extract::State, Json};
use std::sync::Arc;

pub fn create_router(state: Arc<AppState>) -> Router {
    Router::new()
        .route("/v1/completions", post(handle_completion))
        .route("/v1/score", post(handle_score))
        .route("/health", get(health_check))
        .route("/metrics", get(prometheus_metrics))
        .with_state(state)
}

async fn handle_score(
    State(state): State<Arc<AppState>>,
    Json(req): Json<ScoreRequest>,
) -> Result<Json<ScoreResponse>, AppError> {
    // 1. Admission control
    let permit = state.admission.try_acquire(&req)?;
    
    // 2. Route to best backend (consistent hashing for KV locality)
    let backend = state.router.select(&req)?;
    
    // 3. Forward with timeout
    let response = tokio::time::timeout(
        Duration::from_millis(req.timeout_ms),
        backend.forward(req),
    ).await??;
    
    Ok(Json(response))
}
```

---

# PART 3: IMPLEMENTATION — C++ DATA PLANE

> This section shows how the mental model concepts combine into the actual fraud scoring hot path. Each code block references which mental model concept (§1.x) it applies.

## 3.1 System Architecture (C++ Hot Path)

```
┌─────────────────────────────────────────────────────────────────────────┐
│ C++ DATA PLANE: Per-Core Fraud Scoring Engine                           │
│                                                                         │
│  Network Buffer (recv'd by kernel, DMA'd to NIC buffer)                 │
│       │                                                                 │
│       ▼  §1.6 SIMD Parser                                              │
│  TxnView (zero-copy: pointers into network buffer)                      │
│       │                                                                 │
│       ▼  §1.2 Arena (§1.4 NUMA-local)                                  │
│  FeatureBlock (§1.3 cacheline-aligned, all features)                    │
│       │                                                                 │
│       ├──▶ XGBoost Scorer (CPU, reads FeatureBlock)                     │
│       ├──▶ GBDT Scorer (CPU, reads FeatureBlock)                        │
│       ├──▶ Rule Engine (CPU, reads FeatureBlock)                        │
│       └──▶ GPU MLP (CUDA Graph, pinned buffer → GPU → pinned buffer)   │
│       │                                                                 │
│       ▼  §1.7 SPSC Ring (lock-free)                                    │
│  Descriptor → Consumer (publishes Arrow event)                          │
│       │                                                                 │
│       ▼  §1.10 Arrow IPC                                               │
│  Arrow RecordBatch (zero-copy to Rust gateway / Tier 2 / Analytics)     │
└─────────────────────────────────────────────────────────────────────────┘
```

## 3.2 Complete Per-Core Worker Implementation

```cpp
// fraud_scoring_engine.h — The complete hot-path engine

#include <cstdint>
#include <atomic>
#include <immintrin.h>
#include <numa.h>
#include <sys/mman.h>
#include <cuda_runtime.h>

// §1.3: Cacheline-aligned feature block — ONE struct feeds ALL models
struct alignas(64) FeatureBlock {
    // Cache line 1: transaction features
    float amount_normalized;
    float velocity_1h;
    float velocity_24h;
    float velocity_7d;
    float merchant_risk_score;
    float device_risk_score;
    float geo_anomaly_score;
    float time_pattern_score;
    float channel_risk;
    float card_age_factor;
    float avg_transaction_amount;
    float transaction_count_24h;
    float _pad1[4];
    
    // Cache line 2: precomputed embeddings (from feature store lookup)
    float merchant_embedding[16];
    
    // Cache line 3: model outputs (written by each scorer independently)
    float xgboost_score;
    float gbdt_score;
    float mlp_score;
    float rule_score;
    float velocity_score;
    float ensemble_final;
    int8_t decision;  // 0=approve, 1=decline, 2=review
    uint8_t confidence;
    float _pad3[8];
};
static_assert(sizeof(FeatureBlock) % 64 == 0, "Must be multiple of cache line");

// §1.4: NUMA-pinned arena allocator
class NumaArena {
    uint8_t* base_;
    size_t capacity_;
    size_t offset_;
    int numa_node_;

public:
    NumaArena(int numa_node, size_t capacity) 
        : numa_node_(numa_node), capacity_(capacity), offset_(0) {
        base_ = static_cast<uint8_t*>(numa_alloc_onnode(capacity, numa_node));
        madvise(base_, capacity_, MADV_HUGEPAGE);
        // Pre-fault: touch every page to ensure physical allocation
        for (size_t i = 0; i < capacity_; i += 4096) {
            base_[i] = 0;
        }
    }
    
    ~NumaArena() { numa_free(base_, capacity_); }
    
    void* allocate(size_t size, size_t align = 64) {
        offset_ = (offset_ + align - 1) & ~(align - 1);
        if (offset_ + size > capacity_) return nullptr;  // Overflow = reject request
        void* ptr = base_ + offset_;
        offset_ += size;
        return ptr;
    }
    
    void reset() { offset_ = 0; }  // Bulk free — O(1)
    float usage_pct() const { return (float)offset_ / capacity_ * 100.0f; }
};

// §1.6: Zero-copy transaction view
struct TxnView {
    const char* card_bin;     uint16_t card_bin_len;
    const char* amount;       uint16_t amount_len;
    const char* merchant_id;  uint16_t merchant_id_len;
    const char* device_id;    uint16_t device_id_len;
    const char* mcc;          uint16_t mcc_len;
    const char* country;      uint16_t country_len;
    uint64_t timestamp_ns;
};

// §1.6: SIMD-accelerated field finder
inline const char* find_field_end(const char* start, size_t max_len) {
    const __m256i pipe = _mm256_set1_epi8('|');
    for (size_t i = 0; i + 32 <= max_len; i += 32) {
        __m256i chunk = _mm256_loadu_si256((__m256i*)(start + i));
        uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, pipe));
        if (mask) return start + i + __builtin_ctz(mask);
    }
    // Scalar fallback
    for (size_t i = 0; i < max_len; i++) {
        if (start[i] == '|') return start + i;
    }
    return start + max_len;
}

TxnView parse_transaction_simd(const char* buf, size_t len) {
    TxnView view{};
    const char* pos = buf;
    const char* end;
    
    end = find_field_end(pos, len - (pos - buf));
    view.card_bin = pos; view.card_bin_len = end - pos;
    pos = end + 1;
    
    end = find_field_end(pos, len - (pos - buf));
    view.amount = pos; view.amount_len = end - pos;
    pos = end + 1;
    
    // ... remaining fields
    return view;
}

// §1.7: SPSC ring buffer descriptor
struct alignas(64) Descriptor {
    uint32_t arena_offset;
    uint32_t payload_len;
    float    score;
    uint64_t timestamp_ns;
    uint8_t  decision;
    uint8_t  stage_mask;
    uint8_t  _padding[42];  // Pad to full cache line
};
static_assert(sizeof(Descriptor) == 64, "One descriptor = one cache line");

// §1.7: SPSC ring (exactly as in §1.7 but with Descriptor type)
class SPSCRing {
    Descriptor* slots_;
    size_t mask_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    
public:
    SPSCRing(size_t capacity) : mask_(capacity - 1) {
        slots_ = static_cast<Descriptor*>(
            aligned_alloc(64, capacity * sizeof(Descriptor)));
    }
    
    bool push(const Descriptor& desc) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) return false;
        slots_[tail] = desc;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    
    bool pop(Descriptor& desc) {
        size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        desc = slots_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return true;
    }
};

// CUDA Graph scorer (§GPU from master syllabus)
class CudaGraphScorer {
    cudaGraph_t graph_;
    cudaGraphExec_t exec_;
    cudaStream_t stream_;
    float* d_input_;   // Device input buffer (pre-allocated)
    float* d_output_;  // Device output buffer (pre-allocated)
    float* h_input_;   // Pinned host input
    float* h_output_;  // Pinned host output
    
public:
    CudaGraphScorer(const char* model_path) {
        cudaStreamCreate(&stream_);
        cudaMalloc(&d_input_, BATCH_SIZE * FEATURE_DIM * sizeof(float));
        cudaMalloc(&d_output_, BATCH_SIZE * sizeof(float));
        cudaMallocHost(&h_input_, BATCH_SIZE * FEATURE_DIM * sizeof(float));
        cudaMallocHost(&h_output_, BATCH_SIZE * sizeof(float));
        
        // Capture the execution graph once at startup
        cudaStreamBeginCapture(stream_, cudaStreamCaptureModeGlobal);
        cudaMemcpyAsync(d_input_, h_input_, 
                        BATCH_SIZE * FEATURE_DIM * sizeof(float),
                        cudaMemcpyHostToDevice, stream_);
        // model forward pass kernels...
        launch_mlp_kernels(d_input_, d_output_, stream_);
        cudaMemcpyAsync(h_output_, d_output_, 
                        BATCH_SIZE * sizeof(float),
                        cudaMemcpyDeviceToHost, stream_);
        cudaStreamEndCapture(stream_, &graph_);
        cudaGraphInstantiate(&exec_, graph_, nullptr, nullptr, 0);
    }
    
    // Score a batch: single graph launch = ~5µs (vs 150µs without)
    void score_batch(const FeatureBlock* features, int batch_size, float* scores) {
        // Copy features to pinned buffer
        for (int i = 0; i < batch_size; i++) {
            memcpy(h_input_ + i * FEATURE_DIM, 
                   &features[i].amount_normalized,
                   FEATURE_DIM * sizeof(float));
        }
        // Launch entire captured graph
        cudaGraphLaunch(exec_, stream_);
        cudaStreamSynchronize(stream_);
        // Results in h_output_
        memcpy(scores, h_output_, batch_size * sizeof(float));
    }
    
    ~CudaGraphScorer() {
        cudaGraphExecDestroy(exec_);
        cudaGraphDestroy(graph_);
        cudaFree(d_input_);
        cudaFree(d_output_);
        cudaFreeHost(h_input_);
        cudaFreeHost(h_output_);
        cudaStreamDestroy(stream_);
    }
};

// THE COMPLETE PER-CORE WORKER (ties everything together)
class FraudScoringWorker {
    int core_id_;
    NumaArena arena_;
    SPSCRing& input_ring_;
    SPSCRing& output_ring_;
    CudaGraphScorer& gpu_scorer_;
    XGBoostModel& xgboost_;
    std::atomic<bool>& running_;
    
public:
    FraudScoringWorker(int core_id, /* ... dependencies ... */)
        : core_id_(core_id),
          arena_(numa_node_of_cpu(core_id), 64 * 1024) /* 64KB per core */ {
        // Pin thread to core
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }
    
    void run() {
        while (running_.load(std::memory_order_relaxed)) {
            Descriptor in_desc;
            if (!input_ring_.pop(in_desc)) {
                _mm_pause();  // Spin hint — save power while waiting
                continue;
            }
            
            // 1. Parse transaction (zero-copy from shared memory)
            const char* raw = shared_region_ptr_ + in_desc.arena_offset;
            TxnView txn = parse_transaction_simd(raw, in_desc.payload_len);
            
            // 2. Allocate feature block in NUMA-local arena
            auto* features = static_cast<FeatureBlock*>(
                arena_.allocate(sizeof(FeatureBlock)));
            
            // 3. Extract features (populate the block)
            extract_features(txn, features);
            
            // 4. Score with all models (all read same FeatureBlock)
            features->xgboost_score = xgboost_.predict(features);
            features->gbdt_score = gbdt_.predict(features);
            features->rule_score = rule_engine_.evaluate(features);
            // GPU MLP scored in batch (accumulated separately)
            
            // 5. Ensemble and decide
            features->ensemble_final = ensemble_score(features);
            features->decision = (features->ensemble_final > THRESHOLD) ? 1 : 0;
            
            // 6. Publish to output ring
            Descriptor out_desc{};
            out_desc.arena_offset = (uint8_t*)features - arena_.base();
            out_desc.score = features->ensemble_final;
            out_desc.decision = features->decision;
            out_desc.timestamp_ns = now_ns();
            output_ring_.push(out_desc);
            
            // 7. Reset arena (bulk free ALL allocations for this request)
            arena_.reset();
        }
    }
};

// §1.10: FFI entry point for Rust gateway
extern "C" int sentinel_process_batch(
    const uint8_t* input_buf,
    size_t input_len,
    float* scores_out,
    int8_t* decisions_out,
    size_t max_batch
) {
    // Called by Rust gateway via shared memory pointer handoff
    // input_buf points into mmap'd shared region — zero copy
    // scores_out/decisions_out point into mmap'd output region — zero copy
    // Returns number of transactions processed
    return engine_instance->process_batch(
        input_buf, input_len, scores_out, decisions_out, max_batch);
}
```

---

# PART 4: IMPLEMENTATION — RUST CONTROL PLANE

> The Rust control plane owns routing, admission, circuit breaking, streaming, and cancellation. It communicates with the C++ data plane via shared memory (zero serialization).

## 4.1 System Architecture (Rust Gateway)

```
┌─────────────────────────────────────────────────────────────────────────┐
│ RUST CONTROL PLANE: Sentinel Gateway                                    │
│                                                                         │
│  Client Request (gRPC/HTTP)                                             │
│       │                                                                 │
│       ▼  §2.6 Axum + Tokio                                             │
│  Admission Controller (§2.5 Arc<Semaphore>, token budget)               │
│       │                                                                 │
│       ▼  §2.4 Trait-based Backend Selection                             │
│  Consistent Hash Router (§2.1 &str borrows for zero-alloc routing)      │
│       │                                                                 │
│       ▼  §2.3 Result<T, E> with ? propagation                          │
│  Circuit Breaker Check                                                  │
│       │                                                                 │
│       ├──▶ HOT PATH: Write to shared memory → C++ scores → read back   │
│       │    (§2.8 memmap2, §2.10 unsafe for raw pointer ops)             │
│       │                                                                 │
│       └──▶ LLM PATH: Forward to vLLM backend (SSE streaming)           │
│            (§2.6 tokio::select! for timeout/cancellation)               │
│       │                                                                 │
│       ▼  §2.10 Arrow FFI                                                │
│  Publish Audit Event (Arrow IPC via shared memory)                      │
└─────────────────────────────────────────────────────────────────────────┘
```

## 4.2 Complete Gateway Implementation

```rust
// src/main.rs — Sentinel Fraud Gateway

use axum::{Router, routing::{get, post}, extract::State};
use std::sync::Arc;
use tokio::sync::{RwLock, Semaphore};

mod admission;
mod backends;
mod router;
mod shared_mem;
mod arrow_bridge;

// Application state shared across all request handlers
pub struct AppState {
    pub backends: Arc<RwLock<Vec<BackendInfo>>>,
    pub admission: Arc<AdmissionController>,
    pub circuit_breakers: Arc<CircuitBreakerRegistry>,
    pub shared_region: Arc<SharedRegion>,  // §2.8: mmap'd shared memory
    pub metrics: Arc<MetricsCollector>,
}

#[tokio::main]
async fn main() {
    // Initialize shared memory connection to C++ engine
    let shared_region = Arc::new(
        SharedRegion::open("sentinel_hotpath", 4 * 1024 * 1024).unwrap()
    );
    
    let state = Arc::new(AppState {
        backends: Arc::new(RwLock::new(discover_backends().await)),
        admission: Arc::new(AdmissionController::new(AdmissionConfig {
            max_concurrent_requests: 10_000,
            max_tokens_per_tenant: 50_000,
            gpu_memory_headroom_mb: 8192,
        })),
        circuit_breakers: Arc::new(CircuitBreakerRegistry::new()),
        shared_region,
        metrics: Arc::new(MetricsCollector::new()),
    });
    
    // Spawn background health checker
    let state_clone = state.clone();
    tokio::spawn(async move { health_check_loop(state_clone).await });
    
    let app = Router::new()
        .route("/v1/score", post(handle_hot_path_score))
        .route("/v1/analyze", post(handle_llm_analysis))
        .route("/health", get(health_handler))
        .with_state(state);
    
    let listener = tokio::net::TcpListener::bind("0.0.0.0:8080").await.unwrap();
    axum::serve(listener, app).await.unwrap();
}
```

## 4.3 Hot Path: Shared Memory IPC with C++

```rust
// src/shared_mem.rs — Zero-copy communication with C++ engine

use memmap2::{MmapMut, MmapOptions};
use std::sync::atomic::{AtomicU64, Ordering};

/// Layout matches C++ HotPathSharedRegion exactly (repr(C))
#[repr(C)]
struct SharedRegionLayout {
    // Input area (Rust writes, C++ reads)
    input_buffer: [u8; 4 * 1024 * 1024],
    input_len: u64,
    request_count: u64,
    
    // Output area (C++ writes, Rust reads)
    decisions: [i8; 8192],
    scores: [f32; 8192],
    output_count: u64,
    
    // Synchronization flags
    request_ready: AtomicU64,   // Rust sets to 1, C++ resets to 0
    response_ready: AtomicU64,  // C++ sets to 1, Rust resets to 0
}

pub struct SharedRegion {
    mmap: MmapMut,
}

impl SharedRegion {
    pub fn open(name: &str, size: usize) -> std::io::Result<Self> {
        let path = format!("/dev/shm/{}", name);
        let file = std::fs::OpenOptions::new()
            .read(true).write(true).open(&path)?;
        let mmap = unsafe { MmapOptions::new().len(size).map_mut(&file)? };
        Ok(Self { mmap })
    }
    
    /// Write transaction batch to shared memory, wait for C++ to score
    pub fn score_batch(&self, transactions: &[u8]) -> Vec<(f32, i8)> {
        let layout = self.as_layout_mut();
        
        // Write input (§2.10: unsafe pointer ops through safe API)
        unsafe {
            std::ptr::copy_nonoverlapping(
                transactions.as_ptr(),
                layout.input_buffer.as_mut_ptr(),
                transactions.len(),
            );
        }
        layout.input_len = transactions.len() as u64;
        
        // Signal C++ engine: "request ready"
        layout.request_ready.store(1, Ordering::Release);
        
        // Spin-wait for response (in practice: use eventfd/futex for sleep)
        while layout.response_ready.load(Ordering::Acquire) == 0 {
            std::hint::spin_loop();
        }
        
        // Read results (zero-copy: just read from mmap)
        let count = layout.output_count as usize;
        let mut results = Vec::with_capacity(count);
        for i in 0..count {
            results.push((layout.scores[i], layout.decisions[i]));
        }
        
        // Reset flags
        layout.response_ready.store(0, Ordering::Release);
        results
    }
    
    fn as_layout_mut(&self) -> &mut SharedRegionLayout {
        unsafe { &mut *(self.mmap.as_ptr() as *mut SharedRegionLayout) }
    }
}
```

## 4.4 Consistent Hash Router (KV Cache Locality)

```rust
// src/router.rs — Consistent hashing for prefix/KV cache locality

use std::collections::BTreeMap;
use std::hash::{Hash, Hasher};
use siphasher::sip::SipHasher;

pub struct ConsistentHashRing {
    ring: BTreeMap<u64, usize>,  // hash → backend index
    virtual_nodes: usize,
    backends: Vec<BackendInfo>,
}

impl ConsistentHashRing {
    pub fn new(backends: Vec<BackendInfo>, virtual_nodes: usize) -> Self {
        let mut ring = BTreeMap::new();
        for (idx, backend) in backends.iter().enumerate() {
            for vn in 0..virtual_nodes {
                let key = format!("{}:{}", backend.id, vn);
                let hash = Self::hash_key(&key);
                ring.insert(hash, idx);
            }
        }
        Self { ring, virtual_nodes, backends }
    }
    
    /// Route by tenant + model + prompt prefix hash
    /// This keeps same-session requests on same backend → KV cache reuse
    pub fn route(&self, tenant_id: &str, model: &str, prefix_hash: u64) -> Option<&BackendInfo> {
        let key = format!("{}:{}:{}", tenant_id, model, prefix_hash);
        let hash = Self::hash_key(&key);
        
        // Find next node clockwise on the ring
        let entry = self.ring.range(hash..).next()
            .or_else(|| self.ring.iter().next());  // Wrap around
        
        entry.map(|(_, &idx)| &self.backends[idx])
            .filter(|b| b.is_healthy)  // Skip unhealthy
    }
    
    fn hash_key(key: &str) -> u64 {
        let mut hasher = SipHasher::new();
        key.hash(&mut hasher);
        hasher.finish()
    }
}
```

## 4.5 Circuit Breaker

```rust
// src/backends/circuit_breaker.rs

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::time::{Duration, Instant};
use tokio::sync::RwLock;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CircuitState { Closed, Open, HalfOpen }

pub struct CircuitBreaker {
    state: RwLock<CircuitState>,
    failure_count: AtomicU32,
    success_count: AtomicU32,
    last_failure: AtomicU64,
    config: CircuitConfig,
}

pub struct CircuitConfig {
    pub failure_threshold: u32,     // Open after N failures
    pub success_threshold: u32,     // Close after N successes in HalfOpen
    pub timeout: Duration,          // Stay open for this long before HalfOpen
}

impl CircuitBreaker {
    pub async fn check(&self) -> Result<(), CircuitOpenError> {
        let state = *self.state.read().await;
        match state {
            CircuitState::Closed => Ok(()),
            CircuitState::Open => {
                // Check if timeout elapsed → transition to HalfOpen
                let elapsed = Instant::now().duration_since(self.last_failure_instant());
                if elapsed > self.config.timeout {
                    *self.state.write().await = CircuitState::HalfOpen;
                    Ok(())  // Allow probe request
                } else {
                    Err(CircuitOpenError { retry_after: self.config.timeout - elapsed })
                }
            }
            CircuitState::HalfOpen => Ok(()),  // Allow limited traffic
        }
    }
    
    pub async fn record_success(&self) {
        self.failure_count.store(0, Ordering::Relaxed);
        let state = *self.state.read().await;
        if state == CircuitState::HalfOpen {
            let count = self.success_count.fetch_add(1, Ordering::Relaxed) + 1;
            if count >= self.config.success_threshold {
                *self.state.write().await = CircuitState::Closed;
            }
        }
    }
    
    pub async fn record_failure(&self) {
        self.success_count.store(0, Ordering::Relaxed);
        let count = self.failure_count.fetch_add(1, Ordering::Relaxed) + 1;
        if count >= self.config.failure_threshold {
            *self.state.write().await = CircuitState::Open;
        }
    }
}
```

## 4.6 Admission Controller

```rust
// src/admission/controller.rs

use tokio::sync::Semaphore;
use std::sync::atomic::{AtomicI64, Ordering};

pub struct AdmissionController {
    request_semaphore: Semaphore,
    active_tokens: AtomicI64,
    config: AdmissionConfig,
}

pub struct AdmissionConfig {
    pub max_concurrent_requests: usize,
    pub max_tokens_per_tenant: i64,
    pub gpu_memory_headroom_mb: u64,
}

impl AdmissionController {
    pub fn try_acquire(&self, req: &ScoreRequest) -> Result<AdmissionPermit, AdmissionError> {
        // 1. Check concurrent request limit
        let permit = self.request_semaphore.try_acquire()
            .map_err(|_| AdmissionError::TooManyRequests)?;
        
        // 2. Check token budget (LLM path)
        let estimated_tokens = estimate_tokens(&req.prompt);
        let current = self.active_tokens.load(Ordering::Relaxed);
        if current + estimated_tokens > self.config.max_tokens_per_tenant {
            return Err(AdmissionError::TokenBudgetExceeded);
        }
        self.active_tokens.fetch_add(estimated_tokens, Ordering::Relaxed);
        
        // 3. Check GPU memory headroom (from DCGM metrics)
        if get_gpu_free_memory_mb() < self.config.gpu_memory_headroom_mb {
            return Err(AdmissionError::GpuMemoryPressure);
        }
        
        Ok(AdmissionPermit { 
            _semaphore_permit: permit,
            tokens: estimated_tokens,
        })
    }
}

// RAII: when permit is dropped, resources are released automatically
pub struct AdmissionPermit {
    _semaphore_permit: tokio::sync::SemaphorePermit<'static>,
    tokens: i64,
}

impl Drop for AdmissionPermit {
    fn drop(&mut self) {
        // Automatically release token budget when request completes
        GLOBAL_ADMISSION.active_tokens.fetch_sub(self.tokens, Ordering::Relaxed);
    }
}
```

## 4.7 SSE Streaming with Cancellation

```rust
// src/api/proxy.rs — Stream LLM tokens, cancel on client disconnect

use axum::response::sse::{Event, Sse};
use tokio_stream::StreamExt;

pub async fn handle_llm_analysis(
    State(state): State<Arc<AppState>>,
    body: axum::body::Bytes,
) -> Sse<impl futures::Stream<Item = Result<Event, std::convert::Infallible>>> {
    let (tx, rx) = tokio::sync::mpsc::channel(32);
    let cancel = tokio_util::sync::CancellationToken::new();
    let cancel_clone = cancel.clone();
    
    // Spawn backend communication task
    tokio::spawn(async move {
        let backend = state.router.select_llm_backend().await;
        let mut stream = backend.stream_completion(&body).await;
        
        loop {
            tokio::select! {
                // Client disconnected → cancel backend work
                _ = cancel_clone.cancelled() => {
                    backend.abort_request().await;  // FREE GPU RESOURCES
                    break;
                }
                // Got a token from backend → forward to client
                token = stream.next() => {
                    match token {
                        Some(Ok(t)) => {
                            if tx.send(Ok(Event::default().data(t))).await.is_err() {
                                cancel_clone.cancel();  // Client gone
                                break;
                            }
                        }
                        Some(Err(_)) => break,
                        None => break,  // Stream complete
                    }
                }
                // SLA timeout → abort
                _ = tokio::time::sleep(Duration::from_secs(5)) => {
                    backend.abort_request().await;
                    break;
                }
            }
        }
    });
    
    // Return SSE stream to client
    Sse::new(tokio_stream::wrappers::ReceiverStream::new(rx))
        .keep_alive(axum::response::sse::KeepAlive::default())
}
```

---

# PART 5: INTEGRATION PATTERNS

## 5.1 Rust ↔ C++ via FFI

```rust
// src/ffi.rs — Rust side of the C ABI boundary

#[repr(C)]
pub struct BatchResult {
    pub count: u32,
    pub scores: *const f32,
    pub decisions: *const i8,
}

extern "C" {
    fn sentinel_process_batch(
        input_buf: *const u8,
        input_len: usize,
        scores_out: *mut f32,
        decisions_out: *mut i8,
        max_transactions: usize,
    ) -> i32;
    
    fn sentinel_get_health(
        gpu_utilization: *mut f32,
        arena_usage_pct: *mut f32,
        requests_processed: *mut u64,
    ) -> i32;
}

// Safe wrapper
pub fn process_batch(input: &[u8], max_batch: usize) -> Result<Vec<(f32, i8)>, EngineError> {
    let mut scores = vec![0.0f32; max_batch];
    let mut decisions = vec![0i8; max_batch];
    
    let count = unsafe {
        sentinel_process_batch(
            input.as_ptr(),
            input.len(),
            scores.as_mut_ptr(),
            decisions.as_mut_ptr(),
            max_batch,
        )
    };
    
    if count < 0 {
        return Err(EngineError::ProcessingFailed(count));
    }
    
    let count = count as usize;
    Ok(scores[..count].iter().zip(decisions[..count].iter())
        .map(|(&s, &d)| (s, d))
        .collect())
}
```

## 5.2 Arrow Zero-Copy Bridge (Rust ↔ C++ ↔ Python)

```rust
// src/arrow_bridge.rs — Arrow C Data Interface for cross-language zero-copy

use arrow::ffi::{FFI_ArrowArray, FFI_ArrowSchema};
use arrow::record_batch::RecordBatch;
use arrow::array::{Float32Array, Int8Array};

/// Import Arrow data from C++ (zero-copy: shares same physical memory)
pub unsafe fn import_from_cpp(
    c_array: *mut FFI_ArrowArray,
    c_schema: *mut FFI_ArrowSchema,
) -> Result<RecordBatch, arrow::error::ArrowError> {
    let field = arrow::ffi::import_field_from_c(c_schema)?;
    let array = arrow::ffi::import_array_from_c(*c_array, field.data_type())?;
    // The returned RecordBatch references the SAME memory as C++ — true zero-copy
    RecordBatch::try_from_iter(vec![("data", array)])
}

/// Wrap arena memory as Arrow (for publishing to Tier 2/Analytics)
pub unsafe fn wrap_arena_as_arrow(
    scores_ptr: *const f32,
    decisions_ptr: *const i8,
    count: usize,
) -> RecordBatch {
    // These buffers point into the shared arena — NO COPY
    let scores_buf = arrow::buffer::Buffer::from_custom_allocation(
        std::ptr::NonNull::new(scores_ptr as *mut u8).unwrap(),
        count * 4,
        Arc::new(()),  // No-op drop (arena owns the memory)
    );
    
    let decisions_buf = arrow::buffer::Buffer::from_custom_allocation(
        std::ptr::NonNull::new(decisions_ptr as *mut u8).unwrap(),
        count,
        Arc::new(()),
    );
    
    let scores_array = Float32Array::new(scores_buf.into(), None);
    let decisions_array = Int8Array::new(decisions_buf.into(), None);
    
    let schema = Arc::new(arrow::datatypes::Schema::new(vec![
        arrow::datatypes::Field::new("score", arrow::datatypes::DataType::Float32, false),
        arrow::datatypes::Field::new("decision", arrow::datatypes::DataType::Int8, false),
    ]));
    
    RecordBatch::try_new(schema, vec![
        Arc::new(scores_array),
        Arc::new(decisions_array),
    ]).unwrap()
}
```

## 5.3 Integration Summary: What Connects Where

```
┌────────────────────────────────────────────────────────────────────────────┐
│                    COMPLETE SYSTEM INTEGRATION MAP                          │
│                                                                            │
│  CLIENT ──HTTP/gRPC──▶ RUST GATEWAY ──shm/mmap──▶ C++ ENGINE              │
│                            │                          │                    │
│                            │ §FFI (extern "C")        │ §CUDA Graphs       │
│                            │ §Arrow C Data Interface  │ §Pinned Memory     │
│                            │ §memmap2                 │ §ONNX Runtime      │
│                            │                          │                    │
│                            ▼                          ▼                    │
│                    ┌───────────────┐         ┌───────────────┐             │
│                    │ Arrow Event   │         │ GPU (L40S)    │             │
│                    │ Buffer (shm)  │         │ MLP Scoring   │             │
│                    └───────┬───────┘         └───────────────┘             │
│                            │                                               │
│            ┌───────────────┼───────────────┐                               │
│            ▼               ▼               ▼                               │
│    ┌──────────────┐ ┌──────────────┐ ┌──────────────┐                     │
│    │ Tier 2 LLM   │ │ Analytics    │ │ Retraining   │                     │
│    │ (reads Arrow) │ │ (Spark/cuDF) │ │ (PyTorch)    │                     │
│    └──────────────┘ └──────────────┘ └──────────────┘                     │
│                                                                            │
│  ZERO-COPY BOUNDARIES:                                                     │
│  • Network → TxnView (pointer into recv buffer)                           │
│  • TxnView → FeatureBlock (written into NUMA arena)                       │
│  • FeatureBlock → GPU (pinned memory → async DMA)                         │
│  • Scores → Arrow RecordBatch (Buffer::Wrap, no copy)                     │
│  • Arrow → Tier 2/Analytics (mmap'd shared memory, same physical pages)   │
└────────────────────────────────────────────────────────────────────────────┘
```

---

# PART 6: INTERVIEW QUICK REFERENCE

## 6.1 "Explain Your Zero-Copy Architecture" (60 seconds)

> "From network receive to final audit event, data is never copied — only viewed through typed pointers. The transaction arrives in a kernel receive buffer. Our SIMD parser creates a `TxnView` — just pointers into that buffer. Features are extracted directly into a cacheline-aligned `FeatureBlock` allocated from a NUMA-local arena. All CPU models (XGBoost, GBDT, rules) and the GPU MLP read from the same FeatureBlock — no per-model conversion. Scores are written back to the same arena. Finally, Arrow wraps the score buffer as a RecordBatch without copying — downstream systems (Tier 2 LLM, Spark analytics, retraining) read the same physical bytes via shared memory. The entire request allocates zero heap memory and the arena resets in O(1) after each request."

## 6.2 "Why C++ for Data Plane and Rust for Control Plane?"

| Dimension | C++ (Data Plane) | Rust (Control Plane) |
|-----------|-----------------|---------------------|
| **Why** | Explicit memory layout, SIMD intrinsics, CUDA/GPU interop, stable C ABI | Safe async concurrency, memory safety guarantees, expressive error handling |
| **What it owns** | Feature extraction, model scoring, arena management, GPU kernels | Routing, admission, circuit breaking, streaming, cancellation, FFI glue |
| **Latency budget** | <1ms per transaction | <0.5ms routing overhead |
| **Concurrency model** | Per-core workers (no sharing) | Tokio tasks (50K+ concurrent) |
| **Safety model** | Manual (discipline + code review) | Compiler-enforced (ownership + lifetimes) |

## 6.3 Key Libraries and Functions Reference

### C++ Libraries

| Library | Header/Link | Used For |
|---------|-------------|----------|
| `<numa.h>` | `-lnuma` | `numa_alloc_onnode()`, `numa_node_of_cpu()` |
| `<immintrin.h>` | Built-in | AVX2: `_mm256_loadu_si256`, `_mm256_cmpeq_epi8`, `_mm256_movemask_epi8` |
| `<sys/mman.h>` | POSIX | `mmap()`, `shm_open()`, `madvise()` |
| `<atomic>` | C++11 | `std::atomic<T>`, memory orderings |
| `<cuda_runtime.h>` | CUDA toolkit | `cudaMallocHost`, `cudaGraphLaunch`, streams |
| Apache Arrow | `arrow/api.h` | `Buffer::Wrap`, `RecordBatch`, C Data Interface |
| ONNX Runtime | `onnxruntime_cxx_api.h` | `Ort::Session`, CUDA execution provider |

### Rust Crates

| Crate | Used For |
|-------|----------|
| `tokio` | Async runtime, tasks, channels, timers, select! |
| `axum` | HTTP framework (Router, extractors, SSE) |
| `memmap2` | Memory-mapped files and shared memory |
| `arrow` | Arrow arrays, FFI (C Data Interface) |
| `serde` / `serde_json` | Config deserialization (not hot path) |
| `libc` | Raw syscalls: `mmap`, `shm_open`, `numa_*` |
| `dashmap` | Lock-free concurrent HashMap |
| `siphasher` | Fast hashing for consistent hash ring |
| `tokio-util` | CancellationToken |
| `prometheus` | Metrics export |

## 6.4 Top 10 Interview Questions & Answers

| # | Question | 30-Second Answer |
|---|----------|------------------|
| 1 | Why arena over malloc? | "At 24K TPS, 340 allocs/request × 100ns = 34µs overhead. Arena: bump pointer (~0ns) + bulk reset. Zero fragmentation, zero contention." |
| 2 | Why SPSC over mutex queue? | "SPSC is wait-free for single producer/consumer. No lock, no CAS loop, no contention. At 24K TPS, mutex would cost ~500ns + potential priority inversion." |
| 3 | Why shared memory over gRPC? | "gRPC = serialize + syscall + kernel buffer + deserialize = ~200µs minimum. Shared memory = pointer dereference = ~15ns. We save 200µs × 24K = 4.8 seconds of CPU per second." |
| 4 | How do you prevent false sharing? | "alignas(64) on hot structs. SPSC head/tail on separate cache lines. FeatureBlock sections padded to 64-byte boundaries." |
| 5 | Why Rust for the gateway? | "50K concurrent streams need async; ownership prevents use-after-free in complex routing code; Result<T,E> makes error paths explicit; no GC pauses." |
| 6 | What makes CUDA Graphs worth it? | "30 small kernels × 5µs launch = 150µs overhead. Graph captures the sequence and replays as single call = 5µs. Saves 145µs per inference within our 5ms budget." |
| 7 | How does Arrow enable zero-copy? | "Buffer::Wrap takes a raw pointer without copying. Same physical bytes serve the scoring engine, Rust gateway audit, Tier 2 LLM, Spark analytics, and PyTorch retraining." |
| 8 | What's the NUMA strategy? | "Pin worker thread to core. Allocate arena on same NUMA node. GPU and NIC are also on that node (via K8s Topology Manager). Zero cross-socket traffic." |
| 9 | How do you handle backpressure? | "Bounded SPSC ring = natural backpressure. If ring is full, producer drops or queues to overflow. Admission controller rejects before ring. Circuit breaker isolates slow backends." |
| 10 | What's unsafe in your Rust code? | "Three places: (1) memmap2 pointer access for shared memory, (2) FFI calls to C++ engine, (3) Arena pointer arithmetic. Each wrapped in a safe API with documented invariants." |

## 6.5 Concepts to Study Separately (TODOs)

- [ ] **io_uring** — async kernel I/O for network and disk (next-gen to epoll)
- [ ] **AF_XDP** — kernel-bypass networking via eBPF (used for Redis fast path)
- [ ] **DPDK** — full userspace networking (highest perf, highest complexity)
- [ ] **RDMA** — zero-copy network transfer (for cross-node Arrow/KV)
- [ ] **Protobuf / FlatBuffers** — when you DO need serialization (cold paths)
- [ ] **CFS (Completely Fair Scheduler)** — why `isolcpus` matters for latency
- [ ] **TLB and Page Tables** — why huge pages reduce latency
- [ ] **Memory Ordering formal model** — C++ `std::memory_order` in detail
- [ ] **Rust async internals** — how `Future`, `Poll`, `Waker` work under the hood
- [ ] **XGBoost tree traversal with SIMD** — AVX2 branch-free tree scoring

---

# APPENDIX: Pattern Index

| Pattern | C++ Section | Rust Section | Mental Model |
|---------|------------|-------------|--------------|
| Arena allocator | §3.2 NumaArena | §4.3 SharedRegion | §1.2, §1.4 |
| Zero-copy parse | §3.2 TxnView | §4.2 TxnView<'a> | §1.6, §2.1 |
| SPSC ring | §3.2 SPSCRing | §4.2 (via FFI to C++) | §1.7 |
| Cacheline align | §3.2 FeatureBlock | §5.1 repr(C, align(64)) | §1.3 |
| Shared memory | §3.2 shm_open | §4.3 memmap2 | §1.5, §2.8 |
| CUDA Graph | §3.2 CudaGraphScorer | (called via FFI) | §1.10 |
| Arrow IPC | §3.2 extern "C" | §5.2 arrow_bridge | §1.10 |
| Circuit breaker | — | §4.5 | §2.3, §2.5 |
| Admission control | — | §4.6 Semaphore + tokens | §2.5, §2.6 |
| SSE + cancel | — | §4.7 select! | §2.6 |
| Consistent hash | — | §4.4 BTreeMap ring | §2.1 |
| RAII | §1.2 PinnedBuffer | §2.5 Drop for Permit | §1.2 |

---

*End of Systems Programming Interview Guide*
