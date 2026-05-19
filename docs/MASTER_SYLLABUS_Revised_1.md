# AI/HPC SYSTEMS ENGINEERING MASTER SYLLABUS

**Comprehensive Guide for Sr. Staff / Principal / Distinguished Engineer Roles**

**Target Companies:** NVIDIA, Apple, Broadcom, HPE, Capital One, Google, Meta, Microsoft
**Domains:** GPU/Inference Platforms, HPC Systems, Low-Latency Distributed Systems, Agentic AI
**Candidate Profile:** 15+ years experience in AI Systems & Low-Latency Infrastructure

---

## TABLE OF CONTENTS

1. [Executive Summary](#executive-summary)
2. [Project Portfolio](#project-portfolio)
3. [Technology Decision Matrix](#technology-decision-matrix)
4. [System Architecture Reference](#system-architecture-reference)
5. [GPU &amp; CUDA Mastery](#gpu--cuda-mastery)
6. [LLM &amp; ML Inference Mastery](#llm--ml-inference-mastery)
7. [Systems Programming Patterns](#systems-programming-patterns)
8. [Infrastructure &amp; Platform Guide](#infrastructure--platform-guide)
9. [Troubleshooting Runbooks](#troubleshooting-runbooks)
10. [Interview Preparation](#interview-preparation)
11. [Study Plan &amp; Progress Tracking](#study-plan--progress-tracking)

---

# EXECUTIVE SUMMARY

This syllabus consolidates three comprehensive guides into one master reference for AI/HPC systems engineering roles at the Sr. Staff+ level. It covers:

- **4 Major Projects** with complete architecture diagrams, metrics, and drill-down Q&A
- **Technology Decisions** mapped to specific stories with rationale and alternatives
- **System Layers** from application code to bare-metal tuning
- **GPU/CUDA Profiling** workflows and 8 practical debugging tasks
- **Systems Programming** patterns in Rust and C++
- **Infrastructure Checklists** for host, Kubernetes, networking, and storage
- **Troubleshooting Runbooks** for production incidents
- **Interview Preparation** cards and study plans

The unifying theme across all projects is **zero-copy data flow** and **deterministic latency** at massive scale.

---

# PROJECT PORTFOLIO

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

# TECHNOLOGY DECISION MATRIX

> Every technology you've used, mapped to which story, WHY you chose it, and what alternatives you rejected.

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

# SYSTEM ARCHITECTURE REFERENCE

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

# GPU & CUDA MASTERY

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
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              L2 Cache (50 MB on H100)                │   │
│  └─────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              HBM3 (80 GB, 3.35 TB/s on H100)        │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Physical Layout — Inside One SM (H100)

```
┌───────────────────────────────────────────────────────────────────────┐
│                     STREAMING MULTIPROCESSOR (SM)                      │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                    4 × Processing Blocks (Sub-partitions)       │  │
│  │                                                                 │  │
│  │  ┌───────────────────┐   ┌───────────────────┐                │  │
│  │  │  Processing Block 0│   │  Processing Block 1│                │  │
│  │  │                   │   │                   │                │  │
│  │  │  • Warp Scheduler │   │  • Warp Scheduler │                │  │
│  │  │  • Dispatch Unit  │   │  • Dispatch Unit  │                │  │
│  │  │  • 16 FP32 cores  │   │  • 16 FP32 cores  │                │  │
│  │  │  • 16 INT32 cores │   │  • 16 INT32 cores │                │  │
│  │  │  • 1 Tensor Core  │   │  • 1 Tensor Core  │                │  │
│  │  │  • 4 Load/Store   │   │  • 4 Load/Store   │                │  │
│  │  │  • 4 SFU (sin/cos)│   │  • 4 SFU (sin/cos)│                │  │
│  │  │  • 16,384 Regs    │   │  • 16,384 Regs    │                │  │
│  │  └───────────────────┘   └───────────────────┘                │  │
│  │  ┌───────────────────┐   ┌───────────────────┐                │  │
│  │  │  Processing Block 2│   │  Processing Block 3│                │  │
│  │  │  (same as above)   │   │  (same as above)   │                │  │
│  │  └───────────────────┘   └───────────────────┘                │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌────────────────────────────────────────────────────┐               │
│  │  Register File: 65,536 × 32-bit registers (256 KB) │               │
│  └────────────────────────────────────────────────────┘               │
│  ┌────────────────────────────────────────────────────┐               │
│  │  L1 Data Cache / Shared Memory: 228 KB (configurable split)        │
│  │    └── Default: 128 KB Shared + 100 KB L1 (adjustable)            │
│  └────────────────────────────────────────────────────┘               │
│  ┌────────────────────────────────────────────────────┐               │
│  │  Tensor Cores: 4 per SM (4th gen on H100)          │               │
│  │    └── Support: FP64, TF32, BF16, FP16, FP8, INT8 │               │
│  │    └── Throughput: 256 FP16 FMA ops/clock/SM       │               │
│  └────────────────────────────────────────────────────┘               │
│  ┌────────────────────────────────────────────────────┐               │
│  │  Texture / L1 Instruction Cache                    │               │
│  └────────────────────────────────────────────────────┘               │
└───────────────────────────────────────────────────────────────────────┘
```

**H100 SM totals:** 132 SMs × 128 FP32 cores/SM = **16,896 FP32 cores** total

### Physical Specs by Generation

| Resource (per SM)    | Turing (T4) | Ampere (A100) | Hopper (H100) |
| -------------------- | ----------- | ------------- | ------------- |
| FP32 CUDA Cores      | 64          | 64            | 128           |
| INT32 Cores          | 64          | 64            | 128           |
| Tensor Cores         | 8 (2nd gen) | 4 (3rd gen)   | 4 (4th gen)   |
| Register File        | 256 KB      | 256 KB        | 256 KB        |
| Max Registers/Thread | 255         | 255           | 255           |
| Shared Memory        | 96 KB       | 164 KB        | 228 KB        |
| Max Threads/SM       | 1024        | 2048          | 2048          |
| Max Warps/SM         | 32          | 64            | 64            |
| Warp Schedulers      | 4           | 4             | 4             |
| Total SMs            | 40          | 108           | 132           |
| Max Thread Blocks/SM | 16          | 32            | 32            |

### Logical Layout — CUDA Programming Model

```
┌─────────────────────────────────────────────────────────────────────┐
│                    CUDA PROGRAMMING HIERARCHY                        │
│                                                                     │
│  ┌──── Grid (kernel launch) ─────────────────────────────────────┐ │
│  │                                                                │ │
│  │   ┌── Block (0,0) ──┐  ┌── Block (1,0) ──┐  ┌── Block ──┐  │ │
│  │   │                  │  │                  │  │    ...     │  │ │
│  │   │  ┌── Warp 0 ──┐ │  │  ┌── Warp 0 ──┐ │  │            │  │ │
│  │   │  │ T0 T1 .. T31│ │  │  │ T0 T1 .. T31│ │  │            │  │ │
│  │   │  └─────────────┘ │  │  └─────────────┘ │  │            │  │ │
│  │   │  ┌── Warp 1 ──┐ │  │  ┌── Warp 1 ──┐ │  │            │  │ │
│  │   │  │ T32 .. T63  │ │  │  │ T32 .. T63  │ │  │            │  │ │
│  │   │  └─────────────┘ │  │  └─────────────┘ │  │            │  │ │
│  │   │       ...         │  │       ...         │  │            │  │ │
│  │   │  ┌── Warp N ──┐ │  │  ┌── Warp N ──┐ │  │            │  │ │
│  │   │  │ T(N*32)..   │ │  │  │ T(N*32)..   │ │  │            │  │ │
│  │   │  └─────────────┘ │  │  └─────────────┘ │  │            │  │ │
│  │   └──────────────────┘  └──────────────────┘  └────────────┘  │ │
│  │                                                                │ │
│  │   Grid dimensions: gridDim.x × gridDim.y × gridDim.z          │ │
│  │   Block dimensions: blockDim.x × blockDim.y × blockDim.z      │ │
│  │   Threads per block: max 1024                                  │ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

### Physical ↔ Logical Mapping

```
┌─────────────────────────────────────────────────────────────────────┐
│             LOGICAL (programmer)  →  PHYSICAL (hardware)            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Grid                          →  Distributed across ALL SMs        │
│   │                                 (scheduler assigns blocks)      │
│   │                                                                 │
│   ├─ Thread Block              →  Runs on ONE SM (never split)      │
│   │   │                             Multiple blocks can share SM    │
│   │   │                                                             │
│   │   ├─ Warp (32 threads)    →  Scheduled on one Processing Block │
│   │   │   │                         (warp scheduler picks each clk) │
│   │   │   │                                                         │
│   │   │   └─ Thread           →  Executes on one FP32/INT32 core   │
│   │   │                             Registers: private per thread   │
│   │   │                                                             │
│   │   └─ Shared Memory        →  SM's shared memory (228 KB on H100)│
│   │       (declared in block)       Visible to ALL threads in block │
│   │                                                                 │
│   └─ Global Memory            →  HBM (80 GB, accessible by all)    │
│       (cudaMalloc)                  Goes through L2 → L1 caches     │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘

IMPORTANT RULES:
  • One Thread Block → exactly ONE SM (blocks are never split across SMs)
  • Multiple Thread Blocks → CAN run on same SM (if resources allow)
  • Block scheduling order → UNDEFINED (cannot depend on block execution order)
  • Warps within a block → time-sliced on SM's warp schedulers (4 per SM)
  • Threads within a warp → execute in LOCKSTEP (SIMT)
```

### Resource Limits & Occupancy

```
Max occupancy calculation (H100):
  SM has: 65,536 registers, 228 KB shared memory, 2048 max threads, 64 max warps

  Your kernel uses: 64 registers/thread, 48 KB shared memory, 256 threads/block

  Register limit:  65,536 / (64 regs × 256 threads) = 4 blocks/SM → 1024 threads
  Shared mem limit: 228 KB / 48 KB = 4 blocks/SM → 1024 threads
  Thread limit:     2048 / 256 = 8 blocks/SM → 2048 threads
  Warp limit:       64 / (256/32) = 8 blocks/SM → 2048 threads

  BOTTLENECK: Registers + Shared Memory → 4 blocks → 1024 threads
  Occupancy: 1024 / 2048 = 50%

  Trade-offs:
    Reduce registers (--maxrregcount=32) → more blocks but may spill to L1
    Reduce shared memory → more blocks but may need more HBM accesses
    Increase block size → fewer blocks but better intra-block cooperation
```

| Factor                 | Increases Occupancy | Decreases Occupancy | Impact                                       |
| ---------------------- | ------------------- | ------------------- | -------------------------------------------- |
| Fewer registers/thread | ✅ More threads fit |                     | May spill to local memory (slow)             |
| Less shared mem/block  | ✅ More blocks fit  |                     | May need more HBM accesses                   |
| Smaller block size     | ✅ More blocks fit  |                     | Less intra-block cooperation                 |
| More registers/thread  |                     | ❌ Fewer threads    | Better for compute-heavy (keep data in regs) |
| More shared mem/block  |                     | ❌ Fewer blocks     | Better for data reuse (tiling)               |

**Interview insight:** "High occupancy doesn't always mean high performance. A kernel using 255 registers per thread at 25% occupancy can outperform a 100% occupancy kernel with spills to local memory — because register access is free but local memory goes through the cache hierarchy."

### Warp Execution Details

```
WARP = 32 threads executing the SAME instruction in LOCKSTEP

Clock 0:  All 32 threads execute: LOAD R1, [addr + tid*4]
Clock 1:  All 32 threads execute: MUL R2, R1, R3
Clock 2:  All 32 threads execute: ADD R4, R2, R5
Clock 3:  All 32 threads execute: STORE [addr + tid*4], R4

DIVERGENCE (branch within a warp):
  if (threadIdx.x < 16) {   ← threads 0-15 take this path
      doA();
  } else {                   ← threads 16-31 take this path
      doB();
  }

  Execution: doA() runs (threads 16-31 MASKED/idle)
             doB() runs (threads 0-15 MASKED/idle)
  Cost: BOTH paths execute sequentially → 2× time

  Rule: Minimize divergence within a warp. Across warps is FREE.
```

**Warp-level primitives (important for CUDA/Triton interviews):**

| Primitive                              | What                                      | Use Case                       |
| -------------------------------------- | ----------------------------------------- | ------------------------------ |
| `__shfl_sync(mask, val, src)`        | Read `val` from lane `src`            | Broadcast, butterfly reduction |
| `__shfl_down_sync(mask, val, delta)` | Read from lane + delta                    | Parallel reduction (sum)       |
| `__shfl_xor_sync(mask, val, mask)`   | Read from lane XOR mask                   | Butterfly pattern              |
| `__ballot_sync(mask, pred)`          | 32-bit mask of which lanes have pred=true | Count, compress                |
| `__any_sync(mask, pred)`             | 1 if any lane has pred=true               | Early exit                     |
| `__all_sync(mask, pred)`             | 1 if all lanes have pred=true             | Convergence check              |
| `__activemask()`                     | Which lanes are active                    | Divergent code introspection   |

### Thread → Core → SM → GPU Summary Table

| Logical (CUDA) |       Physical (Hardware) | Size                | Scope                             |
| -------------- | ------------------------: | ------------------- | --------------------------------- |
| Thread         |           FP32/INT32 Core | 1                   | Private registers                 |
| Warp           | Warp Scheduler + 32 cores | 32 threads          | Lockstep execution, shuffle       |
| Thread Block   |                    One SM | Up to 1024 threads  | Shared memory,`__syncthreads()` |
| Grid           |      Entire GPU (all SMs) | Millions of threads | Global memory, atomics            |

### Memory Scope Mapping

| Memory Type | CUDA Declaration                | Hardware Location   | Scope       | Lifetime   | Bandwidth             |
| ----------- | ------------------------------- | ------------------- | ----------- | ---------- | --------------------- |
| Register    | Automatic variables             | Register file       | Thread      | Thread     | Unlimited (0 cycles)  |
| Local       | Spilled registers, arrays       | HBM (cached in L1)  | Thread      | Thread     | L1 speed (with cache) |
| Shared      | `__shared__`                  | SM SRAM             | Block       | Block      | ~19 TB/s              |
| L1 Cache    | Automatic                       | SM (same as shared) | SM          | Kernel     | ~19 TB/s              |
| L2 Cache    | Automatic                       | On-chip             | GPU         | Persistent | ~12 TB/s              |
| Global      | `cudaMalloc` / `__device__` | HBM                 | All threads | App        | 3.35 TB/s (H100)      |
| Constant    | `__constant__`                | HBM (cached)        | All threads | App        | Broadcast (cache hit) |
| Texture     | `tex1Dfetch`                  | HBM (spatial cache) | All threads | App        | Good for 2D locality  |

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

## OpenAI Triton — GPU Kernel Programming in Python

> OpenAI Triton is a Python-based language and compiler for writing GPU kernels without needing to write CUDA C++. It's NOT the same as NVIDIA Triton Inference Server. Triton powers `torch.compile`, FlashAttention fused kernels in vLLM, and many custom ops in production LLM serving.

### Why Triton Matters for System Engineers

| Context                        | Role of Triton                                                                    |
| ------------------------------ | --------------------------------------------------------------------------------- |
| **vLLM**                 | Core fused kernels (PagedAttention, RMSNorm, rotary embeddings) written in Triton |
| **torch.compile**        | Default backend (Inductor) generates Triton kernels automatically                 |
| **FlashAttention**       | Reference implementation uses Triton; productionized in CUDA                      |
| **Custom ops**           | 10× faster to prototype than CUDA C++; often sufficient for production           |
| **Quantization kernels** | FP8/INT4 dequantize-fused-GEMM implemented in Triton                              |
| **Interviews**           | "Write a fused kernel" questions increasingly accept Triton                       |

### Triton vs CUDA — When to Use What

| Dimension                     | Triton                                    | CUDA C++                                |
| ----------------------------- | ----------------------------------------- | --------------------------------------- |
| **Language**            | Python (with decorators)                  | C++/C with NVIDIA extensions            |
| **Abstraction level**   | Block-level (tiles)                       | Thread-level (warps, lanes)             |
| **Memory management**   | Automatic (compiler handles shared mem)   | Manual (`__shared__`, bank conflicts) |
| **Occupancy tuning**    | Auto-tuning (`@triton.autotune`)        | Manual (register pressure, block size)  |
| **Compile time**        | JIT (first call slow, then cached)        | AOT (nvcc, separate build step)         |
| **Performance ceiling** | ~90–95% of hand-tuned CUDA               | 100% (full hardware control)            |
| **Development speed**   | 3–5× faster to write/iterate            | Slower, more boilerplate                |
| **Debugging**           | Python-native (print, assert)             | cuda-gdb, printf (harder)               |
| **Best for**            | Fused element-wise, attention, custom ops | Library-grade GEMM, extreme perf        |
| **Who uses**            | vLLM, PyTorch Inductor, research          | cuBLAS, cuDNN, TensorRT, NCCL           |

### Triton Programming Model

```
┌─────────────────────────────────────────────────────────────────────┐
│                   TRITON EXECUTION MODEL                            │
│                                                                     │
│   Python Code          Triton Compiler (MLIR-based)     GPU        │
│                                                                     │
│   @triton.jit    →    Triton IR    →    LLVM IR    →    PTX/SASS   │
│   def kernel():       (tile-level)      (thread-level)   (GPU asm) │
│                                                                     │
│   Key abstraction: BLOCK (tile) not individual threads             │
│                                                                     │
│   Programmer thinks:  "Load a BLOCK_SIZE×BLOCK_SIZE tile,          │
│                        do computation, store result tile"           │
│                                                                     │
│   Compiler handles:   Thread mapping, shared memory allocation,    │
│                        memory coalescing, bank conflict avoidance,  │
│                        register allocation, instruction scheduling  │
└─────────────────────────────────────────────────────────────────────┘
```

### Example 1: Fused RMSNorm (Used in Llama/vLLM)

```python
import triton
import triton.language as tl
import torch

@triton.jit
def rms_norm_kernel(
    X_ptr, W_ptr, Out_ptr,
    stride_x,          # Row stride for X
    N: tl.constexpr,   # Hidden dimension (compile-time constant)
    eps: tl.constexpr,
    BLOCK_SIZE: tl.constexpr,
):
    # Each program instance handles one row (one token)
    row_idx = tl.program_id(0)
  
    # Pointer to start of this row
    row_start = X_ptr + row_idx * stride_x
  
    # Load entire row in tiles (handles N > BLOCK_SIZE)
    # Phase 1: Compute variance
    variance = tl.zeros([BLOCK_SIZE], dtype=tl.float32)
    for off in range(0, N, BLOCK_SIZE):
        cols = off + tl.arange(0, BLOCK_SIZE)
        mask = cols < N
        x = tl.load(row_start + cols, mask=mask, other=0.0).to(tl.float32)
        variance += x * x
  
    variance = tl.sum(variance) / N
    rstd = 1.0 / tl.sqrt(variance + eps)
  
    # Phase 2: Normalize and scale
    for off in range(0, N, BLOCK_SIZE):
        cols = off + tl.arange(0, BLOCK_SIZE)
        mask = cols < N
        x = tl.load(row_start + cols, mask=mask, other=0.0).to(tl.float32)
        w = tl.load(W_ptr + cols, mask=mask, other=0.0).to(tl.float32)
        out = x * rstd * w
        tl.store(Out_ptr + row_idx * stride_x + cols, out, mask=mask)


def rms_norm(x: torch.Tensor, weight: torch.Tensor, eps: float = 1e-6):
    """Launch the Triton RMSNorm kernel."""
    out = torch.empty_like(x)
    M, N = x.shape  # M = num_tokens, N = hidden_dim
  
    # Grid: one program per row (token)
    grid = (M,)
    BLOCK_SIZE = triton.next_power_of_2(N)
  
    rms_norm_kernel[grid](
        x, weight, out,
        x.stride(0),
        N=N, eps=eps,
        BLOCK_SIZE=min(BLOCK_SIZE, 4096),
    )
    return out
```

**Why this is faster than PyTorch native:**

- PyTorch `rms_norm` = 3 separate kernels (square, mean, multiply) = 3 HBM round-trips
- Triton fused = 1 kernel, data stays in registers/SRAM = 1 HBM round-trip
- Typical speedup: 2–3× for hidden_dim=4096

### Example 2: Vector Addition (Minimal Complete Example to Get Started)

```python
import triton
import triton.language as tl
import torch

@triton.jit
def add_kernel(
    x_ptr, y_ptr, output_ptr,
    n_elements,
    BLOCK_SIZE: tl.constexpr,
):
    # Which block of elements this program instance handles
    pid = tl.program_id(axis=0)
  
    # Compute pointers for this block
    block_start = pid * BLOCK_SIZE
    offsets = block_start + tl.arange(0, BLOCK_SIZE)
  
    # Mask for out-of-bounds (last block may be partial)
    mask = offsets < n_elements
  
    # Load, compute, store
    x = tl.load(x_ptr + offsets, mask=mask)
    y = tl.load(y_ptr + offsets, mask=mask)
    output = x + y
    tl.store(output_ptr + offsets, output, mask=mask)


def add(x: torch.Tensor, y: torch.Tensor) -> torch.Tensor:
    output = torch.empty_like(x)
    n_elements = x.numel()
  
    # Grid: how many program instances to launch
    grid = lambda meta: (triton.cdiv(n_elements, meta['BLOCK_SIZE']),)
  
    add_kernel[grid](x, y, output, n_elements, BLOCK_SIZE=1024)
    return output

# Usage
x = torch.rand(1_000_000, device='cuda')
y = torch.rand(1_000_000, device='cuda')
result = add(x, y)
assert torch.allclose(result, x + y)
```

### Example 3: Fused Softmax (Attention Building Block)

```python
@triton.jit
def softmax_kernel(
    input_ptr, output_ptr,
    n_cols: tl.constexpr,
    BLOCK_SIZE: tl.constexpr,
):
    row_idx = tl.program_id(0)
    row_start = input_ptr + row_idx * n_cols
    out_start = output_ptr + row_idx * n_cols
  
    # Load row
    col_offsets = tl.arange(0, BLOCK_SIZE)
    mask = col_offsets < n_cols
    row = tl.load(row_start + col_offsets, mask=mask, other=-float('inf'))
  
    # Numerically stable softmax: subtract max, exp, normalize
    row_max = tl.max(row, axis=0)
    numerator = tl.exp(row - row_max)
    denominator = tl.sum(numerator, axis=0)
    softmax_out = numerator / denominator
  
    tl.store(out_start + col_offsets, softmax_out, mask=mask)
```

### Example 4: Auto-Tuning (Production Pattern)

```python
@triton.autotune(
    configs=[
        triton.Config({'BLOCK_M': 128, 'BLOCK_N': 128, 'BLOCK_K': 32}, num_warps=8),
        triton.Config({'BLOCK_M': 128, 'BLOCK_N': 64,  'BLOCK_K': 32}, num_warps=4),
        triton.Config({'BLOCK_M': 64,  'BLOCK_N': 128, 'BLOCK_K': 32}, num_warps=4),
        triton.Config({'BLOCK_M': 64,  'BLOCK_N': 64,  'BLOCK_K': 64}, num_warps=4),
    ],
    key=['M', 'N', 'K'],  # Re-tune when these change
)
@triton.jit
def matmul_kernel(
    A_ptr, B_ptr, C_ptr,
    M, N, K,
    stride_am, stride_ak,
    stride_bk, stride_bn,
    stride_cm, stride_cn,
    BLOCK_M: tl.constexpr,
    BLOCK_N: tl.constexpr,
    BLOCK_K: tl.constexpr,
):
    # Tiled matmul: each program computes one BLOCK_M × BLOCK_N tile of C
    pid_m = tl.program_id(0)
    pid_n = tl.program_id(1)
  
    # Accumulator (in registers, FP32 for precision)
    acc = tl.zeros((BLOCK_M, BLOCK_N), dtype=tl.float32)
  
    # Loop over K dimension in tiles
    for k in range(0, K, BLOCK_K):
        # Load A tile [BLOCK_M, BLOCK_K]
        a_offsets = (pid_m * BLOCK_M + tl.arange(0, BLOCK_M))[:, None] * stride_am + \
                    (k + tl.arange(0, BLOCK_K))[None, :] * stride_ak
        a = tl.load(A_ptr + a_offsets, mask=..., other=0.0)
    
        # Load B tile [BLOCK_K, BLOCK_N]
        b_offsets = (k + tl.arange(0, BLOCK_K))[:, None] * stride_bk + \
                    (pid_n * BLOCK_N + tl.arange(0, BLOCK_N))[None, :] * stride_bn
        b = tl.load(B_ptr + b_offsets, mask=..., other=0.0)
    
        # Tile matmul (maps to Tensor Core wmma instructions)
        acc += tl.dot(a, b)
  
    # Store result tile
    c_offsets = (pid_m * BLOCK_M + tl.arange(0, BLOCK_M))[:, None] * stride_cm + \
                (pid_n * BLOCK_N + tl.arange(0, BLOCK_N))[None, :] * stride_cn
    tl.store(C_ptr + c_offsets, acc.to(tl.float16), mask=...)


# Auto-tuning: first call benchmarks all configs, caches best
# Subsequent calls use cached optimal configuration
```

**Auto-tuning insight:** The `@triton.autotune` decorator benchmarks all `Config` entries on first call, selects the fastest for the given `key` dimensions, and caches the result. This replaces manual occupancy tuning in CUDA.

### Key Triton Language Primitives

| Primitive                    | Purpose                     | CUDA Equivalent                    |
| ---------------------------- | --------------------------- | ---------------------------------- |
| `tl.program_id(axis)`      | Block index                 | `blockIdx.x`                     |
| `tl.arange(0, N)`          | Thread offsets within block | `threadIdx.x`                    |
| `tl.load(ptr, mask)`       | Coalesced memory read       | `__global__ load + bounds check` |
| `tl.store(ptr, val, mask)` | Coalesced memory write      | Global store + bounds check        |
| `tl.dot(a, b)`             | Tile matrix multiply        | `wmma` / Tensor Core intrinsics  |
| `tl.sum(x, axis)`          | Block-level reduction       | `__shfl_down_sync` + shared mem  |
| `tl.max(x, axis)`          | Block-level max             | Warp shuffle reduction             |
| `tl.exp(x)`                | Element-wise exp            | `__expf()`                       |
| `tl.where(cond, a, b)`     | Conditional select          | Ternary operator                   |
| `tl.atomic_add(ptr, val)`  | Atomic accumulate           | `atomicAdd()`                    |
| `tl.constexpr`             | Compile-time constant       | Template parameter                 |
| `tl.cdiv(a, b)`            | Ceiling division            | `(a + b - 1) / b`                |

### Triton in vLLM — Where It's Used

```
vLLM kernel stack (simplified):
                                            
┌─────────────────────────────────────────────────┐
│  Python API (vLLM engine)                       │
├─────────────────────────────────────────────────┤
│  Custom Triton Kernels (vLLM/vllm/triton_ops/)  │
│  ├── paged_attention_v1/v2.py   ← PagedAttention│
│  ├── layernorm.py               ← Fused RMSNorm │
│  ├── rotary_embedding.py        ← RoPE          │
│  ├── activation.py              ← SiLU/GELU     │
│  ├── quantization/              ← FP8 dequant   │
│  └── moe/                       ← Expert routing│
├─────────────────────────────────────────────────┤
│  cuBLAS / cuBLASLt (GEMM — too complex for Triton) │
├─────────────────────────────────────────────────┤
│  NCCL (collectives — hardware-specific)         │
└─────────────────────────────────────────────────┘

Rule of thumb:
  GEMM → cuBLAS (mature, optimal)
  Attention + fused ops → Triton (flexible, fast iteration)
  Collectives → NCCL (hardware-aware, NVLink/IB optimized)
```

### Practical: Getting Started with Triton

```bash
# ─── Installation ───
pip install triton          # Comes bundled with PyTorch ≥2.0
# Or for latest:
pip install triton-nightly

# ─── Verify installation ───
python -c "import triton; print(triton.__version__)"

# ─── Run the vector add example ───
python examples/vector_add.py

# ─── Benchmark against PyTorch native ───
python -c "
import torch, triton, triton.language as tl
from triton.testing import do_bench

# Compare fused vs unfused softmax
x = torch.randn(1024, 4096, device='cuda')
torch_time = do_bench(lambda: torch.softmax(x, dim=-1))
# triton_time = do_bench(lambda: triton_softmax(x))  # your kernel
print(f'PyTorch softmax: {torch_time:.2f} ms')
"

# ─── Profile a Triton kernel with ncu ───
ncu --set full python my_triton_kernel.py
# Triton kernels show up as regular SASS in ncu (fully compiled)

# ─── Inspect generated PTX/SASS ───
# Set env var to see compiled code:
TRITON_PRINT_AUTOTUNING=1 python my_kernel.py
# Or programmatically:
kernel.warmup(torch.float32, grid=(1,))
print(kernel.asm['ptx'])  # View PTX assembly
print(kernel.asm['cubin'])  # Binary
```

### Performance Tips & Gotchas

| Tip                                               | Why                                                                      |
| ------------------------------------------------- | ------------------------------------------------------------------------ |
| Make `BLOCK_SIZE` a power of 2                  | GPU hardware prefers aligned accesses; compiler can optimize better      |
| Use `tl.constexpr` for all size parameters      | Enables compile-time optimization (unrolling, static allocation)         |
| Accumulate in `tl.float32` even for FP16 inputs | Avoids precision loss in reductions (same as CUDA best practice)         |
| Use `@triton.autotune` in production            | Auto-selects optimal block sizes per hardware; replaces manual tuning    |
| Minimize `tl.atomic_add`                        | Atomic contention kills throughput; restructure to avoid if possible     |
| Fuse element-wise ops into one kernel             | Each separate kernel = 1 HBM round-trip; fusing eliminates intermediates |
| Use `tl.dot` for matrix ops                     | Maps to Tensor Cores automatically (FP16/BF16/FP8)                       |
| Pin `num_warps` in autotune configs             | More warps = better latency hiding, but more register pressure           |
| Profile with `ncu` not Python `time`          | Triton JIT cost is amortized; steady-state perf is what matters          |
| Cache Triton compiled kernels                     | Set `TRITON_CACHE_DIR` for persistent cache across runs                |

### Triton vs torch.compile (Inductor)

```
Developer writes PyTorch code:
    y = torch.softmax(x, dim=-1)
    z = y * weight + bias

torch.compile with Inductor backend:
    1. Trace → FX graph (symbolic representation)
    2. Fuse ops → Identify fusible sequences
    3. Generate Triton kernel automatically:
         @triton.jit
         def fused_softmax_mul_add(...):
             ...
    4. Compile → PTX → SASS → execute

Key insight: You don't always need to write Triton manually.
  torch.compile handles ~80% of fusion opportunities automatically.
  Custom Triton is for the 20% that torch.compile can't optimize
  (e.g., PagedAttention with block tables, custom quantization layouts).
```

### Interview Quick-Reference: Triton

| Question                                 | Answer                                                                                                                              |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| What is OpenAI Triton?                   | Python-based GPU kernel language + compiler. Write at block/tile level; compiler handles thread mapping, shared memory, coalescing. |
| Triton vs CUDA?                          | Triton: 3–5× faster to write, ~90-95% CUDA perf. CUDA: full control, needed for library-grade GEMM/NCCL.                          |
| Where is Triton used in production?      | vLLM (PagedAttention, fused ops), PyTorch Inductor (torch.compile backend), FlashAttention prototyping.                             |
| How does auto-tuning work?               | `@triton.autotune` benchmarks multiple tile/warp configurations on first call, caches optimal choice per input shape.             |
| When NOT to use Triton?                  | GEMM (cuBLAS is better), NCCL collectives (hardware-specific), anything needing inline PTX or warp-level intrinsics.                |
| How does Triton relate to torch.compile? | Inductor backend auto-generates Triton kernels from PyTorch FX graphs. Manual Triton is for ops Inductor can't handle.              |
| What's the memory model?                 | Block-level loads/stores. Compiler decides shared memory usage. User controls tile sizes via `constexpr` params.                  |
| How do you debug Triton?                 | `print()` inside kernel (works!), `tl.device_assert()`, `ncu` for hardware metrics, `TRITON_INTERPRET=1` for CPU emulation. |

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

# LLM & ML INFERENCE MASTERY

> One-stop reference covering transformer internals, serving runtimes, optimization techniques, parallelism strategies, traditional ML inference, and production deployment patterns. Designed to cover ~90% of LLM/ML serving interview needs at the Staff+ level.

---

## Part I: Transformer Architecture for Inference

### The Transformer Block (Inference Perspective)

```
Input Tokens
    │
    ▼
┌──────────────────────────────────────────────────────────────┐
│                  TRANSFORMER LAYER (× N layers)              │
│                                                              │
│   ┌──────────────────────────────────┐                      │
│   │   Multi-Head Self-Attention       │                      │
│   │                                   │                      │
│   │   Q = X·Wq   K = X·Wk   V = X·Wv │  ← Linear projections│
│   │   Attn = softmax(QKᵀ/√d)·V       │  ← O(T²) in prefill │
│   │   Output = Attn·Wo               │                      │
│   └──────────────────────────────────┘                      │
│                    │                                         │
│               [+ Residual] → LayerNorm                      │
│                    │                                         │
│   ┌──────────────────────────────────┐                      │
│   │   Feed-Forward Network (FFN)      │                      │
│   │                                   │                      │
│   │   FFN(x) = GELU(x·W₁)·W₂        │  ← 2/3 of params    │
│   │   (SwiGLU variant in Llama)       │                      │
│   └──────────────────────────────────┘                      │
│                    │                                         │
│               [+ Residual] → LayerNorm                      │
│                    │                                         │
└──────────────────────────────────────────────────────────────┘
    │
    ▼
LM Head → Logits → Sampling → Next Token
```

### Model Anatomy by Size

| Model          | Params | Layers | Heads | Hidden | KV Heads (GQA) | Weights (FP16) | Weights (FP8) |
| -------------- | ------ | ------ | ----- | ------ | -------------- | -------------- | ------------- |
| Llama 3.1 8B   | 8B     | 32     | 32    | 4096   | 8              | ~16 GB         | ~8 GB         |
| Llama 3.1 70B  | 70B    | 80     | 64    | 8192   | 8              | ~140 GB        | ~70 GB        |
| Llama 3.1 405B | 405B   | 126    | 128   | 16384  | 8              | ~810 GB        | ~405 GB       |
| Mixtral 8×7B  | 46.7B  | 32     | 32    | 4096   | 8 (MoE)        | ~93 GB         | ~47 GB        |

### Prefill vs Decode — The Two Phases of LLM Inference

```
┌──────────────────────────────────────┐    ┌──────────────────────────────────────┐
│           PREFILL PHASE              │    │           DECODE PHASE               │
│                                      │    │                                      │
│  Process: All prompt tokens at once  │    │  Process: One token at a time        │
│  Attention: O(T²) — full QKᵀ matrix │    │  Attention: O(T) — append 1 row     │
│  Bound by: COMPUTE (FLOPS)          │    │  Bound by: MEMORY BANDWIDTH          │
│  Output: KV cache for all T tokens  │    │  Output: 1 new token per step        │
│  Metric: TTFT (Time to First Token) │    │  Metric: TPOT (Time Per Output Token)│
│  Parallelism: High (all T at once)  │    │  Parallelism: Low (1 token)          │
│  GPU utilization: High (saturated)  │    │  GPU utilization: Low (batch=1)      │
│                                      │    │                                      │
│  Bottleneck: GEMMs + attention comp │    │  Bottleneck: Loading entire model     │
│              for long sequences      │    │              weights per step         │
└──────────────────────────────────────┘    └──────────────────────────────────────┘
```

**Why this matters for system engineers:**

| Decision                               | Driven By                                                                     |
| -------------------------------------- | ----------------------------------------------------------------------------- |
| When to use chunked prefill            | Prefill O(T²) dominates GPU, blocks decode for other requests                |
| Why increase batch size                | Decode is memory-bound → batching amortizes weight loading                   |
| Why KV cache is critical               | Without it, decode would re-compute all T tokens every step = O(T²) per step |
| Why FP8 helps decode more than prefill | Decode is memory-bound → smaller weights = faster loading                    |
| Why TTFT spikes with longer prompts    | Prefill compute grows quadratically with sequence length                      |

### GPU Memory Budget for LLM Serving

```
H100 80GB Memory Layout (Llama 70B, FP8, TP=4):

┌─────────────────────────────────────────┐
│ Model Weights (FP8):     ~17.5 GB/GPU   │  70GB / 4 GPUs
├─────────────────────────────────────────┤
│ CUDA Context + Activations: ~3 GB       │  Fixed overhead
├─────────────────────────────────────────┤
│ CUDA Graph Workspace:      ~4 GB        │  If graphs enabled
├─────────────────────────────────────────┤
│ KV Cache Pool:             ~55.5 GB     │  ← This determines
│  ├── Per-token KV (FP8):                │     max concurrent
│  │   2 × layers × kv_heads × head_dim  │     requests
│  │   × sizeof(fp8)                      │
│  │   = 2 × 80 × 2 × 128 × 1 byte      │
│  │   = ~40 KB per token per GPU         │
│  ├── At max_model_len=4096:             │
│  │   ~160 MB per request per GPU        │
│  └── Max concurrent: ~346 requests      │
└─────────────────────────────────────────┘
```

**Interview insight:** "When someone says they ran out of KV cache at 60% of expected capacity, the first thing I check is whether CUDA Graph workspace is stealing memory, whether max_model_len is set too high, and whether FP8 KV cache is enabled." (Task 7 from profiling section)

---

## Part II: Attention Mechanisms

### Standard Multi-Head Attention (MHA)

```
MHA: Each head has its own Q, K, V projections

Head 0: Q₀, K₀, V₀  →  Attn₀
Head 1: Q₁, K₁, V₁  →  Attn₁
   ...
Head N: Qₙ, Kₙ, Vₙ  →  Attnₙ

KV Cache Size = 2 × num_layers × num_heads × head_dim × seq_len × dtype_size
```

- Full KV per head → maximum expressiveness but **largest KV cache footprint**

### Multi-Query Attention (MQA)

```
MQA: All heads share ONE K, V set; each head has its own Q

Head 0: Q₀, K_shared, V_shared  →  Attn₀
Head 1: Q₁, K_shared, V_shared  →  Attn₁
   ...
Head N: Qₙ, K_shared, V_shared  →  Attnₙ

KV Cache Size = 2 × num_layers × 1 × head_dim × seq_len × dtype_size
```

- **KV cache reduced by num_heads×** (e.g., 32× for 32-head model)
- Minor quality loss; used in PaLM, Falcon

### Grouped-Query Attention (GQA) — The Modern Standard

```
GQA: Heads grouped; each group shares K, V  (Llama 3, Mistral, Gemma)

Group 0 (heads 0–3):  Q₀,Q₁,Q₂,Q₃, K_g0, V_g0
Group 1 (heads 4–7):  Q₄,Q₅,Q₆,Q₇, K_g1, V_g1
   ...
Group G:              Qₙ₋₃..Qₙ,    K_gG, V_gG

KV Cache Size = 2 × num_layers × num_kv_heads × head_dim × seq_len × dtype_size
```

- **Best tradeoff:** Llama 3.1 70B has 64 Q-heads but only 8 KV-heads → **8× KV cache reduction** vs MHA
- Negligible quality loss; default in all modern LLMs

### FlashAttention (v2/v3)

**Problem:** Standard attention materializes the full $T \times T$ attention matrix in HBM → O(T²) memory.

**FlashAttention insight:** Tile the computation into SRAM-sized blocks, never materializing the full matrix.

```
Standard Attention:
  Q·Kᵀ → [T×T matrix in HBM] → softmax → × V → output
  Memory: O(T²)  |  IO: O(T² × d)

FlashAttention:
  For each block of Q:
    For each block of K, V:
      Compute partial attention in SRAM (shared memory)
      Accumulate with online softmax
      Write only final output to HBM
  Memory: O(T)   |  IO: O(T² × d / SRAM_size)
```

**Impact:**

| Metric            | Standard Attention | FlashAttention v2      |
| ----------------- | ------------------ | ---------------------- |
| Memory            | O(T²)             | O(T)                   |
| Speed (T=2048)    | 1×                | 2–4× faster          |
| Speed (T=8192)    | OOM                | Works                  |
| Exact computation | Yes                | Yes (not approximate!) |

**Key point for interviews:** FlashAttention is **exact** (not an approximation). It computes the same result as standard attention but avoids materializing the full matrix. The speedup comes from reduced HBM reads/writes, not from skipping computation.

### Sliding Window Attention (SWA)

```
Standard:  Each token attends to ALL previous tokens → O(T) KV per token
SWA:       Each token attends to last W tokens only → O(W) KV per token

           Token positions:  1  2  3  4  5  6  7  8  9  10
Standard:  Token 10 attends: 1  2  3  4  5  6  7  8  9  10  (all 10)
SWA(W=4):  Token 10 attends:                   7  8  9  10  (last 4)
```

- Used in Mistral (W=4096) — enables arbitrarily long sequences without linear KV growth
- **KV cache is bounded:** max KV = W × per-token-KV, regardless of sequence length
- Tradeoff: cannot attend to early context beyond window (mitigated by interleaving SWA + full-attention layers)

---

## Part III: Optimization Techniques

### A. Batching Strategies

#### Static Batching (Naive)

```
Batch = [Req1, Req2, Req3, Req4]
All requests padded to max_length
Wait for ALL requests to complete before accepting new batch
→ GPU idle time when short requests finish early
```

- Simple but highly wasteful; suitable only for offline batch inference

#### Dynamic Batching

```
Accumulate requests for T_wait (e.g., 5ms)
Form batch from all accumulated requests
Pad to max length in batch (not global max)
Execute batch
```

- Better than static: batch formation adapts to traffic
- Still wastes GPU when requests finish at different times
- Used in Triton Inference Server, TF Serving

#### Continuous Batching (In-Flight Batching)

```
Time  │ GPU executing
──────┼────────────────────────────────────────
t=0   │ [Req1 step 5][Req2 step 3][Req3 step 1]
t=1   │ [Req1 step 6][Req2 step 4][Req3 step 2][Req4 step 1] ← Req4 joins!
t=2   │ [Req1 DONE]  [Req2 step 5][Req3 step 3][Req4 step 2]
t=3   │ [Req5 step 1][Req2 step 6][Req3 step 4][Req4 step 3] ← Req5 replaces Req1
```

- **No waiting for batch boundaries** — new requests join mid-iteration, completed requests exit immediately
- vLLM, TensorRT-LLM, SGLang all implement this
- **10× throughput improvement** over static batching (Broadcom story: 1K→10K req/sec/GPU)

**Interview insight:** "The difference between dynamic and continuous batching is that dynamic still waits for a batch to form and processes it as a unit. Continuous batching operates at the decode-step level — every decode step is an opportunity to admit new requests or retire completed ones."

### B. KV Cache Management

#### PagedAttention (vLLM)

```
Traditional:                      PagedAttention:
┌──────────────────────┐          ┌────────────┐
│ Request 1: 4096 slots│          │ Block Table│
│ [used: 800]          │          │ Req1 → [B5, B12, B3, ...]
│ [wasted: 3296]       │          │ Req2 → [B8, B15, B1, ...]
└──────────────────────┘          └────────────┘
┌──────────────────────┐    
│ Request 2: 4096 slots│          Physical blocks allocated
│ [used: 1200]         │          on demand (like VM pages)
│ [wasted: 2896]       │          Only 800 blocks for Req1,
└──────────────────────┘          1200 blocks for Req2
                                  Waste: ~0%
Waste: ~75%
```

**Key operations:**

| Operation               | What It Does                                   | When                  |
| ----------------------- | ---------------------------------------------- | --------------------- |
| **Allocate**      | Grab a free physical block                     | New token generated   |
| **Free**          | Return block to free pool                      | Request completes     |
| **Copy-on-Write** | Share blocks between requests with same prefix | Prefix caching        |
| **Swap**          | Move blocks GPU↔CPU                           | Under memory pressure |
| **Preempt**       | Evict lowest-priority request's blocks         | OOM prevention        |

#### Prefix Caching

```
Without prefix caching:
  Req1: [System Prompt (200 tokens)] [User Query A] → compute KV for all
  Req2: [System Prompt (200 tokens)] [User Query B] → recompute same 200 tokens!

With prefix caching:
  First request: compute KV for system prompt → CACHE in KV blocks
  Req2: [CACHED prefix] [User Query B] → skip 200-token prefill!
  Req3: [CACHED prefix] [User Query C] → skip again!
```

- **Savings:** Skip prefill for shared prefix (200 tokens × hundreds of requests)
- vLLM: `--enable-prefix-caching`
- Works with PagedAttention's copy-on-write: cached prefix blocks are shared read-only

#### Prompt Caching (Anthropic / OpenAI Style)

- Server-side: hash the prompt prefix, cache KV states across requests
- Useful for RAG patterns where system prompt + retrieved context is stable across turns
- Reduces cost (fewer input tokens billed) and latency (skip prefill)

### C. Quantization

#### Weight-Only Quantization

```
Full Precision:           Quantized:
W (FP16): 2 bytes/param   W (INT4): 0.5 bytes/param
70B model: 140 GB          70B model: 35 GB (fits 1 GPU!)

Dequantize on-the-fly during GEMM:
  W_fp16 = (W_int4 - zero_point) × scale
```

**Popular methods:**

| Method                    | Bits         | Approach                                   | Quality                     | Speed                       |
| ------------------------- | ------------ | ------------------------------------------ | --------------------------- | --------------------------- |
| **GPTQ**            | 4-bit        | Post-training, layer-wise                  | Good (perplexity +0.1–0.5) | Fast inference              |
| **AWQ**             | 4-bit        | Activation-aware (protect salient weights) | Better than GPTQ            | Fast inference              |
| **SmoothQuant**     | 8-bit (W8A8) | Smooth activation outliers into weights    | Near-FP16                   | Fastest (INT8 Tensor Cores) |
| **FP8 (E4M3/E5M2)** | 8-bit        | Native H100 format                         | Near-FP16                   | Optimal for Hopper          |

#### KV Cache Quantization

- Separate from weight quantization — quantize the cached K, V tensors
- `--kv-cache-dtype fp8` in vLLM → **2× more concurrent requests**
- Risk: precision loss in attention at long sequences (Task 5 from profiling: softmax accumulation in FP8 E4M3 loses accuracy at T>2048)
- Fix: accumulate softmax in BF16, store KV in FP8

#### Activation Quantization

- Quantize intermediate activations (not just weights)
- W8A8: both weights and activations in INT8 → use INT8 Tensor Cores
- Challenge: activation outliers (some channels have values 10-100× larger)
- SmoothQuant: mathematically migrate difficulty from activations to weights

**Interview cheat sheet:**

| Question                          | Answer                                                                                   |
| --------------------------------- | ---------------------------------------------------------------------------------------- |
| "How do you fit 70B on one GPU?"  | FP8 or INT4 quantization (70GB → 35GB in INT4)                                          |
| "Does quantization hurt quality?" | Weight-only INT4: minimal (<0.5 perplexity); KV FP8 at long context: can degrade softmax |
| "GPTQ vs AWQ?"                    | AWQ protects salient weights (activation-aware), slightly better quality                 |
| "FP8 vs INT8?"                    | FP8 is native on Hopper (no dequantize overhead); INT8 on Turing/Ampere                  |

### D. Knowledge Distillation

```
Teacher Model (70B, high quality)
        │
        │ Generate soft labels (probability distributions)
        │ on training data
        ▼
Student Model (8B, fast)
        │
        │ Train to match teacher's soft labels
        │ (+ hard labels from ground truth)
        ▼
Deployed Student: 8B speed with 70B-like quality
```

- **Use case:** Fiserv story — 8B classifier trained via distillation from 70B reasoner
- Typical quality retention: 90–95% of teacher at 5–10× inference speed
- Key: soft labels carry more information than hard labels (inter-class relationships)

### E. Pruning

#### Structured Pruning

```
Before:  [H1][H2][H3][H4][H5][H6][H7][H8]  (8 attention heads)
Prune:   [H1][  ][H3][  ][H5][  ][H7][  ]  (remove 4 heads)
After:   [H1][H3][H5][H7]                    (4 heads, actual speedup)
```

- Remove entire heads, layers, or channels → actual tensor size reduction
- Models remain dense (no sparse matrix operations needed)
- Requires retraining/fine-tuning to recover accuracy

#### Unstructured Pruning

```
Weight matrix:  [0.3  0.01  0.8  0.001]
Pruned (50%):   [0.3  0     0.8  0    ]  ← set small weights to zero
```

- Creates sparse matrices; needs sparse GPU kernels for speedup
- Higher compression but harder to accelerate (sparse × dense GEMM support is limited)
- NVIDIA Ampere+ supports 2:4 structured sparsity (2 zeros in every 4 elements)

### F. Kernel Fusion

```
Without fusion:                        With fusion:
  LayerNorm → write to HBM             ┌─────────────────────┐
  Read from HBM → Bias Add             │ Fused Kernel:       │
  Write to HBM → Activation            │ LayerNorm + Bias +  │
  Read from HBM → next op              │ Activation          │
                                        │ (one HBM read/write)│
4 HBM round-trips                      └─────────────────────┘
                                        1 HBM round-trip
```

- TensorRT-LLM fuses: LayerNorm + bias + activation, QKV projection, attention + softmax
- vLLM uses Triton kernels for fused operations
- **Impact:** 2–3× speedup for element-wise ops (memory-bound → compute-bound)

---

## Part IV: Advanced Optimization Techniques

### Prefill-Decode Disaggregation

**Problem:** Prefill is compute-heavy; decode is memory-bound. On same GPU, prefill blocks decode.

```
Shared GPU:
  Decode: |--tokens--|--tokens--|  BLOCKED  |--tokens--|
  Prefill:                       |--PREFILL--|

P99 TPOT spikes when prefill interrupts decode (Task 6 from profiling)

Disaggregated:
  Prefill GPU:  |--prefill req1--|--prefill req2--|--prefill req3--|
  Decode GPU:   |--decode tokens smoothly, never interrupted--|
                     KV cache transferred via NVLink/network
```

- **vLLM:** `--enable-chunked-prefill` (lightweight disaggregation — chunks prefill into small pieces interleaved with decode)
- **Full disaggregation:** Separate GPU pools for prefill vs decode (used by Splitwise, DistServe)
- Tradeoff: KV cache must be transferred from prefill GPU to decode GPU (NVLink: fast; network: adds latency)

### Speculative Decoding

```
Without speculation:
  Step 1: Run 70B model → token A
  Step 2: Run 70B model → token B
  Step 3: Run 70B model → token C
  Total: 3 × 70B forward passes

With speculation:
  Step 1: Run 8B draft model → tokens A, B, C, D (4 speculative tokens)
  Step 2: Run 70B model on [A, B, C, D] in ONE forward pass (parallel verification)
  Step 3: Accept A, B, C (correct). Reject D → regenerate from 70B
  Total: 1 × 8B pass + 1 × 70B pass for 3 tokens (vs 3 × 70B passes)
```

- **Speedup:** 2–3× for well-matched draft models (draft accepts 60–80% of tokens)
- **Quality:** Mathematically equivalent output (rejection sampling guarantees same distribution)
- **Requirement:** Draft model must be fast and reasonably aligned with target
- **Variants:** Medusa (parallel draft heads on same model), Eagle (feature-level speculation)

### Preemption and Cancellation

**Preemption (vLLM):**

```
High-priority request arrives, GPU fully loaded:
  Option 1: SWAP — move low-priority request's KV cache to CPU RAM
  Option 2: RECOMPUTE — discard KV cache, re-prefill when resources free
```

- vLLM automatically preempts lowest-priority requests when KV cache is full
- Swap is faster to resume (no recompute) but requires CPU memory
- Recompute wastes GPU but doesn't need CPU memory

**Cancellation (from Sentinel Gateway story):**

```
Client disconnects mid-generation:
  Without cancellation: GPU generates 500 more tokens (wasted $0.049/request)
  With cancellation: CancellationToken propagates → backend aborts immediately
  Savings at 10K abandonments/day: ~$478/day
```

- Sentinel implements `CancellationToken` in Rust/tokio — propagates through the full pipeline
- Critical for cost control in production LLM serving

---

## Part V: LLM Serving Runtimes

### Runtime Comparison

| Feature                  | vLLM                            | TensorRT-LLM                   | SGLang                          |
| ------------------------ | ------------------------------- | ------------------------------ | ------------------------------- |
| **Architecture**   | Python + C++ kernels            | C++ compiled engine            | Python + C++                    |
| **Batching**       | Continuous (PagedAttention)     | In-flight batching             | Continuous (RadixAttention)     |
| **KV Cache**       | PagedAttention (block tables)   | Paged KV cache                 | RadixAttention (prefix tree)    |
| **Quantization**   | FP8, INT4 (GPTQ, AWQ)           | FP8, INT4, INT8, W4A16         | FP8, INT4 (GPTQ, AWQ)           |
| **CUDA Graphs**    | Decode phase                    | Full pipeline                  | Decode phase                    |
| **Prefix Caching** | Hash-based block sharing        | Implicit in compiled graphs    | Radix tree (most advanced)      |
| **Parallelism**    | TP, PP                          | TP, PP, EP (MoE)               | TP, DP                          |
| **Best For**       | Variable workloads, flexibility | Fixed shapes, max throughput   | Structured output, prefix-heavy |
| **Weakness**       | Python overhead at high QPS     | Shape changes → recompilation | Younger ecosystem               |

### vLLM Deep Dive

**Key configuration flags (from stories):**

```bash
vllm serve meta-llama/Llama-3.1-70B-Instruct \
  --tensor-parallel-size 4 \           # TP across 4 GPUs (need NVLink)
  --dtype float16 \                     # Model precision
  --quantization fp8 \                  # Weight quantization
  --kv-cache-dtype fp8 \                # KV cache quantization (2× capacity)
  --max-model-len 4096 \                # Max sequence length (affects KV allocation)
  --enable-chunked-prefill \            # Prevent prefill blocking decode
  --enable-prefix-caching \             # Cache shared prefixes (system prompts)
  --gpu-memory-utilization 0.92 \       # How much GPU memory vLLM can use
  --max-num-seqs 128                    # Max concurrent sequences
```

**Key vLLM internals:**

| Component                | What It Does                                                      |
| ------------------------ | ----------------------------------------------------------------- |
| **Scheduler**      | Decides which requests to run, preempt, or swap each step         |
| **Block Manager**  | Allocates/frees physical KV cache blocks (like OS page allocator) |
| **Worker**         | Runs model forward pass on GPU; manages CUDA context              |
| **Tokenizer Pool** | Async tokenization to avoid blocking GPU                          |
| **Engine**         | Coordinates scheduler + workers; handles API requests             |

### TensorRT-LLM Deep Dive

**Key advantage:** Compiled execution — entire model is an optimized TensorRT engine.

```
Model (PyTorch/HF) → TRT-LLM Build → TensorRT Engine (.engine file)
                                        │
                                        ├── All ops fused (LayerNorm+bias+activation)
                                        ├── INT8/FP8 calibrated
                                        ├── CUDA Graphs for full pipeline
                                        └── Static shapes (bucketed)
```

**When to choose TensorRT-LLM over vLLM (Task 8 lesson):**

| Scenario                                | Choose  | Why                                                |
| --------------------------------------- | ------- | -------------------------------------------------- |
| Fixed prompt lengths (e.g., always 512) | TRT-LLM | Compiled graphs optimal; no recompilation          |
| Variable prompt lengths (100–4000)     | vLLM    | Eager mode handles dynamic shapes without overhead |
| Maximum throughput, fixed workload      | TRT-LLM | Compiled kernels are faster per-op                 |
| Rapid experimentation, new models       | vLLM    | No build step; load HF model directly              |
| Hybrid workload                         | Both    | Route fixed-shape to TRT-LLM, variable to vLLM     |

### SGLang and RadixAttention

**RadixAttention:** Organizes all KV cache entries in a radix tree (prefix tree).

```
                    [System Prompt KV]
                    /                \
          [User query A KV]    [User query B KV]
          /          \
   [Follow-up 1]  [Follow-up 2]

Any request sharing a prefix reuses cached KV blocks automatically.
No explicit "enable prefix caching" — it's the default data structure.
```

- **Best for:** Multi-turn chat, structured output (JSON mode), tool-use patterns where prefix sharing is high
- Achieves highest prefix cache hit rates of any runtime

---

## Part VI: Parallelism Strategies

### Tensor Parallelism (TP)

```
Single GPU:          W (full weight matrix)
                     [4096 × 4096]

TP=4 (split columns):
GPU 0: W[:, 0:1024]     GPU 1: W[:, 1024:2048]
GPU 2: W[:, 2048:3072]  GPU 3: W[:, 3072:4096]

Each GPU computes partial result → AllReduce to combine
```

- **Communication:** AllReduce after every attention + MLP layer
- **Requirement:** Fast interconnect (NVLink). PCIe is ~10× slower → kills performance (Task 4)
- **Scaling:** TP=2 (minimal communication), TP=4 (good on NVLink mesh), TP=8 (DGX/HGX only)
- **Rule:** Only use TP across GPUs connected by NVLink

**Cost per decode step (from Task 4):**

```
TP=4 with full NVLink mesh:
  AllReduce: 80 layers × 2 per layer = 160 AllReduces
  NVLink AllReduce: ~0.1 ms each → 16 ms total
  
TP=4 with PCIe between some pairs:
  Some AllReduce via PCIe: ~0.8 ms each → 128 ms total  ← 8× worse!
```

### Pipeline Parallelism (PP)

```
PP=4 (split by layers):
GPU 0: Layers  0–19   →  activations sent to GPU 1
GPU 1: Layers 20–39   →  activations sent to GPU 2
GPU 2: Layers 40–59   →  activations sent to GPU 3
GPU 3: Layers 60–79   →  output
```

- **Communication:** Point-to-point (send activations to next GPU), NOT AllReduce
- **Works over PCIe:** Only sends one activation tensor between stages (not weight-sized)
- **Problem:** Pipeline bubbles — GPU 0 idles while GPU 3 computes
- **Fix:** Micro-batching fills the pipeline (multiple requests in flight)

### Data Parallelism (DP)

```
DP=4 (replicate model):
GPU 0: Full model copy → processes Batch 0
GPU 1: Full model copy → processes Batch 1
GPU 2: Full model copy → processes Batch 2
GPU 3: Full model copy → processes Batch 3
```

- **No communication during inference** (each GPU independent)
- **Requirement:** Model must fit on one GPU (or combined with TP)
- **Best for:** High-throughput serving with model replicas
- **Scaling:** Linear throughput scaling with GPU count

### Expert Parallelism (EP) — For MoE Models

```
Mixtral 8×7B (Mixture of Experts):
Each layer has 8 expert FFNs; router selects top-2 per token

EP=4:
GPU 0: Experts 0, 1     GPU 1: Experts 2, 3
GPU 2: Experts 4, 5     GPU 3: Experts 6, 7

Token routing: router decides which GPU(s) process each token
→ All-to-all communication for routing tokens to correct expert GPU
```

- TensorRT-LLM supports EP natively
- Challenge: load imbalance (some experts activated more than others)

### Combined Strategies

```
405B model on 8× H100 (NVLink mesh):

Option A: TP=8
  Each GPU gets 1/8 of every layer
  160 AllReduces per decode step → communication overhead

Option B: TP=4 × PP=2
  GPUs 0–3: Layers 0–62 (TP=4 within, NVLink)
  GPUs 4–7: Layers 63–125 (TP=4 within, NVLink)
  Only 1 point-to-point transfer between PP stages per step

Option C: TP=2 × DP=4 (if model fits in 2 GPUs)
  4 replicas of TP=2 pairs → 4× throughput
  No inter-replica communication
```

**Decision framework:**

| Model Size            | GPUs | Strategy             | Why                               |
| --------------------- | ---- | -------------------- | --------------------------------- |
| 8B (FP8 = 8GB)        | 1    | None (single GPU)    | Fits easily                       |
| 70B (FP8 = 70GB)      | 1    | INT4 quantization    | Fits on 80GB                      |
| 70B (FP16 = 140GB)    | 2    | TP=2 (NVLink pair)   | Minimal communication             |
| 70B (FP16)            | 4    | TP=4 (NVLink mesh)   | Standard deployment               |
| 405B                  | 8    | TP=4 × PP=2         | Balance communication vs pipeline |
| 405B (max throughput) | 16   | TP=4 × PP=2 × DP=2 | 2 full replicas                   |

---

## Part VII: Traditional ML Model Inference

> Not all inference is LLM inference. Fraud scoring, security classification, and risk assessment use tree models, ensembles, and small neural networks at microsecond latency.

### Model Types and Latency Targets

| Model Type                      | Typical Latency   | Use Case (from stories)                              | Serving Pattern                   |
| ------------------------------- | ----------------- | ---------------------------------------------------- | --------------------------------- |
| XGBoost / GBDT                  | 50–200 µs       | CapitalOne fraud scoring, Broadcom malware detection | CPU-native, SIMD-optimized        |
| Logistic Regression / Scorecard | 10–50 µs        | CapitalOne regulatory scorecard                      | CPU, often hand-tuned             |
| Small Neural Network (MLP)      | 50–500 µs       | CapitalOne GPU MLP scorer                            | GPU + CUDA Graphs                 |
| BERT / DistilBERT               | 5–30 ms          | Broadcom URL classification, Apple NLU               | GPU (TensorRT / ONNX RT)          |
| CNN (image/binary)              | 10–50 ms         | Broadcom malware detection                           | GPU (TF Serving / TRT)            |
| Ensemble (8–20 models)         | < 1 ms (parallel) | CapitalOne Tier 1                                    | All models read same FeatureBlock |

### Serving Patterns

#### Pattern 1: CPU Ensemble (CapitalOne)

```
FeatureBlock (cacheline-aligned, all features)
    │
    ├── XGBoost Model 1  (< 100µs)
    ├── XGBoost Model 2  (< 100µs)
    ├── GBDT Model 1     (< 100µs)
    ├── Logistic Scorecard (< 50µs)
    ├── Rule Engine       (< 50µs)
    └── Velocity Checker  (< 50µs)
    │
    ▼
Weighted Ensemble Score → GO / NO_GO decision
Total: < 1 ms for all models
```

- All models read from **same FeatureBlock** — no per-model data conversion
- Per-core workers — no cross-core contention
- Arena allocation — zero malloc in hot path

#### Pattern 2: GPU + CPU Hybrid (CapitalOne)

```
CPU Path (< 1 ms):                    GPU Path (< 500µs):
  XGBoost ensemble                      MLP classifier
  Rule engine                           via CUDA Graph
  Velocity checker                      (pinned buffers)
                    \                  /
                     ↘              ↙
                   Merge → Final Score → Decision
```

- CPU and GPU score **in parallel** — GPU path uses CUDA Graph, CPU path uses SIMD-optimized tree traversal
- GPU overhead (without graphs): 150µs launch overhead; with graphs: 5µs

#### Pattern 3: Multi-Model GPU Pipeline (Broadcom)

```
RequestContext (1MB shared arena)
    │
    ├── URL Classifier (BERT + GBRT)      → GPU (TF Serving)    < 15ms
    ├── Malware Detector (XGBoost + CNN)  → CPU + GPU           < 20ms
    ├── DLP Engine (NER + Patterns)       → CPU (ONNX RT)       < 25ms
    ├── Content Analyzer (NLP + CV)       → GPU (TF Serving)    < 30ms
    ├── Behavioral Analyzer (Time-series) → CPU                 < 10ms
    └── Threat Intel (Graph + Rules)      → CPU                 < 5ms
    │
    ▼  (parallel execution, early-exit on high confidence)
Decision Engine → ALLOW / BLOCK / WARN / QUARANTINE
Total: < 100ms p99
```

### Tree Model Optimization (XGBoost / GBDT)

| Technique                        | What                                           | Impact                                       |
| -------------------------------- | ---------------------------------------------- | -------------------------------------------- |
| **AVX2/AVX-512 traversal** | Process 8–16 trees in parallel using SIMD     | 3–5× speedup                               |
| **Branch-free comparison** | Conditional moves instead of branches          | Eliminates mispredictions                    |
| **Flat array layout**      | Trees as contiguous arrays (not pointer-based) | Cache-friendly traversal                     |
| **Quantized thresholds**   | INT16 thresholds instead of FP32               | Smaller tree, more in L1 cache               |
| **Custom C++ engine**      | Skip Python/library overhead                   | <5ms for 500-tree ensemble (vs 20ms library) |

### Serving Infrastructure Comparison

| Framework                         | Best For                      | Latency     | Languages   | GPU Support                |
| --------------------------------- | ----------------------------- | ----------- | ----------- | -------------------------- |
| **ONNX Runtime**            | Cross-framework portability   | Medium      | C++, Python | Yes (TensorRT backend)     |
| **TensorFlow Serving**      | TF models at scale            | Medium      | C++         | Yes (batching, versioning) |
| **Triton Inference Server** | Multi-model orchestration     | Low         | C++         | Yes (ensemble DAGs)        |
| **Custom C++ engine**       | Ultra-low-latency tree models | Lowest      | C++         | Optional                   |
| **TorchServe**              | PyTorch models                | Medium-High | Python/Java | Yes                        |

---

## Part VIII: Production Deployment Patterns

### Model Deployment Checklist

```
□ Model format chosen (ONNX / TensorRT / HF / custom)
□ Quantization applied and validated (quality + latency)
□ Memory budget calculated (weights + KV cache + activations + CUDA workspace)
□ Parallelism strategy decided (TP / PP / DP)
□ Batch size / max_model_len tuned for workload
□ Prefix caching evaluated (shared system prompts?)
□ GPU topology verified (NVLink mesh for TP)
□ Health checks configured (GPU + model + pod)
□ Autoscaling metrics selected (GPU util + queue depth + KV pressure)
□ Circuit breakers in place (per-backend, per-model)
□ Admission control configured (token budget + GPU headroom)
□ Monitoring dashboards live (TTFT, TPOT, P99, GPU util, KV usage)
□ Rollback procedure tested (blue-green swap)
```

### Guardrail + LLM Pipeline Optimization

**The Python tax problem:**

```
Traditional:
  Request → [Python] Input Guardrail → [Python] Tokenizer → [GPU] LLM →
  [Python] Detokenizer → [Python] Output Guardrail → Response
  
  Tax: 6 ser/deser hops, 4 CPU↔GPU copies, Python GIL on every hop
  Overhead: 15–40 ms (excluding LLM compute)
```

**Solution architecture (from stories):**

```
Client → Rust Gateway (tokenization + rule guardrails + rate limiting)
                │ preprocessed token IDs (gRPC)
                ▼
         Triton Ensemble (all on GPU, zero Python):
           ├── ML Input Guardrail (TensorRT) ─ tensors on GPU ─→
           ├── LLM (TensorRT-LLM / vLLM)   ─ tensors on GPU ─→
           └── ML Output Guardrail (TensorRT)
                │ generated token IDs (gRPC)
                ▼
       Rust Gateway (detokenization + formatting + audit)
                │
                ▼
              Client

Overhead: < 2 ms (vs 15–40 ms)
```

| Approach                       | Overhead  | Best For            |
| ------------------------------ | --------- | ------------------- |
| Python microservices           | 15–40 ms | Prototyping         |
| Python monolith                | 8–15 ms  | Small scale         |
| Triton ensemble (GPU-resident) | 2–5 ms   | ML-based guardrails |
| Rust gateway + Triton          | < 2 ms    | Production at scale |

### GPU Fleet Cost Optimization

| Technique                  | Savings                       | How                                    |
| -------------------------- | ----------------------------- | -------------------------------------- |
| FP8 quantization           | 2× throughput                | Same GPU, half the memory per token    |
| KV cache FP8               | 2× concurrent requests       | `--kv-cache-dtype fp8`               |
| Prefix caching             | 30–70% prefill reduction     | `--enable-prefix-caching`            |
| Cancellation propagation   | ~$500/day at 10K abandonments | Abort backend on client disconnect     |
| Right-sizing max_model_len | 2–4× more requests          | Match max_model_len to actual workload |
| Session-affine routing     | 70% KV cache hit rate         | Sticky routing for multi-turn          |
| Hierarchical models        | 60% token reduction           | 8B classifier → 70B only when needed  |
| MIG partitioning           | Better GPU utilization        | Split H100 for small models (8B)       |

### Key Metrics to Monitor in Production

| Metric             | Source          | Alert Threshold | Action                                  |
| ------------------ | --------------- | --------------- | --------------------------------------- |
| TTFT p99           | vLLM metrics    | > SLO target    | Scale up, enable chunked prefill        |
| TPOT p99           | vLLM metrics    | > 50ms          | Check prefill interference, KV pressure |
| GPU utilization    | DCGM            | < 50% or > 95%  | Scale down / scale up                   |
| KV cache usage     | vLLM metrics    | > 90%           | Enable FP8 KV, reduce max_model_len     |
| Preemption count   | vLLM metrics    | > 10/min        | Increase capacity or reduce load        |
| Queue depth        | Gateway metrics | > 50            | Scale up or shed low-priority traffic   |
| Request errors     | Gateway metrics | > 1%            | Check circuit breakers, GPU health      |
| Model loading time | Custom          | > 60s           | Use NVMe cache, GPUDirect Storage       |

---

## Part IX: Interview Quick-Reference for LLM/ML Inference

### Top 15 Questions and 1-Line Answers

| #  | Question                               | Answer                                                                                                                  |
| -- | -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| 1  | Prefill vs decode?                     | Prefill is compute-bound O(T²); decode is memory-bound O(T). Different optimization strategies for each.               |
| 2  | What is PagedAttention?                | OS-style virtual memory for KV cache: non-contiguous block allocation, copy-on-write, swap to CPU.                      |
| 3  | Continuous vs dynamic batching?        | Dynamic batches requests then processes as unit; continuous inserts/removes requests at every decode step.              |
| 4  | When to use TP vs PP?                  | TP for NVLink-connected GPUs (AllReduce); PP for PCIe (point-to-point only).                                            |
| 5  | How does FlashAttention work?          | Tiles attention computation into SRAM-sized blocks; never materializes full T×T matrix in HBM. Exact, not approximate. |
| 6  | GQA vs MHA?                            | GQA shares K,V across head groups (e.g., 8 KV-heads for 64 Q-heads). 8× less KV cache, negligible quality loss.        |
| 7  | GPTQ vs AWQ?                           | Both INT4. AWQ is activation-aware (protects salient weights), slightly better quality.                                 |
| 8  | Why does FP8 fail at long context?     | Softmax accumulation loses precision with 3-bit mantissa (E4M3) at T>2048. Fix: accumulate in BF16.                     |
| 9  | Speculative decoding?                  | Draft model generates N tokens; target model verifies in one pass. 2–3× speedup, mathematically equivalent output.    |
| 10 | How to handle prefill blocking decode? | Chunked prefill (break prefill into small pieces) or disaggregation (separate GPUs).                                    |
| 11 | vLLM vs TensorRT-LLM?                  | vLLM for variable workloads (eager mode); TRT-LLM for fixed shapes (compiled, faster per-op).                           |
| 12 | How to fit 405B on 8 GPUs?             | TP=4 × PP=2 with FP8 quantization. TP within NVLink groups, PP across.                                                 |
| 13 | Prefix caching benefit?                | Skip prefill for shared prompt prefix. With system prompts: 30–70% prefill compute saved.                              |
| 14 | KV cache OOM at 60% capacity?          | Check: max_model_len over-allocation, CUDA Graph workspace stealing memory, FP8 KV not enabled.                         |
| 15 | Why custom C++ for XGBoost?            | Library has Python overhead + batch API. Custom: AVX2 SIMD (8 trees parallel), branch-free, <5ms for 500 trees vs 20ms. |

### System Design: "Design an LLM Serving Platform"

```
1. REQUIREMENTS (30 sec)
   Latency SLO? Model size? Throughput? Availability?

2. ARCHITECTURE (2 min)
   ┌── Control Plane (Rust/Go): routing, admission, circuit breakers
   ├── Data Plane: GPU inference (vLLM/TRT-LLM)
   └── Observability: Prometheus + DCGM + custom dashboards

3. KEY DECISIONS (3 min)
   Runtime: vLLM (variable) vs TRT-LLM (fixed shapes)
   Parallelism: TP (NVLink) vs PP (PCIe)
   Quantization: FP8 weights + FP8 KV cache (2× capacity)
   Batching: Continuous batching (vLLM default)
   Caching: Prefix caching for shared system prompts

4. OPTIMIZATIONS (2 min)
   Chunked prefill (protect decode latency)
   Session-affine routing (KV cache reuse)
   Hierarchical models (8B fast → 70B when needed)
   Cancellation propagation (save GPU cost)

5. FAILURE HANDLING (1 min)
   OOM → admission control with token budget
   Latency spike → circuit breaker + fallback
   GPU failure → health checks + auto-replacement
   Model update → blue-green deployment
```

---

# SYSTEMS PROGRAMMING PATTERNS

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

# INFRASTRUCTURE & PLATFORM GUIDE

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

### Storage I/O

| Setting           | Command                                              | Purpose                                                 |
| ----------------- | ---------------------------------------------------- | ------------------------------------------------------- |
| I/O scheduler     | `echo none > /sys/block/nvme0n1/queue/scheduler`   | Disable scheduler for NVMe (already has internal queue) |
| Read-ahead        | `blockdev --setra 4096 /dev/nvme0n1`               | 2MB readahead for sequential model loading              |
| nr_requests       | `echo 1024 > /sys/block/nvme0n1/queue/nr_requests` | Deep queue for parallel I/O                             |
| Lustre readahead  | `lctl set_param llite.*.max_read_ahead_mb=256`     | Large prefetch for sequential model reads               |
| Lustre RPCs       | `lctl set_param osc.*.max_rpcs_in_flight=32`       | Maximize parallel I/O to OSTs                           |
| GDS enable        | `modprobe nvidia_fs`                               | Load GPUDirect Storage kernel module                    |
| Page cache bypass | `O_DIRECT` in application                          | Avoid polluting page cache with model weights           |
| io_uring          | Application-level (liburing)                         | Async batched I/O for parallel shard loading            |

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

### Storage Workload Mapping

| Workload                       | Storage Choice                          | Why                                           |
| ------------------------------ | --------------------------------------- | --------------------------------------------- |
| Model weights (serving)        | Local NVMe (or cached from GPFS/Lustre) | Fast loading; avoid network I/O on startup    |
| Model weights (shared cluster) | GPFS / Lustre → NVMe cache tier        | Single source of truth; local cache for speed |
| KV cache                       | GPU HBM (managed by vLLM)               | Must be in GPU memory                         |
| Checkpoints (training)         | GPFS / Lustre (parallel write)          | Saturate aggregate bandwidth; fault tolerance |
| Checkpoints (cloud)            | S3/GCS with local NVMe staging          | Durable; stage locally for fast resume        |
| Training data                  | Lustre / GPFS / WekaFS                  | Parallel reads across thousands of GPUs       |
| Logs/metrics                   | EBS/PD → shipped to object store       | Cost-effective long-term                      |
| Feature cache                  | Redis / in-memory                       | Sub-millisecond reads                         |
| Scratch / temp                 | Node-local NVMe (tmpfs for tiny)        | Zero network hops; ephemeral                  |

---

### HPC Parallel Filesystems

> In HPC-style GPU clusters, storage I/O is often the bottleneck that limits model loading time, checkpoint frequency, and data pipeline throughput. Parallel filesystems solve this by striping data across many Object Storage Targets (OSTs).

#### Architecture Comparison

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         LUSTRE ARCHITECTURE                             │
│                                                                         │
│   Clients (compute nodes with GPU)                                     │
│     │         │         │         │                                     │
│     └────┬────┴────┬────┴────┬────┘                                     │
│          │         │         │     ← High-speed network (IB/RoCE)      │
│     ┌────┴────┐ ┌──┴──┐ ┌───┴───┐                                      │
│     │  MDS    │ │ OSS │ │  OSS  │   MDS = Metadata Server              │
│     │(metadata)│ │     │ │       │   OSS = Object Storage Server        │
│     └─────────┘ │ OST │ │  OST  │   OST = Object Storage Target (disk) │
│                 │ OST │ │  OST  │                                       │
│                 └─────┘ └───────┘                                       │
│                                                                         │
│   File striped across OSTs → parallel I/O from all OSS simultaneously  │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                         GPFS (Spectrum Scale) ARCHITECTURE              │
│                                                                         │
│   Clients (compute nodes with GPU)                                     │
│     │         │         │         │                                     │
│     └────┬────┴────┬────┴────┬────┘                                     │
│          │         │         │     ← High-speed network (IB/RoCE)      │
│     ┌────┴────────────────────┐                                         │
│     │    NSD Servers           │   NSD = Network Shared Disk            │
│     │  (serve disk blocks      │   All nodes can be NSD servers         │
│     │   over network)          │   (symmetric architecture)             │
│     └──┬──────┬──────┬────────┘                                         │
│        │      │      │                                                  │
│     [Disk] [Disk] [Disk]  (NVMe/SSD/HDD arrays)                       │
│                                                                         │
│   Distributed lock manager → byte-range locking for concurrent access  │
│   Token-based caching → client-side read cache for repeated reads      │
└─────────────────────────────────────────────────────────────────────────┘
```

#### Detailed Comparison

| Feature                     | Lustre                                 | GPFS (Spectrum Scale)               | WekaFS                             | BeeGFS                        |
| --------------------------- | -------------------------------------- | ----------------------------------- | ---------------------------------- | ----------------------------- |
| **Architecture**      | Asymmetric (MDS + OSS)                 | Symmetric (any node = server)       | Distributed (all-flash optimized)  | Asymmetric (meta + storage)   |
| **Max bandwidth**     | 2+ TB/s (large clusters)               | 2+ TB/s                             | 1+ TB/s (NVMe-native)              | 500+ GB/s                     |
| **Metadata**          | Dedicated MDS (bottleneck risk)        | Distributed across nodes            | Distributed                        | Dedicated meta servers        |
| **Locking**           | LDLM (limited)                         | Distributed token-based             | POSIX-compliant                    | Relaxed POSIX                 |
| **Small file perf**   | Weak (metadata bottleneck)             | Better (distributed metadata)       | Excellent (flash-native)           | Good                          |
| **POSIX compliance**  | Full                                   | Full                                | Full                               | Partial                       |
| **Tiering**           | HSM (Lustre/HSM)                       | Policy-based ILM                    | Auto-tiering (NVMe→SSD)           | Manual                        |
| **GPUDirect Storage** | Yes (GDS plugin)                       | Yes (GDS plugin)                    | Yes (native)                       | Limited                       |
| **Cloud support**     | On-prem mostly                         | IBM Cloud + hybrid                  | AWS/Azure/GCP native               | On-prem mostly                |
| **Best for**          | Large HPC clusters, training at scale  | Enterprise HPC, mixed workloads     | All-flash AI clusters, low-latency | Mid-size clusters, easy setup |
| **Used by**           | Most TOP500 sites, NVIDIA DGX SuperPOD | IBM HPC, financial services, genome | AI startups, cloud-adjacent HPC    | European HPC sites            |

#### Lustre Tuning for AI/LLM Workloads

```bash
# Stripe across all available OSTs for large model files
lfs setstripe -c -1 -S 4M /lustre/models/llama-405b/
#   -c -1   → use ALL OSTs (maximum parallelism)
#   -S 4M   → 4MB stripe size (matches typical read pattern)

# Check current striping
lfs getstripe /lustre/models/llama-405b/model-00001-of-00082.safetensors

# For checkpoint writes (large sequential)
lfs setstripe -c 32 -S 16M /lustre/checkpoints/
#   Larger stripe (16M) for sequential write throughput

# For small metadata-heavy dirs (tokenizer configs, etc.)
lfs setstripe -c 1 /lustre/models/tokenizer/
#   Single OST avoids metadata overhead for small files

# Monitor OST balance (avoid hotspots)
lfs df -h    # Check OST utilization
lctl get_param osc.*.stats  # I/O stats per OST

# Client-side tuning
lctl set_param llite.*.max_read_ahead_mb=256   # Prefetch for sequential
lctl set_param osc.*.max_pages_per_rpc=4096    # Larger RPCs
lctl set_param osc.*.max_rpcs_in_flight=32     # More parallel I/O
```

#### GPFS (Spectrum Scale) Tuning for AI Workloads

```bash
# Set large block size for model weight files (up to 16MB)
mmchfs /dev/gpfs_models -B 16M

# Create fileset for LLM models with optimized policy
mmcrfileset gpfs_models llm_weights --inode-space=new

# Policy-based tiering: hot models on NVMe, cold on HDD
mmapplypolicy gpfs_models -P /etc/gpfs/ai_tiering.policy
#   RULE: IF access_age < 1 day THEN NVMe_pool
#   RULE: IF access_age > 7 days THEN HDD_pool

# Prefetch hint for model loading
mmfsd: prefetchAggressiveness=2  # Aggressive readahead

# Tune for large sequential I/O (model loading)
mmchconfig maxMBpS=8000 maxFilesToCache=10000
mmchconfig prefetchPct=80  # Use 80% of cache for prefetch

# Monitor throughput
mmpmon -p  # Real-time I/O monitoring
mmdiag --iostats  # Detailed I/O diagnostics
```

---

### GPUDirect Storage (GDS)

> GPUDirect Storage bypasses the CPU and system memory entirely — DMA transfers data directly from NVMe/NFS/Lustre into GPU HBM.

```
Traditional I/O Path (bounce buffer):
  Storage → PCIe → CPU/System RAM → PCIe → GPU HBM
  Latency: ~500 µs for 1 GB  |  CPU involved (copies, interrupts)

GPUDirect Storage Path:
  Storage → PCIe/NVLink → GPU HBM (direct DMA)
  Latency: ~200 µs for 1 GB  |  CPU free for other work

Bandwidth comparison (H100 + NVMe array):
  Traditional:  ~6 GB/s (CPU bottleneck on memcpy)
  GDS:          ~25 GB/s (saturates PCIe Gen5 x16)
```

**When GDS matters:**

| Scenario                            | Without GDS            | With GDS                  | Impact                    |
| ----------------------------------- | ---------------------- | ------------------------- | ------------------------- |
| Load 70B FP8 model (70GB) to 4 GPUs | ~12s (CPU bounce)      | ~3s (direct DMA)          | 4× faster cold start     |
| Checkpoint 405B model (200GB)       | ~35s                   | ~9s                       | More frequent checkpoints |
| Stream training data to GPU         | CPU saturated at 8 GPU | CPU free, scales linearly | Enables larger clusters   |
| KV cache swap to NVMe (future)      | 2-hop latency          | 1-hop, GPU-initiated      | Feasible for overflow     |

**Configuration:**

```bash
# Verify GDS support
/usr/local/cuda/gds/tools/gdscheck -p

# Mount filesystem with GDS enabled
mount -t lustre -o gds 10.0.0.1@tcp:/lustre /mnt/lustre_gds

# For GPFS
mmchconfig gdsEnabled=yes

# Verify in application
cuFileDriverOpen()    # Initialize GDS driver
cuFileRead()          # Direct GPU read (bypasses CPU)
cuFileBufRegister()   # Register GPU buffer for DMA
```

---

### Storage Tiering for LLM Serving

```
┌─────────────────────────────────────────────────────────────────┐
│                    STORAGE TIERING STRATEGY                      │
│                                                                  │
│  Tier 0: GPU HBM (3.35 TB/s)                                   │
│    └── Active KV cache, model weights (loaded)                  │
│                                                                  │
│  Tier 1: Local NVMe (7–14 GB/s per drive, ~25 GB/s with GDS)   │
│    └── Cached model weights, checkpoint staging, KV overflow    │
│                                                                  │
│  Tier 2: GPFS / Lustre over RDMA (50–200 GB/s aggregate)       │
│    └── Shared model repository, training datasets, checkpoints  │
│                                                                  │
│  Tier 3: Object Store — S3/GCS (1–10 GB/s per client)          │
│    └── Cold models, archived checkpoints, raw datasets          │
│                                                                  │
│  Model Loading Path:                                            │
│    Cold start: Tier 3 → Tier 2 → Tier 1 → Tier 0              │
│    Warm start: Tier 1 → Tier 0 (NVMe cache hit)                │
│    Hot swap:   Tier 0 (already loaded, blue-green on GPU)       │
└─────────────────────────────────────────────────────────────────┘
```

**Cache warming strategy (from production):**

```bash
# Pre-stage model to NVMe before pod scheduling (DaemonSet)
#!/bin/bash
MODEL_PATH="/gpfs/models/llama-3.1-70b-fp8"
NVME_CACHE="/nvme/model-cache/llama-3.1-70b-fp8"

if [ ! -d "$NVME_CACHE" ]; then
  # Parallel copy from GPFS to NVMe (saturate NVMe bandwidth)
  tar -C "$MODEL_PATH" -cf - . | tar -C "$NVME_CACHE" -xf -
  # Or with multiple streams:
  find "$MODEL_PATH" -name '*.safetensors' | \
    xargs -P 8 -I {} cp {} "$NVME_CACHE/"
fi

# Signal readiness to scheduler
touch /nvme/model-cache/.ready
```

---

### Checkpoint I/O Patterns (Training & Fine-Tuning)

> For large model training, checkpoint I/O dominates storage requirements. A 405B model in FP32 = ~1.6 TB per checkpoint. Without parallel I/O, checkpointing stalls training.

#### Distributed Checkpoint Strategies

| Strategy                               | How                                 | Bandwidth                            | Use Case                                |
| -------------------------------------- | ----------------------------------- | ------------------------------------ | --------------------------------------- |
| **Naive (single writer)**        | Rank 0 gathers all → writes        | Limited by 1 node's NVMe (~7 GB/s)   | Small models (<10B)                     |
| **Parallel write (1 file/rank)** | Each rank writes its shard          | N × 7 GB/s (scales with ranks)      | Standard distributed training           |
| **Lustre striped**               | Single file, striped across OSTs    | Aggregate OST bandwidth (~100+ GB/s) | Large shared checkpoints                |
| **GPFS parallel**                | mmfsd handles parallelism           | Aggregate NSD bandwidth              | Enterprise HPC                          |
| **Async checkpoint**             | Copy to CPU RAM → background write | Training not blocked                 | When checkpoint time > acceptable pause |
| **Incremental/delta**            | Only save changed layers            | 10–50% of full size                 | Frequent saves, LoRA fine-tuning        |

#### Checkpoint I/O Example (PyTorch FSDP + Lustre)

```python
import torch.distributed.checkpoint as dcp
from torch.distributed.checkpoint.filesystem import FileSystemWriter

# Parallel checkpoint write — each rank writes its shard
writer = FileSystemWriter(
    path="/lustre/checkpoints/llama-70b/step-10000",
    single_file_per_rank=True,     # One file per rank (max parallelism)
    sync_files=False,              # Don't fsync each file (Lustre handles)
    thread_count=4,                # Parallel writes within rank
)

dcp.save(state_dict={"model": model.state_dict()}, storage_writer=writer)

# Lustre setup for checkpoint dir:
# lfs setstripe -c 16 -S 16M /lustre/checkpoints/
# → Each rank's file striped across 16 OSTs with 16MB chunks
```

---

### I/O Optimization for Model Loading

| Technique                        | What                                 | Impact                                        | When                                   |
| -------------------------------- | ------------------------------------ | --------------------------------------------- | -------------------------------------- |
| **mmap + MAP_POPULATE**    | Prefault all pages on load           | Avoids page faults during inference           | Safetensors format                     |
| **O_DIRECT**               | Bypass page cache                    | Avoid polluting OS cache; predictable latency | Dedicated model server                 |
| **io_uring**               | Async I/O with ring buffers          | Batch I/O submissions; reduce syscalls        | High-throughput loading                |
| **Parallel shard loading** | Load model shards concurrently       | N× speedup for sharded models                | Multi-file models (82 shards for 405B) |
| **NUMA-local I/O**         | Read into memory on same NUMA as GPU | Avoid cross-socket copies on pin              | Multi-socket servers                   |
| **GDS (cuFileRead)**       | Bypass CPU entirely                  | NVMe → GPU direct; frees CPU                 | GPU-heavy workloads                    |
| **Prefetch/readahead**     | OS or app-level readahead            | Hide I/O latency behind compute               | Sequential model loading               |

**Model loading timeline optimization:**

```
Naive loading (70B FP8 = 70GB, single NVMe):
  Read from NVMe:     70 GB / 7 GB/s  = 10.0 s
  CPU→GPU copy:       70 GB / 25 GB/s =  2.8 s  (PCIe Gen5)
  Total:              ~12.8 s

Optimized loading (4× NVMe RAID0 + GDS + parallel shards):
  Read via GDS:       70 GB / 25 GB/s =  2.8 s  (direct to GPU)
  No CPU copy:        0 s
  Total:              ~2.8 s  (4.6× faster)

With NVMe cache warm (already on local NVMe):
  Skip network:       0 s
  GDS to GPU:         2.8 s
  Total:              ~2.8 s (vs 12.8s cold from Lustre)

With Lustre/GPFS (cold, no local cache):
  Network read:       70 GB / 50 GB/s  = 1.4 s  (aggregate from 32 OSTs)
  CPU→GPU copy:       70 GB / 25 GB/s  = 2.8 s
  Total:              ~4.2 s (parallel filesystem advantage)
```

---

### Storage Interview Quick-Reference

| # | Question                             | Answer                                                                                                                                        |
| - | ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 | Lustre vs GPFS?                      | Lustre: asymmetric (dedicated MDS), best for large sequential I/O. GPFS: symmetric (any node = server), better metadata, token-based locking. |
| 2 | Why not just NFS for GPU clusters?   | NFS is single-server; can't stripe files across targets. Max ~3 GB/s vs Lustre/GPFS at 100+ GB/s aggregate.                                   |
| 3 | What is GPUDirect Storage?           | DMA path from storage directly to GPU HBM, bypassing CPU bounce buffer. 3–4× faster model loads.                                            |
| 4 | How to speed up 405B model load?     | Shard across 8 files + 4× NVMe RAID0 + GDS + parallel reads. Or pre-cache on local NVMe (warm start).                                        |
| 5 | Checkpoint I/O bottleneck?           | Use parallel writes (1 file/rank), stripe on Lustre (-c -1 -S 16M), async checkpointing to avoid stalling training.                           |
| 6 | Lustre stripe settings for models?   | `-c -1 -S 4M` (all OSTs, 4MB stripes) for large files. `-c 1` for small metadata files.                                                   |
| 7 | Storage tiering strategy?            | Tier 0: GPU HBM → Tier 1: Local NVMe (cache) → Tier 2: GPFS/Lustre (shared) → Tier 3: Object store (cold).                                 |
| 8 | Why does small file I/O kill Lustre? | Every open/stat hits single MDS. Solution: aggregate small files into tar/shards, or use DNE (Distributed Namespace).                         |

---

# TROUBLESHOOTING RUNBOOKS — STAR-FORMATTED PRODUCTION INCIDENTS

> **Format:** Each runbook is a top-level *observable symptom* (what pager shows). Under each: what was observed → tools used for diagnosis → root causes discovered → how each was fixed → lessons & prevention. Designed as interview talking points — pick 1–2 root causes per story for a focused STAR narrative.

---

## RunBook 1: High Latency Incidents — "System Experiencing Slowness"

> **Story Context:** CapitalOne fraud scoring (5ms SLA), Apple Siri pipeline (300ms SLA), Fiserv LLM orchestration (2.3s SLA)

### What We Observed (Alerts That Fired)

| Alert | Metric Value | Normal Baseline | Story |
|-------|-------------|-----------------|-------|
| P99 latency breached SLA | 7.2ms (CapitalOne), 420ms (Siri), 450ms TTFT (Fiserv) | 3.8ms, 280ms, 95ms | Runbook 5, 6, 7 |
| TTFT spiked after model update | 190ms (was 45ms) | <50ms | CUDA Task 1 |
| GPU utilization *dropped* despite high load | 18% SM util under 10K req/sec | 72% expected | CUDA Story 2 |
| CPU utilization spiked to 92% | 92% (but not from useful work) | 35% | Runbook 8 |
| Per-stage transition time spiked | Search→Orch: 45ms (was 5ms) | <10ms | Runbook 6 |

---

### Root Cause A: CUDA Graph Invalidation (Silent Fallback to Eager)

**Tools Used:**
```bash
# 1. Nsight Systems — timeline shows individual kernel launches instead of graph replay
nsys profile --trace=cuda,osrt,nvtx --cuda-graph-trace=node -o post-update ./fraud-scoring

# 2. Stats report confirms graph not being used
nsys stats post-update.nsys-rep --report cuda_api_sum | grep -i "launch\|graph"
# cudaLaunchKernel: count=47   ← Back to eager!
# cudaGraphLaunch:  count=0    ← Graph NOT used!

# 3. Application metrics (custom)
grep "cuda_graph_hit_rate" /var/log/metrics.log  # Should be 99%+, was 0%
```

**What We Found:** New model v3.3 added 2 extra tree layers (45→47 kernels). Pre-captured CUDA Graph was compiled for 45 kernels. Shape validation failed silently → fell back to eager execution → 30× more launch overhead (150µs vs 5µs).

**How We Fixed It:**
```cpp
// 1. Made graph invalidation LOUD (alert + metric, never silent)
if (!validate_graph_shape(model)) {
    LOG_WARN("CUDA Graph shape mismatch — recapturing");
    recapture_graph(model, &graph_exec);
    emit_metric("cuda_graph.recapture", 1);  // Alerts on this
}

// 2. Recapture graphs during blue-green warmup BEFORE traffic switch
void blue_green_warmup(Model& new_model, ScoringBuffers& buf) {
    for (int bs : {1, 4, 8, 12, 16}) {
        capture_scoring_graph(buf, new_model, bs);
    }
    assert(validate_graph_output(buf, new_model));
}
```

**Lesson:** CUDA Graph invalidation has zero error logs by default — 30× latency regression with silent fallback. Always instrument graph-hit vs eager-fallback as a Prometheus counter.

**Prevention:** Automated CI test that loads new model → captures graph → replays → asserts shape match. Fails pipeline before production.

---

### Root Cause B: NUMA Misalignment (Cross-Socket Memory Access)

**Tools Used:**
```bash
# 1. Check NUMA topology
numactl --hardware                   # Node layout
lstopo-no-graphics                   # CPU → NUMA → PCIe → GPU → NIC mapping

# 2. Check process NUMA distribution
numastat -p $(pgrep inference)       # Per-node allocation — look for "other_node"

# 3. GPU-to-NIC NUMA relationship
nvidia-smi topo -m                   # GPU2 on different NUMA from mlx5_0!

# 4. AllReduce timing (multi-GPU)
nsys profile --trace=cuda,nvtx,nccl --gpu-metrics-device=all -o tp4-drift ./vllm-serve
# Timeline shows GPU #2 completing AllReduce 3ms after others
```

**What We Found:** After a kernel update, NVIDIA driver re-enumerated GPUs. NCCL ring order changed. GPU #2 now cross-NUMA for AllReduce traffic. Added ~3ms per collective × 80 layers × 2 collectives = straggler bottleneck slowing all GPUs.

**How We Fixed It:**
```bash
# Pin CUDA device order explicitly
export CUDA_VISIBLE_DEVICES=0,1,2,3

# Force NCCL topology
export NCCL_TOPO_FILE=/etc/nccl/h100_4gpu_topo.xml

# Pin to correct NUMA node
numactl --cpunodebind=0 --membind=0 vllm serve ... --tensor-parallel-size 4

# Long-term: TP=2 (NVLink pair) + PP=2 to avoid cross-NUMA AllReduce
```

**Lesson:** GPU topology is NOT static. Driver updates, BIOS changes, and kernel upgrades re-enumerate devices. Always verify NUMA alignment after any system-level change.

**Prevention:** Topology pinning in infrastructure-as-code (Terraform/Ansible). On any hardware change, validate topology before admitting node to serving pool.

---

### Root Cause C: Prefill/Decode Interference (GPU Monopolization)

**Tools Used:**
```bash
# 1. nsys timeline capture during P99 spike
nsys profile --trace=cuda,nvtx --duration=60 -o llm-jitter ./vllm-serve

# Timeline reveals:
# Normal:  |--decode (12ms)--|--decode (12ms)--|
# Spike:   |--decode (12ms)--|--PREFILL (85ms)---|--decode (12ms)--|
#                             ↑ 4096-token prefill blocks ALL decodes

# 2. Confirm with kernel duration
nsys stats llm-jitter.nsys-rep --report cuda_gpu_kern_sum
# flash_attn_fwd (prefill, T=4096): 82ms  ← This is the blocker
```

**What We Found:** vLLM's default scheduler allows large prefill operations to execute on same GPU as active decode. A single 4096-token prefill takes 82ms during which ALL in-flight decodes are stalled. TPOT P99 = 180ms (SLA: 50ms).

**How We Fixed It:**
```bash
# Chunked prefill: break large prefills into 512-token chunks
vllm serve meta-llama/Llama-3.1-70B \
  --enable-chunked-prefill \
  --max-num-batched-tokens 512

# Result: 4096-token prefill → 8 chunks of 10ms each
# Decode can interleave between chunks → TPOT P99: 45ms (within SLA)
```

**Lesson:** GPU can only run one thing at a time on an SM. Prefill monopolizes compute. Chunked prefill should be DEFAULT ON for any production deployment with latency SLAs.

**Prevention:** Always deploy with `--enable-chunked-prefill`. The 10% throughput cost is worth the 4× P99 improvement.

---

### Root Cause D: H2D Transfer Regression (Pageable Memory Fallback)

**Tools Used:**
```bash
# 1. nsys transfer analysis
nsys stats trace.nsys-rep --report cuda_gpu_mem_time_sum
# [CUDA memcpy HtoD]: avg=485µs   ← Was ~250µs! Doubled!

# 2. Check for pinned vs pageable (look for staging copies in timeline)
# nsys timeline shows "[CUDA memcpy HtoD]" with internal staging annotation

# 3. Verify pinned memory status
cat /proc/meminfo | grep Mlocked
```

**What We Found:** vLLM upgrade (0.4.x → 0.5.x) changed tokenizer output buffer from `pin_memory()` to standard pageable tensor. CUDA driver must now stage through internal pinned buffer before DMA → doubles H2D time for all prompts.

**How We Fixed It:**
```python
# Pin tokenizer output buffers explicitly
class PinnedTokenizerWrapper:
    def __init__(self, tokenizer, max_seq_len=8192):
        self.pinned_buffer = torch.empty(max_seq_len, dtype=torch.long).pin_memory()

    def encode(self, text):
        tokens = self.tokenizer.encode(text)
        self.pinned_buffer[:len(tokens)].copy_(torch.tensor(tokens))
        return self.pinned_buffer[:len(tokens)]  # Already pinned → fast DMA
```

**Lesson:** Framework upgrades break pinned memory assumptions silently. A `pin_memory()` call removed in a refactor has no functional test failure but causes 2× latency.

**Prevention:** After any framework upgrade, validate H2D transfer times in staging. Add regression test: `assert avg_h2d_time < 1.2 * baseline`.

---

### Root Cause E: Shared Memory Arena Fragmentation (Siri Pipeline)

**Tools Used:**
```bash
# 1. Check shared memory utilization
cat /proc/<pid>/smaps | grep -A4 "/dev/shm/siri_session"

# 2. Arena fragmentation metrics
grep "arena_compaction\|arena_alloc_fail" /var/log/siri/pipeline.log

# 3. Verify stage transition time
# Custom metric: stage_transition_time_ms{from="search", to="orchestration"}
```

**What We Found:** New Siri Suggestions feature wrote variable-length user-history embeddings (4KB–32KB) into shared `ConversationalState` arena. Arena used fixed-offset design. Variable data fragmented it → compaction cycles before next stage could write → 45ms added per transition.

**How We Fixed It:**
```cpp
// Replace fixed-offset with tiered arena + variable-length pool
struct ConversationalState {
    alignas(64) char asr_region[8192];      // Fixed stages
    alignas(64) char nlu_region[8192];
    alignas(64) char search_pool[65536];    // Variable pool (64KB)
    uint32_t search_pool_offset;            // Bump pointer
    struct SearchResult { uint32_t offset; uint32_t length; float score; } results[32];
};
```

**Lesson:** Shared memory arenas designed for fixed-size writes break when features evolve to variable-length data. Design for variable from the start.

**Prevention:** Feature-flag arena layout changes. Deploy new arena structure independently before enabling new features.

---

### Root Cause F: Per-Core Arena Overflow (CapitalOne)

**Tools Used:**
```bash
# 1. Application error logs
grep "arena_overflow" /var/log/fraud-scoring/metrics.log
# arena_overflow: request_size=72KB > slab_capacity=64KB

# 2. Check arena usage distribution
grep "arena_usage_bytes" metrics.log | awk '{print $2}' | sort -n | tail -20
```

**What We Found:** Model v3.3 added merchant-embedding feature (8KB float vector) to FeatureBlock. Per-request memory: 56KB → 72KB. Arena slab hardcoded at 64KB.

**How We Fixed It:**
```cpp
// Dynamic slab sizing from feature schema
size_t slab = next_power_of_two(schema.total_feature_bytes() + 4096);  // 128KB for v3.3
```

**Lesson:** Arena sizes must be schema-versioned. Never hardcode capacity based on current feature set.

**Prevention:** CI test: load schema → compute required slab → assert slab_config >= required. Fails pipeline if schema grows past capacity.

---

## RunBook 2: Throughput Collapse — "Requests Queuing, GPU Underutilized"

> **Story Context:** CapitalOne (10K req/sec/GPU target), Fiserv (500+ concurrent), Broadcom (2M+ req/sec), LLM cluster (60% throughput collapse over 48h)

### What We Observed (Alerts That Fired)

| Alert | Metric Value | Normal Baseline | Story |
|-------|-------------|-----------------|-------|
| Request queue depth growing | 45 waiting (was 0-5) | <10 | Runbook 7 |
| GPU SM utilization LOW under load | 18% (should be 72%) | 70%+ | CUDA Story 2 |
| Throughput per GPU dropped | 510 tok/s (was 850) | >800 | CUDA Task 2 |
| KV cache preemptions climbing | 120/min (normal: <5) | <5/min | Runbook 7 |
| Session affinity miss rate spiked | 30% misses (was <1%) | <1% | Runbook 6 |
| CPU spin-loop burning cores | 92% CPU (from spinning, not work) | 35% | Runbook 8 |

---

### Root Cause A: KV Cache Fragmentation (Gradual Degradation Over Days)

**Tools Used:**
```bash
# 1. vLLM metrics
curl localhost:8000/metrics | grep -E "cache|preempt"
# vllm:gpu_cache_usage_perc    0.94  ← Cache "full"
# vllm:num_preemptions_total   7,842 (in 1 hour)  ← Massive evictions
# vllm:num_requests_running    28    ← Only 28! (capacity should be 50+)

# 2. nvidia-smi shows memory available
nvidia-smi -q -d MEMORY  # 20GB appears free — but KV allocator says FULL

# 3. Block-level fragmentation check
curl http://localhost:8000/debug/block_stats
# Reveals: scattered blocks from long-lived sessions, external fragmentation
```

**What We Found:** PagedAttention uses 16-token blocks. Long-lived compliance-checking sessions (10+ turns over hours) held scattered blocks. Short-burst requests couldn't find contiguous free blocks → allocator reports "full" despite physical memory available. Degradation built over 48 hours without pod restart.

**How We Fixed It:**
```bash
# Immediate: rolling pod restart every 12h (clears fragmentation)
# Better: prefix caching + recompute-based preemption
vllm serve ... --enable-prefix-caching --preemption-mode recompute

# Best: session-TTL eviction (evict sessions idle >5min)
```

**Lesson:** Gradual degradation is harder to detect than cliff failures. KV cache fragmentation builds over days with no single alarm threshold.

**Prevention:** Track `preemptions/min` as a RATE alert (not absolute). Dashboard fragmentation_ratio (allocated/usable). Rolling restart schedule.

---

### Root Cause B: Single-Request Processing (GPU Parallelism Wasted)

**Tools Used:**
```bash
# 1. nsys timeline under realistic traffic
nsys profile --trace=cuda,osrt,nvtx --duration=30 -o throughput ./fraud-scoring

# 2. DCGM metrics confirm low utilization
dcgmi dmon -e 252,253  # SM util: 18%, Memory BW: 5%

# 3. Nsight Compute roofline
ncu --set roofline --kernel-name "forward" ./fraud-scoring
# GEMMs well below roofline at batch=1 — memory-latency-bound, not compute-bound
```

**What We Found:** Processing one request at a time. GPU has thousands of CUDA cores but batch=1 uses fraction. GPU idle ~50% between inferences. 1,000 req/sec/GPU vs 10,000 target.

**How We Fixed It:**
```cpp
// Micro-batching with bounded time window + deadline awareness
struct AdaptiveAccumulator {
    int max_batch = 16;           // Max requests per batch
    uint64_t max_wait_us = 500;   // Max 500µs accumulation
    uint64_t deadline_margin = 2'000'000;  // 2ms safety margin

    void dispatch_when_ready() {
        if (count >= max_batch) dispatch();
        if (oldest_deadline - now < deadline_margin) dispatch();  // STRICT
        if (now - window_start > max_wait_us) dispatch();
    }
};
```

**Results:** 1,000 → 10,000 req/sec/GPU (10×). SM utilization: 18% → 72%. P99 stayed within 5ms SLA.

**Lesson:** Micro-batching is the single biggest throughput lever for GPU inference. Even batch=4 gives 3× vs batch=1.

**Prevention:** Never deploy GPU inference with batch=1 in production. Always implement bounded micro-batching.

---

### Root Cause C: CUDA Graphs Stealing KV Cache Memory

**Tools Used:**
```bash
# 1. vLLM metrics after enabling CUDA Graphs
curl localhost:8000/metrics | grep cache
# vllm:gpu_cache_usage_perc  0.94   ← Almost full
# vllm:num_requests_waiting  12     ← Requests queuing
# vllm:num_preemptions_total 847    ← Mass evictions

# 2. Memory accounting
# CUDA Graph workspace: 847 MB (pre-allocated for all shape buckets)
# KV cache available: reduced from 32GB → 24GB
# Max concurrent: dropped from 64 → 38 requests
```

**What We Found:** CUDA Graphs require static memory allocation for worst-case decode shapes. With many shape buckets × large max_model_len, graph workspace consumed 8GB → KV cache reduced → fewer concurrent requests → lower throughput. Throughput dropped 40%.

**How We Fixed It:**
```bash
# Reduce max-model-len (if prompts fit)
--max-model-len 2048

# OR: hybrid mode — graphs for short sequences, eager for long
# OR: limit graph capture budget (fewer shape variants)
```

**Lesson:** CUDA Graphs trade MEMORY for LATENCY. At high concurrency, the memory cost exceeds the latency benefit. Profile both dimensions.

**Prevention:** Memory budget validation: `model_weights + graph_workspace + (max_seqs × kv_per_seq) < GPU_TOTAL - 8GB_buffer`.

---

### Root Cause D: SPSC Ring Overflow Causing CPU Spin-Loop (Cascading)

**Tools Used:**
```bash
# 1. Ring buffer metrics
grep "ring_buffer_full\|ring_spin_count" metrics.log
# ring_buffer_full: tier0_to_tier2 = 1.0 (100%!)
# ring_spin_count: 847,293/sec

# 2. CPU profile shows spin dominating
perf top -p $(pgrep fraud-scoring)
# 68.2% spsc_ring::try_push (SPIN LOOP!)
# 12.1% score_batch (actual useful work)
```

**What We Found:** Tier 2 crash-loop → can't drain SPSC ring → ring fills → Tier 0 spin-waits (designed for µs-level contention, not minutes) → burns CPU → degrades scoring throughput by 88%.

**How We Fixed It:**
```cpp
// Never spin indefinitely. Bounded try + overflow to durable queue
bool publish_to_tier2(SPSCRing& ring, const ArrowBatch& batch, DurableQueue& overflow) {
    for (int i = 0; i < 10; i++) {       // Max 10 attempts (~50ns)
        if (ring.try_push(batch)) return true;
        _mm_pause();
    }
    overflow.push(batch);                  // Kafka/Redis — guaranteed delivery
    emit_metric("tier2.ring_overflow", 1);
    return true;  // NEVER block Tier 0 on Tier 2 availability
}
```

**Lesson:** Tier isolation must include backpressure handling. "Fire-and-forget" async patterns need bounded retry. Never let downstream failure create CPU-burning spin loops upstream.

**Prevention:** Circuit breaker between tiers. If Tier 2 unavailable >30s, mark "degraded" and skip (scoring still works, just without reasoning).

---

### Root Cause E: Session-Affine Routing Failure (Kubernetes Rolling Update)

**Tools Used:**
```bash
# 1. Session migration rate
grep "session_migration\|affinity_miss" /var/log/siri/router.log | wc -l
# 30% of sessions lost affinity after node drain

# 2. Shared memory lifecycle
ls -la /dev/shm/siri_session_* | wc -l  # Dropping during update

# 3. Cold-start fallback path triggered
# P99 spikes to 900ms for affected sessions (full recomputation)
```

**What We Found:** Session-affine router used pod IP as affinity key. During rolling update, pods get new IPs. Consistent hashing ring didn't drain gracefully → sessions rerouted before shared memory state migrated → 30% of sessions hit cold-start path (850ms).

**How We Fixed It:**
```cpp
// Graceful drain with state handoff
void handle_drain_signal(Router& router, WorkerNode& draining) {
    draining.set_accepting(false);                    // Stop new sessions
    for (auto& session : draining.active_sessions()) {
        auto snapshot = session.serialize_prefix();   // Lightweight state
        auto target = router.find_new_affinity(session.id);
        target.prewarm_session(session.id, snapshot); // Pre-warm on new node
        router.atomic_reroute(session.id, target);    // Switch one-by-one
    }
}
```

**Lesson:** Session affinity is fragile during topology changes. Rolling updates, drains, and autoscaling all break sticky routing.

**Prevention:** Chaos engineering: regularly drain nodes in staging. Exercise migration path before production discovers it. Never rely on "sessions will just reconnect."

---

### Root Cause F: HPA Scaling Storm (Competing Metrics)

**Tools Used:**
```bash
# 1. HPA state
kubectl get hpa -A
# tier0-hpa: 92%/60% CPU  → scaled to MAX (20 pods)  ← Reacting to spin loop!
# tier2-hpa: 100%/70%     → 8 pods PENDING (no GPU)

# 2. Pending pods
kubectl get pods --field-selector=status.phase=Pending
# tier2-reasoning: Pending (0/1 GPU available)

# 3. Cluster autoscaler
kubectl logs -n kube-system cluster-autoscaler
# Scale-up: need 4 GPU nodes, ETA 12-15 minutes
```

**What We Found:** Tier 0 CPU spike (from spin loop) triggered its CPU-based HPA. But Tier 0 doesn't NEED more replicas — it needs Tier 2 to recover. Meanwhile Tier 2's HPA requests GPUs that aren't available. Both HPAs competing → resource deadlock.

**How We Fixed It:**
```yaml
# Tier 0 HPA: use request throughput, NOT CPU
metrics:
  - type: Pods
    pods:
      metric:
        name: fraud_scoring_requests_per_second
      target:
        type: AverageValue
        averageValue: "2000"

# Priority classes: Tier 0 always wins resources
apiVersion: scheduling.k8s.io/v1
kind: PriorityClass
metadata:
  name: tier0-critical
value: 1000000
```

**Lesson:** CPU utilization is a SYMPTOM, not a demand signal. Scaling on symptoms (spin loops) makes everything worse.

**Prevention:** Custom metrics (request throughput, queue depth) as primary HPA signal. Priority classes ensure critical tier always gets resources.

---

## RunBook 3: Quality Degradation — "Model Outputs Are Wrong/Garbage"

> **Story Context:** Fiserv LLM (15% accuracy improvement story), CapitalOne (fraud detection accuracy), Broadcom (security classification)

### What We Observed (Alerts That Fired)

| Alert | Metric Value | Normal Baseline | Story |
|-------|-------------|-----------------|-------|
| Accuracy dropped on eval set | 72% (was 87%) | >85% | CUDA Task 5 |
| Output becomes repetitive/nonsensical | Hallucination rate 40% | <5% | Runbook 3 |
| FP8 garbage on long prompts (>2048 tokens) | Garbled after ~1500 tokens | Coherent | CUDA Task 5 |
| Fraud false positive rate spiked | 12% (was 3%) | <5% | CapitalOne |
| Security model confidence dropped | avg 0.6 (was 0.9) | >0.85 | Broadcom |

---

### Root Cause A: FP8 Precision Loss in Attention (Long Sequences)

**Tools Used:**
```bash
# 1. Compare BF16 vs FP8 outputs
python -c "
from vllm import LLM
llm_bf16 = LLM('model', dtype='bfloat16')
llm_fp8 = LLM('model', quantization='fp8')
# Run same long prompt through both → output diverges after ~1500 tokens
"

# 2. Profile attention kernel numerics
ncu --set full --kernel-name "flash_attn" ./inference --input-len 2048

# 3. Check softmax accumulation precision
# FP8 E4M3: only 3-bit mantissa → sum of exponentials loses precision over 2048+ positions
```

**What We Found:** FP8 attention with long sequences has numerical precision issues in softmax reduction. Sum of exponentials over 2048+ positions loses precision when accumulated in FP8/FP16 without proper scaling. Output diverges progressively after ~1500 tokens.

**How We Fixed It:**
```bash
# Mixed precision: attention stays in BF16, only weights/linear layers use FP8
vllm serve model \
  --quantization fp8 \
  --kv-cache-dtype auto  # BF16 for KV cache (attention precision preserved)

# Rule: Reductions (softmax, LayerNorm) ALWAYS need higher-precision accumulators
```

**Lesson:** FP8 is NOT a drop-in replacement for all operations. Reductions with long accumulations amplify precision loss.

**Prevention:** Eval suite with long-context prompts (4K+ tokens) as gate before FP8 deployment. Compare perplexity against BF16 baseline at multiple sequence lengths.

---

### Root Cause B: KV Cache Corruption from ECC Errors

**Tools Used:**
```bash
# 1. Check ECC error counters
nvidia-smi -q -d ECC
# Volatile ECC DBE (Double-Bit Error): 3  ← FATAL: uncorrectable!

# 2. DCGM health check
dcgmi health -g 1 -c
# GPU 0: FAIL (Double-bit ECC error detected)

# 3. Application symptom: random garbage in output (not consistent pattern)
```

**What We Found:** Double-bit ECC error corrupted a few KV cache blocks. Affected requests produced garbage. Error was intermittent because only requests hitting those specific blocks were affected.

**How We Fixed It:**
```bash
# 1. Immediate: drain and replace affected GPU
kubectl cordon <node>
kubectl drain <node> --ignore-daemonsets

# 2. Reset ECC counters to verify fix (after GPU replacement)
nvidia-smi -r  # Reset GPU
nvidia-smi --ecc-config=1  # Ensure ECC enabled

# 3. Add DCGM-based alerting on SBE (single-bit = early warning)
dcgmi policy -g 1 --set 1,1  # Alert on any ECC error
```

**Lesson:** ECC double-bit errors are SILENT at application level — no CUDA error returned. Output is just wrong. Must monitor hardware errors proactively.

**Prevention:** DCGM exporter → Prometheus → alert on ANY ECC error. SBE trending up = GPU failing, replace proactively.

---

### Root Cause C: Embedding Dimension Mismatch After Model Update (Siri)

**Tools Used:**
```bash
# 1. Profile search stage kernels
nsys profile --trace=cuda,nvtx -o siri-search ./siri-pipeline --stage=search
nsys stats siri-search.nsys-rep --report cuda_gpu_kern_sum | grep -i "gemm\|projection"
# cublas_sgemm_128x32: 500 calls, 35ms avg  ← UNEXPECTED projection!

# 2. Check dimensionality
# NLU outputs 768-dim (BERT-large), FAISS index built with 256-dim
# Projection layer (768→256) running on EVERY query: 35ms overhead
```

**What We Found:** New recommendation model outputs 768-dim embeddings (BERT-large), but FAISS search index was built with 256-dim. A runtime projection layer was added to bridge the gap — runs synchronously before every search, adding 35ms.

**How We Fixed It:**
```cpp
// Option A: Project at INDEX BUILD TIME (offline, zero runtime cost)
void build_search_index(const EmbeddingStore& store) {
    auto projected = projection.forward(store.embeddings);  // 768→256 offline
    faiss_index.add(projected);  // Index stores 256-dim
}
// At query time: project single query vector only (0.05ms for 1 vector vs 35ms for batch)

// Option B: Cache user projections per session
auto& cached = projection_cache.get_or_compute(user_id, raw_embedding);
```

**Lesson:** Embedding dimension mismatches have runtime cost. When upstream models change output dimensions, solve at index-build time, not query time.

**Prevention:** Dimension contract in model registry. When NLU team publishes new embedding model, registry enforces compatibility checks against downstream consumers.

---

### Root Cause D: Config Drift — max_model_len Causing Truncation

**Tools Used:**
```bash
# 1. Check config
kubectl get configmap inference-config -o yaml
# max_model_len: 2048  ← Was 4096! Someone changed it

# 2. Application logs
grep "truncat" /var/log/inference/service.log
# "Input truncated from 3200 to 2048 tokens"  ← Losing critical context!

# 3. Quality correlation
# All quality drops are on prompts > 2048 tokens (truncation removes key information)
```

**What We Found:** A hotfix 3 days earlier reduced `max_model_len` from 4096 to 2048 to fix an OOM issue. This silently truncated long prompts (loan documents, fraud context), removing critical information the model needed for accurate answers.

**How We Fixed It:**
```bash
# Restore max_model_len with proper memory budget
vllm serve ... \
  --max-model-len 4096 \
  --kv-cache-dtype fp8 \          # 2× more KV capacity → no OOM
  --enable-prefix-caching          # Share system prompt across requests
```

**Lesson:** Configuration changes that fix one issue (OOM) can silently cause another (quality). Config drift without eval validation is dangerous.

**Prevention:** Any config change to `max_model_len`, `quantization`, or `temperature` must re-run eval suite before merge. GitOps with mandatory approval + eval gate.

---

## RunBook 4: Cascading Failures — "Multiple Tiers Degrading Simultaneously"

> **Story Context:** CapitalOne three-tier fraud system, Fiserv multi-model orchestration. These are the most impressive interview stories because they demonstrate system-level thinking.

### What We Observed (Alerts That Fired — ALL AT ONCE)

| Alert | Metric | Story |
|-------|--------|-------|
| Tier 2 pods in CrashLoopBackOff | Restart every 90s | Runbook 8 |
| Tier 0 CPU spike to 92% | Not from scoring — from spinning | Runbook 8 |
| Request throughput at 12% of normal | System-wide collapse | Runbook 8 |
| Host memory growing 500MB/hour | Arrow buffer leak | Runbook 8 |
| HPA scaled both tiers to MAX | Competing for resources | Runbook 8 |
| After 48h: OOM killer fires | Memory leak → total outage | Runbook 8 |

---

### The Cascade Timeline

```
T+0:00   Config drift (max_model_len=16384) deployed 3 days ago — no immediate effect
T+0:00   Traffic spike → 50+ concurrent Tier 2 sessions → GPU OOM → crash loop
T+2:00   SPSC ring fills (Tier 2 not draining)
T+2:30   Tier 0 spin-loop starts → CPU spikes to 92%
T+3:00   Arrow buffer leak begins (overflow path doesn't release refcount)
T+3:00   Tier 0 HPA triggers on CPU → scales to max (WRONG — spin loop isn't load)
T+3:30   Tier 2 HPA → requests GPUs → Pending (no capacity)
T+5:00   More Tier 0 pods = more spin loops = more waste
T+8:00   System at 12% throughput (most compute wasted)
T+48:00  Arrow leak → OOM killer → full outage
```

---

### Root Cause A: Trigger — Config Drift Causing GPU OOM on Spike

**Tools Used:**
```bash
kubectl logs <tier2-pod> --previous | grep -i "oom\|cuda"
# RuntimeError: CUDA out of memory. Tried to allocate 2.4 GB
# (GPU 0: 80 GB total, 78.9 GB allocated, 1.1 GB free)

kubectl get configmap -o yaml | grep max_model_len
# max_model_len: 16384  ← Was 8192! Changed in hotfix 3 days ago
```

**What We Found:** A Helm values override set `max_model_len=16384` (was 8192). Under light load, only 20-30 sessions ran (fit in memory). Traffic spike to 50+ → OOM → crash loop.

**Fix:**
```bash
kubectl set env deployment/tier2 VLLM_MAX_MODEL_LEN=8192 VLLM_MAX_NUM_SEQS=48

# Prevention: init container validates memory budget before serving
# model_mem + graph_workspace + (max_seqs × kv_per_seq × max_len) < GPU_TOTAL - 8GB
```

---

### Root Cause B: Amplifier — SPSC Ring Spin-Loop

**What Happened:** Tier 2 crash → can't drain ring → Tier 0 spins indefinitely on full ring → 68% of CPU wasted on spinning.

**Fix:** Bounded retry (10 attempts) + overflow to durable queue. **Never block Tier 0 on Tier 2 availability.**

---

### Root Cause C: Amplifier — Arrow Buffer Reference Leak

**Tools Used:**
```bash
# Memory growth tracking
cat /proc/$(pgrep fraud-scoring)/status | grep VmRSS
# VmRSS growing 500MB/hour linearly

grep "arrow_buffer_refcount_leaked" metrics.log
# 312 leaked buffers/hour
```

**What We Found:** Overflow path copied Arrow batch pointer to durable queue WITHOUT calling `release()` on the original. Buffers accumulated forever.

**Fix:**
```cpp
// Serialize to durable queue (independent copy), then release original
auto serialized = batch.serialize_to_ipc();
queue.push(std::move(serialized));
batch.release();  // Decrement refcount → returns to pool
```

---

### Root Cause D: Amplifier — HPA Scaling on Wrong Metric

**What Happened:** CPU spike from spin-loop → Tier 0 HPA says "need more pods!" → scales to MAX → more spin loops → more waste. Meanwhile Tier 2 can't get GPUs because cluster is "full" of useless Tier 0 replicas.

**Fix:** HPA uses `requests_per_second` (actual demand) not CPU%. Priority classes ensure Tier 0 > Tier 2 for resource allocation.

---

### Lessons From The Cascade

1. **Any single failure should NOT cascade.** The config drift was 3 days old. The system should have survived a Tier 2 crash without affecting Tier 0 scoring.

2. **Bounded retries everywhere.** Spin-loops are for microsecond contention, not minute-level unavailability. Every inter-tier communication needs a timeout + fallback.

3. **Error paths need the same rigor as happy paths.** The Arrow leak was in overflow code — code that only runs when things are already broken. These paths are the MOST critical to test.

4. **Rate-of-change alerts catch gradual failures.** The memory leak was 500MB/hour — invisible in threshold alerts, obvious in trend alerts.

### What I'd Do Differently (Interview Answer)

> "Three things I'd change. First: **circuit breakers between tiers** — if Tier 2 is unavailable for 30 seconds, stop trying. Tier 0 scoring still works, just without explanations. This prevents ALL downstream symptoms.

> Second: **memory budget validation as a readiness probe** — the pod shouldn't accept traffic if its config violates `model_mem + max_seqs × kv_per_seq < available`. This catches config drift before traffic hits it.

> Third: **chaos engineering** — regularly kill Tier 2 under load in staging. The spin-loop behavior would have been caught in the first test run."

---

## RunBook 5: Network & Multi-GPU Scaling Issues — "TP/PP Not Delivering Expected Speedup"

> **Story Context:** Fiserv 70B model on 4×H100, Broadcom multi-region, NCCL collective operations

### What We Observed

| Alert | Metric Value | Expected | Story |
|-------|-------------|----------|-------|
| TP=4 only 2.1× speedup | 25 tok/s (4×H100) | ~45 tok/s (near 4×) | CUDA Task 4 |
| NCCL timeout errors | `NCCL WARN Timeout` | None | Runbook 4 |
| One GPU consistently slower | GPU #2 AllReduce 3ms late | All equal | Runbook 7 |
| Inter-node collective slow | AllReduce 5× slower than spec | Spec bandwidth | Runbook 4 |

---

### Root Cause A: Incomplete NVLink Mesh (PCIe Fallback)

**Tools Used:**
```bash
# 1. Topology check
nvidia-smi topo -m
# GPU0↔GPU1: NV12 (NVLink — good)
# GPU0↔GPU2: SYS  (PCIe cross-socket — BAD!)
# GPU2↔GPU3: NV12 (NVLink — good)
# → Only 2 NVLink PAIRS, not full mesh

# 2. NCCL bandwidth test
./build/all_reduce_perf -b 1M -e 1G -f 2 -g 4
# busBW: 180 GB/s (expected: 450 GB/s with full mesh)
```

**How We Fixed It:**
```bash
# Option A: Use TP=2 (stay on NVLink pair)
vllm serve ... --tensor-parallel-size 2

# Option B: TP=2 × PP=2 (less AllReduce, more pipeline overlap)
vllm serve ... --tensor-parallel-size 2 --pipeline-parallel-size 2

# Option C: Request full NVLink-mesh node (DGX/HGX H100)
```

---

### Root Cause B: NCCL Using TCP Instead of InfiniBand

**Tools Used:**
```bash
# 1. NCCL debug output
NCCL_DEBUG=INFO python serve.py 2>&1 | grep "Using"
# "Using network Socket"  ← BAD! Should be "NET/IB"

# 2. IB device check
ibstat  # State: Down  ← IB not working!
lsmod | grep ib_uverbs  # Not loaded!
```

**How We Fixed It:**
```bash
# Load IB modules
modprobe ib_uverbs
modprobe mlx5_ib

# Pin NCCL to IB device
export NCCL_IB_HCA=mlx5_0
export NCCL_IB_DISABLE=0
export NCCL_NET_GDR_LEVEL=5  # Enable GPUDirect RDMA
```

---

### Root Cause C: GPUDirect RDMA Not Active

**Tools Used:**
```bash
lsmod | grep nvidia_peermem  # Empty! Module not loaded
# Without nvidia_peermem: GPU → CPU RAM → NIC → remote (2× copies)
# With nvidia_peermem: GPU → NIC → remote (direct, no CPU copy)
```

**How We Fixed It:**
```bash
modprobe nvidia_peermem

# Verify GPU and NIC on same NUMA node (required for GDR)
cat /sys/class/infiniband/mlx5_0/device/numa_node  # Must match GPU NUMA
```

**Lesson:** Always verify: NVLink topology, NCCL transport selection, GPUDirect RDMA status. A single missing kernel module can 2× your AllReduce latency.

**Prevention:** Node admission: validate `nvidia-smi topo -m`, `ibstat`, `lsmod | grep nvidia_peermem` before joining serving pool.

---

## RunBook 6: Resource Exhaustion — "OOM, Buffer Pool Depletion, Memory Leaks"

> **Story Context:** All stories — every production system eventually hits resource limits

### What We Observed

| Alert | Metric Value | Normal | Story |
|-------|-------------|--------|-------|
| GPU OOM at 60% expected capacity | 38 concurrent (expected 64) | 64 | CUDA Task 7 |
| Pinned memory allocation failed | cudaMallocHost returns error | Always succeeds | Runbook 5 |
| Buffer pool exhaustion | acquire time: 15µs (was 50ns) | <100ns | Runbook 6 |
| Host memory growing linearly | +500MB/hour | Stable | Runbook 8 |
| /dev/shm full | 100% (shared memory) | <80% | Runbook 6 |

---

### Root Cause A: KV Cache OOM (Over-Provisioned max_model_len)

**Tools Used:**
```bash
# Memory accounting
curl localhost:8000/metrics | grep cache
# gpu_cache_usage: 0.97 at only 38 requests

# Calculate expected vs actual
python -c "
model=16; cuda_overhead=7; gpu=80  # GB
available = gpu - model - cuda_overhead  # 57 GB
kv_per_req = 2*32*8*128*4096*2 / 1e9  # 0.5 GB at max_model_len=4096
expected = available / kv_per_req  # 114!
print(f'Expected: {expected:.0f}, Actual: 38')
# Problem: max_model_len=4096 but avg actual usage only 800 tokens
# 75% of allocated KV is WASTED
"
```

**How We Fixed It:**
```bash
--max-model-len 2048          # If prompts fit (2× more concurrent)
--enable-prefix-caching        # Share KV blocks across similar prompts
--kv-cache-dtype fp8           # 2× more KV capacity
```

---

### Root Cause B: Pinned Memory Exhaustion During Blue-Green Swap

**Tools Used:**
```bash
nvidia-smi -q -d MEMORY | grep -A5 "Pinned"
ulimit -l  # Locked memory limit — may be too low
cat /proc/meminfo | grep Mlocked
```

**What We Found:** Blue-green model swap has BOTH old and new model buffers allocated simultaneously. Doubles pinned memory. `RLIMIT_MEMLOCK` sized for single-model only.

**How We Fixed It:**
```cpp
// Stagger: free old BEFORE allocating new
void atomic_model_swap(ScoringBuffers& buf, Model& new_model) {
    drain_inflight(buf, 50);         // Wait max 50ms
    cudaGraphExecDestroy(buf.graph_exec);
    cudaFreeHost(buf.h_features);    // Release old pinned memory FIRST
    cudaFreeHost(buf.h_scores);
    init_buffers(buf, MAX_BATCH, new_model.num_features());  // Allocate new
    capture_scoring_graph(buf, new_model, MAX_BATCH);
}
```

---

### Root Cause C: gRPC Buffer Pool Exhaustion (Feature Load Change)

**Tools Used:**
```bash
grep "buffer_pool" /var/log/pipeline.log
# pool_size=4096 in_use=4091 free=5 fallback_count=847  ← Exhausted!

# perf: allocation path cache misses
perf stat -e cache-misses -p $(pgrep siri-search) -- sleep 10
```

**What We Found:** Siri Suggestions streams 32 results per session × millions sessions. Each chunk acquires a buffer. Pool sized for pre-Suggestions traffic (4096 buffers). Exhausted → falls back to malloc (15µs each).

**How We Fixed It:**
```cpp
// 1. Resize pool for peak feature load
constexpr int POOL_SIZE = MAX_SESSIONS * MAX_CHUNKS_PER_SESSION * 1.5;  // 32768

// 2. Batch streaming: 8 results per buffer instead of 1
for (int i = 0; i < results.count; i += 8) {
    auto* buf = pool.acquire();  // 4 acquires per session (was 32)
    serialize_batch(results, i, min(i+8, results.count), buf);
    stream.Write(buf);
    pool.release(buf);
}
```

**Lesson:** Buffer pools must be sized for PEAK feature load, not average. New features can 8× the buffer acquire rate overnight.

**Prevention:** Auto-scale pool if >80% utilized for 30s (double, with hard cap). Alert at 70%.

---

## Quick-Reference: Symptoms → Root Cause → Story Mapping

| Symptom | Most Likely Root Cause | Tools | Story to Tell |
|---------|----------------------|-------|---------------|
| P99 spike immediately after deploy | CUDA Graph invalidation | nsys (graph vs eager) | CapitalOne model update |
| P99 spike on long prompts only | Prefill O(T²) or FP8 precision | ncu roofline, dtype comparison | CUDA Task 1/5 |
| Gradual throughput decay over days | KV cache fragmentation | vLLM preemption metrics | LLM infra (Fiserv) |
| GPU util low under load | Batch=1, parallelism wasted | nsys + DCGM | CapitalOne micro-batching |
| GPU util HIGH but P99 bad | Prefill blocking decode | nsys timeline | LLM infra (vLLM scheduling) |
| CPU spike with no work increase | Spin-loop from downstream failure | perf top (spin function) | Cascading failure |
| One GPU slower than others | NUMA misalignment / NVLink | nvidia-smi topo + numastat | Fiserv TP deployment |
| Quality degrades on long context | FP8 softmax precision | BF16 comparison | CUDA Task 5 |
| Memory growing linearly | Reference count leak | VmRSS tracking | Arrow buffer cascade |
| OOM earlier than expected | max_model_len over-provision | Memory budget calc | CUDA Task 7 |
| Session latency spike after drain | Affinity routing broken | migration metrics | Siri rolling update |
| Throughput drops after CUDA Graphs | Graphs stealing KV memory | vLLM cache metrics | CUDA Task 2 |
| H2D transfer time doubled | Pageable memory fallback | nsys mem_time_sum | vLLM upgrade regression |
| HPA scales but doesn't help | Scaling on wrong metric | kubectl get hpa | Cascading failure |

---

## Interview Delivery Cheat Sheet

> Pick ONE RunBook, TWO root causes within it, and tell the STAR story:

**Template (2 minutes):**

> **S:** "We were running [system] at [scale] with [SLA]. Alert fired showing [specific metric]."
>
> **T:** "My job was to diagnose and fix this within [timeframe] while maintaining [availability]."
>
> **A:** "First I ran [tool 1] which showed [finding]. This pointed me to [root cause]. I then confirmed with [tool 2] showing [evidence]. The fix was [specific change]. But I also discovered [second issue] using [tool 3] — the root cause was [explanation] and I fixed it by [change]."
>
> **R:** "P99 dropped from [X] to [Y] — [N×] improvement. Throughput recovered to [Z]. The key lesson was [insight], and I prevented recurrence by [specific prevention measure]."

**Power phrases for profiling stories:**
- "The nsys timeline made it immediately obvious that..."
- "Without profiling, we would have assumed [wrong thing]. The data showed..."
- "The fix wasn't a kernel optimization — it was a scheduling/routing/config change informed by understanding the GPU execution model"
- "The dangerous part was that it was SILENT — no errors, no logs, just gradual degradation"

---

# OBSERVABILITY, PROFILING & MONITORING — END-TO-END TOOLING REFERENCE

> **Purpose:** One-stop reference for every tool used across Runbooks 1–8. Arranged in the natural troubleshooting flow: **Detect → Triage → Profile → Root-Cause → Fix → Verify**. Covers the full AI/HPC stack: Linux → Kubernetes → Networking → Storage → GPU → LLM Serving → Application.


---

## Phase 0: Detection & Alerting (How We Know Something Is Wrong)

> These are the tools and dashboards running continuously. They fire alerts that kick off investigation.

### Application-Level Metrics (Prometheus + Grafana)

| Metric | Source | What It Tells You | Alert Threshold |
|--------|--------|-------------------|-----------------|
| `request_latency_p99` | App instrumentation | End-to-end latency regression | > 2× baseline |
| `request_throughput_rps` | App instrumentation | Throughput collapse | < 50% baseline for 5min |
| `error_rate_5xx` | Ingress/App | Service failures | > 0.1% |
| `queue_depth` | App internal | Backpressure building | > 100 pending |
| `ring_buffer_utilization` | Custom metric | Inter-tier backpressure (Runbook 8) | > 90% |
| `arena_overflow_count` | Custom metric | Per-core memory violations (Runbook 5) | > 0 |
| `cuda_graph_hit_rate` | Custom metric | Graph invalidation (Runbook 5) | < 99% |

```bash
# Prometheus query examples for detection
# Latency regression
rate(request_latency_seconds_sum[5m]) / rate(request_latency_seconds_count[5m]) > 0.005

# Throughput collapse
rate(requests_total[5m]) < 0.5 * avg_over_time(rate(requests_total[5m])[1d:5m])

# KV cache pressure (Runbook 7)
vllm:gpu_cache_usage_perc > 0.9 and rate(vllm:num_preemptions_total[5m]) > 1
```

### vLLM Serving Metrics (Built-in Prometheus Endpoint)

```bash
curl http://localhost:8000/metrics | grep -E "vllm:"
```

| Metric | What It Tells You | Healthy Range |
|--------|-------------------|---------------|
| `vllm:num_requests_running` | Active concurrent requests | < max_num_seqs |
| `vllm:num_requests_waiting` | Queue depth (demand > capacity) | < 10 |
| `vllm:gpu_cache_usage_perc` | KV cache utilization | 0.4–0.8 |
| `vllm:num_preemptions_total` | KV evictions (fragmentation signal) | < 5/min |
| `vllm:avg_prompt_throughput_toks_per_s` | Prefill throughput | Stable ±10% |
| `vllm:avg_generation_throughput_toks_per_s` | Decode throughput | Stable ±10% |
| `vllm:e2e_request_latency_seconds` | Full request lifecycle | Within SLA |
| `vllm:time_to_first_token_seconds` | TTFT (prefill bound) | < SLA target |
| `vllm:time_per_output_token_seconds` | TPOT (decode bound) | < 50ms typical |
| `vllm:num_generation_tokens_total` | Total output tokens (billing/capacity) | Increasing |

### NVIDIA DCGM (Data Center GPU Manager) — Continuous GPU Health

```bash
# Start DCGM daemon
nv-hostengine

# Real-time monitoring (1-second intervals)
dcgmi dmon -e 203,204,252,253,1001,1002,1004,1005,1009,1010,1011,1012 -d 1000
```

| Field ID | Metric | What It Tells You | Warning Threshold |
|----------|--------|-------------------|-------------------|
| 203 | SM Clock (MHz) | Thermal throttling if below boost | < 80% of max boost |
| 204 | Memory Clock (MHz) | Memory subsystem throttling | < 80% of max |
| 252 | GPU Utilization % | Overall GPU busy | < 30% under load = stall |
| 253 | Memory Utilization % | HBM bandwidth usage | > 90% = memory-bound |
| 1001 | ECC SBE volatile | Single-bit errors (correctable) | > 0 (monitor trend) |
| 1002 | ECC DBE volatile | Double-bit errors (FATAL) | > 0 = replace GPU |
| 1004 | GPU Temperature | Thermal health | > 83°C = throttling |
| 1005 | Power Usage (W) | Power limit reached? | Near TDP = throttled |
| 1009 | FB Memory Used (MB) | Allocation tracking | Near total = OOM risk |
| 1010 | FB Memory Free (MB) | Available for KV cache | < 5GB = pressure |
| 1011 | NVLink TX Bytes | Inter-GPU communication volume | Asymmetry = routing issue |
| 1012 | NVLink RX Bytes | Inter-GPU receive | Asymmetry = straggler |

```bash
# DCGM health check (detect ECC/thermal/power issues)
dcgmi health -g 1 -c

# DCGM policy-based alerting
dcgmi policy -g 1 --set 1,1  # Alert on ECC double-bit errors

# Export to Prometheus (dcgm-exporter sidecar in K8s)
# Helm chart: gpu-helm-charts/dcgm-exporter
helm install dcgm-exporter nvidia/dcgm-exporter
```

### Kubernetes Health Signals

```bash
# Pod status overview
kubectl get pods -A --field-selector=status.phase!=Running

# Events (last 30 minutes) — often first signal of trouble
kubectl get events --sort-by='.lastTimestamp' -A | tail -50

# Resource pressure
kubectl top nodes --sort-by=cpu
kubectl top pods --sort-by=memory -n inference

# HPA state (Runbook 8: scaling storm detection)
kubectl get hpa -A -o wide
```

---

## Phase 1: Initial Triage (First 60 Seconds)

> When an alert fires, these commands give you the lay of the land in under a minute.

### System-Level Quick Check (Linux Host)

```bash
# ─── CPU / Memory / IO at a glance ───
htop                          # Interactive: CPU per-core, memory, load average
uptime                        # Load average (1/5/15 min)
free -h                       # Memory: total/used/free/available/swap
vmstat 1 5                    # CPU wait, swap activity, context switches
dmesg -T | tail -30           # Kernel messages (OOM killer, hardware errors)

# ─── Quick process check ───
ps aux --sort=-%mem | head -10   # Top memory consumers
ps aux --sort=-%cpu | head -10   # Top CPU consumers

# ─── Disk / IO ───
iostat -xz 1 3                # Disk utilization, await, queue depth
df -h                         # Filesystem space (full /dev/shm = shared memory issues)
```

### GPU Quick Check (< 10 seconds)

```bash
# ─── GPU health snapshot ───
nvidia-smi                    # Utilization, memory, temperature, power, ECC
nvidia-smi -q -d TEMPERATURE  # Detailed thermal state
nvidia-smi -q -d MEMORY       # Detailed memory breakdown
nvidia-smi -q -d ECC          # ECC error counts

# ─── Quick: is the GPU actually doing work? ───
nvidia-smi dmon -d 1 -c 5    # 5 seconds of GPU/memory util, power, temp
# If GPU util = 0% under load → GPU fell off bus, driver crash, or pipeline stall
```

### Kubernetes Quick Check

```bash
# ─── Inference pod health ───
kubectl get pods -n inference -o wide           # Running? Which node?
kubectl describe pod <failing-pod> -n inference  # Events, conditions
kubectl logs <pod> -n inference --tail=100       # Recent logs
kubectl logs <pod> -n inference --previous       # Logs from crashed container

# ─── Resource state ───
kubectl describe node <gpu-node> | grep -A10 "Allocated resources"
kubectl get priorityclasses                      # Who wins resource competition?
```

---

## Phase 2: Deep Profiling (Finding Root Cause)

> Once triage narrows the problem domain, these tools identify the exact root cause.

### 2A: Linux System Profiling

#### CPU & Scheduling

```bash
# ─── perf: CPU-level profiling ───
# Where is CPU time spent? (find hot functions)
perf top -p $(pgrep fraud-scoring) -g          # Live flamegraph-style view
perf record -g -p $(pgrep inference) -- sleep 30
perf report                                      # Interactive report

# Specific counters (context switches, cache misses, TLB — Runbook 5/8)
perf stat -e context-switches,cpu-migrations,cache-misses,dTLB-load-misses,instructions,cycles \
  -p $(pgrep fraud-scoring) -- sleep 10

# ─── CPU isolation verification (Runbook 5: per-core scoring) ───
cat /sys/devices/system/cpu/isolated             # Which cores are isolated?
taskset -p $(pgrep fraud-scoring)                # Check CPU affinity mask
cat /proc/$(pgrep fraud-scoring)/status | grep Cpus_allowed_list

# ─── IRQ interference check ───
cat /proc/interrupts | grep -E "NVL|mlx5|eth"   # NIC/GPU interrupts per CPU
# If interrupts land on isolated scoring cores → performance interference

# ─── mpstat: per-core utilization breakdown ───
mpstat -P ALL 1 5                                # Per-core: %usr, %sys, %iowait, %soft
# High %soft on one core → IRQ/softIRQ storm (Runbook 4/8)
```

#### Memory & NUMA

```bash
# ─── NUMA topology and allocation ───
numactl --hardware                    # NUMA node layout, CPU-to-memory mapping
numastat                              # Per-node memory allocation stats
numastat -p $(pgrep inference)        # Per-process NUMA distribution
# If significant "other_node" allocation → cross-NUMA access penalty (Runbook 7)

# ─── lstopo: full hardware topology visualization ───
lstopo-no-graphics                    # Text output: CPU → NUMA → PCIe → GPU → NIC
lstopo --output-format png topo.png   # Generate visual topology diagram

# ─── HugePages status ───
cat /proc/meminfo | grep -i huge      # HugePages_Total, Free, Rsvd
cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# ─── Memory pressure ───
cat /proc/pressure/memory              # PSI: some/full memory pressure
cat /proc/pressure/cpu                 # PSI: some/full CPU pressure
cat /proc/pressure/io                  # PSI: some/full IO pressure

# ─── Shared memory (critical for Siri pipeline — Runbook 6) ───
ls -la /dev/shm/                       # Shared memory segments
df -h /dev/shm                         # tmpfs capacity and usage
ipcs -m                                # System V shared memory segments
cat /proc/$(pgrep siri-pipeline)/maps | grep shm   # Process mmap regions
```

#### Disk / Storage / IO

```bash
# ─── iostat: device-level IO ───
iostat -xz 1 10                        # %util, await, r/s, w/s per device
# If await > 10ms on model-weight NVMe → slow model load

# ─── blktrace: block-level tracing ───
blktrace -d /dev/nvme0n1 -o trace & sleep 5 && kill %1
blkparse trace.blktrace.0 | head -50

# ─── fio: storage benchmark (validate expected performance) ───
# Sequential read (model weight loading pattern)
fio --name=seq-read --filename=/data/model.bin --rw=read \
    --bs=1M --direct=1 --numjobs=4 --runtime=10

# Random 4K read (feature store access pattern)
fio --name=rand-read --filename=/data/features.db --rw=randread \
    --bs=4k --direct=1 --numjobs=8 --runtime=10
```

### 2B: Network Profiling

#### Ethernet / TCP (Service-to-Service)

```bash
# ─── ss: socket statistics (connection state) ───
ss -tnp | grep <port>                 # Active connections to inference service
ss -ti dst <inference-pod-ip>          # Detailed: RTT, cwnd, retransmits
# High retransmits → network congestion or packet loss

# ─── tcpdump: packet capture (gRPC/HTTP2 between tiers) ───
tcpdump -i eth0 -nn port 8000 -c 1000 -w /tmp/capture.pcap
# Analyze with tshark:
tshark -r /tmp/capture.pcap -z io,stat,0.1  # IO rate per 100ms bucket

# ─── iperf3: bandwidth validation ───
# Server: iperf3 -s
iperf3 -c <target> -P 8 -t 30         # 8 streams, 30 seconds
# Expected: ~95% of link speed

# ─── ethtool: NIC diagnostics ───
ethtool <interface>                    # Link speed, duplex
ethtool -S <interface> | grep -i "error\|drop\|pause\|discard"
ethtool -k <interface>                 # Offload features (TSO, GRO, etc.)
ethtool -g <interface>                 # Ring buffer sizes

# ─── Network latency profiling ───
ping -c 100 -i 0.01 <target>          # 100 pings, 10ms interval → jitter
mtr -n --report <target>               # Traceroute with statistics
```

#### RDMA / InfiniBand (Multi-GPU / Multi-Node)

```bash
# ─── IB device status ───
ibstat                                  # Port state, speed, LID
ibstatus                                # Quick all-port summary
ibv_devinfo                             # Device capabilities (MTU, max QP)

# ─── IB bandwidth test ───
# Server: ib_write_bw -d mlx5_0 --report_gbits
ib_write_bw -d mlx5_0 --report_gbits <server_ip>
# Expected: HDR=380Gbps, NDR=380Gbps

# ─── IB latency test ───
ib_write_lat -d mlx5_0 <server_ip>
# Expected: IB=1-2µs, RoCE=3-5µs

# ─── GPUDirect RDMA verification ───
lsmod | grep nvidia_peermem             # Must be loaded for GDR
cat /sys/class/infiniband/mlx5_0/device/numa_node  # NIC NUMA node
# Compare with GPU NUMA: nvidia-smi topo -m → both must be same NUMA

# ─── NCCL transport diagnosis ───
NCCL_DEBUG=INFO python -c "import torch.distributed as dist; ..."
# Look for: "NET/IB" (good), "NET/Socket" (bad fallback)
# Look for: "P2P/NVLink" (good), "P2P/SHM" (fallback)
```

#### NVLink / GPU Interconnect

```bash
# ─── Topology map (critical for TP decisions — Runbook 7) ───
nvidia-smi topo -m
# NV# = NVLink (best), PIX = same PCIe switch, SYS = cross-socket (worst)

# ─── NVLink status & errors ───
nvidia-smi nvlink -s                   # Link state (active/inactive)
nvidia-smi nvlink -c                   # Error counters (CRC = failing link)

# ─── NVLink bandwidth measurement ───
# NCCL all_reduce_perf (best tool for measuring actual collective bandwidth)
cd /opt/nccl-tests
./build/all_reduce_perf -b 1M -e 1G -f 2 -g 4
# Expected 4×H100 NVLink: ~450 GB/s bus bandwidth

# ─── nvbandwidth: GPU-to-GPU bandwidth microbenchmark ───
nvbandwidth --testcase=device_to_device_memcpy_read_ce
```

### 2C: GPU Profiling (Nsight Systems + Nsight Compute)

#### Nsight Systems (nsys) — Timeline & System-Level View

> **When to use:** First GPU profiling tool. Shows the *timeline* — what happened when, where are the gaps, what's blocking what. Answers: "Where is time being spent?"

```bash
# ─── Basic capture (most common usage) ───
nsys profile -o <output_name> \
  --trace=cuda,osrt,nvtx,cudnn,cublas \
  --gpu-metrics-device=all \
  ./your_inference_service

# ─── Extended capture with CUDA Graph visibility (Runbook 5/7) ───
nsys profile -o graph_debug \
  --trace=cuda,osrt,nvtx \
  --cuda-graph-trace=node \
  --trace-fork-before-exec=true \
  ./vllm-serve

# ─── Multi-GPU / NCCL capture (Runbook 7: TP issues) ───
nsys profile -o multi_gpu \
  --trace=cuda,nvtx,nccl \
  --gpu-metrics-device=all \
  python -m vllm.entrypoints.openai.api_server \
  --tensor-parallel-size 4

# ─── Duration-limited capture (production, minimal overhead) ───
nsys profile -o prod_sample \
  --duration=30 \
  --trace=cuda,nvtx \
  --sampling-period=1000000 \
  ./fraud-scoring-service

# ─── Stats report (CLI analysis without GUI) ───
nsys stats <output>.nsys-rep --report cuda_gpu_kern_sum     # Kernel time summary
nsys stats <output>.nsys-rep --report cuda_api_sum          # CUDA API call summary
nsys stats <output>.nsys-rep --report cuda_gpu_mem_time_sum # Memory transfer summary
nsys stats <output>.nsys-rep --report nvtx_sum              # NVTX marker summary
```

**What to look for in nsys timeline:**

| Pattern | Indicates | Runbook Reference |
|---------|-----------|-------------------|
| Gaps between kernels | Launch overhead (CPU→GPU dispatch) | Runbook 5 (CUDA Graphs) |
| Large prefill kernel blocking decode | Prefill/decode interference | Runbook 7 (chunked prefill) |
| `cudaMemcpy HtoD` with staging | Pageable memory (not pinned) | Runbook 7 (H2D regression) |
| NCCL AllReduce asymmetry | One GPU lagging (topology/NUMA) | Runbook 7 (TP drift) |
| Single `cudaGraphLaunch` | Graph replay working correctly | Runbook 5 (healthy path) |
| 45+ `cudaLaunchKernel` calls | Graph invalidated, fell to eager | Runbook 5 (regression) |
| GPU idle while CPU busy | Python GIL, serialization | Runbook 1 (original) |
| Overlapped streams | H2D + compute pipelining working | CUDA Story 3 (pinned) |

#### Nsight Compute (ncu) — Kernel-Level Deep Dive

> **When to use:** After nsys identifies a slow kernel. ncu profiles *one kernel* in extreme detail: occupancy, memory throughput, compute throughput, roofline position. Answers: "Why is THIS kernel slow?"

```bash
# ─── Quick roofline analysis (is kernel compute-bound or memory-bound?) ───
ncu --set roofline -o kernel_roofline \
  --kernel-name "flash_attn_fwd" \
  ./inference_service

# ─── Full analysis (all sections) ───
ncu --set full -o kernel_full \
  --kernel-name "flash_attn_fwd" \
  --launch-count 5 \
  ./inference_service

# ─── Specific metrics for memory-bound kernels ───
ncu --metrics \
  sm__throughput.avg.pct_of_peak_sustained_elapsed,\
  dram__throughput.avg.pct_of_peak_sustained_elapsed,\
  l1tex__throughput.avg.pct_of_peak_sustained_elapsed,\
  sm__warps_active.avg.pct_of_peak_sustained_elapsed \
  --kernel-name "rms_norm" ./inference_service

# ─── Compare two runs (before/after optimization) ───
ncu --set full -o before ./service_before
ncu --set full -o after ./service_after
# Then in GUI: File → Open → Compare
```

**Key Nsight Compute metrics and interpretation:**

| Metric | What It Tells You | Action If Bad |
|--------|-------------------|---------------|
| SM Throughput % | Compute utilization | Low → kernel too small (need batching/fusion) |
| DRAM Throughput % | HBM bandwidth usage | High + low SM → memory-bound (need fusion/quantization) |
| Achieved Occupancy | Warps active per SM | Low → register pressure or shared mem limit |
| Warp Stall Reasons | Why warps wait | Long scoreboard → HBM latency; barrier → sync overhead |
| L1/L2 Hit Rate | Cache effectiveness | Low L1 → non-coalesced access; low L2 → working set too large |
| Tensor Core Utilization | TC usage | Low under batch inference → batch size too small |

#### Roofline Model Interpretation

```
                ▲ FLOP/s
                │          ┌────── Compute Ceiling (peak FLOP/s)
                │         /│
                │        / │
                │       /  │   ★ = Your kernel
                │      /   │
                │     /    │
                │    /     │
                │   /      │
                │  / Memory │ Ceiling (peak GB/s × arithmetic intensity)
                │ /        │
                └──────────┴──────────► Arithmetic Intensity (FLOP/byte)

    Left of ridge: MEMORY-BOUND (optimize data movement, caching, quantization)
    Right of ridge: COMPUTE-BOUND (optimize parallelism, tensor cores, fusion)
    Below both: LATENCY-BOUND (occupancy, launch overhead, synchronization)
```

### 2D: LLM-Specific Profiling

#### vLLM Internal Debugging

```bash
# ─── Debug endpoint (block-level KV cache state) ───
curl http://localhost:8000/debug/block_stats
# Returns: allocated_blocks, free_blocks, fragmentation_ratio

# ─── Request-level tracing ───
# Enable with environment variable:
VLLM_TRACE_FUNCTION=1 vllm serve ...
# Produces per-request lifecycle traces (prefill time, decode steps, evictions)

# ─── Scheduler state inspection ───
curl http://localhost:8000/metrics | grep -E "running|waiting|preempt|swap"

# ─── Token-level timing ───
# Custom instrumentation:
import time
start = time.perf_counter_ns()
output = llm.generate(prompt)
ttft_ns = output.metrics.first_token_time - start
tpot_ns = (output.metrics.finished_time - output.metrics.first_token_time) / output.outputs[0].token_count
```

#### Tokenizer/Detokenizer Profiling (Python GIL Issues)

```bash
# ─── py-spy: Python profiling without modifying code ───
py-spy top --pid $(pgrep -f "vllm.entrypoints")     # Live top-like view
py-spy record -o profile.svg --pid $(pgrep -f vllm)  # Flamegraph SVG

# ─── GIL contention measurement ───
# Shows when threads are waiting on GIL vs doing useful work
py-spy record --gil --pid $(pgrep -f vllm) -o gil_profile.svg

# If tokenizer dominates → move to Rust tokenizer (HuggingFace tokenizers crate)
```

#### Model Loading & Initialization Profiling

```bash
# ─── Time model loading (cold start optimization) ───
time python -c "
from vllm import LLM
import torch
torch.cuda.synchronize()
start = torch.cuda.Event(enable_timing=True)
end = torch.cuda.Event(enable_timing=True)
start.record()
llm = LLM('meta-llama/Llama-3.1-70B', tensor_parallel_size=4)
end.record()
torch.cuda.synchronize()
print(f'Model load: {start.elapsed_time(end)/1000:.1f}s')
"

# ─── Check if model weights are mmap'd (zero-copy from NVMe) ───
cat /proc/$(pgrep -f vllm)/maps | grep -c "model"
# Many mmap entries = safetensors mmap working (good)
# If model loaded into anonymous memory → falling back to full copy (bad)
```

### 2E: Data Transfer & Zero-Copy Profiling

#### Host-to-Device (H2D) / Device-to-Host (D2H) Transfer Analysis

```bash
# ─── nsys: identify transfer type (pinned vs pageable) ───
nsys stats <trace>.nsys-rep --report cuda_gpu_mem_time_sum
# Look for:
#   [CUDA Memcpy HtoD] — if avg time >> expected for payload size → pageable (Runbook 7)
#   Pinned 4MB transfer: ~130µs expected on PCIe Gen5
#   Pageable 4MB transfer: ~260µs (2× slower due to staging copy)

# ─── bandwidthTest: CUDA SDK benchmark ───
# Tests all transfer modes:
/usr/local/cuda/samples/1_Utilities/bandwidthTest/bandwidthTest
# Pinned: ~26 GB/s (PCIe Gen4 x16)
# Pageable: ~13 GB/s (half speed)

# ─── Custom H2D benchmark (Fiserv Story 3 pattern) ───
python -c "
import torch, time
sizes = [1024, 4096, 16384, 65536, 262144, 1048576, 4194304]  # bytes
for size in sizes:
    # Pageable
    h_page = torch.randn(size // 4)
    start = time.perf_counter_ns()
    d = h_page.cuda()
    torch.cuda.synchronize()
    t_page = (time.perf_counter_ns() - start) / 1000  # µs

    # Pinned
    h_pin = torch.randn(size // 4).pin_memory()
    start = time.perf_counter_ns()
    d = h_pin.cuda(non_blocking=True)
    torch.cuda.synchronize()
    t_pin = (time.perf_counter_ns() - start) / 1000

    print(f'{size:>10} bytes | pageable: {t_page:>8.1f}µs | pinned: {t_pin:>8.1f}µs | ratio: {t_page/t_pin:.2f}x')
"
```

#### Zero-Copy & Shared Memory Verification (Runbook 6: Siri)

```bash
# ─── Verify shared memory mapping ───
# Check process is using shared memory (not copying)
cat /proc/$(pgrep siri-pipeline)/maps | grep "/dev/shm"
# Should show: rw-s (shared mapping), not rw-p (private copy)

# ─── Shared memory throughput benchmark ───
# Write/read bandwidth to /dev/shm (should be DRAM speed: ~50 GB/s)
dd if=/dev/zero of=/dev/shm/bench bs=1G count=4 oflag=direct 2>&1 | tail -1
dd if=/dev/shm/bench of=/dev/null bs=1G count=4 iflag=direct 2>&1 | tail -1

# ─── Apache Arrow IPC verification (Runbook 5/8: inter-tier) ───
python -c "
import pyarrow as pa
import pyarrow.ipc as ipc

# Verify zero-copy read (no deserialization overhead)
reader = ipc.open_file('/dev/shm/arrow_audit')
batch = reader.get_batch(0)
# Check if buffer address is in shared memory range
print(f'Buffer address: {batch.column(0).buffers()[1].address:#x}')
print(f'Buffer size: {batch.column(0).buffers()[1].size} bytes')
# Address should be in /dev/shm mmap range (verify with /proc/PID/maps)
"

# ─── DLPack / CUDA IPC verification (GPU tensor sharing between processes) ───
python -c "
import torch
import torch.multiprocessing as mp
# Verify CUDA IPC handle works (for GPU-resident data sharing)
t = torch.randn(1000, 1000, device='cuda')
handle = t.storage()._share_cuda_()
print(f'CUDA IPC handle: {handle}')  # Should return valid handle
"
```

#### Service-to-Service Transfer (gRPC / Network Hop)

```bash
# ─── gRPC latency measurement ───
grpc_health_probe -addr=localhost:50051 -connect-timeout=100ms

# ─── ghz: gRPC benchmarking tool ───
ghz --insecure --call=inference.InferenceService/Predict \
  --data='{"features": [1.0, 2.0, ...]}' \
  --connections=10 --concurrency=50 --total=10000 \
  localhost:50051
# Reports: avg/p50/p99 latency, throughput, error rate

# ─── Measure serialization overhead (protobuf vs Arrow vs raw) ───
python -c "
import time, json, struct
import pyarrow as pa
import numpy as np

data = np.random.randn(512).astype(np.float32)  # Feature vector

# JSON serialization
start = time.perf_counter_ns()
for _ in range(10000):
    _ = json.dumps(data.tolist())
json_time = (time.perf_counter_ns() - start) / 10000 / 1000  # µs

# Arrow zero-copy (just wraps existing buffer)
start = time.perf_counter_ns()
for _ in range(10000):
    _ = pa.array(data)
arrow_time = (time.perf_counter_ns() - start) / 10000 / 1000

# Raw struct pack
start = time.perf_counter_ns()
for _ in range(10000):
    _ = struct.pack(f'{len(data)}f', *data)
struct_time = (time.perf_counter_ns() - start) / 10000 / 1000

print(f'JSON: {json_time:.1f}µs | Arrow wrap: {arrow_time:.1f}µs | struct: {struct_time:.1f}µs')
# Typical: JSON ~200µs, Arrow ~5µs, struct ~50µs
"
```

---

## Phase 3: Benchmarking & Validation (Confirming Hypothesis Before Fix)

> Before implementing a fix, validate your hypothesis with targeted benchmarks.

### GPU Compute Benchmarks

```bash
# ─── GEMM throughput (matmul — core LLM operation) ───
python -c "
import torch, time
# Simulate decode GEMM: (batch, hidden) × (hidden, hidden)
M, N, K = 16, 8192, 8192  # batch=16, hidden=8192
a = torch.randn(M, K, device='cuda', dtype=torch.float16)
b = torch.randn(K, N, device='cuda', dtype=torch.float16)

# Warmup
for _ in range(100): _ = torch.mm(a, b)
torch.cuda.synchronize()

# Benchmark
iters = 1000
start = torch.cuda.Event(enable_timing=True)
end = torch.cuda.Event(enable_timing=True)
start.record()
for _ in range(iters): _ = torch.mm(a, b)
end.record()
torch.cuda.synchronize()
elapsed_ms = start.elapsed_time(end) / iters
tflops = 2 * M * N * K / (elapsed_ms / 1000) / 1e12
print(f'GEMM ({M}×{K}) × ({K}×{N}): {elapsed_ms:.3f}ms = {tflops:.1f} TFLOPS')
# H100 peak: ~990 TFLOPS FP16 Tensor Core
"

# ─── Memory bandwidth benchmark ───
python -c "
import torch, time
size = 1024 * 1024 * 1024  # 1 GB
a = torch.randn(size // 4, device='cuda', dtype=torch.float32)
b = torch.empty_like(a)
torch.cuda.synchronize()
start = torch.cuda.Event(enable_timing=True)
end = torch.cuda.Event(enable_timing=True)
start.record()
b.copy_(a)
end.record()
torch.cuda.synchronize()
bw = 2 * size / (start.elapsed_time(end) / 1000) / 1e9
print(f'HBM bandwidth: {bw:.0f} GB/s')
# H100 peak: ~3.35 TB/s
"
```

### LLM Serving Benchmarks

```bash
# ─── vLLM benchmark suite (official) ───
python -m vllm.entrypoints.openai.api_server &  # Start server

# Throughput benchmark
python benchmarks/benchmark_throughput.py \
  --model meta-llama/Llama-3.1-70B \
  --input-len 2048 --output-len 512 \
  --num-prompts 1000

# Latency benchmark (single request, no batching interference)
python benchmarks/benchmark_latency.py \
  --model meta-llama/Llama-3.1-70B \
  --input-len 2048 --output-len 128 \
  --batch-size 1 --num-iters 50

# ─── genai-perf: NVIDIA's LLM benchmark (comprehensive) ───
genai-perf \
  -m meta-llama/Llama-3.1-70B \
  --service-kind openai \
  --endpoint-type chat \
  --streaming \
  --concurrency 32 \
  --measurement-interval 60000 \
  --url localhost:8000
# Reports: TTFT, TPOT, ITL (inter-token latency), throughput, GPU util

# ─── Custom TTFT/TPOT measurement ───
python -c "
import time, requests

url = 'http://localhost:8000/v1/completions'
payload = {
    'model': 'meta-llama/Llama-3.1-70B',
    'prompt': 'Analyze this transaction for fraud indicators: ...',
    'max_tokens': 256,
    'stream': True
}

start = time.perf_counter()
resp = requests.post(url, json=payload, stream=True)
first_token = None
tokens = 0
for line in resp.iter_lines():
    if line and first_token is None:
        first_token = time.perf_counter()
    if line:
        tokens += 1
end = time.perf_counter()

ttft = (first_token - start) * 1000
tpot = ((end - first_token) * 1000) / max(tokens - 1, 1)
print(f'TTFT: {ttft:.1f}ms | TPOT: {tpot:.1f}ms | Tokens: {tokens}')
"
```

### Network / Collective Benchmarks

```bash
# ─── NCCL all_reduce_perf (gold standard for collective perf) ───
cd /opt/nccl-tests
./build/all_reduce_perf -b 1M -e 1G -f 2 -g 4
# Reports: time (µs), algBW (GB/s), busBW (GB/s) per message size
# Expected busBW for 4×H100 NVLink: ~400-450 GB/s at large messages

# ─── all_gather_perf (used in tensor parallelism) ───
./build/all_gather_perf -b 1M -e 512M -f 2 -g 4

# ─── reduce_scatter_perf (used in tensor parallelism) ───
./build/reduce_scatter_perf -b 1M -e 512M -f 2 -g 4

# ─── Multi-node NCCL test ───
mpirun -np 16 --hostfile hosts.txt \
  -x NCCL_DEBUG=INFO -x NCCL_IB_DISABLE=0 -x NCCL_NET_GDR_LEVEL=5 \
  ./build/all_reduce_perf -b 8M -e 2G -f 2 -g 1
```

### Memory Subsystem Benchmarks

```bash
# ─── STREAM: memory bandwidth (host DRAM) ───
# Baseline for NUMA-local vs cross-NUMA access
numactl --cpunodebind=0 --membind=0 ./stream_c.exe  # NUMA-local
numactl --cpunodebind=0 --membind=1 ./stream_c.exe  # Cross-NUMA (should be ~30% slower)

# ─── Intel MLC (Memory Latency Checker) ───
mlc --latency_matrix       # Latency between each CPU and memory node
mlc --bandwidth_matrix     # Bandwidth between each CPU and memory node
# Reveals NUMA penalty: local=~80ns, remote=~130ns (1.6× penalty)

# ─── KV cache capacity validation (Runbook 7) ───
python -c "
# Calculate expected KV cache capacity vs actual
import sys
model = 'Llama-3.1-70B'
gpu_mem_gb = 80
model_weights_gb = 35  # FP16 70B ≈ 35GB with some overhead
cuda_overhead_gb = 5   # CUDA context + activations + graphs
available_for_kv = gpu_mem_gb - model_weights_gb - cuda_overhead_gb

# KV cache per token per layer
num_layers = 80
num_kv_heads = 8
head_dim = 128
dtype_bytes = 2  # FP16
kv_per_token = 2 * num_layers * num_kv_heads * head_dim * dtype_bytes  # bytes
kv_per_token_mb = kv_per_token / (1024**2)

max_seq_len = 8192
kv_per_seq_gb = kv_per_token * max_seq_len / (1024**3)

max_concurrent = available_for_kv / kv_per_seq_gb
print(f'Available for KV: {available_for_kv:.1f} GB')
print(f'KV per sequence ({max_seq_len} tokens): {kv_per_seq_gb:.3f} GB')
print(f'Max concurrent sequences: {max_concurrent:.0f}')
print(f'With FP8 KV cache: {max_concurrent * 2:.0f}')  # 2× with FP8
"
```

---

## Phase 4: Fix Verification (Confirming the Fix Worked)

> After applying a fix, re-run the relevant profiling tools to confirm improvement.

### Verification Checklist

| What Changed | Verify With | Expected Improvement |
|--------------|------------|---------------------|
| CUDA Graph recapture (Runbook 5) | `nsys stats --report cuda_api_sum` | `cudaGraphLaunch` count > 0, `cudaLaunchKernel` ≈ 0 |
| Arena sizing (Runbook 5) | `grep arena_overflow metrics.log` | 0 overflows |
| Micro-batch deadline (Runbook 5) | P99 latency metric | Within SLA at all traffic patterns |
| Shared memory layout (Runbook 6) | `cat /proc/PID/maps \| grep shm` | No compaction events |
| Session migration (Runbook 6) | `grep affinity_miss router.log` | < 1% miss rate during drain |
| Embedding projection (Runbook 6) | `nsys stats --report cuda_gpu_kern_sum` | No 35ms projection kernel |
| Buffer pool sizing (Runbook 6) | `grep pool_exhaustion metrics.log` | 0 fallback events |
| KV fragmentation (Runbook 7) | `vllm:num_preemptions_total` rate | < 5/min |
| Chunked prefill (Runbook 7) | TPOT P99 metric | < 50ms |
| NUMA pinning (Runbook 7) | `numastat -p PID` | 0 cross-NUMA allocation |
| Pinned memory (Runbook 7) | `nsys --report cuda_gpu_mem_time_sum` | H2D avg ≈ expected for pinned |
| Ring overflow (Runbook 8) | `ring_buffer_utilization` metric | < 90% under Tier 2 failure |
| HPA metrics (Runbook 8) | `kubectl get hpa` targets | Custom metric, not CPU |
| Arrow buffer leak (Runbook 8) | `buffer_pool_active` metric | Stable (not growing) |

### Continuous Profiling (Prevent Regression)

```bash
# ─── Scheduled nsys capture (cron-based sampling) ───
# Capture 60s every hour, archive for diff analysis
*/60 * * * * nsys profile --duration=60 -o /var/log/nsys/$(date +\%H).nsys-rep \
  --trace=cuda,nvtx --sampling-period=5000000 \
  --attach-pid=$(pgrep -f "fraud-scoring")

# ─── Automated regression detection ───
# Compare today's kernel times vs yesterday's baseline
python compare_nsys_reports.py \
  --baseline /var/log/nsys/baseline.nsys-rep \
  --current /var/log/nsys/$(date +%H).nsys-rep \
  --threshold 1.2  # Alert if any kernel > 1.2× slower

# ─── Prometheus recording rules for trend detection ───
# prometheus/rules.yaml
groups:
  - name: gpu_regression_detection
    rules:
      - record: inference:ttft_p99:5m
        expr: histogram_quantile(0.99, rate(vllm_e2e_request_latency_seconds_bucket[5m]))
      - alert: TTFTRegression
        expr: inference:ttft_p99:5m > 1.5 * avg_over_time(inference:ttft_p99:5m[7d])
        for: 10m
        labels:
          severity: warning
```

---

## Quick Reference: Tool Selection Decision Tree

```
What's the symptom?
│
├── High latency / slow responses
│   ├── GPU util LOW → nsys timeline (gaps? launch overhead? transfer stalls?)
│   ├── GPU util HIGH → ncu roofline (compute-bound? memory-bound?)
│   ├── CPU util HIGH → perf top (spin loops? GIL? serialization?)
│   └── Network involved → ss -ti (retransmits?) → iperf3 (bandwidth?)
│
├── Throughput collapse
│   ├── Gradual → vLLM metrics (preemptions? cache fragmentation?)
│   ├── Sudden → kubectl events + pod restarts + OOM killer (dmesg)
│   ├── Multi-GPU → NCCL_DEBUG + nccl-tests (topology? straggler?)
│   └── Growing queue → HPA state + resource limits + priority classes
│
├── OOM / Memory issues
│   ├── GPU OOM → nvidia-smi + vLLM cache metrics + memory budget calc
│   ├── Host OOM → free + /proc/PID/status + numastat (NUMA imbalance?)
│   ├── Shared memory → df /dev/shm + /proc/PID/maps (mmap regions)
│   └── Memory leak → track VmRSS over time + buffer pool active count
│
├── Jitter (P99 >> P50)
│   ├── GPU jitter → nsys (prefill interrupting decode? graph recompile?)
│   ├── Network jitter → mtr + ping stats (variable RTT?)
│   ├── CPU jitter → perf stat (context switches? IRQ interference?)
│   └── Scheduling → mpstat (softIRQ storm?) + isolcpus verification
│
└── Quality degradation (wrong output)
    ├── FP8 precision → compare BF16 vs FP8 output + ncu attention kernel
    ├── KV cache corruption → ECC check (dcgmi health) + restart pod
    └── Temperature → nvidia-smi -q -d TEMPERATURE (throttle → wrong compute?)
```

---

## Tool Cheat Sheet: One-Liners for Each Concern

| Concern | One-Liner | Expected Output |
|---------|-----------|-----------------|
| GPU utilization | `nvidia-smi dmon -d 1 -c 10 -s u` | SM%, Mem%, Enc%, Dec% |
| GPU memory | `nvidia-smi -q -d MEMORY \| grep -A3 "FB Memory"` | Used/Free/Total |
| GPU temperature | `nvidia-smi -q -d TEMPERATURE \| grep "GPU Current"` | Temp in °C |
| GPU ECC errors | `nvidia-smi -q -d ECC \| grep -A2 "Volatile"` | SBE/DBE counts |
| GPU topology | `nvidia-smi topo -m` | NV/PIX/SYS matrix |
| NVLink errors | `nvidia-smi nvlink -c` | CRC error counts |
| KV cache pressure | `curl -s :8000/metrics \| grep cache_usage` | 0.0–1.0 |
| TTFT/TPOT | `curl -s :8000/metrics \| grep time_to_first` | Seconds (histogram) |
| vLLM queue | `curl -s :8000/metrics \| grep requests_waiting` | Count |
| Preemptions | `curl -s :8000/metrics \| grep preemptions` | Counter (rate matters) |
| CPU per-core | `mpstat -P ALL 1 1` | %usr/%sys/%soft per core |
| Context switches | `vmstat 1 5 \| awk '{print $12}'` | cs/sec |
| NUMA balance | `numastat -p $(pgrep inference)` | Per-node allocation |
| Memory pressure | `cat /proc/pressure/memory` | some/full avg10/60/300 |
| Disk IO | `iostat -xz 1 3` | %util, await per device |
| Network drops | `ethtool -S eth0 \| grep -i "drop\|error"` | Should be 0 |
| TCP retransmits | `ss -ti \| grep retrans` | Per-connection |
| IB port status | `ibstat \| grep -E "State\|Rate"` | Active, 400 Gb/sec |
| NCCL transport | `NCCL_DEBUG=INFO ... 2>&1 \| grep "Using"` | NET/IB or P2P/NVLink |
| Pod health | `kubectl get pods -o wide -n inference` | Running, node assignment |
| HPA state | `kubectl get hpa -o wide` | Current/target metrics |
| Python GIL | `py-spy top --pid $(pgrep -f vllm)` | Function hotspots |
| Pinned memory | `cat /proc/meminfo \| grep Mlocked` | Amount locked |

---

## Interview Talking Points: Observability Philosophy

> Use these when asked "How do you approach monitoring/observability in production AI systems?"

**1. Four Layers of Observability:**
- **Application metrics** (Prometheus): Request latency, throughput, error rate, queue depth — *what* is wrong
- **GPU telemetry** (DCGM + vLLM): SM util, memory pressure, KV cache, preemptions — *where* in the GPU stack
- **System profiling** (nsys/ncu/perf): Kernel timelines, roofline, CPU hotspots — *why* it's slow
- **Infrastructure signals** (K8s events, NCCL debug, network stats): Topology, resource contention, scaling — *how* the environment contributed

**2. The Profiling Hierarchy (most to least frequent):**
```
Always-on:   Prometheus metrics + DCGM export (< 1% overhead)
Hourly:      60-second nsys samples (automated, archived)
On-alert:    Full nsys + NCCL debug (manual trigger)
Deep-dive:   ncu kernel analysis (offline, heavyweight)
Rare:        Multi-day memory/fragmentation tracking
```

**3. Key Insight From Our Runbooks:**
"The most dangerous production issues aren't the ones that crash — they're the ones that *degrade gradually*. KV cache fragmentation builds over days (Runbook 7). Arrow buffer leaks grow at 500MB/hour (Runbook 8). CUDA Graph invalidation looks normal from nvidia-smi (Runbook 5). You need *rate-of-change* alerts, not just threshold alerts."

---

# INTERVIEW PREPARATION

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

# STUDY PLAN & PROGRESS TRACKING

## 16-Week Study Plan

| Week | Focus Area                           | Key Deliverables                                      | Hours |
| ---- | ------------------------------------ | ----------------------------------------------------- | ----- |
| 1    | Story A: CapitalOne Fraud Platform   | Architecture diagram, metrics, Q&A memorized          | 10    |
| 2    | Story B: Fiserv LLM Orchestration    | Hierarchical context flow, KV cache routing           | 10    |
| 3    | Story C: Apple Siri Pipeline         | Shared memory design, 5-stage optimization            | 10    |
| 4    | Story D: Broadcom Multi-Model        | RequestContext struct, early-exit logic               | 10    |
| 5    | GPU Architecture & CUDA Fundamentals | Memory hierarchy, warp execution, occupancy           | 10    |
| 6    | CUDA Graphs & Profiling              | nsys/ncu workflow, 8 practical tasks                  | 10    |
| 7    | Rust Systems Patterns                | Arena allocators, SPSC queues, Arrow IPC              | 10    |
| 8    | C++ Systems Patterns                 | Shared memory, buffer pools, SIMD                     | 10    |
| 9    | Host Tuning                          | NUMA, huge pages, IRQ affinity, CPU isolation         | 10    |
| 10   | Kubernetes for GPU Workloads         | Device plugins, topology manager, autoscaling         | 10    |
| 11   | Networking & Storage                 | gRPC tuning, NCCL topology, GPFS/Lustre, GDS, tiering | 10    |
| 12   | Troubleshooting Runbooks             | High latency, OOM, quality degradation scenarios      | 10    |
| 13   | Mock Interviews (Technical)          | 3 mock sessions with feedback                         | 10    |
| 14   | Mock Interviews (Behavioral)         | STAR stories, leadership examples                     | 10    |
| 15   | System Design Practice               | 5 LLM serving design problems                         | 10    |
| 16   | Final Review & Rest                  | Light review, sleep, mental preparation               | 5     |

---

## Progress Tracker

### Weekly Checkpoints

| Week | Topic              | Hours Planned | Hours Actual | Completion % | Notes |
| ---- | ------------------ | ------------- | ------------ | ------------ | ----- |
| 1    | CapitalOne Story   | 10            |              |              |       |
| 2    | Fiserv Story       | 10            |              |              |       |
| 3    | Apple Story        | 10            |              |              |       |
| 4    | Broadcom Story     | 10            |              |              |       |
| 5    | GPU Architecture   | 10            |              |              |       |
| 6    | CUDA Profiling     | 10            |              |              |       |
| 7    | Rust Patterns      | 10            |              |              |       |
| 8    | C++ Patterns       | 10            |              |              |       |
| 9    | Host Tuning        | 10            |              |              |       |
| 10   | Kubernetes         | 10            |              |              |       |
| 11   | Networking/Storage | 10            |              |              |       |
| 12   | Troubleshooting    | 10            |              |              |       |
| 13   | Mock Technical     | 10            |              |              |       |
| 14   | Mock Behavioral    | 10            |              |              |       |
| 15   | System Design      | 10            |              |              |       |
| 16   | Final Review       | 5             |              |              |       |

### Confidence Self-Assessment

| Topic                     | Before (1-10) | After (1-10) | Improvement |
| ------------------------- | ------------- | ------------ | ----------- |
| Story Narratives          |               |              |             |
| GPU Architecture          |               |              |             |
| CUDA Profiling            |               |              |             |
| Rust Systems Programming  |               |              |             |
| C++ Systems Programming   |               |              |             |
| Host Tuning               |               |              |             |
| Kubernetes GPU Scheduling |               |              |             |
| Networking Optimization   |               |              |             |
| Troubleshooting           |               |              |             |
| System Design             |               |              |             |
| Behavioral Interviews     |               |              |             |

---

## Key Numbers to Memorize

| Fact                                | Number                             |
| ----------------------------------- | ---------------------------------- |
| CUDA kernel launch overhead         | ~5–15 µs                         |
| H100 HBM bandwidth                  | 3.35 TB/s                          |
| H100 FP16 TFLOPS                    | 1,979                              |
| H100 SMs                            | 132                                |
| NVLink 4 bandwidth (per link)       | 900 GB/s bidirectional             |
| PCIe Gen5 bandwidth                 | ~64 GB/s                           |
| NVLink vs PCIe speedup              | ~14×                              |
| Typical AllReduce latency (NVLink)  | 0.1–0.3 ms                        |
| Typical AllReduce latency (PCIe)    | 0.5–2.0 ms                        |
| malloc latency                      | 100–500 ns                        |
| Pool acquire latency                | ~50 ns                             |
| Shared memory access                | ~20 cycles (~15 ns)                |
| L2 cache access                     | ~200 cycles                        |
| HBM access                          | ~400 cycles                        |
| gRPC roundtrip (same datacenter)    | 0.5–2 ms                          |
| JSON serialization (feature vector) | ~200 µs                           |
| Arrow wrap (zero-copy)              | ~0 µs (pointer assignment)        |
| XGBoost inference (500 trees)       | <100 µs                           |
| vLLM decode step (8B, batch=1)      | ~14 ms                             |
| Llama 8B weights (FP16)             | ~16 GB                             |
| KV cache per token (Llama 8B, FP16) | ~128 KB                            |
| NVMe SSD bandwidth (single)         | 7 GB/s (Gen4) / 14 GB/s (Gen5)     |
| Lustre aggregate bandwidth          | 50–2000+ GB/s (cluster-dependent) |
| GPFS aggregate bandwidth            | 50–2000+ GB/s (cluster-dependent) |
| GPUDirect Storage throughput        | ~25 GB/s (PCIe Gen5 x16)           |
| NFS max throughput (single server)  | ~3 GB/s                            |
| Model load 70B FP8 (NVMe+GDS)       | ~2.8 s                             |
| Model load 70B FP8 (naive single)   | ~12.8 s                            |
| Checkpoint 405B FP32 size           | ~1.6 TB                            |

---

*End of Master Syllabus*

**Remember:** This syllabus covers the **full stack** — from writing NUMA-aware C++ to deploying it on a properly-tuned Kubernetes cluster to troubleshooting production performance issues with flame graphs and eBPF. The four projects form a complete narrative:

```
Story A: CapitalOne — Ultra-low-latency fraud detection (5ms @ 24K TPS)
         ↓
Story B: Fiserv — Large-context LLM orchestration (8.5s → 2.3s)
         ↓
Story C: Apple — Multi-stage conversational pipeline (850ms → 280ms)
         ↓
Story D: Broadcom — Unified multi-model security platform (350ms → 85ms)
```

**You've got this!** 💪
