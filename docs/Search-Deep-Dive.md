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

# PART V-B: INTERVIEW Q&A — LINKEDIN SEARCH & FEED SYSTEMS

> **Coverage:** Tailored for Staff+ engineering interviews at LinkedIn's Search and Feed organization. Covers the full stack: retrieval (Galene/Lucene-based), feed ranking (multi-objective), Economic Graph, embedding-based candidate generation, online experimentation, responsible AI, and infrastructure at LinkedIn scale (~1B members, billions of connections, hundreds of millions of daily sessions).
>
> **LinkedIn-specific context:** LinkedIn operates Galene (custom search engine built on Lucene), a multi-stage feed ranking pipeline (First Pass → Second Pass → Re-ranking), the Economic Graph (members, companies, skills, jobs, content as interconnected entities), and a mature experimentation platform processing thousands of concurrent A/B tests.

---

## 13. Retrieval — LinkedIn Search (People, Jobs, Content)

### Q1: LinkedIn search spans people, jobs, content, and companies. How does retrieval differ across these verticals?

**A:** Each vertical has fundamentally different query semantics, corpus characteristics, and freshness requirements:

| Vertical | Corpus Size | Query Type | Key Retrieval Signal | Freshness |
|----------|-------------|-----------|---------------------|-----------|
| **People** | ~1B profiles | Navigational + exploratory | Name match, title, skills, network proximity | Slowly changing |
| **Jobs** | ~15M active | High-intent, faceted | Title/skill match, location, seniority, recency | Critical (new posts) |
| **Content** | Billions of posts | Topical, trending | Text relevance, engagement, author authority | Very high (hours) |
| **Companies** | ~60M pages | Navigational | Name exact match, industry, size | Slowly changing |

**Retrieval strategy per vertical:**
- **People search:** Heavily relies on structured fields (name, title, company) via inverted index + network distance from searcher (1st/2nd/3rd degree). Dense retrieval adds semantic skill matching ("ML engineer" finds "machine learning scientist").
- **Job search:** Hybrid BM25 (title/description keywords) + embedding similarity (latent skill-job match). Critical addition: **personalized retrieval** — user's skill vector dot-producted with job embedding to surface relevant roles even without explicit keyword match.
- **Content search:** Recency-weighted BM25 + engagement signals (viral posts boosted). Dense retrieval for topical queries. Challenge: corpus is enormous and rapidly growing — index partitioning by time is essential.
- **Company search:** Mostly navigational (user knows what they want). Prefix matching + popularity (follower count, visit frequency) dominates.

**Key LinkedIn-specific challenge:** Search results must respect the **social graph** — a recruiter searching "ML engineer" should see their 2nd-degree connections first, then broader network, then out-of-network. This requires real-time graph distance computation integrated into retrieval scoring.

---

### Q2: How does embedding-based retrieval (EBR) work at LinkedIn scale for candidate generation?

**A:** LinkedIn uses EBR across multiple products — People You May Know (PYMK), job recommendations, content recommendations, and search.

**Architecture (dual-tower at LinkedIn scale):**
```
Member Tower:                          Item Tower (job/content/person):
  [member_id embedding]                  [item_id embedding]
  [skills sequence]                      [required skills]
  [industry + seniority]                 [industry + seniority]
  [activity history]                     [content text / job description]
  [connection graph features]            [engagement features]
         ↓                                       ↓
  Transformer encoder                    Transformer encoder
         ↓                                       ↓
  member_emb ∈ ℝ^256                    item_emb ∈ ℝ^256
         
  score = member_emb · item_emb   (dot product, served via ANN)
```

**Training data at LinkedIn:**
- Positive pairs: (member, job they applied to), (member, content they engaged with), (member, connection they accepted)
- Hard negatives: Jobs the member viewed but didn't apply; people shown in PYMK but not connected
- In-batch negatives: Other positives in the batch (free, large-scale)

**Serving at scale:**
- 1B member embeddings pre-computed → stored in distributed index
- At query time: compute query embedding (member context + explicit query) → ANN search over item corpus
- LinkedIn uses **sharded indexes** — corpus partitioned by geography or industry for locality
- Retrieval returns top-500 to top-1000 candidates fed into ranking

**Key insight for LinkedIn:** The member tower must encode BOTH the member's static profile AND their recent activity (last 50 actions). This makes the query embedding session-aware without full online retraining.

---

### Q3: How does LinkedIn's search engine (Galene) differ from general-purpose search engines like Elasticsearch?

**A:** Galene is LinkedIn's custom search infrastructure, purpose-built for LinkedIn-specific requirements:

| Aspect | Galene (LinkedIn) | Elasticsearch |
|--------|-------------------|---------------|
| **Index structure** | Custom inverted index on Lucene core | Standard Lucene |
| **Graph integration** | Native social graph distance in scoring | Not built-in |
| **Real-time updates** | Near-real-time segment merge for profile changes | NRT with refresh interval |
| **Personalization** | Per-query graph features baked into scoring | Requires external pre-computation |
| **Federation** | Query fans out to vertical-specific indexes | Single cluster model |
| **Typeahead** | Specialized prefix index with fuzzy matching | Requires custom analyzer |
| **Scale** | Billions of documents across verticals | Typically millions per cluster |

**Why build custom (Galene) instead of using Elasticsearch:**
1. **Social graph scoring is first-class:** Every search result's score is modulated by network distance. In ES, this would require a script_score with external lookup per document — unacceptable at scale.
2. **Federated search:** A single query ("machine learning") searches people, jobs, companies, and content simultaneously with vertical-specific ranking, then blends results. ES would need separate clusters + custom federation logic.
3. **Early termination:** Galene can short-circuit evaluation when enough top-K candidates from the user's network are found — massive latency savings for navigational queries.
4. **Custom index layout:** LinkedIn's posting lists are enriched with graph metadata (connection degree, shared connections count) stored inline — eliminates runtime joins.

---

### Q4: What is hybrid retrieval at LinkedIn, and how do you fuse sparse + dense + graph signals?

**A:** LinkedIn retrieval is actually a **three-way hybrid** — not just sparse + dense:

```
Query: "senior ML engineer at FAANG"
    │
    ├──▶ [Sparse (Galene/BM25)] → title:ML, skills:machine_learning, company:FAANG
    │         Results: 50K members with keyword overlap
    │
    ├──▶ [Dense (EBR)] → query_embedding · member_embedding
    │         Results: 10K semantically similar profiles
    │         Catches: "Applied Scientist" (no keyword overlap with "ML engineer")
    │
    └──▶ [Graph (Network)] → 1st/2nd degree connections of searcher
              Results: 2K members in searcher's extended network
              Priority: Network-proximal results dominate top positions

Fusion: Weighted union → pre-ranking → full ranking
```

**Fusion strategies at LinkedIn:**
1. **Network-aware RRF:** Standard RRF but with connection-degree boost: 1st-degree connections get 3x weight multiplier in fusion score.
2. **Learned fusion (preferred):** A lightweight model takes (BM25_score, dense_score, connection_degree, shared_connections, shared_companies) and outputs a unified retrieval score. Trained on click data.
3. **Source-specific recall constraints:** Ensure at least 20% of candidates come from dense retrieval (catches vocabulary mismatch) and at least 30% from user's network (user preference for familiar results).

**LinkedIn-specific challenge — network proximity computation at retrieval time:**
Computing 2nd-degree distance for 50K sparse retrieval results requires checking if each result is a connection-of-connection. At LinkedIn's graph size (1B members, 15B+ edges), this is done via pre-computed Bloom filters or graph shards co-located with the search index.

---

### Q5: How do you handle real-time index updates for LinkedIn's rapidly changing corpus?

**A:** LinkedIn's corpus has very different update patterns per vertical:

| Vertical | Update Frequency | Challenge |
|----------|-----------------|-----------|
| Profiles | ~10M changes/day (title, skills) | Must reflect job changes quickly |
| Jobs | ~1M new posts/day, 500K expire/day | Freshness critical (stale jobs = bad UX) |
| Content/Posts | ~2M new posts/day | Real-time indexing for trending content |
| Connections | ~50M new edges/day | Graph structure changes affect scoring |

**Galene's update architecture:**
```
[Kafka: profile/job/content change events]
         ↓
[Near-Real-Time Indexer]
    • Writes to in-memory segment (immediately searchable)
    • Background merge into optimized on-disk segments
    • Replication to replicas within seconds
         ↓
[Periodic Full Rebuild (weekly)]
    • Re-index entire corpus with latest embeddings
    • Rebuild ANN indexes from scratch (ensures centroid freshness)
    • Zero-downtime via blue-green segment swap
```

**For embedding-based retrieval (EBR) freshness:**
- Profile embedding changes when member updates skills/title → re-embed and update vector index
- Challenge: re-embedding 10M profiles/day with a BERT-class model is expensive
- Solution: **incremental re-embedding** — only re-embed profiles with significant changes (new job, new skills), use cached embedding for minor edits (summary rewording)
- Embedding index uses **appendix buffer pattern**: main ANN index (rebuilt nightly) + small brute-force buffer for today's changes

---

### Q6: What is query understanding at LinkedIn, and how does it feed into retrieval?

**A:** LinkedIn query understanding is richer than general web search because queries have professional context:

**Pipeline:**
```
Raw Query: "senior ML engineer at Google in Bay Area"
    │
    ├── [Intent Classification] → search_people (vs search_jobs, search_content)
    │
    ├── [Named Entity Recognition]
    │       • title: "senior ML engineer"
    │       • company: "Google"
    │       • location: "Bay Area"
    │       • seniority: "senior"
    │
    ├── [Query Expansion]
    │       • "ML engineer" → also match: "machine learning engineer",
    │         "applied scientist", "ML scientist"
    │       • "Google" → also match: "Alphabet", "DeepMind", "Waymo"
    │
    ├── [Skill Extraction (from title)]
    │       • "ML engineer" → skills: [machine_learning, deep_learning,
    │         python, tensorflow]
    │
    └── [Personalization Context]
            • Searcher is a recruiter → emphasize candidate availability
            • Searcher is in same industry → boost industry relevance
```

**How it feeds retrieval:**
- Structured entities → exact match in inverted index (high precision)
- Expanded queries → broader recall from synonym/skill matching
- Skill vectors → dense retrieval with skill-based embeddings
- Intent → selects which vertical index to query (or blends multiple)

**LinkedIn-specific challenges:**
- **Ambiguity:** "Apple" — company or skill (apple ecosystem)? Context from searcher's industry disambiguates.
- **Title normalization:** "VP of Engineering" vs "Vice President, Engineering" vs "VPE" → same canonical title. LinkedIn maintains a title taxonomy (~15K canonical titles) for matching.
- **Skill taxonomy:** LinkedIn's skill graph (40K+ skills with relationships) powers query expansion. "React" → also matches "React.js", "ReactJS", "frontend development."

---

---

## 14. LinkedIn Feed Ranking — Multi-Objective Optimization

### Q7: Walk through LinkedIn's feed ranking pipeline. What are the stages and objectives?

**A:** The choice depends on hardware, update frequency, recall requirements, and latency budget.

| Factor | IVF-PQ | HNSW | CAGRA |
|--------|--------|------|-------|
| **Best on** | GPU (batch queries) | CPU (single queries) | GPU (native) |
| **Build time** | Fast (k-means + PQ training) | Slow (sequential inserts) | Fast (GPU-parallel) |
| **Memory** | Lowest (m bytes/vector) | Highest (d*4 + M*8/vector) | Medium (graph) |
| **Recall@10 (10M)** | 85-95% (nprobe tunable) | 95-99% (efSearch tunable) | 95-99% |
| **Latency (10M)** | 0.1-0.5ms (GPU batch) | 1-5ms (CPU) | 0.3-1ms (GPU) |
| **Updates** | Requires rebuild | Supports incremental add | Requires rebuild |
| **Sweet spot** | >10M vectors, GPU available | <10M vectors, CPU, needs updates | GPU-native pipelines |

**My decision process:**
1. Corpus < 1M vectors? **Flat (brute-force) on GPU** — 100% recall, still sub-ms.
2. Corpus 1-50M, GPU available, infrequent updates? **CAGRA** (or Flat if latency allows).
3. Corpus 1-50M, CPU, needs online updates? **HNSW**.
4. Corpus >100M, memory-constrained? **IVF-PQ** (compression) or **IVF-HNSW** hybrid.
5. On-device (mobile)? **HNSW** (CPU) or **PQ + IVFFlat** for memory savings.

---

### Q4: What is hybrid retrieval, and how do you fuse sparse + dense results?

**A:** Hybrid retrieval runs both BM25 (sparse) and embedding search (dense) in parallel, then merges candidates. This captures both exact-match strength of BM25 and semantic matching of dense retrieval.

**Fusion strategies:**
1. **Reciprocal Rank Fusion (RRF):** `score(d) = sum(1 / (k + rank_i(d)))` across retrieval methods. Simple, no training needed, robust. k=60 is standard.
2. **Linear combination:** `score = α * BM25_norm(d) + (1-α) * dense_sim(d)`. Requires tuning α (typically 0.3-0.7 depending on query type).
3. **Learned fusion:** A lightweight model (logistic regression or small NN) takes both scores as features and predicts relevance. Best quality but needs training data.
4. **Query-adaptive routing:** Classify query type first — entity lookups → BM25 only; semantic queries → dense only; ambiguous → hybrid. Saves compute on obvious cases.

**In our fraud system:** We don't use BM25 (no text query). But the principle applies — we fuse FAISS vector similarity scores with TigerGraph structural scores. The XGBoost ensemble acts as the learned fusion layer, seeing both signal types as input features.

---

### Q5: How do you handle index freshness vs. rebuild cost for a real-time system?

**A:** This is a fundamental tension in production vector search:

**Options (ordered by complexity):**
1. **Brute-force GPU (no index):** For <50M vectors, GPU Flat search is fast enough and supports instant updates. Add/remove vectors with `index.add()` / `index.remove_ids()`. No rebuild ever.
2. **Blue-green index swap:** Build new index in background every N minutes/hours. Atomic pointer swap. During rebuild, old index serves (slightly stale). Our approach for IVF-PQ.
3. **Appendix buffer:** Keep main ANN index (rebuilt periodically) + small brute-force buffer for recent additions. Search both, merge results. Buffer flushed into main index during rebuild.
4. **HNSW online updates:** HNSW supports `add()` without rebuild. But: no `delete()` — must tombstone + periodic compaction. Good for corpus that mostly grows.
5. **Streaming vector DB (Milvus/Qdrant):** Managed solution with LSM-tree-like segments — writes go to in-memory segment, periodic merge into optimized segments. Highest freshness, highest ops cost.

**Our fraud detection choice:** Blue-green with 30-second rebuild cycle. 10M vectors × 128d IVF-PQ rebuilds in ~3s on GPU. We run background rebuild every 30s, swap atomically. Worst-case staleness: 30 seconds. For fraud detection, a 30-second-old transaction vector missing from the index is acceptable — the velocity features and graph query cover the real-time signal.

---

### Q6: What is Matryoshka Representation Learning and why does it matter for serving?

**A:** MRL trains embeddings so that any prefix (first 64, 128, 256, etc. of 768 dims) is a valid embedding with graceful quality degradation. The training loss is a weighted sum of contrastive losses at each truncation point.

**Why it matters for serving:**
- **Tiered retrieval:** Use 64d for fast first-pass (4x less compute), 768d for re-scoring top candidates.
- **Memory savings:** Store 128d on device (4x smaller index) for mobile/edge.
- **Adaptive precision:** High-traffic queries use truncated embeddings (faster); important queries use full dimensions.
- **Cost scaling:** A 768d FAISS search is ~6x more expensive than 128d. At 20K TPS, this is the difference between 1 GPU and 6.

**Example:** We serve on-device Siri search with 256d (truncated from 768d) — saves 3x memory on iPhone. Quality drops only 3% on Recall@10 compared to full 768d.

---

## 14. Ranking & Re-Ranking

### Q7: Walk through a production multi-stage ranking pipeline. What happens at each stage?

**A:**

```
Stage 1: Candidate Generation (Retrieval)
  Input:  Query (or user context)
  Output: ~1000 candidates from 10M+ corpus
  Models: Dual-tower (bi-encoder) + ANN, BM25
  Latency: 1-5ms
  Quality: Optimize for Recall@1000

Stage 2: Pre-Ranking (Lightweight Scoring)
  Input:  ~1000 candidates
  Output: ~200 candidates
  Models: Small MLP (32 features → score), or distilled ranker
  Latency: 1-2ms (batch GPU inference)
  Quality: Optimize for Recall@200 among retrieved

Stage 3: Full Ranking (Main Model)
  Input:  ~200 candidates
  Output: ~50 candidates (fully ordered)
  Models: Multi-task deep model (DCNv2 + sequential transformer)
          Features: user history, item content, context, cross-features
  Latency: 5-15ms (GPU, batched)
  Quality: Optimize for NDCG@50, multi-objective (click, like, dwell, convert)

Stage 4: Re-Ranking (Post-Processing)
  Input:  ~50 candidates (ordered by model score)
  Output: ~20 final results (shown to user)
  Logic:  Diversity injection (MMR), business rules, freshness boost,
          monetization (ads insertion), fairness constraints
  Latency: 1-2ms (rule-based + simple optimization)
  Quality: Optimize for user satisfaction + business KPIs
```

**Key insight:** Each stage makes the next stage affordable. Without pre-ranking, full ranking would need to score 1000 items × 15ms/batch = too expensive. The funnel structure is an economic constraint, not a quality choice.

---

### Q8: How does a multi-task ranking model work? Why not separate models per objective?

**A:** A multi-task model shares a backbone (feature encoder + interaction layers) and has task-specific heads (towers) for each objective:

```
                    Shared Backbone (DCNv2 + Sequential Transformer)
                              |
          ┌───────────┬───────┴───────┬────────────┬────────────┐
          v           v               v            v            v
    Click Head   Like Head    Comment Head   Dwell Head   Purchase Head
     (binary)    (binary)     (binary)       (regression)  (binary)
```

**Why shared > separate:**
1. **Feature efficiency:** Shared backbone computes cross-features once; separate models each recompute independently.
2. **Transfer learning:** Sparse signals (purchases) benefit from dense signals (clicks) via shared representations.
3. **Serving cost:** One forward pass produces all scores; separate models = N forward passes.
4. **Representation quality:** Multi-task regularization prevents overfitting to any single noisy objective.

**Final score combination:** Weighted sum or learned combination:
`final_score = w1*P(click) + w2*P(like) + w3*P(purchase) + w4*E[dwell_time]`

Weights are tuned via online A/B tests to optimize long-term engagement/revenue metrics.

**When separate models win:** When tasks genuinely conflict (e.g., clickbait optimizes clicks but hurts dwell time). MMoE architecture handles this by allowing per-task gating over shared experts.

---

### Q9: What is position bias and how do you handle it in training?

**A:** Position bias: users are more likely to click item at position 1 regardless of quality. If we naively train on click data, the model learns "position 1 is good" instead of "this item is relevant."

**Solutions:**

1. **Position as a feature during training, removed at serving:** Add `position_embedding` as input during training. The model learns to separate position effect from relevance. At serving time, set position=0 (or a fixed value) for all candidates — model scores purely on relevance.

2. **Inverse Propensity Weighting (IPW):** Weight each training example by 1/P(examine|position). Items at position 10 get higher weight because clicks there are more meaningful (user had to scroll past better positions).

3. **Unbiased Learning to Rank (ULTR):** Joint model learns both a relevance component and a position component (examination probability). Only the relevance component is used at serving.

4. **Randomization:** Occasionally shuffle results (epsilon-greedy) to collect unbiased data. Expensive (hurts user experience) but gives ground-truth position-free labels.

**In our system:** We use approach 1 (position as training feature, zeroed at serving). It's simplest and works well empirically. We also periodically collect randomized data (5% of traffic in low-risk positions) to validate the debiasing is working.

---

### Q10: How do you implement diversity in re-ranking? What is MMR?

**A:** Diversity prevents showing 10 nearly-identical results. Users want coverage of different aspects of their query.

**Maximal Marginal Relevance (MMR):**
$$MMR = \arg\max_{d \in R \setminus S} \left[ \lambda \cdot Sim(d, q) - (1-\lambda) \cdot \max_{d_j \in S} Sim(d, d_j) \right]$$

Iteratively select the next item that maximizes relevance to query while minimizing similarity to already-selected items. λ controls relevance vs. diversity tradeoff.

**Other approaches:**
- **Determinantal Point Processes (DPP):** Probabilistic selection that jointly optimizes quality and diversity.
- **Submodular optimization:** Greedy selection with diminishing returns guarantees.
- **Category/facet-based:** Hard constraints — max 3 items from same category, at least 1 from each major facet.
- **Intent-aware diversity:** Identify multiple query intents, ensure each is represented in results.

**In practice (our system):** We use category-based rules (max 2 items from same merchant_category in fraud alerts shown to analyst) plus similarity-based dedup (cosine > 0.95 between two result embeddings → suppress the lower-scored one).

---

### Q11: How would you use an LLM/SLM for re-ranking? What are the tradeoffs?

**A:** A small language model (0.5B-3B params) can score (query, document) pairs with much richer understanding than dot-product or feature-based models:

**Approaches:**
1. **Pointwise scoring:** LLM outputs a relevance score for each (query, doc) pair independently. Simple but no cross-candidate comparison.
2. **Listwise ranking:** LLM sees all candidates together, outputs a ranked permutation. Better quality but sequence length explodes with many candidates.
3. **Pairwise comparison:** LLM compares pairs and derives ranking via tournament. Highest quality per comparison but O(n²) comparisons.

**Production pattern (SGLang):**
- Use scoring-only mode (no generation, just logit extraction)
- Prefix caching: system prompt + query cached across all candidates
- Each candidate appended independently → batched forward pass
- Latency: 200 candidates × 0.6B model ≈ 15-25ms (with prefix caching)

**Tradeoffs:**
| Factor | LLM Re-Ranker | Feature-Based Model |
|--------|--------------|---------------------|
| Quality | +10-20% NDCG | Baseline |
| Latency | 15-50ms (200 items) | 2-5ms (200 items) |
| Cost (GPU) | 10-50x more compute | Minimal |
| Cold start | Works on unseen items (text understanding) | Needs features/history |
| Interpretability | Opaque | Feature importance |

**When to use:** Re-ranking top-50 to top-200 items where quality matters more than cost. Not viable for candidate generation (too expensive at scale).

---

## 15. Knowledge Graph & Graph-Based Features

### Q12: How do graph features improve recommendation/scoring beyond vector similarity?

**A:** Vector similarity captures "semantic closeness" but misses structural relationships. Graph features capture connectivity patterns that embeddings cannot easily encode:

**Examples in fraud detection:**
- `ring_size`: How many cards share a device/IP with this transaction's card? (Value: 1 = normal, 50 = fraud ring)
- `shared_merchant_velocity`: How many other cards from the same device cluster transacted at this merchant in the last hour?
- `shortest_path_to_known_fraud`: Graph distance from this card to any confirmed-fraud card via shared entities.

**Why embeddings alone miss this:**
- A new card with no transaction history has a generic embedding. But if its device fingerprint connects to 20 fraud-confirmed cards via a 2-hop graph path, the graph signal is immediate.
- Embeddings are trained on historical patterns. A novel fraud ring (never-before-seen pattern) won't have a "fraud-like" embedding, but its graph structure will be anomalous.

**Examples in recommendation (LinkedIn-style):**
- `2nd_degree_connection`: Is this job poster connected to someone in my network?
- `company_alumni_overlap`: How many of my connections work at this company?
- `skill_graph_proximity`: How many hops between my skills and the job's required skills in the skill taxonomy?

**Integration pattern:** Graph features are computed at query time (2-hop traversal in TigerGraph, 1-2ms), written to the arena alongside vector features, and fed into the ensemble model as additional input features. The model learns which signal types matter for which decision types.

---

### Q13: How do you execute real-time graph queries at 20K+ TPS without blocking the scoring path?

**A:** Three techniques:

**1. Pre-materialized graph features (most common):**
For slowly-changing graph structure (social connections, account relationships), pre-compute features offline and cache in Redis/memory. Lookup at scoring time = O(1). Refreshed every 5-60 minutes.

**2. Async graph query with timeout:**
Fire graph query in parallel with other scoring stages. If graph result returns within budget (e.g., 2ms), include it. If timeout, use cached/default features. Our TigerGraph queries are bounded at 2ms; if they exceed this, we use the last-known features.

**3. In-process graph (our future direction):**
Embed a lightweight graph engine (Kùzu, Raphtory) in-process. The fraud subgraph (active entities within 3-hop radius of recent transactions) fits in ~2GB memory. Traversal is a function call — no network, no serialization, sub-100µs for 2-hop.

**Our current implementation:**
```
Scoring pipeline (parallel stages):
  [Feature Extraction]──┐
  [FAISS GPU Search]─────┼──▶ [XGBoost Ensemble]
  [TigerGraph 2-hop]─────┘        ↓
                              Final Score
  
  TigerGraph query runs in parallel with FAISS.
  Budget: 2ms. Timeout fallback: cached features.
  Arena receives graph features at known offset.
```

---

### Q14: How do you build and maintain a knowledge graph for real-time fraud detection?

**A:** Our fraud knowledge graph schema:

**Vertices:** Card, Device, IP, Address, Merchant, Phone, Email
**Edges:** `transacted_at(Card→Merchant)`, `used_device(Card→Device)`, `from_ip(Card→IP)`, `registered_to(Card→Address)`, `shared_device(Card↔Card)` — derived edge from co-occurrence on same device.

**Building:**
1. **Streaming ingestion:** Every transaction creates/updates edges in real-time (Kafka → TigerGraph loader). Card→Merchant edge created, Card→Device edge updated.
2. **Derived edges:** Background job computes `shared_device` edges every 5 minutes — any two cards that used the same device within 24h get connected.
3. **Decay:** Edges older than 90 days get reduced weight. After 180 days, pruned.

**Real-time query (GSQL):**
```gsql
CREATE QUERY fraud_ring_features(VERTEX<Card> card) FOR GRAPH fraud_graph {
  SetAccum<VERTEX> @@related_cards;
  SumAccum<INT> @@ring_size;
  MapAccum<STRING, INT> @@shared_entity_counts;
  
  Start = {card};
  
  // 1-hop: find all devices/IPs this card used
  Entities = SELECT t FROM Start:s -(used_device|from_ip)-> :t;
  
  // 2-hop: find all OTHER cards that share those entities
  RelatedCards = SELECT t FROM Entities:s -(used_device|from_ip)-> Card:t
                 WHERE t != card
                 ACCUM @@ring_size += 1,
                       @@shared_entity_counts += (s.type -> 1);
  
  PRINT @@ring_size, @@shared_entity_counts;
}
```

**Maintenance challenges:**
- **Graph explosion:** One compromised public WiFi IP can connect thousands of legitimate cards. Solution: cap edge weight by entity degree — IPs with >1000 cards get downweighted.
- **Temporal decay:** Old edges must expire. Solution: edge property `last_seen_ts`; queries filter edges within time window.
- **Bulk imports:** After a data migration, rebuilding the graph takes hours. Solution: incremental load with parallel GSQL loading jobs (handles 500K edges/sec).

---

### Q15: Compare graph-based retrieval vs. vector-based retrieval. When do you use which?

**A:**

| Dimension | Vector Retrieval | Graph Retrieval |
|-----------|-----------------|-----------------|
| **Signal** | Semantic similarity (learned) | Structural relationships (explicit) |
| **Cold start** | Needs embedding (content-based OK) | Needs graph connections |
| **Explainability** | "Similar to X" (opaque why) | "Connected via Y" (path explains) |
| **Novel patterns** | Limited to training distribution | Discovers unseen connectivity |
| **Latency** | Sub-ms (ANN) | 1-5ms (2-hop traversal) |
| **Scale** | Billions (with ANN) | Billions (TigerGraph/Neptune) |
| **Best for** | Content similarity, semantic search | Relationship-based signals, fraud rings |

**When to use both (hybrid):**
- **Recommendation:** Vector finds semantically similar items; graph finds items popular among user's social circle. Fusion gives better coverage.
- **Fraud:** Vector finds transactions similar to known fraud patterns; graph finds cards structurally connected to fraud rings. Different fraud types caught by different signals.
- **Entity resolution (Siri):** Vector resolves ambiguous entities by semantic similarity; graph disambiguates using user's relationship to entities (contacts, visited places).

---

## 16. GPU & CPU Optimization for Search/Scoring

### Q16: Why run FAISS on GPU? What's the speedup and when doesn't it help?

**A:** GPU FAISS speedup depends on corpus size, batch size, and index type:

| Scenario | GPU Speedup vs CPU | Why |
|----------|-------------------|-----|
| Flat (brute-force), 1M vectors, batch=32 | 50-100x | Embarrassingly parallel dot products |
| IVF-PQ, 10M vectors, batch=32 | 10-30x | Parallelized cluster scan + PQ lookup |
| CAGRA, 10M vectors, batch=1 | 5-10x | Graph traversal has dependencies |
| HNSW, 100K vectors, batch=1 | 0.5-1x (GPU LOSES) | Sequential graph navigation; CPU better |

**When GPU doesn't help:**
- **Single-query latency on graph indexes:** HNSW navigation is inherently sequential (each hop depends on previous). CPU with good cache locality wins.
- **Very small corpus (<10K):** Overhead of GPU launch + H2D transfer > brute-force CPU time.
- **Memory-constrained:** If index doesn't fit in GPU VRAM, you need CPU fallback or sharding anyway.

**When GPU is essential:**
- High batch sizes (>8 queries simultaneously)
- Embedding model already on GPU (avoid D2H transfer for search input)
- Corpus > 1M where parallelism matters
- CAGRA (built for GPU parallelism)

**Our design:** Embedding model runs on GPU, output stays on GPU, FAISS search on same GPU, results stay on GPU for score computation. Zero host transfers in the hot path. This saves 0.8ms of PCIe round-trip per query.

---

### Q17: How do you keep data on GPU across pipeline stages to avoid PCIe transfers?

**A:** The key insight: at 20K TPS, every unnecessary Host↔Device transfer costs latency. PCIe Gen5 has ~2µs latency + bandwidth cost.

**Our approach — single CUDA context, chained operations:**
```
[Network bytes arrive on CPU]
    ↓ (unavoidable H2D: features extracted, copied once)
[Feature extraction on CPU → pinned buffer → async H2D]
    ↓
[GPU: ONNX Runtime embed → d_embedding (stays on GPU)]
    ↓ (pointer passed, no copy)
[GPU: FAISS search(d_embedding) → d_distances, d_labels (on GPU)]
    ↓ (pointer passed, no copy)
[GPU: normalize + score computation → d_score]
    ↓ (one D2H: 4 bytes — the final score)
[CPU: decision logic on float score]
```

**Techniques:**
1. **Shared CUDA context:** ONNX Runtime and FAISS use the same `cudaContext`. Pointers from one are valid in the other.
2. **Same stream execution:** Operations on same stream are ordered — no synchronization barriers needed.
3. **CUDA Graphs:** Capture the entire pipeline (H2D → embed → search → score → D2H) as a single graph. One launch for the whole sequence.
4. **Pinned memory pool:** Pre-allocated page-locked host memory for the single H2D at pipeline entry. No per-request `cudaMallocHost`.
5. **Device allocator (RMM):** RAPIDS Memory Manager pre-allocates a GPU memory pool. No per-request `cudaMalloc`.

**What we transfer:**
- H2D (once): raw feature vector (~512 bytes per request)
- D2H (once): final score (4 bytes) + top-K labels (80 bytes)
- Everything else stays on device

---

### Q18: How do you optimize XGBoost inference for sub-millisecond latency?

**A:** Standard XGBoost library inference adds ~5-20ms due to Python bindings, batch API design, and memory layout. We needed sub-1ms for 8 trees:

**Optimizations:**
1. **Custom C++ tree traversal:** Bypass the XGBoost library entirely. Export tree structure as flat arrays. Traverse with branch-free comparisons:
   ```cpp
   // Branch-free tree traversal (no misprediction stalls)
   for (int node = 0; !is_leaf(node); ) {
       float val = features[split_feature[node]];
       int go_right = (val > split_threshold[node]);
       node = child_left[node] + go_right;  // Branchless: always computes both
   }
   return leaf_value[node];
   ```

2. **AVX2 SIMD (8 trees in parallel):** Process 8 trees simultaneously using SIMD lanes:
   ```cpp
   __m256 thresholds = _mm256_loadu_ps(&split_threshold[nodes * 8]);
   __m256 values = _mm256_set1_ps(feature_val);
   __m256i cmp = _mm256_castps_si256(_mm256_cmp_ps(values, thresholds, _CMP_GT_OQ));
   // 8 tree decisions in 1 cycle
   ```

3. **Cache-aligned tree layout:** Nodes stored in BFS order (cacheline-friendly traversal). Each node fits in 16 bytes (feature_id, threshold, left_child, leaf_value packed).

4. **No memory allocation:** Feature vector read directly from arena (where upstream wrote it). Output score written directly to arena.

**Result:** 8 trees × depth 6 = ~48 node comparisons total. With AVX2: ~6 iterations. Total: 0.5-0.9ms on a single core.

---

### Q19: What is CUDA Graph and why does it matter for search pipelines?

**A:** A CUDA Graph captures a sequence of GPU operations (kernel launches, memory copies) into a single executable entity. Instead of the CPU issuing 30+ individual operations (each costing 3-5µs of CPU-side overhead), it launches one graph.

**Why it matters for our pipeline:**
- Our scoring pipeline has ~35 GPU operations: feature normalization → ONNX (20 layers) → L2 norm → FAISS search → score
- Without graphs: 35 × 4µs = 140µs of CPU dispatch overhead
- With graphs: 5µs single launch
- Savings: 135µs per request. At 20K TPS = 2.7 seconds of CPU saved per second

**Constraints relevant to search:**
1. **Static shapes:** Input batch size must be fixed at capture time. We capture graphs for batch sizes {1, 4, 8, 16} and select based on current queue depth.
2. **No dynamic allocation:** Can't resize FAISS result arrays during graph. Pre-allocate for max top-K.
3. **No control flow:** Can't branch on intermediate results (e.g., "if embedding norm < threshold, skip FAISS"). This is fine for our pipeline — always do all stages.
4. **Parameter updates:** Can update input data pointers without re-capture (via `cudaGraphExecUpdate`). New request data goes to same pinned buffer offset → graph reads new data at same pointer.

**When NOT to use:** Variable-length inputs (different embedding dims), conditional execution paths, or workloads where kernel topology changes per request.

---

### Q20: How do you handle CPU-GPU coordination in a mixed scoring pipeline?

**A:** Our pipeline is ~70% GPU (embedding + FAISS + score) and ~30% CPU (feature extraction + graph query + decision logic). The challenge: don't let CPU stages starve the GPU.

**Our approach — overlapping execution:**
```
Time →
CPU Core:  [Parse req N] [Extract feat N] [Graph query N]  [Decision N]
                          [Parse N+1]  [Extract N+1]  [Graph N+1]  [Decision N+1]
GPU:       [..Embed N..] [...FAISS N...] [Score N]
                         [.Embed N+1.] [...FAISS N+1..] [Score N+1]
```

**Techniques:**
1. **Async DMA (cudaMemcpyAsync):** CPU doesn't wait for H2D to complete. Continues parsing next request.
2. **CUDA streams:** GPU work for request N is on stream N%4. Multiple requests in flight.
3. **Pinned memory ring:** 4 pinned buffers rotated. CPU writes to slot K while GPU reads from slot K-1.
4. **CPU-GPU latency hiding:** Graph query (1.8ms on CPU) overlaps with GPU embedding (8ms) — they're independent. Whichever finishes second triggers ensemble scoring.

**Key invariant:** GPU must never be idle waiting for CPU. CPU prepares the next batch while GPU processes the current one. We maintain a small queue (4-8 requests) to keep the GPU saturated.

---

## 17. Observability & Monitoring for Search Systems

### Q21: What metrics do you track for a production search/recommendation system?

**A:** Organized by layer (RED method + domain-specific):

**Application Layer (Business):**
| Metric | What It Tells You | Alert Threshold |
|--------|-------------------|-----------------|
| NDCG@10 (online) | Are results getting worse? | Drop > 5% from baseline |
| Click-through rate | Are users engaging? | Drop > 10% hourly |
| Zero-result rate | Is retrieval failing? | > 2% of queries |
| Diversity score | Are we showing varied results? | Drop below 0.6 |
| Conversion rate | Business outcome | Drop > 15% |

**Serving Layer (Latency/Throughput):**
| Metric | What It Tells You | Alert Threshold |
|--------|-------------------|-----------------|
| P50/P99 E2E latency | User experience | P99 > 2x SLA |
| Per-stage latency breakdown | Which stage is slow | Any stage > 50% of budget |
| QPS (queries per second) | Load pressure | > 80% of tested capacity |
| Queue depth | Are we falling behind? | > 50 queued requests |
| Timeout rate | Hard failures | > 0.1% |
| Model inference latency | ML health | Drift > 20% from baseline |

**Infrastructure Layer (GPU/CPU/Memory):**
| Metric | What It Tells You | Alert Threshold |
|--------|-------------------|-----------------|
| GPU SM utilization | Is GPU doing useful work? | < 30% (underutilized) or > 95% (saturated) |
| GPU memory usage | Room for growth? | > 85% of VRAM |
| FAISS search latency | Index health | > 2x baseline |
| CPU utilization per core | Hotspots / imbalance | Any core > 90% sustained |
| Arena utilization (%) | Memory pressure | > 80% of arena capacity |
| Ring buffer fullness | Backpressure signal | > 50% for > 10s |
| DRAM bandwidth (STREAM) | Memory subsystem | < 80% of expected |
| TLB miss rate | Hugepage issues | > 1% |

**Index Health:**
| Metric | What It Tells You | Alert Threshold |
|--------|-------------------|-----------------|
| Index age (seconds) | Freshness | > 5 minutes |
| Index size (vectors) | Growth/shrinkage | Unexpected ±10% |
| Recall@10 (sampled) | Quality degradation | < 90% vs brute-force |
| Rebuild duration | Performance regression | > 2x expected |

---

### Q22: How do you detect search quality degradation in production without labeled data?

**A:** In production, you rarely have real-time ground-truth labels. Proxy signals and comparative methods:

**1. Implicit quality signals:**
- **Reformulation rate:** If users rephrase their query immediately, the first result was bad. Track `reformulation_within_30s`.
- **Dwell time vs. position:** Users click result at position 5 and dwell for 60s → positions 1-4 were probably irrelevant.
- **Skip rate:** User scrolls past all results without clicking → zero engagement.
- **Quick-back rate:** User clicks but returns within 3 seconds → result was misleading.

**2. Sampled evaluation:**
- Run 1% of queries through a "gold" system (brute-force retrieval + cross-encoder scoring). Compare production results to gold results. Alert if NDCG agreement drops below threshold.
- Cost: ~10x compute on 1% of traffic = negligible.

**3. Distribution monitoring:**
- Track embedding distribution (centroid, variance) of served results. If it shifts significantly without model update → something upstream changed (data pipeline issue, new content type).
- Track score distribution. If median score drops → either corpus changed or model is stale.

**4. Comparative (interleaving):**
- Show interleaved results from system A (production) and system B (candidate). Measure which system's results get more clicks. Detects quality differences with less traffic than full A/B.

**5. Anomaly detection on engagement metrics:**
- NDCG proxy (computed from clicks + dwell time + conversions) with 24h trailing average. Z-score alerting.

---

### Q23: How do you build an observability stack for shared-memory pipelines where traditional tracing doesn't work?

**A:** Standard distributed tracing (Jaeger, Zipkin) assumes network hops between services. With shared memory, there are no network calls to instrument. Our approach:

**1. Arena-embedded trace metadata:**
Each arena allocation includes a 16-byte header: `{trace_id: u64, stage_mask: u8, timestamp_ns: u64}`. Consuming stages read the trace_id and emit their own spans with the same trace_id.

**2. Ring buffer event stream:**
A dedicated "observability ring" (separate from data ring) carries lightweight timing events:
```rust
struct TraceEvent {
    trace_id: u64,
    stage: u8,       // 0=parse, 1=feature, 2=embed, 3=faiss, 4=graph, 5=score
    event: u8,       // 0=enter, 1=exit
    timestamp_ns: u64,
}
```
Consumer (async, non-blocking) reads events and exports to Prometheus/Jaeger. Zero hot-path overhead — just two atomic writes per stage boundary.

**3. Prometheus histograms per stage:**
Each stage records its duration in a thread-local histogram buffer. A background thread flushes to Prometheus every 5s. No lock contention on the hot path.

**4. Health probes with golden queries:**
Every 10 seconds, inject a synthetic request with known-correct output through the full pipeline. Validate output matches expected. Measures end-to-end health including shared-memory path correctness.

**5. Arena visualization tool:**
CLI tool that attaches to shared memory (read-only), walks allocations by type tag, and renders current arena state. Invaluable for debugging memory layout issues.

---

### Q24: How do you monitor FAISS index quality over time in production?

**A:** FAISS ANN indexes trade recall for speed. Over time, recall can degrade if the data distribution shifts away from what the index was trained on (IVF centroids, PQ codebook).

**Monitoring approach:**

**1. Online recall estimation (cheap):**
Every 1000th query, also run brute-force search on CPU (or on 1% of the index). Compare: how many of brute-force top-10 appear in ANN top-10? This is an unbiased estimate of Recall@10.
```python
# Sampled recall check (runs every 1000 queries)
ann_results = faiss_gpu.search(query, k=10)
exact_results = faiss_flat.search(query, k=10)
recall = len(set(ann_results) & set(exact_results)) / 10
prometheus_histogram.observe(recall)
```

**2. Centroid drift detection (IVF indexes):**
Track average distance from vectors to their assigned centroids. If it increases significantly → data distribution shifted → centroids are stale → recall degrades → time to rebuild.

**3. Rebuild quality gate:**
After every index rebuild, run evaluation suite (1000 sampled queries) before promoting to production. If Recall@10 < 0.90 → block promotion, alert.

**4. Query latency as proxy:**
If nprobe is fixed but search latency increases, clusters may be imbalanced (one cluster got too large). Rebalancing needed.

---

## 18. Infrastructure & Deployment

### Q25: How do you deploy a GPU-accelerated search service on Kubernetes?

**A:** Key architectural decisions:

**1. GPU resource management:**
```yaml
resources:
  limits:
    nvidia.com/gpu: 1        # Full GPU or MIG slice
    memory: 32Gi
    cpu: "8"
  requests:
    nvidia.com/gpu: 1
    memory: 32Gi
    cpu: "8"                  # Dedicated cores (CPU Manager static policy)
```

**2. NVIDIA GPU Operator stack:**
- Device Plugin: Exposes GPUs as K8s resources
- DCGM Exporter: Prometheus metrics (utilization, memory, temp, ECC)
- GPU Feature Discovery: Auto-labels nodes (gpu-product, driver-version)
- MIG Manager: Configures MIG slices if using partitioned GPUs

**3. Topology-aware scheduling:**
```yaml
# Ensure pod lands on NUMA node local to the GPU
topologySpreadConstraints:
  - topologyKey: topology.kubernetes.io/zone
spec:
  topologyManagerPolicy: single-numa-node  # kubelet flag
```

**4. Index lifecycle (init container pattern):**
```yaml
initContainers:
- name: load-index
  image: index-loader:latest
  command: ["python", "load_index.py",
            "--source", "s3://indexes/latest/",
            "--dest", "/data/faiss/",
            "--validate-checksum"]
  volumeMounts:
  - name: index-vol
    mountPath: /data/faiss
```

**5. Readiness gate (warm-up):**
Pod isn't ready until:
- FAISS index loaded into GPU memory
- ONNX model loaded and warmed (run 100 dummy inferences)
- TigerGraph client connected and warmed (1000 synthetic queries)
- CUDA Graph captured for all batch sizes
- Arena allocated and validated

**6. Blue-green model/index update:**
Two deployments (blue/green). Update green → validate via synthetic traffic → shift production traffic via service selector → old blue becomes next update target.

---

### Q26: How do you handle scaling a search service horizontally vs. vertically?

**A:**

**Vertical scaling (bigger GPU / more memory):**
- Move from MIG slice (40GB) to full H100 (80GB) → bigger index in memory
- Move from T4 to H100 → faster FAISS search and embedding
- **When:** Single index fits in GPU, need lower latency per query

**Horizontal scaling (more replicas):**
- Each replica holds a full copy of the index
- Load balancer distributes queries across replicas
- **When:** Throughput (QPS) exceeds single-GPU capacity, index fits per-node

**Sharded scaling (distributed index):**
- Partition the corpus across N nodes (each holds 1/N of vectors)
- Query broadcast to all shards, results merged
- **When:** Index too large for single GPU (>100M vectors × 768d in full precision)

**Our choice (CapitalOne fraud):**
- 10M vectors × 128d IVF-PQ = ~250MB → easily fits one GPU
- We scale **horizontally** — 8 replicas, each with full index copy
- Load balanced by hash(card_bin) → consistent routing for cache locality
- Each replica handles ~3K TPS → total 24K TPS

**If we had 1B vectors:**
- Shard across 10 GPUs (100M each)
- Each query goes to all 10 shards in parallel
- Merge top-K from each shard → global top-K
- Latency: max(per-shard latency) + merge time
- Alternative: Coarse IVF to route query to 2-3 relevant shards only (reduces fan-out)

---

### Q27: How do you handle model/index updates without downtime or quality regression?

**A:** Three interrelated update cycles:

**1. Index update (every 30 seconds to hours):**
```
[Background job: build new index from latest vectors]
         ↓
[Validate: run 1000 golden queries, check recall > 0.90]
         ↓
[Load into shadow GPU memory region]
         ↓
[Atomic pointer swap: index_ptr = new_index]
         ↓
[Free old index memory (after drain)]
```
Zero-downtime. Queries in-flight finish on old index. New queries hit new index.

**2. Embedding model update (weekly/monthly):**
```
[Train new model v2]
         ↓
[Re-embed entire corpus with v2 (offline batch job)]
         ↓
[Build new index from v2 embeddings]
         ↓
[Canary: 5% traffic to v2 pods, compare metrics for 24h]
         ↓
[If NDCG/engagement improved: full rollout]
[If degraded: rollback (old index still active)]
```
**Critical:** Embedding model and index must be version-matched. A v2 query embedding searched against v1 document embeddings gives garbage. Always re-embed corpus on model change.

**3. Ranking model update (weekly):**
```
[Train new ranker]
         ↓
[Shadow scoring: run new ranker alongside production on live traffic]
         ↓
[Compare: new ranker NDCG vs old (on same candidate sets)]
         ↓
[A/B test: 10% traffic, measure engagement]
         ↓
[If improved: promote. If neutral/degraded: discard]
```

**Rollback strategy:** Always keep previous version's artifacts (model, index, config) in cold storage. Rollback = redeploy previous version's pod (< 2 minutes including warm-up).

---

### Q28: How do you handle failure modes in a real-time scoring pipeline?

**A:** Each component has a defined failure mode and fallback:

| Component | Failure Mode | Detection | Fallback | Impact |
|-----------|-------------|-----------|----------|--------|
| **FAISS GPU** | GPU OOM / kernel error | cudaGetLastError() ≠ success | CPU brute-force (slower but correct) | +5ms latency |
| **ONNX Model** | NaN output / timeout | Output validation + deadline | Pre-computed embedding cache (recent queries) | Stale but valid |
| **TigerGraph** | Timeout / connection refused | 2ms deadline, TCP health | Cached graph features (last known) | Slightly stale features |
| **Arena overflow** | Request > arena capacity | Offset check at allocate | Reject request with fast error | 1 request lost |
| **SPSC ring full** | Consumer behind | Ring fullness > 90% | Overflow to durable queue (Kafka) | Async path delayed |
| **GPU hardware** | ECC error / thermal throttle | DCGM alerts | Drain pod, shift traffic to replicas | Capacity -1 replica |

**Circuit breaker pattern:**
```rust
enum CircuitState { Closed, Open, HalfOpen }

struct CircuitBreaker {
    state: CircuitState,
    failure_count: u32,
    threshold: u32,        // 5 consecutive failures → open
    reset_timeout: Duration, // 30s before trying again
}

impl CircuitBreaker {
    fn call<F, T>(&mut self, f: F) -> Result<T, FallbackResult>
    where F: FnOnce() -> Result<T, Error> {
        match self.state {
            Closed => match f() {
                Ok(v) => { self.failure_count = 0; Ok(v) }
                Err(_) => {
                    self.failure_count += 1;
                    if self.failure_count >= self.threshold {
                        self.state = Open;
                    }
                    Err(FallbackResult::UseCached)
                }
            },
            Open => Err(FallbackResult::UseCached),  // Don't even try
            HalfOpen => { /* Try one request, close or re-open */ }
        }
    }
}
```

**Key principle:** The scoring pipeline must ALWAYS return a score (even degraded) rather than fail. A 5ms degraded response is infinitely better than a timeout for fraud detection.

---

### Q29: How do you capacity plan for a GPU-accelerated search cluster?

**A:** Three dimensions to model:

**1. Compute capacity (GPU):**
```
Max QPS per GPU = 1000ms / per_query_latency_ms
  Example: 0.3ms/query (FAISS) + 8ms (embedding) batched across 16 queries
           = 16 queries / 8.3ms = 1,928 QPS per GPU (embedding-bound)
  
With pipeline parallelism (overlap):
  Effective: ~3,000 QPS per GPU (embedding of batch N+1 overlaps FAISS of batch N)
  
Cluster: 8 GPUs = 24,000 QPS (matches our 20K TPS target with 20% headroom)
```

**2. Memory capacity (VRAM):**
```
Per-GPU memory budget:
  FAISS index:     250 MB
  ONNX model:     260 MB
  Activations:    500 MB
  Embedding cache: 300 MB
  CUDA workspace:  2 GB
  Total:          ~3.4 GB
  
  Available on H100: 80 GB → massive headroom
  On MIG 3g.40gb:  36.5 GB → still fine
  On T4 (16 GB):   Tight but workable (remove cache, reduce workspace)
```

**3. Network capacity:**
```
Per request: ~2 KB in (features) + ~500 bytes out (score + metadata)
At 24K TPS: 48 MB/s in + 12 MB/s out = 60 MB/s total
100 Gbps NIC: 12.5 GB/s → network is NOT a bottleneck (0.5% utilization)

Graph queries to TigerGraph: 24K QPS × ~1 KB/response = 24 MB/s
```

**Scaling trigger:**
- Scale OUT (add replica) when: GPU utilization > 70% sustained for 5 minutes
- Scale UP (bigger GPU) when: single-query latency violates SLA (can't parallelize further)
- Scale INDEX (shard) when: index doesn't fit in single GPU VRAM

---

### Q30: How do you ensure reproducibility and debuggability of search results in production?

**A:** Search result debugging is notoriously hard because results depend on query, user state, model version, index version, and feature values — all of which change over time.

**Our approach:**

**1. Request logging with full context:**
For every request (or sampled 10%), log:
```json
{
  "trace_id": "abc123",
  "query_embedding": [0.12, -0.34, ...],  // or hash for storage
  "faiss_results": [{"id": 42, "dist": 0.23}, ...],
  "graph_features": {"ring_size": 3, "shared_devices": 1},
  "model_scores": {"xgboost": 0.847, "ensemble": 0.852},
  "final_decision": "APPROVE",
  "index_version": "2024-01-15T08:30:00Z",
  "model_version": "v3.3",
  "latency_us": 3812
}
```

**2. Replay infrastructure:**
Given a logged request, we can replay it against any (model_version, index_version) pair in a staging environment. Used for:
- "Why was this transaction declined?" → replay shows which features triggered
- Debugging quality regression → replay sample against old and new model

**3. Feature store snapshots:**
Feature values (embeddings, graph features, velocity counters) are point-in-time snapshotted. When replaying, we load the snapshot from that timestamp — ensures identical inputs.

**4. Deterministic scoring:**
- Fixed random seeds for any stochastic component
- Pinned CUDA operations (deterministic cuBLAS mode)
- Sorted candidate lists before scoring (avoid order-dependent floating point)

**5. Shadow scoring:**
Before deploying a new model/index, run it in shadow mode on production traffic for 24h. Log results side-by-side with production. Identify any divergence BEFORE it affects users.

---

## 19. Embedding Models & Training

### Q31: How do you train embedding models for retrieval? What makes a good training setup?

**A:** Key components:

**1. Architecture:** Bi-encoder (dual-tower). Query tower and document tower can share weights (siamese) or be asymmetric (different architectures/sizes).

**2. Training data:** 
- Positive pairs: (query, clicked/engaged document)
- Hard negatives: (query, non-clicked but high-BM25-scored document). Critical for quality.
- In-batch negatives: Other positives in the same batch serve as negatives. Free, but easy.

**3. Loss function:**
```python
# InfoNCE with temperature and hard negatives
def infonce_loss(query_embs, pos_embs, neg_embs, temperature=0.05):
    pos_sim = torch.sum(query_embs * pos_embs, dim=-1) / temperature
    neg_sim = torch.matmul(query_embs, neg_embs.T) / temperature
    logits = torch.cat([pos_sim.unsqueeze(1), neg_sim], dim=1)
    labels = torch.zeros(len(query_embs), dtype=torch.long)
    return F.cross_entropy(logits, labels)
```

**4. Hard negative mining strategy:**
- Epoch 1-2: Random negatives (learn basic relevance signal)
- Epoch 3+: Mine hard negatives from previous epoch's model (top-100 retrieved that aren't positive)
- Curriculum: gradually increase negative difficulty

**5. Key hyperparameters:**
- Batch size: Larger = more in-batch negatives = better (1024-4096)
- Temperature: Lower = sharper distinctions (0.02-0.07)
- Learning rate: 1e-5 to 5e-5 (fine-tuning BERT-class)
- Embedding dimension: 768 (standard), 128-256 (compressed serving)

**6. Evaluation during training:**
- Recall@10 on held-out queries (not in training set)
- MRR on entity resolution tasks
- Offline NDCG on graded relevance labels

---

### Q32: What is the difference between symmetric and asymmetric embedding models? When does it matter?

**A:**

**Symmetric:** Same encoder for query and document. Works when queries and documents are similar in form (sentence-to-sentence similarity, duplicate detection, semantic search where query is a full sentence).

**Asymmetric:** Different encoders (or different input processing) for query and document. Works when queries and documents are structurally different:
- Short queries (3-5 words) vs. long documents (paragraphs)
- User behavior sequence vs. item metadata
- Natural language question vs. structured product listing

**Why it matters for serving:**
- Asymmetric allows a **lighter query tower** (distilled, fewer layers) for real-time serving while keeping a heavy document tower (more layers, higher quality) for offline embedding.
- Query encoder: 6-layer DistilBERT (8ms inference)
- Document encoder: 12-layer BERT (30ms — but offline, amortized over all queries)

**In our system:** Asymmetric. Query tower is lightweight (fast at serving time). Document tower is heavier (runs offline during index build). This is invisible to the scoring pipeline — at serving time, only the query tower runs.

---

### Q33: How do you evaluate embedding quality before deploying to production?

**A:** Multi-stage evaluation pipeline:

**1. Intrinsic metrics (embedding space quality):**
- Alignment: Are positive pairs closer than random pairs? (contrastive accuracy)
- Uniformity: Are embeddings well-distributed on the hypersphere? (not collapsed)
- Anisotropy: Are all dimensions contributing? (avoid dimensional collapse)

**2. Retrieval metrics (offline):**
- Recall@K (K=1, 10, 100) on held-out query-document pairs
- MRR on entity resolution benchmarks
- NDCG@10 on graded relevance test set

**3. Index-aware evaluation:**
- Run evaluation with actual FAISS index (IVF-PQ) not just brute-force
- Measure recall WITH quantization error — sometimes PQ destroys certain embedding dimensions
- If PQ recall << brute-force recall → embedding isn't PQ-friendly (consider OPQ rotation)

**4. A/B test proxy (shadow scoring):**
- Deploy new model in shadow mode
- Compare click-through rate, engagement, conversion on same traffic
- 24-48h shadow period before any traffic shift

**5. Regression tests:**
- Golden query set (100 queries with human-labeled relevant docs)
- Must achieve Recall@10 ≥ X% to pass deployment gate
- Run nightly; alert on any regression

**Gate criteria for deployment:**
- Recall@10 ≥ 92% (vs. 90% previous model)
- No regression on any query category > 5%
- Shadow CTR neutral or improved
- Latency within 10% of previous model

---

## 20. System Design — End-to-End

### Q34: Design a real-time personalized search system from scratch. Walk through key decisions.

**A:** Designing for: 50K QPS, <50ms P99, 100M documents, personalized results.

**Architecture:**

```
[Query] → [API Gateway] → [Query Understanding]
                                    ↓
                           ┌────────┴─────────┐
                           ↓                  ↓
                    [Dense Retrieval]   [Sparse Retrieval]
                    (FAISS, 100M)      (Elasticsearch)
                           ↓                  ↓
                           └────────┬─────────┘
                                    ↓
                            [Candidate Merge (RRF)]
                                    ↓
                            [Pre-Ranking (MLP)]
                                    ↓
                           [Full Ranking (Multi-Task)]
                                    ↓
                           [Re-Ranking (Diversity + Business)]
                                    ↓
                              [Results]
```

**Key decisions:**

| Decision | Choice | Why |
|----------|--------|-----|
| Retrieval split | Hybrid (dense + sparse) | Dense captures semantics; sparse captures exact match |
| Index location | FAISS GPU in-process | <50ms budget = no network hop for retrieval |
| Corpus: 100M | Shard across 10 GPUs (10M each) | 100M × 768d = 300GB (doesn't fit one GPU in full precision) |
| Alternatively | IVF-PQ: 100M × 64 bytes = 6.4GB (fits one GPU!) | PQ compression makes single-GPU viable |
| Personalization | User embedding (from history) concatenated/added to query embedding | Efficient — no per-user index |
| Ranking model | Multi-task (click + dwell + purchase) on GPU | Single forward pass for all objectives |
| Serving framework | Custom C++/Rust (latency) | No room for framework overhead at 50ms total |
| Caching | Result cache (Redis) for identical queries | 30-40% of queries are repeated |
| Fallback | BM25-only if GPU unavailable | Graceful degradation |

**Latency budget allocation:**
```
Query understanding:   3ms  (NER + intent classification)
Dense retrieval:       5ms  (FAISS GPU, nprobe=32)
Sparse retrieval:      8ms  (Elasticsearch, parallel with dense)
Candidate merge:       1ms  (RRF on CPU)
Pre-ranking:           2ms  (small MLP, 1000→200)
Full ranking:         15ms  (GPU, batch=200, multi-task model)
Re-ranking:            2ms  (diversity + business rules)
Network + overhead:    5ms  
─────────────────────────
Total:               ~41ms  (fits 50ms P99 with margin)
```

---

### Q35: How would you migrate a search system from CPU to GPU? What are the pitfalls?

**A:** Migration phases:

**Phase 1: Identify the bottleneck (1 week):**
- Profile end-to-end with nsys/perf. Is it embedding model? FAISS search? Feature extraction?
- If embedding model dominates (>50% of time): GPU will help massively.
- If feature extraction dominates: GPU won't help (CPU-bound string processing).

**Phase 2: Move embedding model to GPU (2-4 weeks):**
- Export model to ONNX, serve via ONNX Runtime with CUDA provider
- Data flow: CPU features → H2D → GPU inference → D2H → CPU ranking
- Expected speedup: 5-20x on model inference alone

**Phase 3: Move FAISS to GPU (2-3 weeks):**
- Convert CPU index (HNSW) to GPU index (IVF-PQ or CAGRA)
- Key: keep embedding output on GPU, feed directly to FAISS (avoid D2H + H2D round-trip)
- Expected: 10-50x on search, PLUS eliminated 2x PCIe transfer

**Phase 4: End-to-end GPU pipeline (4-6 weeks):**
- CUDA Graphs to capture full pipeline
- Pinned memory for remaining H2D transfers
- RMM pool to eliminate per-request GPU allocations

**Pitfalls:**
1. **Moving data back and forth:** Each PCIe transfer adds 2-5µs + bandwidth cost. A naive port that moves results back to CPU between stages loses most GPU benefit.
2. **GPU memory management:** Without RMM or a memory pool, CUDA malloc/free is expensive and fragments memory. Must pre-allocate.
3. **Batch size sensitivity:** GPUs need batch ≥ 8 to saturate. If your queries arrive one-at-a-time, you need a batching layer (accumulate for 1-2ms, then batch-execute).
4. **HNSW doesn't GPU-accelerate well:** Graph traversal is sequential. If you're on HNSW, switch to IVF-PQ or CAGRA when moving to GPU.
5. **Debugging is harder:** GPU errors are asynchronous. A kernel failure from request N might not surface until request N+10 synchronizes.
6. **Operational complexity:** GPU nodes cost 10-50x more than CPU. Must prove utilization justifies cost. MIG slicing helps share GPUs across services.

---

### Q36: How do you handle multi-tenancy in a search infrastructure serving multiple teams/products?

**A:** Three isolation models:

**1. Namespace isolation (weakest, cheapest):**
- Single index with tenant_id metadata filter
- FAISS: post-filter results by tenant_id after ANN search
- Problem: ANN may return 100 results from wrong tenant, requiring over-retrieval (search top-1000 to get top-10 for tenant)
- Works when: tenants have similar data, cost sensitivity is high

**2. Per-tenant index (moderate):**
- Separate FAISS index per tenant on same GPU
- Index selector routes to correct index based on request header
- GPU memory budget: 80GB / N tenants = max index size per tenant
- Works when: tenants have different embedding spaces, moderate scale

**3. Dedicated infrastructure (strongest):**
- Separate pods/GPUs per tenant
- Full resource isolation (CPU Manager, GPU, memory)
- Kubernetes namespaces + resource quotas + network policies
- Works when: compliance requires it (SOC2, PCI-DSS), or tenants pay for dedicated capacity

**Our approach (Broadcom SWG):** Per-tenant index on shared GPU. 10,000 enterprise customers, each with 10K-1M URLs to classify. We partition by tenant size:
- Small tenants (< 100K vectors): packed 10-50 per GPU using MIG slices
- Large tenants (> 1M vectors): dedicated GPU
- Dynamic rebalancing weekly based on query volume

**Key concern: noisy neighbor.**
One tenant's burst traffic can starve others on shared GPU. Solution: per-tenant rate limiting + priority queues. SLA-critical tenants get guaranteed GPU time slots.

---

## 21. Advanced Topics & Edge Cases

### Q37: How do you handle the cold-start problem for new items in a recommendation system?

**A:** New items have no engagement data → collaborative filtering signals are zero. Solutions by effectiveness:

**1. Content-based embedding (immediate, best):**
LLM-based dual-tower encoder generates embedding from item text/metadata alone. The pretrained LLM brings world knowledge — it "understands" that a job posting mentioning "CUDA, distributed systems, H100" is similar to existing GPU engineering jobs even with zero engagement.

**2. Explore/exploit policy (hours to days):**
- Epsilon-greedy: show new items to ε% of users (ε=5-10%)
- Thompson sampling: maintain uncertainty estimate, sample from posterior
- Explore slots: reserve 2 of 20 positions for new/uncertain items

**3. Feature transfer (immediate):**
If the new item shares attributes with existing items (same category, same author, same brand), borrow engagement features from similar items.

**4. Popularity fallback (immediate, weak):**
Show new items to high-engagement users (they interact more, generating signal faster). Or piggyback on related trending topics.

**Our fraud system's equivalent:** New merchant → no historical fraud rate. Solution: (1) category-level fraud rate as prior, (2) graph features from shared attributes (same address as known-fraud merchants), (3) embedding similarity to known-fraud merchants via FAISS.

---

### Q38: How do you handle embedding drift — when production data diverges from training data?

**A:** Over time, user behavior and content evolve. The embedding model becomes stale.

**Detection:**
1. **Embedding distribution shift:** Track centroid of last 1000 query embeddings. If it moves significantly from training-time centroid → drift.
2. **Retrieval quality degradation:** Recall@10 (sampled) trending downward over weeks.
3. **Feature distribution:** Input features to embedding model shifting (new categories, new terms appearing).
4. **Online metric decline:** CTR or engagement slowly degrading without model change.

**Mitigation:**
1. **Scheduled retraining (weekly/monthly):** Retrain embedding model on latest interaction data. Re-embed corpus. This is the gold standard.
2. **Incremental fine-tuning:** Fine-tune existing model on last week's data (fewer epochs, smaller LR). Faster than full retrain, but accumulates errors over time.
3. **Online learning (advanced):** Update model parameters after each batch of interactions. Requires careful learning rate scheduling and stability guarantees.
4. **Adapter layers:** Freeze base model, train lightweight adapters on recent data. Quick to update, easy to rollback.

**Our approach:** Weekly embedding model retrain, nightly index rebuild with latest vectors. Between retrains, staleness is acceptable because:
- Fraud patterns evolve slowly (weeks/months)
- Velocity features (real-time) compensate for embedding staleness
- Graph features detect novel patterns that embeddings miss

---

### Q39: How do you handle extremely high cardinality categorical features (100M+ unique values)?

**A:** Standard one-hot or embedding lookup doesn't scale to 100M categories (e.g., user_id, item_id, query strings).

**Solutions:**
1. **Hashing trick:** Hash feature value to fixed-size embedding table (e.g., 1M buckets). Collisions reduce quality slightly but bound memory.
2. **Learned hash:** Train a small MLP to map high-cardinality value to a dense representation. More expressive than random hash.
3. **Compositional embeddings:** Decompose ID into components (category + popularity_bucket + recency_bucket). Combine sub-embeddings. Memory: sqrt(N) instead of N.
4. **Feature store precomputation:** For user/item IDs, precompute embeddings offline via collaborative filtering. Store in key-value store. Lookup at serving time.
5. **Percentile bucketization:** Convert continuous features to percentile ranks (100 or 1000 buckets). Dramatically reduces cardinality while preserving rank information.

**In our system:** Card IDs (100M+) are represented via precomputed embeddings (from transaction history matrix factorization). Merchant IDs (500K) use direct embedding table (fits in memory). Device fingerprints (unbounded) use hashing trick to 100K buckets + graph features for disambiguation.

---

### Q40: What's the difference between online serving and offline batch inference for recommendations? When do you use each?

**A:**

| Aspect | Online Serving | Offline Batch |
|--------|---------------|---------------|
| **Latency** | <50ms per request | Hours for full corpus |
| **Freshness** | Real-time context (session, time) | Stale (computed hours ago) |
| **Personalization** | Uses current session + real-time signals | Uses historical patterns |
| **Cost** | GPU per-request | GPU amortized over batch |
| **Use case** | Search results, feed ranking | "People also bought", email recommendations |
| **Scale challenge** | Throughput (QPS) | Throughput (items × users) |
| **Failure mode** | Latency spike → bad UX | Stale recs → lower engagement |

**Hybrid pattern (common in production):**
- **Offline:** Pre-compute top-1000 candidates per user nightly (batch)
- **Online:** Re-rank those 1000 with real-time context (session, time, last click) at serving time
- **Benefit:** Expensive retrieval amortized offline; cheap re-ranking captures recency online

**When purely online is required:**
- Search (query is unknown until user types it)
- Fraud scoring (transaction arrives in real-time)
- Conversational AI (context changes every turn)

**When offline is sufficient:**
- Email/push notifications (generated once, sent later)
- Homepage widgets ("recommended for you" — updated hourly)
- Catalog organization (similar items section)

---

## 22. Production War Stories — Quick-Fire Q&A

### Q41: A user reports "search results got worse yesterday." How do you investigate?

**A:** Systematic approach:

1. **Scope:** Is it all users or specific segments? Check metrics by user cohort, query type, region.
2. **Timeline:** Correlate with deployments, index rebuilds, feature pipeline changes. `kubectl get events` + deploy history.
3. **Metrics:** Check Recall@10 (sampled), CTR, zero-result rate over last 48h. Is there a step change or gradual decline?
4. **Index health:** When was last rebuild? Did recall check pass? Any distribution shift in indexed vectors?
5. **Model health:** Did ranking model change? Feature pipeline produce nulls? ONNX model output distribution shifted?
6. **Feature store:** Are all features populating correctly? Null/zero rate for key features?
7. **Reproduce:** Take user's query, replay through system with logging. Compare results to 48h-ago replay.

Most common root causes: (a) feature pipeline broke, producing zeros/nulls for a key feature, (b) index rebuilt from stale embeddings (upstream model didn't update), (c) config change shifted ranking weights accidentally.

---

### Q42: FAISS search suddenly takes 10x longer. GPU utilization is still high. What happened?

**A:** High GPU utilization + slow search = GPU is doing work, but the WRONG work (or too much of it).

**Investigation order:**
1. **nprobe increased?** If nprobe changed from 32 to 256 (config drift), each query scans 8x more clusters. GPU is busy but searching too broadly.
2. **Index imbalanced?** If cluster sizes are highly skewed (one cluster has 80% of vectors), queries hitting that cluster are slow. Check cluster size distribution.
3. **Concurrent workload:** Something else on the GPU (training job, monitoring tool) stealing compute time. `nvidia-smi -q -d PIDS` to check.
4. **Batch size changed?** If upstream is now sending 10x larger batches (upstream batching config changed), GPU is processing correctly but latency per request increases.
5. **GPU thermal throttle?** Check `nvidia-smi -q -d CLOCK` — if clock dropped from 2100 to 1500 MHz, compute is 30% slower.
6. **Memory thrashing?** If index is too large and getting swapped between GPU and host (unified memory), performance collapses. Check `nvidia-smi -q -d MEMORY`.

---

### Q43: Your embedding model gives good offline metrics but poor online engagement. What's wrong?

**A:** Classic "offline/online gap." Common causes:

1. **Train/serve distribution mismatch:** Training on historical clicks (which had position bias). Model learns "what users clicked" not "what users would find useful."
2. **Negative sampling mismatch:** In-batch negatives during training are random; real negatives in production are hard (semantically similar but irrelevant). Model hasn't learned fine-grained distinctions.
3. **Feature leakage:** Training features include information not available at serving time (e.g., item engagement statistics that don't exist for new items).
4. **Metric mismatch:** Optimizing Recall@100 offline but users only see top-10. High recall doesn't guarantee good top-10 ordering.
5. **Missing context:** Offline eval ignores session context, time-of-day, device type. Online users have rich context that the model isn't using.
6. **Latency constraints:** Online system uses truncated embeddings (128d) for speed while offline eval used full 768d.

**Fix:** Close the gap: (a) train with position debiasing, (b) use hard negatives mined from production logs, (c) evaluate on the exact same setup as serving (same index type, same top-K, same filtering).

---

### Q44: You need to serve search for 200 countries with different languages. How?

**A:** Multilingual search architecture:

**Option 1: Multilingual embedding model (preferred):**
- Single model (multilingual-e5, mE5) that encodes all languages into shared vector space
- "cheap eats" (English) and "comida barata" (Spanish) get similar embeddings
- Single index for all languages — simplest operationally
- Works for: 90% of cases where cross-lingual retrieval is acceptable

**Option 2: Per-language indexes + routing:**
- Detect query language → route to language-specific index
- Better precision for language-specific content
- Higher operational cost (200 indexes)
- Works for: markets where cross-lingual results are undesirable

**Option 3: Hybrid:**
- Multilingual index for retrieval (captures cross-lingual semantics)
- Language filter in re-ranking (only show results in user's language)
- Best of both: broad retrieval + language-appropriate results

**Scale consideration at 200 countries:**
- Not all countries need equal capacity. US/EU get dedicated GPU nodes; smaller markets share.
- Content distribution is heavily skewed — index sharding by region makes sense.
- CDN-style: replicate indexes to regional data centers (latency-sensitive for search).

---

### Q45: How do you A/B test a new ranking model when it affects the entire user experience?

**A:** Controlled experimentation for ranking is tricky because:
- Results affect future behavior (exposure bias)
- Network effects (social features depend on others' exposure)
- Long-term effects (engagement today ≠ retention next month)

**Approach:**

**1. User-level randomization:**
Hash(user_id) % 100 determines bucket. Control sees old model; treatment sees new. Ensures consistent experience per user.

**2. Guardrail metrics (stop experiment if violated):**
- No degradation > 2% in CTR, engagement, revenue
- No increase in unsubscribe/churn rate
- No fairness violation (model doesn't discriminate by protected attributes)

**3. Ramp-up schedule:**
- Day 1-3: 1% traffic (validate no crashes/errors)
- Day 4-7: 5% (validate no guardrail violations)
- Day 8-14: 20% (measure engagement with statistical significance)
- Day 15+: 50/50 split (final measurement period)

**4. Metrics to measure:**
- Short-term: CTR, dwell time, conversions
- Medium-term (2 weeks): Sessions per user, return rate
- Long-term proxy: Predicted retention model score

**5. Statistical rigor:**
- Pre-register hypotheses and metrics
- Use sequential testing (stop early if effect is large/clear)
- Account for multiple comparisons (Bonferroni or FDR correction)
- Minimum detectable effect: typically 0.5-1% relative change in primary metric

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
