# PART 5B — SYSTEM DESIGN: Questions 6-10

---

# 6. Design Kafka (Distributed Event Streaming Platform)

## Requirements

### Functional Requirements
- Publish/subscribe messaging with topic-based routing
- Persistent message storage with configurable retention
- Consumer groups with partition-level parallelism
- Message ordering within a partition
- At-least-once, at-most-once, and exactly-once delivery semantics
- Replay capability (re-read from any offset)

### Non-Functional Requirements
- **Throughput:** 10M+ messages/sec per cluster
- **Latency:** < 10ms for produce acknowledgment (async), < 5ms for tail reads
- **Durability:** Zero message loss (replicated, acknowledged writes)
- **Availability:** 99.99% for produce and consume
- **Retention:** Days to years of message history
- **Scalability:** Horizontal scaling by adding brokers and partitions

## Capacity Estimation

```
Messages: 10M/sec = 864B messages/day
Average message size: 1KB
Throughput: 10GB/sec write, 30GB/sec read (3x fan-out average)
Storage: 10GB/sec × 86400 × 7 days retention = 6PB per week
Replication: 3x = 18PB raw storage for 7-day retention
Partitions: 100,000+ partitions across cluster
Brokers: 500+ brokers for a large cluster
```

## High-Level Design

```
┌──────────────┐     ┌──────────────────────────────────────────────────┐
│  Producers   │────▶│                 Kafka Cluster                     │
└──────────────┘     │  ┌─────────────────────────────────────────────┐ │
                     │  │         Controller (Raft/ZooKeeper)          │ │
                     │  └─────────────────────────────────────────────┘ │
                     │                                                   │
                     │  ┌─────────┐  ┌─────────┐  ┌─────────┐         │
                     │  │Broker 1 │  │Broker 2 │  │Broker N │         │
                     │  │         │  │         │  │         │         │
                     │  │Topic-A  │  │Topic-A  │  │Topic-B  │         │
                     │  │ P0(L)   │  │ P0(F)   │  │ P1(L)   │         │
                     │  │ P1(F)   │  │ P1(L)   │  │ P2(L)   │         │
                     │  │Topic-B  │  │Topic-B  │  │Topic-A  │         │
                     │  │ P0(L)   │  │ P0(F)   │  │ P1(F)   │         │
                     │  └─────────┘  └─────────┘  └─────────┘         │
                     └──────────────────────────────────────────────────┘
                                          │
                     ┌────────────────────┼────────────────────┐
                     ▼                    ▼                    ▼
              ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
              │Consumer Grp A│  │Consumer Grp B│  │Consumer Grp C│
              └──────────────┘  └──────────────┘  └──────────────┘
```

## Deep Dive

### Storage Engine (Log-Structured)

```
PARTITION STORAGE:
Topic: "fraud-events", Partition: 3
Directory: /data/fraud-events-3/

├── 00000000000000000000.log    (Segment: offsets 0-999999)
├── 00000000000000000000.index  (Sparse offset → file position index)
├── 00000000000000000000.timeindex (timestamp → offset index)
├── 00000000000001000000.log    (Segment: offsets 1000000-1999999)
├── 00000000000001000000.index
└── ...

SEGMENT STRUCTURE:
Each segment is append-only:
┌────────┬──────────┬─────────┬────────┬──────────┬─────────┐
│Record 0│ Record 1 │Record 2 │Record 3│ Record 4 │  ...    │
└────────┴──────────┴─────────┴────────┴──────────┴─────────┘

Record Format:
├── Offset (8 bytes) — monotonically increasing
├── Timestamp (8 bytes)
├── Key length + Key
├── Value length + Value
├── Headers
├── CRC32 checksum
└── Compression codec

WHY THIS IS FAST:
1. Sequential writes only (no random IO) → saturates disk bandwidth
2. OS page cache for reads (hot data served from memory)
3. Zero-copy: sendfile() syscall — kernel→NIC without user space copy
4. Batching: produce requests batch multiple records in one write
5. Compression: batch-level compression (LZ4/Snappy/ZStd)
```

### Replication Protocol (ISR)

```
IN-SYNC REPLICA SET (ISR):
- Each partition has a Leader and N-1 Followers
- Followers that are "caught up" are in the ISR
- A follower falls out of ISR if it's behind by > replica.lag.time.max.ms

REPLICATION FLOW:
1. Producer sends record to Leader
2. Leader appends to local log
3. Followers fetch from leader (pull-based)
4. Followers append to their local logs
5. Leader tracks: highWatermark = min offset across all ISR replicas

ACKNOWLEDGMENT MODES:
- acks=0: Fire and forget (fastest, may lose data)
- acks=1: Leader acknowledged (fast, may lose on leader crash)
- acks=all: All ISR replicas acknowledged (safest, slowest)

LEADER ELECTION:
- When leader fails, controller picks new leader from ISR
- If ISR is empty: either wait (sacrifice availability) or pick any replica (sacrifice durability)
- Configured by: unclean.leader.election.enable

HIGH WATERMARK:
- Consumers only see messages at or below high watermark
- Ensures consumers never read uncommitted messages
- Prevents "message disappearing" after leader failover
```

### Consumer Groups and Partition Assignment

```
CONSUMER GROUP PROTOCOL:
1. Consumers join a group by contacting Group Coordinator (a broker)
2. Coordinator selects one consumer as "Leader"
3. Leader runs partition assignment algorithm
4. Each consumer assigned a subset of partitions (1:1 or 1:many)
5. Consumer commits offsets to __consumer_offsets topic

PARTITION ASSIGNMENT STRATEGIES:
- Range: Assign contiguous partition ranges (good for co-partitioned topics)
- RoundRobin: Distribute evenly (best for uniform workloads)
- Sticky: Minimize partition movement on rebalance
- Cooperative: Incremental rebalance (no stop-the-world)

REBALANCE TRIGGERS:
- Consumer joins group
- Consumer leaves/crashes (session timeout)
- Partition count changes
- Consumer subscription changes

EXACTLY-ONCE SEMANTICS (EOS):
1. Idempotent producer: Producer ID + sequence number per partition
   - Broker deduplicates by (PID, partition, sequence)
2. Transactions: Group multiple produce + offset commit atomically
   - Transaction coordinator manages 2-phase commit
   - Consumers configured to read_committed see only committed messages
```

## Tools & Frameworks (with trade-offs)

```
COORDINATION:    KRaft (built-in Raft metadata quorum) > ZooKeeper (legacy, extra
                 system to run). New deployments: KRaft.
TIERED STORAGE:  Local NVMe (hot) + S3/HDFS (cold) — KIP-405 tiered storage decouples
                 compute from retention; cheaper long retention, slower cold reads.
SCHEMA:          Confluent Schema Registry / Avro-Protobuf-JSON — enforce compatibility.
CROSS-DC:        MirrorMaker 2 / Confluent Replicator / LinkedIn Brooklin.
ALTERNATIVES:    Pulsar (tiered storage + separate compute/storage, multi-tenant),
                 Redpanda (C++/no-JVM, lower tail latency), Kinesis (managed).
CLIENTS:         librdkafka-based clients; Streams API (Kafka Streams) / Flink for processing.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Metadata | KRaft | ZooKeeper | KRaft for new clusters (one less system) |
| Engine | Apache Kafka | Pulsar / Redpanda | Kafka for ecosystem; Redpanda for tail latency; Pulsar for multi-tenant/geo |
| Retention | Local disk | Tiered (S3) | Tiered for long retention at lower cost |
| Acks | acks=1 | acks=all + min.ISR | all for durability; 1 for throughput |
| Processing | Kafka Streams | Flink | Streams for simple in-app; Flink for complex stateful/windowed |
| Cross-DC | MirrorMaker 2 | Brooklin | MM2 standard; Brooklin for pluggable multi-source |

## Scaling

- **Throughput:** Add partitions (more parallelism) and brokers (more capacity)
- **Storage:** Add brokers, tiered storage (hot: NVMe, cold: S3)
- **Consumers:** Add consumers to group (up to partition count)
- **Multi-DC:** MirrorMaker 2 for cross-datacenter replication

## Reliability

- **Broker failure:** ISR replicas serve as leader (automatic failover)
- **Disk failure:** Replication provides redundancy, rebalance to healthy disk
- **Network partition:** Min ISR ensures write quorum, unclean election configurable
- **Consumer failure:** Partition reassigned to another consumer in group

## Security

- TLS for encryption in transit (client↔broker, broker↔broker)
- SASL authentication (SCRAM, Kerberos, OAuth)
- ACLs for topic-level authorization (produce, consume, admin)
- Encryption at rest (filesystem-level or broker-level)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Partition-level ordering only | Throughput for global ordering |
| Pull-based consumers | Simplicity for minimum latency |
| Log-structured storage | Sequential performance for random read |
| ISR-based replication | Availability for durability guarantee |
| Consumer group rebalance | Exactly-once assignment for consumption pause |

## Follow-up Questions

1. "How would you handle a partition with 100x more traffic than others (hot partition)?"
2. "How would you implement cross-datacenter replication with low latency?"
3. "How would you handle compacted topics for changelog streams?"
4. "How would you migrate consumers without message loss during rebalance?"
5. "How would you implement exactly-once processing across multiple Kafka clusters?"

---

# 7. Design Feature Store

## Requirements

### Functional Requirements
- Store and serve ML features for training and inference
- Online serving (real-time, low-latency) and offline serving (batch, high-throughput)
- Feature versioning and lineage tracking
- Feature sharing across teams (discovery and reuse)
- Point-in-time correctness for training data (avoid data leakage)
- Feature transformation pipeline management

### Non-Functional Requirements
- **Online latency:** < 5ms p99 for feature retrieval
- **Offline throughput:** Process TB-scale training datasets
- **Availability:** 99.99% for online serving
- **Freshness:** Real-time features updated within seconds
- **Consistency:** Point-in-time correct views for training
- **Scale:** 10,000+ features, 1M+ entities, 100K+ QPS

## Capacity Estimation

```
Features: 10,000 unique features across 500 entity types
Entities: 100M (users, items, merchants, etc.)
Online QPS: 100,000 feature lookups/sec (each may request 50-200 features)
Feature values stored: 100M entities × 10K features = 1T values
Online store size: 1T values × 100 bytes avg = 100TB
Offline store: 100TB × 365 days history = 36PB (with compression: ~5PB)
Write rate: 10M feature updates/minute (real-time features)
```

## APIs

```
// Online Feature Serving
GET /v1/features/online
Body: {
  entity_type: "user",
  entity_ids: ["user_123", "user_456"],
  feature_names: ["user_click_rate_7d", "user_avg_session_duration", ...],
  timestamp: null  // null = latest, or specific time for testing
}
Response: {features: {"user_123": {"user_click_rate_7d": 0.034, ...}}}

// Offline Feature Retrieval (for training)
POST /v1/features/offline
Body: {
  entity_type: "user",
  entity_ids: ["user_123", ...],
  feature_names: [...],
  timestamps: ["2024-01-15T10:00:00Z", ...]  // point-in-time
}
Response: Parquet file URL or streaming response

// Feature Registration
POST /v1/features/register
Body: {
  name: "user_click_rate_7d",
  entity_type: "user",
  value_type: "float64",
  description: "7-day rolling click rate",
  owner: "recommendation-team",
  source: {type: "streaming", pipeline: "user-engagement-pipeline"},
  freshness_sla: "5_minutes"
}

// Feature Discovery
GET /v1/features/search?q=click+rate&entity_type=user
```

## Data Model

```
Feature Metadata (Registry):
├── feature_id
├── name (unique within entity_type)
├── entity_type
├── value_type (int, float, string, vector, list)
├── description
├── owner_team
├── source_pipeline
├── freshness_sla
├── created_at, updated_at
├── version
├── tags[]
└── lineage (upstream data sources)

Online Store (Low-latency KV):
├── Key: (entity_type, entity_id, feature_name)
├── Value: feature_value (serialized)
├── Timestamp: when this value was computed
└── Version: feature definition version

Offline Store (Columnar, time-series):
├── entity_id
├── feature_name
├── feature_value
├── event_timestamp (when the event occurred)
├── ingestion_timestamp (when it was stored)
└── Partitioned by: entity_type / date / feature_group
```

## High-Level Design

```
┌──────────────────────────────────────────────────────────────────┐
│                    Feature Computation Layer                       │
├──────────────┬───────────────────┬───────────────────────────────┤
│  Batch       │  Streaming        │  On-Demand                    │
│  (Spark)     │  (Flink/Kafka     │  (Request-time               │
│              │   Streams)        │   computation)                │
└──────┬───────┴─────────┬─────────┴──────────────┬────────────────┘
       │                 │                         │
       ▼                 ▼                         │
┌──────────────┐  ┌──────────────┐                │
│ Offline Store│  │ Online Store │◀───────────────┘
│ (S3/HDFS +   │  │ (Redis/      │
│  Delta Lake) │  │  DynamoDB)   │
└──────┬───────┘  └──────┬───────┘
       │                 │
       ▼                 ▼
┌──────────────┐  ┌──────────────┐     ┌──────────────────┐
│ Training     │  │ Online       │◀────│  ML Inference    │
│ Pipeline     │  │ Serving API  │     │  Service         │
└──────────────┘  └──────────────┘     └──────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                    Feature Registry & Discovery                    │
└──────────────────────────────────────────────────────────────────┘
```

## Deep Dive

### Online Store Design

```
ARCHITECTURE: Redis Cluster + DynamoDB fallback

WHY REDIS:
- Sub-millisecond reads (network + memory access)
- Hash structure: HGET entity:user:123 click_rate_7d
- Batch: HMGET entity:user:123 feat1 feat2 feat3 ... feat50
- Pipelining: request 200 features in one round-trip

OPTIMIZATION FOR ML SERVING:
- Feature groups: Pre-join commonly co-requested features
  Key: "user:123:engagement_features"
  Value: Protobuf/FlatBuffer with all 50 engagement features
  → One read instead of 50

- Tiered freshness:
  Real-time (seconds): Redis (streaming updates)
  Near-real-time (minutes): Redis (micro-batch updates)
  Batch (hours): DynamoDB (daily recomputation)

FAILOVER STRATEGY:
- If Redis unavailable: serve from DynamoDB (higher latency, ~5-10ms)
- If both unavailable: serve stale from local cache (degraded)
- If feature missing: return default value (model handles gracefully)

CAPITAL ONE EXAMPLE:
For fraud decisioning:
- 200 features per transaction
- Served in 2.1ms p99 (Redis cluster, pre-joined feature groups)
- Real-time features (last 5 transactions) updated within 500ms
- Batch features (30-day aggregates) updated hourly
```

### Point-in-Time Correctness

```
PROBLEM: Training data must reflect features AS THEY WERE at prediction time
Otherwise: future data leaks into training → overfit → production performance gap

SOLUTION: Time-travel queries on offline store

IMPLEMENTATION:
1. Store every feature value with its event_timestamp
2. Training query: "Give me user_123's features as of 2024-01-15 10:00:00"
3. For each feature: find latest value WHERE event_timestamp <= requested_time
4. This prevents data leakage from future events

STORAGE FORMAT (Delta Lake / Apache Hudi):
- Append-only writes (never overwrite)
- Time-travel built into storage format
- Efficient for both latest value and historical queries
- Partition by: date, entity_type
- Compaction for old data (reduce file count)

TRAINING DATA GENERATION:
1. Get training labels with timestamps: [(user_123, clicked, 2024-01-15T10:00)]
2. For each label, request features as-of that timestamp
3. Join labels with point-in-time features
4. Result: training dataset with no data leakage
```

### Feature Computation Pipeline

```
STREAMING FEATURES (Kafka Streams / Flink):
Example: "user_click_rate_last_1h"
- Consume click events from Kafka
- Sliding window aggregation (1 hour)
- Emit updated value to online store
- Also emit to offline store (for training)

BATCH FEATURES (Spark):
Example: "user_avg_order_value_30d"
- Scheduled daily (or hourly for higher freshness)
- Read from data warehouse
- Compute aggregations
- Write to both online and offline stores
- Backfill capability for new features

ON-DEMAND FEATURES (Computed at request time):
Example: "time_since_last_login"
- Computed at serving time from stored raw signals
- No pre-computation needed
- Tradeoff: latency vs. storage
- Useful for simple transformations of stored features
```

## Tools & Frameworks (with trade-offs)

```
FEATURE PLATFORM:  Feast (OSS), Tecton (managed), or in-house (LinkedIn Feathr).
ONLINE STORE:      Redis / DynamoDB / Couchbase (sub-ms KV) — latency vs. cost.
OFFLINE STORE:     Delta Lake / Apache Hudi / Iceberg on S3/HDFS — time-travel for
                   point-in-time correctness; columnar (Parquet).
STREAMING COMPUTE: Flink / Kafka Streams / Spark Structured Streaming.
BATCH COMPUTE:     Spark on the warehouse.
SERVING EMBEDDINGS:co-locate vector features w/ ANN (FAISS) for recs (see #8).
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Platform | Feast (OSS) | Tecton/in-house | OSS to start; managed/in-house for scale + governance |
| Online store | Redis | DynamoDB | Redis for lowest latency; Dynamo for managed durability |
| Offline format | Delta Lake | Hudi/Iceberg | Delta for Spark shops; Hudi for upsert-heavy; Iceberg for engine-neutral |
| Freshness | Streaming (Flink) | Batch (Spark) | Streaming for real-time features; batch for aggregates |
| Transform | Pre-compute | On-demand | Pre-compute for hot features; on-demand for cheap transforms |

## Scaling

- **Online reads:** Redis Cluster sharded by entity_id, read replicas
- **Offline storage:** Partitioned by date/entity_type, S3-backed
- **Feature computation:** Separate Flink/Spark clusters, auto-scaling
- **Multi-region:** Independent online stores per region, async replication

## Reliability

- **Online store failure:** Fallback to DynamoDB, then cached defaults
- **Computation pipeline failure:** Features go stale (bounded by SLA)
- **Stale detection:** Monitor feature freshness, alert on SLA breach
- **Data quality:** Validate feature distributions, alert on anomalies

## Security

- Feature-level access control (team ownership)
- PII feature encryption and access logging
- Data lineage for audit (which data sources feed which features)
- GDPR: ability to delete all features for a given entity

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Separate online/offline stores | Complexity for performance |
| Pre-computed features | Storage for latency |
| Point-in-time correctness | Storage (append-only) for training quality |
| Feature groups | Flexibility for read performance |
| Redis for online | Cost for sub-ms latency |

## Follow-up Questions

1. "How would you handle feature drift detection?"
2. "How would you backfill a new feature across 100M entities?"
3. "How would you handle a feature that depends on another team's feature?"
4. "How would you implement feature monitoring and alerting?"
5. "How would you version features when computation logic changes?"

---

# 8. Design Recommendation Platform

## Requirements

### Functional Requirements
- Generate personalized recommendations (people, jobs, content, courses)
- Support multiple recommendation surfaces (feed, sidebar, email, notifications)
- Real-time user signal incorporation (clicks, views, skips)
- Multi-objective optimization (relevance, diversity, freshness, business goals)
- A/B testing framework for model experiments
- Explanation capability ("Why am I seeing this?")

### Non-Functional Requirements
- **Latency:** < 100ms p99 for recommendation serving
- **Availability:** 99.99%
- **Scale:** 800M members, 100M items, 50M+ recommendation requests/day
- **Freshness:** Incorporate user actions within minutes
- **Quality:** Measurable lift in engagement metrics

## Capacity Estimation

```
Recommendation requests: 50M/day = ~600 QPS average, ~2000 QPS peak
Candidates per request: screen 10,000-100,000 → rank top 1,000 → serve top 20
User features: 800M users × 500 features = 400B feature values
Item features: 100M items × 200 features = 20B feature values
User-item interactions: 10B+ historical interactions
Model inference: 2000 QPS × 1000 candidates scored = 2M inferences/sec
```

## High-Level Design

```
┌──────────────┐     ┌──────────────────────────────────────────────────────┐
│  Request     │────▶│            Recommendation Service                     │
│  (User+Context)    │                                                       │
└──────────────┘     │  ┌────────────┐   ┌────────────┐   ┌────────────┐   │
                     │  │ Candidate  │──▶│  Ranking   │──▶│  Business  │   │
                     │  │ Generation │   │  (ML)      │   │  Rules     │   │
                     │  └─────┬──────┘   └─────┬──────┘   └─────┬──────┘   │
                     │        │                │                │           │
                     └────────┼────────────────┼────────────────┼───────────┘
                              │                │                │
               ┌──────────────▼──┐   ┌────────▼───────┐   ┌───▼──────────┐
               │ Candidate Index │   │ Feature Store  │   │ Config       │
               │ (ANN/Inverted)  │   │ (User + Item)  │   │ Service      │
               └─────────────────┘   └────────────────┘   └──────────────┘
                                              ▲
                                              │
               ┌──────────────────────────────┤
               │                              │
        ┌──────▼──────┐              ┌────────▼───────┐
        │ Real-time   │              │ Batch Training │
        │ Signal      │              │ Pipeline       │
        │ (Kafka)     │              │ (Spark/PyTorch)│
        └─────────────┘              └────────────────┘
```

## Deep Dive

### Multi-Stage Ranking Architecture

```
STAGE 1: CANDIDATE GENERATION (10K-100K candidates)
Purpose: Recall — find relevant items from millions
Methods:
├── Collaborative filtering: "Users like you also liked..."
│   - Matrix factorization (ALS)
│   - User-item embedding similarity (two-tower model)
├── Content-based: "Similar to items you've engaged with"
│   - TF-IDF similarity
│   - Embedding similarity (BERT/sentence transformers)
├── Graph-based: "Connected through your network"
│   - Friends-of-friends
│   - Community detection
├── Popularity: "Trending in your industry"
│   - Time-decayed popularity
│   - Segment-specific popularity
└── Rules-based: "New items matching your stated preferences"

Latency budget: 20-30ms
Implementation: ANN index (HNSW/Faiss) for embedding similarity

STAGE 2: RANKING (1K candidates → scored and ordered)Purpose: Precision — order by predicted relevance
Model: Deep learning (DNN with feature crosses)
Features:
├── User features (500): demographics, behavior history, preferences
├── Item features (200): content signals, engagement history, freshness
├── Cross features: user-item affinity, social proof
├── Context features: time, device, session behavior
└── Real-time features: last 5 actions, session intent

Latency budget: 30-50ms
Implementation: TensorFlow Serving / custom inference service
Optimization: Batched inference, feature caching, model distillation

STAGE 3: RE-RANKING (Business Rules + Diversity)
Purpose: Policy enforcement and user experience
Rules:
├── Diversity: No more than 3 items from same category
├── Freshness: Boost items < 24 hours old
├── Business: Insert sponsored content at position 3, 8, 15
├── Deduplication: Don't show items user has already seen
├── Fairness: Ensure representation across creator segments
└── Safety: Filter flagged/moderated content

Latency budget: 10-20ms
Implementation: Rule engine with configurable policies
```

### Two-Tower Retrieval & ANN Serving (deep dive)

```
THE TWO-TOWER (DUAL-ENCODER) MODEL — the workhorse of candidate generation:

┌──────────────────┐                         ┌──────────────────┐
│   USER TOWER     │                         │   ITEM TOWER     │
│  MLP over user   │                         │  MLP over item   │
│  features +      │                         │  features +      │
│  history embeds  │                         │  content embeds  │
└────────┬─────────┘                         └────────┬─────────┘
         │ u_vec (e.g., 128-d)                        │ i_vec (128-d)
         └────────────► score = u_vec · i_vec ◄───────┘   (dot/cosine)

KEY PROPERTY: towers are INDEPENDENT → item vectors are precomputed offline and
indexed; only the user vector is computed online, then ANN finds top-K items.
This is what makes retrieval from 100M items feasible in ~20ms.

TRAINING:
- Positives: user-item engagements (click, apply, connect, dwell)
- Negatives: IN-BATCH negatives (other items in the batch) + HARD negatives
  (sampled popular/impressed-but-not-clicked) → critical for quality
- Loss: sampled softmax / contrastive; correct for popularity bias (logQ correction)
- Frameworks: TensorFlow Recommenders (TFRS), PyTorch; trained on GPU clusters

ANN SERVING (the FAISS/ScaNN/HNSW ask):
┌──────────┬────────────────────────────────────────────────────────────────┐
│ HNSW     │ Graph, best recall/latency on CPU; RAM-heavy; weak in-place edits│
│ IVF-PQ   │ FAISS; quantized → 100M-1B vectors fit in RAM; tunable recall    │
│ ScaNN    │ Google; SOTA CPU recall/latency (anisotropic quantization)       │
│ DiskANN  │ SSD-backed graph for billion-scale when RAM-bound                │
└──────────┴────────────────────────────────────────────────────────────────┘
- GPU (FAISS-GPU / NVIDIA cuVS): index BUILD + very-high-QPS batch search
- CPU (HNSW/ScaNN): most ONLINE serving (cheaper per QPS, colocated with filters)
- Vector DBs (Milvus, Vespa, Vald, Qdrant) wrap ANN with sharding/replication/CRUD
  → faster to prod; raw FAISS gives max control/perf.

FILTERED ANN: recs need constraints (location, seniority, not-already-seen).
Options: pre-filter (restrict candidate subspace) or post-filter (over-fetch K×N
then filter). Vespa/Milvus support hybrid filtered search natively.
```

### Embedding Index Build Pipeline (incremental vs. full)

```
ITEM VECTORS must stay fresh (new jobs/posts/people appear constantly):

FULL BUILD (periodic, offline):
- Spark job re-embeds ALL items with current item tower (GPU fleet)
- Builds optimized ANN index from scratch → atomic versioned swap (blue/green)
- Needed after model retrain, schema change, or graph degradation

INCREMENTAL BUILD (continuous, NRT):
- New/updated items → Kafka → embed on the fly → upsert into a small "fresh"
  ANN index served alongside the base index
- Query = base ANN ∪ fresh ANN, merged; fresh index rebuilt frequently

USER VECTORS:
- Long-term user tower: batch-refreshed (daily) from history
- Real-time component: session embedding updated via Flink within seconds
- Final user vector = blend(long-term, session) computed at request time

PARTITIONING / SHARDING:
- Shard ANN index by item type (jobs/people/content) — different towers & SLAs
- Within type, shard by hash or by cluster (route to relevant IVF cells)
- Replicate shards for QPS + HA; size shards so vectors fit in node RAM
```

### Real-Time Signal Incorporation

```
PROBLEM: User clicked on ML articles 5 minutes ago. 
         Recommendations should immediately reflect this interest.

SOLUTION: Real-time feature updates + lightweight re-scoring

ARCHITECTURE:
1. User action → Kafka event
2. Streaming pipeline updates real-time features:
   - last_5_clicks: [item_ids]
   - session_interests: [topic_embeddings]
   - real_time_click_rate: 0.12
3. Features available in online store within seconds
4. Next recommendation request uses updated features
5. Ranking model weights real-time signals heavily

BONUS: Session-level adaptation
- On first request: use long-term user profile
- After 3+ interactions: incorporate session intent
- "Explore vs. exploit": balance showing known interests vs. discovering new ones
```

### A/B Testing Framework

```
EXPERIMENTATION INFRASTRUCTURE:

1. Traffic Allocation:
   - Hash(user_id + experiment_id) → deterministic bucket
   - Ensures same user always sees same variant
   - Support for mutual exclusivity and layered experiments

2. Metrics Collection:
   - Track engagement: CTR, session duration, return rate
   - Track business: revenue, conversions
   - Track guardrails: diversity, freshness, user complaints
   - Statistical significance: minimum 2 weeks, power analysis

3. Model Variants:
   - Control: current production model
   - Treatment: new model or feature change
   - Shadow: score both, serve control, log treatment predictions

4. Rollout:
   - Experiment: 5% traffic, 2 weeks
   - Ramp: 5% → 20% → 50% (check for scaling effects)
   - Launch: 100% (with monitoring for first 48 hours)
   - Rollback: automatic if guardrail metrics violated
```

## Tools & Frameworks (with trade-offs)

```
RETRIEVAL / ANN:     FAISS, ScaNN, HNSWlib (libraries); Milvus/Vespa/Vald (vector DBs)
EMBEDDING TRAINING:  TensorFlow Recommenders (TFRS) / PyTorch on GPU; two-tower
RANKING MODEL:       DLRM / Wide&Deep / DCN-v2 (deep); XGBoost/LightGBM (light stage)
MODEL SERVING:       NVIDIA Triton / TF-Serving / TorchServe (GPU, dynamic batching)
FEATURE STORE:       online Redis/DynamoDB + offline (see #7); point-in-time correct
STREAMING FEATURES:  Flink / Kafka Streams (session/real-time signals)
BATCH TRAINING:      Spark + GPU clusters; Ray for distributed training/tuning
INDEX BUILD:         Spark (full) + Kafka/Flink (incremental upserts)
EXPERIMENTATION:     in-house A/B platform; bandits for explore/exploit
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Candidate gen | Two-tower ANN | Inverted/graph/heuristics | Blend several sources; two-tower for scale recall |
| ANN library | FAISS/ScaNN (raw) | Vector DB (Milvus/Vespa) | Raw for control/perf; DB for CRUD+sharding speed |
| ANN hardware | CPU serving | GPU serving | CPU for cost; GPU for build + huge candidate sets |
| Ranker | GBDT | Deep (DLRM/DCN) | GBDT light stage; deep for final precision |
| Serving | TF-Serving | Triton | Triton for multi-framework + dynamic batching |
| Filtering | Pre-filter | Post-filter (over-fetch) | Pre-filter for selective constraints; post for loose |
| Fresh items | Full rebuild | Incremental upsert | Both: base periodic + fresh NRT index |

## Scaling

- **Candidate generation:** Pre-computed candidate sets, ANN index sharding
- **Ranking:** GPU inference, batch scoring, model distillation
- **Feature serving:** Feature store (see Design #7)
- **Multi-surface:** Shared infrastructure, surface-specific re-ranking

## Reliability

- **Model serving failure:** Fall back to simpler model (logistic regression)
- **Feature store failure:** Serve with default features (degraded quality)
- **Candidate index failure:** Serve popular items (non-personalized fallback)
- **Cold start:** For new users, serve popular + demographic-based recommendations

## Security

- User data used only with consent (privacy controls)
- Recommendation explanation respects data minimization
- No PII leakage through recommendations (e.g., "because your ex viewed this")
- Bias monitoring and fairness constraints

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Multi-stage pipeline | Complexity for efficiency (can't score 100M items) |
| Real-time features | Infrastructure cost for freshness |
| Deep learning ranking | Training cost + latency for quality |
| ANN for candidates | Approximate results for speed |
| Multiple objectives | Harder to optimize for a single metric |

## Follow-up Questions

1. "How would you handle the cold-start problem for new users and new items?"
2. "How would you measure long-term user satisfaction vs. short-term engagement?"
3. "How would you handle recommendation bias (filter bubbles)?"
4. "How would you implement 'Why am I seeing this?' explanations?"
5. "How would you handle multi-objective optimization (engagement vs. revenue vs. diversity)?"

---

# 9. Design URL Shortener

## Requirements

### Functional Requirements
- Shorten long URLs to short aliases (e.g., lnkd.in/abc123)
- Redirect short URL to original long URL
- Custom aliases (optional)
- Link analytics (click count, geographic distribution, referrers)
- Link expiration (TTL)
- Link management (list, edit, delete user's links)

### Non-Functional Requirements
- **Latency:** Redirect < 10ms p99 (read-heavy)
- **Availability:** 99.99% (broken links = broken trust)
- **Scale:** 100M URLs created/month, 10B redirects/month
- **Durability:** URLs must work for years
- **Read:Write ratio:** 100:1

## Capacity Estimation

```
URL creation: 100M/month = ~40 URLs/sec
Redirects: 10B/month = ~4,000 redirects/sec, peak ~12,000/sec
Storage per URL: ~500 bytes (short_url + long_url + metadata)
Total URLs (5 years): 6B URLs × 500 bytes = 3TB
Key space: 7 characters, base62 = 62^7 = 3.5 trillion (more than enough)
Cache: Top 20% of URLs serve 80% of traffic → ~1.2B URLs cached
Cache size: 1.2B × 500 bytes = 600GB
```

## APIs

```
POST /v1/urls/shorten
Body: {long_url, custom_alias?, expires_at?, user_id?}
Response: {short_url: "lnkd.in/abc123", created_at, expires_at}

GET /v1/urls/{short_code}  → 301/302 Redirect to long_url

GET /v1/urls/{short_code}/stats
Response: {total_clicks, clicks_by_day[], geo_distribution, referrers[]}

DELETE /v1/urls/{short_code}
GET /v1/users/{user_id}/urls?cursor={cursor}
```

## Data Model

```
URLs Table:
├── short_code (primary key, 7 chars)
├── long_url (original URL)
├── user_id (creator, nullable for anonymous)
├── created_at
├── expires_at (nullable)
├── is_active (for soft delete)
└── click_count (denormalized counter)

Analytics Events (append-only):
├── short_code
├── timestamp
├── client_ip (hashed for privacy)
├── user_agent
├── referrer
├── geo (derived from IP)
└── device_type
```

## High-Level Design

```
┌──────────┐     ┌──────────────┐     ┌──────────────────┐
│  Client  │────▶│   CDN/Edge   │────▶│  Redirect        │
│  (Click) │◀────│   Cache      │◀────│  Service         │
└──────────┘     └──────────────┘     └────────┬─────────┘
                                               │
                                    ┌──────────▼─────────┐
                                    │    Cache (Redis)   │
                                    └──────────┬─────────┘
                                               │ (miss)
                                    ┌──────────▼─────────┐
                                    │    URL Database    │
                                    │    (DynamoDB)      │
                                    └────────────────────┘

┌──────────┐     ┌──────────────┐     ┌──────────────────┐
│  Client  │────▶│   API GW     │────▶│  URL Creation    │
│ (Create) │◀────│              │◀────│  Service         │
└──────────┘     └──────────────┘     └────────┬─────────┘
                                               │
                                    ┌──────────▼─────────┐
                                    │  ID Generator      │
                                    │  (Snowflake/Counter)│
                                    └────────────────────┘

Analytics Pipeline:
Click Events → Kafka → Flink → Analytics DB (ClickHouse)
```

## Deep Dive

### Short Code Generation

```
APPROACH 1: Counter-based (Recommended)
- Distributed counter (like Twitter Snowflake)
- Convert counter value to base62
- Guaranteed unique, no collision
- Predictable but not security-sensitive

Implementation:
1. Counter service provides unique 64-bit IDs
2. Take lower 43 bits (enough for 8.7T URLs)
3. Convert to base62 → 7 character code
4. Range pre-allocation: each service instance gets range of 10K IDs

APPROACH 2: Hash-based
- MD5/SHA256(long_url) → take first 7 chars of base62
- Check for collision, retry with salt if collision
- Same URL always generates same short code (dedup)
- Pro: deterministic. Con: collision handling

APPROACH 3: Random
- Generate random 7 base62 characters
- Check database for collision
- Retry if collision (extremely rare with 3.5T keyspace)
- Pro: simple. Con: non-deterministic, needs collision check

CUSTOM ALIASES:
- User provides desired short code
- Check availability → allow if free
- Reserve in database atomically
- Validation: alphanumeric, 3-20 chars, not profanity
```

### Redirect Flow (Optimized)

```
OPTIMIZATION LAYERS:
1. DNS-level: GeoDNS routes to nearest edge
2. CDN cache: Popular URLs cached at edge (TTL: 1 hour)
3. Application cache: Redis with 24-hour TTL
4. Database: DynamoDB single-record lookup

REDIRECT STATUS CODE:
- 301 (Permanent): Browser caches forever. 
  Pro: Fastest repeat visits. Con: Can't track clicks.
- 302 (Temporary): Browser always hits our server.
  Pro: Full analytics. Con: Extra hop on every click.
- CHOICE: 302 for tracked links, 301 for permanent/untracked

LATENCY BREAKDOWN:
- CDN hit: 5ms (edge → client)
- Redis hit: 8ms (edge → nearest POP → Redis → redirect)
- DB hit: 20ms (edge → service → DynamoDB → redirect)
- Cache hit rate target: 99% (hot URLs are very hot)
```

## Tools & Frameworks (with trade-offs)

```
ID GENERATION:   Snowflake-style counter (see #19) | Zookeeper sequencer | DB range alloc
KV STORE:        DynamoDB / Cassandra (short_code → long_url, single-key lookup)
CACHE:           Redis + CDN edge cache (99%+ hit on hot links)
ANALYTICS:       Kafka → Flink → ClickHouse / Druid (async, off redirect path)
ABUSE/SAFETY:    URL reputation (Google Safe Browsing API) at creation
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Code gen | Counter → base62 | Hash of URL | Counter for guaranteed-unique + short; hash for dedup |
| Store | DynamoDB | Cassandra | Dynamo (managed) or Cassandra (self-host) — both single-key KV |
| Redirect | 302 (track) | 301 (cache) | 302 for analytics; 301 for permanent/untracked |
| Analytics | ClickHouse | Druid | CH for SQL/ad-hoc; Druid for real-time rollups |
| Counter | Central service | Per-node ranges | Per-node range alloc to avoid hot counter |

## Scaling

- **Reads:** CDN + Redis cache (99%+ hit rate)
- **Writes:** Partitioned counter service, sharded database
- **Analytics:** Kafka + ClickHouse (async, doesn't impact redirect path)
- **Geographic:** Multi-region deployment, GeoDNS

## Reliability

- **Cache failure:** Fall through to database (higher latency, still works)
- **Database failure:** Serve from cache (eventually stale, but available)
- **Counter service failure:** Pre-allocated ranges per instance (survive minutes)
- **Analytics failure:** Events queue in Kafka (replay when recovered)

## Security

- Malicious URL detection (phishing, malware) before creation
- Rate limiting on creation (prevent abuse)
- Abuse reporting on short links
- No enumeration (can't iterate all URLs)
- HTTPS for all redirects

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| 302 over 301 | Analytics for client latency |
| Counter over hash | Simplicity/uniqueness for determinism |
| Redis cache | Cost for latency |
| Async analytics | Accuracy (slight delay) for redirect speed |
| 7-char codes | Keyspace size for URL length |

## Follow-up Questions

1. "How would you handle a URL that goes viral (millions of redirects/minute)?"
2. "How would you prevent the service from being used for phishing?"
3. "How would you implement link previews (unfurling)?"
4. "How would you implement private links (password-protected)?"
5. "How would you handle URL shortener for a multi-tenant SaaS?"

---

# 10. Design Service Discovery

## Requirements

### Functional Requirements
- Service registration (instances announce their presence)
- Service lookup (clients find available instances)
- Health checking (detect and remove unhealthy instances)
- Load balancing hints (weighted instances, zone-aware routing)
- Service metadata (version, capabilities, configuration)
- Watch/subscribe for changes (real-time updates)

### Non-Functional Requirements
- **Latency:** Lookup < 5ms p99
- **Availability:** 99.999% (service discovery failure = total outage)
- **Consistency:** Eventually consistent (prefer availability over consistency)
- **Scale:** 100,000+ service instances, 1M+ lookups/sec
- **Convergence:** Unhealthy instance removed within 10 seconds

## Capacity Estimation

```
Services: 5,000 unique services
Instances: 100,000 total instances (average 20 per service)
Lookups: 1M/sec (services constantly resolving dependencies)
Registrations: 1,000/sec (instances starting/stopping)
Health checks: 100,000 checks per interval × 4 intervals/min = 400K/min
Metadata per instance: ~2KB
Total registry size: 100,000 × 2KB = 200MB (fits entirely in memory)
Watch subscriptions: 500,000 active watches
```

## APIs

```
// Registration
PUT /v1/services/{service_name}/instances/{instance_id}
Body: {host, port, health_check_url, metadata: {version, zone, weight}, ttl}
Response: {lease_id, ttl}

// Heartbeat (lease renewal)
PUT /v1/leases/{lease_id}/renew
Response: {renewed_ttl}

// Lookup
GET /v1/services/{service_name}/instances?healthy=true&zone={zone}
Response: {instances: [{instance_id, host, port, metadata, health}]}

// Watch (Server-Sent Events)
GET /v1/services/{service_name}/watch
SSE stream: {type: "add"|"remove"|"update", instance: {...}}

// Health status
GET /v1/services/{service_name}/health
Response: {healthy: 18, unhealthy: 2, total: 20}
```

## Data Model

```
Service Registry:
├── service_name (partition key)
├── instances: Map<instance_id, InstanceInfo>
│   ├── instance_id
│   ├── host, port
│   ├── metadata (version, zone, weight, tags)
│   ├── health_status (healthy, unhealthy, unknown)
│   ├── last_heartbeat
│   ├── registered_at
│   └── lease_id, lease_ttl
└── service_metadata (owner, description, SLA)

Health Check Configuration:
├── service_name
├── check_type (HTTP, TCP, gRPC, script)
├── check_interval (seconds)
├── check_timeout (seconds)
├── healthy_threshold (consecutive passes to become healthy)
├── unhealthy_threshold (consecutive fails to become unhealthy)
└── deregister_after (auto-remove after N seconds unhealthy)
```

## High-Level Design

```
┌───────────────┐     ┌──────────────────────────────────────────────┐
│ Service       │────▶│         Service Discovery Cluster             │
│ Instance      │     │                                              │
│ (Register+    │     │  ┌────────────┐  ┌────────────┐            │
│  Heartbeat)   │     │  │  Node 1    │  │  Node 2    │            │
└───────────────┘     │  │  (Leader)  │  │  (Follower)│  ...       │
                      │  └─────┬──────┘  └─────┬──────┘            │
┌───────────────┐     │        │               │                    │
│ Client        │────▶│        ▼               ▼                    │
│ Service       │     │  ┌─────────────────────────────┐            │
│ (Lookup +     │     │  │    Replicated Registry      │            │
│  Watch)       │     │  │    (In-Memory + WAL)        │            │
└───────────────┘     │  └─────────────────────────────┘            │
                      │                                              │
                      │  ┌─────────────────────────────┐            │
                      │  │    Health Checker            │            │
                      │  │    (Distributed across nodes)│            │
                      │  └─────────────────────────────┘            │
                      └──────────────────────────────────────────────┘

CLIENT-SIDE ARCHITECTURE:
┌────────────────────────────────────────────────┐
│  Client Service                                 │
│  ┌──────────────────────────────────────────┐  │
│  │  Service Discovery Client Library         │  │
│  │  ├── Local Cache (in-memory registry)    │  │
│  │  ├── Watch Connection (SSE/gRPC stream)  │  │
│  │  ├── Load Balancer (client-side)         │  │
│  │  └── Fallback (stale cache if SD down)   │  │
│  └──────────────────────────────────────────┘  │
└────────────────────────────────────────────────┘
```

## Deep Dive

### Health Checking Strategy

```
HEALTH CHECK APPROACHES:

1. Active Health Checks (Server-side):
   - Discovery service probes each instance periodically
   - HTTP GET /health → 200 OK = healthy
   - Pro: Authoritative, catches network issues
   - Con: Scalability (100K instances × frequent checks)

2. Passive Health Checks (Heartbeat/Lease):
   - Instance sends heartbeat every N seconds
   - If heartbeat missed for TTL → mark unhealthy
   - Pro: Scales better (instances push, no centralized polling)
   - Con: Only detects process death, not degradation

3. Hybrid (Recommended):
   - Heartbeat as primary liveness signal
   - Active health check for degradation detection
   - Peer-to-peer gossip for fast failure propagation

FAILURE DETECTION TIMING:
- Heartbeat interval: 5 seconds
- Failure threshold: 3 missed heartbeats = 15 seconds
- But network jitter → use phi accrual failure detector
  (adaptive threshold based on heartbeat history)
- After marking unhealthy: immediately notify watchers
- Deregistration: after 60 seconds unhealthy (configurable)

PREVENTING FLAPPING:
- Healthy threshold: Must pass 3 consecutive checks to become healthy
- Unhealthy threshold: Must fail 3 consecutive checks to become unhealthy
- This prevents rapid state oscillation during transient issues
```

### Client-Side Caching and Load Balancing

```
CLIENT-SIDE ARCHITECTURE:

1. Bootstrap:
   - Client connects to discovery cluster
   - Fetches full registry for subscribed services
   - Stores in local memory

2. Watch Updates:
   - Maintains streaming connection (gRPC/SSE)
   - Receives incremental updates (add/remove/modify)
   - Updates local cache immediately
   - If watch connection drops: reconnect + full refresh

3. Load Balancing (client-side):
   - Round-robin (default, simple)
   - Weighted round-robin (respect instance weights)
   - Least connections (route to least busy)
   - Zone-aware (prefer same-zone, fall back to cross-zone)
   - Consistent hashing (for cache-like services)

4. Resilience:
   - If discovery cluster unreachable: use stale local cache
   - Cache remains valid for configured grace period (e.g., 5 minutes)
   - Instances removed from local cache don't affect routing until confirmed
   - Circuit breaker on discovery lookups (don't cascade failure)

WHY CLIENT-SIDE IS IMPORTANT:
- Eliminates discovery service as single point of failure
- Zero additional latency for service resolution (local memory lookup)
- Graceful degradation if discovery cluster has issues
```

### Consistency Model

```
REGISTRY CONSISTENCY:

APPROACH: AP system (Available + Partition-tolerant, Eventual Consistency)

RATIONALE:
- Service discovery MUST be available (if SD is down, everything is down)
- Stale data (serving to a recently-dead instance) is recoverable:
  client gets error, retries with another instance
- Missing data (not knowing about a new instance) reduces capacity but doesn't cause failure

REPLICATION:
- All nodes are peers (no leader for reads)
- Writes go to any node, replicated via gossip/anti-entropy
- Convergence time: < 2 seconds across cluster
- CRDT-based registry (add-wins for registration, remove-wins for deregistration)

CONFLICT RESOLUTION:
- Last-Writer-Wins for instance metadata updates
- Registration wins over deregistration in race condition
  (better to route to instance than miss it)
- Heartbeat timestamp is monotonic (clock skew handled)
```

## Tools & Frameworks (with trade-offs)

```
REGISTRY:        Consul (AP-ish, health checks, DNS) | etcd (CP, Raft) |
                 ZooKeeper (CP, legacy) | Eureka (AP, Netflix).
SERVICE MESH:    Istio/Envoy or Linkerd — sidecar handles discovery+mTLS+LB
                 transparently; cost = extra hop + operational complexity.
DNS-BASED:       CoreDNS / k8s Services — simple, but TTL limits freshness.
LOAD BALANCING:  client-side (Ribbon-style) vs. server-side (Envoy/L7 LB).
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Registry | Consul/Eureka (AP) | etcd/ZK (CP) | AP for discovery (stale OK + retry); CP only if strong view needed |
| Discovery | Client-side | Service mesh | Client lib for low latency; mesh for polyglot + mTLS + central policy |
| Routing | DNS | Registry API/mesh | DNS for simplicity; API/mesh for fast, fine-grained updates |
| Health | Heartbeat (passive) | Active probe | Hybrid — heartbeat + active probe for accuracy |

## Scaling

- **Lookups:** Client-side caching (zero network for common case)
- **Health checks:** Distributed across discovery nodes
- **Registry size:** In-memory (200MB for 100K instances fits easily)
- **Watch connections:** Fan-out from any node, load balanced

## Reliability

- **Discovery cluster failure:** Clients use cached registry (graceful degradation)
- **Network partition:** AP design ensures both partitions can serve lookups
- **Split brain:** CRDT-based registry automatically converges when partition heals
- **Thundering herd:** (all instances register simultaneously) Rate limiting + jitter

## Security

- mTLS between services and discovery cluster
- ACLs: who can register/deregister which services
- Audit logging for registration changes
- Network isolation (discovery cluster not publicly accessible)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| AP over CP | Stale data risk for availability guarantee |
| Client-side cache | Memory/complexity for latency/resilience |
| Heartbeat + active checks | Check overhead for detection accuracy |
| CRDT replication | Complexity for conflict-free convergence |
| In-memory registry | Durability (WAL backup) for speed |

## Follow-up Questions

1. "How would you handle service discovery across multiple data centers?"
2. "How would you implement canary routing through service discovery?"
3. "How would you prevent a misbehaving service from registering?"
4. "How would you handle service discovery during a rolling deploy?"
5. "How would you implement service discovery for serverless functions?"
