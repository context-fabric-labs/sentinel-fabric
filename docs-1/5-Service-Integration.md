
# Layer 5: Data Movement & Zero-Copy — Complete Deep Dive
## "How does data flow through the system without unnecessary copies?"

> **Why this layer matters:** In ultra-low-latency systems, DATA MOVEMENT
> often costs more than DATA PROCESSING. A model inference might take 5 ms,
> but if you serialize/deserialize 4 times between stages, that adds 20 ms.
> Layer 5 is about eliminating every copy that doesn't need to exist.
>
> **The core insight:** The fastest copy is the one that never happens.
> Design your data flow so each piece of data is written ONCE and then
> REFERENCED (not copied) by every stage that needs it.

---

# SECTION 1: ZERO-COPY TECHNIQUES

---

## 1.1 Shared Memory (shm_open + mmap)

### What it is
POSIX shared memory creates a named memory region that multiple processes
can map into their address spaces. Both processes see the SAME physical
pages — no copying between them.

```
Process A (C++ scoring engine):
  int fd = shm_open("/pipeline_arena", O_CREAT | O_RDWR, 0600);
  ftruncate(fd, 512 * 1024 * 1024);  // 512 MB
  void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  // Write session data at offset 1000
  memcpy(resolve(ptr, 1000), &session_data, sizeof(session_data));

Process B (Rust gateway):
  int fd = shm_open("/pipeline_arena", O_RDWR, 0600);
  void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  // Read SAME data at offset 1000 — ZERO COPY
  SessionData* data = resolve(ptr, 1000);
```

### Key rules
- Use **offsets, not pointers** (processes map at different virtual addresses)
- Use **hugepage backing** for large arenas (MAP_HUGETLB)
- Use **cacheline alignment** (64 bytes) to prevent false sharing
- Use **atomics or SPSC rings** for signaling, not the data itself

---

## 1.2 Arena Allocators

### What they are
An arena pre-allocates a large memory block and sub-allocates from it
by bumping a pointer. No per-object free — memory is reclaimed in bulk.

```cpp
class Arena {
    uint8_t* base;
    std::atomic<uint32_t> offset;
    size_t capacity;

public:
    Arena(void* shared_mem, size_t size)
        : base(static_cast<uint8_t*>(shared_mem)),
          capacity(size), offset(0) {}

    uint32_t allocate(size_t size, size_t align = 64) {
        uint32_t current = offset.load(std::memory_order_relaxed);
        uint32_t aligned = (current + align - 1) & ~(align - 1);
        while (!offset.compare_exchange_weak(current, aligned + size))
            aligned = (current + align - 1) & ~(align - 1);
        return aligned;  // returns OFFSET, not pointer
    }

    template<typename T>
    T* resolve(uint32_t off) {
        return reinterpret_cast<T*>(base + off);
    }

    void reset() { offset.store(0); }
};
```

### Why arenas matter for HPC
- **Zero allocation overhead** per request (just bump a counter)
- **Zero fragmentation** (contiguous allocation)
- **Zero deallocation cost** (bulk reset)
- **Cache-friendly** (sequential memory access)
- **Works on shared memory** (arena over mmap'd region)

---

## 1.3 SPSC Descriptor Rings

### What they are
Single-Producer, Single-Consumer lock-free ring buffers that carry
METADATA (offsets/descriptors), not the actual data.

```
Data stays in the shared arena.
Ring carries only: "session at offset 4096 is ready for you"

Producer (Stage B):                  Consumer (Stage C):
  ring.push(4096)  ←─ 4 bytes ─→   ring.pop() → 4096
                                     arena.resolve(4096) → session data
```

### Why descriptors, not data
- Ring entry is 4-8 bytes (an offset), not kilobytes of data
- Data stays in shared arena — never copied through the ring
- Lock-free, wait-free for SPSC
- Bounded backpressure (full ring = upstream slows down)

### Library choices
- **Rust:** `rtrb` (realtime-safe, bounded, lock-free SPSC)
- **C++:** `boost::lockfree::spsc_queue` or `folly::ProducerConsumerQueue`

---

## 1.4 Views Instead of Copies

### The principle
Never copy data just to look at it. Use non-owning references.

```cpp
// BAD: copy the string
std::string transcript(session->transcript_data,
                       session->transcript_len);

// GOOD: view the string (zero copy)
std::string_view transcript(
    arena->resolve<char>(session->transcript_offset),
    session->transcript_len);

// BAD: copy features into a new vector
std::vector<float> features(raw_features, raw_features + N);

// GOOD: span over existing memory
std::span<const float> features(raw_features, N);
```

---

# SECTION 2: SERIALIZATION OPTIMIZATION

---

## 2.1 The Serialization Spectrum

```
SLOWEST                                              FASTEST
  ←────────────────────────────────────────────────────→

JSON        Protobuf      FlatBuffers    Cap'n Proto    Arrow IPC    Shared Memory
~1-5 ms     ~0.1-0.5 ms   ~0.01 ms       ~0.01 ms      ~0.01 ms    ~0 ms
parse+      parse+        access without  wire=memory    columnar    no serialization
allocate    allocate      unpacking       format         zero-copy   at all
```

### When to use which

| Format | Best for | Avoid for |
|---|---|---|
| **JSON** | debugging, config, external APIs | hot path (too slow) |
| **Protobuf** | cross-service APIs, schema evolution | inner hot loop |
| **FlatBuffers** | mmap-friendly structured data, game engines | when you need rich schema evolution |
| **Cap'n Proto** | ultra-low-latency RPC between services | broad ecosystem adoption |
| **Arrow IPC** | columnar batch handoff, analytics, multi-language | per-request single-row data |
| **Shared memory** | same-host pipeline stages | cross-host communication |

---

# SECTION 3: APACHE ARROW

---

## 3.1 What Arrow is
A language-independent columnar in-memory format with zero-copy reads.

### Key properties
- **Columnar:** data stored by column, not by row — cache-friendly for analytics
- **Zero-copy reads:** consumers can read without deserialization
- **C Data Interface:** zero-copy sharing between runtimes in the same process
- **IPC format:** cross-process sharing via memory-mapped files
- **Multi-language:** C++, Rust, Python, Java, Go — same memory layout

### When to use Arrow in your pipeline
```
HOT PATH (per-request):
  → Use fixed-layout structs in shared arena
  → Arrow is overkill for one request

PUBLISH BOUNDARY (after decision):
  → Use Arrow RecordBatch for downstream consumers
  → Warm path, cold path, audit, replay, analytics all read same data

FEATURE PIPELINE (batch):
  → Use Arrow tables for pre-materialized features
  → mmap'd Arrow files for zero-copy access
```

### Wrapping existing memory as Arrow (zero copy)
```cpp
// Your data already exists in shared memory
float* existing_amounts = arena->resolve<float>(amounts_offset);

// Wrap as Arrow buffer WITHOUT copying
auto buffer = std::make_shared<arrow::Buffer>(
    reinterpret_cast<const uint8_t*>(existing_amounts),
    num_rows * sizeof(float));

auto data = arrow::ArrayData::Make(arrow::float32(), num_rows,
                                    {nullptr, buffer});
auto array = arrow::MakeArray(data);
// Arrow array points to the SAME bytes — zero copy
```

---

# SECTION 4: THE COMPLETE DATA FLOW IN YOUR PIPELINE

```
[Stage B: ASR]
  Writes transcript to shared arena at offset X
  Pushes offset X to SPSC ring → Stage C
  ZERO COPY: transcript bytes written once, never moved

[Stage C: NLU + Search]
  Pops offset X from ring
  Resolves transcript from arena (zero copy — same bytes)
  Runs BERT → intent + entities + embedding
  Embedding STAYS ON GPU → feeds FAISS search (zero D2H)
  Writes NLU results to arena at offset Y
  Writes search results to arena at offset Z
  Pushes offset to SPSC ring → Stage D
  ZERO COPY: results written once to arena

[Stage D: Response Orchestration]
  Pops offset from ring
  Resolves NLU results from arena (zero copy)
  Resolves search results from arena (zero copy)
  Selects template, fills slots
  Writes TTS input to arena
  Pushes offset to SPSC ring → Stage E
  ZERO COPY: template text written once

[Publish Boundary]
  After decision, create Arrow RecordBatch
  wrapping arena data (zero copy wrap)
  Downstream consumers (warm path, audit, replay)
  read the SAME Arrow data via mmap or shared memory

TOTAL COPIES IN ENTIRE PIPELINE: ~0 for data, ~4 offset pushes through rings
```

---

# SECTION 5: INTERVIEW CHEAT SHEET

## "How do you achieve zero-copy between pipeline stages?"
→ Shared memory arena (hugepage-backed, offset-based) + SPSC descriptor
   rings. Data stays in arena, rings carry only 4-byte offsets.

## "Why not just use gRPC between stages?"
→ gRPC adds 5-15 ms serialization + 2-5 ms network per hop. With 4 hops,
   that's 30-80 ms of pure infrastructure overhead. Shared memory
   eliminates ALL of it — inter-stage overhead drops to < 2 ms total.

## "When do you use Arrow vs shared memory structs?"
→ Shared memory structs for the hot path (per-request, fixed layout).
   Arrow at the publish boundary (batch handoff to downstream consumers
   in multiple languages).

## "What is the difference between an arena allocator and malloc?"
→ malloc manages memory object-by-object with per-object free. An arena
   pre-allocates a large block and sub-allocates by bumping a pointer.
   Zero allocation overhead, zero fragmentation, bulk reset. Perfect
   for request-scoped memory in a hot path.
