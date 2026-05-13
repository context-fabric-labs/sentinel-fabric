
# Search Infrastructure Interview Preparation — Complete Guide
## Covering: Architecture, Optimizations, Embeddings, Training, and Common Questions

> **Your positioning:** "I built the GPU-accelerated search and ranking platform
> for a large-scale conversational AI system (Siri-like), handling millions of
> queries per day with sub-25ms Stage C latency. My contributions spanned
> zero-copy pipeline architecture, GPU vector search, batched ranking,
> multi-task NLU optimization, and embedding training infrastructure."

---

# PART 1: YOUR COMPLETE SEARCH INFRASTRUCTURE STORY

## The 3-minute narrative (memorize this)

> "I was responsible for the search and ranking infrastructure in a multi-stage
> conversational AI pipeline — similar to what LinkedIn does with Feed ranking
> or what powers voice assistants like Siri.
>
> When I joined, the search subsystem had three problems. First, inter-stage
> communication used gRPC with protobuf serialization, adding 30-80 ms of
> infrastructure overhead across the pipeline. Second, the NLU pipeline ran
> three separate BERT models — one for intent classification, one for entity
> extraction, one for query embedding — tripling GPU memory and compute for
> the same input. Third, vector search was CPU-bound using FAISS, and ranking
> scored candidates one at a time on CPU.
>
> I made four major optimizations. First, I replaced inter-stage gRPC with
> a hugepage-backed shared memory arena and SPSC descriptor rings in Rust,
> eliminating serialization entirely — inter-stage overhead dropped from
> 30-80 ms to under 2 ms.
>
> Second, I worked with the model team to consolidate three separate BERT
> models into one multi-task model with three output heads — intent on the
> CLS token, NER on all tokens, and query embedding via mean pooling. One
> forward pass instead of three: GPU memory dropped from 1.5 GB to 300 MB,
> and NLU latency from 22 ms to 8 ms.
>
> Third, I moved vector search from CPU FAISS to GPU FAISS, and critically,
> kept the query embedding on GPU after the NLU forward pass so it could
> flow directly into the search kernel with zero device-to-host transfer.
> Search latency dropped from 4 ms to 0.7 ms.
>
> Fourth, I batched all ranking candidates into a single GPU forward pass
> instead of scoring them sequentially on CPU. Ranking dropped from 5 ms
> to 0.8 ms.
>
> Together, the total search + ranking stage went from 65 ms at p99 to
> 22 ms — a 66% reduction — while also improving search relevance by 26%
> through the addition of semantic search alongside keyword retrieval."

---

# PART 2: DETAILED OPTIMIZATION STORIES

---

## Optimization A: Zero-Copy Between Pipeline Stages

### The problem
```
ASR → [gRPC + protobuf] → NLU → [gRPC + protobuf] → Search →
[gRPC + protobuf] → Ranking → [gRPC + protobuf] → Response → TTS

Each hop: 5-15 ms serialization + 2-5 ms network
4 hops × ~10-20 ms = 40-80 ms JUST for infrastructure
```

### What I built
```
ASR → [shared memory offset] → NLU → [shared memory offset] →
Search → [shared memory offset] → Ranking → [shared memory offset] →
Response → TTS

Each hop: < 0.5 ms (write offset to SPSC ring)
4 hops × ~0.5 ms = ~2 ms total infrastructure
```

### Implementation highlights

**Shared memory arena (C++, hugepage-backed):**
```cpp
class PipelineArena {
    void* base_ptr;
    std::atomic<uint64_t> write_offset;

public:
    PipelineArena(const std::string& name, size_t size) {
        int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
        ftruncate(fd, size);
        base_ptr = mmap(nullptr, size,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_HUGETLB, fd, 0);
        close(fd);
    }

    uint32_t allocate(size_t size, size_t alignment = 64) {
        // Atomic bump allocation — returns OFFSET, not pointer
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
        return reinterpret_cast<T*>(
            static_cast<uint8_t*>(base_ptr) + offset);
    }
};
```

**Fixed-layout session context (no serialization needed):**
```cpp
struct SessionContext {
    uint64_t session_id;
    uint64_t request_id;

    // ASR output (Stage B writes, Stage C reads)
    uint32_t transcript_offset;
    uint16_t transcript_len;
    float    asr_confidence;

    // NLU output (Stage C writes, Stage D reads)
    uint16_t intent_id;
    float    intent_confidence;
    uint32_t entities_offset;
    uint16_t entity_count;

    // Search results (Stage C writes, Stage D reads)
    uint32_t search_results_offset;
    uint16_t result_count;
    float    top_score;

    // Per-stage timing
    uint64_t stage_b_complete_ns;
    uint64_t stage_c_complete_ns;
    uint64_t stage_d_complete_ns;
};
```

**SPSC descriptor rings (Rust):**
```rust
// Ring carries ONLY 4-byte offsets, not data
fn asr_complete(ring: &mut Producer<u32>, session_offset: u32) {
    ring.push(session_offset).expect("backpressure");
}

fn nlu_receive(ring: &mut Consumer<u32>) -> Option<u32> {
    ring.pop().ok()
}
```

### Key design decisions
- **Offsets, not pointers** — safe across processes mapping at different addresses
- **Hugepage backing** — 512 MB arena with 2 MB pages reduces TLB entries from 262K to 256
- **Cacheline-aligned allocations** (64 bytes) — prevents false sharing
- **SPSC rings** — lock-free, wait-free, bounded backpressure
- **Rust + C++ FFI** — Rust for networking/orchestration, C++ for arena/models

### Results
| Metric | Before | After |
|---|---|---|
| Inter-stage overhead | 30-80 ms | **< 2 ms** |
| Serialization time | 5-15 ms/hop | **0 ms** |
| Memory allocations/request | ~120 | **~8** |
| Context switches/request | ~45 | **~6** |

### Interview follow-up answers

**Q: "Why shared memory instead of just in-process function calls?"**
> "The pipeline stages run as co-located but separate processes for
> fault isolation — if the NLU model crashes, it doesn't take down ASR
> or TTS. Shared memory gives us zero-copy IPC with process-level
> fault boundaries."

**Q: "Why SPSC instead of MPMC?"**
> "Each stage has a fixed producer-consumer relationship — ASR produces
> for NLU, NLU produces for ranking. SPSC is lock-free and wait-free
> with zero contention, which gives us the most deterministic latency.
> MPMC would add unnecessary synchronization overhead."

**Q: "Why hugepages?"**
> "The 512 MB arena spans 131K standard 4KB pages, requiring that many
> TLB entries. With 2 MB hugepages, we need only 256 entries. Since
> the TLB is a scarce hardware resource — typically a few hundred to
> a few thousand entries — hugepages ensure our session data stays
> TLB-resident and avoids expensive page-table walks."

---

## Optimization B: GPU Vector Search with FAISS

### The problem
```
Query embedding (from NLU model, on GPU)
    → cudaMemcpy D2H → embedding on CPU
    → CPU FAISS search (IVF+PQ, 5M docs)
    → 4 ms p50, 8 ms p99
```

### What I built
```
Query embedding (from NLU model, STAYS ON GPU)
    → GPU FAISS search (same GPU, zero transfer)
    → 0.7 ms p50, 1.2 ms p99
```

### Implementation
```cpp
class GPUSearchIndex {
    std::unique_ptr<faiss::gpu::StandardGpuResources> resources;
    std::unique_ptr<faiss::gpu::GpuIndexIVFPQ> index;

public:
    void initialize(int gpu_device, const std::string& path, int dim) {
        resources = std::make_unique<faiss::gpu::StandardGpuResources>();

        faiss::gpu::GpuClonerOptions opts;
        opts.useFloat16 = true;  // 2x memory savings

        faiss::Index* cpu_index = faiss::read_index(path.c_str());
        index.reset(dynamic_cast<faiss::gpu::GpuIndexIVFPQ*>(
            faiss::gpu::index_cpu_to_gpu(
                resources.get(), gpu_device, cpu_index, &opts)));
        delete cpu_index;

        index->setNumProbes(64);
    }

    void search(const float* query, int k, float* distances, int64_t* indices) {
        // If query is already on GPU (from NLU model output),
        // FAISS GPU uses it directly — ZERO transfer
        index->search(1, query, k, distances, indices);
    }
};
```

### The zero-copy insight
```
BEFORE:
  ONNX Runtime BERT (GPU) → output to GPU buffer
  → cudaMemcpyDeviceToHost → CPU buffer
  → pass to FAISS CPU → search on CPU
  = 1 D2H transfer + CPU computation

AFTER:
  ONNX Runtime BERT (GPU) → output to GPU buffer
  → FAISS GPU reads SAME buffer → search on GPU
  = ZERO transfers between NLU and search

The FAISS GPU wiki explicitly says: "If the inputs to search()
are already on the same GPU as the index, then no copies are
performed and the execution is fastest."
```

### Index design for 5M documents
```
Raw:      5M × 768D × 4 bytes = ~15 GB (doesn't fit on one GPU)
IVF+PQ:   5M × 32 bytes = ~160 MB (fits easily)
With FP16: 5M × 16 bytes = ~80 MB (even better)

Config: nlist=4096, m=32, nbits=8, nprobe=64
Recall@10: 95%+ at 0.7 ms search time
```

### Results
| Metric | CPU FAISS | GPU FAISS |
|---|---|---|
| Search p50 | 4 ms | **0.7 ms** |
| Search p99 | 8 ms | **1.2 ms** |
| D2H transfer | 0.1 ms | **0 ms** |
| Index memory | ~160 MB (CPU RAM) | ~80 MB (GPU, FP16) |

### Interview follow-up answers

**Q: "Why not a vector database like Pinecone?"**
> "In 2022, purpose-built vector databases were still very early-stage.
> Our decision was based on three factors: latency (FAISS in-process
> is sub-millisecond vs 5-20 ms network hop to a DB), maturity (FAISS
> had been battle-tested since 2017), and operational cost (embedded
> library vs new infrastructure to manage). Since our index was rebuilt
> daily and we didn't need CRUD, FAISS was the right choice."

**Q: "How do you handle index updates?"**
> "We rebuilt the index daily from the content pipeline. At startup,
> the service loads the latest index file and transfers it to GPU.
> For hot-swapping without downtime, we used a double-buffer approach:
> load the new index on GPU while the old one is still serving, then
> atomically switch the pointer."

**Q: "What about filtering?"**
> "FAISS doesn't support rich metadata filtering natively. For queries
> needing filters (like 'restaurants near me that are open now'), we
> ran the filter on the BM25/OpenSearch side and used FAISS for the
> semantic candidates. The RRF fusion then combined both result sets.
> LinkedIn's LiNR takes this further with GPU-native pre-filtering
> before exhaustive search."

---

## Optimization C: GPU-Batched Ranking

### The problem
```
50-100 candidates from retrieval
    → score each one independently on CPU
    → 50-100 × 0.05 ms = 5 ms p50, 12 ms p99
```

### What I built
```
50-100 candidates
    → batch into ONE tensor
    → ONE GPU forward pass
    → 0.8 ms p50, 1.5 ms p99
```

### Implementation
```cpp
void gpu_batched_ranking(
    int num_candidates,
    const float* candidate_features,  // [num_candidates × NUM_FEATURES]
    float* output_scores              // [num_candidates]
) {
    std::array<int64_t, 2> shape = {num_candidates, NUM_RANK_FEATURES};

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        cpu_mem_info,
        const_cast<float*>(candidate_features),
        num_candidates * NUM_RANK_FEATURES,
        shape.data(), shape.size());

    auto outputs = ranking_session->Run(
        Ort::RunOptions{nullptr},
        input_names, &input_tensor, 1,
        output_names, 1);

    memcpy(output_scores, outputs[0].GetTensorData<float>(),
           num_candidates * sizeof(float));
}
```

### Ranking features per candidate
```
Retrieval signals:
  [0] semantic_score     — from GPU FAISS
  [1] bm25_score         — from OpenSearch
  [2] rrf_fused_score    — from RRF fusion

Document features:
  [3] freshness          — how recent
  [4] quality_score      — editorial quality
  [5] popularity         — engagement rate
  [6] source_type        — knowledge graph / web / support / media
  [7] source_reliability — source trust score

Query-document match:
  [8] title_match_ratio  — keyword overlap with title
  [9] entity_overlap     — NER entities found in document
  [10] coverage_score    — how much of query is addressed

User/context features:
  [11] locale_match      — user language matches doc language
  [12] query_complexity  — simple fact vs complex question
  [13] user_domain_affinity — user's historical domain preference
```

### Results
| Metric | CPU sequential | GPU batched |
|---|---|---|
| 100 candidates p50 | 5 ms | **0.8 ms** |
| 100 candidates p99 | 12 ms | **1.5 ms** |
| Speedup | — | **6.3x** |

---

## Optimization D: Multi-Task BERT (Parallel Intent + NER + Embedding)

### The problem
```
Three separate BERT models, three forward passes:

Intent BERT:    input → 8 ms → intent label
NER BERT:       input → 8 ms → entity labels (SAME input!)
Embedding BERT: input → 6 ms → query vector (SAME input!)

Sequential: 22 ms
GPU memory: 3 × ~500 MB = ~1.5 GB for weights alone
```

### What I built
```
ONE multi-task BERT, ONE forward pass:

Shared BERT backbone: input → 8 ms → token representations
  → Intent head (CLS token):   +0.01 ms → intent label
  → NER head (all tokens):     +0.01 ms → entity labels
  → Embedding head (mean pool): +0.01 ms → query vector

Total: ~8 ms
GPU memory: ~300 MB
```

### The model architecture
```
Input: [CLS] Find cheap Italian restaurants near me [SEP]
            ↓
    ┌──────────────────────────────────────┐
    │  Shared DistilBERT backbone (6 layers)│
    │  ~66M parameters                      │
    │  ONE forward pass: ~8 ms              │
    └──────┬──────────┬──────────┬─────────┘
           │          │          │
    ┌──────▼───┐ ┌────▼────┐ ┌──▼──────────┐
    │ Intent   │ │ Entity  │ │ Embedding   │
    │ Head     │ │ Head    │ │ Head        │
    │ Linear   │ │ Linear  │ │ Mean Pool   │
    │ 768→30   │ │ 768→15  │ │ 768→768     │
    │ +Softmax │ │ +Softmax│ │ +L2 Norm    │
    └──────┬───┘ └────┬────┘ └──┬──────────┘
           │          │          │
    RESTAURANT   cheap=PRICE   [0.12, -0.34,
    _SEARCH      Italian=      0.56, ..., 0.23]
    (0.91)       CUISINE       768-dim vector
```

### Multi-task training (model team, with my input on serving constraints)
```python
class MultiTaskBERT(nn.Module):
    def __init__(self):
        self.backbone = AutoModel.from_pretrained("distilbert-base")
        self.intent_head = nn.Linear(768, NUM_INTENTS)
        self.entity_head = nn.Linear(768, NUM_ENTITY_LABELS)

    def forward(self, input_ids, attention_mask):
        outputs = self.backbone(input_ids, attention_mask)
        tokens = outputs.last_hidden_state   # [batch, seq, 768]
        cls = tokens[:, 0, :]                # [batch, 768]

        intent_logits = self.intent_head(cls)
        entity_logits = self.entity_head(tokens)
        query_embedding = tokens.mean(dim=1)  # mean pooling
        query_embedding = F.normalize(query_embedding, p=2, dim=1)

        return intent_logits, entity_logits, query_embedding

# Combined loss
loss = (0.4 * CrossEntropyLoss(intent_logits, intent_labels)
      + 0.3 * CrossEntropyLoss(entity_logits.view(-1, NUM_LABELS),
                                entity_labels.view(-1))
      + 0.3 * contrastive_loss(query_embedding, positive_doc_emb,
                                negative_doc_emb))
```

### ONNX export (one model, three outputs)
```python
torch.onnx.export(
    model,
    (dummy_ids, dummy_mask),
    "nlu_multitask_v3.onnx",
    input_names=["input_ids", "attention_mask"],
    output_names=["intent_logits", "entity_logits", "query_embedding"],
    dynamic_axes={
        "input_ids": {0: "batch", 1: "seq_len"},
        "attention_mask": {0: "batch", 1: "seq_len"},
    }
)
```

### C++ serving (one Run call, three outputs)
```cpp
auto outputs = session->Run(
    Ort::RunOptions{nullptr},
    input_names, inputs.data(), 2,
    output_names, 3);

float* intent_logits    = outputs[0].GetTensorData<float>();
float* entity_logits    = outputs[1].GetTensorData<float>();
float* query_embedding  = outputs[2].GetTensorData<float>();
// query_embedding is ON GPU — feeds directly to FAISS GPU search
```

### Results
| Metric | 3 separate BERTs | 1 multi-task BERT |
|---|---|---|
| Forward passes | 3 | **1** |
| GPU memory | ~1.5 GB | **~300 MB** |
| NLU latency | 22 ms | **8 ms** |
| Compute (FLOPS) | 3× | **1×** |

---

# PART 3: THE COMPLETE PARALLEL EXECUTION TIMELINE

```
Time →  0ms    2ms    4ms    6ms    8ms    10ms   12ms
        │      │      │      │      │      │      │
GPU S1: [═══════ multi-task BERT (intent+NER+embedding) ═══]
GPU S2:                                     [search][rank]
CPU:    [═══════════ BM25 OpenSearch ═══════════][fusion]

Breakdown:
  GPU Stream 1: BERT forward pass (8 ms)
    → produces intent + entities + query embedding
  GPU Stream 2: starts AFTER embedding is ready
    → FAISS GPU search (0.7 ms) + batched ranking (0.8 ms)
  CPU: BM25 search runs in PARALLEL with GPU (8 ms)
  CPU: RRF fusion after both GPU and CPU results ready (1 ms)

Wall clock: max(GPU, CPU) + fusion + overhead = ~12 ms
```

---

# PART 4: EMBEDDING TRAINING — COMMON INTERVIEW QUESTIONS

---

## Q: "How do you train embeddings for search?"

> "We used a **dual-encoder architecture** trained with **contrastive learning**.
> The query encoder and document encoder share the same BERT backbone but are
> fine-tuned so that relevant query-document pairs have high cosine similarity
> and irrelevant pairs have low similarity."

### Training data
```
Positive pairs (from click logs):
  query: "cheap Italian restaurants near me"
  document: "Affordable Italian Dining in Your Area" (user clicked this)

Hard negatives (from BM25 retrieval):
  query: "cheap Italian restaurants near me"
  document: "Italian Language Learning Resources" (BM25 matched 'Italian'
            but user didn't click — hard negative)

In-batch negatives:
  Other documents in the same training batch serve as additional negatives
```

### Training loss
```python
def contrastive_loss(query_emb, pos_doc_emb, neg_doc_embs, temperature=0.05):
    # Positive similarity
    pos_sim = F.cosine_similarity(query_emb, pos_doc_emb) / temperature

    # Negative similarities
    neg_sims = F.cosine_similarity(
        query_emb.unsqueeze(1), neg_doc_embs, dim=2) / temperature

    # InfoNCE loss
    logits = torch.cat([pos_sim.unsqueeze(1), neg_sims], dim=1)
    labels = torch.zeros(logits.size(0), dtype=torch.long)
    return F.cross_entropy(logits, labels)
```

### Hard negative mining
> "Hard negatives are critical. Random negatives are too easy — the model
> learns nothing from them. We mined hard negatives from BM25: documents
> that match keywords but aren't actually relevant. This forces the model
> to learn semantic understanding beyond keyword overlap."

---

## Q: "How do you evaluate embedding quality?"

> "We used three metrics:
> 1. **Recall@k** — what fraction of relevant documents appear in the top-k
>    retrieved results (measures retrieval quality)
> 2. **NDCG@k** — normalized discounted cumulative gain (measures ranking quality)
> 3. **Embedding alignment** — cosine similarity distribution between
>    positive pairs vs random pairs (measures embedding space quality)
>
> We also did **online A/B tests** — the ultimate measure is whether
> better embeddings lead to better user engagement."

---

## Q: "How do you handle embedding drift / staleness?"

> "Documents get new embeddings when the content pipeline runs (daily rebuild).
> But the query encoder is the same model, so new queries still produce
> compatible embeddings. When we retrain the model (every 2-4 weeks), we
> must re-encode ALL documents because the embedding space changes.
> We use a double-buffer approach: encode documents with the new model
> in the background, then atomically swap the FAISS index."

---

## Q: "How do you handle cold-start (new content with no engagement data)?"

> "This is exactly where semantic embeddings shine. A new document with
> zero engagement history can still be retrieved if its content is
> semantically similar to the query. The LLM-generated embedding captures
> the meaning of the content from the text alone — no engagement history
> needed. LinkedIn's unified retrieval system uses this exact approach,
> and they reported it solved the cold-start problem that collaborative
> filtering couldn't address."

---

## Q: "What is the difference between BM25 and semantic search?"

> "BM25 is keyword-based — it matches exact terms and their frequencies.
> 'cheap Italian restaurants' matches documents containing those exact words.
> Semantic search matches MEANING — 'cheap Italian restaurants' also matches
> 'affordable Tuscan dining' because the embeddings capture that these
> concepts are similar even though the words are different.
>
> In practice, you want BOTH — BM25 for high-precision keyword matches
> and semantic for broader conceptual coverage. That's why we used hybrid
> retrieval with RRF fusion."

---

## Q: "What is RRF (Reciprocal Rank Fusion)?"

> "RRF is a simple but effective method for combining ranked lists from
> different retrieval sources. For each document, you compute:
> score = Σ 1/(k + rank_in_source_i) across all sources.
> Documents ranked highly in multiple sources get boosted. The parameter
> k (typically 60) controls how much weight lower-ranked results get.
>
> It's model-free, requires no training, and works surprisingly well.
> We used it to combine BM25 keyword results with FAISS semantic results."

```python
def rrf_fusion(bm25_results, semantic_results, k=60):
    scores = {}
    for rank, doc_id in enumerate(bm25_results):
        scores[doc_id] = scores.get(doc_id, 0) + 1.0 / (k + rank + 1)
    for rank, doc_id in enumerate(semantic_results):
        scores[doc_id] = scores.get(doc_id, 0) + 1.0 / (k + rank + 1)
    return sorted(scores.items(), key=lambda x: -x[1])
```

---

## Q: "How does a ranking model differ from retrieval?"

> "Retrieval is about RECALL — casting a wide net to find potentially
> relevant candidates from millions of documents. It must be fast
> (sub-millisecond per query) so it uses lightweight scoring like
> dot product similarity.
>
> Ranking is about PRECISION — given 50-100 candidates from retrieval,
> score them with a richer model that considers more features (document
> quality, freshness, user preferences, source reliability). The ranking
> model can be more expensive per candidate because there are only
> 50-100 to score, not millions.
>
> The two-stage pipeline (retrieve → rank) is the standard architecture
> used by Google, LinkedIn, and most search systems."

---

## Q: "What is the difference between pointwise, pairwise, and listwise ranking?"

> "Pointwise: score each candidate independently (simplest, what we used).
> The model predicts a relevance score for each candidate in isolation.
>
> Pairwise: train the model to correctly order pairs of candidates.
> Given candidates A and B, predict which one should rank higher.
> LambdaMART is a classic pairwise approach.
>
> Listwise: optimize the entire ranked list at once. The loss function
> operates on the full ordering, not individual scores or pairs.
> ListMLE and ListNet are examples.
>
> LinkedIn's Feed SR uses a different paradigm — sequential ranking —
> where the model considers the user's entire interaction history as
> context for scoring candidates. This is more powerful than any of
> the three traditional approaches because it captures temporal
> engagement patterns."

---

## Q: "How would you improve retrieval quality beyond what you built?"

> "Three directions:
> 1. **Learned retrieval** — instead of using a fixed FAISS index with
>    pre-computed embeddings, use a model that can score query-document
>    pairs more richly at retrieval time. LinkedIn's LiNR does this by
>    putting the model AND the index on GPU together.
>
> 2. **Cross-encoder reranking** — after the initial retrieval and
>    lightweight ranking, apply a cross-encoder that processes the
>    query and document TOGETHER (not independently). This is much more
>    accurate but too expensive for the retrieval stage.
>
> 3. **Multi-vector retrieval** — instead of one embedding per document,
>    use multiple embeddings (e.g., one per paragraph or one per aspect).
>    ColBERT uses this approach for late interaction between query and
>    document token-level embeddings."

---

# PART 5: COMPLETE RESULTS SUMMARY

| Optimization | Before | After | Impact |
|---|---|---|---|
| **A. Zero-copy pipeline** | 30-80 ms overhead | < 2 ms | 15-40x reduction |
| **B. GPU vector search** | 4 ms (CPU FAISS) | 0.7 ms (GPU FAISS) | 5.7x faster |
| **C. GPU-batched ranking** | 5 ms (CPU, sequential) | 0.8 ms (GPU, batched) | 6.3x faster |
| **D. Multi-task BERT** | 22 ms (3 models) | 8 ms (1 model) | 2.75x faster, 5x less memory |
| **Search relevance (NDCG@5)** | 0.62 (BM25 only) | 0.78 (hybrid) | +26% |
| **Total Stage C p99** | 65 ms | **22 ms** | **66% reduction** |

---

# PART 6: TOP 20 INTERVIEW QUESTIONS WITH ANSWERS

## Architecture & System Design

**1. "Design a search system for a voice assistant"**
→ Draw the pipeline: ASR → NLU → Retrieval → Ranking → Response → TTS
→ Explain two-stage retrieval (recall) + ranking (precision)
→ Discuss latency budget allocation per stage
→ Mention hybrid retrieval (BM25 + semantic)

**2. "How would you reduce search latency from 60ms to 20ms?"**
→ Profile first (find the bottleneck)
→ Zero-copy between stages
→ Multi-task NLU (one model instead of three)
→ GPU vector search + GPU batched ranking
→ Parallel execution (BM25 on CPU ∥ GPU models)

**3. "How do you handle the cold-start problem?"**
→ Semantic embeddings capture meaning from content alone
→ No engagement history needed for new content
→ BM25 still works for keyword-relevant new content
→ Hybrid retrieval covers both

**4. "What happens when search is slow or fails?"**
→ Circuit breaker with fallback
→ If semantic search fails → fall back to BM25 only
→ If both fail → fall back to cached popular results
→ Strict timeouts on every external call

**5. "How do you A/B test search changes?"**
→ Shadow scoring: run new model alongside old, compare offline
→ Interleaving: mix results from old and new, measure engagement
→ Gradual rollout: 1% → 5% → 25% → 100%
→ Metrics: NDCG, click-through rate, time-to-result

## Embeddings & Models

**6. "Explain how BERT produces embeddings"**
→ Tokenize → 12 transformer layers → contextual token representations
→ Mean pooling over token representations → single vector
→ Trained with contrastive loss on query-document pairs

**7. "What is the difference between bi-encoder and cross-encoder?"**
→ Bi-encoder: encode query and document independently, compare with dot product
→ Cross-encoder: encode query + document together, more accurate but much slower
→ Use bi-encoder for retrieval (fast), cross-encoder for reranking (accurate)

**8. "How do you handle synonyms in search?"**
→ Semantic embeddings naturally handle synonyms
→ "cheap" and "affordable" have similar embeddings
→ BM25 misses synonyms; that's why hybrid search is better

**9. "What is approximate nearest neighbor search?"**
→ Exact search over millions of vectors is too slow
→ ANN algorithms (IVF, HNSW, PQ) trade small accuracy loss for large speed gain
→ FAISS IVF+PQ: partition into clusters, compress vectors, search only nearby clusters
→ GPU search (like LiNR) can do exhaustive search because GPU is fast enough

**10. "How do you measure embedding quality?"**
→ Recall@k, NDCG@k, MRR
→ Alignment/uniformity metrics on the embedding space
→ Online A/B tests (ultimate measure)

## GPU & Performance

**11. "Why move search to GPU?"**
→ Vector similarity is embarrassingly parallel (one dot product per candidate)
→ GPU has thousands of cores vs tens on CPU
→ The embedding is ALREADY on GPU from the NLU model — zero transfer
→ 5.7x speedup on search, 6.3x on ranking

**12. "How does FAISS GPU work?"**
→ Index transferred to GPU memory at startup
→ Search kernel computes dot products in parallel
→ If query is already on GPU, zero transfer needed
→ IVF+PQ with FP16 storage for memory efficiency

**13. "What profiling tools did you use?"**
→ Metal System Trace (Apple) / Nsight Systems (NVIDIA) for GPU timeline
→ Nsight Compute for per-kernel analysis
→ Application-level per-stage timing in shared session context
→ perf stat for CPU-side context switches and cache behavior

**14. "How do CUDA streams help in search?"**
→ Independent GPU workloads on separate streams execute concurrently
→ Intent classifier on stream 1 ∥ embedding model on stream 2
→ Wall clock = max of the two, not sum
→ Same principle as LinkedIn's parallel execution

**15. "What is shared-context batching (LinkedIn's approach)?"**
→ Compute the user's history representation ONCE
→ Score all candidate posts against that cached context
→ 1 full pass + N lightweight scoring passes instead of N full passes
→ 80x speedup at LinkedIn; same principle as my embedding reuse

## Operational

**16. "How do you deploy model updates without downtime?"**
→ Double-buffer: load new model/index while old serves
→ Atomic pointer swap when new is ready
→ Warmup inference before accepting traffic
→ Canary deployment: new model on 1% traffic first

**17. "How do you handle NUMA on GPU search nodes?"**
→ Pin worker threads to CPUs on same NUMA node as GPU
→ Allocate pinned host buffers on correct NUMA node
→ Verify with numastat that memory placement is local
→ Configure K8s topology manager with single-numa-node policy

**18. "How do you scale the search service?"**
→ Horizontal: add more GPU nodes, each with full index replica
→ Vertical: bigger GPU for larger index or more concurrent queries
→ Sharding: split index across GPUs if too large for one
→ Caching: cache frequent query embeddings (40% hit rate)

**19. "What is your monitoring strategy?"**
→ Per-stage latency histograms (p50, p99, p999)
→ GPU utilization and memory (DCGM/NVML)
→ Search quality metrics (NDCG, recall, cache hit rate)
→ Error rates and fallback activation rates
→ eBPF for kernel-level TCP retransmits and scheduler jitter

**20. "How does this connect to what LinkedIn does?"**
→ "My GPU vector search parallels LinkedIn's LiNR — both move retrieval
   to GPU for massive parallelism. My multi-task BERT parallels their
   unified embedding approach — one model serving multiple purposes.
   My shared-context reuse (computing BERT once for three tasks) parallels
   their shared-context batching in Feed SR (computing user history once
   for all candidates). The principles are the same: eliminate redundant
   computation, move parallelizable work to GPU, and share invariant
   state across requests."
