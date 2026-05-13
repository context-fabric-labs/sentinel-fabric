
# Apple Siri Cloud Conversational Platform — Interview Story
## Senior Systems Engineer: Low-Latency Serving, LLM Runtime, Search Architecture (2019–2022)

> **Role context:** I was a Senior Systems Engineer on the Siri Cloud Platform team,
> responsible for productionizing the multi-stage conversational pipeline — from
> streaming ingress through ASR, NLU, search/ranking, response orchestration, and TTS.
> My work spanned the serving infrastructure, not model training. The LLM and model
> teams trained the models; the runtime team built the inference engine; I built the
> production serving layer that tied everything together into a single low-latency
> distributed system with shared state.

---

# THE SYSTEM: Multi-Stage Siri Cloud Pipeline

## Architecture overview

The Siri cloud pipeline was not a collection of independent microservices. It was
a **single distributed system with shared state**, co-located on the same bare-metal
hosts, communicating through **zero-copy shared memory** rather than network RPCs
between stages. This was a deliberate design choice driven by the latency budget:
the entire cloud round-trip (from receiving the audio stream to sending back the
first TTS audio chunk) had to complete within **~200–300 ms** for the user to
perceive a responsive assistant.

```
Edge Device (iPhone/iPad/Mac)
    ↓ Encrypted streaming gRPC, session-affine routing
    ↓
Cloud Host (bare metal, Apple Silicon server)
┌─────────────────────────────────────────────────────┐
│ Stage A: Streaming Ingress                           │
│    ↓ zero-copy shared memory                         │
│ Stage B: ASR (Conformer/RNN-T)                       │
│    ↓ zero-copy shared memory                         │
│ Stage C: NLU + Search/Ranking                        │
│    ↓ zero-copy shared memory                         │
│ Stage D: Response Orchestration                      │
│    ↓ zero-copy shared memory                         │
│ Stage E: TTS (Neural speech synthesis)               │
│    ↓                                                 │
│ Shared Session State Layer (across all stages)       │
└─────────────────────────────────────────────────────┘
    ↓ Streaming gRPC response
Edge Device
```

The key design principle was:

> **Treat the multi-stage pipeline as a single process with shared memory,
> not as independent microservices with network calls between them.**

---

# STORY 1: Building the Low-Latency Serving Infrastructure
## (Rust + C++ + Zero-Copy Architecture)

## Situation

When I joined the Siri cloud platform effort, the existing prototype was built as
**separate microservices** communicating over gRPC between stages:

```
ASR service → gRPC → NLU service → gRPC → Search service → gRPC → Orchestration → gRPC → TTS
```

Each inter-stage hop added:
- **5–15 ms of serialization** (protobuf encode/decode)
- **2–5 ms of network latency** (even on localhost)
- **memory allocation** for each deserialized message
- **context switch overhead** at each service boundary

With 4 inter-stage hops, the infrastructure overhead alone was **30–80 ms** —
consuming 15–30% of our 250 ms end-to-end cloud budget before any model
inference even started.

The team lead asked me to redesign the inter-stage communication to bring
infrastructure overhead below **5 ms total** across all stages.

## Approach: Shared-Memory Pipeline with Zero-Copy Handoff

### Design philosophy
Instead of treating each stage as a separate service, I redesigned the pipeline
as a **single co-located process group** on each host, with stages communicating
through **shared memory** instead of network RPCs.

### Architecture I built

#### A. Shared memory arena (C++)

I replaced the inter-stage gRPC calls with a **shared memory arena** backed by
hugepage-mapped memory. All stages on the same host attached to the same
shared region.

```cpp
// Shared arena backed by hugepages for TLB efficiency
class PipelineArena {
    void* base_ptr;
    size_t arena_size;
    std::atomic<uint64_t> write_offset;

public:
    PipelineArena(const std::string& shm_name, size_t size) {
        int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0600);
        ftruncate(fd, size);
        base_ptr = mmap(nullptr, size,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_HUGETLB,
                       fd, 0);
        close(fd);
        write_offset.store(sizeof(ArenaHeader));
    }

    // Bump allocator — sub-allocate from the shared region
    // Returns OFFSET, not pointer (safe across processes)
    uint32_t allocate(size_t size, size_t alignment = 64) {
        size_t current = write_offset.load(std::memory_order_relaxed);
        size_t aligned = (current + alignment - 1) & ~(alignment - 1);
        while (!write_offset.compare_exchange_weak(
            current, aligned + size, std::memory_order_acq_rel)) {
            aligned = (current + alignment - 1) & ~(alignment - 1);
        }
        return static_cast<uint32_t>(aligned);
    }

    template<typename T>
    T* resolve(uint32_t offset) {
        return reinterpret_cast<T*>(static_cast<uint8_t*>(base_ptr) + offset);
    }
};
```

Key design decisions:
- **Hugepage-backed** (`MAP_HUGETLB`) to reduce TLB misses — the arena was
  512 MB to 1 GB, and with 4 KB pages that would be 131K–262K TLB entries;
  with 2 MB hugepages, only 256–512 entries needed
- **Offset-based references** instead of raw pointers — safe across processes
  that may map the arena at different virtual addresses
- **Cacheline-aligned allocations** (64-byte alignment) to prevent false sharing
  between stages writing adjacent data

#### B. Session state in shared memory

Each user session had a **session context block** in the shared arena, containing:
- session ID and routing metadata
- ASR partial/final hypothesis (written by Stage B, read by Stage C)
- NLU intent + entities + confidence (written by Stage C, read by Stage D)
- search results and ranking scores (written by Stage C, read by Stage D)
- dialogue state / conversation history (read/written by Stage D)
- TTS input text (written by Stage D, read by Stage E)

```cpp
// Fixed-layout session context — no serialization needed
struct SessionContext {
    // Header
    uint64_t session_id;
    uint64_t request_id;
    uint64_t timestamp_ns;
    uint32_t state_flags;

    // ASR output (written by Stage B)
    uint32_t transcript_offset;     // offset into arena for variable-length text
    uint16_t transcript_len;
    float    asr_confidence;
    uint8_t  is_final;

    // NLU output (written by Stage C)
    uint16_t intent_id;
    float    intent_confidence;
    uint32_t entities_offset;       // offset to entity array
    uint16_t entity_count;

    // Search results (written by Stage C)
    uint32_t search_results_offset; // offset to ranked result array
    uint16_t result_count;
    float    top_score;

    // Dialogue state (read/written by Stage D)
    uint32_t dialogue_state_offset;
    uint16_t turn_count;

    // TTS input (written by Stage D)
    uint32_t tts_text_offset;
    uint16_t tts_text_len;

    // Timing (for observability)
    uint64_t stage_a_complete_ns;
    uint64_t stage_b_complete_ns;
    uint64_t stage_c_complete_ns;
    uint64_t stage_d_complete_ns;
    uint64_t stage_e_complete_ns;
};
```

Each stage wrote its output into the session context and signaled the next stage.
No serialization, no deserialization, no network hop. The next stage simply
resolved the offset and read the data directly from shared memory.

#### C. Inter-stage signaling (Rust SPSC rings)

Between stages, I used **SPSC descriptor rings** in Rust for signaling.
The rings carried only **session context offsets** (8 bytes each), not the
actual data — the data stayed in the shared arena.

```rust
use rtrb::RingBuffer;

struct StageConnector {
    // Ring carries only offsets into the shared arena
    ring: RingBuffer<u32>,  // u32 = offset to SessionContext
}

// Stage B (ASR) signals Stage C (NLU) that a session is ready
fn asr_complete(connector: &mut StageConnector, session_offset: u32) {
    connector.ring.push(session_offset).expect("ring full — backpressure");
}

// Stage C polls for ready sessions
fn nlu_poll(connector: &mut StageConnector) -> Option<u32> {
    connector.ring.pop().ok()
}
```

This gave us:
- **Lock-free, wait-free** signaling between stages
- **Zero data copying** — only a 4-byte offset moved through the ring
- **Bounded backpressure** — if the ring was full, the upstream stage
  applied backpressure rather than unbounded queueing

#### D. Rust service wrapper with C++ FFI

The overall service was a **Rust binary** that:
- managed the gRPC ingress (streaming audio from edge devices)
- owned the session lifecycle
- dispatched work through SPSC rings
- called into C++ for performance-critical components via FFI

```rust
// Rust calls into C++ shared arena and model runtimes via FFI
extern "C" {
    fn arena_allocate(arena: *mut ArenaOpaque, size: usize, align: usize) -> u32;
    fn arena_resolve(arena: *const ArenaOpaque, offset: u32) -> *const u8;
    fn run_nlu_inference(
        model: *const NLUModelOpaque,
        input_offset: u32,
        output_offset: u32,
        arena: *mut ArenaOpaque
    ) -> i32;
}
```

This Rust + C++ split was deliberate:
- **Rust** for the networking, session management, backpressure, and safety-critical
  orchestration code — ownership model prevented use-after-free and data races
- **C++** for the shared memory arena, model runtime integration, and any
  Metal/Core ML/performance-critical native code

#### E. Session-affine routing

The ingress layer used **consistent hashing on session ID** to route requests
to the same host for the duration of a conversation. This ensured:
- session state in shared memory was always local (no remote fetch)
- ASR context from prior utterances was warm in cache
- dialogue state accumulated across turns without cross-host synchronization

## Results

| Metric | Before (gRPC microservices) | After (shared memory pipeline) | Improvement |
|---|---|---|---|
| Inter-stage overhead (total) | 30–80 ms | **< 2 ms** | **15–40x reduction** |
| Serialization time per hop | 5–15 ms | **0 ms** (no serialization) | **Eliminated** |
| Memory allocations per request | ~120 (across all stages) | **~8** (arena bump allocs) | **15x fewer** |
| p99 end-to-end cloud latency | 380 ms | **220 ms** | **42% faster** |
| Context switches per request | ~45 | **~6** | **7.5x fewer** |
| TLB misses (session data) | High (scattered heap) | **Near zero** (hugepage arena) | **Eliminated** |

### Verification
- **Instruments / Metal System Trace** confirmed zero-copy behavior —
  no memcpy between stages, same physical pages accessed by all stages
- **`perf stat` equivalent** on Apple Silicon confirmed reduced context
  switches and cache misses
- **Custom per-stage timing** in the session context showed exactly where
  each millisecond was spent

## Learnings

1. **Shared memory is the ultimate zero-copy technique for co-located stages.**
   It eliminates serialization, network hops, and memory allocation in one move.
   The key insight is to use **offsets, not pointers** for cross-process safety.

2. **SPSC rings for signaling, shared memory for data** is the cleanest split.
   The rings carry only descriptors (offsets); the actual data never moves.

3. **Hugepage backing matters for large arenas.** Without hugepages, the 512 MB
   arena caused measurable TLB pressure. With 2 MB hugepages, TLB misses on
   session data dropped to near zero.

4. **Rust + C++ FFI works well when the boundary is clean.** Rust owned the
   networking and orchestration; C++ owned the arena and model runtimes.
   The FFI boundary was a thin layer of offset-based functions.

---

# STORY 2: LLM Runtime Integration and Optimization
## (Productionizing the NLU and Response Generation Models)

## Situation

In 2022, the Siri team began integrating a **distilled transformer model** for
improved NLU (intent classification + entity extraction) and a **small generative
model** for response synthesis (replacing pure template-based responses for
select query types). The LLM team had trained these models; the runtime team
had built a basic inference engine on Apple's Metal/Core ML stack; my job was
to **productionize the serving** — make it reliable, fast, and observable at
Siri's scale (hundreds of thousands of concurrent sessions).

The initial integration was straightforward but had serious production issues:

1. **Model loading was slow** — cold start took 8–12 seconds, causing timeouts
   during deployments and scaling events
2. **Inference latency was unpredictable** — p50 was 15 ms but p99 spiked to
   80 ms due to memory allocation and GPU scheduling jitter
3. **No batching** — each request ran independently, wasting GPU capacity
4. **No graceful degradation** — if the model service was slow or failing,
   the entire pipeline stalled

### My contribution
Although I was not on the LLM training or runtime teams, productionizing at
Siri's scale required me to go deep into the runtime internals. I contributed
optimizations back to the runtime team and built the serving wrapper that
made it production-ready.

## Approach

### A. Model pre-loading and warm-start

**Problem:** Models were loaded on first request, causing cold-start latency.

**Solution:** I implemented a pre-loading system that:
- loaded models at service startup (not on first request)
- pre-allocated all inference buffers
- ran a warmup inference pass to trigger any lazy initialization
- kept models resident in memory across requests

```cpp
class ModelServer {
    // Pre-loaded models — loaded once at startup, never freed during service lifetime
    std::unique_ptr<NLUModel> nlu_model;
    std::unique_ptr<GenModel> gen_model;

    // Pre-allocated inference buffers per worker
    struct WorkerBuffers {
        float* input_embeddings;   // pre-allocated, reused
        float* attention_scratch;  // pre-allocated, reused
        float* output_logits;      // pre-allocated, reused
        int    max_seq_len;
    };
    std::vector<WorkerBuffers> worker_buffers;

public:
    void initialize(int num_workers, int max_seq_len) {
        // Load model weights once
        nlu_model = load_model("nlu_v3.mlpackage");
        gen_model = load_model("gen_v2.mlpackage");

        // Pre-allocate per-worker buffers
        for (int i = 0; i < num_workers; i++) {
            WorkerBuffers buf;
            buf.max_seq_len = max_seq_len;
            // Allocate on the correct NUMA node / memory domain
            buf.input_embeddings = allocate_aligned(max_seq_len * EMBED_DIM * sizeof(float));
            buf.attention_scratch = allocate_aligned(SCRATCH_SIZE);
            buf.output_logits = allocate_aligned(VOCAB_SIZE * sizeof(float));
            worker_buffers.push_back(buf);
        }

        // Warmup pass — triggers any lazy Metal shader compilation
        for (int i = 0; i < num_workers; i++) {
            run_warmup_inference(i);
        }
    }
};
```

**Impact:** Cold-start eliminated. First-request latency matched steady-state.

### B. Kernel launch overhead reduction

**Problem:** The NLU transformer forward pass consisted of ~30 small Metal
compute shader dispatches. Each dispatch had CPU-side overhead of ~3–5 µs,
totaling ~100–150 µs of pure dispatch overhead.

**Solution:** I worked with the runtime team to implement **Metal command
buffer batching** — the Apple Silicon equivalent of CUDA Graphs. Instead
of encoding and committing each operation as a separate command buffer,
we encoded the entire forward pass into a **single command buffer** and
committed it once.

```
Before: 30 separate command buffer commits
  [encode op1] [commit] [encode op2] [commit] ... [encode op30] [commit]
  → 30 × 3-5 µs = 90-150 µs dispatch overhead

After: single command buffer with all operations
  [encode op1] [encode op2] ... [encode op30] [commit once]
  → 1 × 5 µs = 5 µs dispatch overhead
```

This is conceptually identical to CUDA Graphs — capture the entire execution
sequence and replay it as one GPU submission.

**Impact:** Dispatch overhead reduced from ~120 µs to ~5 µs per inference.

### C. Micro-batching for GPU utilization

**Problem:** Single-request inference used <15% of GPU compute capacity.

**Solution:** I implemented a **bounded micro-batch accumulator** between
the shared-memory session queue and the model inference call:

- **Maximum batch size:** 8 sessions
- **Maximum wait time:** 500 µs
- **Deadline-aware:** dispatch immediately if oldest request is near SLA

```rust
fn inference_worker_loop(
    rx: &mut Consumer<u32>,       // SPSC ring of session offsets
    arena: &PipelineArena,
    model_server: &ModelServer,
    worker_id: usize,
) {
    let mut batch: Vec<u32> = Vec::with_capacity(MAX_BATCH);
    let mut window_start = Instant::now();

    loop {
        // Try to accumulate a micro-batch
        if let Some(offset) = rx.try_pop() {
            batch.push(offset);
        }

        let should_dispatch =
            batch.len() >= MAX_BATCH ||
            (!batch.is_empty() && window_start.elapsed() > MAX_WINDOW) ||
            (!batch.is_empty() && oldest_near_deadline(&batch, arena));

        if should_dispatch && !batch.is_empty() {
            // Build batched input from session contexts in shared memory
            let input = build_batch_input(&batch, arena);

            // Run batched inference (single GPU dispatch for all sessions)
            let output = model_server.run_batch(worker_id, &input);

            // Write results back to each session's context in shared memory
            scatter_results(&batch, &output, arena);

            batch.clear();
            window_start = Instant::now();
        }
    }
}
```

**Impact:** GPU utilization increased from 15% to 65%. Throughput improved 4x
with only ~300 µs additional latency from the batching window.

### D. Graceful degradation

**Problem:** If model inference was slow or failing, the entire pipeline stalled.

**Solution:** I implemented a **circuit breaker + fallback** pattern:

```rust
fn run_nlu_with_fallback(
    session: &mut SessionContext,
    arena: &PipelineArena,
    model_server: &ModelServer,
    circuit_breaker: &CircuitBreaker,
) -> NLUResult {
    if circuit_breaker.is_open() {
        // Model service is degraded — fall back to rule-based NLU
        return rule_based_nlu(session, arena);
    }

    match model_server.run_with_timeout(session, Duration::from_millis(20)) {
        Ok(result) => {
            circuit_breaker.record_success();
            result
        }
        Err(Timeout) => {
            circuit_breaker.record_failure();
            // Fall back to rule-based NLU for this request
            rule_based_nlu(session, arena)
        }
    }
}
```

The circuit breaker tracked error rates over a sliding window and tripped
to the open state after sustained failures, preventing cascading timeouts.

**Impact:** During model deployment incidents, user-facing error rate dropped
from ~8% to <0.5% because the fallback path handled requests that would
otherwise have timed out.

### E. Observability

I instrumented every stage with per-request timing in the shared session context:

```cpp
// Written by each stage into the session context
session->stage_b_complete_ns = clock_gettime_ns();
session->nlu_model_inference_ns = inference_end - inference_start;
session->nlu_batch_size = current_batch_size;
session->nlu_fallback_used = used_fallback;
```

This gave us a **complete per-request timeline** showing exactly where each
millisecond was spent — ingress, ASR, NLU inference, search, orchestration,
TTS — all without any external tracing framework overhead.

## Results

| Metric | Before | After | Improvement |
|---|---|---|---|
| Cold start latency | 8–12 sec | **0 ms** (pre-loaded) | **Eliminated** |
| NLU inference p50 | 15 ms | **8 ms** | 47% faster |
| NLU inference p99 | 80 ms | **18 ms** | 78% faster |
| GPU utilization | 15% | **65%** | 4.3x better |
| Dispatch overhead | 120 µs | **5 µs** | 24x reduction |
| Error rate during deployments | ~8% | **< 0.5%** | 16x better |
| Throughput (sessions/sec/GPU) | ~800 | **~3,200** | 4x improvement |

---

# STORY 3: Search Architecture and Retrieval Optimization
## (Making Knowledge Retrieval Fast Enough for Real-Time Conversational Response)

## Situation

Stage C of the pipeline handled **NLU + Search/Ranking** — after understanding
the user's intent and entities, the system needed to retrieve relevant knowledge
to formulate a response. In 2022, the search architecture had three retrieval
paths depending on query type:

1. **Structured lookup** — direct API calls (weather, calendar, timers)
2. **Keyword search** — BM25-style retrieval over Apple's content index
3. **Semantic search** — a newer capability we were building for queries that
   didn't match keyword patterns well

The semantic search path was my primary contribution to the search architecture.

## The search stack (2022–2024 realistic)

### What we used

**For keyword search:**
- **OpenSearch** (AWS-managed) with custom analyzers and scoring profiles
- BM25 ranking with domain-specific boosts
- Pre-computed document features stored alongside the index

**For semantic search:**
- **FAISS** as an in-process vector search library (not a separate vector database)
- Purpose-built vector databases like Pinecone and Milvus existed but were
  very early-stage in 2022 — we chose FAISS for latency (in-process, no network hop)
  and maturity
- Sentence-transformer-style embedding model (distilled, ~60M params) for
  query and document encoding

**For structured lookup:**
- Direct API calls to Apple services (weather, maps, calendar, etc.)
- Response time: 5–30 ms depending on the service

### Why FAISS over a vector database

In 2022, purpose-built vector databases were still new. Our decision was:

| Factor | FAISS (chosen) | Vector DB (evaluated) |
|---|---|---|
| Latency | Sub-ms (in-process) | 5–20 ms (network hop) |
| Maturity | Battle-tested since 2017 | Very new (2021–2022) |
| Integration | C++ library, direct FFI | Separate service to operate |
| Operational cost | Zero (embedded in process) | New infra to manage |
| Trade-off | No persistence, no CRUD | Full DB features |

Since our knowledge index was **rebuilt daily** from Apple's content pipeline
and loaded at service startup, we didn't need persistence or CRUD — we needed
raw search speed. FAISS was the right choice.

## What I built

### A. Hybrid retrieval (keyword + semantic)

For queries that needed search, I implemented a **hybrid retrieval pipeline**
that ran keyword and semantic search in parallel and fused the results:

```cpp
struct SearchResult {
    uint32_t doc_id;
    float    bm25_score;      // from OpenSearch
    float    semantic_score;   // from FAISS
    float    fused_score;      // RRF or weighted combination
    uint32_t content_offset;   // offset into shared arena for doc content
};

SearchResults hybrid_search(
    const QueryContext& query,
    OpenSearchClient& os_client,
    FAISSIndex& faiss_index,
    EmbeddingModel& embedder
) {
    // Step 1: Generate query embedding (for semantic search)
    auto query_embedding = embedder.encode(query.text);  // ~2 ms on GPU

    // Step 2: Run keyword and semantic search IN PARALLEL
    auto keyword_future = std::async(std::launch::async, [&]() {
        return os_client.search(query.text, query.filters, TOP_K);  // ~8 ms
    });

    auto semantic_future = std::async(std::launch::async, [&]() {
        return faiss_index.search(query_embedding.data(), TOP_K);  // ~0.5 ms
    });

    auto keyword_results = keyword_future.get();
    auto semantic_results = semantic_future.get();

    // Step 3: Reciprocal Rank Fusion (RRF) to combine results
    return rrf_fusion(keyword_results, semantic_results, RRF_K);
}
```

### B. FAISS index design

I designed the FAISS index for Apple's product knowledge base (~2M documents):

```cpp
class KnowledgeIndex {
    faiss::IndexIVFPQ* index;       // IVF + Product Quantization
    std::vector<DocMetadata> docs;   // document metadata (title, source, etc.)

public:
    void build(const std::vector<float>& embeddings, int num_docs, int dim) {
        // IVF with 4096 clusters + PQ compression to 32 bytes per vector
        int nlist = 4096;
        int m = 32;      // subquantizers
        int nbits = 8;   // bits per subquantizer

        auto quantizer = new faiss::IndexFlatIP(dim);  // inner product
        index = new faiss::IndexIVFPQ(quantizer, dim, nlist, m, nbits);

        // Train on representative sample
        index->train(num_docs, embeddings.data());
        index->add(num_docs, embeddings.data());

        // Search 64 of 4096 clusters (tuned for recall vs speed)
        index->nprobe = 64;
    }

    SearchResults search(const float* query, int k) {
        std::vector<float> distances(k);
        std::vector<faiss::idx_t> indices(k);
        index->search(1, query, k, distances.data(), indices.data());
        // Map indices to document metadata
        return build_results(distances, indices, k);
    }
};
```

Key design decisions:
- **IVF+PQ** for memory efficiency: 2M docs × 768D × 4 bytes = ~6 GB raw;
  with PQ compression to 32 bytes/vector = ~64 MB (94x reduction)
- **nprobe=64** (searching 64 of 4096 clusters) gave us 95%+ recall
  at <1 ms search time
- Index rebuilt daily from the content pipeline; loaded at startup into
  the shared memory arena

### C. Zero-copy integration with the pipeline

Search results were written directly into the shared memory arena, avoiding
any serialization between the search stage and the orchestration stage:

```cpp
void write_search_results_to_session(
    SessionContext* session,
    PipelineArena* arena,
    const SearchResults& results
) {
    // Allocate space in shared arena for result array
    size_t result_size = results.size() * sizeof(SearchResultEntry);
    uint32_t offset = arena->allocate(result_size, 64);

    // Write results directly into shared memory
    auto* entries = arena->resolve<SearchResultEntry>(offset);
    for (size_t i = 0; i < results.size(); i++) {
        entries[i].doc_id = results[i].doc_id;
        entries[i].fused_score = results[i].fused_score;
        entries[i].content_offset = results[i].content_offset;
        entries[i].source_type = results[i].source_type;
    }

    // Update session context — orchestration stage reads this directly
    session->search_results_offset = offset;
    session->result_count = results.size();
    session->top_score = results[0].fused_score;
}
```

The orchestration stage (Stage D) simply resolved the offset and read the
ranked results — no deserialization, no copy, no network fetch.

### D. Query embedding caching

Many queries had similar or identical prefixes (especially for common
intents like "what's the weather" or "play music"). I implemented an
**embedding cache** using a hash of the normalized query text:

```cpp
class EmbeddingCache {
    // LRU cache: query_hash → embedding vector
    LRUCache<uint64_t, std::vector<float>> cache;

public:
    std::vector<float> get_or_compute(
        const std::string& query_text,
        EmbeddingModel& model
    ) {
        uint64_t hash = hash_query(normalize(query_text));

        auto cached = cache.get(hash);
        if (cached) return *cached;

        auto embedding = model.encode(query_text);
        cache.put(hash, embedding);
        return embedding;
    }
};
```

**Impact:** ~40% cache hit rate on production traffic, saving ~2 ms per
cached hit (embedding model inference avoided).

## Results

| Metric | Before (keyword only) | After (hybrid + FAISS) | Improvement |
|---|---|---|---|
| Search relevance (NDCG@5) | 0.62 | **0.78** | 26% better |
| Semantic query coverage | 0% (not supported) | **~35% of queries** | New capability |
| Search latency p50 | 12 ms (OpenSearch only) | **9 ms** (parallel hybrid) | 25% faster |
| Search latency p99 | 45 ms | **22 ms** | 51% faster |
| FAISS search time | N/A | **0.4 ms** | Sub-ms in-process |
| Embedding cache hit rate | N/A | **~40%** | Saves 2 ms per hit |
| Inter-stage handoff | 8 ms (gRPC + protobuf) | **< 0.1 ms** (shared memory) | 80x faster |

---

# HOW THE THREE STORIES CONNECT

These three stories form a **single cohesive system**:

```
Story 1 (Zero-Copy Infrastructure)
  → Built the shared memory pipeline that all stages communicate through
  → Eliminated 30-80 ms of inter-stage overhead
  → Enabled Stories 2 and 3 to focus on model/search optimization
    rather than fighting infrastructure latency

Story 2 (LLM Runtime)
  → Productionized the NLU and response generation models
  → Contributed kernel batching and micro-batching optimizations
    back to the runtime team
  → Built the graceful degradation that kept the system reliable

Story 3 (Search Architecture)
  → Built the hybrid retrieval pipeline (keyword + semantic)
  → Integrated FAISS in-process for sub-ms vector search
  → Used the zero-copy infrastructure from Story 1 for
    search-to-orchestration handoff
```

Together, they reduced the **end-to-end cloud latency from 380 ms to 220 ms**
— a 42% improvement that directly impacted how responsive Siri felt to
hundreds of millions of users.

---

# INTERVIEW DELIVERY GUIDE

## If asked: "Tell me about a complex distributed system you built"
→ **Start with Story 1** — the shared memory pipeline is the most architecturally
   interesting and demonstrates systems thinking

## If asked: "How did you work with ML/LLM teams?"
→ **Use Story 2** — it shows you can go deep into model runtime internals
   even when you're not the model trainer

## If asked: "Tell me about search/retrieval system design"
→ **Use Story 3** — it shows hybrid retrieval, FAISS integration, and
   zero-copy handoff

## If asked: "What's your most impactful optimization?"
→ **Combine Stories 1 + 2** — "I replaced inter-stage gRPC with shared
   memory (Story 1) and optimized the model serving path (Story 2),
   together reducing end-to-end latency by 42%"

## The 60-second version (for phone screens):

> "At Apple, I was a systems engineer on the Siri cloud platform, responsible
> for productionizing the multi-stage conversational pipeline. The pipeline
> had five stages — streaming ingress, ASR, NLU/search, response orchestration,
> and TTS — and my key contribution was redesigning it from separate gRPC
> microservices into a single co-located system with shared-memory communication.
> I built a hugepage-backed shared memory arena in C++ with Rust SPSC descriptor
> rings for inter-stage signaling, eliminating 30-80 ms of serialization and
> network overhead. I also productionized the NLU transformer model serving
> with pre-loaded models, Metal command buffer batching to reduce GPU dispatch
> overhead, micro-batching for throughput, and circuit breaker fallbacks for
> reliability. And I built a hybrid search pipeline combining OpenSearch keyword
> search with in-process FAISS vector search for semantic retrieval, integrated
> through the same zero-copy shared memory infrastructure. Together, these
> brought end-to-end cloud latency from 380 ms to 220 ms."
