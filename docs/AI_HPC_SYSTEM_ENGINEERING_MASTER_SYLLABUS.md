# AI/HPC System Engineering Master Syllabus

**Audience:** Senior/Staff AI Systems Engineer and Solutions Architect interview preparation for NVIDIA, Apple, Broadcom, HPE, Capital One, and similar $400K+ compensation roles.

**Organization Principle:** Stories first. Every technology is anchored to where it was used, why it was chosen, what broke without it, and how to explain it in an interview.

## Story Index

| ID | Story | Primary Interview Angle |
|----|-------|--------------------------|
| **A** | Sentinel Gateway / CapitalOne Fraud Platform | Sub-5ms fraud scoring, Rust gateway, C++/CUDA hot path, bare-metal-style K8s tuning |
| **B** | LLM Serving Infrastructure / Fiserv Sentinel | vLLM/SGLang/TensorRT-LLM, KV-cache routing, CUDA transfer optimization, multi-model LLM serving |
| **C** | Siri-Style Conversational AI Pipeline / Apple | ASR -> NLU -> Search -> Orchestration -> TTS, shared memory, streaming, Apple Silicon |
| **D** | Broadcom Cloud Secure Web Gateway | Multi-model security inference, XGBoost + BERT + CNN + NER, hybrid cloud, multi-tenancy |

---

# Section 1: Story Summaries

## Story A: Sentinel Gateway / CapitalOne Fraud Platform

### 2-Minute Version

"At Capital One, I worked on a real-time fraud detection platform processing about 24,500 transactions per second with a sub-5ms p99 authorization budget. The core challenge was that fraud detection had two different personalities: the inline decision path had to be deterministic and extremely fast, while the decline-reasoning and triage paths needed richer AI reasoning with LLMs. I separated those paths physically and logically.

Tier 1 was a per-core zero-copy scoring pipeline in Rust and C++/CUDA. Each transaction was parsed once into a borrowed `TxnView`, features were written into a cacheline-aligned `FeatureBlock`, and CPU models like XGBoost, GBDT, rules, and scorecards all read the same feature block without per-model serialization. For the neural scoring path, we used preallocated pinned buffers, dedicated CUDA streams, and CUDA Graphs. That removed roughly 145us of launch overhead per inference, which matters when your entire p99 budget is 5ms.

The Sentinel Gateway handled admission control, priority lanes for high-value transactions, consistent routing, circuit breakers, and GPU health signals from DCGM. Declined transactions were published into an Arrow event buffer so the slower reasoning tiers could analyze the same immutable transaction context without rebuilding it. The result was sub-5ms p99 on the inline path, around 10x GPU throughput improvement with micro-batching, and a safer architecture because expensive agentic reasoning could not starve the payment authorization path."

### Architecture Diagram

```text
Card Network / E-commerce
        |
        v
+------------------------+
| Sentinel Gateway       |  Rust: admission, priority lanes, circuit breakers
| Axum/Tokio             |
+-----------+------------+
            |
            v
+------------------------+       +-------------------------+
| Tier 1 Inline Decision | ----> | Arrow Event Buffer      |
| <5ms p99               |       | mmap/shared memory      |
|                        |       +------------+------------+
| TxnView -> FeatureBlock|                    |
| XGBoost/GBDT/Rules     |                    |
| CUDA Graph neural path |                    |
+-----------+------------+                    |
            |                                 |
            v                                 v
     Go / No-Go                    +------------------------+
                                   | Tier 2 Decline Reason |
                                   | 13B fast reasoner     |
                                   +------------------------+
                                                |
                                                v
                                   +------------------------+
                                   | Tier 3 Triage Agent   |
                                   | 70B + tools + actions |
                                   +------------------------+
```

### Key Metrics

| Optimization | Before | After | Improvement |
|--------------|--------|-------|-------------|
| Inline fraud p99 | Above 5ms during GPU path spikes | Sub-5ms p99 | SLA restored |
| CUDA launch overhead | ~150us for 30-45 small launches | ~5us graph launch | ~145us saved |
| GPU inference p99 | 2.5ms | 1.1ms | 56% faster |
| E2E p99 with GPU scoring | 5.8ms | 3.8ms | Within SLA |
| GPU throughput | 1,000 req/sec/GPU | 10,000 req/sec/GPU | 10x |
| GPU SM utilization | 18% | 72% | 4x better |
| Prefix/KV locality where LLM is used | Random LB, low reuse | Consistent routing | Higher cache reuse |

### Technology Stack Map

| Technology | Used Where | Why |
|------------|------------|-----|
| Rust / Axum / Tokio | Sentinel Gateway | Async admission, streaming, routing, cancellation with low memory per connection |
| C++/CUDA | Neural scoring hot path | Deterministic low-latency GPU execution and explicit memory control |
| CUDA Graphs | Neural scorer | Removes repeated CPU launch overhead for fixed inference graph |
| Pinned memory + CUDA streams | GPU scorer | Enables async H2D/D2H transfers and overlap with compute |
| XGBoost / GBDT / rules | Tier 1 scoring | Fast explainable deterministic models for inline fraud decisioning |
| Apache Arrow | Tier boundary | Zero-copy event handoff to reasoning and audit paths |
| SPSC ring buffer | Per-core handoff | Lock-free handoff of descriptors, not payloads |
| DCGM | GPU health | Circuit breaker and admission control use GPU health and memory pressure |
| Kubernetes CPU/Topology Manager | Deployment | Keeps CPUs, memory, GPU, and NIC topology aligned |

### Top 5 Drill-Down Questions

| Question | 30-Second Answer |
|----------|------------------|
| Why split Tier 1 from Tier 2/3? | "The authorization path cannot share fate with LLM reasoning. Tier 1 makes the go/no-go decision inside 5ms; Tier 2/3 consume immutable events asynchronously for explanation and triage." |
| Why CUDA Graphs? | "The model had many tiny kernels. Kernel time was fine, but CPU dispatch gaps dominated. CUDA Graphs collapsed many launches into one replay and saved about 145us." |
| Why Arrow instead of JSON/protobuf? | "The slower tiers needed the exact same feature and score context. Arrow let us publish columnar buffers once and let consumers read by reference without rebuilding." |
| Why Rust gateway instead of Python? | "The gateway needed 50K+ concurrent streams, cancellation, admission, and predictable memory. Tokio tasks and Rust ownership were a better fit than Python in the hot control path." |
| How did you protect the 5ms SLA? | "Physical tier isolation, priority lanes, bounded queues, CPU pinning via K8s policy, CUDA Graphs, pinned buffers, and circuit breakers on degraded GPU backends." |

## Story B: LLM Serving Infrastructure / Fiserv Sentinel

### 2-Minute Version

"At Fiserv, I architected Sentinel, a multi-model LLM orchestration platform for loan processing workflows. The workload was large-context and multi-stage: credit bureau data, bank statements, tax returns, pay stubs, and employment records had to be assembled into a decision workflow fast enough for an interactive applicant experience. The baseline was around 8.5 seconds, with high p99 latency and poor GPU utilization.

I split the system into a Rust control plane and a C++/CUDA data plane. The Rust side handled intake classification, document detection, workflow routing, session stickiness, health-aware routing, and admission control. The GPU side optimized the expensive inference path using pinned memory, async DMA, CUDA streams, CUDA Graphs where the shape was stable, and KV-cache-aware routing. For LLM serving, we used vLLM for continuous batching and PagedAttention, evaluated SGLang for structured serving and prefix reuse, and used TensorRT-LLM for optimized NVIDIA deployments when model shape and hardware were stable enough.

The biggest system optimization was not a single kernel. It was preserving context locality. By routing the same session back to the same worker, we increased KV-cache hit rate to about 70%, reduced follow-up TTFT from roughly 580ms to 180ms, and cut GPU memory pressure significantly. Combined with hierarchical context assembly, token usage dropped about 60%, average latency went from 8.5s to 2.3s, p99 from 32s to 4.1s, GPU utilization reached about 85%, and concurrency scaled from 50 to 500+ applications."

### Architecture Diagram

```text
Loan Application
   |
   v
+----------------------------+
| Rust Sentinel Gateway      |
| classify, route, admit     |
+-------------+--------------+
              |
              v
+----------------------------+
| Context Assembly           |
| credit, tax, bank, paystub |
| hierarchical summaries     |
+-------------+--------------+
              |
              v
+----------------------------+       +-------------------------+
| LLM Serving Pool           | <---- | Session/KV Router      |
| vLLM / SGLang / TRT-LLM    |       | prefix + GPU headroom  |
| H100 GPU pods              |       +-------------------------+
+-------------+--------------+
              |
              v
+----------------------------+
| C++/CUDA Transfer Engine   |
| pinned memory, streams     |
| async DMA, CUDA events     |
+-------------+--------------+
              |
              v
Decision, explanation, audit envelope
```

### Key Metrics

| Optimization | Before | After | Improvement |
|--------------|--------|-------|-------------|
| Average workflow latency | 8.5s | 2.3s | 3.7x faster |
| P99 workflow latency | 32s | 4.1s | 7.8x faster |
| Token usage | 100% | 40% | 60% reduction |
| GPU utilization | 45% | 85% | 89% relative improvement |
| Concurrent applications | 50 | 500+ | 10x scale |
| KV-cache hit rate | 0-12% depending baseline | 70% | 5.8x vs round robin |
| Follow-up TTFT p99 | 580ms | 180ms | 69% faster |
| GPU memory evictions | 45/sec | 12/sec | 73% reduction |

### Technology Stack Map

| Technology | Used Where | Why |
|------------|------------|-----|
| vLLM | High-throughput LLM serving | PagedAttention and continuous batching improve GPU memory use and throughput |
| SGLang | Structured LLM serving path | Useful for structured generation, prefix reuse, and serving control |
| TensorRT-LLM | NVIDIA optimized path | Best for stable shapes and maximum GPU efficiency |
| CUDA streams + pinned memory | Transfer engine | Overlap H2D, compute, and D2H; avoid pageable-memory stalls |
| CUDA Graphs | Stable-shape decode/prefill paths | Removes launch overhead when graph capture is safe |
| KV-cache-aware routing | Gateway | Preserves prefix/session locality and reduces recompute |
| Rust Axum/Tokio | Control plane | Handles routing, SSE streaming, cancellation, and admission at high concurrency |
| OpenTelemetry/Prometheus | Observability | Correlates routing decisions, GPU pressure, and latency |

### Top 5 Drill-Down Questions

| Question | 30-Second Answer |
|----------|------------------|
| Why vLLM? | "vLLM solves the KV-cache fragmentation problem with PagedAttention and keeps the GPU busy with continuous batching." |
| When would you choose TensorRT-LLM instead? | "When the model, shape buckets, and NVIDIA deployment target are stable enough to justify engine build complexity for higher throughput." |
| Why did CUDA Graphs sometimes hurt throughput? | "Graphs need static memory and shape buckets. Too many buckets or long max sequence lengths can consume workspace and reduce KV capacity." |
| What was the biggest LLM optimization? | "KV-cache-aware routing. It avoided recomputing long shared prefixes and turned repeated sessions into cache hits." |
| How did you prevent GPU OOM? | "Admission used token budget, estimated KV growth, current GPU memory headroom, and vLLM cache metrics before accepting requests." |

## Story C: Siri-Style Conversational AI Pipeline / Apple

### 2-Minute Version

"At Apple, I worked on a Siri-style conversational AI platform where the main problem was not one model, but the end-to-end pipeline: audio capture, ASR, NLU, search/ranking, orchestration, and TTS. A naive microservice design added 50-80ms of serialization, network, and buffering overhead at every stage, so the pipeline could spend hundreds of milliseconds just moving intermediate state.

I redesigned the pipeline as a unified low-latency serving system with shared conversational state. ASR wrote partial hypotheses directly into a shared memory arena; NLU read those hypotheses without deserialization; search and orchestration wrote results back into the same session state; TTS streamed audio as soon as the response decision was available. Streaming Conformer ASR emitted partial hypotheses every 50-100ms, DistilBERT handled intent classification with roughly 97% accuracy retention versus full BERT, FAISS supported vector retrieval, and FastSpeech generated the first audio chunk within about 100ms after the response decision.

On the systems side, we used session-affine routing, bounded queues for backpressure, zero-copy gRPC streaming, distributed tracing, and Apple Silicon optimization. NEON accelerated audio feature extraction, and Metal Performance Shaders accelerated transformer layers where appropriate. The result was p99 end-to-end latency reduced from about 850ms to 280ms, stage handoff overhead reduced from 50-80ms to 5-10ms, session cache hit rate improved from 40% to 85%, and the platform maintained 99.99% availability under traffic spikes."

### Architecture Diagram

```text
Edge Device
 audio capture + partial display + playback
        |
        v  gRPC streaming, session-affine
+-----------------------------+
| Stage A: Streaming Ingress  |
+-------------+---------------+
              |
              v
+-----------------------------+
| Stage B: Conformer ASR      |
| partials every 50-100ms     |
+-------------+---------------+
              |
              v
+-----------------------------+
| Shared Conversational State |
| shm/mmap arena              |
+------+------+------+--------+
       |      |      |
       v      v      v
   NLU/BERT  Search  Orchestration
       |      |      |
       +------+------+
              |
              v
+-----------------------------+
| FastSpeech TTS              |
| first chunk ~100ms          |
+-----------------------------+
```

### Key Metrics

| Optimization | Before | After | Improvement |
|--------------|--------|-------|-------------|
| E2E p99 latency | 850ms | 280ms | 3.0x faster |
| Stage-to-stage overhead | 50-80ms | 5-10ms | 5-10x lower |
| Partial ASR updates | Batch/final-heavy | 50-100ms updates | Real-time UX |
| Session cache hit rate | 40% | 85% | 2.1x |
| Audio feature extraction | Scalar baseline | NEON optimized | 4x faster |
| Transformer acceleration | CPU-heavy baseline | Metal GPU acceleration | 2x faster |
| First TTS audio chunk | Late after full response | ~100ms after decision | Lower perceived latency |

### Technology Stack Map

| Technology | Used Where | Why |
|------------|------------|-----|
| Conformer ASR / RNN-T | Streaming ASR | Supports incremental recognition and partial hypotheses |
| DistilBERT | NLU | Faster than full BERT while retaining most accuracy |
| FAISS HNSW | Search/ranking | Low-latency vector retrieval for entities and knowledge |
| FastSpeech | TTS | Non-autoregressive TTS enables fast streaming synthesis |
| POSIX shared memory / mmap | Stage state | Avoids per-stage serialization and network copies |
| gRPC streaming | Edge/cloud transport | Backpressure-aware partial ASR and TTS streaming |
| NEON SIMD | Apple Silicon audio path | Speeds up signal processing and feature extraction |
| Metal Performance Shaders | Apple GPU path | Accelerates transformer layers with unified memory benefits |

### Top 5 Drill-Down Questions

| Question | 30-Second Answer |
|----------|------------------|
| Why shared memory between stages? | "The latency tax was state movement. Shared memory let stages coordinate through pointers instead of serializing each intermediate result." |
| Why DistilBERT? | "Intent classification needed low p99 more than marginal accuracy. DistilBERT gave about 40% speedup with roughly 97% accuracy retention." |
| How did streaming ASR reduce perceived latency? | "Partial hypotheses every 50-100ms let the user see progress while speaking and let downstream NLU begin earlier." |
| How did you prevent queue explosion? | "Every stage boundary had bounded queues, deadline propagation, and backpressure. We dropped stale partials but preserved final results." |
| Why Apple Silicon matters? | "Unified memory reduces CPU/GPU copy overhead, NEON handles audio features efficiently, and Metal accelerates transformer math." |

## Story D: Broadcom Cloud Secure Web Gateway

### 2-Minute Version

"At Broadcom/Symantec, I worked on Cloud Secure Web Gateway, a multi-tenant security platform inspecting billions of web requests across on-prem and GCP deployments. The challenge was to run multiple ML models for malware detection, URL classification, DLP, content analysis, behavioral analysis, and threat intelligence while keeping p99 inference under 100ms. The legacy architecture had six independent model pipelines, repeated serialization, inconsistent hardware behavior, and p99 latency around 350ms.

I helped consolidate the system into a unified multi-model inference platform. The key decision was to treat each request as a shared context object, not as separate JSON or protobuf payloads passed between model services. Feature extraction happened once; XGBoost, BERT, CNN, NER, rule engines, and threat-intel checks read from the same shared memory request context. Independent models ran in parallel, dependent models formed a DAG, and high-confidence clean or malicious cases exited early.

For model serving, we used custom C++ for ultra-low-latency tree models, TensorFlow Serving for deep models, ONNX Runtime for cross-framework portability, and GPU acceleration on T4/V100 where it paid off. Dynamic batching improved GPU throughput from around 1K to 10K requests/sec/GPU. We added tenant-aware routing, priority queues, memory isolation, canary rollout, Prometheus/Grafana observability, and compliance audit trails. The result was end-to-end latency reduced from about 350ms to 85ms, model deployment time reduced from 2-4 weeks to about 2 days, and 99.99% availability."

### Architecture Diagram

```text
Enterprise Traffic
  PAC/WCCP/ICAP agent
        |
        v
+----------------------------+
| Ingress + Tenant Routing   |
| rate limit, batching       |
+-------------+--------------+
              |
              v
+----------------------------+
| Shared Request Context     |
| URL, content, user, threat |
| shm/mmap, zero-copy        |
+------+------+------+-------+
       |      |      |
       v      v      v
 Malware  URL/BERT  DLP/NER
 XGB+CNN  XGB       pattern
       |      |      |
       +------+------+
              |
              v
+----------------------------+
| Decision Engine            |
| allow/block/warn/quarantine|
+----------------------------+
```

### Key Metrics

| Optimization | Before | After | Improvement |
|--------------|--------|-------|-------------|
| E2E p99 latency | 350ms | 85ms | 4.1x faster |
| Multi-model critical path | ~360ms sequential | ~120ms DAG/parallel | 3x faster |
| Serialization overhead | ~200ms across stages | Near-zero shared context | Removed hot-path tax |
| GPU throughput | 1K req/sec/GPU | 10K req/sec/GPU | 10x |
| Model deployment | 2-4 weeks | 2 days | 10-20x faster |
| URL classification | p99 above target | <15ms p99 | SLA met |
| Malware detection | CPU-only bottleneck | XGBoost fast path + CNN deep path | 95% fast exit |

### Technology Stack Map

| Technology | Used Where | Why |
|------------|------------|-----|
| XGBoost / GBRT | Malware and URL fast paths | Fast, explainable, CPU-friendly filtering |
| CNN | Suspicious malware deep analysis | Better accuracy for high-risk payloads |
| BERT / DistilBERT | URL/content NLP | Semantic classification beyond lexical rules |
| NER + pattern matching | DLP | High recall patterns plus contextual precision |
| TensorFlow Serving | Deep model serving | Mature serving path for TensorFlow models |
| ONNX Runtime | Cross-framework serving | Portable model execution across CPU/GPU |
| Custom C++ inference | Tree/rule hot path | Sub-millisecond latency and explicit memory layout |
| eBPF/XDP | Networking/security path | Low-overhead packet/request visibility and filtering |
| Kafka/Flink/RocksDB/Redis | Streaming features and state | Real-time feature computation and tenant/session state |

### Top 5 Drill-Down Questions

| Question | 30-Second Answer |
|----------|------------------|
| Why a shared request context? | "Six models needed overlapping features. Extracting once and sharing pointers removed repeated serialization and duplicate parsing." |
| Why XGBoost before CNN? | "XGBoost filters 95% of traffic in under 5ms; CNN is reserved for suspicious cases where accuracy is worth the GPU cost." |
| How did you handle hybrid on-prem/GCP differences? | "A hardware abstraction layer detected CPU/GPU capability and routed models to AVX/CUDA/XLA-optimized paths." |
| Why dynamic batching? | "Single requests underutilized GPUs. Batching up to latency windows improved throughput 10x while preserving p99 budgets." |
| How did you deploy safely? | "Canary 1% for 24h, phased rollout 10/25/50/100, automated rollback on p99/error/regression signals." |

---

# Section 2: Technology Decision Matrix

| Technology | Story | Why Chosen | Without It | Key Metric | Alternative Rejected | 30-Second Explanation |
|------------|-------|------------|------------|------------|----------------------|-----------------------|
| Rust | A/B | Predictable memory, async concurrency, safe control plane | Python/GIL or GC pressure would add jitter | 50K+ async connections in gateway path | Python Flask for hot gateway | "Rust gave us C-like predictability with safe concurrency for routing and admission." |
| Axum/Tokio | A/B | High-concurrency async HTTP/SSE gateway | Thread-per-connection memory blowup | ~8KB/task vs MB-scale threads | Flask/FastAPI for hot path | "Tokio let each request be a cheap task with cancellation and timeouts." |
| C++17/20 | A/C/D | Explicit memory layout and low-latency model/runtime code | Harder to control cache, NUMA, ABI, and allocator behavior | Sub-ms feature/model paths | Python for hot path | "C++ was used where layout, latency, and ABI stability mattered." |
| CUDA | A/B/D | GPU acceleration for neural scoring, CNN, LLM serving | CPU-only path misses throughput/latency goals | 10x req/sec/GPU with batching | CPU-only inference | "CUDA let us use GPU parallelism but only after fixing launch and transfer overhead." |
| CUDA Graphs | A/B | Remove repeated launch overhead for stable graphs | 30-45 tiny launches add ~150us | 2.5ms -> 1.1ms GPU p99 in A | Eager launch | "Graphs are best when the execution shape is stable and launch overhead dominates." |
| CUDA Streams | A/B | Overlap H2D, compute, D2H | GPU waits for data transfers | GPU utilization 35% -> 85% in B path | Single default stream | "Streams turned serial copy/compute/copy into a pipeline." |
| Pinned Memory | A/B | Enables real async DMA and lower H2D latency | Pageable staging doubles copy cost | 3.2x faster H2D in B | Pageable host buffers | "Pinned buffers are table stakes for serious GPU serving." |
| vLLM | B | PagedAttention + continuous batching | KV fragmentation and lower concurrency | 70% KV hit with routing; 500+ apps | HuggingFace eager serving | "vLLM is the serving engine when KV-cache efficiency dominates." |
| PagedAttention | B | Paged KV-cache blocks avoid contiguous-allocation waste | KV fragmentation causes early OOM and lower concurrency | KV-cache hit/locality to ~70% with routing | Naive contiguous KV cache | "PagedAttention treats KV memory like virtual memory, so long and short requests can coexist efficiently." |
| Continuous batching | B | Keeps decode GPU work full as requests arrive and finish | Static batches leave bubbles or add wait time | GPU utilization 45% -> 85% | Offline fixed batching | "Continuous batching is the scheduler trick that makes online LLM serving efficient." |
| Prefix caching | B | Reuses shared system-prompt KV across related requests | Recomputes common prefixes every time | Prefix hit ~5% -> ~72% with consistent routing in draft Sentinel | Round-robin routing | "Prefix caching only works if the router preserves locality." |
| SGLang | B | Structured LLM serving and prefix reuse | Harder to control structured generation | Better serving control | Generic Python orchestration | "SGLang is useful when generation structure and prefix reuse are first-class." |
| TensorRT-LLM | B | NVIDIA-optimized LLM engine | Lower GPU efficiency on stable deployments | 3-4x class speedups in optimized paths | vLLM only | "TensorRT-LLM pays off when model and shape buckets are stable." |
| TensorRT | B/D | Fuses guardrail/classifier graphs into fewer kernels | Tiny eager kernels add launch gaps | Guardrail kernels 47 -> 3 in draft pipeline | PyTorch eager classifiers | "TensorRT is the compiler path for stable neural classifiers and guardrails." |
| Triton Inference Server | B/D | Multi-framework model serving and ensembles | More custom serving code | Built-in metrics and model orchestration | Hand-rolled model server | "Triton is the platform answer for multi-model serving; custom engines remain for ultra-low latency." |
| OpenAI Triton DSL | B | Fast custom tiled kernels for attention/post-processing experiments | CUDA C++ iteration is slower for ML engineers | Useful for paged attention and fused kernels | Handwritten CUDA for every custom op | "Triton DSL is for kernels; NVIDIA Triton Inference Server is for serving." |
| cuBLAS/cuBLASLt | A/B/D | Production GEMM baseline for dense layers | Custom GEMM likely underperforms | Baseline for Tensor Core paths | Handwritten GEMM first | "I start with cuBLAS/cuBLASLt before custom kernels because GEMM is already deeply optimized." |
| CUTLASS | B | Custom GEMM/fusion layouts when libraries are close but not exact | Hard to fuse or customize with only cuBLAS | Used as building block for optimized kernels | From-scratch WMMA | "CUTLASS is the template library I reach for between cuBLAS and raw CUDA." |
| Thrust | A/D | Quick parallel primitives like sort, scan, reduce | Reinventing primitives wastes time and adds bugs | Faster implementation for non-hot-path GPU utilities | Custom CUDA for every primitive | "Thrust is useful until profiling proves the primitive itself is the bottleneck." |
| FlashAttention | B | IO-aware attention reduces HBM traffic | Attention becomes memory-bound on long prompts | Long-context prefill faster; lower memory traffic | Standard attention | "FlashAttention matters because attention performance is often memory movement, not math." |
| Speculative decoding | B | Draft model proposes tokens, target model verifies | Decode remains serial and target-model bound | Higher tokens/sec when acceptance rate is high | Pure target-model decode | "Speculative decoding helps when a small model can accurately predict the big model's next tokens." |
| Chunked prefill | B | Breaks long prefill into smaller scheduling units | New long prompts block decode traffic | Lower TTFT spikes for long prompts | Monolithic prefill | "Chunked prefill protects interactive decode from one giant prompt." |
| NCCL AllReduce | B | Tensor parallel LLM shards need GPU-to-GPU collectives | TP scaling stalls on synchronization | TP=4 improves only if topology is aligned | Single-GPU only | "NCCL is not optional once tensor parallelism crosses GPUs." |
| NVLink/NVSwitch | B | High-bandwidth GPU interconnect for tensor parallelism | PCIe/SYS paths cap scaling | TP scaling closer to linear | Random GPU placement | "If AllReduce crosses the wrong fabric, the model is waiting on the network, not compute." |
| MIG | A/B | Hard GPU partitioning for smaller inference workloads | Tenants interfere at full-GPU granularity | Better isolation where model size fits | Time-slicing only | "MIG is a capacity/isolation tool, not a free performance win." |
| ONNX Runtime | D | Portable CPU/GPU model execution | Framework lock-in | <5ms class serving; portable deployment | TensorFlow-only serving | "ONNX was the portability layer across environments and model teams." |
| TensorFlow Serving | D | Mature serving for TensorFlow CNN/BERT models | Reinvent deep model serving | Stable deep serving path | Custom C++ for all models | "It handled mature DL serving; we reserved C++ for hot tree/rule paths." |
| XGBoost/GBDT | A/D | Fast, explainable, CPU-friendly risk scoring | LLM/neural path too slow for inline decisions | <5ms fast filter; <100us/model in A | Neural-only model | "Tree models are still ideal for regulated low-latency decisioning." |
| BERT/DistilBERT | C/D | Semantic intent/URL/content classification | Lexical rules miss semantics | 97% retention with faster DistilBERT | Full BERT everywhere | "DistilBERT was the latency/accuracy tradeoff for p99-sensitive NLU." |
| Conformer ASR | C | Streaming speech recognition | Final-only ASR feels slow | Partial updates every 50-100ms | Batch ASR | "Conformer gave streaming accuracy while preserving incremental state." |
| FastSpeech | C | Fast non-autoregressive TTS | Slow first audio | First chunk ~100ms | Autoregressive TTS only | "FastSpeech reduced perceived latency by emitting speech quickly." |
| FAISS/HNSW | C/D | Low-latency vector retrieval | Search/ranking blocks pipeline | Entity/search lookup within budget | Brute force vector scan | "Approximate nearest neighbor search made semantic retrieval fit the conversation SLA." |
| LambdaMART | C | Rank search/action candidates with feature-based learning-to-rank | Vector similarity alone ignores context/business signals | Search + ranking 200ms -> 65ms in C | Pure embedding similarity | "ANN retrieves candidates; LambdaMART ranks them with context." |
| RE2 | D | Safe fast regex for DLP pattern matching | Backtracking regex risks latency spikes | Pattern layer <5ms | PCRE-style backtracking on hot path | "For DLP, regex must be predictable, not just expressive." |
| Apache Arrow | A/B/D | Zero-copy columnar event and audit boundary | JSON/protobuf rebuild tax | Serialization overhead 3.1ms -> 0 in draft Sentinel | JSON/protobuf/Kafka payloads for hot path | "Arrow made event data readable by multiple consumers without conversion." |
| DLPack / CUDA IPC | B | Share GPU tensors across stages with minimal copies | CPU bounce between GPU stages | Avoids extra GPU->CPU->GPU movement | Host serialization between stages | "When tensors already live on GPU, DLPack/CUDA IPC is the handoff format." |
| RAPIDS/cuDF | B/D | GPU-side dataframe/Arrow processing | CPU dataframe stage breaks GPU-resident pipeline | Useful for GPU-side feature transforms | Pandas in hot path | "cuDF keeps tabular transforms near GPU inference when the data is already there." |
| Velox/Spark | A/D | Downstream analytics consume Arrow/columnar outputs | Audit/retraining needs format conversion | Faster analytics and retraining pipeline | JSON logs only | "The hot path publishes Arrow so analytics engines can consume the same data shape." |
| POSIX shm/mmap | A/C/D | Shared state without network serialization | 50-80ms per-stage overhead | C stage overhead 50-80ms -> 5-10ms | Service-per-stage copies | "Shared memory turns pipeline handoff into pointer access." |
| SPSC ring buffer | A | Lock-free per-core handoff | Mutex queues add contention | 0.8ms -> <0.1us inter-stage in draft Sentinel | MPSC/mutex queue | "SPSC works when ownership is designed up front: one producer, one consumer." |
| NUMA arena | A/B | Keep allocation local to CPU/GPU/NIC | Remote memory adds jitter | Cross-NUMA accesses ~2800 -> 0 in draft Sentinel | General heap allocation | "NUMA locality is invisible until p99 explodes." |
| NEON SIMD | C | Apple Silicon audio feature acceleration | CPU audio preprocessing bottleneck | 4x audio feature speedup | Scalar C++ | "NEON handled the tight DSP loops close to hardware." |
| AVX2/AVX-512 | D | CPU inference and feature extraction acceleration | CPU model path misses latency | 2-4x class CPU vector speedups | Scalar CPU path | "Vectorization mattered for hot feature and tree/model loops." |
| gRPC streaming | C/D | Backpressure-aware bidirectional streaming | Polling or unary APIs add buffering | Partial ASR visible in 50-100ms | REST polling | "Streaming RPC matched the shape of speech and partial results." |
| SSE streaming | A/B | Token streaming from LLM gateway | Buffering hides progress and wastes GPU on disconnect | Immediate cancellation on disconnect | Full buffered response | "SSE was simple, browser-friendly, and easy to cancel." |
| Circuit breaker | A/B | Fail fast around degraded backends | Cascading timeouts | Recovery <30 sec in draft gateway | Retries only | "Retries amplify failure; circuit breakers isolate it." |
| Admission control | A/B | Protect GPU/KV budget and p99 | OOM and queue collapse | Prevents KV OOM and decline spikes | Accept-all queue | "Admission says no early so the system does not fail late." |
| Consistent hashing | A/B/C | Session/KV/cache locality | Random LB destroys cache | KV hit 12% -> 70%; session cache 40% -> 85% | Round-robin | "The cache only helps if related requests land on the same worker." |
| Kubernetes CPU Manager | A/B | Exclusive CPUs for latency pods | CFS sharing and throttling jitter | p99 stability | Default CPU policy | "Static CPU Manager turns CPU requests into real pinned cores." |
| Topology Manager | A/B | Align CPU, memory, GPU, NIC | Cross-NUMA transfers | Avoids remote GPU/NIC paths | Random placement | "Topology Manager is Kubernetes' NUMA contract." |
| Memory Manager | A/B | NUMA-aware memory allocation for Guaranteed pods | CPU and memory can land on different NUMA nodes | Lower p99 jitter | Default memory placement | "CPU pinning is incomplete if memory still comes from the wrong socket." |
| GPU Operator / Device Plugin | A/B | Declarative GPU scheduling | Manual device mounts and drift | Reliable GPU allocation | Manual `/dev/nvidia*` mapping | "The plugin makes GPU assignment a schedulable resource." |
| SR-IOV/RDMA | A/B | Low-latency NIC access for data plane | CNI overhead and kernel stack tax | 5-10us class network path | Standard overlay CNI | "Use only where latency or throughput justifies operational cost." |
| Cilium/eBPF | A/B/D | Lower-overhead K8s networking and visibility | iptables/conntrack overhead | Lower pod network latency | Flannel/iptables path | "Cilium is the pragmatic middle ground before SR-IOV." |
| DPDK | A/D | Full userspace networking for extreme data-plane paths | Kernel network stack adds latency | Redis RTT 2.1ms -> 0.4ms in draft eval | Standard sockets | "DPDK is powerful but operationally expensive, so I only use it when the latency budget demands it." |
| AF_XDP | A/D | eBPF/XDP socket fast path with less operational cost than DPDK | Standard TCP path remains too slow | Redis RTT ~0.7ms in draft eval | Full DPDK | "AF_XDP was the pragmatic compromise: much faster than TCP, simpler than DPDK." |
| Redis/RocksDB | A/D | Feature/session/cache state | Recompute hot features | ms-level feature retrieval | Remote DB per request | "Feature stores only work for low latency if hot features are local or cached." |
| Kafka/Flink | D | Streaming feature and audit pipelines | Batch-only features stale | Real-time security context | Cron/batch ETL | "Security models need fresh signals, not yesterday's features." |
| NVMe model cache | B | Local model cache avoids slow object-store cold starts | Pod restart reloads take minutes | Model load ~5 min -> ~15 sec | Remote object store only | "On bare metal, local NVMe is part of the serving design, not just storage." |
| GPUDirect Storage | B | Direct NVMe-to-GPU model loading where supported | CPU bounce slows large model loads | 70B load ~15 sec -> ~6 sec in draft | NVMe -> CPU RAM -> GPU | "GDS is useful when model-load time is operationally visible." |
| Prometheus/Grafana | A/B/C/D | Metrics, p99, SLO dashboards | Blind operations | Debug time down 50% in B | Logs only | "Metrics tell me what changed and whether the fix held." |
| DCGM/NVML | A/B/D | GPU health, memory, utilization, XID/ECC signals | Admission and circuit breakers lack GPU truth | Proactive GPU rejection before OOM | `nvidia-smi` scraping only | "DCGM/NVML turns GPU state into routing and admission signals." |
| OpenTelemetry/Jaeger | A/B/C/D | Cross-stage traces | Cannot localize p99 | Faster bottleneck isolation | Per-service logs | "Tracing tells whether latency is network, queue, model, or downstream." |
| Nsight Systems | A/B/D | Timeline view of CPU gaps, CUDA launches, copies, NCCL | Kernel-level tuning starts in the wrong place | Identified launch/copy/AllReduce stalls | `nvidia-smi` only | "Nsight Systems answers: why is the GPU waiting?" |
| Nsight Compute | A/B/D | Kernel metrics: SOL, memory workload, warp stalls | Cannot tell why a specific kernel is slow | Decode memory-bound finding: DRAM 82%, SM 18% | Timeline-only analysis | "Nsight Compute answers: why is this kernel slow?" |
| PyTorch Profiler | B | Framework-level operator and CPU/GPU attribution | Hard to connect Python ops to CUDA kernels | Finds tokenization/post-processing overhead | CUDA tools only | "I use PyTorch Profiler before Nsight when the issue may be framework overhead." |

---

# Section 3: System Layers Reference

## Layer 5: Application

| Technology | Story | Explanation and Snippet | Cross-References |
|------------|-------|-------------------------|------------------|
| Fraud tiering | A | Separates inline authorization from slow reasoning. `Tier 1 <5ms`, `Tier 2 2-5s`, `Tier 3 5-10s`; the interview point is isolation of latency domains. | Runtime L4, Platform L3 |
| Loan context assembly | B | Hierarchical summarization reduces large-context token load: credit, tax, bank, employment docs become targeted context. | LLM inference in Section 4 |
| Conversational pipeline | C | ASR -> NLU -> Search -> Orchestration -> TTS is treated as one stateful pipeline, not separate APIs. | Shared memory in Section 5 |
| Security model DAG | D | Malware, URL, DLP, content, behavior, and threat-intel models run in parallel where independent and sequential where dependent. | Runtime serving below |

## Layer 4: Runtime

| Technology | Story | Explanation and Snippet | Interacts With |
|------------|-------|-------------------------|----------------|
| vLLM/PagedAttention | B | Used when KV-cache fragmentation limits LLM concurrency. Config is tied to GPU memory budget and block size. | GPU memory, K8s GPU scheduling |
| TensorRT-LLM | B | Used for stable NVIDIA deployments where engine optimization beats runtime flexibility. | CUDA Graphs, tensor parallelism |
| CUDA Graphs | A/B | Captures stable H2D -> kernels -> D2H sequence once, replays with one graph launch. | Code L1, GPU Section 4 |
| TensorFlow Serving/ONNX Runtime | D | Handles deep models and portable deployment while custom C++ handles the hottest tree/rule path. | Platform deployment |
| Metal Performance Shaders | C | Accelerates transformer layers on Apple Silicon and benefits from unified memory. | NEON, shared state |

```rust
// Story A: graph replay path, condensed from the fraud GPU scorer.
unsafe {
    cudaGraphLaunch(self.graph_exec, self.stream);
}
self.stream.synchronize();
```

## Layer 3: Platform

| Technology | Story | Explanation and Snippet | Interacts With |
|------------|-------|-------------------------|----------------|
| Kubernetes CPU Manager | A/B | Gives Guaranteed pods exclusive CPUs when requests equal limits and CPU is integer. | Host CPU isolation |
| Topology Manager | A/B | Aligns CPU, memory, GPU, and NIC on one NUMA node for latency-sensitive pods. | Host NUMA discovery |
| GPU Device Plugin | A/B | Exposes GPUs/MIG devices as schedulable resources. | NVIDIA driver, DCGM |
| PriorityClass/PDB | A/B/D | Protects critical inference pods during disruption. | Reliability runbooks |
| Cilium/SR-IOV/RDMA | A/B/D | Selects network path based on latency/isolation tradeoff. | Host NIC tuning |

```yaml
# Story A/B: latency-sensitive pod policy sketch.
resources:
  requests:
    cpu: "8"
    memory: "32Gi"
    hugepages-2Mi: "4Gi"
    nvidia.com/gpu: "1"
  limits:
    cpu: "8"
    memory: "32Gi"
    hugepages-2Mi: "4Gi"
    nvidia.com/gpu: "1"
```

## Layer 2: Host

| Technology | Story | Explanation and Snippet | Interacts With |
|------------|-------|-------------------------|----------------|
| `isolcpus`, `nohz_full`, `rcu_nocbs` | A/B | Makes performance cores quiet before K8s allocates them. | CPU Manager |
| Huge pages | A/B/C | Reduces TLB pressure for large model/state buffers. | Pod hugepage requests |
| NUMA topology | A/B | Discover on host, enforce for pods via K8s Topology Manager. | CUDA pinned buffers |
| IRQ affinity | A/B/D | Keeps NIC interrupts off performance CPUs. | Cilium/SR-IOV |
| GPUDirect Storage/NVMe/mmap | B | Reduces model load and cache spill latency. | Storage PVs |

```bash
# Host prepares capacity; K8s allocates it.
GRUB_CMDLINE_LINUX="isolcpus=4-31 nohz_full=4-31 rcu_nocbs=4-31 irqaffinity=0-3"
```

## Layer 1: Code

| Pattern | Story | Why It Exists | Snippet |
|---------|-------|---------------|---------|
| Cacheline-aligned feature block | A | One feature layout feeds many models without conversion. | `#[repr(C, align(64))] struct FeatureBlock { ... }` |
| SPSC ring | A | Descriptor handoff without locks. | See Section 5.1 |
| Shared conversational state | C | Stages share state without serialization. | See Section 5.1 |
| Request context | D | Six security models share extracted features. | See Section 5.1 |
| Circuit breaker | A/B | Isolate degraded backends. | See Section 5.2 |

## 3.6 Layer Inventory Audit

Use this as the "no orphan technology" map. If an interviewer asks about any item, anchor it back to the story first, then drill down into the layer.

| Layer | Technologies | Story Mapping | Config / Code Anchor |
|-------|--------------|---------------|----------------------|
| **Layer 5: Application** | Fraud tiering, loan context assembly, conversational ASR->NLU->Search->TTS, security model DAG, DLP, malware detection, URL classification, threat intelligence | A/B/C/D | Section 1 diagrams and metrics |
| **Layer 4: Runtime** | vLLM, PagedAttention, continuous batching, SGLang, TensorRT-LLM, TensorRT, Triton Inference Server, ONNX Runtime, TensorFlow Serving, FlashAttention, speculative decoding, chunked prefill, prefix caching, Metal Performance Shaders | B/C/D | Sections 2 and 4.4-4.8 |
| **Layer 3: Platform** | Kubernetes CPU Manager, Topology Manager, Memory Manager, GPU Operator, NVIDIA device plugin, PriorityClass, PDB, HPA/KEDA, Cilium/eBPF, SR-IOV Network Operator, RDMA/RoCE, local PVs | A/B/D | Section 6.2 and 6.5 |
| **Layer 2: Host** | BIOS performance mode, CPU governor, `isolcpus`, `nohz_full`, `rcu_nocbs`, IRQ affinity, hugepages, NUMA topology, `vm.max_map_count`, locked memory, NVMe, GPUDirect Storage, NVIDIA drivers, DCGM/NVML | A/B/C/D | Section 6.1 and 6.5 |
| **Layer 1: Code** | C++ NUMA arena, SIMD AVX2/AVX-512/NEON, SPSC queue, POSIX shm/mmap, Arrow IPC, Rust FFI, consistent hashing, circuit breaker, admission control, SSE cancellation, `memmap2`, RE2, LambdaMART ranking | A/B/C/D | Section 5 |
| **Observability Cross-Cut** | Prometheus, Grafana, OpenTelemetry, Jaeger, Nsight Systems, Nsight Compute, PyTorch Profiler, DCGM exporter, vLLM metrics | A/B/C/D | Sections 4.5 and 4.9 |

---

# Section 4: GPU & CUDA Reference

## 4.1 GPU Architecture Comparison

| Generation | Example GPUs | Story Context | Interview Explanation |
|------------|--------------|---------------|----------------------|
| Ampere | A100 | A/B | Strong tensor cores, MIG, good for micro-batched inference and CUDA Graphs |
| Ada | L4/L40S | B/D | Cost-efficient inference GPUs for serving and media/security workloads |
| Hopper | H100/H200 | A/B | FP8 transformer engine, stronger memory bandwidth, high-end LLM serving |
| Blackwell | B200/GB200 | B | Next-gen LLM serving focus: higher memory bandwidth, lower precision, larger models |
| Rubin | Future | B | Roadmap awareness: plan abstractions so runtimes can evolve without app rewrite |

## 4.2 CUDA Execution Hierarchy

```text
Thread -> Warp (32 threads) -> Block -> Grid
       scheduled on Streaming Multiprocessors (SMs)
```

Interview answer: "For LLM serving, I usually do not start by writing custom kernels. I use the hierarchy to interpret Nsight results: warp stalls, occupancy, memory coalescing, launch overhead, and whether the GPU is idle because the CPU or network is feeding it too slowly."

## 4.3 Precision Formats

| Format | Story | Use Case | Risk |
|--------|-------|----------|------|
| FP32 | A/B/C/D | Baseline accuracy and numerically sensitive ops | Too slow/large for serving |
| FP16 | A/B/D | General GPU inference | Can underflow in sensitive reductions |
| BF16 | B | LLM attention and safer mixed precision | Slightly larger/less throughput than FP8 |
| FP8 | B | H100/H200 transformer serving | Accuracy loss on long prompts if attention/KV not handled carefully |
| FP4 | B | Emerging low-precision LLM inference | Needs careful calibration and hardware/runtime support |
| INT8 | B/D | Quantized classification and TensorRT paths | Calibration errors and accuracy drift |

## 4.4 LLM Inference Cycle

| Concept | Story | Explanation |
|---------|-------|-------------|
| Prefill | B | Processes the prompt; compute-heavy and parallel. |
| Decode | B | Generates one token at a time; latency-sensitive and memory/KV-cache bound. |
| Q/K/V | B/C | Query attends over Key/Value history; K/V are cached to avoid recomputing past tokens or frames. |
| KV cache | B | Stores prior token state; routing and memory policy determine TTFT and OOM behavior. |
| MHA vs GQA | B | Grouped Query Attention reduces KV memory by sharing K/V heads across query heads. |

## 4.5 Profiling Guide

```text
1. Nsight Systems first: find gaps, copies, synchronization, CPU launch overhead.
2. Nsight Compute second: inspect specific kernels only after the timeline identifies them.
3. DCGM/Grafana always: watch memory pressure, utilization, clocks, ECC/XID.
```

| Symptom | First Tool | Likely Root Cause | Fix |
|---------|------------|-------------------|-----|
| GPU idle gaps | Nsight Systems | CPU launch or input pipeline bottleneck | CUDA Graphs, streams, batching |
| Low SM utilization | Nsight Systems/DCGM | Batch too small | Micro-batching |
| H2D copy blocks compute | Nsight Systems | Pageable memory or same stream | Pinned buffers, async copies, stream pipeline |
| Poor TP scaling | Nsight Systems + `nvidia-smi topo -m` | AllReduce crossing PCIe/SYS | Better GPU placement, TP topology, overlap |
| Long prompts become nonsense under FP8 | Eval + Nsight | Quantization sensitivity in attention/KV | Mixed precision: FP8 weights, BF16 attention/KV |

## 4.6 CUDA Graphs

Use CUDA Graphs when:
- Shape buckets are stable.
- The same kernel sequence repeats many times.
- Launch overhead is visible in Nsight Systems.

Avoid or tune carefully when:
- Sequence lengths vary wildly.
- Graph workspace reduces KV-cache capacity.
- Dynamic allocation occurs inside capture.

```cpp
// Story A/B pattern: capture once, replay many.
cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal);
cudaMemcpyAsync(d_in, h_in, bytes, cudaMemcpyHostToDevice, stream);
model_forward(d_in, d_out, stream);
cudaMemcpyAsync(h_out, d_out, bytes_out, cudaMemcpyDeviceToHost, stream);
cudaStreamEndCapture(stream, &graph);
cudaGraphInstantiate(&graph_exec, graph, nullptr, nullptr, 0);
cudaGraphLaunch(graph_exec, stream);
```

## 4.7 Triton vs CUDA vs Thrust vs CUTLASS

| Tool | Story | Best For | Not Best For |
|------|-------|----------|--------------|
| CUDA C++ | A/B/D | Maximum control, custom kernels, graph/stream integration | Fast iteration by model engineers |
| OpenAI Triton DSL | B | Custom tiled kernels with Python-like workflow | Full application/runtime control |
| Thrust | A/D | Parallel primitives, sort/reduce/scan quickly | Hand-tuned hot kernels |
| CUTLASS | B | GEMM/attention-like building blocks | Non-matrix irregular logic |
| NVIDIA Triton Inference Server | B/D | Model serving platform | Writing kernels; not the same as Triton DSL |

## 4.8 Eight Practical CUDA/LLM Serving Tasks

| Task | Diagnosis | Fix |
|------|-----------|-----|
| Low GPU utilization despite high load | CPU/tokenization or launch gaps in Nsight Systems | Move CPU work out, batch, graph, pipeline |
| CUDA Graphs reduce throughput | Workspace eats KV memory | Reduce graph buckets/max seq, selective eager |
| Decode latency 2x expected | Prefill blocks decode | Separate prefill/decode, scheduler policy |
| TP=4 gives only 2.1x | AllReduce crosses bad topology | Align GPUs/NVLink, adjust TP groups |
| FP8 gives nonsense on long prompts | Attention/KV too sensitive | Mixed precision: BF16 attention/KV |
| P99 >> P50 | Prefill/decode interference or queueing | Priority scheduler, bounded queues |
| KV OOM too early | Fragmentation/block policy | PagedAttention config, FP8 KV, admission |
| TensorRT-LLM graph rebuilds | Variable shapes | Shape bucketing, route by length |

## 4.9 90% Performance Troubleshooting Playbook

Interview framing: "I do not start with every tool. I first classify the symptom by resource and layer, then use the smallest tool set that can separate host, Kubernetes, runtime, and application causes."

| Symptom Class | Host Side: First Tools | K8s Side: First Tools | Application/Runtime: First Tools | Common Root Causes | Fix Pattern |
|---------------|------------------------|------------------------|----------------------------------|--------------------|-------------|
| **Compute / CPU / Threads** | `top`, `pidstat`, `perf top`, `mpstat` | `kubectl top pod/node`, `kubectl describe pod`, CPU throttling metrics | PyTorch Profiler, app pprof/flamegraph, request traces | CPU throttling, tokenization bottleneck, lock contention, wrong core placement | Guaranteed QoS, CPU Manager static, remove Python hot-path work, pin hot services through K8s policies |
| **Memory / NUMA / Allocator** | `free`, `vmstat`, `numastat`, `perf stat`, page-fault counters | Pod OOM events, memory limits, hugepage requests, Memory Manager policy | Runtime heap metrics, allocator stats, KV-cache metrics | Remote NUMA memory, page faults, TLB misses, KV cache pressure, OOMKilled pods | Hugepages, preallocation, arena allocators, K8s Memory Manager, admission control |
| **Network** | `ss`, `sar -n`, `ethtool -S`, NIC IRQ counters | Cilium/Hubble flows, pod events, NetworkAttachmentDefinition for SR-IOV | OpenTelemetry spans, gateway logs, retry/circuit metrics | CNI overhead, bad NIC NUMA locality, retries, backend queueing | Cilium for default path, SR-IOV for critical path, IRQ affinity on host, circuit breakers |
| **Storage / Model Loading** | `iostat`, `nvme`, filesystem latency metrics | PVC events, local PV placement, pod restart timings | Model loader logs, mmap page-fault timing | Remote object-store cold load, slow PV, page-cache misses | Local NVMe cache, mmap model loading, pre-warm, GPUDirect Storage when model load is visible |
| **GPU / CUDA** | `nvidia-smi topo -m`, DCGM/NVML, PCIe/NVLink topology | GPU device plugin allocation, pod resource requests, topology hints | Nsight Systems, Nsight Compute, vLLM metrics | Launch overhead, pageable copies, low batch size, NCCL over wrong path, graph workspace consuming KV | Pinned buffers, CUDA streams, CUDA Graphs, topology-aware placement, shape buckets |
| **Tail Latency / p99** | `perf sched`, IRQ counters, CPU governor | Pod evictions, PDB disruptions, HPA churn, throttling | Distributed traces, queue metrics, p50/p99 split by stage | Queue buildup, noisy neighbor, prefill blocking decode, retries, GC/GIL | Bounded queues, PriorityClass, separate prefill/decode policy, admission, deadline propagation |

Minimal day-to-day tool set:
- **Host:** `top`, `pidstat`, `perf`, `numastat`, `iostat`, `ethtool`, `nvidia-smi topo -m`.
- **K8s:** `kubectl top`, `kubectl describe`, pod events, Prometheus/Grafana, Cilium/Hubble when networking is involved.
- **GPU/runtime:** DCGM/NVML, Nsight Systems first, Nsight Compute second, PyTorch Profiler when framework overhead is suspected, vLLM/TensorRT-LLM runtime metrics.
- **Application:** OpenTelemetry/Jaeger traces, structured logs with request IDs, queue-depth metrics, admission/circuit-breaker metrics.

The rule I would say in an interview: "Nsight Systems before Nsight Compute; traces before code guesses; K8s events before blaming the app; DCGM before assuming the GPU is healthy."

---

# Section 5: Systems Programming Reference

## 5.1 C++ Patterns

### NUMA Arena / Shared State

**Stories:** A, C, D.

Why it exists: allocate hot request state once, keep it local to the CPU/GPU/NIC NUMA node, and let models or stages read by pointer. In Story A, this removed remote memory jitter from the fraud hot path; in Stories C/D, the same idea appears as shared conversational/request state.

```cpp
// Story A: NUMA-pinned bump allocator for hot request data.
class NumaArena {
public:
    NumaArena(int numa_node, size_t arena_size)
        : numa_node_(numa_node), arena_size_(arena_size), offset_(0) {
        base_ = static_cast<uint8_t*>(numa_alloc_onnode(arena_size_, numa_node_));
        if (!base_) throw std::bad_alloc();
        madvise(base_, arena_size_, MADV_HUGEPAGE);
        for (size_t i = 0; i < arena_size_; i += 4096) base_[i] = 0;
    }

    void* allocate(size_t size) {
        size = (size + 63) & ~63; // cacheline alignment
        size_t current = offset_.load(std::memory_order_relaxed);
        if (current + size > arena_size_) return nullptr;
        offset_.store(current + size, std::memory_order_relaxed);
        return base_ + current;
    }

    void reset() { offset_.store(0, std::memory_order_relaxed); }

private:
    uint8_t* base_;
    int numa_node_;
    size_t arena_size_;
    std::atomic<size_t> offset_;
};
```

Performance impact: Story A's draft baseline moved from **18.4ms p99 to 4.1ms p99**, memory allocations per request dropped from **~340 to 0**, and cross-NUMA accesses dropped from **~2,800 to 0**. See Section 6.5 for why Kubernetes should enforce NUMA placement for pods instead of running application pods with ad hoc `numactl`.

### SIMD: AVX2/AVX-512 and NEON

**Stories:** C, D.

Why it exists: CPU hot loops are often feature extraction, audio preprocessing, or tree-model traversal. SIMD makes those loops predictable and cheap.

```cpp
// Story C: NEON vectorized audio feature extraction.
float32x4_t sum = vdupq_n_f32(0);
for (size_t i = 0; i < audio.size(); i += 4) {
    float32x4_t samples = vld1q_f32(audio.data() + i);
    sum = vaddq_f32(sum, samples);
}
```

Performance impact: Story C used NEON for roughly **4x faster** audio feature extraction, while Story D used AVX2/AVX-512-style vectorization to keep URL/content feature extraction inside the security gateway latency budget.

### SPSC Queue

**Story:** A.

Why it exists: one producer and one consumer can communicate without locks if ownership is strict.

```cpp
// Story A: pointer handoff between parse -> score -> GPU stages.
template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0);

public:
    bool try_push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (Capacity - 1);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        item = slots_[tail];
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) T slots_[Capacity];
};
```

Performance impact: Story A reduced inter-stage communication from **0.8ms with mutex queues to <0.1us** with SPSC pointer handoff.

### POSIX Shared Memory / mmap

**Stories:** A, C, D.

Why it exists: Rust, C++, and multiple stage processes need the same bytes without serialization.

```cpp
int fd = shm_open("/sentinel_hotpath", O_CREAT | O_RDWR, 0660);
ftruncate(fd, region_size);
void* ptr = mmap(nullptr, region_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
madvise(ptr, region_size, MADV_HUGEPAGE);
```

Performance impact: Story C reduced stage-to-stage overhead from **50-80ms to 5-10ms**; Story D removed repeated JSON/protobuf serialization across six security models.

### Apache Arrow IPC

**Stories:** A, B, D.

Why it exists: audit, reasoning, analytics, and retraining can share columnar event data without rebuilding objects.

```rust
let batch = RecordBatch::try_new(
    Arc::new(schema),
    vec![Arc::new(score_array), Arc::new(decision_array), Arc::new(trace_array)],
)?;
```

Performance impact: Story A's draft hot path removed **3.1ms** of serialization overhead by publishing Arrow in place, and downstream systems like Velox/Spark/cuDF could consume the same columnar shape.

### FFI Boundary

**Stories:** A, B.

Why it exists: Rust owns routing and safety; C++/CUDA owns hot model execution. The boundary passes pointers, not objects.

```cpp
extern "C" int sentinel_process_batch(
    SentinelEngine* engine,
    const uint8_t* input,
    size_t input_len,
    uint8_t* output,
    size_t output_cap);
```

Performance impact: Story A kept Rust in control of routing and safety while letting C++/CUDA own the hot model path; the boundary is a pointer handoff, not a copy or serialization step.

## 5.2 Rust Patterns

### Consistent Hashing

**Stories:** A, B, C.

Why it exists: cache locality. Same card/session/prompt should land on the same backend when healthy.

```rust
let key = format!("{}:{}", tenant_id, session_id);
let backend = hash_ring.get(&key).ok_or(GatewayError::NoBackendAvailable)?;
```

Performance impact: Story B used session/prefix locality to move KV-cache hit rate toward **70%**; the draft Sentinel routing story shows prefix cache hit rate improving from **~5% to ~72%** versus random load balancing.

### Circuit Breaker

**Stories:** A, B.

Why it exists: degraded GPU/LLM backends should fail fast instead of consuming queue budget.

```rust
pub enum CircuitState { Closed, Open, HalfOpen }

pub async fn check_request(&self) -> Result<RequestPermit, CircuitOpen> {
    match *self.state.read().await {
        CircuitState::Closed => Ok(RequestPermit::new()),
        CircuitState::Open => Err(CircuitOpen),
        CircuitState::HalfOpen => self.try_probe().await,
    }
}
```

Performance impact: Story A/B circuit breakers converted slow backend failure into fast rejection or reroute, with the draft gateway recovering in **<30 seconds** instead of cascading timeouts.

### Admission Control

**Stories:** A, B.

Why it exists: GPU KV memory and latency budget are finite; reject early before OOM or SLA collapse.

```rust
if estimated_tokens > budget.remaining_tokens || gpu.kv_cache_usage_pct > 85 {
    return Err(GatewayError::RejectedByAdmission);
}
```

Performance impact: Story B used token budget and KV-cache pressure to prevent vLLM OOM and p99 collapse; the interview point is "reject early, before the GPU queue becomes unrecoverable."

### SSE Streaming and Cancellation

**Stories:** A, B.

Why it exists: stream tokens as they arrive and free GPU work when the client disconnects.

```rust
tokio::select! {
    _ = cancel.cancelled() => backend.abort(request_id).await?,
    chunk = backend_stream.next() => tx.send(Event::default().data(chunk?)).await?,
}
```

Performance impact: Story B avoids wasting decode tokens after client disconnects; the draft gateway estimate reduced abandoned-request GPU waste from **~$490/day to ~$12/day**.

### `memmap2`

**Stories:** A, B.

Why it exists: Rust consumers can map Arrow/model/shared buffers without copying.

```rust
let file = OpenOptions::new().read(true).write(true).open(path)?;
let mmap = unsafe { memmap2::MmapOptions::new().map_mut(&file)? };
```

Performance impact: Story B uses memory-mapped model/shared buffers to avoid read-copy-load behavior; the draft model-loading note shows a **500MB load improving from ~1.2s to ~0.3s** with populated mmap.

---

# Section 6: Infrastructure Reference

## 6.1 Host Tuning Checklist

| Host Setting | Story | Why |
|--------------|-------|-----|
| `isolcpus`, `nohz_full`, `rcu_nocbs` | A/B | Quiet cores for p99-sensitive pods |
| Huge pages | A/B/C | Reduce TLB misses for large buffers and shared state |
| NUMA discovery | A/B | Align CPU/GPU/NIC/memory placement |
| IRQ affinity | A/B/D | Keep interrupts off performance cores |
| CPU governor performance | A/B | Avoid frequency ramp latency |
| PREEMPT_RT | A if extreme | Only when jitter budget is tighter than standard kernel can provide |

## 6.2 Kubernetes Checklist

| K8s Feature | Story | Why |
|-------------|-------|-----|
| CPU Manager static | A/B | Exclusive CPUs for hot pods |
| Topology Manager single-numa-node | A/B | Align CPU, memory, GPU, NIC |
| Memory Manager | A/B | NUMA-aware memory allocation |
| PriorityClass/PDB | A/B/D | Protect critical serving pods |
| GPU Device Plugin / Operator | A/B | Declarative GPU resources and health |
| `emptyDir.medium: Memory` | A/C | Shared memory inside pod |

## 6.3 Networking Checklist

| Technology | Story | When to Use |
|------------|-------|-------------|
| DPDK | A/D | Full userspace packet path when extreme latency justifies complexity |
| AF_XDP | D | Lower-overhead kernel-bypass-like path with eBPF integration |
| SR-IOV | A/B | Dedicated VF per pod for low-latency data plane |
| Cilium/eBPF | A/B/D | Better default K8s networking with lower overhead than iptables |
| RDMA/RoCE | A/B | Cross-node zero-copy transfer for Arrow/audit/KV-style data when operationally justified |

## 6.4 Storage

| Technology | Story | Why |
|------------|-------|-----|
| Local NVMe | B | Model cache and fast spill path |
| GPUDirect Storage | B | Direct NVMe-to-GPU path for model loading where supported |
| `mmap` model loading | B | Avoid read-copy-load pattern and allow page-cache reuse |
| RocksDB | D | Local state and feature cache for security workloads |

## 6.5 Bare-Metal Kubernetes Ownership Model

Assumption for Stories A/B: the workload runs on **Kubernetes on bare metal in an on-prem environment**. The mental model is: the host prepares deterministic capacity; Kubernetes assigns that capacity to pods; the application should not fight the scheduler from inside the container.

| Area | Host Side Tuning | Kubernetes Side Tuning | Redundant or Risky on Host for K8s Pods |
|------|------------------|------------------------|----------------------------------------|
| **Compute / CPU / Threads** | Set BIOS performance mode, CPU governor `performance`, reserve housekeeping cores with `isolcpus`, `nohz_full`, `rcu_nocbs`, keep IRQs off isolated cores | Enable CPU Manager `static`; run latency pods as Guaranteed QoS with integer CPU requests/limits; use `PriorityClass` for hot paths | Running application pods with manual `taskset` or per-process pinning is usually redundant; let CPU Manager own pod CPU assignment |
| **NUMA / Topology** | Expose accurate topology; disable automatic NUMA balancing when it conflicts with explicit placement; verify `lstopo`/`numactl --hardware` | Enable Topology Manager `single-numa-node`; enable Memory Manager; request CPU, memory, hugepages, GPU, and SR-IOV VF together | Using `numactl` inside normal app containers can fight K8s placement. Use it only for node-level daemons or controlled benchmarks |
| **Memory** | Reserve hugepages; set `vm.max_map_count`; raise locked-memory limits for pinned memory; avoid swap on latency nodes | Request `hugepages-2Mi`; set memory request equal limit for Guaranteed QoS; configure `/dev/shm` with `emptyDir.medium: Memory` when IPC needs it | Allocating hugepages on the host is not enough; without pod hugepage requests, the workload may not get them |
| **Network** | Configure NIC firmware, RSS queues, IRQ affinity, SR-IOV VFs, RoCE/PFC if using RDMA | Attach SR-IOV network via NetworkAttachmentDefinition for critical pods; use Cilium/eBPF for normal pod traffic; set topology-aware placement | DPDK/AF_XDP on the host does not automatically help pod traffic; the pod must receive the VF/socket path explicitly |
| **Storage** | Provide local NVMe, mount options, model-cache directories, GDS driver stack when supported | Use local PVs/node affinity for model cache; pre-warm models; mount NVMe cache into inference pods | Remote object-store-only model loading is operationally risky for large LLM restarts |
| **GPU** | Install/validate NVIDIA driver stack, firmware, persistence mode, MIG strategy if used, DCGM exporter | Use GPU Operator/device plugin; request `nvidia.com/gpu` or MIG resources; align GPU with CPU/memory via Topology Manager | Manually mounting `/dev/nvidia*` into pods bypasses scheduling guarantees and should be avoided |
| **Reliability** | Keep node images consistent; use NTP/PTP; monitor thermals, ECC, XID, disk wear | Use PDB, readiness/liveness probes, canary/blue-green deployment, HPA/KEDA where appropriate | Host-only tuning cannot protect p99 if pod disruption, CPU throttling, or queue admission is wrong |

Interview answer for `numactl`: "On bare metal I use `numactl` to inspect topology and to run controlled benchmarks. For production Kubernetes pods, I do not wrap the app with `numactl`; I configure CPU Manager, Topology Manager, Memory Manager, hugepage requests, and device plugins so the scheduler gives the pod a coherent CPU/memory/GPU/NIC allocation."

---

# Section 7: Interview Quick-Reference Cards

## Card 1: Stories in One Paragraph Each

**A - CapitalOne:** Sub-5ms fraud scoring platform with Rust Sentinel Gateway, C++/CUDA scoring, Arrow tier boundary, CUDA Graphs, pinned memory, SPSC queues, and K8s topology-aware deployment.

**B - Fiserv/LLM Serving:** Multi-model loan-processing LLM platform using Rust control plane, vLLM/SGLang/TensorRT-LLM runtimes, KV-cache-aware routing, CUDA stream transfer engine, and hierarchical context assembly.

**C - Apple Siri:** Multi-stage ASR -> NLU -> Search -> Orchestration -> TTS pipeline redesigned around shared memory, streaming ASR, DistilBERT NLU, FAISS search, FastSpeech TTS, NEON/Metal acceleration.

**D - Broadcom:** Cloud Secure Web Gateway unified six ML security pipelines into one shared-context inference DAG with XGBoost, BERT, CNN, NER, TensorFlow Serving, ONNX Runtime, dynamic batching, and tenant-aware deployment.

## Card 2: Top 20 "Why Did You Choose X?" Answers

1. **Rust:** predictable low-latency control plane without GC.
2. **C++:** explicit memory layout and hot-path ABI control.
3. **CUDA:** GPU parallelism for neural/LLM/CNN workloads.
4. **CUDA Graphs:** remove repeated launch overhead.
5. **Pinned memory:** real async DMA and lower transfer latency.
6. **vLLM:** PagedAttention and continuous batching.
7. **TensorRT-LLM:** maximum NVIDIA performance for stable deployments.
8. **SGLang:** structured serving and prefix-aware workflows.
9. **Arrow:** zero-copy cross-tier events.
10. **Shared memory:** remove serialization between stages.
11. **SPSC:** lock-free handoff under single producer/consumer ownership.
12. **XGBoost:** fast, explainable, regulated inline decisions.
13. **DistilBERT:** latency/accuracy balance.
14. **Conformer:** streaming ASR with incremental state.
15. **FastSpeech:** low-latency TTS.
16. **FAISS:** fast vector retrieval.
17. **CPU Manager:** exclusive cores in K8s.
18. **Topology Manager:** NUMA/device alignment.
19. **Cilium:** lower-overhead K8s networking.
20. **DCGM:** GPU health as a scheduling and circuit-breaker signal.

## Card 3: Top 15 Fundamentals Questions

| Question | 30-Second Answer |
|----------|------------------|
| What is p99 latency? | The latency 99% of requests complete under; it captures tail behavior users and SLAs feel. |
| Why does NUMA matter? | Remote memory crosses socket interconnects and can double memory latency. |
| Why huge pages? | They reduce TLB misses for large memory regions. |
| What is a CUDA warp? | A group of 32 threads scheduled together on an SM. |
| What is occupancy? | Active warps relative to hardware capacity; useful but not the same as utilization. |
| What is coalescing? | Adjacent threads access adjacent memory so the GPU uses fewer memory transactions. |
| Prefill vs decode? | Prefill processes the prompt in parallel; decode emits one token at a time and is KV-cache sensitive. |
| Why KV cache? | It avoids recomputing past attention K/V for every generated token. |
| MHA vs GQA? | GQA shares K/V heads to reduce KV memory while keeping many query heads. |
| Why continuous batching? | It keeps the GPU busy as requests enter/leave dynamically. |
| Why circuit breakers? | They stop cascading timeouts by failing fast around unhealthy backends. |
| Why admission control? | It protects finite queue, token, and GPU memory budgets. |
| Why Arrow? | It lets many consumers read the same columnar bytes without serialization. |
| Why shared memory? | It turns process/stage handoff into pointer access. |
| Why topology-aware K8s? | CPU/GPU/NIC locality decides tail latency on bare metal. |

## Card 4: Key Metrics to Remember

| Story | Metric |
|-------|--------|
| A | Sub-5ms p99 at 24,500 TPS |
| A | CUDA Graphs saved ~145us launch overhead |
| A | GPU throughput 1K -> 10K req/sec/GPU |
| B | Average latency 8.5s -> 2.3s |
| B | P99 latency 32s -> 4.1s |
| B | Token usage reduced 60% |
| B | KV hit rate to 70% |
| C | E2E p99 850ms -> 280ms |
| C | Stage overhead 50-80ms -> 5-10ms |
| C | Session cache hit 40% -> 85% |
| D | E2E latency 350ms -> 85ms |
| D | Model deployment 2-4 weeks -> 2 days |
| D | Dynamic batching 1K -> 10K req/sec/GPU |

## Card 5: Drawable Architecture Diagrams

```text
A: Gateway -> Tier1 FeatureBlock/Models -> Arrow -> Tier2 Reasoner -> Tier3 Agent
B: Gateway -> Context Assembly -> KV Router -> vLLM/SGLang/TRT-LLM -> CUDA Transfer
C: Edge -> ASR -> Shared State -> NLU/Search/Orch -> TTS
D: Ingress -> Shared Request Context -> Malware/URL/DLP/Threat DAG -> Decision
```

## Card 6: What I Would Do Differently

| Story | Answer |
|-------|--------|
| A | "I would invest earlier in deterministic replay and synthetic decline-spike testing, because the hardest bugs were tail-latency interactions across tiers." |
| B | "I would define shape buckets and KV-cache budgets earlier, before enabling CUDA Graphs broadly, because graph workspace can reduce concurrency." |
| C | "I would push trace IDs and queue-time metrics into every stage from day one; the pipeline was only debuggable once we could separate compute from waiting." |
| D | "I would standardize model packaging earlier with ONNX/Triton-style contracts so each security team could move faster without fragmenting the serving platform." |
