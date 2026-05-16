# HPC/AI Systems Engineering — Layered Mental Model

## From Silicon to Serving: The Complete Stack

> **Principle:** Every layer depends on the one below it.
> Problems at lower layers manifest as symptoms at higher layers.
> A strong systems engineer can reason UP and DOWN the stack.

---

# THE SEVEN LAYERS

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 7: APPLICATION & BUSINESS LOGIC                           │
│  What: fraud scoring, search ranking, recommendations, Siri     │
│  Your role: architect the pipeline, choose the right tools      │
├─────────────────────────────────────────────────────────────────┤
│  Layer 6: AI MODEL SERVING & INFERENCE                           │
│  What: model runtimes, batching, KV cache, GPU scheduling       │
│  Your role: productionize models, optimize inference             │
├─────────────────────────────────────────────────────────────────┤
│  Layer 5: DATA MOVEMENT & ZERO-COPY                              │
│  What: serialization, shared memory, Arrow, ring buffers        │
│  Your role: eliminate copies, design data flow                   │
├─────────────────────────────────────────────────────────────────┤
│  Layer 4: GPU & ACCELERATOR                                      │
│  What: CUDA/Metal, kernels, Tensor Cores, memory hierarchy      │
│  Your role: profile, optimize, write/fuse kernels                │
├─────────────────────────────────────────────────────────────────┤
│  Layer 3: PLATFORM & ORCHESTRATION                               │
│  What: Kubernetes, cgroups, device plugins, deployment, eBPF     │
│  Your role: deploy, isolate, observe, scale, protect   	   │
├─────────────────────────────────────────────────────────────────-┤
│  Layer 2: NETWORKING & COMMUNICATION                             │
│  What: TCP tuning, RDMA, DPDK, AF_XDP, NCCL, CNI plugins       │
│  Your role: choose the right transport, tune for latency         │
├─────────────────────────────────────────────────────────────────┤
│  Layer 1: LINUX SYSTEMS & PERFORMANCE                            │
│  What: CPU scheduling, NUMA, TLB, hugepages, CPU isolation      │
│  Your role: tune the host, prevent cliffs, verify with counters         │
└─────────────────────────────────────────────────────────────────┘
```

---

# LAYER 1: LINUX SYSTEMS & PERFORMANCE

## "How does the host OS behave under our workload?"

### Core concepts

```
CPU scheduling
├── CFS (Completely Fair Scheduler)
├── CFS cliff (runnable threads > cores → context switches explode)
├── Context switches → cache/TLB invalidation → p99 spikes
├── CPU isolation (isolcpus, nohz_full, rcu_nocbs, irqaffinity)
├── Thread pinning (sched_setaffinity, pthread_setaffinity_np)
└── Per-core ownership (one hot thread per isolated CPU)

Memory system
├── NUMA (first-touch policy, local vs remote access penalty)
├── NUMA cliff (remote memory access → 2-3x slower)
├── NUMA tools (numactl, numastat, hwloc)
├── TLB (Translation Lookaside Buffer — scarce address translation cache)
├── TLB cliff (poor page locality → TLB misses → expensive page walks)
├── Hugepages (2 MB/1 GB pages → 512x/262Kx more TLB coverage)
│   ├── HugeTLB (explicit, pre-allocated, deterministic)
│   └── THP (transparent, automatic, can cause latency spikes)
└── Memory-mapped files (mmap, shared memory, zero-copy IPC)

Profiling tools
├── perf stat (hardware counters: cs, migrations, TLB, cache, IPC)
├── perf record + flamegraph (CPU time attribution)
├── Intel VTune (GUI: hotspots, memory access, threading, NUMA)
├── AMD uProf (GUI: Zen-specific profiling)
├── numastat (NUMA allocation statistics)
├── BCC/bpftrace (scheduler, block I/O, TCP, custom probes)
└── Hotspot (GUI for perf data)
```

### Key skills

- Reproduce and fix CFS / TLB / NUMA cliffs with counter evidence
- Configure CPU isolation (boot params + cgroups + application pinning)
- Choose between hugepage strategies (HugeTLB vs THP)
- Profile with perf stat and interpret hardware counters
- Use VTune/uProf for guided memory/threading analysis

### Interview signals

- "I profiled with perf stat and found context switches exploded
  from 200/sec to 45,000/sec when threads exceeded cores — a classic
  CFS cliff. Reducing worker threads and pinning to isolated CPUs
  dropped p99 from 12 ms to 3 ms"
- "The 512 MB shared memory arena caused TLB pressure with 4 KB
  pages. Switching to 2 MB hugepages reduced TLB entries from
  262K to 256 and eliminated TLB miss storms"

---

# LAYER 2: NETWORKING & COMMUNICATION

## "How does data move between hosts and services?"

### Core concepts

```
Transport choices (from slowest to fastest)
├── Standard TCP/IP (default, 50-200 µs in K8s pod networking)
├── Cilium eBPF (no iptables, 20-50 µs)
├── TCP tuning (buffer sizes, BBR, busy polling)
├── Unix Domain Socket (1-3 µs, same-host)
├── io_uring (shared SQ/CQ rings, reduced syscall overhead)
├── SR-IOV (5-10 µs, direct NIC access per pod)
├── AF_XDP (XDP redirect + UMEM rings, kernel-native fast path)
├── DPDK (user-space PMD polling, lowest packet latency)
└── RDMA (direct memory access, kernel bypass, NIC-assisted)

Distributed AI communication
├── NCCL (All-Reduce, All-Gather, Reduce-Scatter)
├── Ring vs Tree topology (bandwidth vs latency)
├── Incast (many-to-one → buffer overflow → retransmits)
└── GPU-to-GPU (NVLink, NVSwitch, GPUDirect RDMA)

Host-level tuning
├── NIC ring buffers (ethtool -G)
├── Interrupt coalescing (ethtool -C)
├── IRQ affinity (/proc/irq/*/smp_affinity)
├── Multi-queue NIC + RSS (ethtool -L, ethtool -X)
├── Jumbo frames (MTU 9000)
└── Kernel sysctl (tcp_rmem, tcp_wmem, somaxconn, BBR)
```

### Key skills

- Choose the right CNI for HPC (Cilium > Calico > Flannel)
- Tune TCP for high-throughput data pipelines
- Configure SR-IOV for low-latency GPU inference pods
- Understand NCCL collectives for distributed training/serving
- Set up RDMA device plugin for GPU-to-GPU KV cache transfer

### Interview signals

- "I replaced iptables-based kube-proxy with Cilium's eBPF
  datapath, reducing pod networking overhead from 100 µs to 25 µs"
- "For same-host LLM serving, I used Unix Domain Sockets
  instead of localhost HTTP, saving 5-10 ms per call"

---

# LAYER 3: PLATFORM & ORCHESTRATION

## "How do we deploy, isolate, and manage workloads?"

### Core concepts

```
Kubernetes
├── Pod spec (resources, limits, QoS classes)
├── CPU Manager (static policy → exclusive CPU pinning)
├── Topology Manager (single-numa-node → NUMA alignment)
├── Device Plugin (nvidia.com/gpu → GPU scheduling)
├── PriorityClass (fraud-scoring > training > analytics)
├── PodDisruptionBudget (maxUnavailable: 1)
├── Node affinity / pod anti-affinity (placement control)
└── Volumes (hugepages, shared memory, model storage)

cgroups v2
├── cpuset.cpus + cpuset.mems (CPU + memory node assignment)
├── cpu controller (CPU time limits)
├── memory controller (memory limits, OOM behavior)
└── pids controller (process count limits)

Observability
├── eBPF DaemonSets (Cilium Hubble, OBI, Pixie)
├── Prometheus + Grafana (metrics + dashboards)
├── OpenTelemetry Collector (traces + metrics pipeline)
└── Correlation dashboards (p99 ↔ retransmits ↔ GPU ↔ scheduler)
```

### Key skills

- Write K8s manifests with Guaranteed QoS + GPU + hugepages
- Configure CPU Manager + Topology Manager for NUMA-aware serving
- Set up eBPF observability without application code changes
- Design PriorityClass hierarchy for mixed GPU workloads
- Configure PDB for safe maintenance of GPU nodes

### Interview signals

- "I configured CPU Manager static policy with topology manager
  single-numa-node to ensure GPU inference pods got exclusive
  NUMA-aligned CPU cores"
- "I set up Cilium Hubble + OTel eBPF agent as DaemonSets for
  zero-code distributed tracing across our C++/Rust services"

---

# LAYER 4: GPU & ACCELERATOR

## "How does the GPU execute our workloads?"

### Core concepts

```
GPU architecture
├── SM (Streaming Multiprocessor) — NOT a CPU core, runs thousands of threads
├── Warps (32 threads executing in SIMT lockstep)
├── Warp divergence (if/else → threads wait for slowest path)
├── Tensor Cores (FP16/BF16/FP8 matrix multiply-accumulate)
├── Memory hierarchy (registers → shared/L1 → L2 → HBM)
├── HBM bandwidth (why LLM decode is memory-bandwidth-bound)
├── Coalesced access (contiguous reads vs scattered = 10x difference)
└── Occupancy (balance registers/shared-mem/threads per SM)

CUDA programming patterns
├── Kernel design (tiling, shared memory, warp primitives)
├── Streams (overlap H2D + compute + D2H)
├── CUDA Graphs (capture and replay → eliminate launch overhead)
├── Kernel fusion (combine small ops → reduce HBM traffic)
├── Pinned memory (avoid pageable → pinned staging copy)
├── Multi-GPU (peer copies, NVLink, NCCL collectives)
└── Profiling (Nsight Systems → timeline, Nsight Compute → per-kernel)

GPU-accelerated workloads in your pipeline
├── Transformer inference (BERT/DistilBERT via ONNX Runtime CUDA EP)
├── Vector search (FAISS GPU — GpuIndexIVFPQ, GpuIndexFlatIP)
├── Batched ranking (MLP on GPU via ONNX Runtime)
├── Embedding generation (query encoder on GPU)
└── LLM inference (vLLM / TensorRT-LLM / llama.cpp for warm path)
```

### Key skills

- Explain GPU architecture (SM, warps, memory hierarchy, Tensor Cores)
- Profile with Nsight Systems (timeline) and Nsight Compute (per-kernel)
- Identify and fix kernel launch overhead (CUDA Graphs)
- Identify and fix HBM traffic waste (kernel fusion)
- Use parallel GPU streams for concurrent independent workloads
- Keep data on GPU between stages (embedding → FAISS, zero D2H)

### Interview signals

- "Nsight Systems showed 30 small kernel launches with visible
  GPU idle gaps. I captured the forward pass as a CUDA Graph,
  reducing dispatch overhead from 150 µs to 5 µs"
- "The query embedding was already on GPU from the NLU model.
  I kept it there and fed it directly into FAISS GPU search
  with zero device-to-host transfer"

---

# LAYER 5: DATA MOVEMENT & ZERO-COPY

## "How does data flow through the system without unnecessary copies?"

### Core concepts

```
Zero-copy techniques
├── Shared memory (shm_open + mmap, cross-process zero-copy)
├── Memory-mapped files (mmap, file-backed zero-copy)
├── Arena allocators (bump allocation, bulk reset)
├── Offset-based references (not raw pointers, safe across processes)
├── Views instead of copies (std::span, string_view, Arrow Slice)
├── SPSC descriptor rings (move metadata, not data)
└── Pinned memory (for GPU H2D without staging copy)

Serialization optimization
├── Protocol Buffers (compact but still parse/unparse)
├── FlatBuffers (access without unpacking, mmap-friendly)
├── Cap'n Proto (wire format = memory format)
└── Arrow IPC (columnar, zero-copy on mmap/BufferReader)

Apache Arrow
├── Columnar in-memory format (zero-copy reads)
├── C Data Interface (zero-copy sharing across runtimes)
├── IPC format (cross-process, memory-mappable)
├── Buffer wrapping (expose existing memory as Arrow)
└── Language interop (C++, Rust, Python, Java, Go)

Data flow in your pipeline
├── Hot path: fixed-layout structs in shared arena
├── Stage handoff: SPSC rings carry offsets, not data
├── Feature block: one buffer, all models read from it
├── Publish boundary: Arrow RecordBatch for downstream
└── Warm path: Arrow columns → token-efficient LLM prompt
```

### Key skills

- Design shared memory arenas with hugepage backing
- Build SPSC descriptor rings for inter-stage signaling
- Wrap existing memory as Arrow buffers without copying
- Choose the right serialization format per boundary
- Eliminate redundant copies between models in the same process

### Interview signals

- "I replaced gRPC between pipeline stages with a hugepage-backed
  shared memory arena and SPSC descriptor rings, eliminating
  30-80 ms of serialization overhead"
- "All models — XGBoost, ONNX Runtime, FAISS — read from the
  same pre-allocated feature block. Zero copies between models"

---

# LAYER 6: AI MODEL SERVING & INFERENCE

## "How do we run models fast and reliably in production?"

### Core concepts

```
Model serving patterns
├── In-process embedding (XGBoost C API, ONNX Runtime C++, FAISS C++)
│   → zero network overhead, for hot-path models
├── Separate service (vLLM, TensorRT-LLM, Triton)
│   → managed batching/KV cache, for LLMs
└── Multi-task model (one backbone, multiple heads)
    → one forward pass for intent + NER + embedding

LLM inference runtime
├── vLLM (Python, PagedAttention, continuous batching, prefix caching)
├── TensorRT-LLM (C++, compiled engine, inflight batching, FP8)
├── SGLang (RadixAttention, tree-based prefix sharing)
├── llama.cpp (C/C++, embeddable, quantized GGUF)
└── NVIDIA Dynamo (KV-aware routing, disaggregated prefill/decode)

Key optimizations
├── CUDA Graphs (eliminate kernel launch overhead)
├── Micro-batching (bounded time window + batch size)
├── Prefix caching (skip shared system prompt prefill)
├── KV cache quantization (FP8/INT8 → 2x capacity)
├── KV-aware routing (route to worker with cached prefix)
├── Continuous / inflight batching (no head-of-line blocking)
├── Streaming (token-level → TTS, hide generation latency)
└── Circuit breaker + fallback (degrade gracefully)

Search & retrieval
├── BM25 keyword search (OpenSearch)
├── Semantic vector search (FAISS GPU)
├── Hybrid retrieval (BM25 ∥ FAISS → RRF fusion)
├── GPU-batched ranking (all candidates in one forward pass)
├── Embedding training (dual-encoder, contrastive loss, hard negatives)
└── Index management (daily rebuild, double-buffer hot-swap)
```

### Key skills

- Embed multiple models in one C++ process (XGBoost + ONNX + FAISS)
- Profile model serving with Nsight Systems
- Configure vLLM/TensorRT-LLM with prefix caching + KV routing
- Design hybrid retrieval (BM25 + semantic + RRF)
- Train and evaluate embedding models

### Interview signals

- "I consolidated three separate BERT models into one multi-task
  model with three output heads. One forward pass instead of three:
  GPU memory 1.5 GB → 300 MB, NLU latency 22 ms → 8 ms"
- "For the warm path LLM, prefix caching skips the 300-token
  system prompt prefill, reducing TTFT from 180 ms to 135 ms"

---

# LAYER 7: APPLICATION & BUSINESS LOGIC

## "What are we actually building and why?"

### Core concepts

```
Payment fraud detection (CapitalOne/Fiserv)
├── Hot path (10-30 ms): XGBoost + transformer + rules → Go/No-Go
├── Warm path (2-5 sec): 7B LLM reasoning → explain + overturn/confirm
├── Cold path (5-30 sec): LLM investigation → case packet + actions
├── 3DS integration (hot path flags → 3DS challenge → warm path runs during)
└── Feedback loop (confirmed labels → retrain models)

Conversational AI (Siri-like)
├── Multi-stage pipeline: ASR → NLU → Search → Ranking → Response → TTS
├── Zero-copy shared memory between stages
├── Hybrid retrieval (BM25 + semantic) for knowledge queries
├── Template-based response generation (pre-LLM era)
└── Experimental LLM integration (streaming → TTS)

Search & recommendation (LinkedIn-like)
├── Unified retrieval (LLM-generated dual-encoder embeddings)
├── Sequential ranking (transformer over 1000 interactions)
├── GPU neural retrieval (LiNR — index on GPU, exhaustive search)
├── Shared-context batching (80x speedup)
└── Custom Flash Attention kernel (2x over SDPA)

Security telemetry (Broadcom/Symantec)
├── High-throughput ingestion (DLP content scanning)
├── Policy enforcement gateway (backpressure, audit)
├── ML-assisted classification (false positive reduction)
└── Multi-tenant isolation (per-tenant quotas, rate limits)
```

### Key skills

- Design end-to-end pipelines with clear latency budgets per stage
- Map business requirements to technical architecture
- Choose the right model/serving pattern for each latency tier
- Design fallback and degradation strategies
- Connect optimizations to business impact (revenue, fraud loss, UX)

### Interview signals

- "The hot path processes every transaction in 10-30 ms using
  in-process XGBoost + transformer scoring. Only flagged
  transactions (1-5%) flow to the warm path for LLM reasoning"
- "The three-path architecture reduced false positives by 70%
  while maintaining sub-30ms authorization latency"

---

# HOW TO USE THIS MENTAL MODEL

## When debugging a performance problem

Start at Layer 7 (symptom) and work DOWN:

```
Layer 7: "p99 latency spiked on fraud scoring"
  → Layer 6: "Is model inference slow?" (check per-model timing)
  → Layer 5: "Is data movement slow?" (check serialization, copies)
  → Layer 4: "Is GPU underutilized?" (check Nsight Systems timeline)
  → Layer 3: "Is the CPU oversubscribed?" (check perf stat, context switches)
  → Layer 2: "Are there network retransmits?" (check Hubble, tcpretrans)
  → Layer 1: "Is the pod on the wrong NUMA node?" (check numastat, topology)
```

## When designing a new system

Start at Layer 7 (requirements) and work DOWN:

```
Layer 7: "We need sub-30ms fraud scoring"
  → Layer 6: "In-process models, not separate services"
  → Layer 5: "Zero-copy shared memory, not gRPC between stages"
  → Layer 4: "GPU for transformer + FAISS, CUDA Graphs for launch overhead"
  → Layer 3: "Isolated CPUs, NUMA-aligned, hugepage arena"
  → Layer 2: "Cilium eBPF CNI, tuned TCP, SR-IOV if needed"
  → Layer 1: "K8s with CPU Manager static, topology manager, Guaranteed QoS"
```

## When answering interview questions

Identify which layer(s) the question targets and go DEEP there,
then briefly connect to adjacent layers:

```
Q: "How did you optimize search latency?"
  → Primary: Layer 6 (multi-task BERT, GPU FAISS, batched ranking)
  → Connect UP to Layer 7 (search relevance improved 26%)
  → Connect DOWN to Layer 5 (zero-copy from BERT → FAISS)
  → Connect DOWN to Layer 4 (parallel GPU streams, zero D2H transfer)
```

---

# QUICK REFERENCE: ONE LINE PER LAYER

Layer 1: **Linux**       — "CFS schedules it, NUMA places it, hugepages cover it"
Layer 2: **Networking**  — "Cilium routes it, RDMA moves it, NCCL synchronizes it"
Layer 3: **Platform**    — "K8s deploys it, cgroups isolate it, eBPF observes it"
Layer 4: **GPU**         — "SMs compute it, Tensor Cores accelerate it, HBM feeds it"
Layer 5: **Data**        — "Shared memory holds it, Arrow formats it, rings signal it"
Layer 6: **Serving**     — "ONNX runs it, vLLM batches it, FAISS searches it"
Layer 7: **Application** — "Fraud scores it, Siri speaks it, LinkedIn ranks it"
