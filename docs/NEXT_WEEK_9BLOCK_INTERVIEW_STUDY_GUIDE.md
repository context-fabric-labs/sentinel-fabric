# Next-Week Interview Study Guide — The 9-Block Master Cheat Sheet

**Target companies:** Cisco, Socure, d-Matrix, Akamai, ByteDance, Microsoft (AI Frameworks)
**Anchor projects:** Sentinel Fabric (control-plane + data-plane), Search/Recommendation Infra, CapitalOne / Fiserv / Apple Siri / Broadcom stories
**How to use:** Each block has (1) the *memorize* core, (2) the *deep dive* with why, (3) the *Sentinel/Search mapping* so every claim is backed by code you wrote, (4) *likely questions + crisp answers*, and (5) the *one-liner to land in the room*.

> **The single sentence that frames you across all 9 blocks:**
> *"Every system I build is a fast deterministic data plane sitting in front of a slow, strongly-consistent control plane. I serve the overwhelming majority of traffic from the cheap, zero-copy, per-core tier, and I only pay the expensive cost — consensus, fsync, or a heavy GPU model — when correctness or quality actually demands it. The art is making that boundary as cheap and as rare as possible."*

---

## 0. THE 90-SECOND PROJECT FRAME (say this before any block)

**Sentinel Fabric** = a two-plane LLM serving system you built end-to-end:

- **Control plane (Rust / Axum / Tokio):** gateway + scheduler. A `PodRegistry` tracks backend pods; a `PodScorer` ranks them with an *explainable composite score* across four components — inflight load, GPU-memory headroom, latency EWMA, and error rate — multiplied by a static weight. Adds admission control, session stickiness, KV-cache-pressure awareness, policy-based selection, real vLLM forwarding (OpenAI-compatible `/v1/completions`, `/v1/chat/completions`), and full Prometheus metrics + tracing + a `/debug/route` explainability endpoint.
- **Data plane (C++ / CUDA — "PM-NIXL"):** zero-copy GPU transfer engine. Pinned (page-locked) host buffers, async DMA over CUDA streams, event-based completion, H2D/D2H transfers, KV-page copy primitives, and a benchmark harness proving pinned-vs-pageable speedups.

**Search Infra** = FAISS (IVF-PQ / HNSW / CAGRA) ANN retrieval, two-tower dense + BM25 sparse hybrid, multi-stage funnel (candidate-gen → pre-rank → rank → re-rank), zero-copy Apache Arrow between stages.

Everything below hangs off these two artifacts.

---

# BLOCK 1 — Production LLM Serving Architecture
**Focus: Cisco, Socure, d-Matrix**

## Memorize — the 5 layers
| Layer | Owns | Sentinel Fabric mapping |
|---|---|---|
| **Gateway** | Auth, tenant isolation, quotas, request shaping, rate limiting | Axum API server; request-shape extraction; admission controller |
| **Scheduler** | Queue-depth tracking, dynamic priority, admission control, continuous batching | `PodScorer` composite score; admission/degrade modes; KV-pressure routing |
| **Runtime** | vLLM, SGLang, TensorRT-LLM, Triton, ONNX Runtime (edge) | `Backend` trait → `vllm.rs` adapter (pluggable) |
| **GPU Workers** | Prefill/decode separation, KV-cache mgmt, tensor parallelism (NVLink) | Pod = GPU worker; KV-pressure snapshot per pod |
| **Ops** | Metrics, canary deploy, model registries, automated eval gates | Prometheus metrics, tracing, `/debug/route`, health snapshots |

## Deep dive — what each layer actually does
- **Gateway:** terminates the request, authenticates the tenant, enforces per-tenant quota/rate limit *before* any GPU work, and shapes the request (extracts prompt length, model id, session id) into a normalized `RequestShape`. **Policy lives here, not in the model.**
- **Scheduler:** the brain. Tracks queue depth and inflight per pod, applies admission control (reject/degrade under load), and routes by composite score. Continuous (in-flight) batching is the throughput multiplier — new requests join the running batch every decode iteration instead of waiting for a batch boundary.
- **Runtime:** the actual engine that executes the forward pass. Kept behind a thin `Backend` trait so vLLM today can become TRT-LLM or SGLang tomorrow with zero scheduler changes.
- **GPU workers:** separate **prefill** (compute-bound, processes the whole prompt, O(n²) attention) from **decode** (memory-bound, one token at a time, KV-cache reads). Tensor parallelism splits a layer across GPUs over NVLink.
- **Ops:** metrics + canary + registry + eval gates make change safe. Every weight update passes an automated quality gate (see Block 9).

## Sentinel mapping (concrete)
`PodScore` carries `score_inflight`, `score_gpu_headroom`, `score_latency`, `score_error_rate`, `weight_multiplier`, and an `exclusion_reason` — so every routing decision is **explainable** through `/debug/route`. `PodHealthSnapshot` feeds it `inflight_count`, `gpu_memory_used_mb`, `latency_ewma_ms`, `error_rate`.

## Likely questions
**Q: Walk me through a request through your serving stack.**
A: Gateway authenticates the tenant and shapes the request → admission controller checks quota/load and either admits, degrades, or fast-rejects → scheduler scores all healthy pods (inflight + GPU headroom + latency + error rate) and picks the best, honoring session stickiness → `Backend` adapter forwards to vLLM's OpenAI-compatible endpoint → response streams back while metrics/traces record the full lifecycle.

**Q: Why a composite score instead of round-robin or least-connections?**
A: Round-robin ignores heterogeneity; least-connections ignores GPU memory and latency. A weighted composite lets me trade off load *and* KV headroom *and* tail latency *and* error rate simultaneously, and because each component is stored separately, the decision is auditable — I can answer "why this pod?" in production.

**Q: How do you separate prefill and decode?**
A: Prefill is compute-bound (process the full prompt once) and decode is memory-bound (one token/iteration bottlenecked by KV-cache bandwidth). Disaggregating them lets each run on hardware/batch sizes tuned to its bottleneck and prevents a long prefill from stalling many short decodes.

## One-liner to land
*"I treat serving as four contracts — gateway, scheduler, runtime, GPU worker — with the scheduler making explainable, health-aware routing decisions, and the runtime kept pluggable behind a thin trait so vLLM today can be TRT-LLM tomorrow without touching the control plane."*

---

# BLOCK 2 — Latency & System Debugging
**Focus: Akamai, d-Matrix**

## Memorize — the latency decomposition (in order)
```
Gateway overhead → Queue wait → Tokenization → Prefill execution
→ Decode TPOT → KV-cache allocation → H2D/D2H copies → Network streaming
```

## Memorize — the pivot dimensions (bucket & slice by)
Prompt/output **token length** · **model variant** · **tenant partition** · **target hardware type** · **concurrent batch size** · **prefix-cache hit/miss ratio**.

## Memorize — the telemetry to pull
GPU **SM utilization** · **HBM capacity/pressure** · **KV-cache fragmentation** · **continuous-batching behavior** (batch fill, preemptions) · cross-socket **PCIe/NUMA** bottlenecks.

## The debugging script ("P99/TTFT went up — what do you do?")
1. **Don't average — decompose.** Split the latency into the 8 segments above; a p99 regression almost always lives in *one* segment.
2. **Bucket, then pivot.** Re-slice p99 by the six dimensions. A regression that's invisible in aggregate ("p99 up 30%") becomes obvious when pivoted ("only long-prompt requests on A10 in tenant-7 regressed → prefix-cache hit rate dropped").
3. **Correlate with telemetry.** Map the bad segment to a resource: TTFT up + SM idle → queue/admission or tokenization (CPU/GIL); TPOT up + HBM full → KV fragmentation/eviction; H2D up → pageable (not pinned) memory or PCIe contention from a noisy NUMA neighbor.
4. **Confirm with a profiler** (Block 8/9): `nsys` timeline to see if a CUDA Graph fell back to eager launches; `ncu` if a specific kernel slowed.

## Sentinel/data-plane mapping
- TTFT regressions → check admission queue depth and `PodScorer` inflight component (control plane already exports these as Prometheus gauges).
- H2D regressions → the data-plane benchmark harness directly measures **pinned vs pageable** transfer; a regression here means a buffer wasn't allocated from the pinned pool.
- KV pressure → per-pod KV-pressure snapshot drives routing; rising pressure + falling stickiness hit rate explains a TPOT cliff.

## Likely questions
**Q: TTFT is fine but TPOT doubled. Where do you look?**
A: TPOT is the decode loop — memory-bound. I check HBM pressure and KV-cache fragmentation first (paged-attention block churn), then batch composition (a few huge contexts crowding the batch), then whether decode kernels are memory-bandwidth-bound on the roofline. SM utilization being *low* during the regression confirms it's memory-bound, not compute.

**Q: p99 is up 30% but p50 is flat. What does that tell you?**
A: A tail-only regression — not a systemic slowdown. It's contention or a long-tail bucket: KV evictions under pressure, queue spikes at peak batch, a single slow pod dragging the tail, or prefix-cache misses on a subset of tenants. I pivot p99 by tenant/model/length/hardware to isolate the offending bucket.

**Q: How do you tell memory-bound from compute-bound in the field?**
A: SM utilization + arithmetic intensity. High SM% with the kernel sitting on the compute roofline = compute-bound (quantize, fuse). Low SM% with HBM bandwidth saturated = memory-bound (batch more, compress KV, raise arithmetic intensity).

## One-liner to land
*"I never debug latency on an average. I decompose into the eight segments, pivot p99 by token-length / model / tenant / hardware / batch / cache-hit, then map the bad segment to a hardware resource and confirm it with an nsys timeline."*

---

# BLOCK 3 — Framework Matrix: vLLM vs TRT-LLM vs SGLang vs Triton vs ONNX
**Focus: d-Matrix, Cisco**

## Memorize — the one-line identity of each
| Framework | Identity | Pick it when |
|---|---|---|
| **vLLM** | PagedAttention + continuous batching; flexible production serving | General high-throughput LLM serving, dynamic traffic |
| **SGLang** | Radix/prefix-tree caching; structured/programmatic flows | Multi-agent, RAG, shared system prompts, complex control flow |
| **TensorRT-LLM** | Fused kernels + pre-compiled static engines; peak NVIDIA perf | Max data-center throughput on fixed NVIDIA HW, stable models |
| **Triton Inference Server** | Multi-model orchestration; concurrent backends, versioning, dynamic batch | Serving many models/frameworks together with version control |
| **ONNX Runtime** | Cross-compiled runtime abstraction across platforms | Edge/device portability without re-authoring per hardware |

## Deep dive — the tradeoff axis
- **Flexibility ↔ peak performance.** vLLM/SGLang are flexible and adapt to dynamic traffic; TRT-LLM compiles static engines for the absolute best kernels but is rigid (recompile on change).
- **PagedAttention (vLLM)** solves KV fragmentation via OS-style paging — 60–80% memory utilization vs 20–40% naive, ~4× more concurrent requests on an A100.
- **Radix/prefix caching (SGLang)** dedupes *shared prefixes* (system prompts, agent context) across requests — drops prefill cost for agentic/RAG workloads.
- **Triton** is the orchestration *platform*, not an engine — it fronts TF/PyTorch/ONNX/TRT backends with dynamic batching, model ensembles, and concurrent instances.
- **ONNX Runtime** is the portability layer — train in PyTorch, export to ONNX, run the same graph across CPU/GPU/edge with execution-provider swaps.

## The Edge Architecture position (say verbatim)
> *"I deliberately design edge-compatible model architectures exported to ONNX to decouple our core intelligence from the underlying physical hardware. While I deprioritize bare-metal on-device optimization in favor of high-throughput cloud stacks, I use ONNX Runtime execution wrappers to ensure clean device portability — and I validate cross-platform compatibility by running automated sanity tests against target-device simulations (e.g., Apple HomePod simulator environments) to verify inference correctness, layout behavior, and structural compliance before pushing variants to the edge."*

## Sentinel mapping
The `Backend` trait is exactly this matrix in code: `vllm.rs` today, but the trait (`generate`, `health_check`, `default_timeout`) means swapping to a TRT-LLM or SGLang backend is an adapter change, not a rearchitecture. The scheduler never knows which engine it's talking to.

## Likely questions
**Q: vLLM or TRT-LLM for production?**
A: Depends on traffic shape and change cadence. Dynamic, multi-tenant, frequently-updated models → vLLM (PagedAttention + continuous batching absorbs variance). Fixed model, fixed NVIDIA hardware, max throughput per dollar → TRT-LLM's compiled fused engines. I'd hide both behind one backend trait and benchmark on my real traffic before committing.

**Q: When does SGLang beat vLLM?**
A: Heavy shared-prefix workloads — multi-agent systems and RAG where every request carries the same long system prompt and tool context. SGLang's radix-tree cache reuses that prefill across requests; vLLM's prefix caching is less structured for cyclic agent graphs.

**Q: Why ONNX if it's not the fastest?**
A: Portability, not peak speed. ONNX decouples the model from the hardware so one exported graph runs across server GPU, CPU, and edge devices via execution providers — I validate it against device simulators rather than re-authoring kernels per target.

## One-liner to land
*"I keep the runtime pluggable behind a backend trait: vLLM for flexible dynamic serving, SGLang for shared-prefix agentic flows, TRT-LLM for compiled peak throughput, Triton to orchestrate many models, and ONNX Runtime as the portability layer to the edge."*

---

# BLOCK 4 — Multi-Agent & Orchestration Architectures
**Focus: Socure, Cisco**

## Memorize — the four pillars
1. **State management:** session state across multi-turn cyclic graph paths (LangGraph or custom state abstractions).
2. **Radix/prefix-tree caching:** SGLang caches shared system instructions, persistent prompts, and multi-agent context prefixes → drops prefill overhead.
3. **Tool orchestration:** *decouple selection from execution.* The LLM emits structured tool parameters; the **gateway** intercepts, authorizes, handles external network timeouts, and returns payloads to the context window.
4. **Routing optimization:** small distilled classifiers (~8B) for triage/document validation; reserve large reasoners (~70B) for final evaluation loops only.

## Deep dive — why decouple tool selection from execution
The model proposes *what* tool to call and *with what args* (structured output). It must **never** execute. The gateway is the trust boundary: it authorizes the call against the tenant's policy, enforces timeouts/retries on the external dependency, sanitizes the result, and only then writes the payload back into context. This is what makes agents safe and debuggable.

## The hierarchical-routing pattern (your Fiserv/CapitalOne story)
8B classifier triages → extracts structured fields → composer builds a minimal context → 70B reasoner runs only on what matters. Result in your Fiserv story: **60% token reduction, +15% accuracy, 8.5s→2.3s (3.7×)**. The KV-cache stays warm for returning applicants via **session-affine sticky routing → 70% cache hit rate**.

## Sentinel mapping
Session stickiness (control-plane Story 3) is the mechanism that keeps an agent's session pinned to the pod where its KV/prefix cache is warm — directly enabling the radix-cache and session-affinity claims.

## Likely questions
**Q: How do you keep multi-turn agent state without re-prefilling everything?**
A: Two layers — sticky routing pins the session to the pod whose KV/prefix cache holds the shared prefix, and SGLang's radix tree reuses the system-prompt/agent-context prefix across turns. So each turn only prefills the *new* tokens, not the whole history.

**Q: How do you stop an agent from calling a tool it shouldn't?**
A: The model only *emits* a structured tool request; it never executes. The gateway authorizes every call against tenant policy, applies timeouts to the external dependency, and fails closed if authorization or the call times out. Tool execution lives outside the model's trust boundary.

**Q: How do you control cost in an agent loop?**
A: Tiered routing — a distilled 8B model handles triage, validation, and routing; the expensive 70B reasoner only runs on the final evaluation step. That's how Fiserv hit 60% fewer tokens while improving accuracy.

## One-liner to land
*"Agents are an orchestration problem, not a model problem: I keep state warm with sticky routing and radix-prefix caching, I route cheaply with distilled classifiers and reserve the 70B for final reasoning, and I make the gateway — not the model — the trust boundary that authorizes and executes tools."*

---

# BLOCK 5 — Infrastructure & Networking Stack
**Focus: Akamai, d-Matrix**

## Memorize — the four mechanisms
1. **Memory pipelining:** pinned (page-locked) host-memory pools + async DMA streams → kill H2D bottlenecks.
2. **Zero-copy data layout:** Apache Arrow columnar memory map or hugepage-backed shared-memory arenas → eliminate microservice serialization tax.
3. **Lock-free coordination:** SPSC ring buffers streaming 32-byte descriptors → wait-free thread sync.
4. **Topology awareness:** Kubernetes single-NUMA-node constraints pinning memory controllers, NICs, and accelerators to the same socket.

## Deep dive — why each matters
- **Pinned memory + async DMA:** pageable memory forces the driver to stage through a pinned bounce buffer (an extra copy) and blocks overlap. Pinned buffers let the copy engine DMA directly and overlap with compute via separate CUDA streams → your data plane's measured **pinned-vs-pageable** speedup (your stories cite up to **3.2× faster H2D**, 85% GPU utilization).
- **Arrow / shared memory zero-copy:** serializing a feature vector to JSON between services costs ~200µs and produces GC garbage. Arrow wraps existing bytes as columnar arrays — Tier 2 reasoning, Spark analytics, and PyTorch retraining all read the *same physical bytes*, no deserialization.
- **SPSC ring buffer:** single-producer/single-consumer means no locks, no CAS contention — just a head/tail index pair. Streaming small 32-byte descriptors keeps the hot path wait-free.
- **NUMA pinning:** a request whose memory is on socket 0 but whose GPU hangs off socket 1 pays a cross-socket PCIe/UPI hop. Pinning the whole pipeline (memory controller + NIC + GPU) to one NUMA node removes that tax — and it's a frequent hidden cause of p99 tails (Block 2).

## Sentinel/data-plane mapping (this IS your code)
PM-NIXL data plane: `MemoryKind::HostPinned` buffers, `AsyncTransferEngine` over CUDA streams, event-based completion, KV-page copy primitives, and a benchmark harness with size sweeps and pinned-vs-pageable CSV/JSON output. This block is literally a walkthrough of the data plane you wrote.

## Likely questions
**Q: Why pinned memory — what does it actually buy you?**
A: Two things: the GPU copy engine can DMA directly without staging through a driver bounce buffer (one fewer copy), and the transfer can overlap with compute on a separate stream. My data-plane benchmarks quantify it — pinned vs pageable across a size sweep — and the stories show ~3.2× on H2D.

**Q: Zero-copy between services — how, concretely?**
A: Apache Arrow columnar buffers or hugepage-backed shared-memory arenas. The producer writes once; every consumer (reasoning tier, analytics, retraining) maps the same bytes and reads by pointer. No serialize/deserialize, no GC churn — that's the 200µs/JSON tax gone.

**Q: Why SPSC specifically, not MPMC?**
A: SPSC is wait-free with just two indices and no atomics-heavy contention. By assigning one producer and one consumer per ring and sharding by core, I get the throughput of lock-free without the complexity and contention of multi-producer queues.

**Q: Why does NUMA pinning matter for inference?**
A: Cross-socket hops add latency and jitter. Pinning memory, NIC, and GPU to one NUMA node via Kubernetes topology constraints keeps the whole data path on one socket — it's one of the first things I check when p99 tails appear with healthy averages.

## One-liner to land
*"My data plane is built on four ideas: pinned memory + async DMA to overlap transfer and compute, Arrow/shared-memory zero-copy to delete the serialization tax, SPSC rings for wait-free coordination, and single-NUMA pinning so the whole path lives on one socket. I built and benchmarked all four in the PM-NIXL data plane."*

---

# BLOCK 6 — Responsible AI, Security & Isolation
**Focus: Cisco, Socure**

## Memorize — the four rules
1. **Policy enforcement outside the model:** input validation, syntax checks, threat evaluation live in the **gateway** — never trust the model to self-police.
2. **Explicit access authorization:** strict access control on tools *and* semantic indices; tenants can **never** cross-query each other's vector-store partitions.
3. **Zero-overhead redaction:** strip PII/sensitive patterns at the ingest proxy with deterministic rules **before** data hits the tokenizer.
4. **Fail-closed infrastructure:** if a guardrail or external validation loop times out, the transaction **fails closed** — never emit unverified output.

## Deep dive
- **Why outside the model:** an LLM is a probabilistic component; you cannot prove it will refuse. Deterministic gateway checks (schema validation, allow-lists, threat rules) are auditable and provable. The model is *inside* the trust boundary, never *is* the boundary.
- **Tenant isolation for vector stores:** multi-tenant ANN indices must be partitioned with hard access control. A tenant querying another's partition is a data-exfiltration bug. Enforce partition scoping at the retrieval gateway, not by convention.
- **Redaction before tokenization:** once PII is tokenized and in the KV cache, it can leak through prefix-cache sharing or logs. Strip it deterministically at ingest so sensitive bytes never reach the model.
- **Fail-closed:** availability is *not* worth emitting an unverified/unsafe generation in a security or fraud context. On guardrail timeout, reject — the default is "no answer," not "unchecked answer."

## Sentinel/Broadcom mapping
Broadcom Cloud SWG story: 2M+ req/s, 6 models in parallel, per-tenant isolation, fail-closed security posture, 99.99% availability. Sentinel's admission controller is the gateway enforcement point; the `exclusion_reason` + admission-reject metrics make policy decisions auditable.

## Likely questions
**Q: Where does guardrail logic live and why?**
A: In the gateway, before and after the model — never in the model. Deterministic validation, threat checks, and PII redaction are provable and auditable; a probabilistic model can't be trusted to police itself. The model runs inside the trust boundary the gateway enforces.

**Q: How do you isolate tenants in a shared vector store?**
A: Hard partition scoping with explicit access control at the retrieval layer. Every query is authorized against the tenant's partition; cross-partition reads are impossible by construction, not by convention. Same principle for tools and semantic indices.

**Q: A guardrail service times out under load. What happens to the request?**
A: It fails closed. In a security/fraud context, emitting an unverified generation is worse than returning nothing. The transaction is rejected and surfaced in metrics — availability never overrides safety on the validation path.

## One-liner to land
*"Security is a property of the gateway, not the model. I enforce policy, authorize tools and per-tenant index partitions, and redact PII before tokenization deterministically — and if any guardrail times out, I fail closed, because an unverified answer is worse than no answer."*

---

# BLOCK 7 — Model Lifecycle & Integration
**Focus: d-Matrix**

## Memorize — the best line (say verbatim)
> *"I view PyTorch primarily as an upstream environment optimized for training, fine-tuning, parameter-efficient adaptation (LoRA/QLoRA), and baseline validation. Once a variant satisfies our quality gates, I export the computational path into highly optimized production runtimes — vLLM, SGLang, or custom C++ engines using ONNX Runtime — to satisfy strict low-latency and hardware-specific execution constraints."*

## Deep dive — the train→serve boundary
- **Upstream (PyTorch):** flexible, eager, great for training, LoRA/QLoRA adaptation, and eval. Bad for production latency (Python overhead, no kernel fusion, no static engine).
- **Quality gate:** the variant must pass accuracy + latency + regression gates (Block 9) before promotion.
- **Downstream (production runtime):** export the *computational path* (graph) — to vLLM/SGLang for serving, ONNX for portability, or TRT-LLM/custom C++ for peak. The training framework and serving runtime are deliberately different tools.
- **LoRA/QLoRA:** adapt cheaply upstream (train small adapters, not full weights); merge or hot-load adapters at serve time.

## Sentinel mapping
The `Backend` trait abstraction (Block 1/3) is the integration seam: a model promoted out of PyTorch lands behind the same trait whether it's served by vLLM or a custom C++/ONNX engine — the control plane is unchanged.

## Likely questions
**Q: Why not just serve PyTorch directly?**
A: Eager PyTorch carries Python overhead, no kernel fusion, and no static engine — fine for training and eval, wrong for p99-bound serving. I treat PyTorch as upstream (train, LoRA/QLoRA, validate) and export the graph to vLLM/SGLang/ONNX/TRT-LLM downstream once it clears the quality gate.

**Q: How do you ship a fine-tuned variant safely?**
A: Adapt with LoRA/QLoRA upstream, run it through automated accuracy + latency + regression gates, then export and promote behind the backend trait with a canary. If the canary's nsys timeline or quality metrics regress past threshold, it never reaches full traffic.

## One-liner to land
*"PyTorch is my training and adaptation environment; production is a different tool. Once a LoRA/QLoRA variant clears the quality gate, I export the graph into vLLM, SGLang, or a custom ONNX/C++ engine — the right runtime for the latency and hardware constraints, behind the same backend trait."*

---

# BLOCK 8 — Target Testing Frameworks & Tools
**Focus: ByteDance, Akamai**

## Memorize — the three capabilities
1. **Simulation testing:** hardware emulation + CI loops against simulated targets → verify cross-platform compiler output without manual device setups.
2. **Apple HomePod sanity targets:** run compiled configs against HomePod simulation layers using core-framework profiling tools to audit layout behavior, numerical-precision consistency, and memory usage.
3. **System profilers:** standardize on native tooling — **NVIDIA Nsight Systems (`nsys`)** and **Nsight Compute (`ncu`)** for data-center profiling; custom telemetry hooks in ONNX Runtime to trace performance across deployment environments.

## Deep dive — nsys vs ncu (know the difference cold)
- **`nsys` (Nsight Systems):** *timeline / system-wide* view. Shows CPU↔GPU overlap, kernel launch gaps, memcpy stalls, CUDA Graph vs eager launches, stream concurrency. Use it to find *where time goes* across the whole pipeline.
- **`ncu` (Nsight Compute):** *single-kernel deep dive*. Occupancy, memory throughput, warp stalls, roofline placement. Use it to find *why a specific kernel is slow*.
- **Workflow:** `nsys` first to localize the bad region/segment (ties back to Block 2), then `ncu` to dissect the offending kernel.

## ONNX validation hooks
Custom telemetry inside ONNX Runtime traces per-operator timing across CPU/GPU/edge execution providers — that's how you prove a model behaves identically (latency + numerics) across deployment targets, including simulated edge devices.

## Likely questions
**Q: When do you reach for nsys vs ncu?**
A: `nsys` for the system timeline — to see launch gaps, H2D/D2H stalls, and whether a CUDA Graph fell back to eager launches. `ncu` once I've localized a single slow kernel — to inspect occupancy, memory throughput, and roofline placement. Timeline first, kernel deep-dive second.

**Q: How do you validate a model for an edge device without the hardware?**
A: Simulation. I run the compiled config against device simulators (e.g., HomePod) in CI, auditing layout behavior, numerical-precision consistency, and memory footprint, and I attach ONNX Runtime telemetry hooks to compare per-operator timing across targets — so I catch divergence before shipping to real devices.

## One-liner to land
*"I profile with native tooling on a timeline-then-kernel workflow — nsys to localize the segment, ncu to dissect the kernel — and I validate edge portability in CI against device simulators with ONNX Runtime telemetry hooks, so cross-platform correctness is tested before any hardware is touched."*

---

# BLOCK 9 — Performance Tooling, Regression Detection & Quality Bars
**Focus: Microsoft AI Frameworks**

## Memorize — the automated regression gate
- **CI gate** intercepts every runtime compilation or weight update.
- Run a **multi-size synthetic workload batch** to capture deterministic `nsys` timelines.
- **Baseline compare:** parse `cuda_api_sum` and `cuda_gpu_kern_sum` from the `.nsys-rep` files. **Block deploy / page** if steady-state compute-kernel time slips **>2%**, or if an unexpected `cudaLaunchKernel` appears (a CUDA-Graph→eager fallback).

## Memorize — benchmark the whole stack (not just the app)
```
Programming Models (Triton/CUDA) → Compilers (TorchInductor/XLA)
→ Runtimes (ONNX Runtime/TensorRT-LLM) → Serving APIs (vLLM/Triton Server)
```
Use synthetic token-traffic generators (e.g., **genai-perf**) to stress variable request distributions.

## Memorize — the quality bar (hard gates)
- **TTFT < 500ms** at peak concurrent batch.
- **TPOT** maps cleanly to hardware memory-bandwidth limits (verify arithmetic intensity sits on the memory-bound roofline).
- **Useful-compute ratio:** measure SM cycles spent on actual GEMM vs cycles wasted on CPU dispatch latency, memory thrashing, or tokenization GIL.

## The Microsoft performance line (say verbatim)
> *"When optimizing workloads at trillion-inference scale, you can't rely on manual profiling. I treat performance validation as a scalable distributed system — automated benchmarking frameworks that trace execution metrics from the high-level serving API all the way down to individual CUDA kernel launch timelines. That ensures every optimization or compilation change translates into measurable hardware efficiency and Capex reduction on the data-center floor."*

## Deep dive — why the 2% / eager-fallback gate is the right tripwire
A >2% steady-state kernel slip is below human noticing but compounds to massive Capex at fleet scale. The *eager-fallback* check is even sharper: a CUDA Graph collapsing to per-kernel `cudaLaunchKernel` reintroduces ~5µs/launch × dozens of kernels — a silent latency regression that aggregate dashboards miss but `nsys` API summaries catch deterministically.

## Sentinel mapping
The control plane already exports the serving-API-level metrics (latency histograms, tokens generated, per-component scores). Block 9 extends that *down the stack* to kernel-level nsys parsing — closing the loop from `/v1/chat/completions` p99 to the individual GEMM that caused it.

## Likely questions
**Q: How do you stop a silent perf regression from reaching production?**
A: An automated CI gate on every compile/weight change runs a fixed synthetic workload, captures deterministic nsys timelines, and diffs `cuda_gpu_kern_sum`/`cuda_api_sum` against baseline. >2% steady-state kernel slip or an unexpected eager `cudaLaunchKernel` (CUDA-Graph fallback) blocks the deploy and pages — no human eyeballing required.

**Q: What are your production-readiness hard gates?**
A: TTFT < 500ms at peak batch, TPOT aligned to the memory-bandwidth roofline, and a healthy useful-compute ratio — SM cycles on real GEMM vs wasted on CPU dispatch, memory thrashing, or tokenization GIL. If any fails, it doesn't ship.

**Q: Why benchmark the whole stack instead of the API?**
A: An API-level number hides *where* the cost is. I instrument from programming model (CUDA/Triton) → compiler (TorchInductor/XLA) → runtime (ONNX/TRT-LLM) → serving API (vLLM/Triton), so a regression maps to a specific layer instead of a vague "it got slower."

## One-liner to land
*"I treat performance validation as a distributed system: an automated CI gate captures deterministic nsys timelines on every change, diffs kernel-summary tables against baseline, and blocks deploy on a >2% slip or a CUDA-Graph-to-eager fallback — tracing every regression from the serving API down to the individual GEMM."*

---

# RAPID-FIRE CROSS-BLOCK Q&A (final-night drill)

| # | Question | One-breath answer |
|---|---|---|
| 1 | Continuous vs static batching? | Continuous joins new requests every decode iteration → no batch-boundary idle, 2–4× throughput. |
| 2 | PagedAttention in one sentence? | OS-style paging for KV cache → 60–80% memory utilization, ~4× more concurrent requests. |
| 3 | Prefill vs decode? | Prefill = compute-bound O(n²) prompt pass; decode = memory-bound one-token loop on KV bandwidth. |
| 4 | TTFT vs TPOT — which for chat? | TTFT (users feel initial delay most); TPOT controls streaming smoothness. |
| 5 | Pinned memory buys you? | Direct DMA (no bounce buffer) + overlap with compute → ~3.2× H2D in my data plane. |
| 6 | Zero-copy between services? | Arrow columnar / hugepage shared memory → consumers read same bytes, no serialize tax. |
| 7 | SPSC vs MPMC? | SPSC is wait-free (two indices, no contention); shard by core to scale. |
| 8 | NUMA pinning matters because? | Cross-socket PCIe/UPI hops add tail latency; pin memory+NIC+GPU to one node. |
| 9 | Where do guardrails live? | Gateway, never the model — deterministic, auditable, provable. |
| 10 | Guardrail timeout → ? | Fail closed; unverified output is worse than no output. |
| 11 | 8B vs 70B routing? | Distilled 8B for triage/validation; 70B only for final reasoning → 60% token cut. |
| 12 | nsys vs ncu? | nsys = system timeline (find the segment); ncu = single-kernel deep dive (find why). |
| 13 | Regression tripwire? | >2% kernel-time slip or unexpected eager cudaLaunchKernel (CUDA-Graph fallback). |
| 14 | PyTorch's role? | Upstream training/LoRA/eval only; export to vLLM/SGLang/ONNX/TRT-LLM for serving. |
| 15 | Composite routing score components? | inflight + GPU headroom + latency EWMA + error rate, × weight, all explainable. |
| 16 | ONNX Runtime's job? | Portability — one graph across server/CPU/edge via execution providers. |
| 17 | SGLang's edge over vLLM? | Radix/prefix-tree caching for shared system prompts in agentic/RAG flows. |
| 18 | p99 up, p50 flat → ? | Tail-only: contention/evictions/cache-miss bucket; pivot p99 by tenant/length/model/HW. |
| 19 | Memory- vs compute-bound test? | Low SM% + HBM saturated = memory-bound; high SM% on compute roofline = compute-bound. |
| 20 | Quality bar gates? | TTFT < 500ms at peak, TPOT on memory roofline, high useful-compute (GEMM) ratio. |

---

# COMPANY-FOCUS MAP (what to emphasize per interview)

| Company | Lead with blocks | Signature line |
|---|---|---|
| **Cisco** | 1, 3, 6 | Pluggable runtime trait + gateway-enforced security/isolation. |
| **Socure** | 4, 6 | Agentic orchestration + fail-closed fraud/security posture. |
| **d-Matrix** | 1, 3, 5, 7 | Framework matrix + bare-metal data plane + train→serve export. |
| **Akamai** | 2, 5, 8 | Latency decomposition + NUMA/zero-copy + nsys/ncu profiling. |
| **ByteDance** | 8 | Simulation testing + native profiler standardization. |
| **Microsoft (AI Frameworks)** | 9, 7, 3 | Automated nsys-parsing CI gate + whole-stack benchmarking. |

---

*Built from the 9-block study track + Sentinel Fabric control-plane/data-plane code + Search-Deep-Dive + Inference-Deep-Dive notes. Every claim above is backed by an artifact you can point to in the room.*
