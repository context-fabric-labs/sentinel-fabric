# LinkedIn Search & Feed Department — 3-Day Interview Preparation Guide

## Prepared for: Sr. Staff / Principal Systems Engineer — Search & Recommendation Infrastructure
## Last Updated: May 2026

---

# STUDY SCHEDULE OVERVIEW

| Day | Focus | Hours | Goal |
|-----|-------|-------|------|
| **Day 1** | Search & Retrieval Fundamentals + LinkedIn-Specific Architecture | 8–10 | Understand the full search stack: query understanding → retrieval → ranking → serving |
| **Day 2** | Recommendation & Feed Systems + GPU Infrastructure | 8–10 | Master Feed ranking, sequential recommenders, GPU economics, and serving at scale |
| **Day 3** | Your Two Use Cases + Mock Interview Drills | 6–8 | Nail the FAISS + TigerGraph stories for Fraud Detection and Siri; practice under pressure |

---

# DAY 1: SEARCH & RETRIEVAL FUNDAMENTALS

---

## 1.1 Modern Search Architecture (LinkedIn's Stack as Reference)

LinkedIn recently published their reimagined search stack (Jan 2026). This is **exactly** what you'll be interviewed on.

### High-Level Pipeline

```
┌──────────────────────────────────────────────────────────────────────────┐
│  QUERY UNDERSTANDING                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                   │
│  │ Intent       │  │ Facet        │  │ Query        │                   │
│  │ Classification│→│ Extraction   │→│ Rewriting     │                   │
│  │ (1.5-4B LLM) │  │ (title,co,  │  │ (profile-    │                   │
│  │              │  │  location)   │  │  aware)      │                   │
│  └──────────────┘  └──────────────┘  └──────────────┘                   │
│  + Intelligent Router: lightweight encoder classifies → LLM path        │
│    vs keyword path                                                       │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  RETRIEVAL (Embedding-Based on GPU)                                       │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  Query Embedding (LLM encoder with prompt template)              │    │
│  │  "Instruct: Given a job search query, retrieve relevant postings"│    │
│  │  "Query: {query}"                                                │    │
│  └──────────────────────────────────────────────────────────────────┘    │
│                          │                                               │
│                          ▼                                               │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  GPU Exhaustive kNN Search (CUDA-accelerated)                    │    │
│  │  • Precomputed job embeddings stored in GPU-backed indexes       │    │
│  │  • Dot-product similarity                                        │    │
│  │  • Returns top-K candidates (hundreds to thousands)              │    │
│  └──────────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  RANKING (Cross-Encoder SLM on GPU via SGLang)                            │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  0.6B Decoder-Only SLM                                           │    │
│  │  Prompt: system + query + job_title + company + description      │    │
│  │  Output: P(yes) / P(no) → relevance score                       │    │
│  │                                                                   │    │
│  │  Multi-Task Distillation:                                        │    │
│  │    • 7B teacher → 1.7B → 0.6B student                           │    │
│  │    • Tasks: relevance + click + apply + view + connect           │    │
│  │    • NDCG@10: 0.9239, Click AUC: 0.67                           │    │
│  └──────────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  AUCTION / BLENDING                                                       │
│  • Budget pacing for sponsored results                                   │
│  • Relevance ↔ engagement ↔ business metric balance                     │
│  • Snippets (semantic phrase-level highlighting)                         │
│  • Reasoning module ("Searching for X at Y near Z")                     │
└──────────────────────────────────────────────────────────────────────────┘
```

### Key Numbers to Memorize

| Component | Metric | Value |
|-----------|--------|-------|
| Query Understanding | Model Size | 1.5–4B parameters |
| EBR Model | Architecture | Dual-tower bi-encoder (fine-tuned open-source LLM) |
| EBR Training | Loss | InfoNCE + margin-based pairwise loss |
| EBR Serving | Method | **Exhaustive kNN on GPU** (not ANN!) |
| Ranking SLM | Size | 0.6B decoder-only (distilled from 7B) |
| Ranking SLM | Throughput | 22,000 items/sec/GPU (with embedding compression) |
| Ranking SLM | Quality | NDCG@10 = 0.9239 |
| LLM Judge | Agreement | Weighted Cohen's Kappa ≥ 0.8 with PMs |

---

## 1.2 Embedding-Based Retrieval (EBR) — Deep Dive

### Dual-Tower Architecture

```
┌─────────────────┐     ┌─────────────────┐
│  Query Encoder   │     │  Document Encoder│
│  (shared LLM)    │     │  (shared LLM)    │
│                  │     │                  │
│  Input:          │     │  Input:          │
│  "Instruct: ..." │     │  title + company │
│  "Query: {q}"    │     │  + location +    │
│  "{Aspect}: {v}" │     │  description     │
│                  │     │                  │
│  Output:         │     │  Output:         │
│  query_emb ∈ ℝᵈ  │     │  doc_emb ∈ ℝᵈ   │
└────────┬─────────┘     └────────┬─────────┘
         │                        │
         └────────┐  ┌────────────┘
                  ▼  ▼
           sim(q, d) = q · d   (dot product)
```

### Training Recipe

1. **Data:** Millions of real query–job pairs from production logs
2. **Labels:** LLM-based judge (7B → distilled to 1.7B for throughput)
3. **Loss:** $\mathcal{L} = \mathcal{L}_{InfoNCE} + \lambda \cdot \mathcal{L}_{pair}$
   - InfoNCE: $\mathcal{L}_{InfoNCE} = -\log \frac{e^{sim(q, d^+)/\tau}}{\sum_k e^{sim(q, d_k^-)/\tau}}$
   - Pairwise: $\mathcal{L}_{pair} = \max(0, \gamma - sim(q, d^+) + sim(q, d^-))$
4. **Hard Mining:** Hard positives (relevant but low-ranked) + hard negatives (irrelevant but high-ranked)
5. **Infrastructure:** Hugging Face Accelerate + PyTorch FSDP across multiple GPUs

### Why Exhaustive Search on GPU (Not ANN)?

LinkedIn uses **exhaustive kNN** on GPU rather than approximate methods like FAISS HNSW/IVF. Why?

| Factor | Exhaustive GPU | ANN (FAISS HNSW/IVF) |
|--------|---------------|----------------------|
| **Recall** | 100% (exact) | 90–98% (tunable) |
| **Latency** | Sub-50ms on GPU for millions | Sub-5ms CPU, sub-1ms GPU |
| **Index Updates** | Trivial (replace embedding row) | Requires rebuild/reindex |
| **Freshness** | Near-real-time embedding updates | Stale until index rebuild |
| **Simplicity** | One matrix multiply | Complex index tuning |

**Key Insight:** When you have dedicated GPU capacity and freshness matters (job postings change hourly), exhaustive search is simpler and guarantees 100% recall. ANN is better when corpus is billions+ or GPU budget is constrained.

---

## 1.3 Ranking Model Efficiency — LinkedIn's SGLang Optimizations

LinkedIn published a detailed blog (Feb 2026) on how they optimized SGLang for **prefill-only ranking** workloads. This is cutting-edge and **will** come up in interviews.

### The Insight: Ranking ≠ Generation

```
┌──────────────────────────────────┐    ┌──────────────────────────────────┐
│  TEXT GENERATION (ChatGPT-like)  │    │  PREFILL-ONLY RANKING            │
│                                   │    │  (LinkedIn Search/Feed)          │
│  • Iterative token generation     │    │  • Single forward pass           │
│  • Autoregressive decode loop     │    │  • No decode/sampling            │
│  • KV cache grows per token       │    │  • Extract final logits only     │
│  • Throughput: tokens/sec         │    │  • Throughput: items/sec         │
│  • Latency: time-to-first-token   │    │  • Latency: time-to-score-batch  │
└──────────────────────────────────┘    └──────────────────────────────────┘
```

### 4 Stages of Optimization (memorize these)

**Stage 1: Batch Everything**
- Batch tokenization: 10× P99 reduction (4583ms → 464ms at QPS 500)
- Batch send over ZMQ: 41.5% latency reduction (70ms → 41ms)

**Stage 2: Scoring-Only Fast Path**
- Skip decode loop, sampling, KV cache updates
- Single vectorized GPU→CPU gather
- Result: **13.7× P99 improvement** (6220ms → 454ms)

**Stage 3: In-Batch Prefix Caching**
- All items share query prefix → compute prefix KV once
- Attention merging via log-sum-exp for suffix tokens
- Throughput: ~2,200 items/sec (comparable to Multi-Item Scoring)

**Stage 4: Python Runtime Fixes**
- `gc.freeze()` to eliminate GC stalls (100–300ms pauses)
- Multi-process gRPC to escape GIL
- Multi-process SGLang scheduling: +40% throughput

### Final Throughput Numbers

| Model | Input | Throughput | Gain |
|-------|-------|-----------|------|
| 375M ranker | 50 query + 150 item tokens | 750 → 2,200 items/s/GPU | 3× |
| 0.6B ranker | 60 query tokens + 1 embedding | 10K → 22K items/s/GPU | 2.2× |

---

## 1.4 Key Search Concepts to Know

### Inverted Index vs. Vector Index

| Aspect | Inverted Index (Lucene/Galene) | Vector Index (FAISS/GPU kNN) |
|--------|-------------------------------|------------------------------|
| **Representation** | Term → document_ids (sparse) | Document → embedding ∈ ℝᵈ (dense) |
| **Query** | Boolean/BM25 scoring | Nearest neighbor search |
| **Strengths** | Exact keyword match, entity names | Semantic similarity, synonyms |
| **Weaknesses** | Vocabulary mismatch | No exact match guarantee |
| **LinkedIn Usage** | Name/entity lookups (keyword path) | Job/People search (semantic path) |

### BM25 Scoring

$$BM25(D, Q) = \sum_{i=1}^{n} IDF(q_i) \cdot \frac{f(q_i, D) \cdot (k_1 + 1)}{f(q_i, D) + k_1 \cdot (1 - b + b \cdot \frac{|D|}{avgdl})}$$

Where: $f(q_i, D)$ = term frequency, $k_1 \approx 1.2$, $b \approx 0.75$

### NDCG@K (Normalized Discounted Cumulative Gain)

$$NDCG@K = \frac{DCG@K}{IDCG@K} \quad \text{where} \quad DCG@K = \sum_{i=1}^{K} \frac{2^{rel_i} - 1}{\log_2(i+1)}$$

### Recall@K

$$Recall@K = \frac{|\text{relevant documents in top-K}|}{|\text{total relevant documents}|}$$

### Mean Reciprocal Rank (MRR)

$$MRR = \frac{1}{|Q|} \sum_{i=1}^{|Q|} \frac{1}{rank_i}$$

---

## 1.5 Query Understanding

### Components

1. **Intent Classification:** What does the user want? (job search, people search, content search, navigation)
2. **Facet Extraction:** Structured attributes (title, company, location, skills, school)
3. **Query Rewriting:** Profile-aware expansion (user's location, industry context)
4. **Safety/Policy Checks:** Block harmful or policy-violating queries

### LinkedIn's Approach
- Single fine-tuned 1.5–4B LLM replaces multiple brittle NER + heuristic components
- Intelligent router: lightweight encoder classifies query type at high QPS
- Simple name lookups → keyword path; ambiguous queries → LLM semantic path

---

# DAY 2: RECOMMENDATION & FEED SYSTEMS + GPU INFRASTRUCTURE

---

## 2.1 LinkedIn Feed Architecture (March 2026 Blog)

### The Retrieval + Ranking Pipeline

```
┌──────────────────────────────────────────────────────────────────────────┐
│  UNIFIED RETRIEVAL (LLM-Based Dual Encoder)                              │
│                                                                           │
│  Member Prompt:                     Item Prompt:                         │
│  ┌─────────────────────────┐       ┌─────────────────────────┐          │
│  │ Profile: {headline}     │       │ Format: {type}          │          │
│  │ Skills: {skills}        │       │ Author: {name, headline}│          │
│  │ History: [engaged posts]│       │ Text: {post_text}       │          │
│  │ <view_pct>71</view_pct>│       │ <eng_pct>85</eng_pct>  │          │
│  └───────────┬─────────────┘       └───────────┬─────────────┘          │
│              │                                  │                        │
│              ▼                                  ▼                        │
│       member_emb ∈ ℝᵈ                    item_emb ∈ ℝᵈ                  │
│              │                                  │                        │
│              └──────────┐  ┌────────────────────┘                        │
│                         ▼  ▼                                             │
│                  cosine_sim(member, item)                                │
│                  GPU exhaustive kNN search                               │
│                  top-K candidates → ranking                              │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  SEQUENTIAL RANKING (Generative Recommender - GR)                        │
│                                                                           │
│  Input: 1000+ historical interactions as chronological sequence          │
│  ┌─────────────────────────────────────────────────────────────────┐     │
│  │ [post₁, action₁, post₂, action₂, ... , postₙ, actionₙ, cand] │     │
│  └─────────────────────────────────────────────────────────────────┘     │
│                          │                                               │
│                          ▼                                               │
│  Transformer (causal attention) → Late Fusion (+ count/affinity features)│
│                          │                                               │
│                          ▼                                               │
│  MMoE Prediction Head (DCNv2 experts, per-task gating)                  │
│  • Passive tasks: click, skip, long-dwell                               │
│  • Active tasks: like, comment, share                                   │
│                          │                                               │
│                          ▼                                               │
│  Per-candidate scores → final ranking                                   │
└──────────────────────────────────────────────────────────────────────────┘
```

### Key Feed Engineering Insights (Interview Gold)

1. **Percentile Buckets for Numerical Features:**
   - Raw numbers tokenize poorly ("12345" = multiple tokens, no ordinal meaning)
   - Convert to percentiles + wrap in special tokens: `<view_pct>71</view_pct>`
   - 30× improvement in correlation with embedding similarity
   - Recall@10 improved 15%

2. **Positives-Only History:**
   - Including skipped posts hurts model quality AND costs more compute
   - Positives-only: 37% memory reduction, 2.6× faster training
   - Better signal quality + cheaper compute = compounding gains

3. **Hard Negative Mining:**
   - Easy negatives: random posts not shown to member
   - Hard negatives: impressed but not engaged posts
   - +2 hard negatives per member → +3.6% recall

4. **Late Fusion Architecture:**
   - Transformer processes sequential history → rich temporal representation
   - Count/affinity features fused AFTER transformer (avoid quadratic cost inflation)
   - Best of both: sequential understanding + contextual signals

### Freshness Pipeline (3 Nearline Systems)

```
1. Prompt Generation ──► captures new posts, profile updates, engagement
                          within minutes
2. Embedding Generation ──► GPU inference clusters batch-process prompts
                            near-real-time for new posts
3. GPU-Accelerated Indexing ──► exhaustive kNN, sub-50ms retrieval
                                over millions of posts
```

---

## 2.2 Recommendation System Fundamentals

### Two-Tower (Dual Encoder) vs. Cross-Encoder

| Aspect | Two-Tower | Cross-Encoder |
|--------|-----------|---------------|
| **Architecture** | Separate encoders for query & item | Joint encoder for (query, item) pair |
| **Interaction** | Late interaction (dot product) | Early interaction (full attention) |
| **Inference** | O(1) per item (precompute item embeddings) | O(N) per item (must encode each pair) |
| **Quality** | Good for retrieval | Better for ranking |
| **Usage** | Retrieval stage (millions of candidates) | Ranking stage (hundreds of candidates) |

### Collaborative Filtering

- **User-Item Matrix:** Users × Items → interactions (ratings, clicks, purchases)
- **Matrix Factorization:** $R \approx U \cdot V^T$ where $U \in \mathbb{R}^{m \times k}$, $V \in \mathbb{R}^{n \times k}$
- **ALS (Alternating Least Squares):** Fix U, solve for V; fix V, solve for U
- **Cold Start Problem:** New users/items have no interactions → LLM embeddings help (LinkedIn's insight)

### Content-Based Filtering

- Represent items by features (text, metadata, embeddings)
- Represent user by aggregated features of items they liked
- Score = similarity(user_profile, item_features)
- No cold-start for items, but limited to user's existing taste bubble

### Hybrid Approaches (LinkedIn's Choice)

- LLM embeddings capture **semantic similarity** (world knowledge from pretraining)
- Engagement history captures **behavioral patterns** (collaborative signal)
- Combined: better cold-start handling + personalized ranking

---

## 2.3 Graph-Based Recommendation (TigerGraph Angle)

### Why Graph for Recommendations?

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Knowledge Graph for Search & Recommendation                             │
│                                                                          │
│  (User:Alice) ──[CONNECTED_TO]──► (User:Bob)                           │
│       │                               │                                  │
│   [VIEWED]                        [APPLIED]                             │
│       │                               │                                  │
│       ▼                               ▼                                  │
│  (Job:ML_Eng)  ──[AT_COMPANY]──► (Company:Google)                       │
│       │                               │                                  │
│   [REQUIRES]                     [IN_INDUSTRY]                          │
│       │                               │                                  │
│       ▼                               ▼                                  │
│  (Skill:PyTorch) ◄──[HAS_SKILL]── (User:Alice)                         │
│                                                                          │
│  Graph traversal: 2-3 hops reveals:                                     │
│  • Alice's connections applied to ML roles at Google                    │
│  • Alice has the required skills                                        │
│  • → Recommend this job with high confidence                            │
└─────────────────────────────────────────────────────────────────────────┘
```

### TigerGraph Architecture

| Component | Description |
|-----------|-------------|
| **Storage** | Distributed graph partitioned by vertex type |
| **Query Language** | GSQL — SQL-like with graph traversal primitives |
| **Traversal** | Multi-hop with filters, aggregations, accumulators |
| **Throughput** | Millions of vertices/sec per node |
| **Real-Time** | Sub-second for 2-3 hop queries on billion-edge graphs |

### Graph + Vector Search = Hybrid Retrieval

```
Step 1: Graph retrieval (TigerGraph)
  → "Find entities within 2 hops of user, weighted by edge type"
  → Returns: candidate set with graph-based scores

Step 2: Vector retrieval (FAISS on GPU)
  → "Find semantically similar entities to user's query embedding"
  → Returns: candidate set with embedding similarity scores

Step 3: Fusion
  → Merge candidates, combine scores (learned weights)
  → Re-rank with cross-encoder SLM

Step 4: Serve
  → Top-K results with explanations
```

---

## 2.4 GPU Infrastructure for Search & Ranking

### GPU Memory Hierarchy (Know This Cold)

```
┌──────────────────────────────────────────┐
│  GPU (H100 80GB)                          │
│                                           │
│  L1 Cache:  256 KB/SM  │ ~19 TB/s        │
│  L2 Cache:  50 MB      │ ~12 TB/s        │
│  HBM3:     80 GB       │ 3.35 TB/s       │
│                                           │
│  PCIe 5.0: ─────────── │ 128 GB/s (H↔D)  │
│  NVLink:   ─────────── │ 900 GB/s (D↔D)  │
│                                           │
│  Compute:  989 TFLOPS (FP8 Tensor Core)   │
│            ~60 TFLOPS (FP32)              │
└──────────────────────────────────────────┘
```

### GPU Serving Patterns

1. **Exhaustive kNN (LinkedIn EBR):**
   - Matrix multiply: $Q \cdot D^T$ where Q = queries, D = document matrix
   - Leverages Tensor Cores for massive parallelism
   - Best when: dedicated GPU capacity, need 100% recall, corpus < 100M

2. **FAISS GPU (IVF-PQ, CAGRA):**
   - Approximate NN with configurable recall/speed tradeoff
   - Best when: corpus > 100M, shared GPU, memory-constrained

3. **LLM Ranking (SGLang/vLLM/TRT-LLM):**
   - Prefill-only scoring for cross-encoder ranking
   - Key optimizations: batch tokenization, prefix caching, scoring-only path

### CUDA Optimization Patterns for Search

```cpp
// Pattern 1: CUDA Graphs — eliminate kernel launch overhead
// Capture the entire scoring pipeline as a graph, replay with 1 launch
cudaGraph_t graph;
cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal);
// ... all kernels ...
cudaStreamEndCapture(stream, &graph);
cudaGraphInstantiate(&exec_graph, graph, nullptr, nullptr, 0);
// At inference time:
cudaGraphLaunch(exec_graph, stream);  // ONE launch for entire pipeline

// Pattern 2: Pinned Memory — avoid pageable→pinned copy overhead
float* host_queries;
cudaMallocHost(&host_queries, size);  // Pinned (page-locked) memory
cudaMemcpyAsync(device_queries, host_queries, size,
                cudaMemcpyHostToDevice, stream);

// Pattern 3: Persistent Kernels — keep data in L2 cache
// Use cooperative groups + grid-stride loops
// Process multiple queries without returning to host
```

---

## 2.5 Zero-Copy Architecture Patterns

### Apache Arrow for Cross-Component Data Flow

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│  Feature Store   │    │  Scoring Engine  │    │  Ranking Model  │
│  (Rust/C++)      │    │  (C++/CUDA)      │    │  (PyTorch/C++)  │
│                  │    │                  │    │                  │
│  Arrow RecordBatch    │  Arrow Array      │  Arrow Tensor      │
│  ┌──────────────┐│    │  ┌──────────────┐│    │  ┌──────────────┐│
│  │float32[768]  ││───►│  │float32[768]  ││───►│  │float32[768]  ││
│  │(embedding)   ││    │  │(same memory) ││    │  │(same memory) ││
│  └──────────────┘│    │  └──────────────┘│    │  └──────────────┘│
└─────────────────┘    └─────────────────┘    └─────────────────┘
       │                        │                        │
       └────────────────────────┴────────────────────────┘
              Same physical memory — zero copies
```

### Shared Memory + SPSC Ring Buffer (Rust)

```rust
/// Descriptor ring — carries offsets, not payloads
#[repr(C, align(64))]
struct Descriptor {
    arena_offset: u32,      // Where in shared memory is the data
    payload_len: u32,       // How many bytes
    timestamp: u64,         // For ordering/debugging
    stage_mask: u8,         // Which stages have processed
}

/// SPSC ring — lock-free, cache-friendly
struct SPSCRing {
    buffer: *mut Descriptor,
    capacity: usize,        // Power of 2
    head: AtomicUsize,      // Consumer reads from head
    tail: AtomicUsize,      // Producer writes to tail
}
```

---

# DAY 3: YOUR TWO USE CASES + MOCK DRILLS

---

## 3.1 Use Case 1: FAISS + TigerGraph for Fraud Detection at CapitalOne

### 3-Minute Story

> "At CapitalOne, we built a real-time fraud detection system processing 20,000+ transactions per second with a < 5ms p99 latency SLA. The core innovation was combining **graph-based pattern detection** (TigerGraph) with **embedding-based anomaly scoring** (FAISS GPU) in a zero-copy pipeline.
>
> **The problem:** Traditional rule-based fraud detection missed 15–20% of sophisticated fraud patterns — organized rings that split transactions across multiple cards, merchants, and time windows. These patterns are invisible to single-transaction models but obvious in a graph.
>
> **Graph layer (TigerGraph):** We modeled the transaction network as a graph — cards, merchants, devices, IPs, and addresses as vertices; transactions as edges. A 2-3 hop GSQL query from any flagged entity reveals the entire fraud ring in sub-10ms. Graph features (degree centrality, connected component size, velocity across shared attributes) became first-class features for the scoring model.
>
> **Vector layer (FAISS GPU):** Each transaction is encoded into a 128d embedding via a pretrained MLP (features: amount, time, merchant category, card velocity, device fingerprint). FAISS GpuIndexIVFPQ searches the last 90 days of transactions (~10M embeddings) in < 0.3ms, returning the 10 nearest historical transactions. The distances become anomaly features — transactions far from their card's historical cluster are suspicious.
>
> **Zero-copy integration:** The Rust gateway parses the ISO 8583 message once into a slab-allocated `TxnView`. Features are extracted into a cacheline-aligned `FeatureBlock`. FAISS and TigerGraph results are written into the same Apache Arrow buffer. The XGBoost ensemble reads from this buffer — no serialization between components. The entire pipeline runs in a single process with GPU co-located on the same NUMA node.
>
> **Results:** Fraud detection rate improved from 82% to 94% (+12pp). False positive rate dropped 35%. P99 latency: 3.8ms (within 5ms SLA). The graph layer alone caught 8% of fraud that pure ML models missed — organized rings and velocity anomalies across shared attributes."

### Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────────┐
│  TIER 1: REAL-TIME FRAUD SCORING (< 5ms p99)                             │
│                                                                           │
│  ISO 8583 Message                                                        │
│       │                                                                   │
│       ▼                                                                   │
│  ┌──────────────┐                                                        │
│  │ Rust Gateway  │  Parse once → TxnView<'a> (borrowed, zero-alloc)     │
│  │ (per-core     │                                                        │
│  │  worker)      │                                                        │
│  └──────┬───────┘                                                        │
│         │                                                                 │
│         ▼                                                                 │
│  ┌──────────────┐                                                        │
│  │ FeatureBlock  │  Cacheline-aligned, all features as f32               │
│  │ (64-byte      │  + precomputed embeddings (user, merchant, device)    │
│  │  aligned)     │                                                        │
│  └──────┬───────┘                                                        │
│         │                                                                 │
│    ┌────┴────────────────────┐                                           │
│    │            │            │                                            │
│    ▼            ▼            ▼                                            │
│  ┌────────┐ ┌────────┐ ┌──────────┐                                     │
│  │XGBoost │ │FAISS   │ │TigerGraph│                                     │
│  │Ensemble│ │GPU     │ │2-hop     │                                     │
│  │(CPU)   │ │IVF-PQ  │ │Query     │                                     │
│  │< 1ms   │ │< 0.3ms │ │< 2ms    │                                     │
│  └───┬────┘ └───┬────┘ └────┬─────┘                                     │
│      │          │           │                                            │
│      └──────────┴───────────┘                                            │
│                 │                                                         │
│                 ▼                                                         │
│  ┌──────────────────────┐                                                │
│  │ Weighted Ensemble     │  Graph features + FAISS distances +           │
│  │ (final fraud score)   │  model scores → Go/No-Go decision            │
│  └──────────────────────┘                                                │
│                 │                                                         │
│                 ▼                                                         │
│  ┌──────────────────────┐                                                │
│  │ Arrow Event Buffer    │  → Tier 2 (decline reasoning)                │
│  │ (zero-copy publish)   │  → Tier 3 (triage agent)                     │
│  └──────────────────────┘  → Analytics/Retraining                       │
└──────────────────────────────────────────────────────────────────────────┘
```

### FAISS Configuration for Fraud Detection

```cpp
// 10M transaction embeddings, 128d, IVF-PQ on GPU
int d = 128;           // embedding dimension
int nlist = 4096;      // √(10M) ≈ 3162, round up to 4096
int m = 16;            // 16 sub-quantizers → 16 bytes/vector
int nbits = 8;         // 8 bits per code

faiss::gpu::StandardGpuResources res;
faiss::gpu::GpuIndexIVFPQConfig config;
config.interleavedLayout = true;   // cuVS backend for H100

faiss::gpu::GpuIndexIVFPQ index(&res, d, nlist, m, nbits,
                                 faiss::METRIC_L2, config);

// Train on representative sample
index.train(n_train, train_data.data());
index.add(n_db, db_vectors.data());

// Search at inference time
index.nprobe = 32;     // search 32 of 4096 clusters
index.search(nq, query.data(), 10, distances.data(), labels.data());
// Returns in ~0.3ms on GPU
```

### TigerGraph GSQL Query for Fraud Rings

```gsql
CREATE QUERY detect_fraud_ring(VERTEX<Card> seed_card, INT max_hops = 3)
  FOR GRAPH FraudGraph
  RETURNS (MapAccum<STRING, DOUBLE>)
{
  SetAccum<VERTEX> @@visited;
  MapAccum<STRING, DOUBLE> @@features;
  
  Start = {seed_card};
  
  // Hop 1: Card → Transactions → Merchants/Devices/IPs
  Hop1 = SELECT t FROM Start:s -(Transaction:e)- :t
         WHERE e.amount > 0 AND e.timestamp > now() - 86400
         ACCUM @@visited += t;
  
  // Hop 2: Shared entities → Other cards using same device/IP
  Hop2 = SELECT t FROM Hop1:s -(SharedDevice|SharedIP|SharedAddress:e)- :t
         WHERE t NOT IN @@visited
         ACCUM @@visited += t;
  
  // Aggregate graph features
  @@features += ("ring_size" -> @@visited.size());
  @@features += ("shared_devices" -> COUNT(Hop2 WHERE type == "Device"));
  @@features += ("velocity_24h" -> SUM(Hop1.amount));
  @@features += ("distinct_merchants" -> COUNT(DISTINCT Hop1.merchant_id));
  
  RETURN @@features;
}
```

---

## 3.2 Use Case 2: FAISS + TigerGraph for Siri Voice Assistant

### 3-Minute Story

> "For the Siri conversational AI platform, I built the search and entity resolution infrastructure that handles Stage C — NLU, vector search, and ranking — within a 25ms latency budget, processing millions of queries per day.
>
> **The problem:** When a user says 'Call John at his office' to Siri, the system needs to: (1) understand the intent (MAKE_CALL), (2) resolve 'John' to a specific contact, (3) find 'his office' phone number, and (4) rank if there are multiple Johns. The legacy system used dictionary lookups and regex, which failed on ambiguous queries and couldn't handle multi-turn context.
>
> **Vector layer (FAISS HNSW + GPU CAGRA):** All contact names, place names, music titles, and app names are encoded as 768d BERT embeddings and indexed in FAISS. For on-device (iPhone), we use HNSW on CPU (~2ms for 100K contacts). For cloud, we use CAGRA on GPU (~0.5ms for 10M entities). The key optimization: after the NLU BERT forward pass generates the query embedding on GPU, we keep it on GPU and pass it directly to FAISS CAGRA — zero device-to-host transfer. This saved 0.8ms per query.
>
> **Graph layer (TigerGraph):** The knowledge graph contains entities (contacts, places, apps, media), relationships (works_at, lives_near, listens_to), and user interaction history. A 2-hop traversal from the user vertex, filtered by NLU-extracted intent and entities, produces a contextually ranked candidate set. Example: 'the restaurant John mentioned last Tuesday' → traverse User→John→Conversations→Restaurants, filter by timestamp.
>
> **Zero-copy pipeline:** The shared memory arena (hugepage-backed) holds the entire conversational state — ASR output, NLU embeddings, FAISS results, graph features, ranking scores. Each stage writes to the arena and pushes a 32-byte descriptor to an SPSC ring. The Rust gateway orchestrates stages without serialization. Apache Arrow buffers carry embeddings between FAISS and the ranking model in the same contiguous float32 arrays.
>
> **Results:** Entity resolution accuracy improved from 84% to 96%. End-to-end Stage C latency: 22ms p99 (from 65ms). Search relevance improved 26% (measured by NDCG@10). Multi-turn context resolution went from 0% (stateless) to 89% accuracy."

### Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────────┐
│  STAGE C: NLU + SEARCH + RANKING (< 25ms p99)                           │
│                                                                           │
│  From Stage B (ASR)                                                      │
│  shared_state→asr_hypothesis = "Call John at his office"                 │
│       │                                                                   │
│       ▼                                                                   │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  NLU: Multi-Task DistilBERT (GPU)                                │    │
│  │  • Input: ASR text (zero-copy from shared memory)                │    │
│  │  • Output 1: Intent = MAKE_CALL (CLS head)                      │    │
│  │  • Output 2: Entities = [John:CONTACT, office:LOCATION_TYPE]    │    │
│  │  • Output 3: Query embedding ∈ ℝ⁷⁶⁸ (mean pooling)            │    │
│  │  • Latency: ~8ms                                                 │    │
│  │  • Query embedding STAYS ON GPU → feeds directly to FAISS       │    │
│  └──────────────────────────┬───────────────────────────────────────┘    │
│                              │                                           │
│                 ┌────────────┴────────────┐                              │
│                 │                         │                               │
│                 ▼                         ▼                               │
│  ┌──────────────────────┐  ┌──────────────────────┐                     │
│  │  FAISS CAGRA (GPU)    │  │  TigerGraph           │                     │
│  │  10M entity embeddings│  │  Knowledge Graph       │                     │
│  │  768d, GPU-native     │  │  2-hop traversal       │                     │
│  │  graph index           │  │  User→Contacts→Places │                     │
│  │  top-50 candidates    │  │  + context filter      │                     │
│  │  < 0.5ms              │  │  < 2ms                 │                     │
│  └──────────┬────────────┘  └──────────┬────────────┘                     │
│             │                          │                                  │
│             └──────────┬───────────────┘                                  │
│                        │                                                  │
│                        ▼                                                  │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  Candidate Fusion + Re-Ranking                                   │    │
│  │  • Merge FAISS candidates + graph candidates                     │    │
│  │  • Features: embedding_dist, graph_hops, interaction_recency,    │    │
│  │    contact_frequency, context_match_score                        │    │
│  │  • Batched GPU scoring (all candidates in 1 forward pass)       │    │
│  │  • < 0.8ms                                                       │    │
│  └──────────────────────────────────────────────────────────────────┘    │
│                        │                                                  │
│                        ▼                                                  │
│  Write results to shared_state → Stage D (Response Orchestration)       │
└──────────────────────────────────────────────────────────────────────────┘
```

### FAISS Configuration for Siri Entity Search

```cpp
// On-Device (iPhone — CPU, 100K entities)
// Use HNSW for best recall without GPU
int d = 768;
int M = 32;           // links per node
faiss::IndexHNSWFlat index(d, M);
index.hnsw.efSearch = 128;    // search beam width
// Memory: 768×4 + 32×8 = ~3.3 KB/entity → 330 MB for 100K
// Latency: ~2ms for top-10

// Cloud (GPU — 10M entities)
// Use CAGRA (GPU-native graph index via cuVS)
faiss::gpu::StandardGpuResources res;
faiss::gpu::GpuIndexCagraConfig config;
config.intermediate_graph_degree = 64;
config.graph_degree = 32;

faiss::gpu::GpuIndexCagra index(&res, d, faiss::METRIC_INNER_PRODUCT, config);
index.train(n_train, train_data.data());
index.add(n_db, db_vectors.data());
// Latency: ~0.5ms for top-50 on H100
// Can convert to HNSW for CPU fallback: index.copyTo(cpu_hnsw_index)
```

### TigerGraph for Contextual Entity Resolution

```gsql
CREATE QUERY resolve_entity(
  VERTEX<User> user,
  STRING entity_name,
  STRING intent,
  STRING context_filter
) FOR GRAPH SiriKnowledgeGraph
  RETURNS (ListAccum<TUPLE<STRING name, DOUBLE score>>)
{
  ListAccum<TUPLE<STRING name, DOUBLE score>> @@results;
  
  // Hop 1: User's contacts/entities
  Candidates = SELECT c FROM user:u -(Knows|HasContact|Uses:e)- :c
               WHERE c.name LIKE entity_name + "%"
               OR similarity(c.name_embedding, @@query_embedding) > 0.7;
  
  // Hop 2: Filter by intent context
  IF intent == "MAKE_CALL" THEN
    Candidates = SELECT c FROM Candidates:c
                 WHERE c.has_phone_number == true;
  END;
  
  // Hop 3: Context-aware ranking
  FOREACH c IN Candidates DO
    DOUBLE score = 0.4 * c.interaction_frequency
                 + 0.3 * c.recency_score
                 + 0.2 * c.embedding_similarity
                 + 0.1 * c.graph_proximity;
    @@results += TUPLE(c.name, score);
  END;
  
  RETURN @@results ORDER BY score DESC LIMIT 10;
}
```

---

## 3.3 Mock Interview Questions & Answers

### Q1: "Walk me through how you would design a search system for LinkedIn Jobs"

**Answer Framework (2 minutes):**

> "I'd design it as a 3-stage pipeline: query understanding, retrieval, and ranking.
>
> **Query understanding:** A fine-tuned 1.5–4B LLM extracts intent and facets (title, company, location) from the natural language query. An intelligent router sends precise entity lookups to the keyword path and ambiguous queries to the semantic path.
>
> **Retrieval:** A dual-tower bi-encoder (fine-tuned from an open-source LLM) encodes queries and jobs into dense embeddings. I'd use exhaustive kNN on GPU for retrieval — it gives 100% recall and allows near-real-time index updates as new jobs are posted. Training uses InfoNCE loss with hard negative mining from LLM-judged relevance data.
>
> **Ranking:** A 0.6B decoder-only SLM, distilled from a 7B teacher via multi-task KL divergence, scores each (query, job) pair. Multi-task heads predict relevance + engagement signals. For efficiency, I'd use a scoring-only execution path (no decode loop), in-batch prefix caching (query prefix KV computed once), and embedding compression (condense job description to single token).
>
> **Serving:** SGLang with batch tokenization, multi-process gRPC, and gc.freeze() for stable P99. Target: 22K items/sec/GPU with NDCG@10 > 0.92."

### Q2: "How would you use FAISS at scale?"

**Answer:**

> "It depends on the constraints. Three decision points:
>
> 1. **Corpus size:** < 10M vectors and dedicated GPU → exhaustive kNN (simplest, 100% recall). > 10M → ANN indexes.
>
> 2. **Memory budget:** Unconstrained → HNSW (CPU) or CAGRA (GPU) for best recall. Constrained → IVF-PQ (compressed vectors, m bytes per vector instead of d×4).
>
> 3. **Freshness requirements:** Frequent updates → Flat or IVF (easy to add/remove vectors). Rarely updated → HNSW or CAGRA (best quality, expensive to modify).
>
> For fraud detection, I used GpuIndexIVFPQ: 10M vectors, 128d, nlist=4096, m=16, nprobe=32. Memory: ~160MB on GPU. Search: 0.3ms. The key optimization was keeping embeddings on GPU after the feature encoding step — zero host↔device transfer.
>
> For the conversational AI system, I used CAGRA on GPU for cloud (10M entities, 768d, 0.5ms) and HNSW on CPU for on-device (100K entities, 2ms). CAGRA can be serialized to HNSW format for hybrid GPU-build/CPU-serve deployments."

### Q3: "Tell me about a time you optimized latency"

**Answer (STAR format):**

> **Situation:** The fraud detection hot path had a 5ms p99 SLA, but inter-component communication was eating 40-80ms just for serialization.
>
> **Task:** Reduce end-to-end latency to fit within the SLA while maintaining the full scoring ensemble (XGBoost + GPU neural net + FAISS + TigerGraph).
>
> **Action:** I replaced all gRPC/protobuf boundaries with a hugepage-backed shared memory arena. Each component writes results to the arena and pushes a 32-byte descriptor (offset + length) to an SPSC lock-free ring. The Rust gateway orchestrates stages by reading descriptors — never copies payloads. I also kept GPU tensors on-device between the feature encoder and FAISS search, eliminating a 0.8ms D2H+H2D round trip.
>
> **Result:** Infrastructure overhead went from 40-80ms to under 2ms. Total pipeline: 3.8ms p99. The zero-copy pattern was adopted by two other teams for their hot paths."

### Q4: "How does graph-based retrieval complement vector search?"

**Answer:**

> "They capture fundamentally different signals:
>
> - **Vector search** captures **semantic similarity** — 'cheap eats' matches 'budget restaurants' because their embeddings are close. But it has no notion of relationships or paths.
>
> - **Graph traversal** captures **structural relationships** — 'restaurants my friend John recommended' requires traversing User→Friend:John→Reviewed→Restaurant. No embedding can encode this arbitrary multi-hop path.
>
> In fraud detection, FAISS finds transactions that are anomalous in embedding space (unusual amount/time/merchant combinations), while TigerGraph reveals fraud rings — groups of cards sharing devices, IPs, or addresses that individually look normal but collectively form a suspicious pattern.
>
> The fusion strategy: retrieve candidates from both systems, merge with learned weights, and re-rank with a cross-encoder that sees both embedding distances and graph features."

### Q5: "What's your experience with Rust in systems programming?"

**Answer:**

> "I used Rust for the gateway and data plane in both projects. Key patterns:
>
> - **Zero-copy parsing:** `TxnView<'a>` borrows directly from the network buffer slab — no heap allocation for variable-length fields.
> - **SPSC ring buffers:** Lock-free, cache-line aligned, power-of-2 capacity with bitwise AND for modulo. Single producer/single consumer per core.
> - **FFI to C++/CUDA:** FAISS C API (libfaiss_c.so) linked via bindgen. Arrow C Data Interface for zero-copy buffer sharing between Rust and C++.
> - **Memory safety without GC:** Ownership model ensures arena memory is valid for the lifetime of the request. No use-after-free, no data races at compile time.
> - **Per-core architecture:** Each worker core owns its request from ingress through decision — no cross-core synchronization on the hot path."

---

## 3.4 LinkedIn-Specific Questions to Prepare For

### "Why LinkedIn?" (Have a genuine answer)

> "LinkedIn's search and feed systems operate at the intersection of my two strongest areas — GPU-accelerated inference infrastructure and search/recommendation at scale. The recent reimagining of the search stack with LLM-based semantic search, and the Feed team's work on sequential recommenders with transformer models — these are exactly the kinds of systems I've spent the last several years building. I want to work on problems where the engineering challenge is making sophisticated AI models serve millions of queries per second within strict latency budgets."

### LinkedIn-Specific Technical Topics

1. **Galene** — LinkedIn's custom search engine (inverted index + forward index)
2. **Venice** — Derived data platform (key-value store for embeddings, features)
3. **Samza/Flink** — Stream processing for nearline feature computation
4. **Pro-ML** — LinkedIn's ML platform for training and deployment
5. **SGLang** — Open-source LLM serving system LinkedIn actively contributes to
6. **Liger Kernel** — LinkedIn's GPU kernel library (fused operations for training)
7. **Couchbase** — Used for caching reasoning outputs and embeddings
8. **Flyte** — Workflow orchestration for offline pipelines (Spark-based)
9. **HDFS** — Exabyte-scale storage for training data and embeddings

### Feed Ranking Interview Deep Dives

1. **Multi-objective optimization:** How do you balance relevance, engagement, and business metrics?
2. **Position bias:** Users click top results more — how do you debias training data?
3. **Exploration vs exploitation:** How much novel content vs. proven relevant?
4. **Content quality:** How do you detect and demote low-quality/clickbait content?
5. **Fairness:** How do you ensure equitable treatment across creator demographics?

---

# APPENDIX: KEY PAPERS TO SKIM

| Paper | Relevance |
|-------|-----------|
| [GPU CUDA-based Search (ACM 2025)](https://dl.acm.org/doi/10.1145/3705328.3748116) | LinkedIn's exhaustive GPU retrieval |
| [GPU PyTorch-based Search (arXiv 2407.13218)](https://arxiv.org/pdf/2407.13218) | LinkedIn's embedding-based retrieval on GPU |
| [Efficient LLM Inference (arXiv 2510.22101)](https://arxiv.org/pdf/2510.22101) | Model pruning + context compression for LinkedIn SLM |
| [Context Compression / MixLM (arXiv 2512.07846)](https://arxiv.org/pdf/2512.07846) | Embedding compression for ranking |
| [LLM Query Understanding (arXiv 2509.09690)](https://arxiv.org/pdf/2509.09690) | LinkedIn's unified query understanding |
| [Large Scale Retrieval (arXiv 2510.14223)](https://arxiv.org/pdf/2510.14223) | LinkedIn's Feed retrieval with LLM dual encoders |
| [Generative Recommender (arXiv 2602.12354)](https://arxiv.org/pdf/2602.12354) | LinkedIn's sequential ranking model |
| [Flash Attention (arXiv 2205.14135)](https://arxiv.org/pdf/2205.14135) | Efficient attention computation |
| [InfoNCE Loss (arXiv 1807.03748)](https://arxiv.org/pdf/1807.03748) | Contrastive learning for retrieval |
| [MMoE (ACM 2018)](https://dl.acm.org/doi/pdf/10.1145/3219819.3220007) | Multi-gate Mixture-of-Experts for multi-task ranking |

---

# QUICK REFERENCE: NUMBERS THAT IMPRESS

| What | Number | Context |
|------|--------|---------|
| LinkedIn EBR throughput | >1.6B items exhaustive GPU search | Per-day scale |
| SLM ranking throughput | 22,000 items/sec/GPU | With embedding compression |
| SLM NDCG@10 | 0.9239 | Distilled 0.6B model |
| SGLang P99 improvement | 13.7× (6220ms → 454ms) | Scoring-only path |
| Batch tokenization | 10× P99 (4583ms → 464ms) | Dynamic async batching |
| Feed training speed | 2.6× faster | Positives-only history |
| Fraud detection p99 | 3.8ms | Full ensemble + FAISS + graph |
| FAISS GPU search | 0.3ms | 10M vectors, IVF-PQ, top-10 |
| Zero-copy overhead | 47 bytes copied | Per transaction across tiers |
| Graph ring detection | < 2ms | 2-hop TigerGraph traversal |
