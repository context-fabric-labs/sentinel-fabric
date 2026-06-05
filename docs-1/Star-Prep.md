---
# STAR

    ○ CapitalOne
                        § Three-Tier Fraud Detection
                                □ Architecture Brief
                                        ® How we orchestrate the Gateway Routing to transformer and LLM models leveraging pericular GPU
                                □ Infra Brief
                                □ Stories
                                        ® Sub-50 ms hot path which includes XGBoost + Transformer + FAISS + TigerGraph
                                                ◊ Difficult Target, Ownership, Delivery Under Pressure, Technical Depth
                                        ® Replaced vLLM with SGLang and TensorRT LLM Fusion bringing warm path into hot Path
                                                ◊ Innovation, Failure (Turning into Oppertunity)
                                        ® Fusion of GuradRail with LLM for 20 ms optimization . Build GuardRail governance pipeline
                                                ◊ Conflict with Compliance , Product , GuardRail team
                                        ® Intelligent RUST Gateway
                                                ◊ Above and Beyond
                        § Siri for Apple HomePod
                                □ Stories
                                        ® Unified memory architecture
                                                ◊ Innovation
                        § Broadcom Security Gateway
                                □ Stories
                                        ® ProxySG integration with DLP, CASB , MA
                                                ◊ Unclear requirementsSTAR
---
---

# Search & Recommendation

Search
======

• Pipeline & Stages
        § Ingestion and Pre-Processing
                ○ Parsing :- Extract raw text + metadata from source formats. Using tools like pdfminer, Apache Tika
                ○ Cleaning :- Remove boilerplate (headers/footers, navigation links).
                ○ Normalization :- Lowercasing, Unicode normalization(unicodedata), lemmatization (spaCy).
        § Chunking
                ○ Token-based :- Split by max tokens (e.g., 512–1000), aligns with embedding model’s token limits but may cut mid-sentence.
                ○ sentence-based :- Natural boundaries, preserves meaning but may overshoot token limits.
                ○ semantic splitting - Split at topic boundaries , LangChain’s RecursiveCharacterSplitter, semantic-text-splitter
                ○ Recursive - start by splitting paragraphs, then sentences, and finally by punctuation
        § Tokenization & Embeddings
                □ Dense
			Bi-Encoder
				`all-MiniLM-L6-v2` (Fast)
				`bge-base-en-v1.5` (Balanced)
				text-embedding-3-large (More Recall))
    			Cross Encoder :- Used for reranking
				bge-reranker-v2-m3
                □ Sparse
                        ® BM25
                        ® ColBERT
                □ Multi Model Embedding
                        ® Cohere Embed-4, OpenAI text embedding 3 large, GME 7 BN
        § Indexing
                ○ HNSW (Hierarchical Navigable Small World) :- Builds a multi-layer small-world graph. It made up of two data structures i.e. Graph and skipped linked list
                ○ IVF :- Inverted File Index :- Trains k-means centroids  At query time, find the closest centroids (nprobe)
        § Retrieval
                ○ Querry Embedding
                ○ Sparse Retrieval
                ○ Dense Retrieval
        § Fusion
                ○ Reciprocal Rank Fusion (RRF)
                ○ Cross encoder
        § Ranking
                ○ Top K Candidate

• Challenges
        ○ Context window fitting
        ○ OOV handling
        ○ Token limits & Out of memory errors
        ○ Retrieval relevance,
                § Hallucination
                § Lost in the middle
        ○ Low Recall
        ○ hybrid search latency
        ○ Freshness
        ○ prompt token limits
• Optimizations
        • Simple Optimizations
                § Better Parsers
                § Chunk Sizes
                § Hybrid Seach
                        □ BM-25 & Ensembles
                        □ MMR
                        □ Reciprocal Ranking
                        □ Metadata Filtering
                § Meta Data Filters
        • Advanced Optimization
                § Query Expansion (Multi Query Retrieval) : User query → multiple paraphrases/sub-queries.
                        □ LangChain MultiQueryRetriever.
                        □ Pyserini RM3 (PRF).
                        □ ColBERT with PRF.
                § Reranking
                        □ BM25/TF-IDF: Keyword-based filtering
                                ® Pyserini + PyTorch (BM25 + rerank).
                        □ Cross Encoders .
                                ® HuggingFace Sentense Transformers (cross-encoders).
                        □ LLM ReRanker
                                ® LangChain Rerankers (integrated).
                § Recursive retrieval :- Instead of one-shot retrieval, break queries into sub-queries iteratively and then merge the results.
                        □ LangChain RecursiveRetriever.
                        □ LlamaIndex AutoMergingRetriever (progressive retrieval).
                § Embedded tables :- Good for structured sources like tables and csv. Convert rows/cells into embeddings
                        □ LlamaIndex Structured Retriever.
                        □ OpenAI Embeddings + SQL retrievers
                § Embedding fine tuning
                § Multi-Document Agent
                § Quantization
                § Sharding & Routing
• Observability
        • Retrial evaluation :- How well system retrieves relevant information
                § Precession :- Of what you retrieved, how much was actually relevant?. Precision = TP / (TP + FP)
                § Recall :- Of what was relevant in the corpus, how much did you retrieve?. Recall = TP / (TP + FN)
                § Hit Rate :- least one relevant document is present in the top-k in One query
                § MRR :- Reciprocal of rank of the first relevant doc. if relevant doc is ranked 3rd → 1/3 = 0.33.
                § NDCG(Normalized Discounted cumulative Gain) :- Measure the effectiveness of top ranked document .
                § MAP :- Mean average Precession , its average of all Precession of docs user watched
        • Generator evaluation
                § BLUE(Precession)/ROUGE(Recall)
                § Grounded-ness/Faithfullness :- Did the answer’s claims come from the provided context? (no hallucinations)
                § Context Relevance :- Did retrieval fetch the right evidence for this query??
                § Answer Correctness:- Answer compared with Ground Truth?
        • Techniques
                § LLM as a Judge
                § Topic clustering
        • Tools and Frameworks
                § RAGAS
                § LangSmith
                § Arize Phoenix
        • Drift detection & handling the issues
                § Alerts
                § Actions
• Knowledge Graph
        • TigerGraph
                ○ GSQL
        • Neo4J
                ○ Cypher Query Language
• FAISS
• Interview Q/A
        1. Q: Walk me through a production search pipeline.
           A: I start with ingestion, parsing, cleaning, normalization, chunking, embedding, indexing, retrieval, fusion, ranking, and observability. In production I usually run sparse retrieval for exact matches, dense retrieval for semantic recall, RRF or learned fusion to merge candidates, and a cross-encoder/LLM reranker on the top candidates. The key interview point is that retrieval optimizes recall while ranking optimizes final relevance.

    2. Q: When would you use BM25 vs dense retrieval?
           A: BM25 is strong for exact keywords, names, IDs, legal terms, skills, and rare terms. Dense retrieval is strong when the query and document use different words for the same meaning. For most real systems I use hybrid retrieval because BM25 protects precision and dense retrieval improves semantic recall.

    3. Q: Compare HNSW and IVF for vector search.
           A: HNSW is graph-based, strong on CPU, high recall, and supports incremental adds, but it uses more memory and deletes are usually tombstone/compact. IVF partitions vectors into centroid lists and searches the nearest lists using`nprobe`; it is memory efficient and GPU friendly, but recall depends heavily on centroid quality and rebuild cadence.

    4. Q: How do you choose chunking strategy for RAG/search?
           A: I choose chunking based on the unit of meaning, not just token count. For documentation, semantic or paragraph-based chunks with overlap usually work better than fixed token cuts. I preserve metadata like source, section, timestamp, tenant, and permissions, then evaluate Recall@K and answer groundedness to tune chunk size.

    5. Q: How do you improve low recall?
           A: First inspect failed queries and labels, then increase candidate depth, relax filters, add hybrid search, tune HNSW`efSearch` or IVF `nprobe`, add query expansion, improve chunking, and fine-tune embeddings with hard negatives. I would validate with HitRate@K, Recall@K, MRR, and NDCG before shipping.

    6. Q: Where does reranking fit, and what is the tradeoff?
           A: Reranking happens after candidate generation. A cross-encoder or LLM reranker sees the query and candidate text together, so it is more accurate than embedding similarity alone, but it is too expensive for the full corpus. A common design is retrieve top 500-1000, pre-rank to 100-200, then rerank the final set.

Recommendation
==============

* Core paradigm
  Collaborate Filtering
  Content based Filtering

• Interview Q/A
        1. Q: Walk me through a recommendation pipeline.
           A: A production recommender is usually a funnel: candidate generation produces hundreds or thousands of items, pre-ranking reduces the set cheaply, ranking scores the most promising items with richer features, and re-ranking applies diversity, freshness, policy, and business constraints. Candidate generation optimizes recall; final ranking optimizes user and business objectives.

    2. Q: Compare collaborative filtering and content-based recommendation.
           A: Collaborative filtering learns from user-item interactions, so it captures crowd behavior but struggles with cold-start users/items. Content-based recommendation uses item/user attributes such as text, category, skills, and embeddings, so it handles new items better but can over-personalize. Most modern systems are hybrid.

    3. Q: How do you handle cold start?
           A: For new items, I use content embeddings, taxonomy/category priors, creator/merchant history, and exploration slots. For new users, I use onboarding signals, geography/device/context, popularity priors, and session behavior. The principle is to start with content and priors, then gradually replace them with behavioral signals.

    4. Q: Why do recommendation systems use multi-stage ranking?
           A: Scoring every item with a heavy model is too expensive. Multi-stage ranking lets cheaper retrieval get broad recall, then expensive models focus on a small candidate set. This makes latency and cost manageable while preserving quality.

    5. Q: How do you optimize for multiple objectives like click, dwell, purchase, and trust?
           A: I train multi-task models or separate heads for each objective, then combine scores using weights or a learned calibration layer. I also use guardrails so one metric does not dominate, for example preventing clickbait from improving CTR while hurting dwell time or retention.

    6. Q: What metrics do you use for recommendations?
           A: Offline I use Recall@K, NDCG@K, MAP, coverage, novelty, and diversity. Online I use CTR, conversion, dwell time, retention, revenue, complaint/hide rate, and long-term engagement. Offline metrics are screening tools; A/B tests decide production impact.

---

# Inferencing

• Transformer Architecture
        • Tokenization and Embedding
        • Self-attention
        • Feed forward Network
        • Logits
        • Deeding
• Auto-Regressive
• Architecture
        • Layer-4 :- Orchestration , Routing and Queuing, Throttling
                ○ API Gateway, Async Plane
        • Layer-3 :- Inference Runtime
                ○ Paged Attention, Continuous Batching etc. vLLM/SgLang
        • Layer-2 :- Container Runtime (K8)
                ○ Kubernetes, Scaling , Deployment (Kserve)
        • Layer-1 :- Physical hardware &  Network and Network Transport
                ○ Compute (GPU Selection) , OS Tuning (HugePages), Local Storage for Models (NVME), NCCL, RDMA, NUMA

### Layer-by-Layer Inference Architecture (Bird's Eye View)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    LAYER 4: ORCHESTRATION & ROUTING                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────┐    ┌───────────────────────────────────────────────────┐  │
│  │  Client  │───▶│  API GATEWAY / INGRESS                            │  │
│  │ Request  │    │  • AuthN/AuthZ (JWT, mTLS, RBAC)                  │  │
│  └──────────┘    │  • Rate limiting / Token budget per tenant        │  │
│                  │  • Request classification (model routing)          │  │
│                  │  • Tenant context injection (X-Tenant-ID)          │  │
│                  │  • Input guardrails (prompt injection, PII scan)   │  │
│                  └──────────────────────┬────────────────────────────┘  │
│                                         │                                │
│                                         ▼                                │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  INTELLIGENT ROUTER / LOAD BALANCER                                │  │
│  │  • KV-cache-aware routing (session affinity to warm GPU)           │  │
│  │  • GPU-headroom scheduling (route to pod with free VRAM)           │  │
│  │  • Model-version routing (canary, blue-green, shadow)              │  │
│  │  • Priority queuing (P0 > P1 > P2 per tenant SLA)                 │  │
│  │  • Backpressure / admission control (reject > capacity with 429)   │  │
│  │  • Async plane handoff (Kafka) for non-latency-sensitive requests  │  │
│  └──────────────────────┬──────────────────────────────────────────┘   │
│                          │                                               │
└──────────────────────────┼───────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    LAYER 3: INFERENCE RUNTIME                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  TOKENIZATION & CONTEXT ASSEMBLY                                   │  │
│  │  • Tokenizer (SentencePiece / tiktoken)                            │  │
│  │  • System prompt + user prompt + RAG context composition           │  │
│  │  • Token budget enforcement (max_model_len truncation)             │  │
│  │  • Prefix detection for cache reuse                                │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  SCHEDULER (Continuous Batching Engine)                             │  │
│  │  • Inflight/continuous batching (no head-of-line blocking)         │  │
│  │  • Prefill vs Decode phase separation (chunked prefill)            │  │
│  │  • Sequence preemption (evict low-priority if memory pressure)     │  │
│  │  • Speculative decoding orchestration (draft → verify)             │  │
│  │  • Multi-LoRA adapter selection per request                        │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  KV CACHE MANAGER (PagedAttention / RadixAttention)                │  │
│  │  • Paged allocation (variable-length, no fragmentation)            │  │
│  │  • Prefix caching (shared system prompt KV reuse)                  │  │
│  │  • Tenant-scoped cache keys (hash(tenant_id + tokens))             │  │
│  │  • KV quantization (FP8 → 2× more concurrent sequences)           │  │
│  │  • Eviction policy (per-tenant LRU fairness)                       │  │
│  │  • Offload to CPU/NVMe when GPU memory pressure                    │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  MODEL EXECUTION ENGINE                                            │  │
│  │  • Attention kernel (FlashAttention-2/3, FlashInfer)               │  │
│  │  • GEMM execution (cuBLAS / Composable Kernels / TunableOp)       │  │
│  │  • CUDA/HIP Graphs (captured decode step → single replay)         │  │
│  │  • Tensor Parallelism (AllReduce over NVLink/Infinity Fabric)      │  │
│  │  • Quantized inference (FP8/INT8/INT4 with calibrated scales)      │  │
│  │  • Kernel fusion (LayerNorm + Residual + Activation → 1 kernel)   │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  OUTPUT PROCESSING & GUARDRAILS                                    │  │
│  │  • Detokenization + streaming (SSE / WebSocket)                    │  │
│  │  • Constrained decoding (JSON schema / regex via FSM)              │  │
│  │  • Output guardrails (PII redaction, toxicity filter)              │  │
│  │  • Response watermarking / attribution                             │  │
│  │  • Constant-time response normalization (timing side-channel fix)  │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
└──────────────────────────┼───────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    LAYER 2: CONTAINER RUNTIME (KUBERNETES)                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  GPU ORCHESTRATION                                                 │  │
│  │  • NVIDIA GPU Operator / AMD Device Plugin                         │  │
│  │  • MIG partitioning (hardware tenant isolation)                    │  │
│  │  • Topology-aware scheduling (NVLink mesh, NUMA locality)          │  │
│  │  • Gang scheduling (Volcano / Kueue) for multi-GPU jobs            │  │
│  │  • GFD (GPU Feature Discovery) for node labeling                   │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  SCALING & LIFECYCLE                                               │  │
│  │  • HPA on GPU utilization + request queue depth + TTFT             │  │
│  │  • KEDA (event-driven: Kafka lag, queue depth triggers)            │  │
│  │  • Karpenter (node provisioning: right-size GPU instance)          │  │
│  │  • KServe / Seldon (model serving abstraction)                     │  │
│  │  • Blue-green model deployments (swap CUDA Graph pointer)          │  │
│  │  • Canary rollout with automatic guardrail-based rollback          │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  OBSERVABILITY & SLO ENFORCEMENT                                   │  │
│  │  • DCGM Exporter (GPU util, temp, ECC, memory, Xid errors)        │  │
│  │  • vLLM/SGLang metrics (TTFT, TPOT, cache hit, batch fill)        │  │
│  │  • Prometheus + Grafana + Alertmanager                             │  │
│  │  • Distributed tracing (OpenTelemetry, per-request spans)          │  │
│  │  • Per-tenant SLO dashboards (latency, availability, cost)         │  │
│  │  • Anomaly detection (bimodal latency = side-channel alert)        │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
└──────────────────────────┼───────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    LAYER 1: PHYSICAL HARDWARE & OS                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  GPU HARDWARE                                                      │  │
│  │  • H100 (80GB HBM3, 3.35 TB/s, NVLink 4, FP8 Tensor Cores)       │  │
│  │  • MI300X (192GB HBM3, 5.3 TB/s, Infinity Fabric, CK library)    │  │
│  │  • Multi-GPU: NVSwitch (intra-node), InfiniBand/RoCE (inter-node) │  │
│  │  • PCIe Gen5 for H2D/D2H (pinned memory, async DMA)               │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  NETWORK & COMMUNICATION                                           │  │
│  │  • NCCL / RCCL (GPU collective communication: AllReduce, AllGather)│  │
│  │  • RDMA / RoCE v2 (kernel-bypass, zero-copy GPU-to-GPU)           │  │
│  │  • SR-IOV CNI (pod-level NIC virtualization)                       │  │
│  │  • NIC multi-queue + IRQ affinity (isolate from compute cores)     │  │
│  │  • TCP tuning (rmem/wmem, busy polling, backlog)                   │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  CPU & MEMORY TUNING                                               │  │
│  │  • NUMA pinning (numactl --cpunodebind=N --membind=N)              │  │
│  │  • CPU isolation (isolcpus, nohz_full, rcu_nocbs)                  │  │
│  │  • HugePages (2MB/1GB) for model buffers, KV cache, shared memory │  │
│  │  • THP disabled (madvise) — prevent compaction spikes              │  │
│  │  • CPU Manager static policy (K8s guaranteed QoS)                  │  │
│  │  • Performance governor (no frequency scaling jitter)              │  │
│  └──────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  STORAGE                                                           │  │
│  │  • NVMe local (model weights — fast cold start, no network pull)   │  │
│  │  • Shared filesystem (Lustre/GPFS) for checkpoints (training)      │  │
│  │  • DaemonSet model sync (S3 → local NVMe, pre-cached)             │  │
│  │  • tmpfs for ephemeral scratch (tokenizer cache, temp buffers)     │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘


LATENCY BUDGET BREAKDOWN (request traversing all layers):
┌────────────────────────────────────────────────────────────────────────┐
│  Layer 4: Gateway + Router        │  1-3 ms  (auth, routing decision)  │
│  Layer 3: Tokenize + Schedule     │  0.5-1 ms                          │
│  Layer 3: KV Cache Lookup         │  0.01 ms (hash + pointer)          │
│  Layer 3: Prefill (cache miss)    │  20-100 ms (prompt length dep.)    │
│  Layer 3: Prefill (cache hit)     │  0 ms (skip — reuse cached KV)    │
│  Layer 3: Decode (per token)      │  5-15 ms/token (memory-BW bound)  │
│  Layer 3: Output guardrails       │  1-2 ms                            │
│  Layer 2: K8s overhead            │  ~0 (in-pod, no hop)               │
│  Layer 1: H2D transfer            │  0.01-0.1 ms (pinned, async)      │
│  Layer 1: Network (if TP)         │  0.005 ms/layer (NVLink)           │
├────────────────────────────────────────────────────────────────────────┤
│  TTFT (typical):  50-200 ms  │  TPOT (typical):  8-15 ms/token       │
└────────────────────────────────────────────────────────────────────────┘
```

• Architecture
        • Control Plane
                ○ GitOps (ArhoCD)
                ○ Karpenter
                ○ API Factory
                ○ CLI
        • Data/Compute Plane
                ○ KEDA
                ○ NVIDIA NIM
                ○ Kserve Predictor and Transformer
        • Routing
                § ITSIO
                § CloudFront
        • Async-Plane
                § Kafka
• KPA
        § Reliability
                ○ High avaibility
        § Scaling
                ○ Multi Cloud/Multi-Cluster inferencing
        § Security
                ○ Privacy Preserving
§ Common Challenges
        ○ Memory wall,
        ○ GPU underutilization
        ○ first-token latency
        ○ KV Cache Explosion
• Optimization
        ○ Paged Attention (Inference Engine like vLLM)
        ○ Radix Attention (SgLang)
        ○ Continuous Batching
        ○ Speculative Decoding
                § Token speculation
                § Parallel verification
                § Rejection sampling
        ○ Quantization
                § K-V quantization
                § Weight Quantization
                § Activation Quantization
        ○ KV Caching Optimization
                § Quantization
                § Session affinity
        ○ Custom Kernel (Fusion to reduce memory bandwidth)
        ○ Flashed Attention (Implement in Model Code)
        ○ Grouped Query and Multi-Query Attention (Model Code)
        ○ Sliding Window Attention
        ○ Kernel Fusion
                § Horizonal Fusion :- Common Inputs
                § Vertical Fusion :- Sequential Inputs
        ○ Dynamic Tensor Memory
        ○ Pruning (Training Phase)
                § Structured Pruning (Remove Layer)
                § Unstructured Pruning(Remove Neurons)
                § Activation based
        ○ Cuda Stream
        ○ Time Slicing or CUIDA MPS (Multi Process Service)
        ○ MIG (Multi Instance GPU)
        ○ LLM Parameters
                § Temperature :- Lower the temperature , more determinstic is the model
                § Top_P :- Lower Top_P is determinstic
                § Top_K
                § Max_token
        ○ Long Context Optimization
        ○ Separate Prefill-Decode phase
                § llm-d
                § NVIDIA Dynamo
        ○ Preemption
        ○ Prompt Caching

• Observability
        § System performance
                § Time To First Token (TTFT)
                § Time Per Output Token (TPOT)
                § Throughput :- Query / Sec
                § Token Rate( avg 400 Token / Min is good rate ).
                § Concurred Sessions
                § Queue Wait time
                § GPU utilization (%)
                § Cost / Million Token
        § Functional Performance
                § Task Success Rate
                § Hallucination Rate
                § instruction adherence
                § Citation accuracy
                § Safety
                § BLUE/ROUGE
        § Sources
                § Your gateway metrics + logs (FastAPI, nginx) for request timers and queueing.
                § Engine metrics (e.g., vLLM/TensorRT-LLM/TGI) for tokens/sec, active sequences, cache stats.
        § Tools & Frameworks
                § W & B
                § Arize Pheneix
• LLM Runtimes
        • vLLM
                ○ max_model_len
                ○ max_num_batched_tokens
                ○ max_num_seqs
                ○ enable_chunked_prefill
                ○ enable_prefix_caching
                ○ tensor_parallel_size
                ○ gpu_memory_utilization
        • SgLang
        • Titon + TensorRT-LLM
• Operation Excellence
        • Multi-tenant: per-tenant adapters, JWT/RBAC, quotas/rate limits, per-tenant KV isolation, cost tagging, noisy neighbor controls.
        • Autoscaling: GPU node pools by model size; bin-packing by VRAM; cold-start strategies; queueing + SLO-aware admission.
        • Resilience: health checks by token progress, backpressure, circuit breakers, canary deploys; blue/green for drivers/kernels.
        • Observability: per-request tokens, TTFT/TBT, queue times, batch size histograms, cache hit rates, OOMs, evictions, cost per 1k tokens.
        • Define SLOs (p90 TTFT, p95 latency, availability) and enforce via admission control.
        • Keep models hot (warm pools) for popular SKUs; load adapters lazily.
        • Separate gateway (auth, rate limit, routing) from GPU workers.
        • Always instrument and budget latency—optimize the biggest slice first.
        • Keep compat contracts: prompt/response schema, safety headers, model version pins.
        • pinned memory, larger batch windows (with max wait), speculative/medusa heads, flash-/paged-attention, CUDA streams overlap.

• Safety
• LLM Parameters
        • Top-p
        • Top-k
        • temperature
• Interview Questions
        • High-Signal Interview Q/A
                1. Q: Walk me through what happens when a prompt hits an LLM serving system.
                   A: The gateway authenticates and admits the request, the tokenizer converts text to token IDs, the runtime schedules prefill, the model computes logits, the sampler chooses the next token, and decode repeats autoregressively until stop conditions. Production systems also track queue time, TTFT, TPOT, KV-cache usage, tenant quota, and safety/guardrail results.

    2. Q: What is the difference between prefill and decode?
                   A: Prefill processes the whole input prompt and builds the KV cache; it is compute-heavy and benefits from parallelism. Decode generates one token at a time and repeatedly reads KV cache; it is often memory-bandwidth and scheduling limited. Optimizing inference requires treating these two phases differently.

    3. Q: Why are PagedAttention and continuous batching important?
                   A: PagedAttention reduces KV-cache fragmentation by allocating cache in blocks/pages, which improves concurrency for variable-length requests. Continuous batching keeps the GPU busy by inserting new requests as old ones finish, instead of waiting for an entire static batch to complete.

    4. Q: How would you debug high TTFT?
                   A: Break TTFT into gateway queue time, scheduler wait, tokenization, model load/cold start, prefill time, and safety overhead. Then bucket by prompt length, model, tenant, batch size, and GPU memory pressure. Long prompts, cold models, KV-cache pressure, or head-of-line blocking are common causes.

    5. Q: When would you choose vLLM, SGLang, TensorRT-LLM, or ONNX Runtime?
                   A: vLLM is strong for flexible high-throughput LLM serving with PagedAttention. SGLang is strong for structured generation, prefix reuse, and agentic workflows. TensorRT-LLM is best when the model/hardware/shape buckets are stable and maximum NVIDIA performance matters. ONNX Runtime is good for portable traditional ML and smaller neural models.

    6. Q: How do you design multi-tenant LLM inference?
                   A: I separate gateway and GPU workers, enforce tenant quotas, estimate token/KV cost before admission, route by model and cache locality, isolate adapters when needed, tag cost per tenant, and monitor noisy-neighbor behavior. Backpressure and graceful rejection are as important as raw throughput.

    • Transformer and autoregressive basics
                1. Walk me through what happens when a user sends a prompt to a decoder-only LLM.
                Expected points: tokenization, embeddings, transformer layers, self-attention, FFN, logits, sampling, next-token generation, autoregressive loop.
                2. What is the difference between prefill and decode in LLM inference?
                Expected points: prefill processes all input tokens in parallel; decode generates one token per sequence step; prefill is compute-heavy, decode is memory/KV-cache-heavy.
                3. Why is LLM generation called autoregressive?
                Expected points: each new token depends on previous tokens; model appends generated token back into context; cannot generate all output tokens fully in parallel.
                4. What are logits, and how do temperature, top-k, and top-p affect decoding?
                Expected points: logits are raw scores before softmax; temperature controls randomness; top-k limits candidates by count; top-p limits candidates by cumulative probability.
                5. Why does attention become expensive for long-context prompts?
                Expected points: attention compares tokens with previous tokens; prefill cost grows with sequence length; KV cache memory grows with context length and active sequences.
        • Latency, throughput, and serving lifecycle
                6. Explain the full latency budget of an LLM request.
                7. What is TTFT, and why does it matter?
                Expected points: Time To First Token; user-perceived responsiveness; affected by queueing, prompt length, prefill, batching, scheduling, cold starts.
                8. What is TPOT or inter-token latency?
                Expected points: Time Per Output Token; decode performance; affected by KV cache bandwidth, batch size, sampling overhead, memory bandwidth.
                9. Why can P50 latency look good while P99 latency is bad?
                Expected points: queueing, long prompts, KV pressure, GPU memory fragmentation, noisy neighbors, batching delays, cold starts, outlier requests.
                10. How would you debug a sudden increase in P99 TTFT after prompt length increases?
                Expected points: isolate queue wait vs prefill; profile input length buckets; check GPU memory, KV cache allocation, batch composition, prefill kernels, attention implementation.
        • Batching and scheduling
                11. What is the difference between static batching, dynamic batching, and continuous batching?
                Expected points:
                12. Why is continuous batching important for LLM serving?
                Expected points: requests finish at different times; decode length varies; continuous batching keeps GPU busy by admitting new requests without waiting for entire batch to finish.
                13. How does chunked prefill work, and why is it useful?
                Expected points: split long prompt prefill into chunks; interleave prefill and decode; reduce head-of-line blocking; improve responsiveness for mixed workloads.
                14. What is request preemption in LLM serving?
                Expected points: pause/evict lower-priority sequence; free KV cache or scheduling slot; useful for priority traffic, overload, long requests.
                15. How do you design backpressure for an LLM gateway?
                Expected points: bounded queues, admission control, max prompt tokens, max output tokens, tenant quotas, queue timeout, 429/503, degrade modes.
        • KV cache, memory, and attention optimization
                16. What is KV cache, and why is it critical for decode performance?
                Expected points: stores key/value tensors from previous tokens; avoids recomputing past attention; consumes large GPU memory; decode reads it every token.
                17. What problem does PagedAttention solve?
                Expected points: KV cache fragmentation; variable sequence lengths; page/block-based KV allocation; better memory utilization; enables high concurrency.
                18. Compare PagedAttention and RadixAttention / prefix caching.
                Expected points: PagedAttention manages KV memory efficiently; RadixAttention/prefix caching reuses shared prefixes; useful for repeated system prompts, agents, RAG templates.
                19. What is FlashAttention, and where does it help?
                Expected points: optimized attention kernel; reduces HBM traffic; uses tiling and recomputation; more useful during prefill/attention-heavy phases.
                20. What is the difference between MHA, MQA, and GQA?
        • Model/runtime optimization
                21. How does speculative decoding improve throughput?
                Expected points: small draft model proposes tokens; large model verifies in parallel; accepted tokens reduce expensive decode steps; rejection sampling maintains output distribution.
                22. How does quantization affect LLM inference?
                Expected points: lower memory footprint, higher throughput, possible accuracy loss; INT8/INT4; weight-only vs activation quantization; kernel support matters.
                23. When would you use TensorRT-LLM over vLLM?
                Expected points: TensorRT-LLM for highly optimized NVIDIA deployment, quantization, fused kernels, static/optimized engines; vLLM for flexible serving, PagedAttention, continuous batching, easier ops.
                24. Where does Triton Inference Server fit compared with vLLM, TensorRT-LLM, and ONNX Runtime?
                Expected points: Triton is serving layer; can host multiple backends; useful for model repository, batching, metrics, versioning; vLLM/TensorRT-LLM/ONNX Runtime are execution/runtime backends or engines.
                25. Design a production LLM inference platform for multi-tenant traffic. What are the main components?
        • Safety / Guardrail

---

# GPU/CUDA

• Generations
        • Tesla, Fermi, Kepler, VOLTA(2017), Ampere, Hopper, Blackwell(2024), Rubin (Future)
• Architecture
        • Gia Thread Unit :- Allocate Blocks to various SM
        ┌─────────────────────────────────────────────────────────────────────┐
        │             LOGICAL (programmer)  →  PHYSICAL (hardware)            │
        ├─────────────────────────────────────────────────────────────────────┤
        │                                                                                                │
        │  Grid                          →  Distributed across ALL SMs                  │
        │   │                                 (scheduler assigns blocks)                    │
        │   │                                                                 │
        │   ├─ Thread Block              →  Runs on ONE SM (never split) / One cudablock = 1024 threds      │
        │   │   │                             Multiple blocks can share SM               │
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

• Memory Hierarchy

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

• CUDA
        • Vector Operations
                ○ Addition e.g. C[i]=A[i] + B[i] where i=threadID.x where threadID will be from 0-1023
                ○ cudaMalloc :- allocate the Memory on Device(GPU)
                ○ cudaMemcpy :- Copy data from Host to device
                ○ __global__ :- This lets compiler know that its cuda program e.g. __global__ void vectorAdd(int* A, int* B, int* C, int n)
                ○ Index I for multi block :- i=threadID+ blockID * num_of_threds_per_block
        • 2-D indexing
                ○ int row = blockIdx.y * blockDim.y + threadIdx.y;int col = blockIdx.x * blockDim.x + threadIdx.x;int idx = row * stride + col;
                ○ Stride :- how many elements you jump in memory to move to the next row,
        • Matrix Multiplication (MatMul)
        • L1/L2 Hit Rate (TBD)
                ○ L1 CacheLine = 128 bytes
                ○ Based on matrix calculate the hit rate
        • Coalesced access :- Threads read from continuous memory
        • Shared Memory (TBD)
                ○ Its per SM
                ○ Tiling
                ○ Bank Conflicts
        • Reduction :- Partial operation and aggregation
        • Warp divergence :- Conditional operation , its bad
        • Host-Device transfer (TBD)
                ○ Normal transfer :- CPU -> Staging Copy -> GPU
                ○ Pinned memory :- CPU -> GPU
                ○ Mapped Memory
                ○ Unified Memory
        • CUDA Stream
        • CUDA Graph
        • Waves and Stall(TBD)
        • Performance
                ○ Occupancy
                ○ Rooflineing
        • Error and Debugging
                ○ cudaDeviceProp
                ○ cudaError
                ○ cudaEvent
        • Tools
                ○ NVIDIA NSIGHT
                ○ NCU
                ○ NVIDIA-SMI
                ○ Cuda-memcheck
        • Libraries(TBD)
                ○ cuTLAS
                ○ cuBLAS
                ○ Thrust

• Interview Q/A
        1. Q: Explain CUDA's programming model from thread to GPU.
           A: A kernel launches a grid. The grid contains thread blocks. Each block runs on one SM and is split into warps of 32 threads. Threads in a warp execute in SIMT lockstep. Registers are private to a thread, shared memory is visible to a block, and global memory is visible to the whole GPU.

    2. Q: What is coalesced memory access and why does it matter?
           A: Coalescing means neighboring threads in a warp read or write neighboring memory addresses so the GPU can combine requests into efficient memory transactions. Non-coalesced access wastes memory bandwidth, which is often the real bottleneck in GPU kernels.

    3. Q: When would you use shared memory?
           A: I use shared memory when multiple threads in a block reuse the same data, such as tiled matrix multiplication, reductions, or staging query vectors. It is much faster than global memory, but I must watch capacity, bank conflicts, and synchronization overhead.

    4. Q: What is occupancy, and can high occupancy still be slow?
           A: Occupancy is how many warps are resident on an SM compared with the hardware maximum. High occupancy helps hide latency, but it does not guarantee speed. A kernel can have high occupancy and still be slow because of memory bandwidth limits, uncoalesced access, bank conflicts, warp divergence, or too many instructions.

    5. Q: How do pinned memory, CUDA streams, and CUDA Graphs help inference?
           A: Pinned memory enables fast async host-device transfers. Streams allow transfers and compute to overlap when dependencies permit. CUDA Graphs reduce CPU launch overhead by replaying a captured sequence of GPU operations, which helps when the same scoring graph runs many times.

    6. Q: How do you debug a slow CUDA kernel?
           A: I first measure with Nsight Systems to see timeline gaps, copies, synchronization, and launch overhead. Then I use Nsight Compute for SM utilization, memory throughput, occupancy, warp stalls, branch divergence, and cache behavior. I compare against a roofline expectation to decide if the kernel is memory-bound or compute-bound.

---

# System (OS)

    • Practical Issue & actions

• System Challenges & Optimization
        ○ CPU
                § Issues
                        § CFS Cliff (Thread Context Switching) & IRQ Interrupts (NIC)
                                □ CPU Isolation
                                        ® GRUB_CMDLINE_LINUX="isolcpus=4-19,24-39 nohz_full=4-19,24-39 rcu_nocbs=4-19,24-39"
                        § NUMA Cliffs
                                □ NUMA Pinning
                                        ® Host :- numactl --cpunodebind=0 --membind=0 -- ./fraud-scorer.
                                        ® K8 :- CPU Manager, kind: ClusterConfiguration and kind: KubeletConfiguratio with static policy
                                        ® K8 :- Resource and Limits
        ○ Memory
                § Issues
                        § TLB Cliff
                                □ HugePages on Host
                                □ Shared Memory (/dev/shm)
                                □ Volume with HugePage and shared memory (IPC Namespace) on K8
        ○ Network
                § Overlays overhead
                        § SR-IV CNI pluging. RDMA access via SR-IOV.
                § OS Kernel overhead
                        § AF_XDP (Instead DPDK due to high operation challenges)
                        § NIC Ring Buffer and Multi-Queue configuration . iouring
        ○ GPU
                § Memory Pinning
        ○ Storage
                § Slow Read/Write
                        § NVME
• Monitoring & Troubleshooting
        • USE Methods
                ○ Utilization
                ○ Saturation
                ○ Error
        • RED Methods
                ○ Rate
                ○ Error
                ○ Duration
        • Metrics
                ○ Host Metrics
                ○ K8 Metrics
                ○ DGCM Exporter metrics
                ○ LLM Runtime metrics
                ○ Application metrics
        • Tools
                ○ CPU
                        § Top/htp, uptime, ps
                ○ Memory
                        § Free, vstat
                ○ Network
                        § ss, netstat
                ○ NUMA
                        § numastat
                ○ Storage
                        § Iostat, iotop
                ○ GPU
                        § nvidia-smi
• Profiling
        • CPU
                ○ Perf
                ○ Intel vTune
        • Memory
                ○ Heaptrack
                ○ tcmalloc
        • Network
                ○ Wireshark
        • GPU
                ○ NVIDIA Nsight
                ○ Nvidia Compute
• Benchmarking
        • CPU memory bandwidth
                ○ STREAM
        • CPU/GPU compute sanity
                ○ HPL
        • GPU
                ○ DGCM Diag
        • Storage
                ○ FIO
        • Network
                ○ NCCL tests
        • End_To_End
                ○ MLPerf
• Telemetry
        • eBPF
        • OTEL
• Real Stories

• Interview Q/A
        1. Q: How do you tune a Linux host for p99-sensitive inference or fraud scoring?
           A: I isolate hot CPU cores, pin worker threads, align NUMA placement, use hugepages for large hot memory, keep IRQs away from hot cores, use NVMe/local model storage, and monitor queue depth and context switches. The goal is not just throughput; it is reducing jitter.

    2. Q: What is a NUMA cliff?
           A: A NUMA cliff happens when a thread frequently accesses memory attached to a remote socket or a remote PCIe/GPU path. Average latency may look acceptable, but p99 suffers. I fix it by pinning CPU, memory, NIC queues, and GPU placement to the same topology where possible.

    3. Q: Why do hugepages help?
           A: Hugepages reduce TLB pressure because each page table entry covers more memory. For large arenas, model buffers, and shared-memory regions, this reduces page walks and improves latency stability. The tradeoff is operational complexity and less flexible memory allocation.

    4. Q: How do you reduce network overhead for low-latency services?
           A: Start with kernel tuning, multi-queue NICs, IRQ affinity, keepalive/backlog tuning, and avoiding unnecessary overlays. If the kernel path is still the bottleneck, consider SR-IOV, AF_XDP, DPDK, or RDMA depending on the workload and operational tolerance.

    5. Q: What are USE and RED methods?
           A: USE means Utilization, Saturation, and Errors, and is useful for infrastructure resources like CPU, memory, disk, network, and GPU. RED means Rate, Errors, and Duration, and is useful for services. Together they help separate resource bottlenecks from application-level failures.

    6. Q: How would you troubleshoot a sudden p99 latency spike?
           A: I split latency by stage, then check deployment changes, queue depth, CPU throttling, context switches, IRQ placement, NUMA misses, memory pressure, network retries, GPU utilization, and downstream timeouts. I use`perf`, `numastat`, `iostat`, `ss`, `nvidia-smi`, DCGM, and application traces.

---

# System Programming

• CPP
        • Memory
                ○ malloc/free :- Manual pointer management
                ○ Smart Pointers :- Automatic lifetime management using RAII
                ○ Memory Arena :- Managing region where we allocate many small objects , all objects get destroyed once we destroy arena
                ○ Shared Memory
                        § mmap
        • CPU
                ○ Cache Line and coherence
                        § Use alignas e.g.  alignas(64) int64_t count1;
                ○ Allocate Memory on specific NUMA NODE
                ○ NUMA pinning
                        § numa_local_alloc & pthread_setaffinity_np
                ○ SIMD
                ○ SPSC Queue
                ○ Multi-threading
                        § Pthread :- Legacy
                        § Std::thread :- Modern c++
        • Best Practises
                ○ RAII
                ○ Templates

• RUST
        • Ownership and Borrowing
        • Structs, Enums, and repr(C)
        • Error Handling: Result and Option
        • Traits — Rust's Interface System
        • Smart Pointers
        • Channels: MPSC
        • Shared Memory(memmap2)
        • Iterators
        • Dynamic Dispatch :- Polymorphism

• Interview Q/A
        1. Q: How would you implement a zero-copy fraud scoring hot path?
           A: I would keep the network body in a stable buffer, parse it into borrowed views, build a cacheline-aligned `FeatureBlock`, pass pointers to CPU/GPU scorers, and publish only descriptors to rings. Downstream consumers read shared memory or Arrow buffers instead of rebuilding objects.

    2. Q: Explain RAII in C++ and ownership in Rust.
           A: RAII ties C++ resource cleanup to destructors, so objects release files, memory maps, CUDA streams, and model sessions automatically. Rust ownership and`Drop` provide the same deterministic cleanup idea with stronger compile-time checks around aliasing and lifetime.

    3. Q: How does an SPSC queue work?
           A: A single producer writes a descriptor into a fixed-size ring and release-stores the tail. A single consumer acquire-loads the tail, reads the descriptor, and advances the head. Because only one thread writes each index, it avoids locks and heavy CAS loops.

    4. Q: When do you use shared memory with`mmap`?
           A: I use it when two processes need to see the same bytes without serialization, such as audit buffers, Arrow event batches, or model/index metadata. The design should store offsets and lengths, not raw pointers, because virtual addresses differ across processes.

    5. Q: What makes a safe Rust-C++ FFI boundary?
           A: Use`repr(C)` structs, raw pointers plus lengths, explicit ownership rules, version fields, status codes, and no exceptions or panics crossing the boundary. Rust should wrap unsafe calls in a small safe API.

    6. Q: How do SIMD, cachelines, and NUMA connect in system programming?
           A: SIMD needs contiguous predictable data. Cacheline alignment prevents false sharing and improves locality. NUMA placement keeps memory close to the CPU/GPU doing the work. Together they decide whether the CPU hot path is deterministic or jittery.

---

# Training

• Training Objectives
        • Fine Tuning
                • In-Context Prompting :- Just Prompt Engineering , no training
                • Prompt Tuning (Soft Prompts ):- Learn a small set of virtual tokens at the input embedding layer
                • Prefix Tuning :- Learn virtual key/value prefixes injected into each attention layer.
                • Instruction SFT (LORA/QLORA) :- Add low-rank adapters to selected weight matrices .
                • Preference Tuning(DPO) :- Objective now incorporates answer preferences along with instructions
                • RL/RLHF(PPO) :- You train with a reward signal (reward model, rules, environment).
        • Knowledge Distillation
• Infrastrcture
        • Control Plane
                ○ Training Framework
                        § Kubeflow Training Operator
                        § Ray Train
                ○ Gang Scheduling
                        § Kueue
                        § Volcano
                        § SLURM
                ○ Data versioning and tagging :-
                        § The DVC
                ○ Model version and experiment tracking :-
    § MLFLow
                ○ Hyper Parameter management :-
    § Hyperopt / Katib
                ○ Feature Store :-
                        § Feast
        • Compute
                ○ NVIDIA GPU
                ○ AWS Trainium(Neuron)
                ○ Google TPU
                ○ Intel Gaudi (Havbana)
        • Network
                ○ Inter Node vs Intra Node
                ○ RDMA(EFA on AWS)
                ○ NCCL
                ○ NUMA
                ○ NVLink
                ○ Infiniband, RoCE, GPUDirect, PXN, rail optimization
        • Storage
                ○ FSx Lustre
                ○ GPFS for Checkpoint
                ○ GPU Direct Storage
                ○ NVME Storage
        • Data Pipeline
                ○ PyTorch Data Loader
                ○ NVIDIA DALI
                ○ TFRecords
        • Scaling
                ○ Multi-Cluster Architecture
                        § Needs
                                □ Multi tenancy and Multi cluster
                                □ Managing resources and quotas
                                □ Managing upgrades
                                □ Troubleshooting
                                □ Monitoring
                                □ Stateful application and Data intensive load
                                □ BottleRocket OS, Karpenter ,graviton aws
•
• Observability
        ○ Infrastructure Observability
                § Metrics
                        □ GPU / CPU utilization
                        □ Read/Write Throughput
                        □ $/step, TFLOPs/GPU.
                        □ FLOPS Performance
                                ® Tools
                                        ◊ DGCM
        ○ Training Observability
                § Training Job
                        □ Cross Entropy Loss :- Loss is the average penalty the model gets for its guesses.
                        □ Perplexity :- how many choices it’s juggling. Lower the Perplexity the better is the model
                        □ Token / Sec
                        □ Rouge(Recall Bsed)
                        □ BLEU(Precision Based)
                        □ Accuracy / Precesion / F1
                        □ Tools
                                ® Weights and Biases
                                        ◊ Registry
                                        ◊ Experiments
                                        ◊ Hyper Parameters Optimization using Sweeps
• Profiling and Benchmarking
        • App Profiling
                § Performance Characteristics
                        ◊ GPU Utilization
                        ◊ Execution Time
                        ◊ Memory Usage
                        ◊ CUDA Kernel launches
                        ◊ CPU-GPU Synchronization Delays
                        ◊ NCCL communication
                        ◊ DataLoader
                § Tools
                        ◊ PyTorch Profiler() :- High level CPU and CUDA activities using torch.profiler.ProfilerActivity (CPU/CUDA etc)
                        ◊ NVIDIA Nsight (nsys profile / nsys stats)
                                § Sub Modules
                                        ◊ NVIDIA Compute
                                        ◊ NVTX Annotation
                                § Profiling Sections
                                        ◊ GPU Speed Of Light Throughput
                                        ◊ GPU Speed of Light RoofLine
                                        ◊ Memory workload analysis
                                        ◊ Wrap Stats Statistics
                        ◊  CUDA GDB
                        ◊ Triton, CUTLASS, CUB, Thrust, cuDNN, and cuBLAS
                        ◊ TensorBoard
                        ◊ NVPROF/NVTX :- Lower annotation and profiling
        • Infra Benchmarking
                § Tools
                        ◊ MLPERF
• Optimization
        ○ Training
                ® Prefetching
                        □ PyTorch DataLoader
                        ® NVIDIA DALI
                        ® TFRecords
                ® Async Copy from CPU to GPU - NonBlocking(CUDA  Stream)
                ® Data Caching
                ® Execution caching
                ® Training and Gradient Checkpointing
                ® Dynamic Batching and Padding
                ® Gradient Accumulation
                ® Data Augmentation
                ® DataSet Sharding and Localization
                ® Early Stopping
                ® Weight Quantization
                ® Pruning
                ® Batch Normalization
                ® Special Optimizers
                ® Global and Micro Batch size
        ○ Infra
                ® NUMA Locality
• Operations
        • Infra Challenges
                ○ Cluster level Fragmentation
                ○ Single GPU Fragmentation
                ○ Muti cluster workload management
                ○ I/O bottlenecks
                ○ Sharing Anomaly & Multi-Tenancy Fairness
                ○ GPU starvation
                ○ Data Copy between CPU to GPU
        • Training Job Challenges
                ○ Data Consistency
                ○ Fault Tolerance
                ○ Concurrency Controls
                ○ Load balancing
                ○ Security Concerns
                ○ Pipeline Bubble in PP :- How do we decide the optimum level of TP and PP
        • Other
                ○ Operation Expenses
                ○ Power / Cooling / Carbon footprints
• On-Premise and HPC
        • GPU
                ○ PTX, SASS, warps, cooperative groups, Tensor Cores, and the memory hierarchy
                ○ Metrics
                        □ H100 / A-100
                        □ AWS Tanium
                        □ GCP TPU
                        □ Intel Gaudi
        • K8 on Baremetal
                ○ Server vendor
                        □ Dell Poweredge R640
                        § Nvidia DGX A100 Towers
                ○ Server Power Management
    § Bootstrapping with iDRAC and Ansible (Enterprise) Dell OpenManage mdule
                        § OS Managment  with iDRAC and Ansible (Enterprise) Dell OpenManage mdule
                ○ Kubernetes Management
                        § Ansible Dynamic Inventory
                        § Ansible + Kubespray
                        § Monitoring :- Prometheus + Thanos
                        § Logging :- Splunk
                        § Tracing
                ○ Networking
                        § MetalLB for load balancing K8 services
                        § DNS
                        § BGP
                        § Switch
                ○ Storage
                        § SATA / SAN
                        § Rook / Mayastor
                        § AWS S3 outpost
• Governance
        • ML Lifecycle management
        • Reproducibility
        • Discoverability
        • Access controls
• Developer Notes
        • Fine Tuning
                ○
                ○ Full Fine Tuning :- It updates all the parameter weights
                        □ Fine Tuning and its use case
                                □ LoRA / QLoRA
    ® Low-rank adaptation with reduced memory footprint
                                □ Adapters
    ® Task modularity with insertable modules
                                □ Delta/DARE
    ® Separation of base model and fine-tuned diff
                                □ Prefix Tuning
                                        ® Prepend task-specific tokens
                                □ SLERP Merge   Interpolating multiple fine-tunes
                ○ Libraries
                        □ DeepSpeed
                        □  Accelerate

    ○ PEFT :- freeze most of the base model, and only train a small fraction of parameters
                        □ Adapters :- A New Layer is added and trained keeping res of the base model frozen .
                        □ LoRA / QLoRA :- We track only changes and track then into TWO separate small metrics . These metrics can create the full matrix using Matrix Decomposition method .
                                □ Rank :- More the rank more the accuracy .
                                □ Alpha :-
                                □ DropOut
                                □ QLORA :- We further quantized the weights from FP16 to 4 bit int
                        □ 1 Bit LLMs
                        □ Prefix/Prompt Tuning
                        □ BitFit/ Selective unfreezing

    ○ Instruction Fine Tuning
                ○ Knowledge Distillation
                        Response-Based Distillation
                                Feature-Based Distillation
                                Relation-Based Distillation
                                Model distillation is a technique where a large, powerful model (the "teacher") is used to generate labeled data or outputs, which are then used to train a much smaller "student" model to mimic the teacher's performance on a specific task

    use cases for model distillation:

    Instruction following

    Multi-turn dialogue

    Retrieval-augmented generation

    Tool and function calling

    Text annotation

    Different Training Paarmeters like LR to adjust the knowledge distillation fine tuning

    ○ Mixture Of Expert
                ○ Model merging
                        □ SLERP
                        □ DARE
                ○ Multi-Model Fine tuning
        • Reinforcement Learning
                ○ RLHF
                        □ Reward Model / Reward Metrics (Rewards/rejected rewards/margins)
                        □ HuggingFace TRL
                        □ Ref :-
                ○ PPO
                ○ DPO
        • Distributed Training
                ○ Data Parallelism(DDP) :- Each GPU hold complete copy of Model and Data is split .
                        □ FSDP :- sharding of params/grads/opt states
                                □ ZERO-1 :- Shard the Optimizers across GPU
                                □ ZRRO-2 :- Shard the Optimizers Plus Gradients across GPU
                                □ ZERO-3 :- Shard the Optimizers Plus Gradients Plus Weights across GPU
                ○ Pipeline Parallelism :- Vertical split , each GPU hold different layers .
                        □ Cons :- GPU stays Idle and solve that we use micro batching
                ○ Model(Tensor) Parallelism :- Horizontal split, Each GPU hold part of weights from each layers
                ○ 3-D Parallelism
                        □ Framework :- Megatron-LM / NVIDIA NEMO
    ○ Mixed Precession Training
                ○ Zero Redundancy optimizers
                ○ Optimizers
                ○ Parameter server
                ○ All Reduce
                ○ Ring vs Tree
                ○ Checkpoint and Recovery
        • Quantization
                ○ Post Training Quantization
                ○ Quantized aware Training
        • Federated Training
                ○ TensorFlow Federated
                ○ Flower
        • Resilience & Efficiency
                ○ Checkpointing
                        § NeuronX
                        § Layer coalescing and precision strategies

• Training Frameworks
        ○ Pytorch
        ○ Tensorflow
Tuning ladders: Prompting → In-context → SFT → DPO/ORPO → RM/RLHF (know trade-offs in data, cost, stability).

Catastrophic forgetting & data mix: preserve general skills with balanced, deduped, instruction-style datasets; curriculum ordering helps.

PEFT: LoRA/QLoRA, adapters, prefix/prompt tuning—why PEFT wins (memory/IO), when full-finetune still needed.

Quantization: PTQ vs QAT; NF4 4-bit + LoRA pattern (QLoRA) for memory savings while training adapters; when INT8 is safer.

Optimization: cosine/one-cycle LR, warmups, grad clipping, label smoothing; mixed precision; gradient checkpointing.

Alignment: SFT for helpfulness + DPO/ORPO for preference; safety taxonomies and refusal balance.

b) Production Implementation (what a Platform Eng cares about)
Data pipeline: schema, PII scrubbing, dedupe (MinHash/SimHash), token-balanced sampling, eval splits that mirror prod.

Training stack: HF-Transformers/TRL + PEFT/bitsandbytes, DeepSpeed ZeRO / FSDP for sharding; Megatron-LM if tensor/PP needed; Composer/Axolotl for orchestration.

Hardware: A100/H100 vs L40S; NVLink vs PCIe; storage bandwidth (don’t starve GPUs).

Monitoring: throughput tokens/s, step time breakdown, GPU util, activation/optimizer memory, OOM sentry, loss plateaus.

Artifacts & lineage: model registry, dataset snapshots, recipe hashes, deterministic seeds; canary eval before promote.

Rollout: adapters per customer/domain, merge-and-shrink if needed; shadow traffic, then canary; automated rollback.

Cost levers: PEFT over full FT; token-efficient data; spot/preemptible where safe; resume training checkpoints.

c) Best Practices
Small tight SFT > huge noisy SFT. Add hard negatives & adversarial prompts.

Always ship with evals: instruction-following, factuality, refusal, toxicity, jailbreak resilience.

Guard data governance: licenses, privacy, consent, redaction; audit trails.

Prefer PEFT; only full fine-tune when you must change base capabilities.

Quantize for serving; keep training heads/adapters in FP16/BF16.

Be ready to answer: When would you pick QLoRA vs full FT? How do you stop regressions in math/code while specializing?

• Interview Q/A
        1. Q: Explain the tuning ladder from prompting to RLHF.
           A: Start with prompting or in-context examples because they are cheapest. Move to prompt/prefix tuning or LoRA when behavior needs adaptation. Use SFT for instruction following, DPO/ORPO for preference alignment, and RLHF/PPO only when reward-driven optimization is worth the complexity and instability risk.

    2. Q: When would you choose LoRA/QLoRA over full fine-tuning?
           A: I choose LoRA/QLoRA when I need domain adaptation with lower GPU memory, faster iteration, and easier rollback. Full fine-tuning is justified when the base model must deeply change behavior or when adapter capacity is insufficient. QLoRA is especially useful when training memory is constrained.

    3. Q: Compare DDP, FSDP/ZeRO, tensor parallelism, and pipeline parallelism.
           A: DDP replicates the model and splits data. FSDP/ZeRO shards parameters, gradients, and optimizer states to reduce memory. Tensor parallelism splits individual layers across GPUs. Pipeline parallelism splits layers vertically and uses microbatches to reduce idle bubbles. Large training often combines them.

    4. Q: What makes a good training data pipeline?
           A: It should be versioned, deduped, PII-scrubbed, schema-validated, token-balanced, shuffled correctly, and split to mirror production. It must feed GPUs fast enough through locality, caching, prefetching, and sharding; otherwise expensive GPUs wait on data.

    5. Q: How do you make distributed training fault tolerant?
           A: Use periodic checkpoints, save model/optimizer/scheduler/RNG state, support resume by global step, write checkpoints atomically, and validate checkpoint integrity. At cluster level, use gang scheduling, retry policies, and clear ownership of preemptible vs non-preemptible workloads.

    6. Q: What do you monitor during training?
           A: Monitor loss/perplexity, eval metrics, tokens/sec, step time, GPU utilization, GPU memory, data loader wait, NCCL communication, gradient norms, OOMs, checkpoint time, cost per step, and regressions on safety/factuality/task-specific evals.

---

# Data Science

• Sampling Techniques
        • Simple Random Sampling
        • Stratified Sampling :-  Its for Non overlapping group e.g. Male/Female, Age Groups etc.
        • Systematic Sampling :- Kth item in the total population , could be biased .
        • Convenience Sampling :- Samples convenient to access e.g. Street Interview
• Data and Measures
        ○ Nominal Data :- Categorical Data
        ○ Ordinal Data :- Order of the Data important and not values, e.g. Rank of students in the class
        ○ Intervals :- Temperature scale, where we have range of values in order .
        ○ Ratio:- Contains Interval

• Data Exploration :- Fundamentals questions to be asked about the data after arranging it in frame
        ○ What are the variables , how many variables, what are their data types in my data
        ○ How many observations are there in my sample data i.e number of rows
        ○ Are there any missing values , what are their min , max and average values my data frame .
• Data preparation ,Transformation a
        ○ How to cleanse the data like null values
    ○ How to handle unbalanced dataset
• Feature Handling
        ○ Feature Selection Techniques .
                ▪ Univariate Selection
                ▪ Feature importance
                ▪ Correlation matrix with HeatMap
        ○ How to handle categorical and numerical data
• Descriptive Stats :- Organizing and summarizing the Data
        ○ Central Measure Tendencies
                § Mean :- It’s a default Measure if we do not have outliers in data
                § Median :- If we have outliers then median is more effective than Mean
                § Mode :- Most frequent value in Dataset, usually get consider to replace missing values in data
        ○ Measure of Dispersion
                § Variance
                        □ Population Variance :- Entire population e.g. Census but in real life we hardly have it .
                        □ Sample Variance :- Variance of Sample from the population .
                § Std Deviation :- Its sqrt of variance .
                § Percentile and Quartile(25% and 75 % ) :- First step to find Outliers .
                        □ Five Number Summary (Box Plot) :- Min, Max, Median , 1st Quartile, 3rd Quartile  .
                        □ IQR (Inter Quartile Range) :- Q3 - Q1 .
                        □ Calculate Outliers :- outliers value |< (Q1 - 1.5 * IQR) --- (Q3+ 1.5 * IQR) > | outliers values

    ○ Distributions
                § Normal/Gaussian Distribution :- This tell how data is distributed .
                        □ Empirical Formula of 68 (1st Std)-95(2nd Std)-99.7(3rd Std) % Rule
                § Z-Score ([xi -u]/ 6) :- Value indicating that how much away the given value from Standard deviation .
                        □ Z- Table :- Gives the area based on Z-Score
                        □ Finding Outliers :- Anything grater than 2/3-Stad deviation (left and right both) will be considered as outliers .
                § Std Normal Distribution :- After applying Z-Score the given data we get new distribution (e.g. -3,-2,-1,0,1,2,3) which is Std. Deviation and called as Std Normal Distribution . (It always had Mean 0, and Std deviation is 1
                        □ Standardization(u=0, 6=1) :- is the process where we apply z-score to given data to standardize the values . This helps to bring the data with different units (Age, Salary, Wights) under one scale  .
                        □ Normalization :- Its a process where we define Upper and Lower Bound and convert all the values in that scales (Typically between 0 to 1) . E.g MinMaxScaler .
                § Bernoulli Distribution
                § Binomial Distribution
                § Log Normal Distribution

    ○ Probability :- Measure of the likelihood of an event
                § Mutually Exclusive :- only one event can occur at same time .
                        □ Addition Rule :- P(A or B) = P(A) + P(B)
                § Non Mutually Exclusive :- Multiple events can occur at the same time .
                        □ Addition Rule :- P(A or B) = P(A) + P(B) - P(A & B)
                § Independent Events:- One event not depends on another (Rolling a dice)
                        □ Multiplication Rule :- P (A & B) = P(A) * P(B)
                § Dependent Events :- One event affects other(taking marble from bag )
                        □ Multiplication Rule :- P (A & B) = P(A) * P(B/N) , where N represent remaining items .
                § Permutation and Combination
                        □ Permutation (All combinations) :- nPr = n ! / (n -r) !
                        □ Commination (Unique) :- nCr = n! / [r! *(n-r)!]
• Inferential Stats :- Technique to draw conclusion from measured data
        ○  P-Value :- Value of Probability
        ○ Hypothesis Testing
                § Null Hypothesis (H0):- Coin is Fair
                § Alternate Hypothesis(H1) :- Coin is Unfair
                § Significance Value ( Alpha  ):- How far we are from Mean . If p-value is less than Significance value then Null Hypothesis is wrong
                § Confidence Interval :- If P value lies between confidence interval , then NULL Hypothesis is True .
                § Point Estimate :- Value of any statistics that estimate value of parameter
        ○ Z-Test :- Used when The population variance is known
        ○ T-Test :- One Continuous Variables like Height or Weight , population variance is unknown, small sample size
        ○ Chi-Square :-Two category feature like Gender , Age Group etc .
        ○ Pearson Correlation :- Two continuous variables having relationship
        ○ F-Test (Anova) :- Mix of Category and Continuous Variables

    ○ We can use spacy to calculate the p-value
• PCA and Feature Selection
        ○ Bias :- Model pays less attention to Data, resulted in Underfitting Problem
        ○ Variance :-  Model plays too much attention to data and its noise , resulting into Overfitting .
        ○ Correlation Coefficient :- Measures the linear relationship between each feature and the target variable.
        ○ Regularization :- It’s a technique used to avoid Overfitting
                § L1 (Lasso):-
                § L2 (Ridge):-
                § Elastic Net (L1 + L2)
        ○ Techniques to reduce the features
                § Correlation , we can use person correlation coiff
                § Feature importance :- Many tree algorith provides feature importance value
                § Chi-Square test for categorical feature :- Lower the p-value , higher is the importance
                § PCA :- It reduces the multiple features into small subsets using Decomposition .
        ○ Techniques for feature selection
                § Filter methods :- Features are Ranked based some criteria and low ranked Features are removed .
                        □ Correlation Coefficient
                        □ Chi-Square Test
                § Wrapper methods :- Feature combinations are prepared, trained , evaluated, and compared.
                § Embedded methods :- Use some algorithm which takes into account combination of Filter and Wrapper methods .
                        □ LASSO Regression :-
                        □ Decision Tree and Random Forest :-
        ○ Feature Transformation
                § Standardization
                § Normalization
        ○ Categorical Features
                § Ordinal Encoding
                § Label Encoding
                § One Hot Encoding
                § Binary encoding
        ○ Textual Features
                § Bag Of Words
                § TF-IDF
                § Word Embeddings
        ○ Handling missing data
        ○ Imbalanced DataSet .
        ○ Data cleaning .
                § Remove Duplicates .
Drop oe replace  Nulls
        ○ Dealing with outlier .
                § IQR .
                § Z-Score & Std. Deviation .
• Model Validation Methods
        ○ Standard Validation :- 80/20
        ○ K-Fold Cross Validation :- 1 part for validation, and remaining part for training
        ○ Stratified Cross Validation :- Here we distribute the classes in proper proportion in train and test . Ideal for Imbalanced Dataset.
• Bias-variance tradeoff
• Regularization techniques (e.g., L1, L2 regularization)
• Model selection and hyperparameter tuning
• Model Performance Metric
        ○ Classification
                § Confusion Metrics - TP, FP, FN, TN
                § Accuracy :- It measures the ratio of correctly predicted observation to the total observations. Not suitable for imbalanced datasets.
                        § Accuracy = (TP + TN) / (TP + FP + FN + TN)
                § Precession :- Out of all positive predicted how many correctly predicted :- TP/ (TP + FP) .
                § Recall :- Out of Actual Positive how may are correctly predicted as Positive :- TP / (TP + FN) . We focus on False Negative
                § F Beta (F1) :- Both recall and Precession is important . [2 · (Precision · Recall) / (Precision + Recall)]
                § FPR (Type-I error) : FP / (FP + TN)
                § FNR (Type-II error) :- FN / (TP + FN)
                § ROC/AUC
    § Lift and Gain Charts

    ○ Regression
                § RMSE
                § MSE
        ○ Clustering
• Drift Monitoring
        ○ Data Drift :- Changes in Data Distribution
                § Min, Max Monitoring
                § Null values Monitoring

    ○ Concept Drift :- Changes in
• Model Section Techniques
        ○ Ensamble :- bagging, boosting, stacking
        ○ Random Forest
Model Validation and Overfitting:
        Cross-validation techniques
        Bias-variance tradeoff
        Regularization techniques (e.g., L1, L2 regularization)
        Model selection and hyperparameter tuning
• Training the data , Model selection , Model Validation .
        ○ Over-fitting and Under-fitting of Data :- This is how well your model learned from the provided data points I.e. amount of data or number of features
                ▪ Under-Fitting :- Model did not learn from the data points , resulting into wrong accuracy / MSE /RMSE on validation dataset
                ▪ Over-Fitting :- Model learned so well on data points during training but providing wrong accuracy on validation dataset
                        • Reduce the number of features
                        • Resampling the data or Increase the training data
                        • Regularization (L1/Lasso & L2/Ridge) :- Try to find out the sweet spot while learning and draw the line
        ○ Model validation Techniques and Metrics
                ▪ Methods for validation
                        • 80/20 split of Training Data
                        • K-Fold (4,10) cross validation
                        • Grid CV Search :- This is used to loop over the different hyper parameter combination and getting different score for training
• Deep Learning
        ○ Types of Neural Networks
                ▪ ANN :- Neurons are connected Feed Forward Networks  . It has Input Layer , hidden layers and Output Layer
                ▪ CNN :- Its Feed Forward , captures spatial features from image , mainly used for Image classifications .
                ▪ RNN :- It is recurrent (Feed Forward and Backward) , its mainly used in Time-Series, NLP .
        ○ Layers
                ▪ Simple
                        • Input
                        • Embedding
                        • Dropout
                        • Flattern
                        • MaxPool
                ▪ Specialized
                        • Convolutional (1-D , 2-D, 3-D)
                        • RNN and LSTM
        ○ Loss Function
        ○ Models
                ▪ Sequential
                ▪ Functionals
        ○ Activation
                ▪ Relu
                ▪ SoftMax
        ○ Optimizers
                ▪ ADAM
        ○ Metrix
        ○ Batch Size
        ○ Epoch
        ○ Steps
• Natural Language Processing
        ○ Feature Engineering
                • TextExtraction  & Vectorization  :-This builds the feature vectors from text documents
                        • CountVectorizer
                        • TfidfVectorizer
                        • HashingVectorizer
                        • Word-2Vect
                        • Named Entity Recognition (NER)
                        • Part Of Speech Tagging
                        • Dependancy Parsing
                • Encoding Methods (Transformers) :- This uses .transform method to convert one DF into another
                        • N-gram
                        • Binarizer
                        • String Indexer
                        • Binary Encoding
                        • Binning and Bucketing

• Stages in NLP
        ○ Exploratory Data Analysis
        ○ Data Acquisition
        ○ Data Cleanup
        ○ Data Pre-Processing
                § Tokenization
                § Stemming
                § Lemmatization
        ○ Feature Engineering
                § One Hot Encoding
                § Bag Of Words(1 Gram) / Bag of N Gram
                § TF-IDF
                § Word Embeddings / Vectorization
                        □ Word2Vect
                        □ FastText
                        □ GloVe
                        □ SageMaker Blazing Text
        ○ Model Training
                § Classification
                § Named Entity Recognition (NER)
                § Topic Modeling
                        □ LDA
                        □ NNMF
                        □ LSA
                § Negative Sampling
                § Curriculum Learning
                § Beam Search
                § Regularization
                        □ Gradient Clipping
                        □ Early Stopping
                § Data Augmentation
                § Batch Normalization / Layer Normalization
                § Ensemble
                § Few-Shot & Zero-Shot
                § Performance Metrics
                § Imbalanced DataSet .
                § Dimensionality Curse
                § Handling OOV
        ○ Model Inference
                § Fine tuning
                § Infrastructure & Deployment .
                        □ Kubernetes
                        □ Sage Maker
                § Performance
• Framework, libraries & Tools
        ○ BERT
                § Two steps i.e. Pre-Processing and Encoding
                § Keys in BERT:- Pooled_Output(num_sentenses, 768), Sequence_Output (num_sentences, 128, 768)
        ○ PyTorch
        ○ Gensim
        ○ Spacy
        ○ NLTK
• Real Word Use Cases
        ○ News Classification.
        ○ Voice Agent - Siri
        ○ Malware Classification
        ○ ChatBots
        ○ Search , indexing
        ○ Sequence to Sequence Modeling
        ○ Knowledge Distillation
Important ML Terms and Concepts

Sparse vs Dense matrix
Areas of NLP :- Sentiment Analysis, Text Generation, Topic Modeling
Python Libraries :- NLTK, TextBlob, Gensim , re, WordCloud

 N-Gram, Hash-Gram,
'NLTK/SpaCy/Gensim/FlashText for Text Analysis'

Some of  the supporting Techniques
We Scarping :- The libraries used are a) Request , b) Beautiful Soup

Regression
Cross Validation
        Four/Ten Fold Cross Validation
        Leave On Out Cross Validation

Tuning Parameter
Confusion Metrix , Sensitivity and Specificity
Bias and Variance
Regularization , Boosting and Bagging
ROC and AUC
Regularization
        Ridge Regression
        Lasso Regression
GridCVSearch
Model Selection , Ensamble
Machine Learning Models
        Linear
        KNeighborsRegressor
        Precesions, Measure , Accuracy, Cross Function, Evaluate the Algorithm, Differentiation,
        Grediant Discent , sarcastic, minibatch
        Bucketizer,Discretizer, StringIndexer, OneHotEncoder, VectorAssembler, Imputer , SparseVector, DenseVector, CrossValidation
        Categorical Variable
        Principal Component Analysis/  Dimension Reduction
        Ensemble , BootStrap Aggrigation and Voting
        Kfold Cross Validation , Stratified K Fold ,
        sklearn.metrics.classification_report , cohen_kappa_score , confusion_matrix
        Python StatsModel
        ELI5 Library
        Data Visialization
                MatPlotLib (Basic)
                Seaborn(Advanced, based on MatPlotLib),
                Plotly (Rich in visualization and 3D),
                Bokeh (Integrates with Falsk and DJango). Best for handling large data for Interactive graphs for Web Presentation
        Hyper Parameter tuning & using
                SciKit:- Grid Search
        Plotting :-
                BoxPlot,
Time Series Analysis, ARIMA

• Interview Q/A
        1. Q: How do you handle missing values and outliers?
           A: First understand why the values are missing or extreme. For missing values, I may drop, impute with mean/median/mode, use model-based imputation, or add a missingness indicator. For outliers, I check if they are valid business events, then use IQR, z-score, winsorization, robust scaling, or separate treatment.

    2. Q: Accuracy is high on an imbalanced fraud dataset. Is the model good?
           A: Not necessarily. A model can predict the majority class and get high accuracy while missing fraud. For imbalanced fraud data, I focus on precision, recall, F1/F-beta, PR-AUC, false negative rate, false positive cost, and threshold tuning based on business impact.

    3. Q: Explain precision, recall, and F1.
           A: Precision answers: of predicted positives, how many were actually positive. Recall answers: of actual positives, how many did we catch. F1 balances both. In fraud, recall protects against missed fraud, while precision protects against blocking legitimate customers.

    4. Q: How do you prevent data leakage?
           A: I split data by time or entity before feature engineering where needed, avoid future-derived features, fit scalers/encoders only on training data, dedupe near-identical examples across splits, and ensure validation mirrors production. Leakage often creates impressive offline metrics that collapse online.

    5. Q: How do you choose features or reduce dimensionality?
           A: I start with domain logic and correlation checks, then use filter methods, tree feature importance, regularization like L1, wrapper methods, and PCA/embedding methods when dimensions are high. I validate feature choices with cross-validation and stability over time.

    6. Q: What is data drift vs concept drift?
           A: Data drift means input distributions changed, such as transaction amounts, countries, or device types. Concept drift means the relationship between features and target changed, such as fraudsters changing behavior. I monitor feature distributions, score distributions, label delay metrics, and live performance to decide retraining or rule updates.

---

# Security

### Layer-by-Layer Security Analysis

```
┌─────────────────────────────────────────────────────────────────────┐
│                        EXTERNAL BOUNDARY                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────┐    ┌──────────────────────────────────────────────┐  │
│  │  User /  │───▶│  AI GATEWAY / POLICY ENFORCEMENT POINT       │  │
│  │API Client│    │  • AuthN (JWT/mTLS/OIDC)                     │  │
│  └──────────┘    │  • Rate limit / Token budget                 │  │
│                  │  • Request classification                    │  │
│                  │  • Tenant context injection                  │  │
│                  └──────────────┬───────────────────────────────┘  │
│                                 │                                   │
│                                 ▼                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  IDENTITY & TENANT CONTEXT                                    │  │
│  │  • User identity propagation                                  │  │
│  │  • Tenant isolation boundary                                  │  │
│  │  • Permission set resolution                                  │  │
│  │  • Scoped capability tokens minted                            │  │
│  └──────────────────────────────┬───────────────────────────────┘  │
│                                 │                                   │
│                                 ▼                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  INPUT GUARDRAILS                                             │  │
│  │  • Prompt injection detection (classifier + regex + AST)      │  │
│  │  • Content policy (toxicity, prohibited topics)               │  │
│  │  • Schema validation for structured inputs                    │  │
│  │  • PII/secrets scanning                                       │  │
│  │  • Token budget enforcement                                   │  │
│  └──────────────────────────────┬───────────────────────────────┘  │
│                                 │                                   │
│                                 ▼                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  RAG RETRIEVAL WITH DATA AUTHORIZATION                        │  │
│  │  • User/tenant ACL enforced at retrieval time                 │  │
│  │  • Metadata filtering (mandatory, not optional)               │  │
│  │  • Document classification labels checked                     │  │
│  │  • Retrieved content tagged as UNTRUSTED                      │  │
│  │  • Citation/provenance metadata attached                      │  │
│  └──────────────────────────────┬───────────────────────────────┘  │
│                                 │                                   │
│                                 ▼                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  LLM / AGENT RUNTIME                                          │  │
│  │  • Isolated execution context per tenant                      │  │
│  │  • System prompt separation from user/retrieved content       │  │
│  │  • Token/step/time budget enforcement                         │  │
│  │  • No direct network/filesystem access                        │  │
│  └──────────────────────────────┬───────────────────────────────┘  │
│                                 │                                   │
│                                 ▼                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  TOOL BROKER / ACTION AUTHORIZER                              │  │
│  │  • Policy engine evaluation (OPA/Cedar)                       │  │
│  │  • Scoped, short-lived capability tokens per tool call        │  │
│  │  • Parameter validation and sanitization                      │  │
│  │  • Rate/cost/blast-radius limits per tool                     │  │
│  │  • Human-in-the-loop gate for high-risk actions               │  │
│  └──────────────────────────────┬───────────────────────────────┘  │
│                                 │                                   │
│                                 ▼                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  OUTPUT GUARDRAILS                                            │  │
│  │  • PII/secrets detection and redaction                        │  │
│  │  • Data classification enforcement                            │  │
│  │  • Cross-tenant data leak detection                           │  │
│  │  • Content policy enforcement                                 │  │
│  │  • Response attribution/watermarking                          │  │
│  └──────────────────────────────┬───────────────────────────────┘  │
│                                 │                                   │
│                                 ▼                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  AUDIT / DETECTION / INCIDENT RESPONSE                        │  │
│  │  • Immutable audit log (every decision, every denial)         │  │
│  │  • Anomaly detection (unusual tool patterns, data volumes)    │  │
│  │  • Alert routing and escalation                               │  │
│  │  • Kill switch / circuit breaker activation                   │  │
│  │  • Forensic replay capability                                 │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Input Validation

```
┌─────────────────────────────────────────────┐
│ Input Validation Pipeline                    │
├─────────────────────────────────────────────┤
│ 1. Schema validation (JSON Schema / Pydantic)│
│ 2. Length/token limits                       │
│ 3. Character set validation                  │
│ 4. Encoding normalization (prevent Unicode   │
│    tricks)                                   │
│ 5. Structured field extraction               │
└─────────────────────────────────────────────┘
```

# Stories


“I’m Shailesh, and I bring 25+ years of experience across distributed systems, programming, data analytics, and data science, with the last 8 years focused deeply on AI systems engineering for ultra-low-latency, high-throughput, HPC-style production platforms. My strength is that I understand both sides of AI and security. On the **AI for Security** side, I’ve built real security products powered by AI: fraud detection platforms processing tens of thousands of transactions per second with sub-5ms decisioning, conversational AI inference pipelines like Siri at massive concurrency, and cyber-security inference systems that inspect web traffic using multi-model AI pipelines. These systems required deep work across Rust/C++, CUDA, GPU inference, XGBoost/BERT/LLMs, zero-copy data flow, Kubernetes, observability, and production SLOs. On the **Security for AI** side, I’ve also worked on securing the AI infrastructure itself: multi-tenant isolation, guardrails, policy-based tool authorization, secure RAG, auditability, KV-cache isolation, timing side-channel mitigation, and fail-closed controls around LLM and agent platforms. So my journey has been from building distributed data and analytics systems, to building AI-powered security products, to securing the AI platforms that now power those products. At the Principal level, I focus on designing systems where performance, reliability, and security are not separate concerns, but part of the same architecture.”

==

    Influence/Architecture/Outcome 
		Fraud detection 3 tier
		Siri conlidation of all stages
	Wrong dcesions / Failures / Mistakes
		Initial cost estimation of using Correwave instead bedrock went wrong
		Using Milvus for Vector DB

    Difficult issue to fix
		vLLM cutmization for KV sharing
	Conflicts
		Usage of GuardRail in warm wath between Cyber and Product team
    Unified architecture for siri
