# Architecture Diagrams - Fiserv Loan Director

Use these diagrams for interviews, presentations, and portfolio documentation.

---

## 1. High-Level System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Loan Application Gateway                         │
│                    (REST API / Message Queue / UI)                       │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Loan Application
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                   SENTINEL CONTROL PLANE (Rust)                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              Multi-Stage Routing Pipeline                          │  │
│  │                                                                    │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │  │
│  │  │   Stage 1    │  │   Stage 2    │  │   Stage 3    │            │  │
│  │  │   Intake     │→ │   Missing    │→ │   Workflow   │            │  │
│  │  │Classification│  │   Document   │  │   Routing    │            │  │
│  │  │   (8B LLM)   │  │   Detection  │  │              │            │  │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │  │
│  │                                                                    │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │  │
│  │  │   Stage 4    │  │   Stage 5    │  │   Stage 6    │            │  │
│  │  │   Safety &   │→ │   Context    │→ │   LLM        │            │  │
│  │  │   Scope      │  │   Assembly   │  │   Inference  │            │  │
│  │  │   Validation │  │              │  │   (70B LLM)  │            │  │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              PodRegistry + Health Monitor                          │  │
│  │  • Pod state tracking (healthy/unhealthy)                         │  │
│  │  • GPU memory utilization                                         │  │
│  │  • Inflight request count                                         │  │
│  │  • Latency EWMA, Error rates                                      │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              PodScorer (Multi-Factor Scoring)                      │  │
│  │  Score = (Inflight × 0.3) + (GPU Headroom × 0.4) +                │  │
│  │          (Latency × 0.2) + (Error Rate × 0.1) +                   │  │
│  │          (KV Pressure × dynamic_weight)                           │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Routing Decision
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                   SENTINEL DATA PLANE (C++/CUDA)                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              Zero-Copy Transfer Engine                             │  │
│  │  • CUDA Pinned Memory (Page-Locked)                               │  │
│  │  • Async DMA Transfers (cudaMemcpyAsync)                         │  │
│  │  • CUDA Stream Management                                         │  │
│  │  • Event-Based Completion Tracking                                │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              KV-Cache Manager                                      │  │
│  │  • Session Registry (SessionID → PodID mapping)                  │  │
│  │  • GPU Memory Region Allocation                                   │  │
│  │  • Cache Preloading & Reuse                                       │  │
│  │  • Memory Pressure Estimation                                     │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Context + Request
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GPU Inference Pods (20+ Pods)                         │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐       │
│  │  Pod 1     │  │  Pod 2     │  │  Pod 3     │  │  Pod N     │       │
│  │  llama-8b  │  │ llama-70b  │  │  Custom    │  │  llama-70b │       │
│  │  80GB H100 │  │ 80GB H100  │  │  Decision  │  │ 80GB H100  │       │
│  │  KV-Cache  │  │ KV-Cache   │  │  Model     │  │ KV-Cache   │       │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘       │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Loan Decision
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         Response Gateway                                 │
│              (Approval/Denial, Conditions, Next Steps)                   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Context Assembly Pipeline

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Loan Application (Raw Data)                           │
│  • Application Form (JSON)                                               │
│  • Credit Bureau Data (XML/JSON)                                        │
│  • Bank Statements (PDF)                                                │
│  • Pay Stubs (PDF/Images)                                               │
│  • Tax Returns (PDF - 1040, W2, 1099)                                  │
│  • Employment Verification (Email/API)                                  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              TIER 1: Structured Data Extraction                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐     │
│  │  Bank Statement  │  │   Pay Stub       │  │  Tax Return      │     │
│  │  Parser          │  │   Parser         │  │  Parser          │     │
│  │                  │  │                  │  │                  │     │
│  │  • Transactions  │  │  • Gross Income  │  │  • AGI           │     │
│  │  • Balances      │  │  • Net Income    │  │  • Wages         │     │
│  │  • Cash Flow     │  │  • YTD           │  │  • Deductions    │     │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘     │
│                                                                          │
│  Output: Structured JSON (~500 tokens)                                  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              TIER 2: Intermediate Summaries                              │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Credit Profile Summary (8B LLM)                                 │  │
│  │  • Credit Score, Utilization, Payment History                    │  │
│  │  • Derogatory Marks, Credit Age, Inquiries                       │  │
│  │  Output: ~300 tokens                                             │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Income Analysis (8B LLM)                                        │  │
│  │  • Base Income, Overtime, Bonuses                                │  │
│  │  • Debt-to-Income Ratio (DTI)                                    │  │
│  │  • Income Stability Assessment                                   │  │
│  │  Output: ~400 tokens                                             │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Asset & Employment Summary (8B LLM)                             │  │
│  │  • Liquid Assets, Reserves                                       │  │
│  │  • Employment Verification Status                                │  │
│  │  • Job Stability                                                 │  │
│  │  Output: ~300 tokens                                             │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Total Intermediate Summary: ~1,000 tokens                              │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              TIER 3: Targeted Context Assembly                           │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Context Router (Based on Loan Type)                             │  │
│  │                                                                  │  │
│  │  IF Personal Loan:                                               │  │
│  │    • Credit Profile (full)                                       │  │
│  │    • Income Summary (condensed)                                  │  │
│  │    • Employment Status                                           │  │
│  │    → ~8,000 tokens                                               │  │
│  │                                                                  │  │
│  │  IF Mortgage:                                                    │  │
│  │    • Credit Profile (full)                                       │  │
│  │    • Income Analysis (detailed with tax returns)                 │  │
│  │    • Asset Summary (reserves calculation)                        │  │
│  │    • Property Information                                        │  │
│  │    → ~32,000 tokens                                              │  │
│  │                                                                  │  │
│  │  IF Business Loan:                                               │  │
│  │    • Business Credit Profile                                     │  │
│  │    • Business Financials (P&L, Balance Sheet)                    │  │
│  │    • Personal Guarantee Analysis                                 │  │
│  │    • Industry Risk Assessment                                    │  │
│  │    → ~48,000 tokens                                              │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              TIER 4: Dynamic Context Pruning                             │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Attention-Based Pruning                                         │  │
│  │  • Monitor attention weights during inference                    │  │
│  │  • Identify low-attention context segments                       │  │
│  │  • Dynamically prune for subsequent turns                        │  │
│  │                                                                  │  │
│  │  Result: 20-30% additional token reduction                       │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    Final Context to LLM                                  │
│  • Structured Data: ~500 tokens                                        │
│  • Intermediate Summaries: ~1,000 tokens                               │
│  • Targeted Context: 8K-48K tokens (loan type dependent)               │
│  • System Prompt: ~500 tokens                                          │
│  ───────────────────────────────────────────────────────────────────   │
│  Total: 10K-50K tokens (vs. 100K+ with naive approach)                 │
│  Token Reduction: 60%                                                  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Session Stickiness & KV-Cache Reuse

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Loan Application Workflow                             │
│                    Session ID: ABC-123                                   │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Request 1: Document Classification
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Sentinel Router (First Request)                             │
│                                                                          │
│  Session Registry: [EMPTY]                                               │
│  → No existing session found                                            │
│                                                                          │
│  Pod Scoring:                                                           │
│  • Pod-1: Score=0.85 (GPU=90%, Inflight=5, KV=80%)                     │
│  • Pod-2: Score=0.72 (GPU=75%, Inflight=12, KV=65%)                    │
│  • Pod-3: Score=0.68 (GPU=70%, Inflight=15, KV=60%)                    │
│                                                                          │
│  Decision: Route to Pod-1 (highest score)                               │
│  Action: Register Session ABC-123 → Pod-1                               │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GPU Pod-1 (llama-70b)                                 │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  KV-Cache Allocation                                             │  │
│  │  Session: ABC-123                                                │  │
│  │  Context: Document Classification                                │  │
│  │  KV-Cache Size: 2GB                                              │  │
│  │  GPU Memory: 80GB H100                                           │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Inference Result: Mortgage Loan Detected                               │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Request 2: Missing Document Check
                                    │ Session ID: ABC-123
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Sentinel Router (Sticky Request)                            │
│                                                                          │
│  Session Registry: [ABC-123 → Pod-1]                                    │
│  → Session found! Route to Pod-1                                        │
│                                                                          │
│  Benefit: KV-Cache Reuse (2GB already in GPU memory)                    │
│  Savings: ~150ms (no context re-computation)                            │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GPU Pod-1 (llama-70b)                                 │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  KV-Cache Reuse                                                  │  │
│  │  Session: ABC-123                                                │  │
│  │  Previous KV-Cache: Document Classification                      │  │
│  │  New KV-Cache: + Missing Document Check                          │  │
│  │  Total KV-Cache: 3.5GB                                           │  │
│  │                                                                  │  │
│  │  Attention: Reuses previous context KV without recomputation     │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Inference Result: Missing Tax Returns (2024)                           │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Request 3: Final Decision
                                    │ Session ID: ABC-123
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Sentinel Router (Sticky Request)                            │
│                                                                          │
│  Session Registry: [ABC-123 → Pod-1]                                    │
│  → Session found! Route to Pod-1                                        │
│                                                                          │
│  Benefit: KV-Cache Reuse (3.5GB already in GPU memory)                  │  │
│  Savings: ~250ms (no context re-computation)                            │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GPU Pod-1 (llama-70b)                                 │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  KV-Cache Reuse                                                  │  │
│  │  Session: ABC-123                                                │  │
│  │  Previous KV-Cache: Classification + Missing Docs                │  │
│  │  New KV-Cache: + Final Decision                                  │  │
│  │  Total KV-Cache: 5GB                                             │  │
│  │                                                                  │  │
│  │  Attention: Reuses all previous context without recomputation    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Inference Result: APPROVED with Conditions                             │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                    KV-Cache Reuse Metrics                                │
│  • Total Requests in Session: 3                                        │
│  • KV-Cache Hits: 2 (Request 2 & 3)                                    │
│  • KV-Cache Hit Rate: 67%                                              │
│  • Total Latency Saved: ~400ms                                         │
│  • GPU Memory Efficient: Single copy of context                        │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Zero-Copy Data Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    CPU Memory (Host)                                     │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │              Traditional Approach (Synchronous)                    │  │
│  │                                                                  │  │
│  │  1. Allocate Pageable Memory                                     │  │
│  │  2. Copy Context Data                                            │  │
│  │  3. cudaMemcpy (H2D) - BLOCKING                                  │  │
│  │  4. GPU Processes                                                │  │
│  │  5. cudaMemcpy (D2H) - BLOCKING                                  │  │
│  │                                                                  │  │
│  │  Total Time: ~3.2ms                                              │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │              Zero-Copy Approach (Async DMA)                      │  │
│  │                                                                  │  │
│  │  1. Allocate Pinned Memory (Page-Locked)                         │  │
│  │     → Direct DMA path to GPU                                     │  │
│  │                                                                  │  │
│  │  2. cudaMemcpyAsync (H2D) - NON-BLOCKING                         │  │
│  │     → Returns immediately, DMA in progress                       │  │
│  │     → CPU can prepare next request                               │  │
│  │                                                                  │  │
│  │  3. GPU Processes (in CUDA Stream)                               │  │
│  │     → Overlaps with next H2D transfer                            │  │
│  │                                                                  │  │
│  │  4. cudaEventRecord + cudaStreamQuery                            │  │
│  │     → Non-blocking completion check                              │  │
│  │                                                                  │  │
│  │  5. cudaMemcpyAsync (D2H) - NON-BLOCKING                         │  │
│  │     → Result transfer while GPU starts next task                 │  │
│  │                                                                  │  │
│  │  Total Time: ~1.0ms (3.2x faster)                                │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GPU Memory (Device)                                   │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │              CUDA Stream Pipeline                                │  │
│  │                                                                  │  │
│  │  Stream 0: [H2D] → [Compute] → [D2H]                            │  │
│  │  Stream 1: [H2D] → [Compute] → [D2H]                            │  │
│  │  Stream 2: [H2D] → [Compute] → [D2H]                            │  │
│  │                                                                  │  │
│  │  Timeline:                                                       │  │
│  │  T0: Stream0[H2D]                                                │  │
│  │  T1: Stream0[Compute]  Stream1[H2D]                              │  │
│  │  T2: Stream0[D2H]    Stream1[Compute]  Stream2[H2D]              │  │
│  │  T3:                 Stream1[D2H]    Stream2[Compute]            │  │
│  │  T4:                               Stream2[D2H]                  │  │
│  │                                                                  │  │
│  │  Result: 3x throughput improvement                               │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                    Memory Types Comparison                               │
│  ┌──────────────────┬──────────────┬──────────────┬─────────────────┐  │
│  │ Memory Type      │ Alloc Speed │ Transfer Speed │ Use Case        │  │
│  ├──────────────────┼──────────────┼──────────────┼─────────────────┤  │
│  │ Host Pageable    │ Fast         │ Slow          │ General CPU     │  │
│  │ Host Pinned      │ Slow         │ Fast (DMA)    │ Zero-Copy H2D   │  │
│  │ CudaDevice       │ Medium       │ N/A           │ GPU Compute     │  │
│  └──────────────────┴──────────────┴──────────────┴─────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Multi-Factor Pod Scoring

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Incoming Loan Application                             │
│  Request Shape:                                                         │
│  • Loan Type: Mortgage                                                 │
│  • Context Size: 32,000 tokens                                         │
│  • Expected KV-Cache: 4GB                                              │
│  • Batch Size: 1                                                       │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Pod State Snapshot (Real-Time)                              │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Pod-1                                                           │  │
│  │  • Health: ✓ Healthy                                             │  │
│  │  • GPU Memory: 80GB Total, 50GB Used (62.5%)                    │  │
│  │  • GPU Headroom: 30GB (37.5%)                                    │  │
│  │  • Inflight Requests: 5                                          │  │
│  │  • Latency EWMA: 1.8ms                                           │  │
│  │  • Error Rate: 0.02 (2%)                                         │  │
│  │  • KV-Cache Pressure: 0.75 (Good)                                │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Pod-2                                                           │  │
│  │  • Health: ✓ Healthy                                             │  │
│  │  • GPU Memory: 80GB Total, 70GB Used (87.5%)                    │  │
│  │  • GPU Headroom: 10GB (12.5%)                                    │  │
│  │  • Inflight Requests: 12                                         │  │
│  │  • Latency EWMA: 2.4ms                                           │  │
│  │  • Error Rate: 0.05 (5%)                                         │  │
│  │  • KV-Cache Pressure: 0.45 (High)                                │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Pod-3                                                           │  │
│  │  • Health: ✗ Unhealthy (last check failed)                       │  │
│  │  • GPU Memory: 80GB Total, 40GB Used (50%)                      │  │
│  │  • GPU Headroom: 40GB (50%)                                      │  │
│  │  • Inflight Requests: 8                                          │  │
│  │  • Latency EWMA: 1.5ms                                           │  │
│  │  • Error Rate: 0.15 (15%)                                        │  │
│  │  • KV-Cache Pressure: 0.80 (Good)                                │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Scoring Calculation (Weights: Default)                      │
│  Weights:                                                               │
│  • Inflight: 0.3                                                        │
│  • GPU Headroom: 0.4                                                    │
│  • Latency: 0.2                                                         │
│  • Error Rate: 0.1                                                      │
│  • KV Pressure: dynamic (based on request size)                         │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Pod Score Breakdown                                         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Pod-1 Score Calculation                                         │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  Inflight Score:    1.0 - (5/100) = 0.95                        │  │
│  │  GPU Headroom Score: 30GB / 80GB = 0.375                         │  │
│  │  Latency Score:     1.0 - (1.8/10) = 0.82                        │  │
│  │  Error Rate Score:  1.0 - 0.02 = 0.98                            │  │
│  │  KV Pressure Score: 0.75                                         │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  Weighted Sum: (0.95×0.3) + (0.375×0.4) + (0.82×0.2) +          │  │
│  │                (0.98×0.1) + (0.75×0.2) = 0.85                    │  │
│  │  Weight Multiplier: 1.0                                          │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  FINAL SCORE: 0.85 ✓                                             │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Pod-2 Score Calculation                                         │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  Inflight Score:    1.0 - (12/100) = 0.88                       │  │
│  │  GPU Headroom Score: 10GB / 80GB = 0.125                         │  │
│  │  Latency Score:     1.0 - (2.4/10) = 0.76                        │  │
│  │  Error Rate Score:  1.0 - 0.05 = 0.95                            │  │
│  │  KV Pressure Score: 0.45                                         │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  Weighted Sum: (0.88×0.3) + (0.125×0.4) + (0.76×0.2) +          │  │
│  │                (0.95×0.1) + (0.45×0.2) = 0.72                    │  │
│  │  Weight Multiplier: 1.0                                          │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  FINAL SCORE: 0.72                                               │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Pod-3 Score Calculation                                         │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  Health Check: FAILED ✗                                          │  │
│  │  ──────────────────────────────────────────────────────────────  │  │
│  │  FINAL SCORE: 0.0 (Excluded from routing)                        │  │
│  │  Exclusion Reason: Pod is unhealthy                              │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    Routing Decision                                      │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Winner: Pod-1                                                   │  │
│  │  Score: 0.85                                                     │  │
│  │                                                                  │  │
│  │  Score Breakdown:                                                │  │
│  │  • Inflight:    0.285 (33.5%)                                    │  │
│  │  • GPU Headroom: 0.150 (17.6%)                                   │  │
│  │  • Latency:     0.164 (19.3%)                                    │  │
│  │  • Error Rate:  0.098 (11.5%)                                    │  │
│  │  • KV Pressure: 0.150 (17.6%)                                    │  │
│  │  • Weight:      1.0x                                             │  │
│  │                                                                  │  │
│  │  Why Pod-1 Won:                                                  │  │
│  │  ✓ Best GPU headroom for KV-cache allocation                     │  │
│  │  ✓ Low inflight count                                            │  │
│  │  ✓ Excellent error rate                                          │  │
│  │  ✓ Good KV-cache availability                                    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                    Explainability Metrics                                │
│  • Routing decision logged with full score breakdown                    │
│  • Prometheus metrics:                                                  │
│    - sentinel_routing_decisions_total{outcome="success",pod="pod-1"}   │
│    - sentinel_pod_score{pod="pod-1",component="inflight"} 0.95         │
│    - sentinel_pod_score{pod="pod-1",component="gpu_headroom"} 0.375    │
│    - sentinel_pod_score{pod="pod-1",component="latency"} 0.82          │
│    - sentinel_pod_score{pod="pod-1",component="error_rate"} 0.98       │
│    - sentinel_pod_score{pod="pod-1",component="kv_pressure"} 0.75      │
│  • Trace spans for debugging latency issues                            │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Usage Instructions

### For Interviews:
1. **Print these diagrams** on large paper for whiteboard-style discussions
2. **Use as reference** when answering "Tell me about the architecture"
3. **Draw simplified versions** to demonstrate system understanding

### For Portfolio:
1. **Include in GitHub README** for Sentinel-Fabric project
2. **Create visual versions** using tools like draw.io or Lucidchart
3. **Add to personal website** with interactive elements

### For Presentations:
1. **Convert to slides** with one diagram per slide
2. **Animate the flow** to show data movement
3. **Highlight key metrics** in callout boxes

---

## Key Metrics to Emphasize

- **Latency:** 8.5s → 2.3s (73% improvement)
- **Token Efficiency:** 60% reduction
- **GPU Utilization:** 45% → 85%
- **KV-Cache Hit Rate:** 70%
- **Scale:** 10x concurrent applications
- **Availability:** 99.9%

---

## Diagram Legend

- **Rectangles:** Components/Services
- **Arrows:** Data flow direction
- **Cylinders:** Data storage (KV-Cache)
- **Diamonds:** Decision points
- **Tables:** Metrics/Comparisons
