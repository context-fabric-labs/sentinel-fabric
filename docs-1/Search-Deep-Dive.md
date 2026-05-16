# HPC Search & Recommendation Systems Engineering Guide

## A Practitioner's Reference: Architecture Decisions, Implementation, and Lessons Learned

**Domain:** High-Performance Computing for Vector Search, Recommendation, and Real-Time Scoring  
**Tech Stack:** FAISS (C++), TigerGraph (GSQL), Rust, C++/CUDA, Apache Arrow, Zero-Copy Shared Memory  
**Last Updated:** May 2026

---

# PART I: FUNDAMENTALS OF SEARCH & RECOMMENDATION

---

## 1. Search Fundamentals

### 1.1 Information Retrieval (IR) Core Concepts

**Document:** Any searchable unit — a web page, product listing, transaction record, contact entry, job posting, or embedding vector representing an entity.

**Corpus:** The complete collection of documents that can be searched. May range from thousands (on-device contacts) to billions (web-scale search).

**Query:** A user's expression of information need — can be keyword text, natural language, a click sequence, an embedding vector, or a structured filter.

**Relevance:** The degree to which a document satisfies the user's information need. Can be binary (relevant/not) or graded (4-point scale: perfect, excellent, good, fair, bad).

**Recall:** The fraction of all relevant documents that the system successfully retrieves.

$$Recall@K = \frac{|\text{relevant documents in top-K}|}{|\text{total relevant documents in corpus}|}$$

**Precision:** The fraction of retrieved documents that are actually relevant.

$$Precision@K = \frac{|\text{relevant documents in top-K}|}{K}$$

**F1 Score:** Harmonic mean of precision and recall.

$$F1 = 2 \cdot \frac{Precision \cdot Recall}{Precision + Recall}$$

### 1.2 Ranking Metrics

**NDCG@K (Normalized Discounted Cumulative Gain):** The standard metric for graded relevance. Measures how well the system ranks documents by relevance, penalizing relevant documents that appear lower in the list.

$$DCG@K = \sum_{i=1}^{K} \frac{2^{rel_i} - 1}{\log_2(i+1)}$$

$$NDCG@K = \frac{DCG@K}{IDCG@K}$$

Where IDCG@K is the DCG of the ideal (perfect) ranking. NDCG = 1.0 means perfect ordering.

**MRR (Mean Reciprocal Rank):** Average of 1/rank for the first relevant result across queries. Used when there's exactly one correct answer (entity resolution, fact lookup).

$$MRR = \frac{1}{|Q|} \sum_{i=1}^{|Q|} \frac{1}{rank_i}$$

**MAP (Mean Average Precision):** Average precision computed at each relevant document's rank, then averaged across queries. Better than MRR when multiple documents are relevant.

$$AP = \frac{1}{|R|} \sum_{k=1}^{N} Precision@k \cdot rel(k)$$

**Hit Rate@K:** Binary — did the correct answer appear in the top-K results? Used for retrieval-stage evaluation.

**AUC (Area Under ROC Curve):** For binary classification tasks (click/no-click, fraud/not-fraud). Measures the probability that a random positive example scores higher than a random negative.

### 1.3 Sparse Retrieval

**Inverted Index:** The fundamental data structure for keyword search. Maps each term to a posting list of document IDs containing that term. Enables sub-millisecond keyword lookup over billions of documents.

```
"machine"  → [doc_3, doc_17, doc_42, doc_891, ...]
"learning" → [doc_3, doc_42, doc_156, doc_891, ...]
"machine learning" (AND) → [doc_3, doc_42, doc_891, ...]
```

**BM25 (Best Match 25):** The standard sparse scoring function. Weighs term frequency (TF) with diminishing returns and penalizes long documents.

$$BM25(D, Q) = \sum_{i=1}^{n} IDF(q_i) \cdot \frac{f(q_i, D) \cdot (k_1 + 1)}{f(q_i, D) + k_1 \cdot (1 - b + b \cdot \frac{|D|}{avgdl})}$$

Parameters: k_1 ~ 1.2 (TF saturation), b ~ 0.75 (length normalization).

**TF-IDF (Term Frequency - Inverse Document Frequency):** Simpler predecessor to BM25. Weight = TF(t,d) * log(N/DF(t)), where DF is the number of documents containing term t.

**Vocabulary Mismatch Problem:** Sparse retrieval fails when the query uses different words than the document for the same concept ("cheap eats" vs "budget restaurants"). This is the primary motivation for dense retrieval.

### 1.4 Dense Retrieval (Embedding-Based Retrieval / EBR)

**Embedding:** A learned dense vector representation of text, images, or entities in a continuous vector space. Semantically similar items have embeddings that are close in distance.

**Bi-Encoder (Dual-Tower):** Two independent encoders — one for queries, one for documents. Each produces an embedding independently. Similarity is computed as dot product or cosine similarity between the two embeddings. Enables offline precomputation of all document embeddings.

```
Query Encoder(q)  → q_emb ∈ ℝ^d
Doc Encoder(d)    → d_emb ∈ ℝ^d
score(q, d)       = q_emb · d_emb   (dot product)
```

**Cross-Encoder:** Processes (query, document) as a single concatenated input through one encoder. Captures fine-grained token-level interactions. Much higher quality but O(N) cost — cannot precompute.

**Late Interaction (ColBERT-style):** Compromise between bi-encoder and cross-encoder. Each token gets its own embedding; similarity is computed via MaxSim over all token pairs. Better quality than bi-encoder, cheaper than cross-encoder.

**Contrastive Learning:** The training paradigm for embedding models. Positive pairs (query + relevant document) should have high similarity; negative pairs (query + irrelevant document) should have low similarity.

**InfoNCE Loss:** The standard contrastive loss for retrieval model training.

$$\mathcal{L} = -\log \frac{e^{sim(q, d^+)/\tau}}{\sum_{k=0}^{N} e^{sim(q, d_k)/\tau}}$$

Where tau is temperature, d+ is the positive document, and the denominator sums over the positive plus all negatives in the batch.

**Hard Negative Mining:** Selecting training negatives that are difficult (high-scoring but irrelevant) rather than random. Critical for learning fine-grained distinctions. Sources: BM25 top results that aren't relevant, previous model's false positives, in-batch negatives from other queries.

**Matryoshka Representation Learning (MRL):** Training embeddings so that any prefix of the full vector (e.g., first 64 of 768 dims) is a valid lower-dimensional embedding. Enables adaptive precision — use full dimensions for high-quality retrieval, truncated for faster/cheaper retrieval.

### 1.5 Approximate Nearest Neighbor (ANN) Search

**Exact kNN:** Brute-force comparison of the query against every vector in the corpus. O(N*d) complexity. Guarantees 100% recall but doesn't scale beyond ~10M vectors on a single machine.

**ANN Indexes:** Data structures that trade a small recall loss for orders-of-magnitude speedup. The fundamental approximation: instead of checking all N vectors, check only a promising subset.

**IVF (Inverted File Index):** Partitions vectors into K clusters via k-means. At query time, searches only the nprobe nearest clusters. Recall is tunable via nprobe (more clusters = higher recall, slower).

**Product Quantization (PQ):** Compresses vectors by splitting d dimensions into m sub-vectors, then quantizing each sub-vector to its nearest centroid in a learned codebook. Reduces memory from d*4 bytes to m bytes per vector. Enables distance computation in compressed domain (ADC — Asymmetric Distance Computation).

**HNSW (Hierarchical Navigable Small World):** Graph-based index. Builds a multi-layer navigable graph where each node connects to M neighbors at each layer. Search navigates from a random entry point through the graph, getting progressively closer to the query. Best recall-speed tradeoff on CPU. Not trivially updatable.

**CAGRA (CUDA ANN GRAph-based):** NVIDIA's GPU-native graph index. Similar principle to HNSW but designed for massive GPU parallelism. Builds graph using IVF-PQ as initial kNN approximation, then prunes. Can be serialized to HNSW format for CPU serving.

**Locality-Sensitive Hashing (LSH):** Hash functions that map similar vectors to the same bucket with high probability. Less used in modern systems (HNSW/IVF-PQ dominate).

**ScaNN (Scalable Nearest Neighbors):** Google's ANN library. Uses anisotropic vector quantization — quantization error is minimized in the direction that matters for ranking rather than isotropically.

**RaBitQ (Random Bit Quantization):** Extreme compression — 1 bit per dimension plus a small correction factor. ~32x compression vs. float32 with surprisingly good recall. Newer approach (2024+).

### 1.6 Vector Distance Metrics

**L2 (Euclidean) Distance:** ||x - y||^2. Standard for general embeddings. Sensitive to magnitude — two vectors pointing in the same direction but with different lengths have nonzero L2 distance.

**Inner Product (IP):** x · y = sum(x_i * y_i). Equivalent to cosine similarity when vectors are L2-normalized. Not a proper metric (doesn't satisfy triangle inequality).

**Cosine Similarity:** cos(theta) = (x · y) / (||x|| * ||y||). Measures angle between vectors, invariant to magnitude. In FAISS: normalize vectors to unit length, then use IP metric.

**Maximum Inner Product Search (MIPS):** Finding the vector with highest dot product. Relevant when embeddings encode both "direction" (relevance) and "magnitude" (popularity/quality).

### 1.7 Query Understanding

**Intent Classification:** Determining what the user wants to do (navigate, search, buy, call, etc.). Often a multi-class classifier on top of the query representation.

**Named Entity Recognition (NER):** Extracting structured entities from the query (person names, companies, locations, skills, product names).

**Query Rewriting/Expansion:** Modifying the query to improve retrieval — adding synonyms, fixing typos, expanding abbreviations, injecting user context (location, history).

**Faceted Search:** Extracting structured attribute filters from natural language ("senior ML engineer in NYC" → title=ML engineer, seniority=senior, location=NYC).

**Query Routing:** Deciding which retrieval path to use based on query type — keyword path for exact entity lookups, semantic path for vague/complex queries.

---

## 2. Recommendation System Fundamentals

### 2.1 Core Paradigms

**Collaborative Filtering (CF):** "Users who liked X also liked Y." Exploits patterns in user-item interaction matrices. No item content needed. Suffers cold-start for new users/items.

**Matrix Factorization:** Decomposes the user-item interaction matrix R into low-rank factors: R ≈ U * V^T, where U (m x k) represents users and V (n x k) represents items in a shared k-dimensional latent space.

**ALS (Alternating Least Squares):** Optimization algorithm for matrix factorization. Alternately fixes U to solve for V, then fixes V to solve for U. Each step is a closed-form linear regression.

**Content-Based Filtering:** Recommends items similar to what the user liked before, based on item features (text, metadata, embeddings). No cold-start for items, but creates filter bubbles.

**Hybrid Approaches:** Combine collaborative and content signals. Modern systems use embeddings that implicitly capture both — LLM-based embeddings encode world knowledge (content) while being trained on engagement data (collaborative signal).

### 2.2 Deep Learning for Recommendations

**Two-Tower Models (Dual Encoder for Recommendations):** Same architecture as dense retrieval bi-encoders, but for recommendations. User tower encodes user features/history; item tower encodes item features. Score = dot product. Enables precomputation of item embeddings for fast serving.

**Sequential Recommendation:** Models the user's interaction history as a sequence. Uses transformers (causal attention) to predict the next item the user will engage with. Captures temporal patterns, session context, and evolving interests.

**Multi-Task Learning (MTL):** A single model predicts multiple objectives simultaneously (click, like, comment, share, dwell time, purchase). Shared representation learns richer features; task-specific heads specialize.

**MMoE (Multi-gate Mixture of Experts):** Architecture for multi-task recommendation. Multiple expert networks (e.g., DCNv2) share bottom layers. Each task has its own gating network that selects which experts to use. Handles task conflicts better than hard parameter sharing.

**DCNv2 (Deep Cross Network v2):** Explicit feature crossing layer. Computes x_{l+1} = x_0 * (W * x_l + b) + x_l. Efficiently learns polynomial feature interactions without manual feature engineering.

**Generative Recommender:** Treats recommendation as sequence generation. Input: chronological sequence of [item_embedding, action_type] pairs. Model predicts engagement probability for candidate items given the full history.

### 2.3 Multi-Stage Architecture

Modern recommendation systems use a funnel:

```
Candidate Generation (retrieval)  → 10M items → 1000 candidates
    ↓
Pre-Ranking (lightweight scoring)  → 1000 → 200 candidates  
    ↓
Ranking (full model scoring)       → 200 → 50 candidates
    ↓
Re-Ranking (business logic, diversity, fairness)  → 50 → 20 shown
```

**Candidate Generation:** Fast, approximate. Dual-tower models + ANN search. Multiple retrieval channels (collaborative, content, trending, social graph). Union of candidates from all channels.

**Pre-Ranking:** Lightweight model (small neural net or simple features) to reduce candidate set cheaply before expensive full ranking.

**Full Ranking:** The most sophisticated model — multi-task, sequential, cross-attention, late fusion of all features. Runs on GPU for throughput.

**Re-Ranking:** Post-processing rules — diversity injection, business rules (monetization, fairness quotas), deduplication, position bias correction.

### 2.4 Training & Evaluation Concepts

**Implicit Feedback:** User actions that imply preference (clicks, views, purchases, dwell time) rather than explicit ratings. Most real-world recommendation data is implicit.

**Position Bias:** Users tend to click higher-ranked items regardless of relevance. Training on click data without correction learns position effects rather than true relevance. Solutions: inverse propensity weighting, position features during training (removed at serving).

**Exposure Bias:** Items that were never shown to a user get no feedback signal. The model can't learn about unseen items from implicit data alone.

**Offline Evaluation:** NDCG, Recall, Hit Rate on held-out interactions. Necessary but not sufficient — doesn't capture novelty, serendipity, or long-term engagement.

**Online Evaluation (A/B Testing):** The gold standard. Measure actual user behavior (engagement, retention, revenue) under randomized treatment/control assignment.

**Exploration vs. Exploitation:** Tradeoff between showing items the model is confident about (exploitation) vs. showing uncertain items to gather information (exploration). Thompson sampling, epsilon-greedy, or learned exploration policies.

### 2.5 Feature Engineering for Recommendations

**User Features:** Demographics, historical engagement patterns, session context, device type, time-of-day, connection graph properties.

**Item Features:** Content embeddings (text, image), metadata (category, price, author), engagement statistics (CTR, average dwell), freshness (time since creation).

**Context Features:** Time of day, day of week, device type, location, session length, items already seen in session.

**Cross Features:** User-item interactions (has user engaged with this author before? similar items?). Computed via feature crossing (DCNv2) or lookup (graph traversal).

**Percentile Buckets:** Converting raw numerical features into percentile ranks wrapped in special tokens. Dramatically improves tokenization quality for LLM-based models (30x better correlation with embedding similarity).

### 2.6 Cold Start Problem

**New User Cold Start:** No interaction history. Solutions: use demographic/context features, popular items, onboarding surveys, transfer from related platforms.

**New Item Cold Start:** No engagement data. Solutions: content-based features (LLM embeddings encode world knowledge), explore/exploit policies that show new items.

**LLM Embeddings for Cold Start:** A key insight — LLM-based dual encoders can encode rich semantic information about new items (from their text/metadata) without needing any interaction data. The pretrained LLM brings world knowledge.

### 2.7 Knowledge Graph-Based Recommendation

**Knowledge Graph (KG):** A graph of entities and relationships that encodes domain knowledge. Nodes = entities (users, items, attributes). Edges = relationships (purchased, has_category, similar_to, works_at).

**Graph-Based Retrieval:** Traverse the knowledge graph from the user node to find candidate items. 2-3 hop traversal reveals items connected through shared attributes, social connections, or interaction patterns.

**Graph Features:** Structural properties extracted from the graph — node degree, path length, connected component size, centrality measures, shared neighbor count. Used as features in ranking models.

**Graph + Vector Hybrid:** Vector search captures semantic similarity; graph traversal captures structural relationships. Neither alone is sufficient. Fusion strategy: retrieve from both, merge candidates, re-rank with a model that sees both signal types.

---

## 3. Glossary of Key Terms

| Term | Definition |
|------|-----------|
| **ANN** | Approximate Nearest Neighbor — trading small recall loss for massive speedup |
| **AUC** | Area Under ROC Curve — ranking metric for binary classification |
| **Bi-Encoder** | Two independent encoders producing embeddings for query and document separately |
| **BM25** | Best Match 25 — standard sparse retrieval scoring function |
| **CAGRA** | CUDA ANN Graph-based index — GPU-native graph index from NVIDIA |
| **Candidate Generation** | First stage: fast retrieval of ~1000 candidates from millions |
| **Collaborative Filtering** | Recommendation based on similar users' behavior patterns |
| **Contrastive Learning** | Training by pulling positives close, pushing negatives apart |
| **Cross-Encoder** | Joint encoding of (query, document) pair for fine-grained scoring |
| **CTR** | Click-Through Rate — clicks / impressions |
| **cuVS** | CUDA Vector Search — NVIDIA's GPU-accelerated ANN library |
| **DCG** | Discounted Cumulative Gain — sum of graded relevance / log(rank+1) |
| **DCNv2** | Deep Cross Network v2 — explicit feature crossing layer |
| **Dense Retrieval** | Finding similar items via embedding vector proximity |
| **Dot Product** | Inner product sim(q,d) = sum(q_i * d_i) — standard for embeddings |
| **Dual-Tower** | Same as bi-encoder — separate towers for query and item |
| **EBR** | Embedding-Based Retrieval — dense vector search for candidate generation |
| **Embedding** | Dense vector representation of an entity in continuous space |
| **Exploration** | Showing uncertain items to gather signal (vs. exploitation) |
| **FAISS** | Facebook AI Similarity Search — Meta's vector search library (C++) |
| **Hard Negative** | A negative example that is difficult (high-scoring but irrelevant) |
| **HNSW** | Hierarchical Navigable Small World — graph-based ANN index |
| **Hybrid Retrieval** | Combining multiple retrieval methods (vector + graph + keyword) |
| **IDF** | Inverse Document Frequency — log(N/df) measures term rarity |
| **Implicit Feedback** | User behavior signals (clicks, views) vs. explicit ratings |
| **InfoNCE** | Noise Contrastive Estimation loss — standard for contrastive learning |
| **Inverted Index** | Term → posting list of document IDs — core of keyword search |
| **IVF** | Inverted File Index — cluster-based partitioning for ANN |
| **KG** | Knowledge Graph — structured graph of entities and relationships |
| **kNN** | k-Nearest Neighbors — find k closest vectors to query |
| **Late Fusion** | Combining features after independent encoding (vs. early fusion) |
| **LSH** | Locality-Sensitive Hashing — hash-based ANN (mostly superseded) |
| **MAP** | Mean Average Precision — average of precision@k for relevant docs |
| **MIPS** | Maximum Inner Product Search — find highest dot-product vector |
| **MMoE** | Multi-gate Mixture of Experts — multi-task architecture |
| **MRR** | Mean Reciprocal Rank — 1/rank of first correct result |
| **MTL** | Multi-Task Learning — shared model predicting multiple objectives |
| **NDCG** | Normalized Discounted Cumulative Gain — standard ranking metric |
| **NER** | Named Entity Recognition — extracting entities from text |
| **Position Bias** | Users click top results regardless of relevance |
| **PQ** | Product Quantization — vector compression into m-byte codes |
| **Precision** | Fraction of retrieved documents that are relevant |
| **Pre-Ranking** | Lightweight scoring to reduce candidate set before full ranking |
| **Query Routing** | Directing queries to appropriate retrieval path |
| **RaBitQ** | Random Bit Quantization — 1-bit per dimension compression |
| **Ranking** | Scoring and ordering candidates by predicted relevance/engagement |
| **Re-Ranking** | Post-processing: diversity, business rules, fairness |
| **Recall** | Fraction of relevant documents successfully retrieved |
| **ScaNN** | Scalable Nearest Neighbors — Google's anisotropic ANN library |
| **Sequential Rec** | Modeling user history as ordered sequence for prediction |
| **Sparse Retrieval** | Keyword/term-based search (inverted index + BM25) |
| **Two-Tower** | Dual-encoder architecture for fast retrieval scoring |
| **Vector Index** | Data structure for efficient nearest-neighbor search |

---

# PART II: HPC ENVIRONMENT — INDUSTRY STANDARDS & OUR ARCHITECTURE CHOICES

---

## 4. Industry Standard Approaches (What Exists)

### 4.1 Vector Search Solutions Landscape

| Solution | Type | Language | GPU | Scale | Typical Use |
|----------|------|----------|-----|-------|-------------|
| **FAISS** | Library (in-process) | C++ | Yes (cuVS) | Billions | Search infra teams with custom pipelines |
| **Milvus** | Managed vector DB | Go/C++ | Yes | Billions | Teams wanting managed infra |
| **Pinecone** | Cloud-native vector DB | Proprietary | Yes | Billions | SaaS-first teams |
| **Weaviate** | Vector DB | Go | Limited | Millions | Semantic search + schema |
| **Qdrant** | Vector DB | Rust | No | Millions | Rust ecosystem, filtering |
| **Elasticsearch kNN** | Plugin in search engine | Java/C++ | No | Millions | Teams already on ES |
| **Redis VSS** | Module in cache | C | No | Millions | Low-latency with caching |
| **Vespa** | Search + serving platform | Java/C++ | Limited | Billions | Yahoo-scale serving |
| **ScaNN** | Library (in-process) | C++ | No | Billions | Google-internal + OSS |
| **cuVS (RAFT/CAGRA)** | GPU library | C++/CUDA | Yes (native) | Billions | GPU-native workflows |

### 4.2 Graph Database Solutions

| Solution | Query Language | Scale | Real-Time | Typical Use |
|----------|---------------|-------|-----------|-------------|
| **TigerGraph** | GSQL | Billions of edges | Yes (sub-second) | Fraud detection, social graphs |
| **Neo4j** | Cypher | Millions of edges | Moderate | Knowledge graphs, CRUD |
| **Amazon Neptune** | Gremlin/SPARQL | Billions | Moderate | AWS-native graph workloads |
| **JanusGraph** | Gremlin | Billions | Moderate | Open-source distributed |
| **DGraph** | GraphQL+- | Billions | Yes | Low-latency graph queries |
| **ArangoDB** | AQL | Millions | Yes | Multi-model (doc + graph) |

### 4.3 ML Serving Frameworks

| Framework | Language | GPU | Batching | Typical Use |
|-----------|----------|-----|----------|-------------|
| **SGLang** | Python/C++ | Yes | Advanced (prefix caching) | LLM/SLM ranking |
| **vLLM** | Python/C++ | Yes | PagedAttention | LLM serving |
| **TensorRT-LLM** | C++/Python | Yes | In-flight batching | NVIDIA-optimized LLM |
| **Triton Inference Server** | C++ | Yes | Dynamic batching | Multi-model serving |
| **ONNX Runtime** | C++ | Yes | Limited | Cross-framework inference |
| **TorchServe** | Python | Yes | Basic | PyTorch ecosystem |
| **Ray Serve** | Python | Yes | Adaptive | Distributed serving |

### 4.4 Data Serialization for HPC Pipelines

| Format | Zero-Copy | Language Support | GPU | Typical Use |
|--------|-----------|-----------------|-----|-------------|
| **Apache Arrow** | Yes (columnar) | C++, Rust, Python, Java | Via cuDF | Cross-component data flow |
| **FlatBuffers** | Yes | C++, Rust, Go, Java | No | Schema-evolved messages |
| **Cap'n Proto** | Yes | C++, Rust | No | RPC without serialization |
| **Protocol Buffers** | No (copies) | All major languages | No | RPC (gRPC), general interchange |
| **MessagePack** | No | All | No | Compact JSON alternative |
| **Shared Memory** | Yes (by definition) | C++, Rust (raw) | CUDA IPC | Intra-host zero-copy |

### 4.5 Inter-Process Communication for Low-Latency

| Mechanism | Latency | Throughput | Complexity | Typical Use |
|-----------|---------|-----------|-----------|-------------|
| **Shared Memory (shm)** | ~100ns | Limited by memory bandwidth | High | Same-host, zero-copy |
| **UNIX Domain Sockets** | ~1-5us | ~10 GB/s | Low | Same-host, stream |
| **TCP (loopback)** | ~10-50us | ~5 GB/s | Low | General networking |
| **RDMA (InfiniBand)** | ~1-2us | 200-400 GB/s | High | Cross-node HPC |
| **DPDK** | ~1-5us | Line rate (100 Gbps) | Very High | Network packet processing |
| **io_uring** | ~1-5us | ~millions ops/sec | Medium | Linux async I/O |
| **SPSC Ring Buffer** | ~10-50ns | Memory bandwidth | Medium | Lock-free producer/consumer |

---

## 5. Our Architecture Choices — What We Selected and Why

### 5.1 Vector Search: FAISS (Library) over Managed Vector Databases

**Choice:** FAISS as an in-process C++ library linked directly into our scoring service.

**Alternatives Considered:**
- Milvus (managed vector DB with network API)
- Pinecone (cloud-hosted)
- Elasticsearch kNN (add-on to existing ES)
- Custom brute-force CUDA kernel

**Why FAISS:**

| Factor | FAISS (chosen) | Milvus/Pinecone | ES kNN |
|--------|---------------|-----------------|--------|
| **Latency** | 0.3ms (in-process, GPU) | 2-10ms (network hop) | 5-20ms |
| **Control** | Full — index params, memory layout, GPU streams | Limited API | Very limited |
| **Integration** | Zero-copy with our C++/Rust pipeline | Serialize/deserialize | JSON over HTTP |
| **GPU** | Native (cuVS, CAGRA) | GPU support varies | No |
| **Cost** | GPU VRAM only | Per-query pricing or cluster | Node licensing |
| **Operational** | We own it (higher ops burden) | Managed | Managed |

**Key Reasoning:**
1. Our 5ms SLA cannot tolerate network round-trips. A managed vector DB adds 2-10ms minimum for serialization + network + deserialization. FAISS in-process gives us 0.3ms.
2. We need GPU co-location — the embedding model and FAISS search must share the same GPU to avoid device-to-host transfers (saves 0.8ms per query).
3. We already operate a C++/Rust service — adding FAISS as a linked library is zero operational overhead vs. a new distributed system.
4. FAISS gives us full control over index parameters, rebuild schedules, memory layout, and multi-tenancy. A managed DB abstracts these away.

**What we'd do differently:** For warm-path workloads (200ms+ SLA), we'd consider Milvus or Vespa to reduce ops burden. The in-process approach only makes sense when you're already running custom C++ and need sub-millisecond latency.

### 5.2 Graph Database: TigerGraph over Neo4j/Neptune

**Choice:** TigerGraph for real-time graph queries in the hot path.

**Alternatives Considered:**
- Neo4j (most popular graph DB)
- Amazon Neptune (managed)
- JanusGraph (open-source distributed)
- Custom in-memory adjacency lists

**Why TigerGraph:**

| Factor | TigerGraph (chosen) | Neo4j | Neptune | Custom |
|--------|---------------------|-------|---------|--------|
| **2-hop latency** | 1-2ms | 5-20ms | 10-50ms | <1ms (if fits memory) |
| **Scale** | Billions of edges | ~100M edges/node | Billions | Limited by RAM |
| **Real-time updates** | Yes (streaming) | ACID (slower) | Eventual | Custom logic |
| **Query language** | GSQL (powerful accumulators) | Cypher | Gremlin | Custom |
| **Throughput** | Millions vertices/sec | Thousands | Variable | Highest (in-memory) |

**Key Reasoning:**
1. TigerGraph's GSQL accumulators (MapAccum, SetAccum, ListAccum) let us compute graph features within the traversal itself — no post-processing step.
2. Sub-2ms for 2-hop queries on billion-edge graphs fits our 5ms SLA.
3. Built-in REST endpoint auto-generation means the operations team can inspect graph queries without custom tooling.
4. Native distributed partitioning handles our fraud graph (5B+ edges across card, merchant, device, IP vertices).

**What we'd do differently:** For the Siri knowledge graph (simpler schema, smaller scale), Neo4j would have been simpler to operate. We chose TigerGraph for both use cases to avoid running two graph DBs, but the operational complexity of TigerGraph is non-trivial for simpler workloads.

### 5.3 ML Serving: ONNX Runtime + Custom C++ over Triton/SGLang

**Choice:** ONNX Runtime (C++) for transformer models, custom C++ for XGBoost ensemble.

**Alternatives Considered:**
- Triton Inference Server (NVIDIA)
- TorchServe
- SGLang/vLLM (for LLM ranking)
- TensorRT (compiled models)

**Why ONNX Runtime:**

| Factor | ONNX Runtime (chosen) | Triton | TorchServe |
|--------|----------------------|--------|------------|
| **Integration** | In-process C++ API | gRPC (network hop) | HTTP |
| **Latency overhead** | ~0 (function call) | 1-5ms (IPC) | 5-10ms |
| **GPU sharing** | Same CUDA context as FAISS | Separate process | Separate |
| **Model format** | ONNX (portable) | ONNX/TRT/PyTorch | PyTorch |
| **Batching** | Custom (we control) | Dynamic batching | Basic |

**Key Reasoning:**
1. In-process execution means the NLU model output (embedding) stays on GPU and feeds directly to FAISS — no serialization.
2. We need tight control over batching strategy — our workload is prefill-only (single forward pass for scoring), not generation. Triton's batching assumptions don't match.
3. ONNX gives us portability — we can train in PyTorch, export to ONNX, and serve in C++ without Python in production.

**What we'd do differently:** For the SLM ranking stage (0.6B model), SGLang's scoring-only path with prefix caching would be 2-3x more efficient than our custom approach. We built our own before SGLang published their optimizations. Today we'd adopt SGLang for any model > 300M parameters.

### 5.4 Data Flow: Apache Arrow + Shared Memory over gRPC/Protobuf

**Choice:** Hugepage-backed shared memory arena with Apache Arrow columnar layout and SPSC ring buffers for coordination.

**Alternatives Considered:**
- gRPC with Protocol Buffers (standard microservice communication)
- Unix domain sockets with FlatBuffers
- ZeroMQ with custom serialization
- Cap'n Proto

**Why Shared Memory + Arrow:**

| Factor | Shared Memory (chosen) | gRPC/Protobuf | Unix Socket |
|--------|----------------------|---------------|-------------|
| **Per-message overhead** | ~0 (descriptor only) | 20-80us (serialize/copy/deserialize) | 5-10us |
| **Bytes copied** | 47 bytes/transaction | Entire payload twice | Entire payload |
| **CPU overhead** | Atomic operations only | Marshal/unmarshal | Syscalls |
| **Cross-language** | Arrow C Data Interface | Yes (codegen) | Manual |
| **Debugging** | Harder (raw memory) | Excellent (structured) | Moderate |

**Key Reasoning:**
1. At 20,000+ TPS with a 5ms SLA, serialization overhead was eating 40-80ms. Shared memory eliminates it entirely — components read from the same physical memory.
2. Apache Arrow provides the columnar layout (contiguous float32 arrays for embeddings) that FAISS expects natively. No conversion needed.
3. The SPSC ring buffer carries only 32-byte descriptors (offset + length) — the actual data is in the arena. This keeps coordination cache-friendly and lock-free.
4. Hugepages (2MB or 1GB) reduce TLB misses when accessing the arena, which matters for random-access patterns in large embedding tables.

**What we'd do differently:** The debugging story for shared memory is poor. We spent significant time on tooling (arena visualizers, descriptor ring inspectors) that wouldn't have been necessary with structured serialization. For any pipeline with latency budget > 10ms, gRPC + Arrow Flight would give us 90% of the performance with much better observability.

### 5.5 Programming Languages: C++ and Rust over Go/Java/Python

**Choice:** Rust for the gateway/data plane, C++ for FAISS/CUDA/ML integration, Python only for training.

**Alternatives Considered:**
- Go (garbage collected, simpler concurrency)
- Java (JVM, mature ML ecosystem)
- Python everywhere (simplest for ML)
- Pure C++ (no Rust)

**Why Rust + C++:**

| Concern | Rust (gateway) | C++ (FAISS/CUDA) | Go | Java |
|---------|---------------|-------------------|-----|------|
| **GC pauses** | None | None | 1-10ms (unpredictable) | 10-100ms+ |
| **Memory safety** | Compile-time guaranteed | Manual (dangerous) | GC | GC |
| **Zero-copy** | Lifetime system enables it safely | Raw pointers | Not practical with GC | Not practical |
| **FFI to FAISS** | Via C API (bindgen) | Native | cgo (slow) | JNI (slow) |
| **Concurrency** | Ownership prevents data races | Manual (error-prone) | Goroutines | Threads |
| **Latency tail** | Deterministic | Deterministic | GC tail | GC tail |

**Key Reasoning:**
1. GC pauses are incompatible with 5ms SLA. A 10ms GC pause = SLA violation. Rust and C++ give us deterministic latency.
2. FAISS and CUDA are C++ libraries. Calling them from C++ is zero-overhead. From Go/Java, there's a CGo/JNI boundary penalty + you can't keep data on GPU.
3. Rust's ownership system lets us build zero-copy pipelines (borrowed references into slab-allocated network buffers) with compile-time safety guarantees. In C++, the same patterns are possible but use-after-free bugs are common.
4. We use C++ specifically for code that interfaces with CUDA and FAISS (same language, same build system). Rust handles everything else (networking, parsing, orchestration, ring buffers).

**What we'd do differently:** We underestimated Rust's compile times. Our CI pipeline went from 3 minutes (C++) to 12 minutes (Rust). We'd invest in workspace splitting and incremental compilation infrastructure earlier. Also, the Rust FAISS bindings (via bindgen on c_api) are fragile — we'd consider a thin C++ shim library with a cleaner C API surface.

---

## 6. Infrastructure Architecture

### 6.1 GPU Cluster Topology

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  ON-PREM GPU CLUSTER (Kubernetes)                                             │
│                                                                               │
│  ┌───────────────────────────────────────────────────────────────────────┐   │
│  │  GPU Node Pool (8 x DGX-style nodes)                                  │   │
│  │                                                                        │   │
│  │  Each Node:                                                           │   │
│  │  ┌──────────────────────────────────────────────────────────┐        │   │
│  │  │  4 x NVIDIA H100 80GB (NVLink mesh between GPUs)         │        │   │
│  │  │  2 x Intel Xeon Platinum (64 cores each, 2 NUMA nodes)   │        │   │
│  │  │  1 TB DDR5 RAM (512 GB per NUMA node)                    │        │   │
│  │  │  8 x NVMe SSDs (RAID-0, 50 GB/s sequential read)         │        │   │
│  │  │  2 x ConnectX-7 200Gbps InfiniBand NICs                 │        │   │
│  │  │  PCIe Gen5 backplane (32 GT/s per lane)                  │        │   │
│  │  └──────────────────────────────────────────────────────────┘        │   │
│  │                                                                        │   │
│  │  Network:                                                             │   │
│  │  • NVLink: 900 GB/s (GPU-to-GPU intra-node)                          │   │
│  │  • InfiniBand: 200 Gbps (node-to-node for distributed training)      │   │
│  │  • Ethernet: 100 Gbps (management + data plane)                       │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
│                                                                               │
│  ┌───────────────────────────────────────────────────────────────────────┐   │
│  │  CPU Node Pool (inference fallback + general compute)                  │   │
│  │  • 16 x standard nodes (no GPU)                                       │   │
│  │  • 128 cores, 512 GB RAM each                                         │   │
│  │  • For: CPU-only HNSW serving, XGBoost, feature pipelines             │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
│                                                                               │
│  ┌───────────────────────────────────────────────────────────────────────┐   │
│  │  Storage                                                               │   │
│  │  • NFS (shared model/index storage): 100 TB, 10 GB/s aggregate        │   │
│  │  • Local NVMe (per-node): 30 TB, for index loading + checkpoints      │   │
│  │  • S3-compatible object store: PB-scale for training data              │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 Kubernetes Configuration

**Resource Management:**
- NVIDIA GPU Operator (DaemonSet) — driver lifecycle, device plugin, monitoring
- Node Feature Discovery (NFD) — auto-labels nodes with GPU capabilities
- Topology-Aware Scheduling — pins pods to NUMA node closest to requested GPU
- CPU Manager (static policy) — exclusive CPU cores for latency-sensitive pods

**Pod Architecture (Fraud Detection):**
```
┌─────────────────────────────────────────────────────┐
│  Pod: fraud-scorer-{replica}                         │
│                                                      │
│  Container 1: scorer (main)                          │
│    • Rust gateway + C++ FAISS/ONNX (single binary)  │
│    • GPU: 1 x H100 (or MIG slice: 3g.40gb)         │
│    • CPU: 8 dedicated cores (CPU Manager static)    │
│    • Memory: 32 Gi (+ 256 Mi hugepages)            │
│    • Mounts: /data/faiss (index PVC)                │
│    •         /dev/hugepages (shared arena)          │
│                                                      │
│  Container 2: tg-client (sidecar)                   │
│    • TigerGraph client library (graph queries)      │
│    • CPU: 4 cores                                    │
│    • Memory: 8 Gi                                    │
│    • Communicates via shared memory arena            │
│                                                      │
│  Init Container: index-loader                        │
│    • Downloads latest FAISS index from feature store │
│    • Validates checksum, writes to PVC              │
│    • Downloads latest ONNX model                     │
└─────────────────────────────────────────────────────┘
```

### 6.3 GPU Memory Layout (H100 80GB)

```
┌────────────────────────────────────────────────────────────┐
│  H100 80GB HBM3 — Memory Allocation                         │
│                                                             │
│  ┌──────────────────────────────────────────────┐  0 GB    │
│  │  FAISS Index (GpuIndexIVFPQ)                  │          │
│  │  10M x 128d x 16 bytes (PQ) = 160 MB         │          │
│  │  + centroids + metadata ~ 250 MB              │          │
│  └──────────────────────────────────────────────┘  ~0.3 GB │
│                                                             │
│  ┌──────────────────────────────────────────────┐          │
│  │  ONNX Runtime workspace                       │          │
│  │  DistilBERT (66M params) ~ 260 MB            │          │
│  │  + activation memory ~ 500 MB                 │          │
│  └──────────────────────────────────────────────┘  ~1.1 GB │
│                                                             │
│  ┌──────────────────────────────────────────────┐          │
│  │  Embedding cache (recent queries)             │          │
│  │  100K x 768d x 4 bytes ~ 300 MB              │          │
│  └──────────────────────────────────────────────┘  ~1.4 GB │
│                                                             │
│  ┌──────────────────────────────────────────────┐          │
│  │  CUDA workspace (temp allocations)            │          │
│  │  cuBLAS, kernel scratch, RMM pool ~ 2 GB     │          │
│  └──────────────────────────────────────────────┘  ~3.4 GB │
│                                                             │
│  ┌──────────────────────────────────────────────┐          │
│  │  FREE (available for larger models, scaling)   │          │
│  │  ~ 76 GB                                      │          │
│  └──────────────────────────────────────────────┘  80 GB   │
│                                                             │
│  Note: With MIG (3g.40gb slice), available = 36.5 GB       │
│  Still sufficient for our workload (~3.4 GB used)          │
└────────────────────────────────────────────────────────────┘
```

### 6.4 NUMA-Aware Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│  DUAL-SOCKET SERVER — NUMA Topology                                      │
│                                                                          │
│  ┌─────────────────────────────────┐  ┌─────────────────────────────────┐
│  │  NUMA Node 0                     │  │  NUMA Node 1                     │
│  │                                  │  │                                  │
│  │  CPU Socket 0 (32 cores)         │  │  CPU Socket 1 (32 cores)         │
│  │  DDR5: 512 GB                    │  │  DDR5: 512 GB                    │
│  │                                  │  │                                  │
│  │  PCIe Root Complex 0:            │  │  PCIe Root Complex 1:            │
│  │    GPU 0 (H100)                  │  │    GPU 2 (H100)                  │
│  │    GPU 1 (H100)                  │  │    GPU 3 (H100)                  │
│  │    NIC 0 (200Gbps IB)           │  │    NIC 1 (200Gbps IB)           │
│  │    NVMe 0-3                      │  │    NVMe 4-7                      │
│  │                                  │  │                                  │
│  │  <-- OUR POD PINNED HERE -->     │  │                                  │
│  │  (CPU cores 0-7, GPU 0,          │  │  (other workloads)               │
│  │   NIC 0, local DDR5)            │  │                                  │
│  └─────────────────────────────────┘  └─────────────────────────────────┘
│                                                                          │
│  WHY: Cross-NUMA memory access = +50-100ns latency per access.          │
│  At 20K TPS with multiple memory lookups per transaction,               │
│  NUMA-local execution saves 5-15% in tail latency.                      │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.5 Network Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│  NETWORK TOPOLOGY                                                         │
│                                                                           │
│  External Traffic (transactions, queries)                                 │
│       │                                                                   │
│       v                                                                   │
│  ┌──────────────┐                                                        │
│  │  L4 Load     │  TCP/TLS termination                                   │
│  │  Balancer    │  (HAProxy or Envoy)                                    │
│  └──────┬───────┘                                                        │
│         │                                                                 │
│         v                                                                 │
│  ┌──────────────┐                                                        │
│  │  Kubernetes   │  Service mesh (Istio sidecar)                         │
│  │  Ingress      │  mTLS, routing, rate limiting                         │
│  └──────┬───────┘                                                        │
│         │                                                                 │
│    ┌────┴────────────────────────┐                                       │
│    │                             │                                        │
│    v                             v                                        │
│  ┌──────────┐               ┌──────────┐                                │
│  │ Scorer    │ <--shared-->  │ TigerGraph│  (Graph cluster: 3-node HA)   │
│  │ Pods (x8) │    memory     │ Cluster   │                                │
│  └──────────┘               └──────────┘                                │
│    │                                                                      │
│    v                                                                      │
│  ┌──────────────┐                                                        │
│  │  Event Bus    │  (Kafka / Arrow Flight for Tier 2/3 consumers)        │
│  │  (async)      │                                                        │
│  └──────────────┘                                                        │
└──────────────────────────────────────────────────────────────────────────┘
```

---

# PART III: IMPLEMENTATION — C++ AND RUST DEVELOPMENT

---

## 7. C++ Implementation

### 7.1 FAISS Integration Layer

Our C++ code interfaces with FAISS for index management, search execution, and GPU lifecycle.

```cpp
// faiss_search_engine.h — Manages FAISS index lifecycle and search
#pragma once
#include <faiss/gpu/GpuIndexIVFPQ.h>
#include <faiss/gpu/StandardGpuResources.h>
#include <faiss/gpu/GpuIndexCagra.h>
#include <faiss/index_io.h>
#include <memory>
#include <vector>

class FaissSearchEngine {
public:
    struct Config {
        int dimension;
        int nlist;
        int m_subquantizers;
        int nbits;
        int nprobe;
        int top_k;
        int gpu_device_id;
        bool use_cuvs;
        bool use_cagra;
        size_t rmm_pool_size;
    };

    explicit FaissSearchEngine(const Config& config)
        : config_(config) {
        
        res_ = std::make_unique<faiss::gpu::StandardGpuResources>();
        if (config.rmm_pool_size > 0) {
            res_->setTempMemory(config.rmm_pool_size);
        }
        res_->setDefaultStream(config.gpu_device_id, cuda_stream_);
        
        if (config.use_cagra) {
            init_cagra();
        } else {
            init_ivfpq();
        }
    }

    // Search with device pointer (zero-copy from upstream GPU model)
    void search_gpu(const float* d_queries, int nq,
                    float* d_distances, int64_t* d_labels) {
        index_->search(nq, d_queries, config_.top_k,
                       d_distances, d_labels);
    }

    // Search with host pointer (fallback)
    void search_cpu(const float* h_queries, int nq,
                    std::vector<float>& distances,
                    std::vector<int64_t>& labels) {
        distances.resize(nq * config_.top_k);
        labels.resize(nq * config_.top_k);
        index_->search(nq, h_queries, config_.top_k,
                       distances.data(), labels.data());
    }

    // Hot-reload index without downtime (blue-green swap)
    void reload_index(const std::string& index_path) {
        auto new_index = load_and_transfer(index_path);
        std::lock_guard<std::mutex> lock(swap_mutex_);
        index_ = std::move(new_index);
    }

private:
    void init_ivfpq() {
        faiss::gpu::GpuIndexIVFPQConfig gpu_config;
        gpu_config.device = config_.gpu_device_id;
        gpu_config.interleavedLayout = config_.use_cuvs;

        auto idx = std::make_unique<faiss::gpu::GpuIndexIVFPQ>(
            res_.get(), config_.dimension, config_.nlist,
            config_.m_subquantizers, config_.nbits,
            faiss::METRIC_L2, gpu_config);
        idx->nprobe = config_.nprobe;
        index_ = std::move(idx);
    }

    void init_cagra() {
        faiss::gpu::GpuIndexCagraConfig cagra_config;
        cagra_config.device = config_.gpu_device_id;
        cagra_config.intermediate_graph_degree = 64;
        cagra_config.graph_degree = 32;

        index_ = std::make_unique<faiss::gpu::GpuIndexCagra>(
            res_.get(), config_.dimension,
            faiss::METRIC_INNER_PRODUCT, cagra_config);
    }

    std::unique_ptr<faiss::gpu::GpuIndex> load_and_transfer(
            const std::string& path) {
        auto cpu_index = std::unique_ptr<faiss::Index>(
            faiss::read_index(path.c_str()));
        faiss::gpu::GpuClonerOptions clone_opts;
        clone_opts.useFloat16 = false;
        return std::unique_ptr<faiss::gpu::GpuIndex>(
            dynamic_cast<faiss::gpu::GpuIndex*>(
                faiss::gpu::index_cpu_to_gpu(
                    res_.get(), config_.gpu_device_id,
                    cpu_index.get(), &clone_opts)));
    }

    Config config_;
    std::unique_ptr<faiss::gpu::StandardGpuResources> res_;
    std::unique_ptr<faiss::gpu::GpuIndex> index_;
    cudaStream_t cuda_stream_;
    std::mutex swap_mutex_;
};
```

### 7.2 CUDA Kernel Patterns

```cpp
// Custom CUDA kernels for search pipeline operations

// Pattern 1: Feature extraction — raw features to normalized embedding-ready format
__global__ void extract_features_kernel(
    const float* __restrict__ raw_features,
    float* __restrict__ normalized,
    const float* __restrict__ mean,
    const float* __restrict__ inv_std,
    int batch_size, int raw_dim, int feat_dim) {
    
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int batch_idx = tid / feat_dim;
    int feat_idx = tid % feat_dim;
    
    if (batch_idx < batch_size && feat_idx < feat_dim) {
        float val = raw_features[batch_idx * raw_dim + feat_idx];
        normalized[batch_idx * feat_dim + feat_idx] = 
            (val - mean[feat_idx]) * inv_std[feat_idx];
    }
}

// Pattern 2: CUDA Graph — capture entire scoring pipeline as single launchable graph
class ScoringPipelineGraph {
public:
    void capture(cudaStream_t stream, int batch_size) {
        cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal);
        
        int threads = 256;
        int blocks = (batch_size * feat_dim_ + threads - 1) / threads;
        extract_features_kernel<<<blocks, threads, 0, stream>>>(
            d_raw_features_, d_normalized_, d_mean_, d_inv_std_,
            batch_size, raw_dim_, feat_dim_);
        
        // ONNX Runtime inference on same CUDA stream
        ort_session_->RunAsync(stream, d_normalized_, d_embedding_);
        
        // L2 normalization for cosine similarity
        normalize_l2_kernel<<<blocks2, threads, 0, stream>>>(
            d_embedding_, batch_size, embed_dim_);
        
        cudaStreamEndCapture(stream, &graph_);
        cudaGraphInstantiate(&exec_graph_, graph_, nullptr, nullptr, 0);
        captured_ = true;
    }

    void execute(cudaStream_t stream) {
        cudaGraphLaunch(exec_graph_, stream);
    }

private:
    cudaGraph_t graph_;
    cudaGraphExec_t exec_graph_;
    bool captured_ = false;
};

// Pattern 3: Pinned memory pool — async H2D transfers without per-request alloc
class PinnedMemoryPool {
public:
    PinnedMemoryPool(size_t pool_size) {
        cudaMallocHost(&pool_, pool_size);
        write_offset_ = 0;
        capacity_ = pool_size;
    }
    
    ~PinnedMemoryPool() { cudaFreeHost(pool_); }

    void* allocate(size_t size, size_t align = 64) {
        size_t aligned = (write_offset_ + align - 1) & ~(align - 1);
        void* ptr = static_cast<char*>(pool_) + aligned;
        write_offset_ = aligned + size;
        return ptr;
    }

    void reset() { write_offset_ = 0; }

private:
    void* pool_;
    size_t write_offset_;
    size_t capacity_;
};

// Pattern 4: Persistent kernel — multi-query processing without host roundtrips
__global__ void persistent_knn_kernel(
    const float* __restrict__ queries,
    const float* __restrict__ database,
    float* __restrict__ distances,
    int64_t* __restrict__ indices,
    int num_queries, int num_db, int d, int k,
    volatile int* query_counter) {
    
    // Grid-stride loop: thread blocks process multiple queries
    while (true) {
        int query_idx = atomicAdd((int*)query_counter, 1);
        if (query_idx >= num_queries) break;
        
        extern __shared__ float shared_query[];
        for (int i = threadIdx.x; i < d; i += blockDim.x) {
            shared_query[i] = queries[query_idx * d + i];
        }
        __syncthreads();
        
        // Compute distances and top-k (simplified)
        // Real: shared memory tiling, warp-level reduction, partial sort
    }
}
```

### 7.3 ONNX Runtime Integration (C++)

```cpp
// onnx_model_server.h — In-process ONNX model serving with GPU
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

class OnnxModelServer {
public:
    OnnxModelServer(const std::string& model_path, int gpu_device_id) {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(4);
        opts.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);
        
        OrtCUDAProviderOptions cuda_opts;
        cuda_opts.device_id = gpu_device_id;
        cuda_opts.arena_extend_strategy = 1;
        cuda_opts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchDefault;
        opts.AppendExecutionProvider_CUDA(cuda_opts);
        
        session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts);
    }

    // Input stays on GPU, output stays on GPU — zero host transfers
    void run(const float* d_input, int batch_size, int input_dim,
             float* d_output) {
        
        Ort::MemoryInfo mem_info("Cuda", OrtDeviceAllocator, 0, 
                                 OrtMemTypeDefault);
        
        std::array<int64_t, 2> input_shape = {batch_size, input_dim};
        auto input_tensor = Ort::Value::CreateTensor(
            mem_info, const_cast<float*>(d_input),
            batch_size * input_dim, input_shape.data(), 2);
        
        const char* input_names[] = {"input"};
        const char* output_names[] = {"embedding"};
        
        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1);
        
        float* output_data = outputs[0].GetTensorMutableData<float>();
        cudaMemcpyAsync(d_output, output_data,
                        batch_size * embed_dim_ * sizeof(float),
                        cudaMemcpyDeviceToDevice, stream_);
    }

private:
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "scorer"};
    std::unique_ptr<Ort::Session> session_;
    cudaStream_t stream_;
    int embed_dim_ = 768;
};
```

### 7.4 Apache Arrow Integration (C++)

```cpp
// arrow_bridge.h — Zero-copy Arrow buffers for cross-component data flow
#include <arrow/api.h>
#include <arrow/c/bridge.h>

class ArrowBridge {
public:
    // Wrap raw FAISS output as Arrow RecordBatch — no copy
    std::shared_ptr<arrow::RecordBatch> wrap_faiss_results(
            float* distances, int64_t* labels, int nq, int k) {
        
        auto dist_buf = arrow::Buffer::Wrap(distances, nq * k * sizeof(float));
        auto label_buf = arrow::Buffer::Wrap(labels, nq * k * sizeof(int64_t));
        
        auto dist_array = std::make_shared<arrow::FloatArray>(nq * k, dist_buf);
        auto label_array = std::make_shared<arrow::Int64Array>(nq * k, label_buf);
        
        auto schema = arrow::schema({
            arrow::field("distance", arrow::float32()),
            arrow::field("label", arrow::int64())
        });
        
        return arrow::RecordBatch::Make(schema, nq * k,
                                        {dist_array, label_array});
    }

    // Export via C Data Interface for Rust consumption
    void export_to_c_interface(
            std::shared_ptr<arrow::RecordBatch> batch,
            struct ArrowArray* out_array,
            struct ArrowSchema* out_schema) {
        arrow::ExportRecordBatch(*batch, out_array, out_schema);
    }

    // Import from Rust via C Data Interface
    std::shared_ptr<arrow::RecordBatch> import_from_c_interface(
            struct ArrowArray* array,
            struct ArrowSchema* schema) {
        auto result = arrow::ImportRecordBatch(array, schema);
        return result.ValueOrDie();
    }
};
```

---

## 8. Rust Implementation

### 8.1 Gateway Architecture

```rust
// gateway/src/main.rs — Per-core worker architecture
use std::sync::atomic::{AtomicUsize, Ordering};

/// Each CPU core gets its own worker — no cross-core synchronization on hot path
struct CoreWorker {
    core_id: usize,
    arena: PipelineArena,
    faiss_engine: FaissClient,
    graph_client: TigerGraphClient,
    ring: SPSCRing,
    pinned_pool: PinnedPool,
}

impl CoreWorker {
    /// Process a single transaction — entire hot path on one core
    fn process(&mut self, raw_bytes: &[u8]) -> ScoringResult {
        // Phase 1: Zero-alloc parse (borrowed from network slab)
        let txn = TxnView::parse(raw_bytes);
        
        // Phase 2: Extract features into arena (cacheline-aligned)
        let feature_offset = self.arena.allocate_aligned(
            std::mem::size_of::<FeatureBlock>(), 64);
        let features = self.arena.write_at::<FeatureBlock>(feature_offset);
        features.populate_from(&txn);
        
        // Phase 3: GPU embedding + FAISS search (via C FFI)
        let embedding_offset = self.arena.allocate_aligned(128 * 4, 64);
        self.faiss_engine.embed_and_search(
            features.as_raw_ptr(),
            self.arena.ptr_at(embedding_offset),
        );
        
        // Phase 4: Graph query (results written to arena)
        let graph_offset = self.arena.allocate_aligned(
            std::mem::size_of::<GraphFeatures>(), 64);
        self.graph_client.query_fraud_ring(
            &txn.card_id,
            self.arena.ptr_at(graph_offset),
        );
        
        // Phase 5: Ensemble scoring (reads all features from arena)
        let score = self.score_ensemble(feature_offset, 
                                         embedding_offset, 
                                         graph_offset);
        
        // Phase 6: Push descriptor to consumer ring
        let desc = Descriptor {
            arena_offset: feature_offset as u32,
            payload_len: std::mem::size_of::<FeatureBlock>() as u32,
            score,
            timestamp: timestamp_ns(),
            stage_mask: 0xFF,
        };
        self.ring.push(desc);
        
        ScoringResult { score, latency_ns: elapsed() }
    }
}
```

### 8.2 Zero-Copy Transaction Parsing

```rust
// gateway/src/parser.rs — Zero-allocation ISO 8583 parsing

/// Borrows directly from network receive buffer — no heap allocation
/// Lifetime 'a tied to network slab (valid for request duration)
pub struct TxnView<'a> {
    pub card_id: &'a [u8],
    pub amount: f64,
    pub merchant_id: &'a [u8],
    pub merchant_name: &'a [u8],
    pub terminal_id: &'a [u8],
    pub timestamp: u64,
    pub mcc: u16,
    pub device_fingerprint: &'a [u8],
    raw: &'a [u8],
}

impl<'a> TxnView<'a> {
    /// Parse ISO 8583 in-place — all fields are borrowed references
    pub fn parse(raw: &'a [u8]) -> Self {
        let bitmap = &raw[4..20];
        let mut offset = 20;
        let mut view = TxnView {
            card_id: &[],
            amount: 0.0,
            merchant_id: &[],
            merchant_name: &[],
            terminal_id: &[],
            timestamp: 0,
            mcc: 0,
            device_fingerprint: &[],
            raw,
        };
        
        // Walk bitmap, extract fields by reference (not copy)
        if bit_set(bitmap, 2) {
            let (field, new_offset) = extract_llvar(raw, offset);
            view.card_id = field;
            offset = new_offset;
        }
        // ... remaining fields ...
        
        view
    }
}

/// Network receive buffer — slab-allocated, reused across requests
pub struct NetworkSlab {
    buffer: Box<[u8; 64 * 1024]>,
    active: bool,
}
```

### 8.3 SPSC Ring Buffer (Lock-Free)

```rust
// gateway/src/ring.rs — Single-Producer Single-Consumer lock-free ring

use std::sync::atomic::{AtomicUsize, Ordering::*};

/// 32-byte descriptor — carries metadata, not payloads
#[repr(C, align(64))]
#[derive(Clone, Copy)]
pub struct Descriptor {
    pub arena_offset: u32,
    pub payload_len: u32,
    pub score: f32,
    pub timestamp: u64,
    pub stage_mask: u8,
    pub flags: u8,
    _padding: [u8; 10],
}

/// SPSC ring — lock-free, cache-line padded to prevent false sharing
#[repr(C)]
pub struct SPSCRing {
    buffer: *mut Descriptor,
    mask: usize,
    
    // Producer state (own cache line)
    tail: AtomicUsize,
    _pad_tail: [u8; 56],
    
    // Consumer state (own cache line)
    head: AtomicUsize,
    _pad_head: [u8; 56],
}

impl SPSCRing {
    pub fn new(capacity: usize) -> Self {
        assert!(capacity.is_power_of_two());
        let layout = std::alloc::Layout::array::<Descriptor>(capacity).unwrap();
        let buffer = unsafe { std::alloc::alloc_zeroed(layout) as *mut Descriptor };
        
        SPSCRing {
            buffer,
            mask: capacity - 1,
            tail: AtomicUsize::new(0),
            _pad_tail: [0; 56],
            head: AtomicUsize::new(0),
            _pad_head: [0; 56],
        }
    }

    /// Producer: push descriptor (returns false if full)
    pub fn push(&self, desc: Descriptor) -> bool {
        let tail = self.tail.load(Relaxed);
        let head = self.head.load(Acquire);
        
        if tail - head >= self.mask + 1 {
            return false;
        }
        
        unsafe {
            *self.buffer.add(tail & self.mask) = desc;
        }
        self.tail.store(tail + 1, Release);
        true
    }

    /// Consumer: pop descriptor (returns None if empty)
    pub fn pop(&self) -> Option<Descriptor> {
        let head = self.head.load(Relaxed);
        let tail = self.tail.load(Acquire);
        
        if head >= tail {
            return None;
        }
        
        let desc = unsafe { *self.buffer.add(head & self.mask) };
        self.head.store(head + 1, Release);
        Some(desc)
    }

    pub fn len(&self) -> usize {
        let tail = self.tail.load(Relaxed);
        let head = self.head.load(Relaxed);
        tail.wrapping_sub(head)
    }
}

unsafe impl Send for SPSCRing {}
unsafe impl Sync for SPSCRing {}
```

### 8.4 Shared Memory Arena

```rust
// gateway/src/arena.rs — Hugepage-backed shared memory arena

use std::sync::atomic::{AtomicU64, Ordering::*};

/// Arena allocator backed by hugepages (2MB or 1GB)
pub struct PipelineArena {
    base: *mut u8,
    capacity: usize,
    write_offset: AtomicU64,
}

impl PipelineArena {
    pub fn new(size: usize) -> std::io::Result<Self> {
        use libc::{mmap, MAP_ANONYMOUS, MAP_HUGETLB, MAP_PRIVATE,
                   PROT_READ, PROT_WRITE};
        
        let ptr = unsafe {
            mmap(
                std::ptr::null_mut(),
                size,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                -1,
                0,
            )
        };
        
        if ptr == libc::MAP_FAILED {
            return Err(std::io::Error::last_os_error());
        }
        
        Ok(PipelineArena {
            base: ptr as *mut u8,
            capacity: size,
            write_offset: AtomicU64::new(0),
        })
    }

    /// Bump-allocate with alignment guarantee
    pub fn allocate_aligned(&self, size: usize, align: usize) -> usize {
        loop {
            let current = self.write_offset.load(Relaxed);
            let aligned = (current as usize + align - 1) & !(align - 1);
            let new_offset = aligned + size;
            
            if new_offset > self.capacity {
                panic!("Arena exhausted");
            }
            
            if self.write_offset
                .compare_exchange_weak(
                    current, new_offset as u64, Release, Relaxed)
                .is_ok()
            {
                return aligned;
            }
        }
    }

    pub fn ptr_at(&self, offset: usize) -> *mut u8 {
        unsafe { self.base.add(offset) }
    }

    pub fn write_at<T: Copy>(&self, offset: usize) -> &mut T {
        unsafe { &mut *(self.base.add(offset) as *mut T) }
    }

    pub fn read_at<T: Copy>(&self, offset: usize) -> &T {
        unsafe { &*(self.base.add(offset) as *const T) }
    }

    pub fn reset(&self) {
        self.write_offset.store(0, Release);
    }
}

impl Drop for PipelineArena {
    fn drop(&mut self) {
        unsafe { libc::munmap(self.base as *mut libc::c_void, self.capacity); }
    }
}

unsafe impl Send for PipelineArena {}
unsafe impl Sync for PipelineArena {}
```

### 8.5 FAISS C FFI Bindings (Rust)

```rust
// gateway/src/faiss_ffi.rs — Safe Rust wrapper over FAISS C API

use std::ffi::CString;

#[link(name = "faiss_c")]
extern "C" {
    fn faiss_IndexFlatL2_new(p_index: *mut *mut FaissIndex, d: i64) -> i32;
    fn faiss_Index_add(index: *mut FaissIndex, n: i64, x: *const f32) -> i32;
    fn faiss_Index_search(
        index: *const FaissIndex, n: i64, x: *const f32,
        k: i64, distances: *mut f32, labels: *mut i64,
    ) -> i32;
    fn faiss_read_index_fname(
        fname: *const libc::c_char, io_flags: i32,
        p_out: *mut *mut FaissIndex,
    ) -> i32;
    fn faiss_Index_free(index: *mut FaissIndex);
}

#[repr(C)]
pub struct FaissIndex { _opaque: [u8; 0] }

/// Safe wrapper with lifetime management and error handling
pub struct FaissClient {
    index: *mut FaissIndex,
    dimension: usize,
    top_k: usize,
}

impl FaissClient {
    pub fn load(path: &str, top_k: usize, dimension: usize) -> Result<Self, String> {
        let c_path = CString::new(path).map_err(|e| e.to_string())?;
        let mut index: *mut FaissIndex = std::ptr::null_mut();
        
        let rc = unsafe {
            faiss_read_index_fname(c_path.as_ptr(), 0, &mut index)
        };
        
        if rc != 0 {
            return Err(format!("FAISS load failed: code {}", rc));
        }
        
        Ok(FaissClient { index, dimension, top_k })
    }

    pub fn search(
        &self, queries: &[f32], nq: usize,
        distances: &mut [f32], labels: &mut [i64],
    ) -> Result<(), String> {
        assert_eq!(queries.len(), nq * self.dimension);
        assert!(distances.len() >= nq * self.top_k);
        assert!(labels.len() >= nq * self.top_k);
        
        let rc = unsafe {
            faiss_Index_search(
                self.index, nq as i64, queries.as_ptr(),
                self.top_k as i64, distances.as_mut_ptr(),
                labels.as_mut_ptr(),
            )
        };
        
        if rc != 0 {
            return Err(format!("FAISS search failed: code {}", rc));
        }
        Ok(())
    }
}

impl Drop for FaissClient {
    fn drop(&mut self) {
        if !self.index.is_null() {
            unsafe { faiss_Index_free(self.index); }
        }
    }
}

unsafe impl Send for FaissClient {}
```

### 8.6 Arrow C Data Interface (Rust to C++)

```rust
// gateway/src/arrow_bridge.rs — Zero-copy Arrow sharing with C++ components

use arrow::ffi::{FFI_ArrowArray, FFI_ArrowSchema};
use arrow::array::{Float32Array, Int64Array};
use arrow::record_batch::RecordBatch;

/// Import Arrow RecordBatch from C++ (zero-copy — shares same buffers)
pub unsafe fn import_from_cpp(
    array: *mut FFI_ArrowArray,
    schema: *mut FFI_ArrowSchema,
) -> RecordBatch {
    let imported_array = arrow::ffi::from_ffi(
        std::ptr::read(array),
        &std::ptr::read(schema),
    ).expect("Arrow import failed");
    
    RecordBatch::from(imported_array)
}

/// Export Rust Arrow data to C++
pub fn export_to_cpp(
    batch: &RecordBatch,
    out_array: *mut FFI_ArrowArray,
    out_schema: *mut FFI_ArrowSchema,
) {
    let (array, schema) = arrow::ffi::to_ffi(
        &batch.clone().into()
    ).expect("Arrow export failed");
    
    unsafe {
        std::ptr::write(out_array, array);
        std::ptr::write(out_schema, schema);
    }
}

/// Wrap raw FAISS results (in arena memory) as Arrow arrays — true zero-copy
pub unsafe fn wrap_faiss_results_as_arrow(
    distances_ptr: *const f32,
    labels_ptr: *const i64,
    count: usize,
) -> RecordBatch {
    let dist_buffer = arrow::buffer::Buffer::from_raw_parts(
        std::ptr::NonNull::new(distances_ptr as *mut u8).unwrap(),
        count * std::mem::size_of::<f32>(),
        count * std::mem::size_of::<f32>(),
    );
    
    let label_buffer = arrow::buffer::Buffer::from_raw_parts(
        std::ptr::NonNull::new(labels_ptr as *mut u8).unwrap(),
        count * std::mem::size_of::<i64>(),
        count * std::mem::size_of::<i64>(),
    );
    
    let distances = Float32Array::new(dist_buffer.into(), None);
    let labels = Int64Array::new(label_buffer.into(), None);
    
    let schema = arrow::datatypes::Schema::new(vec![
        arrow::datatypes::Field::new("distance",
            arrow::datatypes::DataType::Float32, false),
        arrow::datatypes::Field::new("label",
            arrow::datatypes::DataType::Int64, false),
    ]);
    
    RecordBatch::try_new(
        std::sync::Arc::new(schema),
        vec![std::sync::Arc::new(distances), std::sync::Arc::new(labels)],
    ).unwrap()
}
```

---

# PART IV: USE CASES (REBUILT STORIES)

---

## 9. Use Case 1: Real-Time Fraud Detection (CapitalOne)

### 9.1 Problem Statement

Traditional rule-based fraud detection missed 15-20% of sophisticated fraud patterns — particularly organized rings that split transactions across multiple cards, merchants, and time windows. Single-transaction ML models improved individual scoring but remained blind to graph-level patterns. The system processed 20,000+ transactions per second with a hard < 5ms p99 latency SLA.

### 9.2 Architecture Choices and Rationale

| Decision | Choice | Alternative | Why |
|----------|--------|-------------|-----|
| Scoring latency | In-process pipeline | Microservices (gRPC) | 5ms SLA = no network budget |
| Vector search | FAISS GPU (IVF-PQ) | Milvus, ES kNN | In-process = 0.3ms; Milvus adds 5ms+ |
| Graph queries | TigerGraph | Neo4j, custom | Sub-2ms 2-hop on billion edges |
| Data flow | Shared memory arena | Kafka, gRPC | Zero-copy eliminates 40-80ms serialization |
| Language (gateway) | Rust | Go, Java | No GC pauses; safe zero-copy |
| Language (ML/FAISS) | C++ | Python | In-process GPU; no GIL |
| Ensemble model | XGBoost (CPU) | Neural network (GPU) | Sub-1ms; doesn't need GPU |
| Embedding model | DistilBERT (ONNX) | Custom MLP | Better generalization; portable |
| GPU | H100 (MIG slice) | A100, full H100 | Isolation without wasting 80GB |

### 9.3 Implementation Flow

```
Transaction arrives (ISO 8583 over TCP)
    │
    v
[1] Rust Gateway — parse in-place
    • TxnView<'a> borrows from network slab
    • Zero heap allocations
    • Fields: card_id, amount, merchant, device, timestamp
    │
    v
[2] Feature Extraction — write to arena
    • FeatureBlock at 64-byte aligned offset
    • Numerical: amount_log, time_sin/cos, velocity
    • Categorical: merchant_mcc, device_risk_score
    • Pre-computed: card_embedding[64], merchant_embedding[64]
    │
    ├──────────────────────────────────────────────┐
    │                                              │
    v                                              v
[3a] FAISS GPU Search (0.3ms)           [3b] TigerGraph Query (1.8ms)
    • 128d embedding from FeatureBlock        • 2-hop from card vertex
    • Passed to GPU (stays on device)         • Shared devices/IPs/addresses
    • IVF-PQ: 10M vectors, nprobe=32         • Returns: ring_size, velocity,
    • Returns: 10 nearest + distances           shared_device_count, etc.
    • Distances written to arena              • Graph features to arena
    │                                              │
    └──────────────────┬───────────────────────────┘
                       │
                       v
[4] XGBoost Ensemble (0.9ms)
    • Reads from arena: features + FAISS distances + graph features
    • 8 trees, max_depth=6
    • Output: fraud probability [0, 1]
    │
    v
[5] Decision + Event
    • score > threshold -> DECLINE
    • Push 32-byte descriptor to SPSC ring
    • Arrow RecordBatch wraps arena -> Tier 2/3 consumers
    │
    v
Total: 3.8ms p99
```

### 9.4 Results

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Fraud detection rate | 82% | 94% | +12 percentage points |
| False positive rate | 5.1% | 3.2% | -37% |
| P99 latency | 120ms (microservices) | 3.8ms | 31x faster |
| Throughput | 8,000 TPS | 24,500 TPS | 3x |
| Graph-only catches | 0% | 8% of all fraud | New capability |
| Bytes copied per txn | ~4 KB (protobuf) | 47 bytes | 85x reduction |

---

## 10. Use Case 2: Conversational AI Entity Resolution (Siri)

### 10.1 Problem Statement

The Siri conversational AI pipeline (ASR -> NLU -> Search -> Ranking -> TTS) allocated < 25ms for Stage C (NLU + Search + Ranking). The legacy system used dictionary lookups and regex, achieving only 84% accuracy. It could not handle multi-turn context, ambiguous entities, or semantic matching.

### 10.2 Architecture Choices and Rationale

| Decision | Choice | Alternative | Why |
|----------|--------|-------------|-----|
| On-device index | FAISS HNSW (CPU) | Flat, IVF | Best recall/speed on CPU; 100K fits |
| Cloud index | FAISS CAGRA (GPU) | HNSW, IVF-PQ | GPU-native; 4.7x faster than CPU HNSW |
| NLU model | Multi-task DistilBERT | Separate models | One forward pass for 3 tasks; 8ms |
| Knowledge graph | TigerGraph | Neo4j | Sub-2ms; GSQL accumulators |
| Embedding dim | 768d | 128d, 256d | Higher = better quality; GPU handles cost |
| Re-ranking | Batched GPU scoring | Sequential CPU | All candidates in 1 pass; 0.8ms |
| State management | Shared memory arena | Redis, RPC | Zero-copy; latency critical |
| Hybrid deploy | CAGRA to HNSW export | Separate indexes | Build once on GPU; serve both |

### 10.3 Dual-Mode Architecture

**Cloud Path (10M entities, GPU):**
- Query embedding generated by NLU on GPU stays on GPU
- FAISS CAGRA search on same GPU: 0.5ms, top-50 candidates
- No device-to-host transfer between NLU and FAISS (saves 0.8ms)

**On-Device Path (100K contacts, CPU):**
- Contact embeddings precomputed and stored on device
- HNSW index: 768d, M=32, efSearch=128
- Memory: ~400 MB (fits iPhone with 8GB RAM)
- Latency: ~2ms for top-10

**Hybrid Build/Serve:**
- Build CAGRA index on GPU (12x faster than CPU HNSW build)
- Export to HNSW format: `gpu_index.copyTo(&cpu_hnsw)`
- Fast daily rebuilds on GPU cluster, serve on CPU fleet

### 10.4 Multi-Turn Context Resolution

```
Turn 1: "What's the weather in Springfield?"
  NLU: intent=WEATHER, entity=Springfield (ambiguous)
  FAISS: Returns Springfield {IL, MO, MA, OR}
  TigerGraph: User lives near Springfield IL (2-hop from home)
  Result: Springfield, IL (graph disambiguates)

Turn 2: "No, the one in Massachusetts"
  NLU: intent=CORRECTION, entity=Massachusetts
  Context Update: override_location = Springfield, MA
  No FAISS search needed — context override

Turn 3: "How far is it from here?"
  NLU: intent=DISTANCE, reference="it" (anaphora)
  Context: "it" = Springfield, MA from turn 2
  Result: Distance from current_location to Springfield, MA
```

### 10.5 Results

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Entity resolution accuracy | 84% | 96% | +12 percentage points |
| Stage C p99 latency | 65ms | 22ms | 3x faster |
| Search relevance (NDCG@10) | baseline | +26% | Significant |
| Multi-turn context accuracy | 0% (stateless) | 89% | New capability |
| On-device search latency | 15ms (dictionary) | 2ms (HNSW) | 7.5x faster |

---

# PART V: CHALLENGES, SOLUTIONS, AND RETROSPECTIVE

---

## 11. Challenges Faced and How We Solved Them

### 11.1 FAISS Index Staleness vs. Rebuild Cost

**Problem:** Fraud index needs recent transactions (freshness matters). Rebuilding 10M-vector IVF-PQ takes ~3s on GPU. During rebuild, old index serves stale results.

**Solution:** Blue-green index deployment. Two index instances in GPU memory; background rebuild; atomic pointer swap; no downtime.

**Memory cost:** 2x index memory during swap (~500 MB). Acceptable given 80 GB budget.

**Retrospective:** Brute-force `IndexFlatL2` on GPU would have been simpler for 10M vectors — no rebuild needed, just update the row. We over-optimized for memory that wasn't constrained.

### 11.2 TigerGraph Cold Start Latency

**Problem:** First query after pod startup: 200-500ms (JVM warmup). Subsequent: 1-2ms. First ~100 transactions violate SLA.

**Solution:** Warm-up in readiness probe — 1000 synthetic queries before marking pod ready. Took startup from 30s to 90s, but eliminated cold-start SLA violations.

**Retrospective:** A native C++ graph engine (DGraph, Kùzu) would avoid JVM warm-up entirely.

### 11.3 CUDA Context Switching Overhead

**Problem:** FAISS and ONNX Runtime creating separate CUDA contexts adds 50-200us per transition. At 20K TPS, significant.

**Solution:** Single shared CUDA context. Both libraries execute on the same stream sequentially. Eliminated context switch overhead entirely.

**Tradeoff:** Can't overlap ONNX and FAISS on same GPU. For our pipeline (ONNX produces embedding that FAISS searches), this is the natural dependency order.

### 11.4 Rust-C++ FFI Complexity

**Problem:** FAISS C API incomplete — doesn't expose GPU index types or CAGRA.

**Solution:** 500-line C++ shim (`search_bridge.cpp`) exposing our workflows as `extern "C"` functions. Rust calls via `#[link(name = "search_bridge")]`.

**Retrospective:** Would use `cxx` crate today — safer than raw FFI, supports C++ types directly.

### 11.5 Debugging Shared Memory Pipelines

**Problem:** Raw memory arena is invisible to standard debugging. Corruption in one stage silently poisons downstream.

**Solution:** Multi-layered: (1) magic bytes at allocation boundaries, (2) ring inspector CLI, (3) periodic Arrow IPC snapshots, (4) per-stage CRC32 checksums, (5) Prometheus latency histograms.

**Retrospective:** Design observability from day one. Embed type metadata in arena allocations (type tag, size, producing stage).

### 11.6 False Positives in Graph-Based Fraud Detection

**Problem:** Graph flagged legitimate shared devices (family tablets, corporate IPs). Ring_size > 5 had 40% false positive rate.

**Solution:** (1) Device sharing classifier (legitimate vs suspicious), (2) IP reputation scoring by type, (3) temporal pattern features, (4) let XGBoost learn decision boundary from all features instead of hard thresholds.

**Result:** False positive rate: initial 8% to final 3.2%.

### 11.7 On-Device FAISS Memory Pressure (iPhone)

**Problem:** 100K x 768d x 4 bytes = 307 MB + HNSW graph = 400 MB. Significant on 8 GB device.

**Solution:** (1) float16 storage (307 -> 153 MB), (2) lazy load/unload, (3) top-100 contacts in always-resident Flat index, (4) cloud fallback if memory unavailable.

**Retrospective:** Product Quantization on device (96 bytes/vector = 9.6 MB for 100K) with IVFPQFastScan would have been far smaller with acceptable quality.

---

## 12. What We Would Do Differently (Retrospective)

### 12.1 Technical Decisions

| Original Decision | Better Alternative | Why |
|-------------------|-------------------|-----|
| IVF-PQ for 10M fraud vectors | Flat (brute-force) on GPU | Fits easily; no rebuild; 100% recall |
| Custom scoring path | SGLang scoring-only mode | 2x our throughput; battle-tested |
| TigerGraph for both use cases | TigerGraph (fraud) + Neo4j (Siri) | Simpler schema doesn't need TG complexity |
| 768d on device | PQ-compressed (96 bytes/vector) | 6x memory reduction; acceptable recall |
| Bindgen for FAISS C API | cxx crate with C++ shim | Better type safety |
| Arena without headers | Arena with embedded type metadata | Saves weeks of debugging |
| ONNX for all models | ONNX (small) + SGLang (SLM) | Better batching for larger models |

### 12.2 Process Decisions

| Original | Better | Why |
|----------|--------|-----|
| Custom from day 1 | Start managed, customize for SLA-critical | Faster time-to-value |
| Optimize each component independently | Profile end-to-end first | Some optimizations unnecessary |
| Minimal observability initially | Design into shared memory from start | Raw memory debugging is expensive |
| Single large binary | Separate index-management process | Simpler blue-green lifecycle |

### 12.3 If Starting Today (2026)

1. **Vector search:** FAISS Flat on GPU for < 50M vectors. Only ANN when brute-force bottlenecks.
2. **LLM serving:** SGLang for any model > 100M parameters.
3. **Embeddings:** Open-source model with Matryoshka training (full dims on GPU, truncated on device).
4. **Graph:** In-process graph analytics (Raphtory, Kùzu) for fraud — eliminates network hop.
5. **Rust-GPU:** `cudarc` crate for direct CUDA without C++ intermediary.
6. **Observability:** OpenTelemetry end-to-end, including shared-memory stages.

---

# PART VI: APPENDIX

---

## A. GPU Hardware Reference (H100)

| Spec | Value |
|------|-------|
| Architecture | Hopper |
| FP32 Performance | ~60 TFLOPS |
| FP16/BF16 (Tensor Core) | ~990 TFLOPS |
| FP8 (Tensor Core) | ~1,979 TFLOPS |
| INT8 (Tensor Core) | ~1,979 TOPS |
| HBM3 Memory | 80 GB |
| Memory Bandwidth | 3.35 TB/s |
| L2 Cache | 50 MB |
| NVLink Bandwidth | 900 GB/s (bi-directional) |
| PCIe Gen5 Bandwidth | 128 GB/s |
| TDP | 700W |
| MIG Partitions | Up to 7 instances |

## B. Performance Cheat Sheet

| Operation | Latency | Context |
|-----------|---------|---------|
| L1 cache hit (GPU) | ~1ns | 256 KB per SM |
| L2 cache hit (GPU) | ~5ns | 50 MB shared |
| HBM3 read | ~100ns | 80 GB |
| CPU L1 cache | ~1ns | 32 KB per core |
| CPU L3 cache | ~10ns | 30-100 MB shared |
| DDR5 read | ~50-100ns | Per NUMA node |
| Cross-NUMA read | +50-100ns | Additional penalty |
| PCIe Gen5 transfer | ~100ns + size/128 GB/s | Latency + bandwidth |
| NVLink transfer | ~100ns + size/900 GB/s | GPU-to-GPU |
| Shared memory IPC | ~100-200ns | Same host |
| SPSC ring push/pop | ~10-50ns | Lock-free |
| FAISS GPU search (IVF-PQ, 10M) | ~300us | nprobe=32, top-10 |
| FAISS GPU search (Flat, 1M) | ~100us | Brute force |
| FAISS CAGRA search (10M) | ~500us | GPU graph index |
| FAISS CPU HNSW (100K) | ~2ms | efSearch=128 |
| TigerGraph 2-hop | ~1-2ms | Billion-edge graph |
| ONNX DistilBERT inference | ~8ms | Single input, GPU |
| XGBoost prediction | ~0.5-1ms | 8 trees, CPU |
| TCP loopback | ~10-50us | Same host |
| gRPC unary call | ~100-500us | With serialization |

## C. Build System Configuration

```cmake
# CMakeLists.txt for C++ scoring engine
cmake_minimum_required(VERSION 3.24)
project(search_scorer CUDA CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES "90")  # H100 (Hopper)

find_package(faiss REQUIRED)
find_package(onnxruntime REQUIRED)
find_package(Arrow REQUIRED)
find_package(CUDAToolkit REQUIRED)

add_library(search_bridge SHARED
    src/faiss_search_engine.cpp
    src/onnx_model_server.cpp
    src/arrow_bridge.cpp
    src/cuda_kernels.cu
)

target_link_libraries(search_bridge
    faiss
    onnxruntime::onnxruntime
    Arrow::arrow_shared
    CUDA::cudart
    CUDA::cublas
)

set_target_properties(search_bridge PROPERTIES
    PUBLIC_HEADER "include/search_bridge.h"
)
```

```toml
# Cargo.toml for Rust gateway
[package]
name = "fraud-gateway"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio = { version = "1", features = ["full"] }
arrow = { version = "52", features = ["ffi"] }
libc = "0.2"
prometheus = "0.13"

[build-dependencies]
cc = "1.0"

[[bin]]
name = "gateway"
path = "src/main.rs"
```

## D. Key Formulas Summary

| Formula | Use |
|---------|-----|
| BM25 = sum IDF(qi) * (f*(k1+1)) / (f + k1*(1-b+b*\|D\|/avgdl)) | Sparse retrieval scoring |
| InfoNCE = -log(exp(sim(q,d+)/t) / sum exp(sim(q,dk)/t)) | Contrastive learning loss |
| NDCG@K = DCG@K / IDCG@K | Ranking quality metric |
| DCG@K = sum (2^rel_i - 1) / log2(i+1) | Discounted cumulative gain |
| MRR = (1/\|Q\|) * sum 1/rank_i | First-correct-result metric |
| Recall@K = \|relevant intersect top-K\| / \|relevant\| | Retrieval completeness |
| Cosine(x,y) = (x*y) / (\|\|x\|\|*\|\|y\|\|) | Angular similarity |
| PQ memory = m bytes per vector | Compression ratio |
| IVF clusters = 4*sqrt(N) to 16*sqrt(N) | Rule of thumb for nlist |
| HNSW memory = d*4 + M*8 bytes per vector | Graph overhead |

---

*End of Guide*
