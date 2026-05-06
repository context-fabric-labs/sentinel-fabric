# MASTER INTERVIEW SYLLABUS

### NVIDIA | Apple | Broadcom | HPE | Capital One

---

**Candidate:** Shailesh Pilare | 15+ years | AI Systems & Low-Latency Infrastructure
**Target Level:** Sr. Staff / Principal / Distinguished Engineer
**Domains:** GPU/Inference Platforms, HPC Systems, Low-Latency Distributed Systems, Agentic AI

---

# SECTION 1: STORY SUMMARIES

> Each story is a 2-minute verbal narrative backed by architecture diagrams, metrics, tech stack, and drill-down Q&A.

---

## Story A: CapitalOne — Three-Tier Agentic AI Fraud Detection Platform

### 2-Minute Summary

"I designed and built a **three-tier agentic AI fraud detection platform** at Capital One processing **24,500+ TPS** with **<5ms p99** at Tier 1. The system uses a shared-nothing, per-core architecture in Rust with zero-copy data flow through Apache Arrow.

**Tier 1 (Transaction Decisioning):** Sub-5ms latency. Request arrives, gets pinned to a worker core via hash(card_bin + device_id). A per-core arena allocator parses the request into a `TxnView` — zero-copy, no heap allocation. A cacheline-aligned `FeatureBlock` is built with precomputed embeddings (no online tokenization). An ensemble of 8–20 CPU scorers (XGBoost, GBDT, logistic scorecard, rule engine) all read from the same feature block. For GPU-accelerated MLP scoring, we use pre-instantiated CUDA Graphs with pinned buffers — 5µs launch overhead vs 150µs without graphs.

**Tier 2 (Failure Reasoning):** 2–5s latency. Declined transactions get routed to a 13B LLM (deployed via Sentinel/vLLM) that generates structured explanations. The LLM reads the full Arrow event buffer published by Tier 1 — zero deserialization.

**Tier 3 (Quick Triage):** 5–10s latency. Agent workflow for complex cases with tool integration.

The key architectural insight was **separating decisioning from reasoning** — Tier 1 never waits for an explanation. We publish a shared immutable Arrow buffer at the tier boundary, enabling downstream consumers (reasoning, triage, analytics, retraining) to read the same physical bytes without serialization."

### Architecture Diagram

```
                    24,500+ TPS
                        │
         ┌──────────────▼──────────────┐
         │     TIER 1: DECISIONING     │  < 5ms p99
         │  ┌────────────────────────┐ │
         │  │  Per-Core Workers      │ │
         │  │  • Arena allocator     │ │
         │  │  • TxnView (zero-copy) │ │
         │  │  • FeatureBlock        │ │
         │  │  • CPU Ensemble (8-20) │ │
         │  │  • GPU CUDA Graph MLP  │ │
         │  └────────────────────────┘ │
         │         GO / NO_GO          │
         └──────────────┬──────────────┘
                        │ Arrow Event Buffer (shared, immutable)
              ┌─────────┼─────────┐
              ▼         ▼         ▼
    ┌─────────────┐ ┌────────┐ ┌──────────┐
    │  TIER 2:    │ │Analytics│ │Retraining│
    │  REASONING  │ │Pipeline │ │Pipeline  │
    │  13B LLM    │ └────────┘ └──────────┘
    │  2-5s p99   │
    └──────┬──────┘
           │ (complex cases)
           ▼
    ┌─────────────┐
    │  TIER 3:    │
    │  TRIAGE     │
    │  Agent + Tools│
    │  5-10s p99  │
    └─────────────┘
```

### Key Metrics

| Metric                     | Value                      |
| -------------------------- | -------------------------- |
| Throughput                 | 24,500+ TPS                |
| Tier 1 p99 latency         | < 5 ms                     |
| Tier 2 p99 latency         | 2–5 s                     |
| CPU scoring time           | < 1 ms (8–20 models)      |
| CUDA Graph launch overhead | 5 µs (vs 150 µs without) |
| Arena allocation           | 0 ns (slab reset)          |
| Cross-tier serialization   | 0 (Apache Arrow zero-copy) |

### Tech Stack

| Component       | Technology                               | Why                                                         |
| --------------- | ---------------------------------------- | ----------------------------------------------------------- |
| Core language   | Rust                                     | Memory safety + zero-cost abstractions + per-core ownership |
| GPU scoring     | CUDA Graphs + pinned buffers             | Eliminate 145µs launch overhead per inference              |
| Feature store   | In-memory cache + precomputed embeddings | No online tokenization in hot path                          |
| CPU models      | XGBoost, GBDT, logistic scorecard        | <100µs per model, parallel ensemble                        |
| Inter-tier data | Apache Arrow                             | Zero-copy publish/subscribe, columnar layout                |
| LLM serving     | Sentinel/vLLM (PagedAttention)           | Continuous batching, dynamic KV cache                       |
| Ring buffer     | SPSC lock-free                           | No locks, no cross-core contention                          |

### Drill-Down Q&A

**Q: Why per-core arena allocators instead of jemalloc/mimalloc?**
A: At 24K TPS, even jemalloc's thread-local caches have occasional cross-arena migrations. A per-core slab with bulk reset (set offset=0) means zero allocation overhead and zero fragmentation. The slab is sized for worst-case request and reset after each one.

**Q: Why not send raw JSON between tiers?**
A: JSON serialization of a feature vector (512 floats + 20 scores + metadata) costs ~200µs and produces garbage for GC. Arrow wraps existing memory as columnar arrays — same bytes feed Tier 2 reasoning, analytics (Spark reads Arrow natively), and model retraining (PyTorch reads Arrow via datasets).

**Q: How do CUDA Graphs help at Tier 1?**
A: The GPU MLP has 20–50 small kernels (matmul, bias, activation). Without graphs, each kernel costs 5µs CPU launch overhead = 100–250µs. With CUDA Graph capture at startup, the entire sequence is a single graph launch = 5µs. This fits within the 5ms budget alongside CPU scoring.

**Q: What happens when a core's arena overflows?**
A: The slab is sized at startup for max request size (configurable, typically 64KB). If a request exceeds it (malformed or attack), we reject with a fast-path error — no allocation, no OOM.

**Q: How do you handle model updates without downtime?**
A: Blue-green deployment at the model layer. New model loads into a shadow CUDA Graph. Traffic switches atomically by swapping the graph_exec pointer. Old graph is freed after drain.

---

## Story B: Fiserv — AI-Powered Loan Processing Platform

### 2-Minute Summary

"At Fiserv, I architected a **multi-model LLM orchestration platform** for loan intake, underwriting, and decisioning. The system coordinates 8B classification models with 70B reasoning models, achieving **3.7× latency reduction (8.5s→2.3s)** with 99.9% availability.

The key challenge was processing borrower documents (credit bureau reports, bank statements, tax returns, pay stubs) through large-context LLM workflows while keeping latency interactive. My solution was a **hierarchical context assembly engine** — instead of stuffing all documents into one prompt, we classify documents with fast 8B models, extract structured fields, then compose a minimal context for the 70B reasoner. This reduced token usage by **60%** while improving accuracy by **15%**.

The infrastructure uses a **Rust control plane** (Axum/Tokio) with health-aware routing, GPU-headroom scheduling, and explainable multi-factor scoring across 20+ inference pods on AWS EKS. I implemented a **zero-copy GPU transfer engine** in C++/CUDA with pinned memory, async DMA, and CUDA streams, achieving **3.2× faster H2D transfers** and 85% GPU utilization.

For multi-turn loan workflows, I designed **session-aware KV-cache management** with sticky routing, achieving **70% cache hit rate** — meaning returning applicants' conversation history stays warm in GPU memory."

### Architecture Diagram

```
         Loan Application
              │
    ┌─────────▼─────────────────────────┐
    │   Rust Control Plane (Axum/Tokio) │
    │   • Health-aware routing           │
    │   • GPU-headroom scheduling        │
    │   • Session-affine sticky routing  │
    │   • Admission control + circuit    │
    │     breakers                       │
    └─────────┬─────────────────────────┘
              │
    ┌─────────▼─────────────────────────┐
    │   Hierarchical Context Assembly    │
    │                                    │
    │   8B Classifier → Document type    │
    │   Field Extractor → Structured     │
    │   Context Composer → Minimal prompt│
    │                                    │
    │   Token reduction: 60%             │
    └─────────┬─────────────────────────┘
              │ (minimal context)
    ┌─────────▼─────────────────────────┐
    │   70B Reasoning Model (vLLM)       │
    │   • PagedAttention                 │
    │   • Continuous batching            │
    │   • Session-aware KV cache (70%    │
    │     hit rate)                       │
    │   • Zero-copy GPU transfers        │
    │     (pinned mem + async DMA)       │
    └─────────┬─────────────────────────┘
              │
    ┌─────────▼─────────────────────────┐
    │   Decision + Explanation            │
    │   2.3s E2E (was 8.5s)             │
    └───────────────────────────────────┘
```

### Key Metrics

| Metric                        | Value                               |
| ----------------------------- | ----------------------------------- |
| E2E latency                   | 2.3s (3.7× improvement from 8.5s)  |
| Token reduction               | 60% via hierarchical context        |
| Accuracy improvement          | 15%                                 |
| GPU utilization               | 85%                                 |
| H2D transfer speedup          | 3.2× via pinned memory + async DMA |
| KV cache hit rate             | 70% (session-aware routing)         |
| GPU memory pressure reduction | 50%                                 |
| Availability                  | 99.9%                               |
| Concurrent applications       | 500+                                |
| Inference pods                | 20+ on AWS EKS                      |

### Tech Stack

| Component     | Technology                                 | Why                                        |
| ------------- | ------------------------------------------ | ------------------------------------------ |
| Control plane | Rust (Axum/Tokio)                          | Async, safe concurrency, low overhead      |
| GPU transfers | C++/CUDA (pinned memory, async DMA)        | Zero-copy, overlapped compute+transfer     |
| LLM serving   | vLLM (PagedAttention, continuous batching) | Dynamic memory, high throughput            |
| Orchestration | AWS EKS                                    | Managed K8s, autoscaling                   |
| Models        | 8B classifier + 70B reasoner               | Hierarchical: fast triage + deep reasoning |

### Drill-Down Q&A

**Q: Why hierarchical context instead of one large prompt?**
A: A loan package is 50–200 pages. Stuffing everything into a 70B prompt wastes tokens (cost + latency) and dilutes attention. The 8B classifier identifies document types in <100ms, field extraction pulls structured data, and the composer builds a focused prompt. The 70B model sees only what matters — better accuracy at 40% of the tokens.

**Q: How does session-aware KV cache work?**
A: Loan applications are multi-turn (applicant submits docs over days). Sticky routing sends the same applicant to the same pod. If KV cache for their session is still resident, we skip prefill for the shared context prefix — that's a 70% hit rate. On miss, we reprefill but it's still faster than cold start because the hierarchical context is compact.

**Q: What's GPU-headroom scheduling?**
A: Each pod reports available GPU memory and SM utilization via Prometheus. The control plane routes 70B requests only to pods with >20GB free KV cache headroom. This prevents OOM-driven evictions that would destroy cache hit rates.

---

## Story C: Apple Siri — Multi-Stage Conversational AI Pipeline

### 2-Minute Summary

"At Apple, I built the **server-side inference pipeline for Siri** — a multi-stage system handling **millions of concurrent sessions** with **<300ms p99 end-to-end** latency and **<50ms partial streaming** for real-time response.

The pipeline has 5 stages: **Streaming Conformer ASR → BERT NLU → Knowledge Search → Orchestration → Neural TTS**. The critical architectural challenge was that traditional microservice boundaries (gRPC between stages) added 50–80ms per hop of serialization overhead — unacceptable when your total budget is 300ms across 5 stages.

My solution: **zero-copy shared memory session state**. A single `ConversationalState` struct is mapped into all stage processes via shared memory. Each stage writes its output (ASR hypotheses, NLU intents, search results) directly to a preallocated arena within this struct. The next stage reads by pointer — no serialization, no network hop for data. Only control signals flow over gRPC.

The second innovation was **session-affine routing**: each session is pinned to a server group where its shared memory state lives. Combined with buffer pools for gRPC streaming, this eliminated all per-request memory allocation from the hot path.

**Result:** Reduced per-stage overhead from 50–80ms to 5–10ms, saving **200–350ms end-to-end** — the difference between a responsive assistant and a laggy one. E2E latency dropped from **850ms to 280ms** (3.0× improvement)."

### Architecture Diagram

```
    ┌──────────────────────────────────────────────────────────────┐
    │                    Edge Device (iPhone)                        │
    │    Audio capture → VAD → Streaming upload (opus/speex)       │
    └────────────────────────────┬─────────────────────────────────┘
                                 │ gRPC stream (audio chunks)
                                 ▼
    ┌──────────────────────────────────────────────────────────────┐
    │              Session-Affine Router                             │
    │    session_id → server_group (shared memory lives here)      │
    └────────────────────────────┬─────────────────────────────────┘
                                 │
    ┌────────────────────────────▼─────────────────────────────────┐
    │         SHARED MEMORY: ConversationalState (64KB arena)       │
    │  ┌──────────┬──────────┬───────────┬──────────┬──────────┐  │
    │  │ ASR Out  │ NLU Out  │ Search    │ Orch     │ TTS      │  │
    │  │(hypoths) │(intents) │(results)  │(decision)│(audio)   │  │
    │  └──────────┴──────────┴───────────┴──────────┴──────────┘  │
    └──────────────────────────────────────────────────────────────┘
         ▲ write      ▲ write      ▲ write      ▲ write
         │            │            │            │
    ┌────┴────┐ ┌────┴────┐ ┌────┴────┐ ┌────┴────┐ ┌──────────┐
    │ Stage A │→│ Stage B │→│ Stage C │→│ Stage D │→│ Stage E  │
    │Streaming│ │  BERT   │ │Knowledge│ │Orchestr.│ │Neural TTS│
    │Conformer│ │  NLU    │ │ Search  │ │ Engine  │ │          │
    │  ASR    │ │         │ │         │ │         │ │          │
    └─────────┘ └─────────┘ └─────────┘ └─────────┘ └──────────┘
       <50ms      <30ms       <80ms       <20ms       <100ms
  
    Total: < 300ms p99 E2E (was 850ms → 280ms actual = 3.0× improvement)
```

### Key Metrics

| Metric                          | Value                           |
| ------------------------------- | ------------------------------- |
| E2E p99 latency                 | < 300ms (280ms actual)          |
| Improvement                     | 3.0× (850ms → 280ms)          |
| Partial streaming latency       | < 50ms                          |
| Concurrent sessions             | Millions                        |
| Per-stage serialization savings | 40–70ms × 5 = 200–350ms      |
| Shared memory access time       | 5–10ms per stage (vs 50–80ms) |
| Buffer pool allocation          | 0 per-request allocs            |

### Tech Stack

| Component | Technology                         | Why                                         |
| --------- | ---------------------------------- | ------------------------------------------- |
| Languages | C++ / Python                       | C++ for hot path, Python for model training |
| IPC       | Shared memory (mmap)               | Zero-copy cross-stage data                  |
| Network   | gRPC streaming (zero-copy buffers) | Control signals only; buffer pools          |
| ASR       | Streaming Conformer                | Real-time, partial hypotheses               |
| NLU       | BERT                               | Intent + entity extraction                  |
| TTS       | Neural TTS (FastSpeech-variant)    | Natural speech synthesis                    |
| Routing   | Session-affine                     | Keep shared memory warm                     |

### Drill-Down Q&A

**Q: Why shared memory instead of a message queue (Kafka, Redis)?**
A: At millions of concurrent sessions, a message queue adds network RTT + serialization + deserialization per hop. Shared memory is mapped once at session start; subsequent stage reads are a pointer dereference (nanoseconds). The 5-stage pipeline saves 200–350ms of serialization — that's the entire latency budget.

**Q: How do you handle stage failures with shared memory?**
A: Each stage writes atomically (stage_mask bitfield). If a stage crashes, the orchestrator detects missing bit in stage_mask within the deadline_ns window and either retries the stage or routes to a fallback. The shared memory is never corrupted because writes are append-only to the arena.

**Q: Why session-affine routing?**
A: The ConversationalState struct is 64KB. Moving it per request across servers would cost the very serialization we're avoiding. Sticky routing means the state stays mapped in the same server group for the session lifetime (seconds to minutes).

**Q: How do buffer pools prevent allocation in the gRPC path?**
A: At startup, we preallocate N buffers (64KB each) into a free list. gRPC handlers acquire from the pool (lock + pop = ~100ns), use the buffer for the request lifetime, then release back. No malloc/free in the hot path.

---

## Story D: Broadcom Cloud SWG — Multi-Model AI Security Platform

### 2-Minute Summary

"At Broadcom, I built the **Unified AI Inference Platform** for Cloud Secure Web Gateway — a system inspecting **2+ million web requests per second** globally with **6 ML models in parallel** achieving **<100ms p99** for the complete multi-model pipeline. This protected **10,000+ enterprise customers** with 99.99% availability across hybrid infrastructure (on-premise + GCP).

The core challenge: six independent security teams had built separate ML pipelines with different frameworks (TensorFlow, scikit-learn, custom C++), causing 350ms p99 latency and operational chaos. Each request was re-serialized through 6+ services.

My solution: **Treat multi-model inference as a unified pipeline with shared state.** I built a `RequestContext` struct (cacheline-aligned, 1MB arena) that's shared across all models — URL classification (BERT + GBRT), malware detection (XGBoost + CNN), DLP (NER + patterns), content analysis (NLP + CV), behavioral analysis (time-series), and threat intelligence (graph). All models read from the same zero-copy context. Models execute in parallel where dependencies allow, with early-exit on high-confidence threats.

Model serving uses TensorFlow Serving for deep learning, ONNX Runtime for cross-framework compatibility, custom C++ inference for tree models (XGBoost, GBRT), with GPU acceleration (T4, V100, A100) and CPU optimization (AVX2, AVX-512).

**Result:** Reduced p99 from 350ms to 85ms (**4.1× improvement**), achieved 99.99% availability, and enabled 10× throughput improvement via dynamic batching (1K→10K req/sec/GPU)."

### Architecture Diagram

```
    ┌─────────────────────────────────────────────────────────────┐
    │   Edge: Cloud SWG Agent (On-Premise)                        │
    │   • SSL/TLS decryption • Local caching • Preprocessing     │
    └───────────────────────────┬─────────────────────────────────┘
                                │ Encrypted gRPC
                                ▼
    ┌─────────────────────────────────────────────────────────────┐
    │   Ingress: Tenant-Aware Routing + Request Batching          │
    └───────────────────────────┬─────────────────────────────────┘
                                │
    ┌───────────────────────────▼─────────────────────────────────┐
    │   SHARED CONTEXT: RequestContext (1MB arena, zero-copy)      │
    │   • URL features (domain, path, lexical risk, category)     │
    │   • Content features (entropy, script count, iframes)       │
    │   • Session context (request patterns, anomaly score)       │
    │   • Model outputs (written by each model)                   │
    └───────────────────────────┬─────────────────────────────────┘
         │         │         │         │         │         │
    ┌────▼───┐┌───▼────┐┌───▼───┐┌───▼────┐┌───▼────┐┌───▼────┐
    │Malware ││  URL   ││  DLP  ││Content ││Behav.  ││Threat  │
    │Detect  ││ Class  ││Engine ││Analysis││Analysis││Intel   │
    │XGB+CNN ││BERT+GBT││NER+Pat││NLP+CV  ││TimeSer.││Graph   │
    │ <20ms  ││ <15ms  ││ <25ms ││ <30ms  ││ <10ms  ││ <5ms   │
    └────────┘└────────┘└───────┘└────────┘└────────┘└────────┘
         │ PARALLEL EXECUTION (early-exit on high-confidence)
         ▼
    ┌─────────────────────────────────────────────────────────────┐
    │   Decision Engine: Risk Aggregation + Policy Enforcement     │
    │   → ALLOW | BLOCK | WARN | QUARANTINE                       │
    └─────────────────────────────────────────────────────────────┘
  
    Total: < 100ms p99 (was 350ms → 85ms = 4.1× improvement)
```

### Key Metrics

| Metric                  | Value                                   |
| ----------------------- | --------------------------------------- |
| Global throughput       | 2M+ requests/sec                        |
| ML inference p99        | < 100ms (85ms actual)                   |
| Improvement             | 4.1× (350ms → 85ms)                   |
| Throughput per GPU      | 10K req/sec (10× via dynamic batching) |
| Availability            | 99.99%                                  |
| Enterprise customers    | 10,000+                                 |
| Concurrent users/tenant | 50,000+                                 |
| ML models per request   | 6 (parallel)                            |
| Compliance              | SOC2, PCI-DSS, HIPAA, GDPR, FedRAMP     |

### Tech Stack

| Component     | Technology                       | Why                                          |
| ------------- | -------------------------------- | -------------------------------------------- |
| Languages     | C++, Python                      | C++ for hot path + inference engines         |
| DL Serving    | TensorFlow Serving, ONNX Runtime | Cross-framework, GPU-accelerated             |
| Tree models   | Custom C++ (XGBoost, GBRT)       | Lowest overhead for tabular models           |
| GPU           | NVIDIA T4, V100, A100            | Progressive upgrade path                     |
| CPU opt.      | AVX2, AVX-512                    | SIMD for tree traversal + feature extraction |
| Cloud         | GCP + On-Premise hybrid          | Multi-tenant SaaS + customer deployments     |
| Orchestration | Kubernetes                       | Multi-cluster, multi-region                  |
| Batching      | Dynamic batching                 | 10× throughput (request accumulation)       |

### Drill-Down Q&A

**Q: How do 6 models share a RequestContext without contention?**
A: Models write to disjoint fields (`model_outputs.malware_score`, `model_outputs.phishing_score`, etc.). Reads are shared (URL features, content). The struct is cacheline-aligned per section to prevent false sharing. No mutex needed — single-writer per field, multiple-reader for features.

**Q: How does early-exit work?**
A: If the malware detector returns >0.99 confidence within 5ms, we short-circuit — skip DLP/content analysis and immediately block. The `model_mask` bitfield tracks which models have completed. Decision engine can act as soon as a high-confidence threat is detected.

**Q: Why custom C++ inference for XGBoost instead of using the XGBoost library?**
A: The official XGBoost library has Python bindings overhead and isn't optimized for single-request latency. Our C++ engine traverses trees with AVX2 SIMD (8 trees in parallel), uses branch-free comparison, and avoids the library's batch-oriented API. Result: <5ms for 500-tree ensemble vs 20ms with the library.

**Q: How did you achieve 99.99% availability?**
A: Multi-region active-active deployment. Each request is served by the nearest region with automatic failover. Model serving uses rolling blue-green deployments (never more than 25% of capacity upgrading at once). Circuit breakers on each model with fallback to rule-based scoring if ML is unavailable.

---

## Story Summary Comparison Table

| Dimension                   | CapitalOne                   | Fiserv                          | Apple Siri             | Broadcom SWG                |
| --------------------------- | ---------------------------- | ------------------------------- | ---------------------- | --------------------------- |
| **Domain**            | Fraud detection              | Loan processing                 | Conversational AI      | Web security                |
| **Scale**             | 24,500 TPS                   | 500+ concurrent                 | Millions sessions      | 2M+ req/sec                 |
| **Latency**           | <5ms p99 (T1)                | 2.3s E2E                        | <300ms p99             | <100ms p99                  |
| **Key Innovation**    | Per-core arena + CUDA Graphs | Hierarchical context + KV cache | Shared memory pipeline | Unified multi-model context |
| **Zero-Copy Pattern** | Arrow event buffer           | Pinned GPU memory               | mmap shared state      | Shared RequestContext       |
| **Languages**         | Rust + C++/CUDA              | Rust + C++/CUDA                 | C++ + Python           | C++ + Python                |
| **GPU Use**           | MLP scoring (CUDA Graphs)    | 70B LLM inference               | TTS + optional         | Multi-model (T4/V100/A100)  |
| **ML Models**         | XGBoost/GBDT ensemble        | 8B + 70B LLMs                   | Conformer/BERT/TTS     | 6 models (CNN/BERT/XGB/NER) |

---

# SECTION 2: TECHNOLOGY DECISION MATRIX

> Every technology you've used, mapped to which story, WHY you chose it, and what alternatives you rejected.

---

## Core Languages

| Technology            | Stories            | WHY                                                                                                                        | Alternatives Considered | Why Not                                                                                                 |
| --------------------- | ------------------ | -------------------------------------------------------------------------------------------------------------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------- |
| **Rust**        | CapitalOne, Fiserv | Memory safety without GC; zero-cost abstractions; per-core ownership model; arena allocators;`repr(C)` for FFI with CUDA | Go, C++                 | Go has GC pauses (unacceptable at 5ms budget); C++ lacks ownership model (use-after-free risk at scale) |
| **C++ (17/20)** | All four stories   | Maximum control over memory layout; CUDA interop; SIMD intrinsics (AVX2/AVX-512); established in ML inference ecosystems   | Rust                    | Legacy codebases; TensorFlow/ONNX runtime are C++; CUDA SDK is C/C++ native                             |
| **Python**      | All four stories   | Model training; rapid prototyping; ML ecosystem (PyTorch, TF, scikit-learn)                                                | Julia                   | Ecosystem maturity; team expertise; tooling                                                             |
| **Go**          | Control planes     | gRPC services; Kubernetes operators; simple concurrency model                                                              | Rust                    | Faster development for non-latency-critical paths                                                       |

## GPU & Inference

| Technology                            | Stories                 | WHY                                                                                      | Alternatives    | Why Not                                                                 |
| ------------------------------------- | ----------------------- | ---------------------------------------------------------------------------------------- | --------------- | ----------------------------------------------------------------------- |
| **CUDA Graphs**                 | CapitalOne, Fiserv      | Eliminate per-kernel launch overhead (5µs × 30 kernels = 150µs → 5µs single launch) | Eager execution | 145µs savings critical when total budget is 5ms                        |
| **vLLM (PagedAttention)**       | CapitalOne T2, Fiserv   | Dynamic KV cache allocation; continuous batching; high GPU utilization for LLMs          | TensorRT-LLM    | Variable sequence lengths cause TRT graph recompilation (Task 8 lesson) |
| **TensorRT-LLM**                | Broadcom (inference)    | Compiled execution; kernel fusion; optimal for fixed-shape workloads                     | vLLM            | Better throughput for predictable request shapes                        |
| **TensorFlow Serving**          | Broadcom                | Mature serving infrastructure; GPU batching; model versioning                            | Triton          | Already deployed; proven at scale                                       |
| **ONNX Runtime**                | Broadcom                | Cross-framework model compatibility (export from TF/PyTorch/sklearn)                     | Custom          | Standardized format; hardware-agnostic optimization                     |
| **Triton Inference Server**     | Guardrail integration   | Ensemble pipelines (DAG); GPU-resident data flow; multi-model orchestration              | Custom serving  | NVIDIA-supported; eliminates inter-model serialization                  |
| **NVIDIA DCGM/Nsight**          | All stories             | GPU utilization monitoring; kernel profiling; memory debugging                           | Custom metrics  | Industry standard; ncu/nsys reveal kernel-level bottlenecks             |
| **Pinned (page-locked) memory** | CapitalOne, Fiserv      | Enables async DMA; prevents page faults during H2D transfer; 3.2× faster transfers      | Pageable memory | Pageable requires extra CPU-side copy by CUDA driver                    |
| **FP8/FP16/BF16 quantization**  | Fiserv, profiling tasks | 2× throughput; reduced GPU memory; but precision tradeoffs                              | Full FP32       | Memory savings enable larger batch sizes / more concurrent requests     |

## Zero-Copy & Data Flow

| Technology                     | Stories           | WHY                                                                                                              | Alternatives         | Why Not                                                                                     |
| ------------------------------ | ----------------- | ---------------------------------------------------------------------------------------------------------------- | -------------------- | ------------------------------------------------------------------------------------------- |
| **Apache Arrow**         | CapitalOne        | Columnar zero-copy across tier boundaries; same bytes feed analytics (Spark), ML (PyTorch), LLM (Arrow→tensors) | Protobuf, JSON, Avro | Protobuf/JSON require ser/deser; Avro is row-oriented; Arrow is zero-copy by design         |
| **Shared memory (mmap)** | Apple Siri        | Sub-microsecond cross-stage access; no network hop; natural for co-located pipeline stages                       | Message queues       | Kafka/Redis add network RTT + serialization per hop; unacceptable at 5-stage pipeline scale |
| **gRPC streaming**       | Apple, CapitalOne | Low-overhead RPC; bidirectional streaming; HTTP/2 multiplexing                                                   | REST, Thrift         | REST lacks streaming; Thrift less ecosystem support; gRPC has native zero-copy extensions   |
| **SPSC ring buffers**    | CapitalOne        | Lock-free single-producer/single-consumer; no atomics contention; per-core ownership                             | MPMC queues          | MPMC requires CAS loops (contention at 24K TPS); SPSC is wait-free                          |
| **Buffer pools**         | Apple Siri        | Eliminate per-request malloc/free in hot path; preallocated free list                                            | Per-request alloc    | malloc is 100–500ns; pool acquire is ~50ns; at millions of sessions, adds up               |

## Platform & Infrastructure

| Technology                     | Stories              | WHY                                                                             | Alternatives       | Why Not                                                                         |
| ------------------------------ | -------------------- | ------------------------------------------------------------------------------- | ------------------ | ------------------------------------------------------------------------------- |
| **Kubernetes (EKS/GKE)** | All stories          | Container orchestration; autoscaling; rolling deployments; multi-cluster        | Bare metal         | Operational overhead of bare metal at scale; K8s provides declarative scaling   |
| **Helm + ArgoCD**        | All stories          | GitOps deployment; version-controlled config; automated rollbacks               | Manual kubectl     | Audit trail; reproducibility; team velocity                                     |
| **Prometheus + Grafana** | All stories          | Metrics collection; SLO/SLI dashboards; alerting; GPU metrics via DCGM exporter | Datadog, New Relic | Self-hosted (compliance); native K8s integration; no per-host licensing         |
| **Redis**                | CapitalOne, Broadcom | Feature cache; session state; sub-millisecond reads; pub/sub                    | Memcached          | Redis has richer data structures; persistence options; pub/sub for invalidation |
| **Kafka**                | CapitalOne (T2/T3)   | Event streaming between tiers; replay capability; exactly-once semantics        | RabbitMQ, Pulsar   | Battle-tested at financial scale; exactly-once for compliance; ecosystem        |
| **Terraform/Pulumi**     | All stories          | Infrastructure as code; multi-cloud; state management                           | CloudFormation     | Multi-cloud support (AWS + GCP); HCL is team-familiar                           |

## ML Frameworks & Models

| Technology                    | Stories                  | WHY                                                                      | Alternatives              | Why Not                                                                                       |
| ----------------------------- | ------------------------ | ------------------------------------------------------------------------ | ------------------------- | --------------------------------------------------------------------------------------------- |
| **XGBoost / GBDT**      | CapitalOne, Broadcom     | <100µs inference for tabular data; interpretable; regulatory compliance | Deep learning             | Trees are faster, more interpretable for fraud/security; regulators require explainability    |
| **BERT / DistilBERT**   | Broadcom, Apple          | NLU intent/entity extraction; URL classification                         | GPT models                | BERT is bidirectional (better for classification); DistilBERT is 2× faster with 95% accuracy |
| **Streaming Conformer** | Apple Siri               | Real-time ASR with partial hypotheses; streaming-native architecture     | Whisper                   | Whisper is batch-only; Conformer streams partials as audio arrives                            |
| **LLMs (8B–70B)**      | CapitalOne T2/T3, Fiserv | Complex reasoning; document understanding; explanation generation        | Fine-tuned smaller models | 70B quality needed for loan underwriting; 8B sufficient for classification                    |

---

# SECTION 3: SYSTEM LAYERS REFERENCE

> Five layers from application to hardware, with key concepts and interview talking points at each.

---

## Layer 1: Application Layer

### Patterns Across Stories

| Pattern                       | CapitalOne                   | Fiserv                        | Apple                          | Broadcom                 |
| ----------------------------- | ---------------------------- | ----------------------------- | ------------------------------ | ------------------------ |
| **Request processing**  | Per-core worker, arena alloc | Hierarchical context assembly | Session-affine routing         | Tenant-aware routing     |
| **Latency strategy**    | Tiered SLOs (5ms/5s/10s)     | Token reduction (60%)         | Shared memory eliminates hops  | Early exit on confidence |
| **Data representation** | TxnView + FeatureBlock       | Document → structured fields | ConversationalState struct     | RequestContext struct    |
| **Concurrency model**   | Shared-nothing per-core      | Async Tokio tasks             | Process-per-stage + shared mem | Thread pool per model    |

### Key Concepts

- **Tiered Architecture:** Separate fast-path (milliseconds) from slow-path (seconds). Never let reasoning block decisioning.
- **Zero-Copy Data Representation:** Design struct layouts at system design time. `repr(C, align(64))` in Rust, `__attribute__((aligned(64)))` in C++.
- **Arena/Slab Allocation:** Preallocate worst-case memory per request. Reset (offset=0) instead of free. Eliminates allocator contention.
- **Feature Blocks:** One cacheline-aligned struct feeds all models. No per-model data conversion.

---

## Layer 2: Runtime Layer

### Patterns Across Stories

| Pattern                     | CapitalOne            | Fiserv                    | Apple                     | Broadcom                  |
| --------------------------- | --------------------- | ------------------------- | ------------------------- | ------------------------- |
| **Memory management** | Per-core slab + arena | KV cache + pinned buffers | Buffer pools + shared mem | Arena per request context |
| **Scheduling**        | SPSC ring → per-core | GPU-headroom scheduling   | Session-affine + pipeline | Dynamic batching          |
| **GPU interaction**   | CUDA Graph (MLP)      | Zero-copy DMA streams     | Optional (TTS)            | TF Serving / ONNX RT      |
| **Async model**       | Tokio (Rust async)    | Tokio (Axum server)       | epoll + process pipeline  | Event loop + thread pool  |

### Key Concepts

- **CUDA Graphs:** Capture a static kernel sequence once, replay with single API call. Eliminates CPU launch overhead. Critical when kernel count is high but each kernel is small.
- **Pinned Memory:** Page-locked host memory enables async DMA (cudaMemcpyAsync). Without pinning, driver must stage through internal buffer.
- **PagedAttention (vLLM):** KV cache managed like virtual memory pages. Allocate on demand, not worst-case. Enables 2–4× more concurrent requests.
- **Dynamic Batching:** Accumulate requests for N ms, batch to GPU. Throughput vs latency tradeoff (higher batch = better GPU utilization, worse tail latency).
- **Continuous Batching:** Don't wait for full batch. Insert new requests into running batch as slots free up. vLLM/TRT-LLM implement this.

---

## Layer 3: Platform Layer (Kubernetes / Orchestration)

### Patterns Across Stories

| Pattern                     | Implementation                                                                                                 |
| --------------------------- | -------------------------------------------------------------------------------------------------------------- |
| **GPU scheduling**    | GPU-headroom aware routing; pod reports free memory via Prometheus; control plane routes to pods with capacity |
| **Autoscaling**       | HPA on GPU utilization + request queue depth; not just CPU/memory                                              |
| **Deployment**        | Blue-green for models; rolling for control plane; canary for traffic shifts                                    |
| **Multi-tenancy**     | Namespace isolation (Broadcom); resource quotas; network policies                                              |
| **Health checking**   | GPU health (DCGM), model health (inference latency), pod health (liveness/readiness)                           |
| **Admission control** | Bounded queues + circuit breakers; reject early rather than queue indefinitely                                 |

### Key Concepts

- **Device Plugin:** How K8s exposes GPUs to pods. `nvidia.com/gpu` resource request. NVIDIA device plugin manages allocation.
- **Topology-Aware Scheduling:** Place TP=4 workloads on nodes with full NVLink mesh. Avoid PCIe-only pairs (Task 4 lesson).
- **Pod Disruption Budgets:** Ensure rolling updates don't drop below minimum replicas for serving SLOs.
- **Service Mesh (optional):** Istio/Linkerd for mTLS, traffic splitting, observability. Adds 1–2ms latency — acceptable for non-ultra-low-latency paths.

---

## Layer 4: Host / OS Layer

### Key Tuning Areas

| Area                             | What to Tune                                               | Why                                                         |
| -------------------------------- | ---------------------------------------------------------- | ----------------------------------------------------------- |
| **NUMA**                   | Pin processes to NUMA node local to GPU (numactl, taskset) | Cross-NUMA memory access is 2× slower                      |
| **Huge Pages**             | Enable 2MB/1GB huge pages for GPU memory-mapped buffers    | Reduces TLB misses for large allocations                    |
| **CPU Isolation**          | isolcpus for latency-critical workers                      | Prevents kernel scheduler from preempting hot-path cores    |
| **IRQ Affinity**           | Pin NIC interrupts to non-compute cores                    | Prevents network interrupts from disturbing model inference |
| **CFS Bandwidth**          | Disable CFS throttling for RT workloads                    | CFS can throttle CPU-bound inference unexpectedly           |
| **Transparent Huge Pages** | Disable THP (or set to `madvise`)                        | THP compaction causes latency spikes                        |
| **vm.swappiness**          | Set to 0                                                   | Prevent swapping GPU page-locked memory                     |
| **Network**                | TCP tuning (rmem/wmem), busy polling, XDP                  | Reduce network stack overhead for gRPC                      |

### Relevant Across Stories

- **CapitalOne:** isolcpus for per-core workers; NUMA-local GPU for CUDA Graph scorer
- **Apple:** IRQ affinity + CPU isolation for ASR streaming pipeline (real-time audio)
- **Broadcom:** Huge pages for 1MB request context arenas; NUMA for multi-GPU inference
- **Fiserv:** NUMA pinning for GPU pods; CFS tuning for predictable latency

---

## Layer 5: Code / Implementation Layer

### Patterns Across Stories

| Pattern                   | Rust (CapitalOne/Fiserv)                        | C++ (Apple/Broadcom)                        |
| ------------------------- | ----------------------------------------------- | ------------------------------------------- |
| **Memory layout**   | `#[repr(C, align(64))]`                       | `__attribute__((aligned(64)))`            |
| **Zero-copy view**  | `&'a [u8]` lifetime-bound slice               | `const uint8_t*` with manual lifetime     |
| **Arena allocator** | Custom slab with offset reset                   | Custom arena with placement new             |
| **Lock-free queue** | `AtomicUsize` + `Ordering::Release/Acquire` | `std::atomic<size_t>` + memory orders     |
| **SIMD**            | (via C FFI)                                     | AVX2/AVX-512 intrinsics for tree traversal  |
| **Error handling**  | `Result<T, E>` — no panics in hot path       | Error codes (no exceptions in hot path)     |
| **Unsafe boundary** | `unsafe { }` blocks for FFI, raw ptrs         | All code is "unsafe" — discipline required |

---

# SECTION 4: GPU & CUDA REFERENCE

> Architecture, memory hierarchy, precision formats, LLM inference cycle, profiling workflow, CUDA Graphs, and 8 practical debugging tasks.

---

## GPU Architecture Quick Reference

### NVIDIA GPU Generations (Relevant to Stories)

| Generation | GPU  | Key Feature                   | Story Usage                         |
| ---------- | ---- | ----------------------------- | ----------------------------------- |
| Turing     | T4   | INT8 Tensor Cores, 16GB       | Broadcom (inference at scale)       |
| Ampere     | A100 | TF32, 80GB HBM2e, NVLink 3    | Broadcom (high-end inference)       |
| Hopper     | H100 | FP8, 80GB HBM3, NVLink 4, TMA | Fiserv, CapitalOne T2 (LLM serving) |

### GPU Execution Model

```
┌─────────────────────────────────────────────────────────────┐
│                        GPU (e.g., H100)                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐     ┌──────────┐  │
│  │   SM 0   │ │   SM 1   │ │   SM 2   │ ... │  SM 131  │  │
│  │  ┌─────┐ │ │  ┌─────┐ │ │  ┌─────┐ │     │  ┌─────┐ │  │
│  │  │Warp0│ │ │  │Warp0│ │ │  │Warp0│ │     │  │Warp0│ │  │
│  │  │Warp1│ │ │  │Warp1│ │ │  │Warp1│ │     │  │Warp1│ │  │
│  │  │ ... │ │ │  │ ... │ │ │  │ ... │ │     │  │ ... │ │  │
│  │  │WarpN│ │ │  │WarpN│ │ │  │WarpN│ │     │  │WarpN│ │  │
│  │  └─────┘ │ │  └─────┘ │ │  └─────┘ │     │  └─────┘ │  │
│  │  Shared  │ │  Shared  │ │  Shared  │     │  Shared  │  │
│  │  Memory  │ │  Memory  │ │  Memory  │     │  Memory  │  │
│  │  (228KB) │ │  (228KB) │ │  (228KB) │     │  (228KB) │  │
│  └──────────┘ └──────────┘ └──────────┘     └──────────┘  │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              L2 Cache (50 MB on H100)                │   │
│  └─────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              HBM3 (80 GB, 3.35 TB/s on H100)        │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Memory Hierarchy

| Level         | Size (H100)        | Bandwidth                            | Latency     | Use Case                                     |
| ------------- | ------------------ | ------------------------------------ | ----------- | -------------------------------------------- |
| Registers     | 256KB/SM           | —                                   | 0 cycles    | Thread-local variables                       |
| Shared Memory | 228KB/SM           | ~19 TB/s                             | ~20 cycles  | Block-level cooperation (reductions, tiling) |
| L1 Cache      | Combined w/ shared | ~19 TB/s                             | ~30 cycles  | Automatic caching                            |
| L2 Cache      | 50 MB              | ~12 TB/s                             | ~200 cycles | Cross-SM data sharing                        |
| HBM (Global)  | 80 GB              | 3.35 TB/s                            | ~400 cycles | Model weights, KV cache, activations         |
| Host (CPU)    | TB-scale           | 50 GB/s (PCIe5) / 900 GB/s (NVLink4) | µs-scale   | Input data, orchestration                    |

### Key Concepts for Interviews

- **Warp (32 threads):** Execution unit. All threads in warp execute same instruction (SIMT). Divergent branches serialize.
- **Occupancy:** Ratio of active warps to max warps per SM. Higher occupancy hides memory latency but isn't always optimal (register pressure tradeoff).
- **Coalescing:** Adjacent threads should access adjacent memory addresses. Coalesced access = single memory transaction. Strided access = multiple transactions (bandwidth waste).
- **Bank conflicts:** Shared memory has 32 banks. If multiple threads access same bank, accesses serialize. Pad arrays to avoid.

---

## Precision Formats

| Format   | Bits | Exponent | Mantissa | Dynamic Range | Use Case                                            |
| -------- | ---- | -------- | -------- | ------------- | --------------------------------------------------- |
| FP32     | 32   | 8        | 23       | ±3.4×10³⁸ | Training, reference inference                       |
| TF32     | 19   | 8        | 10       | ±3.4×10³⁸ | A100+ training (same range as FP32, less precision) |
| BF16     | 16   | 8        | 7        | ±3.4×10³⁸ | Training & inference (same range as FP32)           |
| FP16     | 16   | 5        | 10       | ±65504       | Inference (more precise than BF16, less range)      |
| FP8 E4M3 | 8    | 4        | 3        | ±448         | H100 inference (weights)                            |
| FP8 E5M2 | 8    | 5        | 2        | ±57344       | H100 inference (activations/gradients, more range)  |
| INT8     | 8    | —       | —       | -128 to 127   | Quantized inference (Turing+)                       |
| INT4     | 4    | —       | —       | -8 to 7       | Aggressive quantization (GPTQ, AWQ)                 |

### Precision Tradeoffs (from Task 5)

- FP8 E4M3: Only 3-bit mantissa → softmax accumulation loses precision at long sequences (>2048 tokens)
- Fix: Keep attention computation in BF16 (mixed precision), use FP8 only for weights/linear layers
- Rule: **Reductions (softmax, LayerNorm) always need higher precision accumulators**

---

## LLM Inference Cycle

### Two Phases

```
┌────────────────────────────────┐    ┌────────────────────────────────┐
│         PREFILL PHASE          │    │         DECODE PHASE           │
│                                │    │                                │
│  Input: Full prompt tokens     │    │  Input: 1 token at a time     │
│  Compute: O(T²) attention     │    │  Compute: O(T) per step       │
│  Bound by: COMPUTE (FLOPS)    │    │  Bound by: MEMORY BANDWIDTH   │
│  Produces: KV cache for all T │    │  Produces: 1 token per step   │
│  Metric: TTFT                 │    │  Metric: TPOT (inter-token)   │
│  Parallelism: High (all T)    │    │  Parallelism: Low (1 token)   │
└────────────────────────────────┘    └────────────────────────────────┘
```

### Key Metrics

| Metric                           | Definition                                 | Target (CapitalOne T2) |
| -------------------------------- | ------------------------------------------ | ---------------------- |
| TTFT (Time to First Token)       | Prompt ingestion + first output token      | < 500ms                |
| TPOT (Time Per Output Token)     | Time between consecutive output tokens     | < 20ms                 |
| ITL (Inter-Token Latency)        | Same as TPOT at P99                        | < 30ms                 |
| Throughput (tokens/sec)          | Total tokens generated across all requests | Maximize               |
| TPS (Tokens Per Second per user) | User-perceived generation speed            | > 50                   |

### Why Decode is Memory-Bound

```
Each decode step loads:
  Model weights: ~16 GB (Llama 8B, FP16)
  KV cache: grows with sequence length
  
For batch_size=1:
  FLOPS needed: ~16 GFLOPS (tiny)
  Bytes loaded: ~16 GB (entire model)
  
  Arithmetic Intensity = FLOPS / Bytes = 16G / 16G = 1 FLOP/byte
  
  H100 has: 1979 TFLOPS compute, 3.35 TB/s bandwidth
  Compute limit: 1979 TFLOPS / 16 GFLOPS = 123,000 steps/sec
  Memory limit: 3.35 TB/s / 16 GB = 209 steps/sec  ← BOTTLENECK
  
  → Decode is 600× more memory-bound than compute-bound at batch=1
  → Fix: Increase batch size (amortize weight loading across requests)
```

---

## Profiling Workflow

### Tools

| Tool                            | What It Shows                                                         | When to Use                       |
| ------------------------------- | --------------------------------------------------------------------- | --------------------------------- |
| **nsys (Nsight Systems)** | Timeline: kernels, memory copies, CPU activity, NCCL, gaps            | First pass — see the big picture |
| **ncu (Nsight Compute)**  | Per-kernel metrics: occupancy, memory throughput, compute utilization | Drill into specific slow kernel   |
| **nvidia-smi**            | Real-time: utilization, memory, temperature, power                    | Quick health check                |
| **nvidia-smi topo -m**    | NVLink/PCIe topology between GPUs                                     | Multi-GPU debugging (Task 4)      |
| **DCGM**                  | Prometheus-exportable GPU metrics at scale                            | Production monitoring             |

### Profiling Decision Tree

```
Symptom: Latency too high
    │
    ├── Run nsys → Look at timeline
    │       │
    │       ├── Big gaps between kernels? → CPU overhead / launch latency
    │       │       → Fix: CUDA Graphs, kernel fusion, reduce Python
    │       │
    │       ├── Kernel itself is slow? → Run ncu on that kernel
    │       │       │
    │       │       ├── Low occupancy? → Register pressure / shared mem
    │       │       ├── Low memory throughput? → Uncoalesced access
    │       │       └── Low compute utilization? → Memory-bound
    │       │
    │       ├── Long AllReduce? → NVLink topology issue
    │       │       → Fix: TP=2 on NVLink pair, or get full mesh
    │       │
    │       └── Prefill interrupting decode? → Scheduling issue
    │               → Fix: Chunked prefill, disaggregation
    │
    ├── OOM earlier than expected? → Memory accounting
    │       → Check: max_model_len × KV cache size vs actual usage
    │       → Fix: Reduce max_model_len, FP8 KV cache, prefix caching
    │
    └── Inconsistent latency (P99 >> P50)? → Jitter analysis
            → Check nsys for: GC pauses, prefill interrupts, thermal throttle
            → Fix: Chunked prefill, isolcpus, disable THP
```

---

## CUDA Graphs — Deep Dive

### What They Are

```
WITHOUT CUDA Graphs:
  CPU: launch_kernel_1 → launch_kernel_2 → ... → launch_kernel_30
  GPU: [  kernel_1  ][gap][  kernel_2  ][gap]...[  kernel_30  ]
  
  CPU overhead: 30 × 5µs = 150µs
  GPU idle in gaps: waiting for next launch

WITH CUDA Graphs:
  CPU: launch_graph (single call)
  GPU: [kernel_1][kernel_2]...[kernel_30]  ← back-to-back, no gaps
  
  CPU overhead: 1 × 5µs = 5µs
  Savings: 145µs per inference
```

### When to Use (from stories)

| Scenario                         | Use CUDA Graphs?        | Why                                                       |
| -------------------------------- | ----------------------- | --------------------------------------------------------- |
| CapitalOne GPU MLP (fixed batch) | ✅ Yes                  | Static shapes, 30 small kernels, 5ms budget               |
| vLLM decode phase                | ✅ Yes (vLLM does this) | Fixed batch size per decode step                          |
| vLLM prefill phase               | ❌ No                   | Variable sequence length → graph rebuild                 |
| TensorRT-LLM (fixed shapes)      | ✅ Yes                  | Compiled engine captures full graph                       |
| Variable-length fraud workload   | ⚠️ Bucket shapes      | Task 8 lesson: unbounded shapes → constant recompilation |

### Constraints

- **Static shapes:** All tensor dimensions must be known at capture time
- **Static memory:** Cannot allocate during graph execution
- **No CPU logic:** Cannot have conditionals (if/else) inside graph
- **No synchronization:** Cannot call cudaDeviceSynchronize inside graph
- **Update limitation:** Can update kernel parameters but not topology

### Code Pattern (from CapitalOne)

```rust
// Capture once at startup
let graph = CudaGraph::capture(stream, || {
    cudaMemcpyAsync(d_input, h_input, size, H2D, stream);  // H2D
    model.forward(d_input, d_output, stream);               // 20-50 kernels
    cudaMemcpyAsync(h_output, d_output, size, D2H, stream); // D2H
});
let graph_exec = graph.instantiate();

// Execute per-request (single API call)
cudaGraphLaunch(graph_exec, stream);
stream.synchronize();
```

---

## 8 Practical Profiling Tasks (Summary)

| # | Symptom                                      | Root Cause                                            | CUDA Knowledge Applied                           | Fix                                                  |
| - | -------------------------------------------- | ----------------------------------------------------- | ------------------------------------------------ | ---------------------------------------------------- |
| 1 | TTFT spike after longer prompts              | Attention is O(T²), compute-bound prefill            | Compute vs memory bound                          | Chunked prefill, prompt design                       |
| 2 | CUDA Graphs hurt throughput at high batch    | Graphs allocate max memory upfront, reducing KV cache | Static memory constraint                         | Memory budget tuning, disable at high batch          |
| 3 | Tiny kernel storms in decode (45ms overhead) | 6 small Python ops × 500 steps = launch overhead     | Kernel launch overhead (~5-15µs each)           | Fusion, torch.compile, graph capture, CPU offload    |
| 4 | TP=4 only 2.1× speedup (expected ~4×)      | Cross-pair AllReduce via PCIe, not NVLink             | AllReduce cost, NVLink topology                  | TP=2 on NVLink pair, or full-mesh node               |
| 5 | FP8 garbage output on long prompts           | Softmax accumulation loses precision in FP8 E4M3      | Reduction precision at long sequences            | Mixed precision: attention in BF16, weights in FP8   |
| 6 | P99 = 3× P50 latency (58ms vs 18ms)         | Prefill interrupts decode on same GPU                 | GPU can only run one thing per SM                | Chunked prefill, disaggregation, priority scheduling |
| 7 | KV cache OOM at 38 requests (expected 64)    | max_model_len allocated per request (avg usage 20%)   | GPU memory layout, PagedAttention                | Reduce max_model_len, FP8 KV cache, prefix caching   |
| 8 | TensorRT-LLM slower than vLLM                | Variable sequence lengths cause graph recompilation   | CUDA Graph / TRT compilation needs static shapes | Shape bucketing, route variable traffic to vLLM      |

### One-Liner Takeaway

> A system engineer's practical CUDA work in LLM serving is NOT writing kernels — it's using Nsight to diagnose latency/throughput/memory issues, understanding GPU execution constraints, and applying fixes at the configuration/scheduling/topology level.

---

## Guardrail + LLM Integration Patterns

### The Problem

```
Traditional pipeline:
  Request → Python Input Guardrail → Python Tokenizer → GPU LLM →
  Python Detokenizer → Python Output Guardrail → Response
  
Tax: 6 serialize/deserialize hops, 4 CPU↔GPU copies, GIL contention
At high QPS: GPUs sit idle waiting for Python CPU work
```

### Solution Approaches

| Approach                             | How                                                          | Eliminates                                         | Best For                                      |
| ------------------------------------ | ------------------------------------------------------------ | -------------------------------------------------- | --------------------------------------------- |
| **Triton Ensemble**            | DAG of GPU models (guardrail→LLM→guardrail)                | All inter-model serialization; tensors stay on GPU | ML-based guardrails (toxicity classifiers)    |
| **Rust Gateway (SMG pattern)** | Move all CPU work to Rust; GPU process does only tensor math | 100% Python GIL; all non-tensor serialization      | Rule-based guardrails; high QPS               |
| **Apache Arrow shared memory** | All stages read/write Arrow buffers (zero-copy)              | Serialization between any stage                    | Heterogeneous pipelines (Python + Rust + C++) |
| **Co-located serving**         | All models in same GPU process                               | Network hops; inter-process serialization          | When GPU memory allows                        |
| **Compiled pipeline**          | TensorRT compile guardrail+LLM into single graph             | Intermediate tensor materialization                | Fixed-shape, static workloads                 |

### Recommended Architecture (from draft)

```
Client → Rust Gateway (tokenization, rule guardrails, rate limiting)
       → Triton (ML Input Guardrail → LLM → ML Output Guardrail) [all GPU]
       → Rust Gateway (detokenization, formatting)
       → Client
```

---

# SECTION 5: SYSTEMS PROGRAMMING REFERENCE

> C++ and Rust patterns from stories — memory layout, lock-free structures, arena allocation, SIMD, zero-copy.

---

## Rust Patterns (CapitalOne / Fiserv)

### Pattern 1: Per-Core Arena Allocator

```rust
#[repr(C)]
struct PerCoreSlab {
    slab: Vec<u8>,          // Pre-allocated at startup (e.g., 64KB)
    slab_offset: usize,     // Current write position
}

impl PerCoreSlab {
    fn alloc(&mut self, len: usize) -> &mut [u8] {
        let start = self.slab_offset;
        self.slab_offset += len;
        &mut self.slab[start..self.slab_offset]
    }
  
    fn reset(&mut self) {
        self.slab_offset = 0;  // Bulk reset — no deallocation
    }
}
```

**Interview talking point:** "At 24K TPS, even jemalloc's thread-local caches occasionally contend. A per-core slab with offset=0 reset means zero allocation overhead, zero fragmentation, and deterministic latency."

### Pattern 2: Zero-Copy Transaction View

```rust
/// Borrows directly from network buffer — no deserialization
#[repr(C)]
struct TxnView<'a> {
    card_bin: u32,
    amount_minor: i64,
    merchant_id: u64,
    device_id_hash: u64,
    mcc: u16,
    country_code: u16,
    channel: u8,
    merchant_desc: &'a [u8],   // Slice into original buffer
    device_blob: &'a [u8],     // Slice into original buffer
}
```

**Interview talking point:** "The lifetime `'a` guarantees the view can't outlive the buffer. No copies, no parsing — we just overlay the struct on the incoming bytes with validated offsets."

### Pattern 3: Cacheline-Aligned Feature Block

```rust
#[repr(C, align(64))]
struct FeatureBlock {
    amount_normalized: f32,
    velocity_1h: f32,
    velocity_24h: f32,
    // ... numeric features
    merchant_embedding: [f32; 128],   // Precomputed offline
    device_embedding: [f32; 64],      // Precomputed offline
}

impl FeatureBlock {
    fn as_slice(&self) -> &[f32] {
        unsafe {
            std::slice::from_raw_parts(
                self as *const Self as *const f32,
                std::mem::size_of::<Self>() / 4,
            )
        }
    }
}
```

**Interview talking point:** "All models read from the same FeatureBlock — XGBoost, GBDT, scorecard, GPU MLP. One struct, one cacheline alignment, no per-model data conversion. The `as_slice` lets any model treat it as a contiguous f32 array."

### Pattern 4: SPSC Lock-Free Ring Buffer

```rust
struct SPSCRing {
    buffer: Vec<RingDescriptor>,
    capacity: usize,
    head: AtomicUsize,   // Consumer reads
    tail: AtomicUsize,   // Producer writes
}

impl SPSCRing {
    fn push(&self, desc: RingDescriptor) -> Result<(), RingFull> {
        let tail = self.tail.load(Ordering::Relaxed);
        let next_tail = (tail + 1) % self.capacity;
        if next_tail == self.head.load(Ordering::Acquire) {
            return Err(RingFull);
        }
        self.buffer[tail] = desc;
        self.tail.store(next_tail, Ordering::Release);
        Ok(())
    }
  
    fn pop(&self) -> Option<RingDescriptor> {
        let head = self.head.load(Ordering::Relaxed);
        if head == self.tail.load(Ordering::Acquire) {
            return None;
        }
        let desc = self.buffer[head];
        self.head.store((head + 1) % self.capacity, Ordering::Release);
        Some(desc)
    }
}
```

**Interview talking point:** "SPSC means single-producer/single-consumer — no CAS loops, no contention. The ring carries descriptors (32 bytes) not payloads — actual data stays in the slab. `Release/Acquire` ordering ensures the consumer sees the write before the updated tail."

### Pattern 5: Apache Arrow Zero-Copy Cross-Tier

```rust
fn publish_to_arrow_buffer(
    txn_view: &TxnView,
    feature_block: &FeatureBlock,
    ensemble_score: &EnsembleScore,
) -> ArrowRecordBatch {
    // Wraps existing memory — no copies
    let feature_array = FixedSizeListArray::from_iter(
        vec![Some(feature_block.as_slice())], 512,
    );
    RecordBatch::try_new(Arc::new(schema), vec![
        Arc::new(feature_array),
        Arc::new(Float32Array::from_iter_values(vec![ensemble_score.final_score])),
        // ...
    ]).unwrap()
}
```

**Interview talking point:** "Arrow wraps existing memory as typed arrays. The same physical bytes are consumed by Tier 2 LLM (Arrow→tensor), by Spark (native Arrow reader), and by retraining pipelines (Arrow→PyTorch dataset). Zero serialization at boundaries."

---

## C++ Patterns (Apple Siri / Broadcom)

### Pattern 1: Shared Memory State (Multi-Stage Pipeline)

```cpp
struct ConversationalState {
    struct Header {
        uint64_t session_id;
        uint64_t sequence_num;
        uint32_t state_flags;
        uint32_t stage_mask;       // Which stages processed
        int64_t ingress_timestamp_ns;
        int64_t deadline_ns;
    } __attribute__((aligned(64)));
  
    Header header;
  
    struct ASROutput {
        float confidence;
        uint32_t hypothesis_count;
    } asr_output;
  
    struct NLUOutput {
        uint32_t intent_id;
        float intent_confidence;
        uint32_t entity_count;
    } nlu_output;
  
    // Variable-length arena
    uint8_t arena[65536];          // 64KB shared arena
    uint32_t arena_offset;
};
```

**Interview talking point:** "Single shared memory region mapped into all stage processes. Fixed header with atomic flags for coordination. Variable data in preallocated arena — no malloc during hot path. Cacheline alignment prevents false sharing between stage writers."

### Pattern 2: Zero-Copy Buffer Pool

```cpp
class BufferPool {
    std::vector<Buffer*> free_list;
    std::mutex pool_mutex;
  
public:
    Buffer* acquire() {
        std::lock_guard<std::mutex> lock(pool_mutex);
        if (free_list.empty()) {
            return new Buffer(65536);
        }
        Buffer* buf = free_list.back();
        free_list.pop_back();
        buf->reset();
        return buf;
    }
  
    void release(Buffer* buf) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        free_list.push_back(buf);
    }
};
```

**Interview talking point:** "At millions of concurrent sessions, malloc/free per request costs 100–500ns and fragments the heap. Pool acquire is ~50ns (lock + pop). For ultra-low-latency, we can use a lock-free pool with tagged pointers to avoid the mutex entirely."

### Pattern 3: Cacheline-Aligned Request Context (Multi-Model)

```cpp
struct RequestContext {
    struct Header {
        uint64_t request_id;
        uint64_t tenant_id;
        uint32_t flags;
        uint32_t model_mask;
    } __attribute__((aligned(64)));
  
    struct URLFeatures {
        char domain[256];
        float lexical_risk_score;
        uint32_t category_id;
    } url_features;
  
    struct ModelOutputs {
        float malware_score;
        float phishing_score;
        float dlp_risk_score;
        uint32_t final_action;     // ALLOW=0, BLOCK=1
    } model_outputs;
  
    uint8_t arena[1048576];        // 1MB
    uint32_t arena_offset;
};
```

**Interview talking point:** "Six models write to disjoint fields in ModelOutputs — no mutex needed. All models read shared URLFeatures and ContentFeatures. Cacheline alignment per section prevents false sharing. The 1MB arena holds variable-length content (HTTP body, headers) for malware/DLP scanning."

---

## Memory Ordering Quick Reference

| Ordering    | Guarantee                                                 | Use Case                     |
| ----------- | --------------------------------------------------------- | ---------------------------- |
| `Relaxed` | No ordering guarantee (compiler/hardware can reorder)     | Counters, statistics         |
| `Acquire` | All reads/writes AFTER this are visible after the load    | Consumer side of ring buffer |
| `Release` | All reads/writes BEFORE this are visible before the store | Producer side of ring buffer |
| `AcqRel`  | Both acquire and release                                  | Read-modify-write (CAS)      |
| `SeqCst`  | Total global ordering (strongest, slowest)                | Rarely needed; fallback      |

**SPSC pattern:** Producer stores data, then `Release`-stores tail. Consumer `Acquire`-loads tail, then reads data. The `Release`/`Acquire` pair guarantees the consumer sees the data the producer wrote.

---

# SECTION 6: INFRASTRUCTURE REFERENCE

> Host tuning, Kubernetes, Networking, Storage checklists for GPU inference workloads.

---

## Host Tuning Checklist (GPU Inference Node)

### CPU & Scheduling

| Setting              | Command                                                        | Purpose                               |
| -------------------- | -------------------------------------------------------------- | ------------------------------------- |
| CPU isolation        | `isolcpus=4-15` (kernel boot)                                | Reserve cores for inference workers   |
| NUMA affinity        | `numactl --cpunodebind=0 --membind=0 ./server`               | Keep process on same NUMA node as GPU |
| CFS throttle disable | `echo -1 > /proc/sys/kernel/sched_rt_runtime_us`             | Prevent RT throttling                 |
| IRQ affinity         | `echo 3 > /proc/irq/XX/smp_affinity`                         | Pin NIC IRQs to non-inference cores   |
| CPU governor         | `cpupower frequency-set -g performance`                      | Disable frequency scaling             |
| Disable THP          | `echo madvise > /sys/kernel/mm/transparent_hugepage/enabled` | Prevent compaction stalls             |

### Memory

| Setting          | Command                                   | Purpose                                            |
| ---------------- | ----------------------------------------- | -------------------------------------------------- |
| Huge pages (2MB) | `echo 1024 > /proc/sys/vm/nr_hugepages` | Reduce TLB misses for large buffers                |
| Huge pages (1GB) | `hugepagesz=1G hugepages=4` (boot)      | For GPU pinned memory regions                      |
| Swappiness       | `sysctl vm.swappiness=0`                | Never swap (GPU pinned mem = non-swappable anyway) |
| Overcommit       | `sysctl vm.overcommit_memory=1`         | Allow arena pre-allocation                         |
| IOMMU            | Disable or passthrough for GPU            | Avoid IOMMU overhead on DMA                        |

### Network (for gRPC/NCCL)

| Setting          | Command                               | Purpose                                  |
| ---------------- | ------------------------------------- | ---------------------------------------- |
| TCP buffer sizes | `sysctl net.core.rmem_max=16777216` | Large receive buffers for gRPC           |
| TCP nodelay      | Application-level                     | Disable Nagle for latency-sensitive RPCs |
| Busy polling     | `sysctl net.core.busy_read=50`      | Reduce network interrupt overhead        |
| Ring buffer size | `ethtool -G eth0 rx 4096 tx 4096`   | Prevent packet drops under burst         |
| MTU (for NCCL)   | `ip link set dev eth0 mtu 9000`     | Jumbo frames for AllReduce traffic       |

### GPU-Specific

| Setting          | Command                             | Purpose                                    |
| ---------------- | ----------------------------------- | ------------------------------------------ |
| Persistence mode | `nvidia-smi -pm 1`                | Keep GPU initialized (avoid cold-start)    |
| Max clocks       | `nvidia-smi -ac 1593,1410`        | Lock to max memory/GPU clocks              |
| ECC mode         | `nvidia-smi --ecc-config=1`       | Error correction (required for production) |
| MIG (optional)   | `nvidia-smi mig -cgi 9,9,9 -C`    | Partition H100 for multi-tenant            |
| Compute mode     | `nvidia-smi -c EXCLUSIVE_PROCESS` | One process per GPU (prevent interference) |

---

## Kubernetes Reference (GPU Inference)

### Pod Spec for GPU Inference

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: llm-inference
spec:
  containers:
  - name: vllm
    image: vllm/vllm-openai:latest
    resources:
      requests:
        nvidia.com/gpu: 1
        memory: "32Gi"
        cpu: "8"
      limits:
        nvidia.com/gpu: 1
        memory: "64Gi"
    env:
    - name: CUDA_VISIBLE_DEVICES
      value: "0"
    - name: NCCL_SOCKET_IFNAME
      value: "eth0"
  nodeSelector:
    nvidia.com/gpu.product: "NVIDIA-H100-80GB-HBM3"
  tolerations:
  - key: nvidia.com/gpu
    operator: Exists
    effect: NoSchedule
```

### Autoscaling Strategy

| Metric                 | Scale Trigger   | Why                          |
| ---------------------- | --------------- | ---------------------------- |
| GPU utilization (DCGM) | > 80% for 2 min | GPU is saturated             |
| Request queue depth    | > 50 pending    | Requests waiting too long    |
| KV cache usage         | > 90%           | Memory pressure → evictions |
| P99 latency            | > SLO target    | SLO breach imminent          |
| GPU memory free        | < 10 GB         | No room for new requests     |

### Deployment Patterns

| Pattern                 | Use Case       | How                                                              |
| ----------------------- | -------------- | ---------------------------------------------------------------- |
| **Blue-Green**    | Model updates  | Deploy new model to green; switch traffic atomically; drain blue |
| **Canary**        | Config changes | 5% traffic to new config; monitor P99; promote or rollback       |
| **Rolling**       | Code updates   | Update pods one at a time; PDB ensures min available             |
| **Multi-cluster** | Regional       | Active-active across regions; DNS-based failover                 |

### Key K8s Concepts for Interviews

- **Device Plugin:** NVIDIA k8s-device-plugin exposes GPUs as schedulable resources. Handles allocation and health checks.
- **Topology Manager:** Ensures GPU and CPU are on same NUMA node. Policy: `single-numa-node`.
- **Resource Quotas:** Limit GPU usage per namespace (multi-tenancy).
- **Priority Classes:** Inference pods get higher priority than batch training.
- **Pod Disruption Budget:** `minAvailable: 75%` ensures rolling updates don't breach SLOs.

---

## Networking Reference

### For LLM Serving (gRPC)

| Concern               | Solution                                                  |
| --------------------- | --------------------------------------------------------- |
| Connection management | gRPC connection pooling; HTTP/2 multiplexing              |
| Load balancing        | L7 (Envoy/Istio) for gRPC; client-side LB for performance |
| Backpressure          | Bounded request queue + HTTP 429 on overflow              |
| Timeout strategy      | Per-tier: T1=10ms, T2=5s, T3=15s; deadline propagation    |
| TLS overhead          | mTLS with session resumption; TLS 1.3 0-RTT               |

### For Multi-GPU (NCCL)

| Concern                | Solution                                                     |
| ---------------------- | ------------------------------------------------------------ |
| AllReduce optimization | NVLink for intra-node; RoCE/InfiniBand for inter-node        |
| Topology check         | `nvidia-smi topo -m` before deployment                     |
| NCCL tuning            | `NCCL_ALGO=Ring`, `NCCL_MIN_NCHANNELS`, socket interface |
| PCIe bottleneck        | Avoid TP across PCIe-only pairs (Task 4)                     |
| NVSwitch               | Full mesh AllReduce without ring penalty                     |

---

## Storage Reference

| Workload      | Storage Choice                    | Why                                    |
| ------------- | --------------------------------- | -------------------------------------- |
| Model weights | Local NVMe (or cached)            | Fast loading; avoid network on startup |
| KV cache      | GPU HBM (managed by vLLM)         | Must be in GPU memory                  |
| Checkpoints   | S3/GCS with local cache           | Durable; load on pod startup           |
| Logs/metrics  | EBS/PD → shipped to object store | Cost-effective long-term               |
| Training data | Object store (S3/GCS)             | Scalable; read-heavy                   |
| Feature cache | Redis / in-memory                 | Sub-millisecond reads                  |

---

# SECTION 7: INTERVIEW QUICK-REFERENCE CARDS

> 6 cards for last-day cramming — one page each, core facts and talking points.

---

## Card 1: "Tell Me About Yourself" (30 seconds)

> "I'm a systems engineer with 15 years building AI infrastructure at massive scale. My sweet spot is the intersection of GPU optimization, low-latency systems, and production ML.
>
> At Capital One, I built a three-tier fraud platform — sub-5ms at 24,500 TPS using Rust, CUDA Graphs, and zero-copy Arrow.
> At Apple, I optimized Siri's inference pipeline from 850ms to 280ms with shared-memory zero-copy.
> At Broadcom, I unified six ML models into a single pipeline — 350ms down to 85ms for 2 million requests per second.
> At Fiserv now, I'm orchestrating 8B+70B LLMs for loan processing with 3.7× latency improvement.
>
> I'm looking for a role where I can apply this combination of GPU systems knowledge and production engineering at scale — which is exactly what your team does."

---

## Card 2: Metrics Cheat Sheet

| Story                   | Before     | After                 | Improvement          |
| ----------------------- | ---------- | --------------------- | -------------------- |
| CapitalOne T1           | —         | <5ms p99 @ 24,500 TPS | Absolute target met  |
| Fiserv                  | 8.5s       | 2.3s                  | 3.7×                |
| Apple Siri              | 850ms      | 280ms                 | 3.0×                |
| Broadcom SWG            | 350ms      | 85ms                  | 4.1×                |
| Broadcom throughput/GPU | 1K req/sec | 10K req/sec           | 10×                 |
| Fiserv token usage      | 100%       | 40%                   | 60% reduction        |
| Fiserv GPU utilization  | —         | 85%                   | —                   |
| Fiserv KV cache hit     | —         | 70%                   | —                   |
| Broadcom availability   | 99.9%      | 99.99%                | 10× fewer incidents |

---

## Card 3: Zero-Copy Patterns (Universal Theme)

| Pattern                   | Where                   | How                                   | Savings                    |
| ------------------------- | ----------------------- | ------------------------------------- | -------------------------- |
| Arena slab + offset reset | CapitalOne (per-core)   | Preallocate worst-case; reset offset  | 0 alloc overhead           |
| Shared memory (mmap)      | Apple (cross-stage)     | Map struct into all processes         | 200–350ms saved           |
| Apache Arrow buffers      | CapitalOne (cross-tier) | Columnar format wraps existing memory | 0 serialization            |
| Pinned GPU memory         | CapitalOne, Fiserv      | cudaMallocHost; async DMA             | 3.2× transfer speed       |
| Buffer pools              | Apple (gRPC)            | Free list; acquire/release            | 0 per-request malloc       |
| RequestContext struct     | Broadcom (multi-model)  | Single 1MB arena, disjoint writes     | 0 ser/deser between models |
| CUDA Graphs               | CapitalOne (MLP)        | Capture kernel DAG; single launch     | 145µs per inference       |
| FeatureBlock              | CapitalOne (ensemble)   | One aligned struct feeds all models   | 0 per-model conversion     |

**Universal principle:** Allocate once at startup, structure data for zero-copy reuse, reset instead of free.

---

## Card 4: "Why Did You Choose X?" Quick Answers

| Question                               | 1-Line Answer                                                                            |
| -------------------------------------- | ---------------------------------------------------------------------------------------- |
| Why Rust over C++?                     | Ownership model prevents use-after-free; zero-cost arena allocators; no GC pauses        |
| Why CUDA Graphs over eager?            | 30 small kernels × 5µs = 150µs overhead; graph reduces to 5µs                        |
| Why Arrow over protobuf?               | Zero-copy (same bytes for inference, analytics, retraining); protobuf requires ser/deser |
| Why shared memory over message queue?  | Nanosecond access vs millisecond (network + serialization); 5 stages × 50ms savings     |
| Why vLLM over TensorRT-LLM?            | Variable sequence lengths cause TRT graph recompilation; vLLM handles dynamic shapes     |
| Why XGBoost over deep learning?        | <100µs per tree model; interpretable for regulators; better for tabular fraud features  |
| Why per-core workers over thread pool? | No cross-core cache bouncing; NUMA-local; deterministic latency at 24K TPS               |
| Why dynamic batching?                  | 10× GPU throughput; amortize weight loading across requests                             |
| Why hierarchical context?              | 60% token reduction; 70B model sees only what matters; better accuracy + lower cost      |
| Why session-affine routing?            | 70% KV cache hit rate; skip prefill for returning applicants                             |

---

## Card 5: System Design Answer Framework

### For "Design an LLM Serving System" Questions

```
1. REQUIREMENTS (30 sec)
   - Latency SLO? (TTFT < X, TPOT < Y)
   - Throughput? (requests/sec)
   - Model size? (fits one GPU or needs TP/PP?)
   - Availability? (99.9% vs 99.99%)

2. HIGH-LEVEL ARCHITECTURE (2 min)
   - Control plane (routing, scheduling, admission)
   - Data plane (GPU inference, memory management)
   - Observability (metrics, profiling, alerting)

3. KEY DECISIONS (3 min)
   - Serving framework: vLLM (variable shapes) vs TRT-LLM (fixed shapes)
   - Parallelism: TP (need NVLink) vs PP (less communication)
   - Memory: PagedAttention, KV cache sizing, FP8 cache
   - Batching: Continuous batching (vLLM default)
   - Scaling: GPU util + queue depth → HPA

4. OPTIMIZATIONS (2 min)
   - Prefix caching (shared system prompts)
   - Chunked prefill (protect decode latency)
   - CUDA Graphs (decode phase)
   - Quantization (FP8 weights, BF16 attention)
   - Session-affine routing (KV cache reuse)

5. FAILURE MODES (1 min)
   - OOM → admission control, queue bounds
   - Latency spike → circuit breaker, fallback
   - GPU failure → health check, auto-replacement
   - Model update → blue-green, atomic swap
```

---

## Card 6: Behavioral / Leadership Talking Points

| Question Pattern                          | Story to Use                        | Key Point                                                                                                                                          |
| ----------------------------------------- | ----------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| "Tell me about a hard technical decision" | CapitalOne: tiered architecture     | Separated decisioning (5ms) from reasoning (5s) — controversial but enabled both SLOs                                                             |
| "How do you handle disagreements?"        | Broadcom: unifying 6 teams          | Each team had their own ML pipeline; I proposed shared context — won buy-in by showing 4× latency improvement                                    |
| "Biggest failure / what you learned"      | Task 8 pattern: TRT-LLM slower      | Assumed compiled = faster. Learned variable shapes invalidate that assumption. Now always profile before committing                                |
| "How do you mentor?"                      | All stories: code patterns          | I create reusable patterns (arena allocators, ring buffers, feature blocks) that team members adopt. Document the "why" not just "how"             |
| "Cross-team collaboration"                | Broadcom (6 ML teams)               | Built shared platform that all teams deploy to. Defined contracts (RequestContext struct), let teams own their models                              |
| "How do you prioritize?"                  | Apple: 300ms budget across 5 stages | Profiled each stage, found serialization was 70% of latency. Fixed the biggest contributor first (shared memory), then optimized individual stages |

---

# APPENDIX: Key Numbers to Memorize

| Fact                                | Number                      |
| ----------------------------------- | --------------------------- |
| CUDA kernel launch overhead         | ~5–15 µs                  |
| H100 HBM bandwidth                  | 3.35 TB/s                   |
| H100 FP16 TFLOPS                    | 1,979                       |
| H100 SMs                            | 132                         |
| NVLink 4 bandwidth (per link)       | 900 GB/s bidirectional      |
| PCIe Gen5 bandwidth                 | ~64 GB/s                    |
| NVLink vs PCIe speedup              | ~14×                       |
| Typical AllReduce latency (NVLink)  | 0.1–0.3 ms                 |
| Typical AllReduce latency (PCIe)    | 0.5–2.0 ms                 |
| malloc latency                      | 100–500 ns                 |
| Pool acquire latency                | ~50 ns                      |
| Shared memory access                | ~20 cycles (~15 ns)         |
| L2 cache access                     | ~200 cycles                 |
| HBM access                          | ~400 cycles                 |
| gRPC roundtrip (same datacenter)    | 0.5–2 ms                   |
| JSON serialization (feature vector) | ~200 µs                    |
| Arrow wrap (zero-copy)              | ~0 µs (pointer assignment) |
| XGBoost inference (500 trees)       | <100 µs                    |
| vLLM decode step (8B, batch=1)      | ~14 ms                      |
| Llama 8B weights (FP16)             | ~16 GB                      |
| KV cache per token (Llama 8B, FP16) | ~128 KB                     |

---

*End of Master Interview Syllabus*
