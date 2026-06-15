# PART 5A — SYSTEM DESIGN: Questions 1-5

## Design Approach for Senior Staff Level

At Senior Staff level, system design is evaluated on:
1. **Problem framing** — Do you understand the business context before diving in?
2. **Breadth AND depth** — Can you sketch the full system AND go deep on critical components?
3. **Tradeoff articulation** — Do you present options with reasoned choices?
4. **Operational maturity** — Do you think about day-2 operations, not just day-1 launch?
5. **Scale awareness** — Do you naturally consider LinkedIn-scale numbers?

---

# 1. Design LinkedIn Feed

## Requirements

### Functional Requirements
- Users see a personalized feed of posts, articles, shares, and ads
- Feed includes content from connections, followed companies, followed topics
- Support reactions, comments, shares (engagement actions)
- Real-time updates for new content while viewing feed
- Support for different content types: text, images, video, articles, polls

### Non-Functional Requirements
- **Latency:** Feed load < 200ms p99
- **Availability:** 99.99% 
- **Scale:** 800M members, 300M MAU, ~100M DAU
- **Freshness:** New posts appear in followers' feeds within 30 seconds
- **Personalization:** Feed ranked by relevance, not just chronology

## Capacity Estimation

```
Users: 800M total, 100M DAU
Feed requests: ~10 feed loads per user/day = 1B feed loads/day
Peak QPS: 1B / 86400 × 3 (peak factor) ≈ 35,000 QPS
Posts created: ~5M posts/day
Average connections: 500 per user
Fan-out per post: 500 connections × 5M posts = 2.5B feed entries/day

Storage:
- Post metadata: 5M posts/day × 2KB = 10GB/day
- Feed entries: 2.5B × 100 bytes = 250GB/day  
- Media: separate CDN-backed storage

Bandwidth:
- Feed response: ~50KB per load (20 items with metadata)
- Peak: 35,000 QPS × 50KB = 1.75 GB/s
```

## APIs

```
GET /v1/feed?user_id={id}&cursor={cursor}&count=20
Response: {items: [{post_id, author, content, engagement, rank_score}], next_cursor}

POST /v1/posts
Body: {author_id, content_type, content, visibility, mentions}

POST /v1/feed/engagement
Body: {user_id, post_id, action: like|comment|share, payload}

GET /v1/feed/notifications (SSE/WebSocket for real-time updates)
```

## Data Model

```
Posts Table:
├── post_id (UUID, partition key)
├── author_id
├── content_type (text, image, video, article, poll)
├── content_blob (or media_url)
├── visibility (public, connections, specific)
├── created_at
├── engagement_counts {likes, comments, shares}
└── metadata (hashtags, mentions, entities)

Feed Index (per user):
├── user_id (partition key)
├── post_id (sort key, recent first)
├── rank_score
├── source (connection, follow, topic, ad)
├── created_at
└── engagement_snapshot

User Graph:
├── user_id → [connection_ids]
├── user_id → [followed_companies]
├── user_id → [followed_topics]
└── user_id → [blocked_users]

Engagement Events:
├── event_id
├── user_id
├── post_id
├── action_type
├── timestamp
└── metadata
```

## High-Level Design

```
┌──────────┐     ┌──────────────┐     ┌──────────────┐
│  Client  │────▶│   API GW /   │────▶│  Feed Service│
│  (App)   │◀────│  Load Balancer│◀────│  (Read Path) │
└──────────┘     └──────────────┘     └──────┬───────┘
                                              │
                              ┌────────────────┼────────────────┐
                              ▼                ▼                ▼
                     ┌──────────────┐  ┌────────────┐  ┌──────────┐
                     │  Feed Cache  │  │   Ranking  │  │  Social  │
                     │  (Redis)     │  │   Service  │  │  Graph   │
                     └──────────────┘  └────────────┘  └──────────┘
                              ▲                ▲
                              │                │
┌──────────┐     ┌────────────┴──┐    ┌───────┴──────┐
│  Post    │────▶│  Fan-out      │    │   ML Ranking │
│  Service │     │  Service      │    │   (Feature   │
└──────────┘     └───────────────┘    │    Store)    │
       │                              └──────────────┘
       ▼
┌──────────────┐
│  Post Store  │
│  (Espresso)  │
└──────────────┘
```

## Deep Dive

### Fan-out Strategy: Hybrid Push/Pull

```
DECISION: Who gets push (pre-computed) vs. pull (computed at read time)?

Push (Fan-out on Write):
- For users with < 10,000 followers
- Write new post_id to each follower's feed index
- Pro: Fast reads (feed already assembled)
- Con: Expensive writes for popular users

Pull (Fan-out on Read):
- For users with > 10,000 followers (celebrities, influencers)
- At feed read time, query celebrity posts and merge
- Pro: Cheap writes
- Con: Slightly slower reads (but cacheable)

HYBRID APPROACH:
┌─────────────────────────────────────────────────────────┐
│ New Post Published                                       │
├─────────────────────────────────────────────────────────┤
│ IF author.followers < 10,000:                           │
│   → Fan-out to all followers' feed indexes (async)      │
│   → Write to Kafka topic: feed-fanout                   │
│   → Workers consume and write to feed index             │
│ ELSE:                                                   │
│   → Store post in "celebrity posts" index               │
│   → At read time: merge celebrity posts with feed index │
└─────────────────────────────────────────────────────────┘
```

### Ranking Pipeline

```
FEED ASSEMBLY (at read time):
1. Retrieve candidate posts from feed index (pre-filtered by fan-out)
2. Merge celebrity posts (pull-based)
3. Apply business rules (remove blocked users, expired content)
4. Feature extraction (author features, content features, user features, interaction features)
5. ML ranking model scores each candidate
6. Re-rank with diversity constraints (no 5 posts from same author)
7. Insert ads at designated positions
8. Return top N with pagination cursor

RANKING FEATURES:
- Author authority score (engagement rate, connection strength)
- Content freshness (time decay function)
- Content quality signals (media richness, length, engagement rate)
- Social proof (how many of my connections engaged)
- User affinity (past interactions with this author)
- Topic relevance (user's declared and inferred interests)
```

### Multi-Stage Ranking: Retrieval → Scoring → Re-rank

```
The feed is NOT just "fetch fan-out rows + ML score". At scale it's a multi-stage
funnel identical in shape to the recommendation stack (see #8):

STAGE 0 — CANDIDATE SOURCES (recall, thousands of items):
├── Fan-out feed index (connections' recent posts)
├── Two-tower EMBEDDING retrieval: out-of-network posts the user may like
│     user_tower(user) · post_tower(post) → ANN top-K (FAISS/HNSW)
├── Follows (companies, topics, creators, newsletters)
├── Trending / viral in user's network or industry
└── Ads candidate pool (separate auction path)

STAGE 1 — LIGHT RANKER (filter thousands → hundreds):
- Cheap GBDT / small DNN scores all candidates fast (CPU)
- Drop obvious low-relevance before the expensive model

STAGE 2 — HEAVY RANKER (hundreds → ordered):
- Deep model (DLRM / wide-and-deep / DCN) with feature crosses
- Served on GPU (Triton/TF-Serving) with continuous/dynamic batching
- Multi-task heads: P(like), P(comment), P(share), P(dwell), P(hide)
  → final score = weighted combination tuned to objective (value model)

STAGE 3 — RE-RANK / POLICY:
- Diversity (no 5 posts from one author), freshness boost, dedup seen
- Ad insertion at fixed slots, integrity/safety filters
```

### Two-Tower Candidate Generation (Out-of-Network)

```
WHY: Connections alone make a thin feed. Two-tower finds relevant out-of-network
content from millions of posts without scoring them all.

OFFLINE:  post_tower embeds every recent post → vectors → ANN index (rebuilt often
          because posts expire fast; "fresh" ANN index updated every few minutes)
ONLINE:   user_tower embeds the user (cached, refreshed on activity) → ANN top-K
TRAINING: implicit feedback (clicks/dwell) with in-batch + hard negatives;
          embeddings refreshed via batch (Spark) + streaming (Flink) features.

Serve user/post embeddings from the Feature Store (#7); ANN index is partitioned
by post recency (hot fresh shard vs. cold) — same incremental/full split as Search.
```

### Model Serving Infrastructure

```
- Heavy ranker on GPU inference servers (NVIDIA Triton / TF-Serving / TorchServe)
- DYNAMIC BATCHING: group requests within a few ms to fill the GPU → 5-10x throughput
- Feature fetch from online Feature Store (Redis/Couchbase) — co-fetch in one RPC
- Model registry + shadow/canary deploys; A/B via traffic split (see #8)
- Fallbacks: GPU/ranker down → light GBDT ranker → chronological (graceful degrade)
- Distillation: big teacher model → smaller student for latency-critical path
```

### Real-Time Updates

```
APPROACH: Server-Sent Events (SSE) for online users

1. User opens feed → Establish SSE connection
2. New posts fan-out → Check if recipient is online
3. If online: push notification through SSE channel
4. Client receives: "3 new posts available" badge
5. User clicks → Fetch new posts (normal feed request with updated cursor)

WHY NOT WEBSOCKET:
- Feed updates are server→client only (no client→server for feed)
- SSE is simpler, works with HTTP/2, auto-reconnects
- Lower infrastructure cost (no bidirectional state)

CONNECTION MANAGEMENT:
- Connection pool per gateway instance
- 100M DAU × 30% concurrently online = 30M connections
- Distributed across 1000+ gateway instances = 30K connections per instance
- Heartbeat every 30 seconds to detect dead connections
```

## Tools & Frameworks (with trade-offs)

```
FAN-OUT / STREAM:        Kafka (durable async fan-out) + Flink/Samza (feature compute)
FEED STORE:              LinkedIn Espresso / DynamoDB / Cassandra (per-user feed rows)
FEED CACHE:              Redis Cluster / Couchbase (assembled feed, 99% hit)
CANDIDATE RETRIEVAL:     FAISS / HNSW two-tower ANN (out-of-network)
RANKING SERVING:         Triton / TF-Serving / TorchServe (GPU) + GBDT (CPU light)
FEATURE STORE:           online Redis/Couchbase + offline (see #7)
REAL-TIME PUSH:          SSE over HTTP/2 (server→client only)
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Fan-out | Push (write) | Pull (read) | Hybrid: push for normal users, pull for celebrities |
| Candidate gen | Graph fan-out only | + Two-tower ANN | Add ANN for richer out-of-network discovery |
| Ranker serving | CPU GBDT | GPU deep model | GBDT for light stage; GPU heavy model for top candidates |
| Feed store | Cassandra (AP) | Espresso/Dynamo | AP store fine — feed tolerates eventual consistency |
| Real-time | SSE | WebSocket | SSE (one-way updates); WS only if bidirectional needed |
| Ranking model | Single-task | Multi-task (value model) | Multi-task to balance like/comment/share/dwell |

## Scaling

- **Read scaling:** Feed cache (Redis Cluster) with 99% hit rate
- **Write scaling:** Kafka-based async fan-out with partitioned workers
- **Data scaling:** Sharded feed indexes by user_id (consistent hashing)
- **ML scaling:** Pre-computed features, cached model inference
- **Geographic scaling:** Multi-region with eventual consistency acceptable for feed

## Reliability

- **Feed cache failure:** Fall back to direct database reads (slower but functional)
- **Ranking service failure:** Return chronologically sorted unranked feed (degraded but available)
- **Fan-out service failure:** Posts queue in Kafka, delivered when recovered
- **Circuit breaker:** On ranking service, with fallback to cached scores

## Security

- Visibility enforcement: Every feed item checked against author's visibility settings
- Rate limiting: Feed requests capped per user (prevent scraping)
- Content moderation: ML-based flagging before fan-out
- Privacy: GDPR-compliant data handling, user blocking respected in fan-out

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Hybrid fan-out | Complexity for efficiency at scale |
| ML ranking | Latency for relevance |
| Eventual consistency | Freshness vs. complexity |
| SSE over WebSocket | Simplicity vs. bidirectional capability |
| Pre-computed features | Storage for latency |

## Follow-up Questions

1. "How would you handle feed for a user who just signed up (cold start)?"
2. "How would you A/B test a new ranking model?"
3. "How would you handle content that goes viral (sudden fan-out spike)?"
4. "How would you implement 'People Also Viewed' in the feed?"
5. "How would you ensure feed diversity (not all same content type)?"

---

# 2. Design LinkedIn Search

## Requirements

### Functional Requirements
- Search across members, posts, jobs, companies, groups
- Type-ahead / auto-complete suggestions
- Faceted search with filters (location, company, title, date)
- Relevance ranking personalized to searcher
- Federated search across multiple entity types

### Non-Functional Requirements
- **Latency:** < 100ms p99 for search results, < 50ms for typeahead
- **Availability:** 99.99%
- **Scale:** 1B+ documents indexed, 500M+ searches/day
- **Freshness:** New content searchable within 5 minutes
- **Relevance:** High precision in top 5 results

## Capacity Estimation

```
Documents: 1B+ (800M member profiles + posts + jobs + companies)
Searches: 500M/day = ~6,000 QPS average, ~18,000 QPS peak
Index size: 1B docs × 5KB avg = 5TB raw, ~2TB compressed index
Typeahead: 3B/day (5-6 keystrokes per search) = ~35,000 QPS
Updates: 50M document updates/day (profile edits, new posts)
```

## APIs

```
GET /v1/search?q={query}&type={people|posts|jobs|all}&filters={}&cursor={}&count=10
Response: {results: [{entity_type, entity_id, snippet, score}], facets, suggestions, total}

GET /v1/typeahead?prefix={prefix}&context={user_id}
Response: {suggestions: [{text, type, entity_id, icon}]}

POST /v1/search/index  (internal - for indexing pipeline)
Body: {entity_type, entity_id, document, timestamp}
```

## Data Model

```
Search Index (Inverted Index):
├── term → [{doc_id, field, position, tf-idf score}]
├── document store: doc_id → {entity_type, fields, metadata}
└── field indexes: title, description, skills, company, location

Member Profile Document:
├── member_id
├── name (analyzed: tokenized, lowercased, phonetic)
├── headline
├── current_title, current_company
├── skills[] (weighted by endorsements)
├── location
├── industry
├── connections_count
├── profile_strength_score
└── last_active

Post Document:
├── post_id
├── author_id, author_name
├── content (full-text analyzed)
├── hashtags[]
├── engagement_score
├── created_at
└── visibility

Typeahead Index (Prefix Trie + Popularity):
├── prefix → [{suggestion, type, score, entity_id}]
├── Personalized layer: prefix + user_context → suggestions
└── Updated in near-real-time from search logs
```

## High-Level Design

```
┌──────────┐     ┌──────────┐     ┌───────────────────────────────────┐
│  Client  │────▶│  API GW  │────▶│        Search Orchestrator         │
└──────────┘     └──────────┘     └──────┬──────────┬─────────┬───────┘
                                         │          │         │
                              ┌──────────▼───┐  ┌──▼────┐  ┌─▼────────┐
                              │  People      │  │ Posts  │  │  Jobs    │
                              │  Search      │  │ Search │  │  Search  │
                              │  (Galene)    │  │        │  │          │
                              └──────┬───────┘  └──┬────┘  └──┬───────┘
                                     │             │           │
                              ┌──────▼─────────────▼───────────▼───────┐
                              │         Ranking / Blending Layer        │
                              └────────────────────────────────────────┘
                                                   ▲
                                                   │
┌─────────────────┐     ┌──────────────┐     ┌────┴──────────┐
│  Indexing       │────▶│   Kafka      │────▶│  Index Builder │
│  Pipeline       │     │   (Changes)  │     │  (Near RT)     │
└─────────────────┘     └──────────────┘     └───────────────┘
```

## Deep Dive

### Indexing Architecture (LinkedIn uses Galene)

```
INDEXING PIPELINE:
1. Source systems emit change events (profile edit, new post, job posted)
2. Change events flow through Kafka
3. Index builder consumes events, transforms to search documents
4. Documents written to search index shards
5. Near-real-time: new content searchable within seconds

INDEX STRUCTURE:
├── Sharding: By document_id hash (even distribution)
├── Replication: 3 replicas per shard (availability)
├── Segments: Immutable segments with periodic merging
├── Fields: Analyzed (full-text) + Keyword (exact match) + Numeric
└── Updates: Soft-delete old doc + add new doc (immutable segments)

INVERTED INDEX INTERNALS:
Term: "engineer"
├── Posting List: [doc1:pos2, doc5:pos1, doc9:pos3:pos7, ...]
├── Term Frequency per doc
├── Document Frequency (for IDF calculation)
└── Skip lists for fast intersection

ADVANCED FEATURES:
- Phonetic matching (for name search): "Shailesh" matches "Shailesh"
- Synonym expansion: "SWE" → "Software Engineer"
- Stemming: "engineering" → "engineer"
- Entity recognition: "Apple" → company vs. fruit (context-dependent)
```

### Query Processing Pipeline

```
QUERY FLOW:
1. Query Parsing
   ├── Tokenization and normalization
   ├── Intent classification (navigational vs. exploratory)
   ├── Entity extraction ("Python engineer at Google in SF")
   └── Query expansion (synonyms, related terms)

2. Query Routing
   ├── Determine which indexes to query (people, posts, jobs, all)
   ├── Fan-out to relevant index shards
   └── Parallel execution with deadline propagation

3. Retrieval (per shard)
   ├── Boolean retrieval (must-match terms)
   ├── Scoring (BM25 base + custom signals)
   ├── Top-K per shard
   └── Early termination (WAND algorithm)

4. Merging & Ranking
   ├── Merge top-K from all shards
   ├── Apply ML re-ranking model
   ├── Personalization signals (connection distance, industry, viewed)
   └── Diversity constraints

5. Result Assembly
   ├── Snippet generation (highlight matching terms)
   ├── Facet computation (aggregations for filters)
   ├── Spell-check suggestions
   └── Related searches
```

### Typeahead System

```
ARCHITECTURE:
├── In-memory prefix index (Trie or sorted array)
├── Sharded by prefix first character
├── Personalized: blend global popularity with user's network
├── Updated every few minutes from search logs

RANKING SIGNALS FOR SUGGESTIONS:
1. Query frequency (how often searched globally)
2. Recency (trending queries ranked higher)
3. Personal relevance (user's connections, interests)
4. Entity signals (verified profiles rank higher)

LATENCY OPTIMIZATION:
- Serve from memory (no disk IO)
- Prefix index fits in RAM (~50GB for all suggestions)
- Edge caching for common prefixes
- Debounce on client (100ms between keystrokes)
```

### Semantic / Vector Search (Embedding Retrieval)

```
WHY: Keyword (BM25) misses meaning. "ML engineer" should match a profile that
says "deep learning researcher". Semantic search retrieves by MEANING using
dense vector embeddings + Approximate Nearest Neighbor (ANN) search.

TWO-TOWER (Dual-Encoder) RETRIEVAL MODEL:
┌──────────────────┐                      ┌──────────────────┐
│  Query Tower     │                      │  Document Tower  │
│  (encodes the    │                      │  (encodes each   │
│   search query)  │                      │   profile/post)  │
└────────┬─────────┘                      └────────┬─────────┘
         │ q_vec (768-d)                           │ d_vec (768-d)
         └──────────────► cosine / dot product ◄───┘
                          score = sim(q_vec, d_vec)

- Document tower runs OFFLINE: embed all 1B docs in batch → store vectors in ANN index
- Query tower runs ONLINE: embed the query at request time (must be < 10ms)
- Retrieval = ANN top-K over the document vectors
- Models: LinkedIn uses fine-tuned BERT-family encoders; query tower distilled
  for low latency. Trained with in-batch negatives + hard negatives.

ANN ALGORITHMS & LIBRARIES (the core ask):
┌──────────┬───────────────────────────────────────────────────────────────┐
│ HNSW     │ Graph-based. Best recall/latency on CPU. High RAM. Hard to      │
│ (graph)  │ update in place. Default in Lucene 9+, FAISS, pgvector, Vespa.  │
│ IVF-PQ   │ Inverted file + Product Quantization. Compresses vectors 10-30x │
│ (FAISS)  │ → billions of vectors fit in RAM. Slight recall loss. Tunable.  │
│ ScaNN    │ Google's anisotropic quantization. SOTA recall/latency on CPU.  │
│ DiskANN  │ Graph on SSD → serve billions per node when RAM-bound. Higher   │
│          │ latency (disk hops) but huge capacity. Good cost/scale balance. │
└──────────┴───────────────────────────────────────────────────────────────┘

GPU vs CPU FOR VECTOR INDEXES:
- GPU (FAISS-GPU, NVIDIA cuVS/RAFT): 10-50x faster batch search & index BUILD.
  Use for: offline index construction, brute-force on huge K, high-QPS surfaces.
  Cost: GPUs expensive, limited HBM (40-80GB) caps in-memory vectors per card.
- CPU (HNSW/ScaNN on many cores): cheaper per QPS for online serving at LinkedIn
  scale, easier to colocate with the lexical index. Most ONLINE serving is CPU.
- PRACTICAL SPLIT: build & re-embed on GPU fleet; serve online on CPU shards.
  Use GPU online only for surfaces with very high QPS or large candidate sets.

QUANTIZATION (fit 1B vectors in RAM):
- float32 (4B/dim × 768 = 3KB/vec) → 1B vecs = 3TB (too big per node)
- PQ / OPQ / Scalar-Quant → ~96-192 bytes/vec → 1B = ~100-200GB (shardable)
- Trade-off: more compression = lower recall. Tune nbits/subquantizers per SLA.
```

### Hybrid Retrieval (Lexical + Semantic)

```
PRODUCTION SEARCH IS HYBRID — neither alone is enough:
- Lexical (BM25/Galene): exact terms, names, IDs, rare tokens, filters
- Semantic (ANN): meaning, synonyms, paraphrases, vague queries

FUSION:
1. Run BM25 retrieval AND ANN retrieval in parallel (each returns top-K)
2. Merge candidate sets (union, dedup by doc_id)
3. Fuse scores: Reciprocal Rank Fusion (RRF) or learned linear blend
4. Pass merged candidates to the ML re-ranker (cross-encoder, see below)

RE-RANKING (cross-encoder / LTR):
- Retrieval is "recall-cheap" (bi-encoder, fast). Re-rank is "precision-expensive".
- Cross-encoder (query+doc together through a transformer) → far higher accuracy,
  but only feasible on the top ~100-1000 candidates (too slow for millions).
- Or GBDT/LTR (XGBoost/LambdaMART) over hand+embedding features for low latency.
- Serve re-ranker on GPU (TF-Serving / Triton / TorchServe) or CPU GBDT.
```

### Index Partitioning & Sharding Strategy

```
TWO ORTHOGONAL PARTITIONING AXES:

1. DOCUMENT PARTITIONING (sharding) — split the corpus:
   ├── Hash-based: shard = hash(doc_id) % N → even load, scatter-gather all shards
   ├── Entity-type: separate indexes for people / posts / jobs / companies
   │     (different schemas, ranking, freshness, hardware sizing)
   ├── Time-based: posts/news sharded by recency (hot recent shard, cold archive)
   └── Semantic/cluster: route query only to relevant IVF cells (skip most shards)

2. TERM/FIELD PARTITIONING (rare): split by term — generally avoided, hot terms
   create skew. Document partitioning is the standard at LinkedIn scale.

QUERY FAN-OUT (scatter-gather):
- Broker fans query to all (or routed subset of) shards in parallel
- Each shard returns its local top-K (with early termination / WAND)
- Broker merges → global top-K → re-rank
- Deadline propagation: slow shard is dropped (serve partial, mark degraded)

REPLICATION: each shard has R replicas (read scaling + HA). Shard count sized by
RAM (vectors must fit) and per-shard QPS, not just disk.
```

### Incremental vs. Full Index Builds

```
THE TWO BUILD MODES (a classic LinkedIn/Galene topic):

FULL (BASE) BUILD — offline, periodic (e.g., daily/weekly):
- Spark/MapReduce job reads the ENTIRE source of truth (profiles, posts, jobs)
- Re-embeds docs (GPU fleet) + rebuilds inverted + ANN index from scratch
- Produces immutable, optimized, compacted segments (best query performance)
- Atomically swapped in via versioned alias (blue/green) → zero-downtime
- WHY NEEDED: schema/analyzer/model changes, segment compaction, drift fixes,
  ANN graphs degrade after many in-place edits → periodic clean rebuild

INCREMENTAL (LIVE / NRT) BUILD — continuous, seconds-fresh:
- Source changes (profile edit, new post) → Kafka → index builder
- Writes to a small, mutable "live" segment layered ON TOP of the base index
- Lucene-style: new docs in new segments; updates = soft-delete + add
- Query = base segments ∪ live segments, merged at read time
- Periodic segment merge keeps live segment small

LAMBDA-STYLE TWO-LAYER SERVING:
  ┌─────────────────────┐     ┌─────────────────────┐
  │  BASE INDEX (huge,   │  +  │  LIVE INDEX (tiny,   │  → unified query
  │  immutable, daily)   │     │  mutable, seconds)   │
  └─────────────────────┘     └─────────────────────┘
- On next full build, live changes fold into the new base → live resets

ANN-SPECIFIC CHALLENGE: HNSW/IVF don't love high-rate in-place updates.
Strategy: serve recent vectors from a small "fresh" ANN index (rebuilt often),
big base ANN index rebuilt on the batch cadence; query both, merge.

BACKFILL / REINDEX: triggered by mapping or embedding-model change → replay
Kafka or re-scan source through the full-build pipeline into a new index version.
```

## Tools & Frameworks (with trade-offs)

```
SEARCH ENGINE / INVERTED INDEX:
- Lucene/Elasticsearch/OpenSearch: ubiquitous, rich, NRT. Heavier, JVM GC tuning.
- Vespa (Yahoo/Vespa.ai): native hybrid (tensor + lexical) + ranking in one engine
  → fewer moving parts; steeper learning curve, smaller community.
- LinkedIn Galene: in-house, tuned for graph-aware ranking & scale; not OSS.
  Pick OSS (ES/OpenSearch) for portability; in-house/Vespa for tight latency+ranking.

VECTOR / ANN:
- FAISS (Meta): fastest, GPU+CPU, library not a service → you build serving around it.
- ScaNN (Google): best CPU recall/latency; less ecosystem tooling.
- HNSWlib: simple, great CPU recall; RAM-heavy, weak at deletes.
- Vespa / Vald / Milvus / Qdrant / Weaviate: full vector DBs (sharding, replication,
  filtering, CRUD built in) → faster to production; less control than raw FAISS.
- pgvector: easy if already on Postgres + modest scale; not for 1B vectors.

EMBEDDING MODELS / INFERENCE:
- BERT/Sentence-Transformers fine-tuned (two-tower). Distill query tower for latency.
- Serving: Triton / TF-Serving / TorchServe (GPU) for the encoder & cross-encoder.

INDEX BUILD PIPELINE:
- Spark / Hadoop for full builds; Kafka + Flink/Samza for incremental.
- Beam if you want one model for batch + streaming (portability vs. extra abstraction).

RANKING:
- XGBoost/LightGBM (LambdaMART) for fast LTR; deep cross-encoders for top-K precision.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Retrieval | Lexical BM25 | Semantic (two-tower ANN) | Hybrid in prod — names need lexical, meaning needs vectors |
| ANN index | HNSW (CPU, high recall) | IVF-PQ (RAM-efficient, billions) | HNSW for ≤100M/node; IVF-PQ/DiskANN at 1B scale |
| ANN hardware | CPU (ScaNN/HNSW) | GPU (FAISS-GPU/cuVS) | CPU for online serving cost; GPU for build + very high QPS |
| Build cadence | Full (daily) | Incremental (NRT) | Both — base for quality/compaction, live for freshness |
| Engine | Elasticsearch/OpenSearch | Vespa / in-house | ES for ecosystem; Vespa/in-house for hybrid+latency control |
| Re-ranker | GBDT/LTR (fast) | Cross-encoder (accurate) | GBDT broadly; cross-encoder on top-100 for precision |

## Scaling

- **Index sharding:** Horizontal partitioning by document hash
- **Query fan-out:** Parallel scatter-gather across shards
- **Caching:** Result cache for popular queries (5-minute TTL)
- **Tiered storage:** Hot index in SSD, warm segments in HDD
- **Replication:** 3x replication for read scaling and HA

## Reliability

- **Index corruption:** Rebuild from source of truth (Kafka replay)
- **Shard failure:** Route to replica (transparent failover)
- **Ranking failure:** Fall back to BM25 scoring (no ML)
- **Indexing lag:** Serve stale results (better than no results)

## Security

- Access control in search results (respect visibility settings)
- Query sanitization (prevent injection attacks)
- Rate limiting (prevent scraping via search)
- Audit logging for compliance (who searched for whom)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Near-real-time indexing | Infrastructure cost for freshness |
| ML re-ranking | Latency for relevance |
| Federated search | Complexity for unified experience |
| Prefix-based typeahead | Memory for speed |
| Per-shard Top-K | Approximate results for latency |

## Follow-up Questions

1. "How would you handle a query that matches 10M documents?"
2. "How would you implement 'People Also Search For'?"
3. "How would you handle search during index rebuilds?"
4. "How would you implement semantic search (not just keyword match)?"
5. "How would you handle multi-language search?"

---

# 3. Design Messaging Platform

## Requirements

### Functional Requirements
- 1:1 messaging between members
- Group messaging (up to 50 participants)
- Read receipts, typing indicators
- Message search within conversations
- File/image sharing
- Message reactions
- Offline message delivery

### Non-Functional Requirements
- **Latency:** Message delivery < 200ms for online users
- **Availability:** 99.99%
- **Scale:** 800M users, 100M DAU, ~1B messages/day
- **Ordering:** Messages strictly ordered within a conversation
- **Durability:** Zero message loss
- **Consistency:** Messages visible to all participants in same order

## Capacity Estimation

```
Messages: 1B/day = ~12,000 messages/sec average, ~36,000 peak
Active conversations: 500M concurrent threads
Online users: 30M simultaneously connected
Storage: 1B messages × 1KB avg = 1TB/day, 365TB/year
Connections: 30M WebSocket connections
Fan-out: Average group size 3 → 3B delivery events/day
```

## APIs

```
WebSocket: wss://messaging.linkedin.com/ws?token={auth_token}

// Send message
{type: "send", conversation_id, content, content_type, client_msg_id}

// Receive message
{type: "message", conversation_id, msg_id, sender_id, content, timestamp}

// Typing indicator
{type: "typing", conversation_id, user_id, is_typing: bool}

// Read receipt
{type: "read", conversation_id, user_id, last_read_msg_id}

// REST APIs for non-real-time operations
GET /v1/conversations?user_id={id}&cursor={cursor}
GET /v1/conversations/{id}/messages?cursor={cursor}&count=50
POST /v1/conversations (create new conversation)
```

## Data Model

```
Conversations:
├── conversation_id (partition key)
├── type (1:1 | group)
├── participant_ids[]
├── created_at
├── last_message_at
├── last_message_preview
└── metadata (name for group chats, settings)

Messages:
├── conversation_id (partition key)
├── message_id (sort key, monotonically increasing)
├── sender_id
├── content
├── content_type (text, image, file, reaction)
├── timestamp
├── status (sent, delivered, read)
└── client_msg_id (for deduplication)

User Conversations (inbox):
├── user_id (partition key)
├── conversation_id (sort key by last_message_at DESC)
├── unread_count
├── last_read_msg_id
├── muted
└── archived

Connection State:
├── user_id → {gateway_instance_id, connected_at, last_active}
└── Used for message routing to correct WebSocket connection
```

## High-Level Design

```
┌──────────┐    WebSocket     ┌──────────────────┐
│  Client  │◀───────────────▶│  Connection      │
│          │                  │  Gateway         │
└──────────┘                  │  (Stateful)      │
                              └────────┬─────────┘
                                       │
                              ┌────────▼─────────┐
                              │  Message Router  │
                              │  Service         │
                              └────┬───────┬─────┘
                                   │       │
                    ┌──────────────▼┐    ┌─▼──────────────┐
                    │  Message      │    │  Presence      │
                    │  Store        │    │  Service       │
                    │  (Cassandra)  │    │  (Redis)       │
                    └───────────────┘    └────────────────┘
                           │
                    ┌──────▼────────┐
                    │  Kafka        │
                    │  (Async       │
                    │   delivery)   │
                    └───────────────┘
```

## Deep Dive

### Message Delivery Flow

```
ONLINE USER MESSAGE FLOW (< 200ms):
1. Sender → WebSocket → Connection Gateway
2. Gateway → Message Router Service
3. Message Router:
   a. Persist to Message Store (async, but confirm write)
   b. Look up recipient connection in Presence Service
   c. Route to recipient's Gateway instance
4. Recipient Gateway → WebSocket → Recipient Client
5. Recipient sends ACK → Mark as delivered

OFFLINE USER MESSAGE FLOW:
1. Steps 1-3a same as above
3. Presence Service: user is offline
4. Store in "pending delivery" queue
5. When user comes online: drain pending queue
6. Push notification sent via notification service

GROUP MESSAGE FLOW:
1. Sender sends to conversation_id
2. Message Router looks up all participants
3. For each participant:
   - If online: route to their gateway
   - If offline: queue for later delivery
4. Fan-out is parallel but message ordering preserved per conversation

ORDERING GUARANTEE:
- Messages within a conversation have monotonically increasing IDs
- Server assigns message_id (not client) to ensure total order
- Conversation-level lock ensures sequential ID assignment
- Client displays in message_id order regardless of delivery order
```

### Connection Gateway Design

```
GATEWAY ARCHITECTURE:
├── Each gateway handles ~30K WebSocket connections
├── 1000 gateway instances = 30M total connections
├── Stateful: knows which users are connected
├── Registers connections in Presence Service (Redis)
├── Handles authentication, heartbeat, reconnection

CONNECTION LIFECYCLE:
1. Client connects → TLS handshake → Auth token validation
2. Register in Presence: user_id → gateway_instance_id
3. Heartbeat every 30s (client→server ping/pong)
4. Missed heartbeat → 60s grace period → mark offline
5. Reconnection: resume from last received message_id

SCALABILITY:
- Consistent hashing of user_id to gateway (sticky sessions)
- But clients can connect to any gateway (presence lookup handles routing)
- Gateway failure: clients reconnect to another gateway, resume from last_msg_id
- No message loss because messages are persisted before delivery attempt
```

### Message Ordering and Consistency

```
CHALLENGE: Ensuring all participants see messages in same order

SOLUTION: Conversation-level sequencing
1. Each conversation has a sequence counter (atomic)
2. When message arrives, atomically increment and assign sequence number
3. All participants display by sequence number
4. If messages arrive out of order, client buffers and sorts

IMPLEMENTATION:
- Sequence counter stored in Redis (INCR is atomic)
- Conversation partition key ensures single-node sequencing
- For high-throughput conversations: batched sequencing

IDEMPOTENCY:
- Client includes client_msg_id (UUID)
- Server deduplicates by client_msg_id within conversation
- Prevents double-sends on network retry
```

## Tools & Frameworks (with trade-offs)

```
CONNECTION LAYER:   WebSocket gateways (Netty/Vert.x) or managed (AWS API GW WS)
PRESENCE/ROUTING:   Redis (user → gateway map), consistent hashing
MESSAGE STORE:      Cassandra / ScyllaDB (partition by conversation_id, wide rows)
DURABLE LOG:        Kafka (write-ahead before persist, fan-out to participants)
SEQUENCING:         Redis INCR per conversation (atomic monotonic message_id)
SEARCH:             Elasticsearch/OpenSearch (async-indexed message content)
MEDIA:              S3/blob + CDN, presigned upload URLs
PUSH (offline):     APNs / FCM via notification platform (#4)
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Transport | WebSocket | Long-poll/SSE | WS for bidirectional chat (typing, receipts) |
| Message store | Cassandra/Scylla (AP) | Espresso/Dynamo | Wide-column AP store fits append-only conversation rows |
| Sequencing | Redis INCR | DB sequence | Redis for low-latency per-conversation ordering |
| Gateway | Self-managed (Netty) | Managed WS | Self-managed at LinkedIn scale (cost/control) |
| Delivery | At-least-once + dedup | Exactly-once | At-least-once + client_msg_id dedup (simpler) |
| Search | Async ES index | Inline | Async — never block send path on indexing |

## Scaling

- **Connection scaling:** Horizontal gateway fleet, consistent hashing
- **Message storage:** Cassandra with conversation_id partitioning
- **Hot conversations:** (celebrity group chats) — dedicated partition handling
- **Message search:** Separate Elasticsearch cluster, async indexing
- **Media:** CDN for images/files, presigned URLs for upload

## Reliability

- **Gateway failure:** Clients reconnect, resume from last message_id
- **Message store failure:** Write to Kafka first (durable), async persist
- **Network partition:** Messages queue, deliver when partition heals
- **Duplicate prevention:** client_msg_id deduplication at server

## Security

- End-to-end encryption option for sensitive conversations
- Message retention policies (compliance, regulatory)
- Spam detection on message content
- Rate limiting per user (messages per minute)
- Abuse reporting and moderation

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| WebSocket over long-polling | Infrastructure complexity for latency |
| Conversation-level ordering | Throughput limited per conversation |
| Cassandra for storage | Eventual consistency for write performance |
| Push + pull for offline | Complexity for completeness |
| Server-assigned IDs | Single point of sequencing for ordering guarantee |

## Follow-up Questions

1. "How would you handle message editing and deletion?"
2. "How would you implement message search across all conversations?"
3. "How would you handle a user with 10,000 unread messages?"
4. "How would you implement disappearing messages?"
5. "How would you handle message delivery confirmation at scale?"

---

# 4. Design Notification Platform

## Requirements

### Functional Requirements
- Multi-channel notifications: push, email, in-app, SMS
- User notification preferences (per channel, per type)
- Batching/digesting (don't send 50 individual emails)
- Priority-based delivery (urgent vs. batched)
- Read/unread tracking for in-app notifications
- Notification templates and personalization
- A/B testing of notification content

### Non-Functional Requirements
- **Latency:** Real-time notifications < 1 second, batched per schedule
- **Availability:** 99.99% (notifications are engagement-critical)
- **Scale:** 800M users, 5B+ notifications/day
- **Reliability:** No duplicate notifications, at-least-once delivery
- **Compliance:** Respect opt-out, CAN-SPAM, GDPR

## Capacity Estimation

```
Notifications generated: 5B/day
Channels: Push (2B), Email (1B), In-app (2B), SMS (100M)
Peak rate: 5B / 86400 × 5 (peak factor) ≈ 300,000 notifications/sec
Push payload: ~1KB per notification
Email: ~10KB per email  
Template renders: 300K/sec at peak
Preference checks: 300K/sec (one per notification)
Storage: notification history: 5B × 200 bytes = 1TB/day
```

## APIs

```
// Internal - Notification request
POST /v1/notifications/send
Body: {
  recipient_ids: [user_ids],
  notification_type: "connection_request",
  priority: "real-time" | "batched",
  template_id: "conn_request_v2",
  template_data: {sender_name, sender_title},
  channels: ["push", "email", "in_app"],
  dedup_key: "conn_req_{sender}_{recipient}"
}

// External - User facing
GET /v1/notifications?user_id={id}&cursor={cursor}&count=20
PUT /v1/notifications/{id}/read
PUT /v1/notifications/preferences
GET /v1/notifications/preferences
```

## Data Model

```
Notification Events (Source of truth):
├── notification_id (UUID)
├── recipient_id
├── notification_type
├── source_entity (who/what triggered)
├── template_id + template_data
├── channels_requested[]
├── channels_delivered[]
├── priority
├── created_at
├── status (pending, delivered, read, dismissed)
└── dedup_key

User Preferences:
├── user_id
├── channel_preferences: {
│     push: {enabled: true, quiet_hours: "22:00-08:00"},
│     email: {enabled: true, frequency: "daily_digest"},
│     sms: {enabled: false}
│   }
├── type_preferences: {
│     connection_request: {push: true, email: true},
│     post_engagement: {push: true, email: false},
│     job_alert: {push: false, email: true}
│   }
└── global_mute: false

Notification Inbox (In-app):
├── user_id (partition key)
├── notification_id (sort key, descending)
├── type, title, body, icon_url
├── action_url
├── is_read
├── created_at
└── group_key (for batching display)

Delivery Log:
├── notification_id + channel
├── delivery_status (sent, delivered, bounced, failed)
├── provider_response
├── delivered_at
└── retry_count
```

## High-Level Design

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  Event Sources   │────▶│  Kafka           │────▶│  Notification    │
│  (Feed, Msg,     │     │  (notification   │     │  Orchestrator    │
│   Jobs, etc.)    │     │   events topic)  │     │                  │
└──────────────────┘     └──────────────────┘     └────────┬─────────┘
                                                           │
                         ┌─────────────────────────────────┼────────────┐
                         │                                 │            │
                    ┌────▼────┐  ┌───────────────┐  ┌─────▼──────┐    │
                    │Preference│  │  Rate Limiter │  │  Template  │    │
                    │ Service  │  │  & Dedup      │  │  Service   │    │
                    └────┬────┘  └───────┬───────┘  └─────┬──────┘    │
                         │               │                │            │
                         └───────────────┼────────────────┘            │
                                         ▼                             │
                              ┌──────────────────┐                     │
                              │  Channel Router  │                     │
                              └──┬───────┬───┬───┘                     │
                                 │       │   │                         │
                    ┌────────────▼┐  ┌───▼──┐│  ┌──────────┐  ┌──────▼──┐
                    │  Push       │  │Email ││  │  SMS     │  │  In-App │
                    │  Provider   │  │Sender││  │  Provider│  │  Store  │
                    │  (APNS/FCM) │  │      ││  │          │  │         │
                    └─────────────┘  └──────┘│  └──────────┘  └─────────┘
                                             │
                                      ┌──────▼──────┐
                                      │  Batch/     │
                                      │  Digest     │
                                      │  Aggregator │
                                      └─────────────┘
```

## Deep Dive

### Notification Orchestrator Pipeline

```
PROCESSING STAGES:
1. Ingestion: Receive notification event from Kafka
2. Deduplication: Check dedup_key (Redis, TTL-based)
3. Preference Check: User opted into this type + channel?
4. Rate Limiting: Per user per channel per time window
5. Priority Routing:
   - Real-time → immediate delivery queue
   - Batched → aggregation buffer
6. Template Rendering: Personalize content per channel
7. Channel Delivery: Route to appropriate sender
8. Status Tracking: Update delivery status

DEDUPLICATION STRATEGY:
- Dedup key stored in Redis with TTL (e.g., 24 hours)
- Prevents: same connection request notification sent twice
- Idempotent processing at every stage
- Consumer group ensures exactly-once processing from Kafka

RATE LIMITING:
- Per user: max 10 push/hour, max 3 email/day
- Per channel: global rate limits for provider compliance
- Burst allowance: real-time notifications can exceed limits
- Sliding window algorithm (Redis sorted sets)
```

### Batching and Digest System

```
BATCHING LOGIC:
1. Events marked "batchable" go to aggregation buffer
2. Buffer accumulates per user per notification type
3. Trigger conditions:
   - Time-based: every 4 hours, or daily at preferred time
   - Count-based: when accumulated > threshold (e.g., 5 likes)
   - Priority override: urgent event triggers immediate flush

DIGEST EXAMPLES:
"5 people liked your post"  (instead of 5 separate notifications)
"3 new job matches"         (instead of 3 separate emails)
"Weekly network update"     (consolidated weekly email)

IMPLEMENTATION:
- Aggregation buffer: Redis sorted set per user per type
- Scheduled flush: Cron-like scheduler triggers batch processing
- Template: Digest templates handle variable-length content
- Personalization: ML model determines optimal send time per user
```

### ML Relevance & Volume Optimization (the real differentiator)

```
NAIVE notification systems send everything → users disable notifications forever.
The hard problem is DECIDING WHETHER TO SEND AT ALL, on which channel, when.

1. RELEVANCE MODEL — P(click | user, notification):
   - Two-tower / DNN scoring each candidate notification for this user
   - Features: notification type, source affinity, recency, user's past CTR by
     type, time-of-day, device, content embedding
   - Drop notifications below a per-user relevance threshold (personalized)

2. VOLUME / FATIGUE MODEL — "send budget":
   - Each user has a learned daily/weekly notification budget (fatigue-aware)
   - Sending a low-value notif "spends" budget → suppresses future low-value ones
   - Models long-term effect: over-notifying raises disable-rate (a guardrail metric)
   - Often framed as a constrained optimization / multi-armed bandit per user

3. CHANNEL SELECTION — which of push/email/in-app maximizes value:
   - Per-(user, type) model picks channel(s); respects preferences + provider cost

4. SEND-TIME OPTIMIZATION (STO):
   - Predict each user's active windows (per timezone, per day-of-week)
   - Schedule batched/digest notifications for predicted peak-open time
   - Implemented as a scheduled job (see #16) keyed off per-user STO scores

SERVING: relevance/volume scores from a model server (GBDT on CPU is enough for
most; deep model on GPU for high-value surfaces). Features from Feature Store (#7).
Scores cached; the orchestrator's "Preference Check" stage becomes an ML gate.
```

## Tools & Frameworks (with trade-offs)

```
PIPELINE BACKBONE:   Kafka (event ingestion) + Flink/Samza (enrich, aggregate, STO)
DEDUP / RATE LIMIT:  Redis (TTL keys, sliding-window sorted sets)
PREFERENCES STORE:   Espresso/DynamoDB + Redis cache (99.9% hit)
RELEVANCE/VOLUME ML: GBDT (XGBoost/LightGBM) on CPU; DNN on GPU (Triton) for top surfaces
SCHEDULING (STO):    Distributed job scheduler (#16) for timezone-aware sends
PUSH:                APNs (iOS), FCM (Android) — multi-provider failover
EMAIL:               SES / SendGrid / in-house MTA with SPF/DKIM/DMARC
DIGEST AGGREGATION:  Redis sorted sets per (user,type), cron flush
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Send decision | Rule/threshold | ML relevance + volume model | ML to fight fatigue & raise long-term engagement |
| Channel | Fixed per type | Per-user channel model | Model when cost/engagement varies by user |
| Send time | Immediate/fixed | Send-time optimization (STO) | STO for batched/digest; immediate for urgent |
| Delivery | At-least-once | Exactly-once | At-least-once + idempotent dedup (cheaper, sufficient) |
| Relevance serving | CPU GBDT | GPU deep model | GBDT broadly; GPU only for highest-value surfaces |
| Aggregation | Synchronous | Redis buffer + cron flush | Buffer for digests; sync only for real-time |

## Scaling

- **Event ingestion:** Kafka partitioned by recipient_id
- **Preference lookups:** Redis cache (99.9% hit rate)
- **Template rendering:** Stateless, horizontally scalable
- **Channel delivery:** Independent scaling per channel
- **Digest aggregation:** Sharded by user_id

## Reliability

- **Kafka durability:** No notification event lost (replication factor 3)
- **At-least-once delivery:** Retry with idempotency for provider failures
- **Dead letter queue:** Failed notifications for investigation
- **Provider failover:** Multiple push/email providers with automatic failover
- **Backpressure:** If channel capacity saturated, backpressure to orchestrator

## Security

- User data in templates (name, company) from authorized sources only
- Email authentication: SPF, DKIM, DMARC
- Opt-out enforcement at platform level (can't bypass)
- Audit trail for all notifications sent (compliance)
- PII handling in notification content (encryption at rest)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Kafka-based pipeline | Latency for durability and scalability |
| Per-user rate limiting | User experience for system protection |
| Batching | Immediacy for user attention management |
| Multi-provider | Complexity for reliability |
| Redis for dedup/preferences | Memory cost for speed |

## Follow-up Questions

1. "How would you implement notification priority during high-load events?"
2. "How would you handle timezone-aware scheduling for email digests?"
3. "How would you A/B test notification content without biasing results?"
4. "How would you handle notification for a viral post (millions of recipients)?"
5. "How would you implement cross-device notification sync?"

---

# 5. Design Distributed Cache

## Requirements

### Functional Requirements
- Key-value store with get/set/delete operations
- TTL-based expiration
- Support for various data structures (strings, lists, sets, hashes)
- Atomic operations (increment, compare-and-swap)
- Cache invalidation (event-driven and TTL-based)

### Non-Functional Requirements
- **Latency:** < 1ms p99 for reads, < 5ms p99 for writes
- **Availability:** 99.999% (cache unavailability cascades to backend)
- **Scale:** 100TB+ data, 10M+ operations/sec
- **Consistency:** Eventual consistency acceptable (with option for strong)
- **Partition Tolerance:** Must survive network partitions gracefully

## Capacity Estimation

```
Total cached data: 100TB across cluster
Operations: 10M ops/sec (80% reads, 20% writes)
Average value size: 1KB (with outliers up to 1MB)
Nodes: 100TB / 64GB per node = ~1,600 nodes
Replication: 3x = ~4,800 node processes
Network: 10M ops/sec × 1KB = 10GB/sec aggregate throughput
Hot keys: top 1% of keys serve 80% of traffic
```

## APIs

```
// Basic operations
GET(key) → value | null
SET(key, value, ttl_seconds) → ok
DELETE(key) → ok | not_found
EXISTS(key) → bool

// Atomic operations  
INCR(key, delta) → new_value
CAS(key, expected_value, new_value) → ok | conflict

// Batch operations
MGET(keys[]) → values[]
MSET(entries[]) → ok

// Data structure operations
HGET(key, field) → value
HSET(key, field, value) → ok
LPUSH(key, value) → list_length
SADD(key, member) → ok

// Cache management
INVALIDATE(key) → ok
INVALIDATE_PREFIX(prefix) → count
TTL(key) → seconds_remaining
```

## Data Model

```
Cache Entry:
├── key (byte string, max 256 bytes)
├── value (byte string, max 1MB)
├── ttl (seconds, 0 = no expiry)
├── created_at (for TTL calculation)
├── version (for CAS operations)
├── flags (compressed, serialization format)
└── metadata (origin service, cache policy)

Cluster Metadata:
├── Ring: hash_range → node_id mapping
├── Nodes: node_id → {host, port, status, load}
├── Replication: key → [primary, replica1, replica2]
└── Config: replication_factor, consistency_level, eviction_policy
```

## High-Level Design

```
┌──────────────┐     ┌──────────────────┐     ┌──────────────────────┐
│   Client     │────▶│  Cache Client    │────▶│  Cache Proxy         │
│   Service    │     │  Library         │     │  (Optional: for      │
│              │◀────│  (Smart routing) │◀────│   connection pooling) │
└──────────────┘     └──────────────────┘     └──────────┬───────────┘
                                                         │
                                              ┌──────────▼───────────┐
                                              │   Consistent Hash    │
                                              │   Ring               │
                                              └──┬──────┬──────┬────┘
                                                 │      │      │
                                         ┌───────▼┐  ┌──▼───┐  ┌▼───────┐
                                         │Node 1  │  │Node 2│  │Node N  │
                                         │Primary │  │      │  │        │
                                         │+Replica│  │      │  │        │
                                         └────────┘  └──────┘  └────────┘

┌──────────────────────────────────────────────────────────────────────┐
│                    Control Plane                                       │
├──────────────┬────────────────┬──────────────────┬───────────────────┤
│  Membership  │  Rebalancing   │  Health Check    │  Config Manager   │
│  Protocol    │  Coordinator   │  & Failover      │                   │
└──────────────┴────────────────┴──────────────────┴───────────────────┘
```

## Deep Dive

### Consistent Hashing with Virtual Nodes

```
HASH RING:
- 2^32 positions on the ring
- Each physical node owns multiple virtual nodes (150-200)
- Virtual nodes provide even distribution
- Key → hash → find next clockwise node on ring

ADVANTAGES:
- Adding a node: only K/N keys need to move (minimal disruption)
- Removing a node: keys redistribute to neighbors only
- Virtual nodes handle heterogeneous hardware (more vnodes for bigger machines)

REPLICATION:
- Walk clockwise from primary to find N-1 distinct physical nodes
- Write to all N replicas (synchronous for strong consistency, async for eventual)
- Read from any replica (eventual) or quorum (strong)

EXAMPLE:
Key "user:12345:profile" → hash(key) = 0x7A3B...
Ring lookup → Primary: Node-7, Replica: Node-12, Node-23
Write: send to all three, return after primary ACK (eventual)
       OR wait for 2/3 ACKs (quorum/strong)
```

### Eviction Policies

```
EVICTION STRATEGIES:
├── LRU (Least Recently Used): Best general-purpose default
├── LFU (Least Frequently Used): Better for skewed access patterns  
├── TTL-based: Explicit expiration (predictable behavior)
├── Random: Surprisingly good when memory is very tight
└── Adaptive: Switch between LRU/LFU based on access patterns

IMPLEMENTATION (Approximate LRU):
- Don't maintain perfect LRU ordering (too expensive at 10M ops/sec)
- Sample-based: on eviction, sample 5-10 random keys, evict oldest
- Per-slab LRU: group by value size, evict within same slab
- Background eviction: scan expired keys periodically (lazy + active)

MEMORY MANAGEMENT:
- Slab allocation (like memcached): reduce fragmentation
- Pre-allocated memory pools per value size class
- Never return memory to OS (avoid allocation overhead)
- Monitor fragmentation ratio: if > 1.5x, consider compaction
```

### Hot Key Handling

```
PROBLEM: Single key receiving 1M+ ops/sec overwhelms one node

SOLUTIONS:
1. Client-side caching (L1 cache):
   - Cache hot keys in application memory (100ms TTL)
   - Reduces cache cluster load by 90% for hot keys
   - Challenge: invalidation (short TTL or pub/sub invalidation)

2. Key replication (read replicas for hot keys):
   - Detect hot keys (access count > threshold)
   - Replicate to additional nodes
   - Spread reads across all replicas
   - Invalidation: write to all replicas

3. Key splitting:
   - Split "hot_key" into "hot_key:shard:0" through "hot_key:shard:7"
   - Client randomly picks shard for reads
   - Writes go to all shards
   - Works for counters, not for complex values
```

### Cache Invalidation Strategy

```
INVALIDATION APPROACHES:

1. TTL-based (simplest):
   - Set appropriate TTL per key type
   - Accept staleness within TTL window
   - Best for: frequently changing data where some staleness is OK

2. Event-driven (most accurate):
   - Source of truth emits change event (CDC/Kafka)
   - Cache invalidation service consumes events
   - Invalidates relevant cache keys
   - Best for: data that must be fresh

3. Write-through:
   - Application writes to cache AND database atomically
   - Cache always reflects latest write
   - Challenge: write amplification, complex failure modes

4. Write-behind (write-back):
   - Write to cache, async persist to database
   - Lowest write latency
   - Risk: data loss if cache node fails before persist
   - Only for non-critical data

LINKEDIN APPROACH:
- Write-through for user profile data (must be fresh)
- TTL-based for feed ranking scores (30-second staleness OK)
- Event-driven for social graph cache (connection changes)
- Multi-layer: L1 (app memory, 10s) → L2 (Redis, 5min) → DB
```

## Tools & Frameworks (with trade-offs)

```
CACHE ENGINE:    Redis (rich data structures, Lua, Cluster) | Memcached (simple,
                 multi-threaded, lower memory overhead) | Couchbase (persistence+
                 cross-DC) | LinkedIn uses Couchbase & Memcached heavily.
CLIENT:          Smart client w/ consistent-hash ring (no proxy hop) vs.
                 proxy (Twemproxy/mcrouter — thin clients, central control).
LOCAL L1:        Caffeine (JVM) / in-process LRU for hottest keys.
NEAR-CACHE:      Client-side cache w/ invalidation (Redis client tracking).
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Engine | Redis | Memcached | Redis for data structures/atomics; Memcached for pure KV + multi-thread |
| Persistence | In-memory only | Couchbase (disk-backed) | Couchbase when warm restart / durability matters |
| Topology | Smart client | Proxy (mcrouter) | Smart client for latency; proxy for polyglot/central control |
| Consistency | Eventual (async repl) | Quorum | Eventual for cache; quorum only if cache is source-ish |
| Eviction | LRU | LFU / TTL | LFU for skewed access; TTL for predictable freshness |
| Hot key | Client L1 cache | Key replication/split | L1 for read-hot; split for write-hot |

## Scaling

- **Horizontal:** Add nodes, rebalance ring (minimal key movement)
- **Hot key mitigation:** Client-side caching, key replication
- **Multi-region:** Independent clusters per region, async replication
- **Memory optimization:** Compression for large values, slab allocation

## Reliability

- **Node failure:** Replicas serve reads, rebuild from replica for writes
- **Network partition:** Serve stale from available partition (AP)
- **Thundering herd:** Request coalescing (single backend request for many waiters)
- **Cache stampede:** Probabilistic early expiration, mutex locks
- **Cold start:** Pre-warming from database on node start

## Security

- Authentication between client and cache (mutual TLS)
- Network isolation (cache nodes not accessible from public network)
- Encryption at rest for sensitive cached data
- Access control per key prefix (multi-tenant isolation)
- Audit logging for key operations (compliance)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Eventual consistency | Freshness for availability |
| Consistent hashing | Complexity for minimal rebalancing |
| Approximate LRU | Memory efficiency for precision |
| Multi-layer caching | Complexity for latency |
| Client-side routing | Client complexity for proxy elimination |

## Follow-up Questions

1. "How would you handle a cache node running out of memory?"
2. "How would you implement cross-datacenter cache consistency?"
3. "How would you handle a thundering herd problem?"
4. "How would you implement cache warming after a cold deploy?"
5. "How would you monitor cache effectiveness?"
