
# Layer 6: AI Model Serving & Inference — Complete Deep Dive
## "How do we run models fast and reliably in production?"

> **Why this layer matters:** This is where AI/HPC rubber meets the road.
> You can have perfect GPU kernels (Layer 4) and zero-copy data flow
> (Layer 5), but if you serve the model wrong — wrong batching, wrong
> runtime, wrong memory management — latency and throughput suffer.
>
> **The core insight:** The serving pattern depends on the model type.
> Small stateless models → embed in-process. Large stateful models
> (LLMs with KV cache) → serve separately. The wrong choice wastes
> either latency (unnecessary network hops) or resources (duplicate
> GPU memory).

---

# SECTION 1: MODEL SERVING PATTERNS

---

## 1.1 In-Process Embedding (for hot-path models)

### When to use
- Model fits comfortably in GPU memory alongside other models
- Model is STATELESS (no KV cache, no growing memory)
- Latency budget is tight (10-30 ms, can't afford network hop)
- Model is called on every request (100% of traffic)

### Models that fit this pattern
```
XGBoost         → C API          → ~0.3-1 ms    → in-process
ONNX Runtime    → C++ API        → ~2-10 ms     → in-process
FAISS           → C++ library    → ~0.5-1 ms    → in-process
Rules engine    → compiled C++   → ~0.01-0.1 ms → in-process
Small BERT      → ONNX Runtime   → ~8-15 ms     → in-process
```

### The architecture
```cpp
class ScoringEngine {
    BoosterHandle xgb;                    // XGBoost C API
    std::unique_ptr<Ort::Session> bert;   // ONNX Runtime C++ API
    std::unique_ptr<faiss::Index> index;  // FAISS C++ library
    RulesEngine rules;                    // compiled C++

    struct WorkerBuffers { /* pre-allocated per worker */ };
    std::vector<WorkerBuffers> workers;

    float score(int worker_id, const Features& features) {
        auto& buf = workers[worker_id];
        // ALL models read from SAME feature block — zero copy
        float xgb_score = run_xgboost(buf);
        float bert_score = run_bert(buf);        // GPU
        float faiss_score = run_faiss_search(buf); // GPU (zero D2H!)
        float rules_score = rules.evaluate(buf);
        return weighted_combine(xgb_score, bert_score, faiss_score, rules_score);
    }
};
```

### Why this works
- **Zero network overhead** — function calls, not gRPC
- **Zero serialization** — shared feature buffer
- **CPU ∥ GPU parallel** — XGBoost+rules on CPU while BERT+FAISS on GPU
- **Pre-allocated buffers** — no per-request allocation

---

## 1.2 Separate Service (for LLMs)

### When to use
- Model is LARGE (7B+ parameters, 10+ GB GPU memory)
- Model is STATEFUL (KV cache grows/shrinks per request)
- Model benefits from cross-request batching (continuous batching)
- Latency budget allows network hop (> 1 sec SLA)

### Models that fit this pattern
```
7B LLM (vLLM)          → HTTP/gRPC    → ~100-500 ms TTFT
7B LLM (TensorRT-LLM)  → HTTP/gRPC    → ~50-200 ms TTFT
13B+ LLM               → HTTP/gRPC    → ~200-1000 ms TTFT
```

### Why separate service for LLMs
1. **KV cache management** — paged allocation, eviction, offload
2. **Cross-request batching** — inflight/continuous batching
3. **Prefix caching** — shared system prompt KV reuse
4. **GPU memory isolation** — LLM doesn't compete with hot-path models
5. **Independent scaling** — add GPU capacity for LLM without touching hot path

---

## 1.3 Multi-Task Model (shared backbone, multiple heads)

### The optimization
Instead of 3 separate BERT models (intent + NER + embedding):
- ONE shared backbone → ONE forward pass
- Three lightweight heads on top
- 3× less GPU memory, 3× less compute, same accuracy

```
3 separate models:           1 multi-task model:
  BERT → intent  (8 ms)       BERT backbone (8 ms)
  BERT → NER     (8 ms)         → intent head  (+0.01 ms)
  BERT → embed   (6 ms)         → NER head     (+0.01 ms)
  Total: 22 ms                   → embed head   (+0.01 ms)
  GPU mem: ~1.5 GB              Total: ~8 ms
                                GPU mem: ~300 MB
```

---

# SECTION 2: LLM INFERENCE RUNTIMES

---

## 2.1 The Runtime Landscape

| Runtime | Language | Best for | Key feature |
|---|---|---|---|
| **vLLM** | Python | Easy deployment, broad model support | PagedAttention, APC |
| **TensorRT-LLM** | C++ | Max NVIDIA GPU performance | Compiled engine, FP8, C++ API |
| **SGLang** | Python | Prefix-heavy workloads | RadixAttention tree cache |
| **llama.cpp** | C/C++ | Embed in C++ process, edge | GGUF quantization, no deps |
| **NVIDIA Dynamo** | Multi | Fleet-scale serving | KV-aware routing, disaggregation |

## 2.2 Key LLM Serving Optimizations

### Continuous / Inflight Batching
```
WITHOUT (static batching):
  Batch = [A(100 tok), B(500 tok), C(50 tok)]
  C finishes at 50 → GPU slot WASTED until B finishes at 500
  
WITH (inflight batching):
  C finishes at 50 → slot freed → D enters immediately
  GPU always full, no head-of-line blocking
```

### Prefix Caching
```
System prompt (500 tokens) → computed ONCE, cached
Request 1: [cached 500 tokens] + [new 200 tokens] → prefill only 200
Request 2: [cached 500 tokens] + [new 150 tokens] → prefill only 150
Savings: 500 tokens × N requests worth of prefill compute
```

### KV Cache Quantization
```
FP16 KV: 7B model, 2048 context → ~6 GB per sequence
FP8 KV:  same → ~3 GB per sequence (2× more concurrent users)
INT8 KV: same → ~3 GB per sequence
```

### KV-Aware Routing (Dynamo)
```
Request with session_id=X arrives
Router checks: which worker has session X's KV cached?
  Worker 3 has it → route to Worker 3 → skip re-prefill
  
Without routing: every follow-up re-prefills entire history
With routing:    follow-ups reuse cached KV → 50-80% TTFT reduction
```

---

# SECTION 3: SEARCH & RETRIEVAL

---

## 3.1 Two-Stage Pipeline (Retrieve → Rank)

```
RETRIEVAL (recall-focused, fast, approximate):
  5M documents → top 50-100 candidates
  Methods: BM25 (keyword) + FAISS GPU (semantic)
  Latency: ~8 ms (parallel BM25 ∥ FAISS)

RANKING (precision-focused, slower, richer features):
  50-100 candidates → scored with ranking model → top 5-10
  Method: MLP or small transformer on GPU (batched)
  Latency: ~0.8 ms (GPU batched forward pass)
```

## 3.2 Hybrid Retrieval (BM25 + Semantic)

```
BM25 (keyword):  matches exact words → great for specific terms
FAISS (semantic): matches meaning → great for paraphrases/concepts
RRF fusion:       documents ranked high in BOTH get highest score

"cheap eats near me":
  BM25 finds: documents with "cheap" and "eats"
  FAISS finds: documents about "affordable restaurants"
  Fusion: documents about affordable restaurants with matching keywords → TOP
```

## 3.3 Embedding Training

```
Dual-encoder architecture:
  Query encoder:    "cheap eats near me" → query_vector (768D)
  Document encoder: "Affordable dining guide" → doc_vector (768D)

Training:
  Positive pairs: (query, clicked_document) → push vectors CLOSE
  Hard negatives: (query, keyword_match_but_irrelevant) → push APART
  Loss: contrastive (InfoNCE)

Evaluation:
  Recall@k, NDCG@k, online A/B tests
```

---

# SECTION 4: THE COMPLETE SERVING ARCHITECTURE

```
HOT PATH (10-30 ms):
┌──────────────────────────────────────────────┐
│ ALL IN-PROCESS (C++ / Rust)                   │
│                                              │
│ XGBoost (CPU)  ∥  Multi-task BERT (GPU)      │
│ Rules (CPU)    ∥  FAISS GPU search           │
│                ∥  GPU-batched ranking         │
│                                              │
│ Shared feature block → all models read same  │
│ Pre-allocated buffers → zero allocation      │
│ CUDA Graphs → minimal launch overhead        │
│ Parallel streams → CPU ∥ GPU concurrent      │
└──────────────────────────────────────────────┘
        │ FLAGGED (Arrow IPC, shared memory)
        ↓
WARM PATH (2-5 sec):
┌──────────────────────────────────────────────┐
│ LLM AS SEPARATE SERVICE                       │
│                                              │
│ Context builder reads Arrow (zero-copy)       │
│ → UDS to co-located TensorRT-LLM / vLLM     │
│ → Prefix caching (skip system prompt prefill) │
│ → KV-aware routing (reuse session KV)        │
│ → FP8 quantization (2× concurrent capacity)  │
│ → Streaming → TTS (hide generation latency)  │
└──────────────────────────────────────────────┘
```

---

# SECTION 5: INTERVIEW CHEAT SHEET

## "How do you serve multiple models without network overhead?"
→ Embed all hot-path models in one C++ process: XGBoost C API, ONNX Runtime
   C++ API, FAISS C++ library. All read from same feature block. Zero
   serialization, zero network hops.

## "Why not embed the LLM in-process too?"
→ LLM is stateful (KV cache management), benefits from cross-request
   batching, and uses 10+ GB GPU memory. Separate service gives better
   utilization, isolation, and scaling.

## "How do you optimize LLM serving?"
→ Prefix caching (skip shared prompt prefill), inflight batching (no
   head-of-line blocking), KV quantization (2× capacity), KV-aware
   routing (reuse cached state), CUDA Graphs (decode launch overhead).

## "What is the difference between retrieval and ranking?"
→ Retrieval = recall (cast wide net, find candidates from millions).
   Ranking = precision (score candidates with richer features, pick top-k).
   Two stages because retrieval must be fast (sub-ms per candidate) while
   ranking can be richer (one batched GPU forward pass for all candidates).

## "How do you train embeddings for search?"
→ Dual-encoder with contrastive loss on query-document pairs. Hard
   negatives from BM25 (keyword match but irrelevant). Evaluate with
   Recall@k, NDCG@k, and online A/B tests.
