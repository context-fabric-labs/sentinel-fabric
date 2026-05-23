# System Programming Deep Dive for Interviews

**Focus:** C++ and Rust system programming through one top-down project: a CapitalOne-style real-time fraud detection platform.

**Goal:** Build a practical mental model for interviews. Instead of studying every C++ and Rust topic bottom-up, learn the 80% of concepts that repeatedly show up when you implement a low-latency, zero-copy, lock-free, multi-threaded, CPU/GPU fraud scoring system.

**Project Anchor:** 20K-25K transactions per second, under 50 ms end-to-end, with a Tier 1 authorization path targeting sub-5 ms p99 for the inline decision. The pipeline uses Rust for gateway/control-plane ownership, C++/CUDA for hot scoring and model integration, Apache Arrow and shared memory for zero-copy handoff, and Python for offline training and experimentation.

---

# How To Use This Guide

Read it in this order:

1. Understand the project frame and the mental model.
2. Learn the C++ and Rust concepts only as they are used in the project.
3. Walk through the implementation path module by module.
4. Use the cheat sheets and Q&A to prepare interview answers.
5. Use the TODO notes only for deeper follow-up study.

The interview target is not "I know every language feature." The target is:

> "I know how memory, ownership, CPU caches, concurrency, OS primitives, GPU execution, and serialization boundaries interact in a production low-latency system."

---

# Part I: Top-Down Mental Model

## 1. Project Frame

The fraud platform has three different latency personalities:

| Tier | Purpose | Latency Target | Programming Style |
|------|---------|----------------|-------------------|
| Tier 1 | Inline go/no-go fraud decision | Sub-5 ms p99 inside a larger 50 ms authorization budget | Rust + C++/CUDA, fixed memory, no unbounded waits |
| Tier 2 | Decline explanation and reasoning | 2-5 seconds | LLM/reasoning path, async, batchable |
| Tier 3 | Case triage and remediation | 5-10 seconds | Agent/tool workflow, human/audit integration |

This guide is mostly about Tier 1 and the data handoff from Tier 1 to slower consumers.

```text
Network bytes
    |
    v
Rust gateway: route, admit, parse borrowed views
    |
    v
Per-core arena: FeatureBlock, descriptors, trace metadata
    |
    +--> CPU scorers: rules, scorecard, XGBoost/GBDT
    |
    +--> C++/CUDA scorer: ONNX/TensorRT, FAISS/vector lookup, CUDA Graphs
    |
    v
Decision: approve, decline, challenge
    |
    v
Arrow event buffer in shared memory
    |
    +--> Tier 2 explanation
    +--> audit
    +--> analytics
    +--> retraining
```

## 2. The Core Mental Model

Every systems-programming concept in this guide fits into one of seven buckets:

| Bucket | Interview Question It Answers | C++ Tooling | Rust Tooling |
|--------|-------------------------------|-------------|--------------|
| Ownership | Who frees memory, file handles, GPU resources, sockets? | RAII, destructors, `unique_ptr`, `shared_ptr` | Ownership, borrowing, `Drop`, `Box`, `Arc` |
| Layout | How are bytes arranged so CPU/GPU can read them fast? | `alignas`, POD structs, arenas, SIMD-friendly arrays | `#[repr(C)]`, `#[repr(align)]`, slices, arenas |
| Locality | Are CPU, memory, NIC, and GPU on the same NUMA path? | `numa_alloc_onnode`, `sched_setaffinity`, hugepages | `core_affinity`, `libc`, per-core workers |
| Coordination | How do stages communicate without locks? | atomics, SPSC ring, acquire/release | atomics, SPSC/MPSC channels, Tokio tasks |
| Boundaries | How do C++, Rust, Python, and processes share data? | C ABI, Arrow C Data, `mmap`, `shm_open` | `extern "C"`, `memmap2`, Arrow FFI |
| Acceleration | Where does SIMD/GPU actually help? | AVX2/AVX-512, CUDA, cuBLAS, CUTLASS, ONNX Runtime | FFI to C++/CUDA, `std::simd` where available |
| Failure | What happens under overload or backend failure? | timeouts, status codes, RAII cleanup | `Result`, circuit breakers, cancellation |

## 3. Language Ownership In This Project

| Responsibility | Best Fit | Why |
|----------------|----------|-----|
| Network ingress, routing, admission control | Rust | Strong ownership for request lifetime, async concurrency, safe cancellation |
| Zero-copy request parsing | Rust | Borrowed slices make "view into buffer" explicit |
| CPU feature block construction | Rust or C++ | Rust if owned by gateway; C++ if shared with scorer ABI |
| GPU model execution | C++/CUDA | Direct CUDA, ONNX Runtime, TensorRT, FAISS, cuBLAS/CUTLASS integration |
| Cross-language boundary | C ABI | Stable, simple, language-neutral |
| Shared analytics/retraining buffer | Apache Arrow | Columnar, zero-copy, readable from Rust, C++, Python, Spark, Velox |
| Offline training and experiments | Python | PyTorch, pandas, notebooks, model export |

Interview line:

> "Rust owns request lifetime and safety at the edge. C++ owns the hot native/GPU integrations. The boundary is a C ABI that passes pointers to stable, versioned, `repr(C)` data. Arrow is used when the same data has to feed multiple consumers."

---

# Part II: C++ Mental Model

## 4. C++ Role In The Fraud Platform

C++ is the right tool when you need direct control over:

- GPU resources: CUDA streams, CUDA Graphs, cuBLAS, CUTLASS, ONNX Runtime CUDA provider, TensorRT, FAISS GPU.
- Memory layout: pinned host memory, aligned buffers, hugepage-backed arenas, shared memory.
- CPU hot loops: SIMD parsing, feature normalization, tree traversal, vector math.
- ABI boundaries: stable `extern "C"` functions callable from Rust.

C++ is not used because "it is faster by default." It is used where the runtime and memory behavior must be explicit.

## 5. RAII: The First C++ Mental Model

RAII means Resource Acquisition Is Initialization. Acquire a resource in a constructor, release it in a destructor, and let object lifetime control cleanup.

Resources include memory, files, sockets, shared memory file descriptors, CUDA streams, CUDA events, mmap regions, model sessions, thread handles, and locks.

```cpp
class MMapRegion {
public:
    MMapRegion(const char* name, size_t size) : size_(size) {
        fd_ = shm_open(name, O_CREAT | O_RDWR, 0660);
        if (fd_ < 0) throw std::runtime_error("shm_open failed");
        if (ftruncate(fd_, size_) != 0) throw std::runtime_error("ftruncate failed");

        ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) throw std::runtime_error("mmap failed");
        madvise(ptr_, size_, MADV_HUGEPAGE);
    }

    ~MMapRegion() {
        if (ptr_ && ptr_ != MAP_FAILED) munmap(ptr_, size_);
        if (fd_ >= 0) close(fd_);
    }

    void* data() const { return ptr_; }
    size_t size() const { return size_; }

    MMapRegion(const MMapRegion&) = delete;
    MMapRegion& operator=(const MMapRegion&) = delete;

private:
    int fd_{-1};
    void* ptr_{nullptr};
    size_t size_{0};
};
```

Interview line:

> "RAII is how I make native systems code failure-safe. If a constructor succeeds, the destructor is responsible for cleanup, including error paths and exceptions."

TODO: Review copy constructor, move constructor, copy assignment, move assignment, and the Rule of 5.

## 6. Pointers And Smart Pointers

Raw pointers are addresses. They do not say who owns memory. In interview code, you should be explicit:

| Pointer Type | Meaning | Fraud Platform Use |
|--------------|---------|--------------------|
| `T*` | Non-owning pointer or C ABI pointer | Passing `FeatureBlock*` from Rust to C++ |
| `std::unique_ptr<T>` | One owner | Own a FAISS index, ONNX session, CUDA engine |
| `std::shared_ptr<T>` | Shared ownership | Shared immutable config or Arrow buffer metadata |
| `std::weak_ptr<T>` | Non-owning reference to shared object | Avoid cycles in service registries |
| `std::span<T>` | View over contiguous memory | Function takes features without owning them |

Preferred style:

```cpp
class ScoringEngine {
public:
    explicit ScoringEngine(EngineConfig cfg)
        : cfg_(std::move(cfg)),
          faiss_(std::make_unique<FaissSearchEngine>(cfg_.faiss)),
          onnx_(std::make_unique<OnnxModelServer>(cfg_.model_path, cfg_.gpu_id)) {}

    Score score(std::span<const float> features);

private:
    EngineConfig cfg_;
    std::unique_ptr<FaissSearchEngine> faiss_;
    std::unique_ptr<OnnxModelServer> onnx_;
};
```

Interview line:

> "Raw pointers cross ABI boundaries. Smart pointers own C++ objects inside the C++ boundary. I do not expose `std::unique_ptr` or C++ exceptions across the Rust ABI."

## 7. `malloc/free`, `new/delete`, And Arenas

You should understand `malloc/free`, but avoid using them directly in modern C++ application code except under custom allocators or FFI layers.

| API | What It Does | Why It Matters |
|-----|--------------|----------------|
| `malloc/free` | C heap allocation | Useful under C ABI, no constructors/destructors |
| `new/delete` | C++ allocation plus constructor/destructor | Avoid in hot path; prefer smart pointers |
| `std::pmr` | Polymorphic memory resources | Good for custom arenas in modern C++ |
| Arena/bump allocator | Fast linear allocation, bulk reset | Best for per-request hot path |

Fraud path rule:

> No per-request heap allocation after ingress. Allocate once per core, write into a bounded arena, then reset the arena after the decision.

```cpp
class BumpArena {
public:
    BumpArena(void* base, size_t capacity)
        : base_(static_cast<std::byte*>(base)), capacity_(capacity) {}

    void* allocate(size_t size, size_t align = 64) {
        size_t aligned = (offset_ + align - 1) & ~(align - 1);
        if (aligned + size > capacity_) return nullptr;
        void* ptr = base_ + aligned;
        offset_ = aligned + size;
        return ptr;
    }

    void reset() { offset_ = 0; }

private:
    std::byte* base_;
    size_t capacity_;
    size_t offset_{0};
};
```

Interview line:

> "An arena turns many small dynamic allocations into pointer arithmetic plus one bulk reset. That removes allocator jitter and fragmentation from the p99 path."

## 8. POSIX Shared Memory And `mmap`

Shared memory is how multiple processes see the same bytes.

Core APIs:

| API | Purpose |
|-----|---------|
| `shm_open` | Create/open a named shared memory object |
| `ftruncate` | Set the object size |
| `mmap` | Map the object into process virtual memory |
| `munmap` | Unmap it |
| `madvise` | Give kernel hints, such as hugepage preference |
| `msync` | Flush mapped file changes when needed |

Use it for:

- Hot shared ring descriptors.
- Arrow event buffers.
- Model/index metadata snapshots.
- Cross-process handoff where gRPC/protobuf would add copy and serialization overhead.

Do not use it blindly for:

- Untrusted variable-size payloads without bounds checks.
- Complex object graphs with pointers. Pointers are process-local virtual addresses unless you store offsets.

Shared memory design rule:

> Store offsets, lengths, and type tags. Do not store raw process-local pointers in shared memory.

```cpp
struct SharedDescriptor {
    uint32_t offset;
    uint32_t len;
    uint16_t type_id;
    uint16_t version;
    uint64_t trace_id;
};
```

## 9. Zero-Copy Parsing

Zero-copy does not mean "do nothing." It means parse once into views and avoid rebuilding data structures.

Bad pattern:

```text
network bytes -> JSON object -> protobuf -> model feature struct -> audit JSON
```

Good pattern:

```text
network bytes -> validated offsets -> TxnView -> FeatureBlock -> Arrow wrapper
```

C++ view pattern:

```cpp
struct ByteView {
    const uint8_t* data;
    uint32_t len;
};

struct TxnViewCpp {
    uint64_t merchant_id;
    int64_t amount_minor;
    uint16_t mcc;
    ByteView merchant_desc;
    ByteView device_blob;
};
```

The parser validates lengths and offsets. The view borrows memory from the network slab or per-core arena.

Interview line:

> "Zero-copy is a lifetime discipline. The view is only safe while the backing buffer is alive and immutable."

## 10. SIMD And Data Layout

SIMD accelerates repeated operations over contiguous data:

- Feature normalization.
- Checksum or validation.
- Float vector dot products.
- Tree model traversal.
- Token/byte scanning.

SIMD-friendly layout:

- Prefer struct-of-arrays for vectorized batch operations.
- Use array-of-structs for single request locality.
- Align hot arrays to 32 bytes for AVX2 or 64 bytes for AVX-512/cache lines.
- Keep branches out of tight loops when possible.

Example: branch-light normalization.

```cpp
void normalize_avx2(const float* x, const float* mean, const float* inv_std,
                    float* y, size_t n) {
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 vx = _mm256_loadu_ps(x + i);
        __m256 vm = _mm256_loadu_ps(mean + i);
        __m256 vs = _mm256_loadu_ps(inv_std + i);
        __m256 out = _mm256_mul_ps(_mm256_sub_ps(vx, vm), vs);
        _mm256_storeu_ps(y + i, out);
    }
    for (; i < n; ++i) y[i] = (x[i] - mean[i]) * inv_std[i];
}
```

Interview line:

> "SIMD only helps when the memory layout feeds it. The first optimization is usually contiguous aligned data, then vectorization."

TODO: Practice AVX2 registers (`__m256`), AVX-512 masks, NEON equivalents, and compiler auto-vectorization reports.

## 11. Cache Lines, False Sharing, And Feature Layout

Most modern CPU cache lines are 64 bytes. If two threads write different variables on the same cache line, they still invalidate each other's cache line. That is false sharing.

Use cacheline alignment for independent producer/consumer state:

```cpp
struct alignas(64) ProducerState {
    std::atomic<size_t> tail{0};
};

struct alignas(64) ConsumerState {
    std::atomic<size_t> head{0};
};
```

Feature block design:

```cpp
struct alignas(64) FeatureBlock {
    float amount_normalized;
    float velocity_1h;
    float velocity_24h;
    float merchant_risk;
    float graph_ring_size;
    float embedding[256];
};
static_assert(alignof(FeatureBlock) == 64);
```

Cache rules for interview:

- Hot fields first.
- Keep read-mostly fields separate from write-heavy fields.
- Avoid sharing writable cache lines between threads.
- Batch operations when the model benefits from contiguous arrays.
- Measure with `perf stat`, cache miss rate, and p99 latency, not intuition.

## 12. NUMA Pinning

NUMA means memory access cost depends on which CPU socket owns the memory. A thread on socket 0 reading memory allocated on socket 1 pays remote-memory latency.

Fraud hot path goal:

```text
NIC queue -> CPU core -> local DRAM arena -> local GPU/NVLink/PCIe path
```

C++ APIs and tools:

| Tool/API | Purpose |
|----------|---------|
| `numa_alloc_onnode` | Allocate memory on a specific NUMA node |
| `numa_run_on_node` | Prefer running on a NUMA node |
| `sched_setaffinity` | Pin thread to CPU cores |
| `pthread_setaffinity_np` | Pin pthreads directly |
| `numactl --hardware` | Inspect topology |
| `lstopo` | Visualize CPU/NIC/GPU locality |

Interview line:

> "NUMA is a p99 problem. Average latency may look fine, but remote memory or cross-socket interrupts show up as jitter."

TODO: Separately review Linux CFS, CPU Manager static policy in Kubernetes, `isolcpus`, `nohz_full`, IRQ affinity, and when not to use real-time scheduling.

## 13. Threading And Processes

Processes have separate virtual address spaces. Threads share the same address space inside one process.

| Choice | Strength | Cost |
|--------|----------|------|
| Process | Fault isolation, security boundary | IPC required for sharing data |
| Thread | Cheap shared memory access | Race conditions if ownership is unclear |
| Async task | Many concurrent I/O operations | CPU-heavy work must not block executor |

C++ style:

- Prefer `std::thread`, `std::jthread`, and RAII wrappers over raw pthreads.
- Use pthread APIs only where you need Linux-specific tuning like affinity or scheduling.
- Use bounded queues and cancellation.
- Do not create threads per request.

```cpp
std::jthread worker([&](std::stop_token stop) {
    while (!stop.stop_requested()) {
        Descriptor d;
        if (ring.try_pop(d)) process(d);
    }
});
```

Interview line:

> "`std::jthread` is RAII-safe because it joins on destruction and supports cooperative cancellation. Raw pthreads are useful for low-level knobs, but I hide them behind a safe wrapper."

## 14. Lock-Free SPSC Queue

SPSC means single producer, single consumer. Because only one thread writes `tail` and only one thread writes `head`, the algorithm is simple and fast.

```cpp
template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0);

public:
    bool try_push(const T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) & (Capacity - 1);
        if (next == head_.load(std::memory_order_acquire)) return false;
        slots_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        item = slots_[head];
        head_.store((head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) T slots_[Capacity];
};
```

Memory ordering answer:

- Producer writes item.
- Producer release-stores `tail`.
- Consumer acquire-loads `tail`.
- Consumer can now safely read item.

Interview line:

> "The ring carries descriptors, not payloads. Payload bytes stay in the arena or shared memory. That keeps the ring fixed-size and cache-friendly."

## 15. Templates, Concepts, OOP, And Macros

Templates are compile-time generic programming. They do not create functions at runtime; the compiler instantiates concrete code for used types.

```cpp
template <typename T>
concept ScoreInput = std::same_as<T, FeatureBlock> || std::same_as<T, BatchFeatureBlock>;

template <ScoreInput T>
Score run_score(const T& input) {
    return score_impl(input);
}
```

Use templates for:

- Generic ring buffers.
- Fixed-capacity arrays.
- Type-safe scoring wrappers.
- Compile-time specialization.

Use OOP for:

- Resource-owning engine classes.
- Plugin interfaces.
- Swappable scoring backends.

Use macros rarely:

- Compile-time feature flags.
- Logging wrappers.
- Platform-specific attributes.

Avoid macros for type-safe logic that templates or functions can express.

Interview line:

> "Templates give compile-time specialization and type safety. Virtual interfaces give runtime polymorphism. I choose based on whether the variability is known at compile time or only at deployment/runtime."

## 16. File I/O And Memory-Mapped Files

Use normal file I/O for small configs and logs. Use memory-mapped files for large immutable artifacts:

- FAISS indexes.
- model weights or model metadata.
- Arrow IPC files.
- replay/audit snapshots.

Relevant APIs:

| C++/POSIX API | Purpose |
|---------------|---------|
| `open`, `read`, `pread` | File reads |
| `mmap` | Map large file into memory |
| `posix_fadvise` | Tell kernel expected access pattern |
| `O_DIRECT` | Bypass page cache when appropriate |
| `io_uring` | Async file/network I/O on Linux |

Interview line:

> "For large immutable artifacts, `mmap` lets the kernel fault pages on demand and share them across processes. For hot request state, I prefer preallocated arenas."

## 17. C++ Integration Libraries

| Need | Library/API | Why |
|------|-------------|-----|
| Vector search | FAISS CPU/GPU | ANN and brute-force vector search, GPU indexes |
| Model inference | ONNX Runtime C++ API | Portable model serving, CUDA provider |
| Optimized inference | TensorRT | NVIDIA-optimized engines for stable shapes |
| Matrix multiplication | cuBLAS | Dense GEMM, neural network primitives |
| Custom kernels/templates | CUTLASS | High-performance CUDA kernel building blocks |
| General CUDA kernels | CUDA Runtime API | Streams, events, graphs, pinned memory |
| Data interchange | Apache Arrow C++ | Zero-copy columnar buffers, C Data Interface |
| Graph features | TigerGraph client or custom C++ graph | Fraud ring traversal and graph features |
| FFI | `extern "C"` | Stable ABI for Rust calls |

## 18. CUDA And GPU Mental Model

GPU acceleration is useful when:

- Work is parallel.
- Data is already on GPU or transfers are amortized.
- Batch size is enough to occupy the GPU.
- Shapes are stable enough for graph capture or optimized engines.

Fraud GPU path:

```text
FeatureBlock in pinned host memory
    |
    v
cudaMemcpyAsync H2D
    |
    v
ONNX/TensorRT neural scorer
    |
    v
optional FAISS/vector lookup
    |
    v
score kernel / ensemble merge
    |
    v
small D2H copy of final score
```

Key APIs:

| API | Purpose |
|-----|---------|
| `cudaMallocHost` | Pinned host memory for fast async H2D/D2H |
| `cudaMemcpyAsync` | Non-blocking transfer on a stream |
| `cudaStreamCreate` | Ordered GPU work queue |
| `cudaEventRecord` | Timing and dependency tracking |
| `cudaStreamBeginCapture` | Start CUDA Graph capture |
| `cudaGraphInstantiate` | Compile captured graph |
| `cudaGraphLaunch` | Replay the graph cheaply |

Interview line:

> "The mistake is moving data CPU-GPU-CPU between every stage. The design should minimize transfers, use pinned buffers, and chain GPU work on streams."

---

# Part III: Rust Mental Model

## 19. Rust Role In The Fraud Platform

Rust is used where you want C-like performance with stronger compile-time guarantees:

- Gateway routing and admission control.
- Per-core worker ownership.
- Zero-copy request views.
- Lock-free rings and shared-memory descriptors.
- Safe wrappers around unsafe FFI.
- Async services with Tokio/Axum.

Rust's most important interview idea:

> Ownership and borrowing turn resource lifetime into a type-system problem instead of a convention.

## 20. Ownership, Borrowing, And Lifetimes

Ownership answers: who is responsible for dropping this value?

Borrowing answers: can this code read or mutate something without owning it?

Lifetime answers: how long is this borrowed view valid?

Zero-copy transaction view:

```rust
pub struct TxnView<'a> {
    pub card_id: &'a [u8],
    pub merchant_id: &'a [u8],
    pub amount_minor: i64,
    pub mcc: u16,
    pub device_fingerprint: &'a [u8],
    raw: &'a [u8],
}

impl<'a> TxnView<'a> {
    pub fn parse(raw: &'a [u8]) -> Result<Self, ParseError> {
        let card_id = read_llvar(raw, 2)?;
        let merchant_id = read_llvar(raw, 42)?;
        Ok(Self {
            card_id,
            merchant_id,
            amount_minor: read_amount(raw)?,
            mcc: read_mcc(raw)?,
            device_fingerprint: read_device(raw)?,
            raw,
        })
    }
}
```

Interview line:

> "The lifetime `'a` says the transaction view cannot outlive the network buffer. That is exactly the zero-copy safety contract."

TODO: Practice lifetime elision, explicit lifetimes on structs, and why self-referential structs are hard in Rust.

## 21. Structs, Enums, Pattern Matching

Rust structs model stable data layout. Enums model state machines and error cases.

```rust
#[repr(C, align(64))]
pub struct FeatureBlock {
    pub amount_normalized: f32,
    pub velocity_1h: f32,
    pub velocity_24h: f32,
    pub merchant_risk: f32,
    pub graph_ring_size: f32,
    pub embedding: [f32; 256],
}

pub enum Decision {
    Approve { score: f32 },
    Decline { score: f32, reason_code: u16 },
    Challenge { score: f32, challenge_type: ChallengeType },
}
```

Interview line:

> "Enums make invalid states harder to represent. A transaction is not a string status plus optional fields; it is one of approve, decline, or challenge with the data required by that state."

## 22. `Option`, `Result`, And Error Handling

Use `Option<T>` when absence is expected. Use `Result<T, E>` when an operation can fail.

```rust
pub fn get_velocity(card_hash: u64, cache: &FeatureCache) -> Result<f32, FeatureError> {
    cache
        .velocity_1h(card_hash)
        .ok_or(FeatureError::MissingVelocity(card_hash))
}
```

Hot path rule:

- Use explicit error codes at FFI boundaries.
- Avoid panics in request processing.
- Convert errors to fast fallback decisions when business rules allow.

```rust
pub enum ScoringError {
    Parse(ParseError),
    Feature(FeatureError),
    GpuUnavailable,
    RingFull,
}
```

Interview line:

> "`Result` makes the fallback path visible in the type signature. That matters in low-latency systems where hidden exceptions or panics can become outages."

## 23. Shadowing, Loops, And Iterators

Shadowing is reusing a name for a new value:

```rust
let amount = read_amount(raw)?;
let amount = normalize_amount(amount);
```

It is useful for transformation pipelines, but do not overuse it when debugging hot path state.

Loop labels help break nested loops:

```rust
'scan: for bucket in buckets {
    for candidate in bucket {
        if candidate.id == target {
            break 'scan;
        }
    }
}
```

Iterators are expressive, but in the hot path you should check generated performance if the logic is complex. Simple iterator chains are usually zero-cost after optimization.

```rust
let risk_sum: f32 = scores.iter().copied().filter(|s| *s > 0.5).sum();
```

Interview line:

> "I use iterators for clarity, but I benchmark hot loops. If the compiler cannot optimize the abstraction away, I switch to a simple loop."

## 24. Smart Pointers In Rust

| Type | Meaning | Fraud Platform Use |
|------|---------|--------------------|
| `Box<T>` | Heap allocation with one owner | Own a large config or engine object |
| `Arc<T>` | Atomic shared ownership | Share immutable config across worker threads |
| `Rc<T>` | Non-atomic shared ownership | Single-threaded graphs; not hot multi-thread path |
| `Mutex<T>` | Mutual exclusion | Control-plane shared state, not per-request hot path |
| `RwLock<T>` | Many readers or one writer | Model registry/config snapshots |
| `Pin<T>` | Prevent moving after pinning | Async futures, self-referential cases |

Hot path preference:

- Per-core ownership instead of shared locks.
- `Arc` for immutable shared config.
- Atomics for small coordination state.
- Message passing for ownership transfer.

```rust
pub struct Worker {
    cfg: Arc<GatewayConfig>,
    arena: PipelineArena,
    scorer: ScorerHandle,
}
```

## 25. Dynamic Dispatch

Rust has two common polymorphism styles:

| Style | Syntax | When |
|-------|--------|------|
| Static dispatch | generics, `impl Trait` | Hot path, compile-time specialization |
| Dynamic dispatch | `dyn Trait` | Plugin-style backends chosen at runtime |

```rust
pub trait Scorer: Send + Sync {
    fn score(&self, features: &FeatureBlock) -> Result<f32, ScoringError>;
}

pub struct Ensemble {
    scorers: Vec<Box<dyn Scorer>>,
}
```

Interview line:

> "I keep dynamic dispatch at coarse plugin boundaries, not inside tiny per-feature loops."

## 26. Unsafe Rust

Unsafe is not "turning off Rust." It means the programmer must uphold invariants the compiler cannot verify.

Unsafe appears in this project for:

- Raw pointers from FFI.
- Shared memory mapping.
- Typed views over bytes.
- Lock-free ring internals.
- Arrow buffer wrapping.

Safe wrapper pattern:

```rust
pub struct PipelineArena {
    base: *mut u8,
    capacity: usize,
    offset: AtomicUsize,
}

impl PipelineArena {
    pub fn ptr_at(&self, offset: usize, len: usize) -> Result<*mut u8, ArenaError> {
        if offset + len > self.capacity {
            return Err(ArenaError::OutOfBounds);
        }
        Ok(unsafe { self.base.add(offset) })
    }
}

unsafe impl Send for PipelineArena {}
unsafe impl Sync for PipelineArena {}
```

Interview line:

> "Unsafe belongs in small audited modules with safe APIs around it. The rest of the codebase should not manipulate raw pointers directly."

TODO: Review Rust aliasing rules, `Send`/`Sync`, Miri, and the Rustonomicon sections on FFI and concurrency.

## 27. Rust Memory Arena

Rust arena pattern:

```rust
pub struct RequestArena {
    slab: Vec<u8>,
    offset: usize,
}

impl RequestArena {
    pub fn new(capacity: usize) -> Self {
        Self { slab: vec![0; capacity], offset: 0 }
    }

    pub fn alloc(&mut self, len: usize, align: usize) -> Result<&mut [u8], ArenaError> {
        let start = (self.offset + align - 1) & !(align - 1);
        let end = start + len;
        if end > self.slab.len() {
            return Err(ArenaError::OutOfMemory);
        }
        self.offset = end;
        Ok(&mut self.slab[start..end])
    }

    pub fn reset(&mut self) {
        self.offset = 0;
    }
}
```

Shared-memory arena uses `mmap`/`memmap2` and stores offsets instead of references.

Interview line:

> "A normal Rust reference cannot safely cross process boundaries. Shared memory uses offsets and typed accessors that validate bounds."

## 28. Rust Socket Programming, Proxying, Routing

Common stack:

| Need | Rust Tool |
|------|-----------|
| Async runtime | Tokio |
| HTTP gateway | Axum or Hyper |
| TCP sockets | `tokio::net::TcpListener`, `TcpStream` |
| Byte buffers | `bytes::Bytes`, `BytesMut` |
| Load balancing | consistent hash ring, power-of-two choices |
| Backpressure | bounded channels, semaphores |
| Cancellation | `CancellationToken`, `tokio::select!` |

Gateway pattern:

```rust
async fn score_handler(
    State(state): State<Arc<AppState>>,
    body: bytes::Bytes,
) -> Result<Json<DecisionResponse>, GatewayError> {
    let worker = state.router.pick_worker(&body)?;
    let permit = state.admission.try_acquire()?;
    let decision = worker.score(body, permit).await?;
    Ok(Json(decision.into()))
}
```

Interview line:

> "Tokio handles I/O concurrency. CPU-bound scoring is routed to per-core workers so the async runtime is not blocked by heavy computation."

## 29. Channels: MPSC, SPSC, And Thread Pools

| Channel | Meaning | Use |
|---------|---------|-----|
| SPSC | one producer, one consumer | Parse worker to scorer worker |
| MPSC | many producers, one consumer | Gateway tasks to a per-core worker |
| Broadcast | one producer, many consumers | Config update notifications |
| Watch | latest value only | Model version/config snapshot |

Rust MPSC:

```rust
let (tx, mut rx) = tokio::sync::mpsc::channel::<ScoreRequest>(1024);

tokio::spawn(async move {
    while let Some(req) = rx.recv().await {
        process_request(req).await;
    }
});
```

For the strict hot path, prefer a fixed SPSC ring or per-core queue with no allocation after startup.

## 30. Rust FFI To C++

FFI boundary principle:

- Rust passes pointers to `#[repr(C)]` structs.
- C++ returns integer status codes.
- No C++ exceptions cross boundary.
- No Rust panics cross boundary.
- Ownership transfer is explicit.

Rust side:

```rust
#[repr(C)]
pub struct FeatureBlock {
    pub amount_normalized: f32,
    pub velocity_1h: f32,
    pub velocity_24h: f32,
    pub embedding: [f32; 256],
}

#[repr(C)]
pub struct ScoreOutput {
    pub score: f32,
    pub reason_code: u32,
}

#[link(name = "sentinel_scorer")]
unsafe extern "C" {
    fn sentinel_score(
        engine: *mut SentinelEngine,
        features: *const FeatureBlock,
        output: *mut ScoreOutput,
    ) -> i32;
}
```

C++ side:

```cpp
extern "C" int sentinel_score(
    SentinelEngine* engine,
    const FeatureBlock* features,
    ScoreOutput* output) noexcept {
    if (!engine || !features || !output) return -1;
    try {
        *output = engine->score(*features);
        return 0;
    } catch (...) {
        return -2;
    }
}
```

Interview line:

> "The FFI boundary is intentionally boring: pointers, lengths, versions, status codes. That makes it debuggable and stable."

## 31. Rust Shared Memory With `memmap2`

```rust
use memmap2::MmapMut;
use std::fs::OpenOptions;

pub struct SharedRegion {
    mmap: MmapMut,
}

impl SharedRegion {
    pub fn open(path: &str, size: u64) -> std::io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(path)?;
        file.set_len(size)?;
        let mmap = unsafe { memmap2::MmapOptions::new().map_mut(&file)? };
        Ok(Self { mmap })
    }

    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.mmap.as_mut_ptr()
    }
}
```

Use `memmap2` for mapped files. Use POSIX `shm_open` through `libc` when you specifically need named shared memory objects.

## 32. Serialization And Deserialization

| Format | Best For | Tradeoff |
|--------|----------|----------|
| Protobuf | Service APIs, schema evolution | Deserialization creates objects |
| FlatBuffers | Read-mostly structured data | More complex schema/tooling |
| Cap'n Proto | Zero-copy-ish RPC/data | Operational familiarity varies |
| Apache Arrow | Columnar analytics, batch features, cross-language zero-copy | Not a general RPC format |
| Raw `repr(C)` structs | Hot internal ABI | Versioning and safety are manual |

Fraud platform rule:

- External APIs can use protobuf/HTTP/gRPC.
- Tier 1 hot path uses views and fixed structs.
- Cross-tier analytics/reasoning uses Arrow.

## 33. Fast Networking Options

| Technology | What It Is | When To Mention |
|------------|------------|-----------------|
| `io_uring` | Linux async I/O interface | High-throughput socket/file I/O with fewer syscalls |
| AF_XDP | Packet path into user space via XDP | Very low-latency packet processing |
| DPDK | User-space packet processing | Extreme throughput, bypass kernel network stack |
| netmap | Fast packet I/O framework | Similar class of problem as DPDK |
| RDMA | Direct memory access across machines | HPC/distributed GPU/data transfer workloads |

Interview line:

> "I do not start with DPDK/RDMA unless the kernel stack is proven to be the bottleneck. First I measure, then I choose the least complex tool that meets the p99 target."

---

# Part IV: Python Mental Model

## 34. Python's Role

Python is important, but not for the Tier 1 hot path.

Use Python for:

- Offline feature analysis.
- PyTorch model training.
- Embedding generation jobs.
- Export to ONNX/TensorRT.
- Golden-query/replay test harnesses.
- Benchmark plotting and analysis.

Avoid Python for:

- Per-transaction synchronous scoring in the authorization path.
- Lock-free data structures.
- Shared memory ownership logic.
- GPU memory lifecycle in production hot path, unless hidden behind a mature serving runtime.

## 35. PyTorch, `torch.compile`, And Triton

| Tool | Use |
|------|-----|
| PyTorch eager | Research and debugging |
| `torch.compile` | Optimize model graphs with minimal code change |
| Triton language | Write custom GPU kernels in Python syntax |
| ONNX export | Move trained model to C++ inference runtime |
| TensorRT export | Optimize stable NVIDIA deployment |

Interview line:

> "Python is the model development language. C++/CUDA is the deterministic serving language when p99 and resource ownership matter."

TODO: Practice one PyTorch-to-ONNX export, one `torch.compile` speedup, and one small Triton kernel such as fused normalization.

---

# Part V: Implementation Walkthrough

## 36. Target Architecture

```text
+-------------------+        +---------------------+        +----------------------+
| Axum/Tokio Gateway| -----> | Per-Core Rust Worker| -----> | C++/CUDA Scoring     |
| admission/routing |        | parse, arena, ring  |        | ONNX, FAISS, CUDA    |
+-------------------+        +----------+----------+        +----------+-----------+
                                       |                              |
                                       v                              v
                              +------------------+          +----------------------+
                              | Shared Mem Arena | <------> | Arrow C Data Bridge  |
                              +--------+---------+          +----------+-----------+
                                       |                               |
                                       v                               v
                              Tier 2 reasoning, audit, analytics, replay
```

## 37. Module Map

| Module | Language | Responsibility | Mental Model Used |
|--------|----------|----------------|-------------------|
| `gateway` | Rust | HTTP/TCP ingress, admission, routing | async, backpressure, ownership |
| `parser` | Rust | ISO/message parsing into `TxnView` | lifetimes, zero-copy |
| `arena` | Rust | per-core memory and shared memory | arenas, unsafe wrapper |
| `ring` | Rust/C++ | descriptor handoff | atomics, acquire/release |
| `scorer_ffi` | Rust | safe wrapper over C ABI | FFI, `Result`, status codes |
| `scoring_engine` | C++ | ensemble scoring | RAII, smart pointers, SIMD |
| `gpu_runner` | C++/CUDA | ONNX/TensorRT scoring | CUDA streams, graphs, pinned memory |
| `faiss_engine` | C++ | vector similarity lookup | FAISS GPU/CPU |
| `arrow_bridge` | C++/Rust | publish event buffers | Arrow C Data Interface |
| `observability` | Rust/C++ | trace headers, histograms, replay | low-overhead metrics |

## 38. Step 1: Gateway And Admission

The gateway accepts requests, applies early admission control, routes to a worker, and preserves the original body buffer.

Key Rust concepts:

- `Bytes` is cheap to clone because it is reference-counted.
- `Arc<AppState>` shares immutable state safely.
- Bounded queues enforce backpressure.
- `Result` makes rejection explicit.

```rust
pub struct AppState {
    router: WorkerRouter,
    admission: AdmissionController,
}

pub async fn authorize(
    State(state): State<Arc<AppState>>,
    body: bytes::Bytes,
) -> Result<Json<AuthResponse>, GatewayError> {
    let permit = state.admission.try_acquire()?;
    let worker = state.router.pick(&body)?;
    let decision = worker.submit(body, permit).await?;
    Ok(Json(decision.into()))
}
```

Interview line:

> "The gateway does not deserialize into an owned object. It keeps the body as bytes and sends ownership of that buffer to a worker."

## 39. Step 2: Zero-Copy Transaction View

The parser validates the request and returns borrowed slices.

```rust
pub struct TxnView<'a> {
    pub card_id: &'a [u8],
    pub merchant_id: &'a [u8],
    pub amount_minor: i64,
    pub mcc: u16,
    pub device_blob: &'a [u8],
}

pub fn parse_txn(raw: &[u8]) -> Result<TxnView<'_>, ParseError> {
    validate_min_len(raw)?;
    let card_id = field(raw, 2)?;
    let merchant_id = field(raw, 42)?;
    let amount_minor = parse_amount(field(raw, 4)?)?;
    let mcc = parse_mcc(field(raw, 18)?)?;
    let device_blob = optional_field(raw, 127).unwrap_or(&[]);

    Ok(TxnView { card_id, merchant_id, amount_minor, mcc, device_blob })
}
```

Mental model link:

- Rust lifetime: prevents view from outliving buffer.
- Zero-copy: fields are slices, not new strings.
- Security: parser validates bounds before creating views.

## 40. Step 3: Feature Block

`FeatureBlock` is the stable ABI between Rust and C++.

```rust
#[repr(C, align(64))]
pub struct FeatureBlock {
    pub amount_normalized: f32,
    pub velocity_1h: f32,
    pub velocity_24h: f32,
    pub merchant_risk: f32,
    pub device_risk: f32,
    pub graph_ring_size: f32,
    pub merchant_embedding: [f32; 128],
    pub device_embedding: [f32; 64],
    pub user_embedding: [f32; 64],
}

impl FeatureBlock {
    pub fn from_txn(txn: &TxnView<'_>, cache: &FeatureCache) -> Result<Self, FeatureError> {
        Ok(Self {
            amount_normalized: normalize_amount(txn.amount_minor),
            velocity_1h: cache.velocity_1h(txn.card_id)?,
            velocity_24h: cache.velocity_24h(txn.card_id)?,
            merchant_risk: cache.merchant_risk(txn.merchant_id)?,
            device_risk: cache.device_risk(txn.device_blob).unwrap_or(0.0),
            graph_ring_size: 0.0,
            merchant_embedding: cache.merchant_embedding(txn.merchant_id)?,
            device_embedding: cache.device_embedding(txn.device_blob)?,
            user_embedding: cache.user_embedding(txn.card_id)?,
        })
    }
}
```

Mental model link:

- `repr(C)`: stable C ABI layout.
- `align(64)`: cacheline alignment.
- arrays: contiguous model input.
- no variable-length fields in the ABI object.

## 41. Step 4: Per-Core Worker

Each worker owns its arena, queues, and scorer handle.

```rust
pub struct CoreWorker {
    worker_id: usize,
    arena: RequestArena,
    scorer: ScorerFfi,
    audit_ring: SPSCRing<Descriptor>,
}

impl CoreWorker {
    pub fn process(&mut self, raw: bytes::Bytes) -> Result<Decision, ScoringError> {
        self.arena.reset();

        let txn = parse_txn(&raw)?;
        let features = FeatureBlock::from_txn(&txn, self.feature_cache())?;
        let output = self.scorer.score(&features)?;

        let desc = self.publish_audit_descriptor(&raw, &features, &output)?;
        self.audit_ring.try_push(desc).map_err(|_| ScoringError::RingFull)?;

        Ok(output.into_decision())
    }
}
```

Mental model link:

- One worker owns mutable state.
- No locks in the hot per-request path.
- Descriptor ring moves metadata, not payloads.

## 42. Step 5: C++ Scoring Engine RAII

```cpp
class SentinelScoringEngine {
public:
    explicit SentinelScoringEngine(const EngineConfig& cfg)
        : cfg_(cfg),
          gpu_(std::make_unique<GpuRunner>(cfg.gpu)),
          faiss_(std::make_unique<FaissSearchEngine>(cfg.faiss)),
          xgb_(std::make_unique<XgboostRunner>(cfg.xgb_model_path)) {}

    ScoreOutput score(const FeatureBlock& f) {
        CpuScores cpu = run_cpu_models(f);
        GpuScores gpu = gpu_->run(f);
        VectorScores vector = faiss_->search(f.user_embedding, cfg_.top_k);
        return ensemble(cpu, gpu, vector);
    }

private:
    CpuScores run_cpu_models(const FeatureBlock& f);

    EngineConfig cfg_;
    std::unique_ptr<GpuRunner> gpu_;
    std::unique_ptr<FaissSearchEngine> faiss_;
    std::unique_ptr<XgboostRunner> xgb_;
};
```

Mental model link:

- RAII owns heavy resources.
- `unique_ptr` expresses single ownership.
- C++ exceptions stay inside the C++ layer.

## 43. Step 6: C ABI Boundary

```cpp
extern "C" SentinelScoringEngine* sentinel_engine_new(const EngineConfig* cfg) noexcept {
    try {
        return new SentinelScoringEngine(*cfg);
    } catch (...) {
        return nullptr;
    }
}

extern "C" int sentinel_engine_score(
    SentinelScoringEngine* engine,
    const FeatureBlock* features,
    ScoreOutput* output) noexcept {
    if (!engine || !features || !output) return -1;
    try {
        *output = engine->score(*features);
        return 0;
    } catch (...) {
        return -2;
    }
}

extern "C" void sentinel_engine_free(SentinelScoringEngine* engine) noexcept {
    delete engine;
}
```

Rust wrapper:

```rust
pub struct ScorerFfi {
    raw: NonNull<SentinelScoringEngine>,
}

impl ScorerFfi {
    pub fn score(&self, features: &FeatureBlock) -> Result<ScoreOutput, ScoringError> {
        let mut out = ScoreOutput::default();
        let rc = unsafe { sentinel_engine_score(self.raw.as_ptr(), features, &mut out) };
        match rc {
            0 => Ok(out),
            -1 => Err(ScoringError::BadFfiArgument),
            -2 => Err(ScoringError::NativeFailure),
            other => Err(ScoringError::UnknownNativeCode(other)),
        }
    }
}

impl Drop for ScorerFfi {
    fn drop(&mut self) {
        unsafe { sentinel_engine_free(self.raw.as_ptr()) }
    }
}
```

Mental model link:

- Rust owns the pointer wrapper.
- C++ owns the internal engine.
- `Drop` calls the C++ destructor path.

## 44. Step 7: GPU Runner

```cpp
class CudaStream {
public:
    CudaStream() { cudaStreamCreate(&stream_); }
    ~CudaStream() { cudaStreamDestroy(stream_); }
    cudaStream_t get() const { return stream_; }

    CudaStream(const CudaStream&) = delete;
    CudaStream& operator=(const CudaStream&) = delete;

private:
    cudaStream_t stream_{};
};

class PinnedBuffer {
public:
    explicit PinnedBuffer(size_t bytes) : bytes_(bytes) {
        cudaMallocHost(&ptr_, bytes_);
    }

    ~PinnedBuffer() {
        if (ptr_) cudaFreeHost(ptr_);
    }

    void* data() const { return ptr_; }

private:
    void* ptr_{nullptr};
    size_t bytes_{0};
};
```

Mental model link:

- RAII controls CUDA resources.
- Pinned memory supports async DMA.
- Stream orders GPU work without global synchronization.

CUDA Graph usage:

```cpp
class ScoringGraph {
public:
    void capture(cudaStream_t stream) {
        cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal);
        launch_normalize(stream);
        run_onnx(stream);
        launch_score_kernel(stream);
        cudaStreamEndCapture(stream, &graph_);
        cudaGraphInstantiate(&exec_, graph_, nullptr, nullptr, 0);
    }

    void launch(cudaStream_t stream) {
        cudaGraphLaunch(exec_, stream);
    }

private:
    cudaGraph_t graph_{};
    cudaGraphExec_t exec_{};
};
```

Interview line:

> "CUDA Graphs help when the operation graph is stable and launch overhead is material. They are less useful when shapes and control flow change every request."

## 45. Step 8: FAISS And Vector Lookup

Fraud scoring can use vector search for:

- Similarity to known fraud transactions.
- Similar merchants/devices/cards.
- Graph-neighborhood embeddings.
- High-risk cluster lookup.

```cpp
class FaissSearchEngine {
public:
    explicit FaissSearchEngine(const FaissConfig& cfg) : cfg_(cfg) {
        resources_ = std::make_unique<faiss::gpu::StandardGpuResources>();
        resources_->setTempMemory(cfg.temp_memory_bytes);
        load_index(cfg.index_path);
    }

    VectorScores search(const float* embedding, int top_k) {
        std::vector<float> distances(top_k);
        std::vector<int64_t> labels(top_k);
        index_->search(1, embedding, top_k, distances.data(), labels.data());
        return {std::move(distances), std::move(labels)};
    }

private:
    void load_index(const std::string& path);

    FaissConfig cfg_;
    std::unique_ptr<faiss::gpu::StandardGpuResources> resources_;
    std::unique_ptr<faiss::Index> index_;
};
```

Interview line:

> "For small enough corpora, brute-force GPU search can be simpler and more accurate than ANN. ANN is a memory/latency tradeoff, not a default."

## 46. Step 9: Arrow Event Buffer

Arrow is used after the decision, when the same event must feed slower consumers.

Event contents:

- Trace ID and timestamps.
- Sanitized transaction metadata.
- Feature vector or feature hashes.
- Model scores.
- Decision and reason code.
- Model/index/config versions.

C++ wrapper pattern:

```cpp
std::shared_ptr<arrow::RecordBatch> wrap_scores(
    const float* scores,
    int64_t count) {
    auto score_buf = arrow::Buffer::Wrap(scores, count * sizeof(float));
    auto score_array = std::make_shared<arrow::FloatArray>(count, score_buf);

    auto schema = arrow::schema({
        arrow::field("score", arrow::float32())
    });

    return arrow::RecordBatch::Make(schema, count, {score_array});
}
```

Mental model link:

- Arrow wraps buffers without copying.
- Ownership of backing memory must outlive the Arrow arrays.
- C Data Interface lets C++ and Rust exchange Arrow structures.

## 47. Step 10: Observability Without Killing The Hot Path

Track:

| Layer | Metrics |
|-------|---------|
| Gateway | QPS, admission rejects, queue depth, timeout rate |
| Parser | parse errors, malformed fields, bytes/request |
| Arena | high-water mark, overflow count |
| Ring | fullness, push failures, consumer lag |
| CPU scoring | per-model latency, fallback count |
| GPU | H2D/D2H time, graph launch time, SM utilization, memory usage |
| FAISS | search latency, index version, recall sample |
| Decision | approve/decline/challenge rates, score distribution |

Low-overhead event:

```rust
#[repr(C)]
pub struct TraceEvent {
    pub trace_id: u64,
    pub stage: u8,
    pub event: u8,
    pub timestamp_ns: u64,
}
```

Interview line:

> "The hot path emits fixed-size trace events to a non-blocking ring. Aggregation and export happen off the request path."

---

# Part VI: Library And Function Cheat Sheets

## 48. C++ Cheat Sheet

| Area | Functions / Types | How It Appears |
|------|-------------------|----------------|
| RAII | constructors, destructors, Rule of 5 | CUDA stream, mmap region, model session |
| Smart pointers | `unique_ptr`, `shared_ptr`, `weak_ptr` | Engine ownership and shared config |
| Views | `std::span`, raw pointer + length | C ABI and model input |
| Memory | `malloc`, `free`, `new`, `delete`, `std::pmr` | allocator knowledge, custom arena internals |
| Shared memory | `shm_open`, `ftruncate`, `mmap`, `munmap` | cross-process Arrow/event buffers |
| Alignment | `alignas(64)`, `std::aligned_alloc` | cacheline-aligned structs |
| Atomics | `std::atomic`, `memory_order_acquire/release` | SPSC ring |
| Threads | `std::thread`, `std::jthread`, `sched_setaffinity` | worker pinning |
| SIMD | AVX2/AVX-512 intrinsics, compiler vectorization | feature normalization and scanning |
| CUDA | streams, events, graphs, pinned memory | GPU scoring |
| cuBLAS/CUTLASS | GEMM and custom kernels | neural scorer optimization |
| ONNX Runtime | `Ort::Session`, CUDA provider | portable model inference |
| FAISS | `IndexFlat`, `GpuIndex`, IVF/PQ/CAGRA | vector lookup |
| Arrow | `RecordBatch`, `Buffer::Wrap`, C Data Interface | zero-copy event sharing |
| FFI | `extern "C"`, `noexcept` | Rust calls into C++ |

## 49. Rust Cheat Sheet

| Area | Types / Crates | How It Appears |
|------|----------------|----------------|
| Ownership | move semantics, borrowing | request buffer lifetime |
| Lifetimes | `TxnView<'a>` | zero-copy parser |
| Layout | `#[repr(C)]`, `#[repr(align(64))]` | FFI structs and cacheline layout |
| Errors | `Option`, `Result`, `thiserror` | parse/feature/scoring failures |
| Smart pointers | `Box`, `Arc`, `Mutex`, `RwLock` | owned engines and shared config |
| Async | Tokio, Axum, Hyper | gateway and routing |
| Buffers | `bytes::Bytes`, `BytesMut` | network body without copies |
| Channels | `tokio::mpsc`, crossbeam, custom SPSC | handoff and backpressure |
| Atomics | `AtomicUsize`, acquire/release | lock-free ring |
| Memory map | `memmap2`, `libc::mmap` | shared memory consumers |
| FFI | `extern "C"`, `NonNull`, `bindgen`, `cxx` | C++ scorer wrapper |
| Arrow | `arrow`, `arrow-array`, `arrow-schema` | event buffers |
| Serialization | prost, flatbuffers, serde | external APIs and config |
| Thread pinning | `core_affinity`, `libc` | per-core workers |
| Observability | tracing, metrics, OpenTelemetry | off-path telemetry |

## 50. Fast Networking Cheat Sheet

| Need | Start With | Escalate To |
|------|------------|-------------|
| Normal service ingress | Axum/Hyper/Tokio | gRPC with tonic |
| High concurrent TCP | Tokio + bounded queues | `io_uring` if syscall overhead is proven |
| Packet processing | Kernel network stack | AF_XDP/DPDK if kernel path is bottleneck |
| Cross-machine low latency | TCP/gRPC | RDMA when memory-to-memory transfer dominates |
| Load balancing | L4/L7 LB + consistent hashing | custom userspace routing for strict locality |

---

# Part VII: Interview Q&A

## 51. C++ And Rust Fundamentals

### Q1: Why use Rust and C++ together instead of only one language?

**A:** Rust is excellent for request ownership, gateway concurrency, parsing, and safe wrappers around unsafe code. C++ is strongest for mature native/GPU libraries like CUDA, ONNX Runtime, TensorRT, FAISS, cuBLAS, and Arrow C++. I use Rust at the control and memory-safety boundary, C++ for hot native integration, and a small C ABI between them.

### Q2: What is RAII, and what is the Rust equivalent?

**A:** RAII ties resource cleanup to object lifetime: constructors acquire resources, destructors release them. Rust's equivalent is ownership plus `Drop`. In both languages, the point is deterministic cleanup for resources like files, mmap regions, CUDA streams, sockets, and model sessions.

### Q3: When do you use raw pointers?

**A:** At FFI boundaries, shared memory access, and low-level allocator/ring internals. Raw pointers should be hidden behind safe abstractions. Normal application code should use references, slices, smart pointers, or typed wrappers.

### Q4: What is the difference between a process and a thread?

**A:** Processes have separate virtual address spaces and require IPC for sharing. Threads share one address space and can communicate through memory directly, but need synchronization and ownership discipline. In this platform, processes isolate tiers, while threads/workers keep Tier 1 fast.

### Q5: Why not use pthreads directly?

**A:** In C++ I prefer `std::thread` or `std::jthread` for RAII-safe lifecycle. I use pthread or Linux APIs only for lower-level controls like CPU affinity or scheduling policy, usually behind a wrapper.

## 52. Memory And Zero-Copy

### Q6: What does zero-copy really mean?

**A:** It means we do not repeatedly serialize, deserialize, or allocate new objects for the same data. We parse bytes once, create validated views, build a stable feature block, and wrap buffers for downstream consumers. It still requires validation, lifetime management, and versioning.

### Q7: Why use an arena instead of malloc/free?

**A:** An arena converts many allocations into pointer arithmetic and one reset. It avoids fragmentation, allocator locks, and p99 jitter. This is ideal for per-request temporary data with a clear lifetime.

### Q8: Why store offsets in shared memory instead of pointers?

**A:** Pointers are virtual addresses meaningful only inside one process. Another process can map the same shared memory at a different address. Offsets from the shared region base are portable across processes.

### Q9: Why use Arrow?

**A:** Arrow provides a typed columnar memory format that C++, Rust, Python, Spark, Velox, and other systems can consume. For this platform, Arrow is useful after Tier 1: audit, explanation, analytics, and retraining can read the same event data without rebuilding it.

### Q10: When would protobuf or FlatBuffers be better than Arrow?

**A:** Protobuf is better for external service APIs and schema evolution. FlatBuffers are useful for read-mostly structured data with direct access. Arrow is best for columnar batches and analytics/interchange, not necessarily for every RPC.

## 53. CPU Performance

### Q11: How do you design for L1/L2/L3 cache?

**A:** Keep hot fields compact and contiguous, align hot structs, separate read-mostly from write-heavy fields, avoid false sharing, batch where useful, and measure cache misses. A 64-byte cache line is the mental unit.

### Q12: What is false sharing?

**A:** Two threads write different variables that happen to live on the same cache line. The CPU cache coherence protocol invalidates the whole line, causing latency even though the variables are logically independent.

### Q13: How do you take advantage of SIMD?

**A:** Make data contiguous, aligned, and branch-light. Use compiler auto-vectorization where possible, intrinsics for critical loops, and validate with assembly or vectorization reports. SIMD is useful for feature normalization, byte scanning, dot products, and batch scoring.

### Q14: What is NUMA and why does it matter?

**A:** NUMA means memory is physically closer to some CPU cores than others. Remote memory access increases latency and p99 jitter. For fraud scoring, the worker core, arena memory, NIC queue, and GPU should be topology-aware.

### Q15: What is CFS and how does scheduling affect p99?

**A:** CFS is Linux's default Completely Fair Scheduler. It is good for general fairness, but p99-sensitive workloads need CPU isolation, affinity, bounded background work, and sometimes Kubernetes CPU Manager static policy so hot workers are not frequently preempted.

## 54. Concurrency

### Q16: Why use SPSC instead of a mutex queue?

**A:** If the topology is truly one producer and one consumer, SPSC avoids locks and contention. The producer writes data then release-stores the tail; the consumer acquire-loads the tail then reads data.

### Q17: What is acquire/release memory ordering?

**A:** Release says writes before this store become visible before the store is observed. Acquire says reads after this load happen after the load observes the released value. Together, they publish data safely between threads.

### Q18: When do you use MPSC?

**A:** When many producers send work to one consumer, such as many gateway tasks feeding one worker. For stricter per-core hot paths, I prefer one queue per producer/worker pair or a work-stealing design with clear ownership.

### Q19: How do you avoid blocking the Tokio runtime?

**A:** Keep async tasks for I/O orchestration. CPU-heavy parsing/scoring goes to dedicated workers or `spawn_blocking` if appropriate. The runtime should not run long CPU loops on core reactor threads.

### Q20: How do you handle backpressure?

**A:** Use bounded queues, admission control, timeouts, and circuit breakers. Reject early or degrade gracefully before queues grow enough to violate p99.

## 55. GPU And Native Integration

### Q21: Why pinned memory?

**A:** Pinned host memory cannot be paged out, so the GPU can DMA from it efficiently. It enables true async H2D/D2H transfers with `cudaMemcpyAsync`.

### Q22: Why CUDA streams?

**A:** A stream is an ordered queue of GPU work. Streams let us overlap transfers and compute where dependencies allow, and avoid global synchronization.

### Q23: Why CUDA Graphs?

**A:** They reduce CPU launch overhead for repeated stable GPU workloads. If the neural scoring path has many small kernels, graph replay can save hundreds of microseconds at p99.

### Q24: When should you not use CUDA Graphs?

**A:** When shapes, memory addresses, or control flow change too much, or when capture complexity outweighs launch overhead. Dynamic workloads may be better served with normal streams and batching.

### Q25: How does ONNX Runtime fit?

**A:** ONNX Runtime lets us serve a trained model from C++ with a CUDA execution provider. It is a portability layer between Python training and native production inference.

### Q26: How does FAISS fit in fraud detection?

**A:** FAISS can find transactions, merchants, cards, or devices similar to known fraud patterns. The vector score becomes another feature in the ensemble. It is especially useful for similarity search and anomaly-neighborhood signals.

## 56. FFI And Safety

### Q27: What makes a good Rust-C++ boundary?

**A:** Stable `repr(C)` structs, raw pointers plus lengths, explicit version fields, integer status codes, no exceptions or panics across the boundary, and clear ownership rules.

### Q28: Why not pass C++ objects directly into Rust?

**A:** C++ object layout, destructors, exceptions, and templates are not stable ABI contracts. Rust should see opaque handles and C-compatible functions.

### Q29: How do you make unsafe Rust acceptable?

**A:** Keep unsafe in small modules, document invariants, validate bounds, wrap raw pointers in safe APIs, test with sanitizers/Miri where possible, and never let the rest of the codebase casually manipulate raw pointers.

### Q30: What is `repr(C)`?

**A:** It tells Rust to lay out a struct in a C-compatible way. It is required for structs passed across FFI or read by C++.

## 57. System Design And Production

### Q31: Walk through the 50 ms fraud pipeline.

**A:** Gateway receives bytes, admission checks load, routes to a per-core worker, parses into borrowed `TxnView`, builds a cacheline-aligned `FeatureBlock`, runs CPU scorers and optional C++/CUDA scorer, merges scores, returns decision, and publishes an Arrow event to slower consumers.

### Q32: How do you protect the Tier 1 SLA from Tier 2/3 LLM work?

**A:** Physically and logically isolate the tiers. Tier 1 publishes immutable events and never waits for LLM reasoning. Tier 2/3 consume asynchronously with their own admission control and GPU budgets.

### Q33: What happens if GPU scoring fails?

**A:** The circuit breaker opens, requests use CPU fallback or a conservative score path, and the system emits alerts. The authorization path should return a decision rather than hanging behind a broken GPU.

### Q34: How do you update models without downtime?

**A:** Load the new model/index in a shadow engine, warm it, validate golden requests, then atomically swap the active engine pointer or route traffic to green pods. Keep the old version for rollback.

### Q35: How do you debug a bad decision?

**A:** Replay the request using logged trace ID, sanitized input, feature snapshot, model version, index version, and score outputs. Deterministic replay is essential because live features and models change over time.

### Q36: What should you monitor?

**A:** P50/p99 latency by stage, queue depth, arena overflow, ring fullness, parse error rate, CPU model latency, GPU transfer/compute time, FAISS latency, score distribution, decision rates, model/index versions, and fallback rate.

### Q37: Where would you use `io_uring`, DPDK, or RDMA?

**A:** Only after measuring the kernel network or syscall path as the bottleneck. `io_uring` helps with async I/O syscall overhead, DPDK/AF_XDP with packet processing bypass, and RDMA with direct memory movement across machines.

### Q38: How do you explain the top-down study approach?

**A:** I start with the production request path, then learn each language feature only where it affects latency, safety, or operability. That creates a mental map instead of disconnected syntax knowledge.

---

# Part VIII: Study Plan

## 58. Seven-Day Interview Plan

| Day | Focus | Output |
|-----|-------|--------|
| 1 | Draw the fraud architecture and latency budget | Explain the pipeline in 3 minutes |
| 2 | C++ RAII, smart pointers, arenas, mmap | Write RAII wrappers for mmap and CUDA stream |
| 3 | Rust ownership, lifetimes, `TxnView`, `FeatureBlock` | Write parser returning borrowed slices |
| 4 | Lock-free SPSC and memory ordering | Explain acquire/release without notes |
| 5 | FFI and Arrow | Define `repr(C)` structs and C ABI wrapper |
| 6 | GPU path | Explain pinned memory, streams, CUDA Graphs, ONNX/FAISS |
| 7 | Mock interview | Answer Q1-Q38 aloud and draw the system |

## 59. Bottom-Up TODO List

Study these separately only after the top-down map feels clear:

- C++ Rule of 5, move semantics, and exception safety.
- C++ `std::pmr` custom allocators.
- C++20 concepts, especially `std::same_as`.
- AVX2/AVX-512 intrinsics and auto-vectorization reports.
- Rust lifetimes beyond simple borrowed views.
- Rust `Send`, `Sync`, `Pin`, and unsafe aliasing rules.
- Tokio runtime internals and avoiding blocking executor threads.
- POSIX shared memory permissions and cleanup.
- Apache Arrow buffer ownership and C Data Interface details.
- Linux scheduler tuning: CFS, cpusets, CPU isolation, IRQ affinity.
- NUMA topology inspection with `lstopo`, `numactl`, and Kubernetes Topology Manager.
- CUDA streams, events, graphs, and memory pools.
- ONNX Runtime CUDA provider configuration.
- FAISS CPU vs GPU index choices.
- Protobuf vs FlatBuffers vs Arrow schema evolution.
- `io_uring`, AF_XDP, DPDK, netmap, and RDMA tradeoffs.

---

# Final Interview Sound Bite

"For this fraud platform, my mental model is bytes, ownership, locality, and bounded work. Rust owns request lifetime, routing, and safe zero-copy views. C++ owns native model and GPU execution through RAII-managed resources. The hot path avoids heap allocation, keeps data cacheline-aligned and NUMA-local, communicates through descriptor rings, and publishes Arrow buffers for slower consumers. Every optimization is tied to a measurable p99 risk: allocation jitter, serialization overhead, remote memory, lock contention, GPU launch overhead, or unbounded queues."
