# Revamped STAR Stories — AI System Engineering Portfolio

> **Updated:** May 2026
> **Context:** Capital One, Apple, Broadcom/Symantec
> **Domains:** HPC, GPU/CUDA, Zero-Copy, Search, LLM Serving, AI System Engineering
> **Format:** Category → Theme → Situation → Task → Action → Result → Lesson Learnt

---

# Story 1 – Capital One – Customer Obsession & Zero-Copy GenAI Platform

**Category:** Story 1 – Difficult Customer / Customer Obsession
**Theme:** RAG chatbot + agent assistant rollout for a skeptical Ops VP; GenAI, RAG, CUDA Graphs, Arrow IPC, observability, zero-copy architecture.

## S – Situation

Capital One's CardTech team owned a legacy Conversation API built with legacy NLP solutions that was failing systematically. The credit card support center was under severe pressure:

- Customers routinely bypassed the automated system by saying "agent" to reach live help — deflection was near zero
- Agents spent **1–3 minutes manually searching** through wikis, SharePoint, PDFs, and policy docs to answer even routine questions on products, fees, and policies
- The cost was high — agent handle time + multiple model/platform teams maintaining separate stacks (NLP, rules engine, IVR, web chat)
- New feature or policy rollouts required coordination across several teams, making time-to-market for changes very slow
- There was **no end-to-end explainability or observability** — impossible to see which intent fired, what knowledge source was used, or why the system failed

The Ops VP was openly hostile toward "chatbot hype" due to a failed earlier bot and cited prior bad CX and hallucinations. Previous attempts had damaged stakeholder trust — Operations leaders were skeptical about another AI initiative unless it clearly improved CSAT and deflection.

Business objectives were: (1) reduce tripping from automated system to Agents by **50%** in Phase-1, (2) reduce agent response time from **1–3 min to 10–30 sec** with an AI assistant, and (3) reduce operational cost by reorganizing teams and services.

## T – Task

Own the end-to-end design and rollout of the GenAI chatbot + agent assistant platform:

- Prove safe, grounded answers using enterprise knowledge bases with RAG
- Hit target of **15–20% self-service deflection** in 3 months
- Reduce agent response time from 1–3 min to 10–30 sec
- Win over the skeptical Ops VP by showing reliability, not just demos
- Build a zero-copy architecture that eliminates serialization overhead between pipeline stages
- Deliver end-to-end explainability (which intent fired, which KB source was used, why a failure happened)

## A – Action

### 1. Designed a Three-Tier Zero-Copy Architecture

Instead of treating the chatbot, agent assistant, and scoring as separate services, I architected a **unified platform with shared immutable Arrow event buffers** between tiers:

```
┌─────────────────────────────────────────────────────────────────┐
│         TIER 1 — Transaction Decisioning (< 5 ms p99)            │
│  • Per-core Rust/CUDA scoring pipeline                          │
│  • Borrowed TxnView (zero heap allocs) → FeatureBlock           │
│  • CUDA Graphs for GPU scoring (145µs launch savings)           │
│  • SPSC ring buffers (lock-free, per-core)                      │
└─────────────────────────────────────────────────────────────────┘
                                    │
                      Shared Immutable Arrow Event Buffer
                          (shm_open + mmap — zero-copy)
                                    ▼
        ┌───────────────────────────┴───────────────────────────┐
        ▼                                                       ▼
┌─────────────────────────┐                         ┌─────────────────────────┐
│   TIER 2 — Agent        │                         │   TIER 3 — Customer     │
│   Assistant (2–5 sec)   │                         │   Chatbot (5–10 sec)    │
│  • 13B reasoning model  │                         │  • Hybrid RAG pipeline  │
│  • TensorRT-LLM (FP8)   │                         │  • OpenSearch + PGVector │
│  • Triton + Sentinel    │                         │  • LangGraph multi-agent│
│  • Reads Arrow buffer   │                         │  • JSON-schema outputs  │
└─────────────────────────┘                         └─────────────────────────┘
```

### 2. Implemented Zero-Copy Data Flow with Arrow IPC

Replaced JSON/protobuf serialization between tiers with **Apache Arrow columnar buffers** published via shared memory. The C++ hot-path engine wrote Arrow RecordBatches directly into a `shm_open` + `mmap` region backed by hugepages and NUMA-pinned to the correct node:

- Tier 1 writes once to Arrow buffer (zero-copy from FeatureBlock)
- Tiers 2/3 read by reference — no deserialization, no network hop
- Same physical bytes feed reasoning, chatbot, logging, analytics, retraining
- Reduced per-transaction bytes copied from **2 KB to 47 bytes**
- Control flags on separate cache lines with release/acquire fences for cross-process signaling

### 3. Built NUMA-Aware, SIMD-Accelerated Hot-Path Engine in C++

The Python hot path consumed ~18 ms P99 — nearly all in serialization, memory allocation, GIL contention, and cross-boundary copies. Actual compute was < 3 ms. I rewrote it in C++ as a shared library callable from Rust via FFI:

- **NUMA-pinned memory arena:** `numa_alloc_onnode` + `MADV_HUGEPAGE` + pre-faulting → zero cross-NUMA accesses, TLB miss rate from 4.2% → 0.15%
- **SIMD-accelerated zero-copy parser:** AVX2 delimiter search processing 32 bytes per instruction, producing `FieldView` pointers into the original receive buffer (zero copies, zero allocations)
- **Lock-free SPSC ring buffers:** Cacheline-padded head/tail, power-of-2 capacity with bitwise AND wrap-around, acquire/release ordering → < 0.1 µs inter-stage communication
- **Rust FFI:** `shm_open` shared memory region with `#[repr(C, align(64))]` layout — Rust writes transaction data directly into mmap'd region, C++ reads from same physical pages

### 4. CUDA Graphs for GPU Scoring Branch

Profiled with **Nsight Systems** and found ~45 small kernel launches per inference with visible idle gaps (5–15 µs each). Captured the entire forward pass as a single CUDA Graph:

- Pre-allocated pinned host buffers (`cudaMallocHost`) on NUMA node closest to GPU
- Captured: H2D → model forward (all 45 kernels) → D2H as single graph
- Launch overhead: **150 µs → 5 µs** (30x reduction)
- Pre-captured graphs for common batch sizes (1, 4, 8, 12, 16)

### 5. Hybrid RAG + Multi-Agent Workflow for Customer Chatbot

- LangGraph for multi-agent orchestration (policy lookup, fee calculator, dispute flow)
- OpenSearch (BM25) + PGVector (semantic) hybrid retrieval for knowledge grounding
- JSON-schema outputs (Pydantic) to keep responses structured and controllable
- Full AI observability: OpenTelemetry traces, LangSmith runs, dashboards for groundedness, escalation rate, latency, hallucination flags

### 6. Phased Rollout and Stakeholder Management

When the Ops VP wanted "full launch to 100% traffic" after a strong POC, I pushed back with data:

- Proposed phased rollout (5% → 25% → 50%) with guardrails and auto-escalation thresholds
- Backed with data: what failure modes would look like at scale and how we'd detect/mitigate
- Weekly review with Ops on real transcripts and error cases

## R – Result

Within 8–10 weeks:

- **~22% deflection** on eligible intents (target was 15–20%)
- **~35% reduction** in manual handle time on calls that started with the bot
- Agent response time reduced from **1–3 min to 10–30 sec** with AI assistant
- "Unhelpful / misleading" responses cut to **low single digits** through RAG + guardrails
- GPU scoring branch achieved **3.8 ms p99** (within 5 ms SLA)
- Zero-copy Arrow boundary eliminated **2 KB of serialization per transaction**
- TLB miss rate: 4.2% → 0.15%; cross-NUMA accesses: ~2,800 → 0 per request
- Ops VP became a champion; the earlier "failed bot story" was replaced by this success
- Architecture reused for disputes and loans domains

## Lesson Learnt

1. **Zero-copy is the ultimate enabler for multi-tier AI systems.** When data flows between stages without reserialization, you eliminate the biggest source of latency. Arrow IPC + shared memory (`shm_open` + `mmap` + hugepages) turned a 30–80 ms serialization tax into < 2 ms.
2. **CUDA Graphs don't reduce HBM traffic — they eliminate launch overhead.** If unfused operation chains remain, kernel fusion is also needed. CUDA Graphs are best when the execution shape is stable and launch overhead dominates. Nsight Systems made this immediately visible.
3. **Phased rollouts with guardrails build trust faster than big-bang launches.** The Ops VP became a champion not because of the demo, but because we showed we could detect and recover from failure modes at scale. Data-driven pushback ("you want X% but I recommend Y% because metrics show Z") is how you earn credibility with skeptical stakeholders.

**Covers keywords:** Difficult customer, Went above & beyond, Pushing back on customer, Customer wanted X but needed Y, Most impactful customer win, Zero-copy (shm_open, mmap, Arrow IPC), CUDA Graphs, SIMD parsing, NUMA-aware arena, SPSC ring buffers, RAG, Observability, Three-tier architecture

---

# Story 2 – Apple – Ownership & GPU-Optimized Search Pipeline Under Deadline

**Category:** Story 2 – Ownership & Big Delivery Under Pressure
**Theme:** Siri search/recommendation pipeline with FAISS GPU, Metal command buffer batching, hybrid retrieval under hard OS launch deadline; stepping up as de facto technical owner.

## S – Situation

At Apple, Siri's knowledge-graph search + recommendation stack was missing relevance targets ahead of a major OS release. The existing pipeline relied heavily on keyword/BM25 search and classic ML rankers, which struggled with complex, conversational, or ambiguous queries.

A new LLM-augmented query understanding + neural re-ranking prototype existed but lived as a research POC with fragile scripts and no production readiness. Multiple teams were involved — Search, Knowledge Graph, Personalization, Infra — and ownership for "end-to-end delivery" was unclear, leading to delays and finger-pointing.

Latency budgets for Siri were strict (multi-device, global footprint, voice UX, ~200–300 ms end-to-end cloud budget), so naive LLM integration risked breaking p95 latency SLOs. Leadership had committed to relevance improvements in this OS cycle, so there was a firm deadline with high visibility, but the project timeline was already slipping. On-call teams lacked clear dashboards or metrics for the LLM-assisted pipeline.

## T – Task

Step up as the de facto technical owner and:

- Stabilize and productionize the LLM-assisted search pipeline
- Hit relevance and latency SLOs in time for the OS launch
- Coordinate 3–4 cross-functional teams in ~12 weeks
- Integrate GPU-accelerated vector search (FAISS) for semantic retrieval
- Build production observability for the new pipeline

## A – Action

### 1. Took End-to-End Ownership

Mapped all components: query logs → embeddings → KG search → LLM re-ranker → final answer. Identified the critical path and removed nonessential "nice-to-haves" from v1. Defined clear interfaces between teams to eliminate finger-pointing.

### 2. Re-Architected with Hybrid Retrieval + FAISS GPU

Introduced a **hybrid retrieval pipeline** running keyword (OpenSearch BM25) and semantic (FAISS GPU) search in parallel, fused via Reciprocal Rank Fusion (RRF):

- **FAISS GPU index design:** IVF+PQ with 4096 clusters, nprobe=64, PQ compression to 32 bytes/vector — 2M docs × 768D = ~6 GB raw → ~64 MB (94x memory reduction)
- **Sub-ms in-process search:** FAISS GPU (GpuIndexIVFPQ) delivered 0.4 ms per query with zero network hop
- **Embedding cache:** ~40% cache hit rate on production traffic, saving ~2 ms per hit
- Index rebuilt daily from Apple's content pipeline, loaded at startup into the shared memory arena

### 3. Metal Command Buffer Batching (Apple's CUDA Graphs Equivalent)

The NLU transformer forward pass consisted of ~30 small Metal compute shader dispatches. Each dispatch had CPU-side overhead of ~3–5 µs, totaling ~100–150 µs of pure dispatch overhead. I worked with the runtime team to implement Metal command buffer batching:

- Encoded the entire forward pass into a **single command buffer** and committed it once
- Dispatch overhead: **120 µs → 5 µs** (24x reduction), conceptually identical to CUDA Graphs

### 4. Micro-Batching for GPU Utilization

Implemented a **bounded micro-batch accumulator** between the shared-memory session queue and the model inference call:

- Maximum batch size: 8 sessions, maximum wait time: 500 µs
- Deadline-aware: dispatch immediately if oldest request is near SLA
- GPU utilization: **15% → 65%** (4.3x better), throughput improved 4x with only ~300 µs additional latency

### 5. Zero-Copy Infrastructure with Shared Memory Arena

Replaced inter-stage gRPC calls with a **shared memory arena** backed by hugepage-mapped memory (`MAP_HUGETLB`), offset-based references (safe across processes), cacheline-aligned allocations, and SPSC descriptor rings carrying only 4-byte offsets. This eliminated 30–80 ms of inter-stage serialization and network overhead.

### 6. Built Robust Data & Evaluation Loops

- Offline: curated eval sets from query logs by intent, language, and device type
- Online: A/B tests with guardrails for latency and click-through degradation
- Exposed metrics (p50/p95 latency, recall@k, click-through) into unified dashboards
- Defined on-call runbooks and error budgets with the platform team

## R – Result

Delivered the pipeline on time for launch:

- **~12–15% lift** in top-1 answer precision
- p95 latency **within SLO** despite LLM re-ranking
- FAISS GPU search: **0.4 ms** per query (sub-ms in-process)
- Dispatch overhead reduced from **120 µs to 5 µs** (24x reduction)
- GPU utilization increased from **15% to 65%** (4.3x better)
- Significantly fewer "no answer" / generic responses
- Leadership recognized me as the technical owner for Siri's LLM-augmented search
- Architecture reused for other verticals across Siri

## Lesson Learnt

1. **Hybrid retrieval (BM25 + semantic) is the production answer.** Pure semantic search misses exact-match queries; pure keyword misses intent. RRF fusion gives the best of both. FAISS GPU in-process beats vector databases for latency when your index is rebuilt daily.
2. **GPU dispatch overhead matters as much as kernel execution.** 30 small dispatches at 3–5 µs each = 90–150 µs of pure overhead. Command buffer batching / CUDA Graphs collapses this to 5 µs. This is a system-level fix, not a kernel rewrite.
3. **Ownership means defining interfaces, not owning code.** The finger-pointing stopped when I mapped every component, defined clear contracts between teams, and tracked end-to-end latency. Extreme ownership isn't about doing everything — it's about ensuring nothing falls through the cracks.

**Covers keywords:** Extreme ownership, Impossible deadline, Took over failing project, High-pressure delivery, FAISS GPU, Metal command buffer batching, Hybrid retrieval, Micro-batching, Shared memory arena, SPSC rings

---

# Story 3 – Capital One – HPC Fraud Detection with CUDA Profiling & System-Level Optimization

**Category:** Story 3 – HPC, GPU/CUDA Profiling & System Engineering
**Theme:** Real-time fraud scoring infrastructure redesigned from host level through Kubernetes through GPU; profiling-first methodology with Nsight Systems, CUDA Graphs, micro-batching, pinned memory, NUMA pinning, HugePages, and AF_XDP networking.

## S – Situation

I was brought in as a System Engineer to design and build the real-time fraud scoring infrastructure for a large payment processor handling **~12,000 transactions per second** across 3 geographic regions. The existing system was a legacy Java microservices stack running on generic Kubernetes with **~35 ms P99 latency** for fraud scoring. Business required **sub-10 ms P99 end-to-end** for the hot-path fraud decision to meet card-network SLA requirements and reduce false declines.

The scoring pipeline was: feature extraction (15 features from transaction history) → XGBoost ensemble (CPU) + small transformer classifier (GPU) → 200+ business rules → decision. The GPU branch ran a distilled transformer classifier alongside CPU-based XGBoost models.

During a capacity review, we noticed that even though individual kernel execution times were fast, the **overall GPU inference time was higher than expected**. The model itself was small and well-optimized, so the suspicion was that overhead outside of actual compute was the bottleneck.

## T – Task

Redesign the infrastructure from host level through Kubernetes through networking through GPU:

- Profile the GPU scoring path to identify the real bottleneck using Nsight
- Optimize kernel launch overhead, data transfer, and GPU utilization
- Achieve sub-10 ms P99 end-to-end while maintaining 12,000+ TPS
- Build a profiling-first methodology the team could reuse

## A – Action

### Phase 1: Host-Level Foundation (bare-metal nodes)

**CPU isolation and NUMA pinning:** Configured `isolcpus=4-19,24-39 nohz_full=4-19,24-39 rcu_nocbs=4-19,24-39` — removed isolated cores from general scheduler, disabled timer interrupts, offloaded RCU callbacks. Pinned feature extraction to cores 4–11 (NUMA 0, close to NIC), model inference to cores 12–19 (NUMA 0, close to GPU), rule engine to cores 24–31 (NUMA 1).

**HugePages:** Allocated 2 MB HugePages (8,192 pages = 16 GB) — TLB miss rate dropped from **4.2% → 0.3%** verified with `perf stat -e dTLB-load-misses`.

**Kernel tuning:** `vm.swappiness=0`, tuned TCP buffer sizes, disabled Transparent HugePages (khugepaged causes latency spikes), increased `somaxconn` and `netdev_max_backlog`.

**IRQ affinity:** Pinned NIC IRQs to housekeeping cores (0–3, 20–23) — kept scoring cores interrupt-free. Configured NIC ring buffers to 8192, enabled RFS for socket-to-queue affinity.

**AF_XDP for feature store:** Evaluated DPDK (Redis RTT 2.1 ms → 0.4 ms) but chose AF_XDP (0.7 ms RTT with much simpler operations via eBPF/XDP fast path).

### Phase 2: Kubernetes Configuration

- **Static CPU Manager policy:** Guaranteed QoS pods get exclusive CPU sets
- **Topology Manager:** `single-numa-node` — all pod resources from same NUMA node
- **Memory Manager:** HugePages + 8 GB `/dev/shm` for IPC, `shareProcessNamespace: true`
- **PodDisruptionBudget:** `maxUnavailable: 0` — no voluntary evictions
- **PriorityClass:** 1,000,000 value with `PreemptLowerPriority`
- **SR-IOV:** Dedicated VF per fraud-scorer pod for direct NIC access
- **NVMe:** Local P5800X for model weight caching and transaction log buffering

### Phase 3: GPU Profiling & Optimization

**Nsight Systems profiling** revealed the root cause:

```bash
nsys profile --trace=cuda,osrt,nvtx -o fraud-scoring-baseline ./fraud-scoring-service
```

Key findings: ~45 small kernel launches per inference step with visible idle gaps (5–15 µs each). Total launch overhead: ~150 µs per inference. Individual kernels completed in 2–8 µs — bottleneck was **launch overhead, not kernel execution**.

**CUDA Graph capture:** Captured H2D → model forward (all 45 kernels) → D2H as single graph. Pre-allocated pinned host buffers (`cudaMallocHost`). Replayed per micro-batch. Launch overhead: **150 µs → 5 µs** (30x reduction).

**Bounded micro-batching:** Accumulator with max 500 µs window, max 16 requests per batch, deadline-aware dispatch. Pre-captured CUDA Graphs for batch sizes 1, 4, 8, 12, 16. GPU SM utilization: **18% → 72%**.

**Pinned memory + async DMA:** Replaced `cudaMemcpy` (blocking, pageable memory causing implicit staging copies) with `cudaMemcpyAsync` + pre-allocated pinned buffers + multi-slot pipelining. H2D transfer time: **850 µs → 265 µs** (3.2x faster).

## R – Result

| Metric                   | Before            | After              | Improvement                |
| ------------------------ | ----------------- | ------------------ | -------------------------- |
| Kernel launch overhead   | ~150 µs          | ~5 µs             | **30x reduction**    |
| p99 GPU inference time   | 2.5 ms            | 1.1 ms             | **56% faster**       |
| End-to-end p99           | 35 ms             | 8.7 ms             | **Within 10 ms SLA** |
| Throughput               | 1,000 req/sec/GPU | 10,000 req/sec/GPU | **10x**              |
| GPU SM utilization       | 18%               | 72%                | **4x better**        |
| H2D transfer time (2 MB) | 850 µs           | 265 µs            | **3.2x faster**      |
| TLB miss rate            | 4.2%              | 0.3%               | **14x reduction**    |
| Cross-NUMA accesses      | frequent          | eliminated         | —                         |
| NIC→app latency         | 2.1 ms            | 0.7 ms             | **3x faster**        |

## Lesson Learnt

1. **Profiling first, not guessing.** Without Nsight Systems, we would have assumed the model itself needed optimization. The timeline made it immediately obvious that launch overhead, not kernel compute, was the bottleneck.
2. **A system engineer's practical CUDA work is NOT writing kernels.** It's using Nsight to diagnose latency/throughput/memory issues, understanding GPU execution constraints, and applying fixes at the configuration/scheduling/topology level.
3. **Micro-batching is the single biggest throughput lever for GPU inference.** The GPU is a throughput engine — feeding it one request at a time is like using a highway with one car. Bounded time windows preserve latency discipline.
4. **Host-level foundation matters.** isolcpus + nohz_full + rcu_nocbs + IRQ affinity + HugePages + NUMA pinning are prerequisites, not optimizations. Without them, no amount of kernel tuning will give you consistent tail latency.
5. **Pinned memory is not optional.** The implicit staging copy from pageable memory is a hidden tax that roughly doubles H2D latency. Pre-allocated pinned buffers + async DMA + multi-slot pipelining are table stakes for serious GPU serving.

**Covers keywords:** HPC, CUDA profiling, Nsight Systems, Nsight Compute, CUDA Graphs, Micro-batching, Pinned memory, NUMA pinning, HugePages, isolcpus, nohz_full, AF_XDP, SR-IOV, Topology Manager, Profiling-first methodology

---

# Story 4 – Apple – Leadership, Privacy & Tiered LLM Architecture

**Category:** Story 4 – Leadership, Conflict & Influence
**Theme:** Conflict between ML team and Privacy/Legal over data for LLM/RAG personalization & observability; resolved with tiered data strategy, on-device aggregation, and differential privacy.

## S – Situation

For Siri's LLM-augmented personalization and RAG flows, my team wanted to leverage richer query logs and device telemetry to improve relevance, and more detailed logs for LLM observability. The Privacy and Legal teams pushed back hard on logging and data retention, worried about identifiable patterns and regulatory exposure (especially EU regions and child accounts). Product was stuck between "better personalization" and "risk."

The ML/search teams saw that more detailed query logs, context signals, and click/engagement data would significantly improve both training and online evaluation. But Privacy/Legal were extremely cautious — observability for LLM/RAG flows seemed at odds with Apple's privacy commitments. Discussions stalled: ML felt blocked by "privacy red tape," Privacy felt ML was pushing for "unnecessary" data collection. Without compromise, key LLM features would slip or ship with weak observability.

## T – Task

Influence cross-functional partners to find a balanced design:

- Maintain strong relevance and debug-ability for the LLM/search system
- Respect strict privacy constraints (especially for EU/child accounts)
- Avoid delaying roadmap items due to gridlock
- Create a reusable pattern for future ML/LLM projects

## A – Action

### 1. Facilitated Multi-Team Workshops

Brought in ML, Privacy, Legal, Security, and Product to map user journeys and data flows end-to-end. Separated "must-have signals for quality" from "nice-to-have but risky" logging.

### 2. Proposed a Three-Tier Data Strategy

**Tier 1 — On-Device Aggregation (no central logging):**

- Personalization features computed entirely on-device
- Federated signals for model improvement (aggregated, anonymized)
- No raw query logs leave the device

**Tier 2 — Differentially Private Summaries:**

- Observability metrics computed with differential privacy
- Heavily-aggregated counts (e.g., "N queries of intent X had latency > Y ms")
- No user-level raw data in central systems

**Tier 3 — Strictly Redacted Logs (with controls):**

- Sensitive fields hashed/redacted before reaching central systems
- Short retention: 24h for debug logs, 7 days for aggregated
- Regional data residency (EU data stays in EU)
- Config flags to disable telemetry for specific geos or user segments

### 3. Built Shared Dashboards to Build Trust

Showed that even with constrained data, we could still measure relevance, latency, and error rates. Demonstrated how we detect and correct quality regressions without user-level raw data. This was the turning point — Privacy/Legal became allies when they saw we could deliver quality without collecting sensitive data.

### 4. Coached Junior Engineers

Taught junior engineers how to discuss privacy trade-offs with non-technical stakeholders. Emphasized that privacy constraints are product requirements that shape architecture, not "red tape" to work around.

## R – Result

- Reached agreement allowing LLM-augmented features to ship without delays while satisfying Privacy/Legal
- The pattern (tiered data, on-device aggregation, strict retention) became a template for future ML/LLM projects across Apple
- I was seen as a bridge between engineering and compliance, not just a "model person"
- Key features shipped on time with strong observability and zero privacy incidents

## Lesson Learnt

1. **Privacy constraints are product requirements, not blockers.** The best architectures are designed with privacy as a first-class concern, not bolted on after the fact. Tiered data strategies resolve gridlock by giving options between "collect everything" and "collect nothing."
2. **Shared dashboards build trust faster than any slide deck.** When Privacy/Legal can see that you're measuring quality without collecting sensitive data, they become allies, not adversaries. Show, don't tell.
3. **Coaching engineers on stakeholder communication is as important as technical design.** The best architecture fails if the team can't explain it to non-technical partners. Teaching junior engineers how to frame trade-offs (not just argue) multiplies your influence.

**Covers keywords:** Conflict with stakeholder, Influenced without authority, Aligning misaligned teams, Privacy-preserving ML, Differential privacy, On-device aggregation, Federated learning, Coaching/mentoring, Tiered data strategy

---

# Story 5 – Broadcom/Symantec – Failure, Risk & Learning from False Positives in Production

**Category:** Story 5 – Failure, Risk, Ethics & Learning
**Theme:** New deep-learning phishing model causing harmful false positives in production; owning the mistake, transparent rollback, and building a robust evaluation + deployment framework that became standard practice.

## S – Situation

At Symantec/BlueCoat, I led development of a new deep-learning model (char-CNN/LSTM/transformer) to detect phishing and malicious sites from large-scale web proxy telemetry. The existing rules + reputation-based system had good precision but missed newer, fast-changing phishing patterns, so leadership wanted a modern DL model.

Offline experiments on historical data showed impressive metrics (high recall, strong AUC), creating optimism that this model could significantly boost protection. Under pressure to differentiate the product and close competitive gaps, there was a push to move the model quickly into an **inline blocking path**, not just analysis mode.

Critical gaps in the process: evaluation focused heavily on aggregate metrics; customer-specific validation sets and real-world "good" login patterns were underrepresented in training/validation. The team rolled the model out with partial safeguards but **without a fully mature shadow deployment and rollback framework**.

Shortly after rollout, large enterprise customers reported that **legitimate login portals and internal web apps were being mistakenly blocked**, causing business disruption and escalations to executive level.

## T – Task

Own the failure completely, minimize customer impact, and rebuild trust while fixing the model and process:

- Diagnose why the model behaved differently in production than offline
- Reduce false positives to acceptable levels
- Put processes in place so this kind of incident doesn't repeat across the organization

## A – Action

### 1. Immediate Rollback and Transparent Communication

I immediately recommended rolling back the model from blocking mode to **shadow mode** (monitor-only). Communicated transparently with Product and Support: exactly what happened, what we knew, what we didn't know yet, and what we were doing. No sugar-coating — I owned the mistake publicly.

### 2. Deep Post-Mortem Analysis

Led a detailed post-mortem and discovered:

- **Training data bias:** Over-representation of certain "login-style" templates from previous phishing campaigns caused the model to over-weight particular lexical patterns
- **Label noise:** Some URLs mis-labeled in historical data reinforced the bias
- **Evaluation gap:** Customer-specific validation sets were missing — we tested on aggregate data, not real customer traffic

### 3. Built a Robust Evaluation + Deployment Framework

- **Customer-specific validation sets:** Constructed from each customer's domains with known good/bad traffic
- **Business-critical whitelists:** Tiered decisioning pipeline: model → reputation DB → policy rules
- **Calibrated thresholds:** Per-segment thresholds instead of global cutoff
- **Shadow deployment requirement:** All models must run in monitor-only mode for N weeks, pass drift and false-positive criteria before gating real traffic
- **Automated rollback:** If error rate > 0.1% or FP rate exceeds threshold, auto-rollback within minutes

### 4. Retrained and Re-Integrated the Model

- Cleaned labels, added harder negative examples, introduced calibrated thresholds per segment
- Deployed first in **monitor-only mode** with rich telemetry to compare old vs new decisions
- Only after passing shadow criteria did we gate real traffic

### 5. Organizational Learning

Documented lessons and shared with all ML teams: mandatory customer-representative datasets before inline deployment, shadow-mode requirement, automated rollback frameworks. These became **standard practice across multiple security products**.

## R – Result

- False positives for affected customers dropped to **below previous baselines** after retraining and gating
- Customers appreciated transparent communication and speed of rollback + fix; **no major churn**
- The new evaluation + deployment policies became standard practice across multiple security products
- I had a strong, honest "failure story" showing ownership, ethics, and systematic learning — not just a fix, but **process change that prevented recurrence across the organization**

## Lesson Learnt

1. **Offline metrics lie when your test set doesn't match production.** Aggregate AUC and recall mean nothing if customer-specific validation sets are missing. The model statistically "worked" but failed in the real world because our eval didn't represent real traffic.
2. **Shadow deployment is not optional for security products.** Running in monitor-only mode for weeks with telemetry comparison would have caught the FP spike before it impacted customers. This is now a hard requirement.
3. **Owning a failure transparently builds more trust than hiding it.** Customers didn't churn because we communicated honestly and fixed fast. The internal learning was even more valuable: the new evaluation framework prevented similar incidents across other ML teams.
4. **The fix must be a process change, not just a model retrain.** Retraining fixes this incident; shadow deployment + customer-specific validation + automated rollback prevents the next one.

**Covers keywords:** Major failure, Biggest mistake, Risk that went wrong, Missed quality goal, Critical feedback received, Tough ethical decision (rollback, owning impact), Admitting you were wrong, Process improvement

---

# Story 6 – Capital One – Building GPU-Accelerated LLM Fraud Analysis with Guardrails & KV-Cache Routing

**Category:** Story 6 – LLM Serving, GPU Memory Management & System Engineering
**Theme:** Building the entire GPU infrastructure stack for LLM-powered fraud analysis — from host-level GPU configuration through Kubernetes GPU scheduling through LLM serving with guardrails; Rust gateway + Triton ensemble pipeline eliminating Python tax; KV-cache aware routing achieving 70% cache hit rate.

## S – Situation

After stabilizing the hot-path fraud scoring at sub-10 ms (Story 3), the business wanted to add an **LLM-powered fraud analysis layer** for complex cases. When the XGBoost model flagged a transaction as "medium risk" (score 0.4–0.8), instead of auto-declining, we'd route it to a Llama-3.1-8B model for natural-language fraud assessment.

Requirements: < 200 ms end-to-end for the LLM path (including input/output guardrails), 500+ concurrent LLM requests across 3 regions, input guardrail to block jailbreak/injection, output guardrail ensuring approved decision categories. Run on NVIDIA H100 80GB GPUs (8 per node, NVLink mesh). Use TensorRT-LLM with tensor parallelism for 70B model variant.

The initial Python-based guardrail + LLM pipeline consumed **15–40 ms of overhead** from serialization, GIL contention, and CPU↔GPU copies — before any LLM compute even started. At high QPS, GPUs worth hundreds of thousands of dollars sat idle waiting for Python tokenization and guardrail checks.

## T – Task

Design the entire GPU infrastructure stack from host level through Kubernetes through the LLM serving pipeline with guardrails:

- Eliminate Python tax from the guardrail + LLM hot path
- Achieve < 200 ms end-to-end including guardrails
- Build KV-cache aware routing for multi-turn fraud investigations
- Support multi-region deployment with latency-aware routing

## A – Action

### Phase 1: Host-Level GPU Configuration (HGX H100 nodes)

- **CPU isolation:** `isolcpus=8-31,48-71 nohz_full=8-31,48-71 rcu_nocbs=8-31,48-71 iommu=pt intel_iommu=on`
- **HugePages:** 32 GB of 2 MB HugePages for KV cache pool, model weights, and CUDA context
- **NUMA alignment:** GPUs 0–3 on NUMA 0, GPUs 4–7 on NUMA 1; vLLM workers pinned to matching NUMA nodes
- **Kernel tuning:** `vm.max_map_count=1048576`, `kernel.numa_balancing=0`, increased AIO limits for NVMe
- **GPUDirect Storage:** NVMe → GPU direct model loading (15 sec → 6 sec for 70B model)

### Phase 2: Kubernetes GPU Scheduling

- **NVIDIA GPU Operator:** GPU device plugin, GPU Feature Discovery (labels: `nvidia.com/gpu.product: NVIDIA-H100-SXM5-80GB`), DCGM exporter
- **MIG strategy:** H100 split into 3g.40gb MIG instances for Llama-8B; full GPUs for Llama-70B TP=4
- **Topology Manager:** `single-numa-node` ensuring CPU, memory, GPU, and NIC all on same NUMA node
- **SR-IOV for NCCL:** Dedicated VF per pod for inter-node NCCL traffic; `NCCL_NET_GDR_LEVEL=5` for GPUDirect RDMA
- **Cilium (eBPF):** Network policies, L7 visibility, kube-proxy replacement for LLM serving traffic

### Phase 3: Eliminating the Python Tax — Rust Gateway + Triton Ensemble

Designed a guardrail + LLM pipeline that eliminated Python from the hot path:

```
Client request
    ↓
Rust Gateway (tokenization + rule-based input guardrail + prompt construction)
    ↓ gRPC (preprocessed token IDs)
Triton Inference Server
    ├── ML Input Guardrail (TensorRT, GPU) — jailbreak/injection classifier
    ├── Llama-3.1-8B (TensorRT-LLM, GPU) — fraud analysis
    └── ML Output Guardrail (TensorRT, GPU) — response category validator
    ↓ gRPC (generated token IDs)
Rust Gateway (detokenization + rule-based output guardrail + response formatting)
    ↓
Client
```

**What was eliminated:**

- Python GIL: Rust gateway handles ALL CPU work (tokenization, rule-based guardrails, formatting)
- Serialization between guardrail ↔ LLM: Triton ensemble keeps tensors on GPU between stages
- CPU↔GPU copies between stages: GPU-resident pipeline inside Triton
- Multiple network hops: 2 hops total (gateway ↔ Triton)
- Overhead: **15–40 ms → < 2 ms**

**Profiling & optimization with Nsight:**

- Guardrail model: exported as single TensorRT engine (47 kernels → 3)
- LLM decode: CUDA Graphs for steady-state decode (12 µs CPU gaps eliminated)
- Attention kernel: memory-bound decode — switched KV cache to FP8, improved decode TPS by **~45%**
- FP8 with long prompts (> 2048 tokens): mixed precision — FP8 weights + KV cache, BF16 attention accumulation

### Phase 4: KV-Cache Aware Routing for Multi-Turn Investigations

Multi-turn fraud investigations suffered from **12% KV-cache hit rate** with round-robin load balancing. Every follow-up request recomputed the entire conversation prefix. Built a KV-aware routing layer:

- **Consistent hash ring** with 150 virtual nodes per backend, routing key = `{tenant}:{model}:{prefix_hash}`
- **Scoring function:** KV cache overlap (weight 10×) + GPU memory headroom (weight 3×) + active sequence count (weight 2×)
- **Workers report KV state** periodically — router skips backends with < 15% free GPU memory
- **Circuit breaker** per backend: closed → open after 5 consecutive failures, half-open probe after 30s

**Results:**

- KV cache hit rate: **12% → 70%** (5.8x better)
- Follow-up TTFT p50: **320 ms → 85 ms** (73% faster)
- Redundant prefill FLOPS: **75% → 20%** (73% reduction)
- GPU memory evictions: **45/sec → 12/sec** (73% reduction)

### Phase 5: Multi-Region Deployment

- US-East: 4× H100 nodes, Llama-70B TP=4 (highest traffic)
- US-West: 2× H100 nodes, Llama-8B MIG (cost-optimized)
- EU-West: 2× L40S nodes, Llama-8B FP8 (GDPR, moderate traffic)
- Istio VirtualService for geo-routing with latency-aware failover

## R – Result

| Metric                        | Before (Python pipeline) | After (Rust + Triton + tuned GPU) | Improvement             |
| ----------------------------- | ------------------------ | --------------------------------- | ----------------------- |
| End-to-end LLM path P50       | 280 ms                   | 95 ms                             | **66% faster**    |
| End-to-end LLM path P99       | 850 ms                   | 185 ms                            | **78% faster**    |
| Guardrail overhead            | 40 ms                    | 3 ms                              | **13x reduction** |
| Python serialization tax      | 15 ms                    | 0 ms                              | **Eliminated**    |
| GPU utilization during decode | 45%                      | 78%                               | **73% better**    |
| KV cache hit rate             | 12%                      | 70%                               | **5.8x better**   |
| Concurrent requests per H100  | 32                       | 96                                | **3x**            |
| Cost per 1M fraud analyses    | $847 | $312              | **63% lower**               |                         |
| Model loading time            | 5 min                    | 6 sec (GDS)                       | **50x faster**    |

## Lesson Learnt

1. **The Python tax is real and measurable.** At high QPS, the GIL-bound tokenization/guardrail path becomes the bottleneck — GPUs sit idle waiting for CPU work. Moving tokenization + rule-based guardrails to Rust and ML guardrails into a Triton GPU-resident ensemble eliminated 15–40 ms of overhead.
2. **KV-cache routing is a system-design optimization, not a kernel optimization.** The GPU kernels were already fast — the problem was running them on data already computed elsewhere. Session affinity alone captured most of the benefit (12% → 70% hit rate).
3. **Nsight profiling reveals different bottlenecks at different layers.** The guardrail pipeline had 47 tiny kernel launches (fusion fix). The decode phase had CPU gaps (CUDA Graph fix). The attention kernel was memory-bound (FP8 KV cache fix). Each needed a different optimization strategy.
4. **GPU memory headroom tracking prevents cascading failures.** Without it, a worker can enter a "thrashing" state where every request triggers a full re-prefill because KV cache is over-evicted. The headroom factor in routing prevents this.
5. **A system engineer's practical CUDA work in LLM serving is NOT writing kernels.** It's using Nsight to diagnose issues, understanding GPU execution constraints, and applying fixes at the configuration/scheduling/topology level.

**Covers keywords:** LLM serving, KV-cache routing, Session affinity, GPU memory management, Triton ensemble, Rust gateway, Python tax elimination, TensorRT-LLM, FP8 quantization, MIG, GPUDirect Storage, Nsight profiling, Prefix caching, Multi-region deployment, Circuit breaker, Consistent hashing

---

# Summary: Coverage Matrix

| Story | Company           | Category               | HPC                         | GPU/CUDA                           | Zero-Copy                       | Search                             | LLM                            | System Engineering                    |
| ----- | ----------------- | ---------------------- | --------------------------- | ---------------------------------- | ------------------------------- | ---------------------------------- | ------------------------------ | ------------------------------------- |
| 1     | Capital One       | Customer Obsession     | NUMA, HugePages             | CUDA Graphs                        | Arrow IPC, shm_open, mmap       | Hybrid RAG                         | LangGraph, RAG                 | Three-tier, SPSC rings                |
| 2     | Apple             | Ownership & Delivery   | —                          | Metal batching, FAISS GPU          | Shared memory arena, SPSC rings | Hybrid retrieval (BM25 + semantic) | LLM re-ranker                  | Micro-batching                        |
| 3     | Capital One       | HPC & CUDA Profiling   | isolcpus, HugePages, AF_XDP | Nsight, CUDA Graphs, Pinned memory | —                              | —                                 | —                             | NUMA pinning, SR-IOV, K8s CPU Manager |
| 4     | Apple             | Leadership & Privacy   | —                          | —                                 | —                              | —                                 | LLM/RAG personalization        | Tiered data, Differential privacy     |
| 5     | Broadcom/Symantec | Failure & Learning     | —                          | GPU acceleration                   | —                              | —                                 | Phishing detection             | Shadow deployment, Eval framework     |
| 6     | Capital One       | LLM Serving & KV-Cache | HugePages, GDS              | MIG, Nsight, FP8, CUDA Graphs      | Rust gateway + Triton           | —                                 | TensorRT-LLM, KV-cache routing | Multi-region, Circuit breaker         |

---

# Interview Delivery Guide

## The 60-second version (for phone screens):

> "I'm a systems engineer specializing in AI infrastructure — from GPU profiling and CUDA optimization to zero-copy data flow and LLM serving. At Capital One, I built a three-tier platform with CUDA Graphs and Arrow IPC that hit sub-5 ms p99 at 20,000 TPS for fraud detection, plus an LLM-powered agent assistant that reduced handle time from 1–3 min to 10–30 sec. At Apple, I stepped up as technical owner for Siri's LLM-augmented search pipeline with FAISS GPU and Metal command buffer batching, delivering on time for a major OS launch. I've designed KV-cache aware routing that improved hit rates from 12% to 70%, and led a deep-learning security model failure where I owned the rollback and built the evaluation framework that became standard practice. My approach is profiling-first: use Nsight to find the real bottleneck, then apply fixes at the configuration/scheduling/topology level."

## If asked specific questions:

| Question                                                      | Use Story                                                                                      |
| ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| Tell me about a time you dealt with a difficult customer      | **Story 1** — the skeptical Ops VP, data-driven pushback on rollout                     |
| Tell me about a time you took ownership of a failing project  | **Story 2** — Siri search pipeline, ~12 weeks to OS launch                              |
| Tell me about your experience with GPU/CUDA optimization      | **Story 3** — Nsight profiling, CUDA Graphs, micro-batching, host-level foundation      |
| Tell me about a time you resolved conflict between teams      | **Story 4** — ML vs Privacy/Legal, tiered data strategy                                 |
| Tell me about a major failure and how you handled it          | **Story 5** — phishing model false positives, transparent rollback, process change      |
| Tell me about your experience with LLM serving infrastructure | **Story 6** — Rust + Triton ensemble, KV-cache routing, multi-region GPU deployment     |
| What's your most impactful performance optimization?          | **Stories 1 + 3** — CUDA Graphs (150 µs → 5 µs) + Arrow zero-copy (2 KB → 47 bytes) |
| How do you approach system design under ambiguity?            | **Stories 1 + 3** — profiled first, designed minimal viable paths, measured everything  |
