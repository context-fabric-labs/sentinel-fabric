# PART 5D — SYSTEM DESIGN: Questions 16-20 (Distributed Systems Favorites)

> **Why this file exists:** These five designs round out the distributed-systems
> question bank with problems LinkedIn (and peer companies) frequently ask in the
> infrastructure track. Two of the commonly-requested examples — **Distributed
> Rate Limiter** and **Distributed Cache** — are already covered:
> - Distributed Rate Limiter → [05c #13](05c-SYSTEM-DESIGN-11-TO-15.md)
> - Distributed Cache → [05a #5](05a-SYSTEM-DESIGN-1-TO-5.md)
>
> This file adds: **Job Scheduler**, **Log Ingestion Pipeline**, **Consistent
> Hashing Router/Load Balancer**, plus two LinkedIn-relevant bonuses
> (**Distributed Unique ID Generator** and **Change Data Capture / Databus**).

---

# 16. Design Distributed Job Scheduler

## Requirements

### Functional Requirements
- Schedule one-time jobs (run at time T) and recurring jobs (cron expressions)
- Support delayed jobs (run after N seconds/minutes)
- At-least-once execution with idempotency support (exactly-once effect)
- Job dependencies / DAG execution (job B runs after job A succeeds)
- Retry with backoff on failure; dead-letter for permanent failures
- Job prioritization and per-tenant fair-share
- Visibility: status, history, logs, and manual retry/cancel

### Non-Functional Requirements
- **Scale:** 100M+ scheduled jobs, 1M+ executions/minute at peak
- **Timing accuracy:** Jobs fire within ±1 second of scheduled time (p99)
- **Availability:** 99.99% — scheduler outage must not lose or double-fire jobs
- **Durability:** No job lost even if a scheduler node crashes
- **Isolation:** A flood of jobs from one tenant can't starve others

## Capacity Estimation

```
Scheduled jobs (active): 100M
Executions: 1M/min average = ~17K/sec, peak ~50K/sec
Job metadata: ~1KB per job → 100M × 1KB = 100GB
Execution history: 1M/min × 1KB × 30 days = ~43TB (downsample/TTL old runs)
Worker fleet: 50K concurrent executions → ~5K workers (10 jobs each)
Due-job scan rate: every 1s, scan jobs due in next window
```

## APIs

```
POST /v1/jobs
Body: {
  job_type, payload, schedule: {type: "once"|"cron"|"delay",
  run_at?, cron_expr?, delay_ms?}, priority, max_retries,
  idempotency_key, depends_on: [job_ids], timeout_ms
}
Response: {job_id, status: "scheduled", next_run_at}

GET    /v1/jobs/{job_id}            → status, history, next_run
DELETE /v1/jobs/{job_id}            → cancel
POST   /v1/jobs/{job_id}/trigger    → run now (manual)
GET    /v1/jobs?tenant={id}&status={status}&cursor={c}
```

## Data Model

```
Jobs (source of truth — partitioned by job_id):
├── job_id (PK)
├── tenant_id
├── job_type, payload
├── schedule_type (once | cron | delay)
├── cron_expr / run_at / delay_ms
├── next_run_at        (indexed — the key scheduling field)
├── priority
├── status (scheduled, queued, running, succeeded, failed, cancelled)
├── max_retries, retry_count, backoff_policy
├── idempotency_key
├── depends_on[]       (DAG edges)
├── timeout_ms
└── created_at, updated_at, version (for optimistic locking)

Time Index (for efficient due-job lookup):
├── time_bucket (e.g., minute granularity, partition key)
├── run_at (sort key)
└── job_id

Execution History (append-only, TTL'd):
├── execution_id, job_id, worker_id
├── started_at, finished_at
├── status, attempt_number
├── error_message, output_ref
```

## High-Level Design

```
┌──────────┐   ┌──────────────┐   ┌───────────────────────────────┐
│  Client  │──▶│   API /      │──▶│   Job Store (Espresso/        │
│          │   │   Admission   │   │   DynamoDB) + Time Index      │
└──────────┘   └──────────────┘   └──────────────┬────────────────┘
                                                  │
                         ┌────────────────────────▼───────────────┐
                         │   Scheduler (sharded, leader per shard) │
                         │   - Scans "due soon" jobs every 1s      │
                         │   - Pushes due jobs to Dispatch Queue   │
                         └────────────────────────┬───────────────┘
                                                  │
                                        ┌─────────▼─────────┐
                                        │  Dispatch Queue   │
                                        │  (Kafka, by       │
                                        │   priority+tenant)│
                                        └─────────┬─────────┘
                                                  │
                  ┌──────────────┬────────────────┼────────────────┐
            ┌─────▼─────┐  ┌─────▼─────┐    ┌─────▼─────┐    ┌──────▼────┐
            │ Worker 1  │  │ Worker 2  │ …  │ Worker N  │    │ DAG       │
            │(lease+run)│  │           │    │           │    │ Coordinator│
            └───────────┘  └───────────┘    └───────────┘    └───────────┘
```

## Deep Dive

### Timing Wheel vs. Time-Bucketed Scan

```
PROBLEM: How do you efficiently find "all jobs due now" out of 100M jobs?

OPTION A — DATABASE TIME INDEX (recommended for durability at scale):
- Index jobs by next_run_at, bucketed by minute
- Every second, scheduler queries: WHERE next_run_at <= now + lookahead
- Lookahead window (e.g., 5s) absorbs scan latency and clock skew
- Claims jobs atomically (see leasing below), pushes to dispatch queue

OPTION B — HIERARCHICAL TIMING WHEEL (in-memory, low-latency):
- Ring buffer of buckets, each bucket = a time slot
- Multiple wheels (seconds → minutes → hours) like a clock
- O(1) insert and tick; great for short-horizon, high-precision timers
- Used by Kafka (purgatory), Netty. Downside: must be backed by durable
  store + rebuilt on restart for crash safety.

HYBRID (what large schedulers actually do):
- Durable time index is source of truth
- Load the next few minutes of jobs into an in-memory timing wheel per shard
- Fire from the wheel for precision; persist state transitions to the store
```

### Exactly-Once Firing via Leasing + Fencing

```
GOAL: A due job is dispatched exactly once even with multiple scheduler nodes.

LEASE-BASED CLAIM:
1. Scheduler shard reads due jobs (status=scheduled, next_run_at<=now)
2. Atomic conditional update: status scheduled→queued, set lease_owner,
   lease_expiry, increment version (optimistic concurrency / CAS)
3. Only the winner of the CAS dispatches the job
4. If worker dies mid-run, lease expires → job re-eligible (at-least-once)

FENCING: each dispatch carries a monotonic execution_id / epoch. Downstream
side-effects keyed by idempotency_key reject stale/duplicate attempts, turning
at-least-once delivery into exactly-once *effect*.

WHY NOT just "delete row on claim"? Because a crash after claim but before
execution would lose the job. Status transitions + lease expiry give recovery.
```

### Recurring Jobs & Clock Correctness

```
CRON HANDLING:
- On successful fire, compute next_run_at from cron_expr and re-insert
- Store timezone with the schedule (DST correctness)
- Missed-run policy: if scheduler was down, choose:
    * "fire once on recovery" (catch-up, dedup'd), or
    * "skip to next" (for jobs where stale runs are useless)

CLOCK SKEW: never trust a single node's wall clock. Use NTP-synced time +
a lookahead window; sequence ordering uses the store's authoritative time.
```

### DAG / Dependency Execution

```
- depends_on[] forms edges; job becomes eligible only when all parents succeed
- DAG Coordinator listens to completion events, decrements parent-count
- When count hits 0 → enqueue child
- Cycle detection at submit time (reject cyclic DAGs)
- Failure propagation policy: fail-fast (cancel descendants) vs. continue
```

## Tools & Frameworks (with trade-offs)

```
WORKFLOW/DAG:   Apache Airflow (batch ETL, Python DAGs) | Temporal (durable
                workflows, code-as-workflow, great failure handling) | Cadence |
                Argo Workflows (k8s-native) | Dagster/Prefect (data pipelines).
LEADER/LEASE:   ZooKeeper / etcd (shard leader election, leases).
QUEUE:          Kafka (durable dispatch, partition by priority+tenant) |
                SQS / RabbitMQ (simpler, built-in delay & DLQ).
TIMERS:         Hierarchical timing wheel (in-mem) backed by DB time-index.
STORE:          DynamoDB/Cassandra (job rows + time index) | Redis (delayed sets).
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Engine | Temporal (durable workflow) | Airflow (DAG scheduler) | Temporal for stateful/retry-heavy; Airflow for batch ETL |
| Dispatch | Kafka | SQS/RabbitMQ | Kafka for scale+replay; SQS for simple delay+DLQ |
| Due-job lookup | DB time-index | Timing wheel | DB for durability/scale; wheel for sub-ms precision |
| Delivery | At-least-once + idempotent | Exactly-once | At-least-once + idempotency key (simpler, safe) |
| Leader | etcd/ZK lease | Raft per shard | Lease for simplicity; Raft if state must replicate |

## Scaling
- **Shard the scheduler** by hash(job_id); one leader per shard (Raft/ZK lease)
- **Partition dispatch queue** by priority + tenant to prevent head-of-line blocking
- **Workers are stateless** and horizontally scalable; pull from queue
- **Hot tenant isolation:** per-tenant token buckets + weighted fair queueing

## Reliability
- **Scheduler node crash:** another shard leader elected; leased jobs recovered on expiry
- **Worker crash:** lease expiry re-queues the job (idempotency prevents double effect)
- **Store outage:** dispatch queue (Kafka) buffers; no job lost, timing degrades
- **Poison job:** retry with exponential backoff → dead-letter after max_retries

## Security
- Per-tenant authz on job submission and payload size limits
- Payloads with secrets reference a secrets manager, never inline
- Audit log of trigger/cancel/retry actions
- Sandbox/Resource-limit worker execution (cgroups, timeouts)

## Tradeoffs
| Decision | Tradeoff |
|----------|----------|
| DB time index vs. timing wheel | Durability/scale vs. sub-ms precision |
| Lease + status transitions | Extra writes vs. crash-safe exactly-once firing |
| At-least-once + idempotency | Client must be idempotent vs. simpler delivery |
| Per-tenant queues | More partitions vs. fairness/isolation |
| Catch-up vs. skip missed runs | Correctness semantics differ per job type |

## Follow-up Questions
1. "How do you guarantee a cron job isn't fired twice if two scheduler nodes both think they're leader?"
2. "How would you support 1M jobs all scheduled for exactly midnight (thundering herd)?"
3. "How do you handle a job whose execution takes longer than its lease?"
4. "How would you add priorities without starving low-priority jobs?"
5. "How do you migrate the scheduler to a new shard count with zero missed fires?"

---

# 17. Design Log Ingestion Pipeline

## Requirements

### Functional Requirements
- Collect logs from 100K+ hosts/containers (app, access, audit logs)
- Parse, enrich (host, service, region, trace_id), and structure logs
- Reliable delivery with buffering during downstream outages
- Full-text and structured search over recent logs
- Tail/live-stream a service's logs; alert on patterns
- Tiered retention: hot (searchable) → warm → cold (archive)

### Non-Functional Requirements
- **Throughput:** 10M log lines/sec, ~10GB/sec ingest
- **Ingest-to-searchable latency:** < 30 seconds p99
- **Durability:** No log loss for audit/compliance logs (at-least-once)
- **Availability:** 99.9% ingest; back-pressure instead of data loss
- **Cost:** Logs are huge — compression + tiering are first-class concerns

## Capacity Estimation

```
Volume: 10M lines/sec × 500 bytes avg = 5GB/sec raw, ~10GB/sec with metadata
Daily: 10GB/s × 86400 ≈ 850TB/day raw → ~85TB/day compressed (10:1)
Hot tier (7 days searchable): ~600TB
Warm (30 days): object store, queryable on demand
Cold (1–7 years): compressed archive (S3 Glacier), compliance only
Index overhead: ~30–50% of stored log size for inverted index
```

## APIs

```
# Ingest (agent → collector), usually gRPC/HTTP batch
POST /v1/logs/ingest
Body: {batch: [{ts, host, service, level, message, fields{...}}], compression}

# Query
GET /v1/logs/search?q={lucene}&service={s}&from={t1}&to={t2}&limit=100
GET /v1/logs/tail?service={s}&filter={expr}     (SSE/WebSocket live stream)
```

## Data Model

```
Structured Log Event:
├── timestamp (event time + ingest time)
├── host, container_id, service, region, env
├── level (DEBUG/INFO/WARN/ERROR)
├── message (raw)
├── trace_id, span_id        (for correlation with traces)
├── fields {} (parsed key-values, e.g., status=500 latency_ms=812)
└── source_offset (for dedup / replay)

Index (per time-partitioned shard):
├── inverted index on tokenized message + keyword fields
├── doc store: event_id → full event
└── time-partitioned blocks (e.g., 1-hour), immutable + compacted
```

## High-Level Design

```
┌────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐
│  Agents    │─▶│  Collectors  │─▶│   Kafka      │─▶│  Processing      │
│ (Fluent    │  │ (LB, auth,   │  │  (durable    │  │  (parse, enrich, │
│  Bit/Vector│  │  batch, ack) │  │   buffer)    │  │  route — Flink)  │
│  per host) │  └──────────────┘  └──────────────┘  └────────┬─────────┘
└────────────┘                                               │
                       ┌─────────────────────────────────────┼───────────────┐
                       ▼                       ▼              ▼               ▼
                ┌────────────┐         ┌────────────┐  ┌────────────┐  ┌────────────┐
                │ Hot Index  │         │ Object     │  │ Metrics    │  │ Alerting   │
                │(ES/Loki/   │         │ Store      │  │ extraction │  │ (pattern   │
                │ OpenSearch)│         │ (warm/cold)│  │ (counts)   │  │  match)    │
                └────────────┘         └────────────┘  └────────────┘  └────────────┘
```

## Deep Dive

### Agent → Collector: Reliability & Back-Pressure

```
AGENT (sidecar/daemonset, e.g., Fluent Bit / Vector / OTel Collector):
- Tails files / reads stdout, batches lines, compresses
- LOCAL DISK BUFFER (spool): survives collector outages and bursts
- At-least-once: tracks file offset; only advances after collector ACK
- Back-pressure: if buffer fills, slow the reader (never silently drop —
  except for explicitly best-effort, sampled DEBUG logs)

WHY KAFKA IN THE MIDDLE:
- Shock absorber: decouples bursty producers from slower indexers
- Durable replay: re-index after a mapping change or index loss
- Multiple consumers: index, archive, metrics, alerting read independently
- Partition by service (or host) for ordering + parallelism
```

### Parse / Enrich / Route (Stream Processing)

```
STAGES (Flink / Kafka Streams):
1. Parse: regex/grok or structured JSON → typed fields
2. Enrich: attach service metadata, geo from IP, k8s labels, trace linkage
3. Sample/Filter: drop or sample high-volume low-value logs (cost control)
4. Route: by retention class + destination
   - audit/security  → durable index + 7yr cold archive (no sampling)
   - app ERROR/WARN  → hot index, 7–14 days
   - app DEBUG/INFO  → sampled, short retention
5. Derive metrics: e.g., count of status=500 per service → metrics platform
```

### Indexing & Tiered Storage

```
HOT TIER (searchable, expensive):
- Time-partitioned, immutable segments; rolling indices per hour/day
- Index only fields people query; keep raw message in a doc store
- Force-merge + read-only old segments; then roll off after N days

WARM/COLD:
- Move sealed blocks to object store (S3) in columnar/compressed form
- Queryable on demand (slower) via serverless query (e.g., Athena/Presto)
- COLD: Glacier-class, compliance retention, restore-before-query

COMPRESSION: gzip/zstd on raw; columnar (Parquet/ORC) for warm analytics.
Dedup: source_offset + content hash guards against agent re-sends on retry.
```

### Live Tail & Alerting

```
LIVE TAIL: a dedicated low-latency consumer filters the Kafka stream for the
requested service/filter and pushes over SSE/WebSocket — does NOT hit the index.

ALERTING: pattern-match stage emits events ("ERROR rate > X", "stack trace
signature Y appeared") → notification platform. Also extract metrics so you
alert on aggregates, not raw lines.
```

## Tools & Frameworks (with trade-offs)

```
AGENT/COLLECTOR: Fluent Bit (light, C) | Vector (Rust, fast, transforms) |
                 Fluentd (plugin-rich, heavier) | OpenTelemetry Collector (neutral).
BUFFER:          Kafka (durable shock absorber, replay, multi-consumer).
PROCESSING:      Flink / Kafka Streams / Logstash (parse, enrich, route).
INDEX/SEARCH:    Elasticsearch/OpenSearch (full-text, heavy) | Grafana Loki
                 (label-index only, cheaper, less full-text) | ClickHouse
                 (columnar, fast aggregations, cost-efficient at scale).
WAREHOUSE/COLD:  S3 + Parquet, queried via Athena/Presto/Trino.
VISUALIZE:       Kibana / Grafana.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Agent | Fluent Bit | Vector | Fluent Bit for footprint; Vector for rich transforms |
| Backend | Elasticsearch | Loki / ClickHouse | ES for full-text; Loki/CH for cost at huge volume |
| Buffer | Kafka | Direct-to-index | Kafka for durability/replay; direct only at small scale |
| Retention | Hot index | Tiered to S3 | Tier aggressively — logs are the biggest cost driver |
| Volume control | Index everything | Sample low-value | Sample DEBUG/INFO; never sample audit |

## Scaling
- **Ingest:** stateless collectors behind LB; scale with traffic
- **Buffer:** Kafka partitions = parallelism unit; add partitions/brokers
- **Index:** shard by time + service; hot/warm tiering caps cost
- **Query fan-out:** scatter-gather across shards, time-range pruning

## Reliability
- **Downstream (index) outage:** Kafka retains; consumers catch up, no loss
- **Collector outage:** agent disk spool buffers, replays on recovery
- **Index corruption/mapping change:** re-consume from Kafka offset
- **Poison/oversized lines:** route to a quarantine topic, never block pipeline

## Security
- TLS agent→collector; authn per host/service identity (mTLS)
- PII scrubbing/redaction in the enrich stage (emails, tokens)
- Access control on log search (a team sees only its services)
- Immutable, tamper-evident audit log stream (WORM storage)

## Tradeoffs
| Decision | Tradeoff |
|----------|----------|
| Kafka buffer | Extra hop/cost vs. durability + replay + decoupling |
| Index only queried fields | Less flexible ad-hoc vs. far lower cost |
| Sampling DEBUG/INFO | Lost detail vs. 10x cost reduction |
| Hot/warm/cold tiering | Query complexity vs. cost control |
| At-least-once + dedup | Duplicate handling vs. zero loss for audit |

## Follow-up Questions
1. "A service starts logging 100x normal volume — how does the pipeline avoid taking down the index?"
2. "How do you guarantee zero loss for audit logs but allow sampling for debug logs?"
3. "How do you correlate a log line with a distributed trace?"
4. "How would you re-index 30 days of logs after changing the parsing schema?"
5. "How do you keep ingest-to-searchable under 30s during a 5x traffic spike?"

---

# 18. Design Consistent-Hashing Router (Sharding Load Balancer)

## Requirements

### Functional Requirements
- Route each request/key to the correct backend shard deterministically
- Add/remove backends with minimal key movement (no full reshuffle)
- Even load distribution across heterogeneous backends
- Replication-aware: produce the ordered replica set for a key (primary + N-1)
- Support sticky routing (session/key affinity) and locality awareness
- React to backend health (remove dead nodes, add recovered nodes)

### Non-Functional Requirements
- **Routing latency:** < 100µs (in-process library) — it's on every request path
- **Availability:** Routing must keep working during membership changes
- **Even distribution:** No backend > ~1.25x the mean load (hotspot bound)
- **Scale:** 1000s of backends, millions of routing decisions/sec per client

## APIs

```
# In-process router library (preferred — no extra hop)
router.route(key) -> node_id
router.replicas(key, n) -> [primary, replica1, ... replica_{n-1}]
router.addNode(node, weight)
router.removeNode(node)
router.updateMembership(node_list)   # from membership/health service
```

## High-Level Design

```
            ┌─────────────────────────────────────────────┐
            │  Membership / Health Service (gossip or      │
            │  ZK/etcd watch) — authoritative node list     │
            └───────────────────────┬─────────────────────┘
                                    │ push/watch updates
        ┌───────────────────────────▼───────────────────────────┐
        │   Client (with embedded Consistent-Hash Router lib)    │
        │   ring: sorted vnode hashes → physical node            │
        └───────────────┬───────────────────────────────────────┘
                        │  route(key)
        ┌───────────────▼───────┐  ┌──────────────┐  ┌──────────────┐
        │   Backend Shard A     │  │  Shard B     │  │  Shard C     │
        │  (vnodes: a1..a200)   │  │ (b1..b200)   │  │ (c1..c200)   │
        └───────────────────────┘  └──────────────┘  └──────────────┘
```

## Deep Dive

### The Ring with Virtual Nodes

```
WHY NOT hash(key) % N?
- Changing N (add/remove node) remaps ~ (N-1)/N of all keys → cache stampede,
  data reshuffle. Unacceptable at scale.

CONSISTENT HASHING:
- Map nodes AND keys onto a ring [0, 2^32)
- key → first node clockwise from hash(key)
- Add/remove a node → only keys between it and its predecessor move (~1/N)

VIRTUAL NODES (VNODES) — essential:
- With few physical nodes, plain CH is very uneven (variance is high)
- Give each physical node V vnodes (100–200): hash("nodeX#1"), ("nodeX#2")...
- Spreads each node across the ring → near-uniform load
- WEIGHTING: bigger machines get more vnodes (heterogeneous fleets)

LOOKUP: binary search on sorted array of vnode hashes → O(log(V·N)).
```

### Replication-Aware Routing

```
REPLICA SET for a key:
1. Find primary (first vnode clockwise)
2. Continue clockwise, skipping vnodes that map to already-chosen PHYSICAL
   nodes, until N distinct physical nodes are collected
3. (Optional) skip to ensure replicas span fault domains / racks / AZs

This yields a deterministic, ordered preferred list — exactly Dynamo/Cassandra
style. Reads/writes use quorum across this set (see 06 §8 Quorum).
```

### Bounded-Load & Hotspots

```
PROBLEM: Even with vnodes, a single HOT KEY (one celebrity) overloads its node.

MITIGATIONS:
- Consistent Hashing with BOUNDED LOADS: cap each node at (1+ε)·average;
  if the target node is "full", probe next clockwise (Google's algorithm).
  Keeps minimal movement while enforcing an even-load ceiling.
- Hot-key replication: replicate the hot key to K adjacent nodes; reads spread.
- Key splitting: shard hot key into key#0..key#7 across nodes; aggregate on read.
- Client-side micro-cache for ultra-hot keys (short TTL).
```

### Membership Changes Without Disruption

```
- Membership service (gossip like SWIM, or ZK/etcd watch) is the source of truth
- Router subscribes; on change it rebuilds the ring (cheap, in-memory)
- GRACE/HANDOFF: when a node is added, it warms up (copy/replicate its new key
  range) before receiving full traffic — avoids cold-cache miss storms
- During the window, route to old owner OR fan-out read to old+new (read-repair)
- Health: failed node removed from ring; its range temporarily served by the
  next clockwise node (and its replicas) — availability preserved
```

### Client-side vs. Server-side Routing
```
CLIENT-SIDE (smart client / embedded library):  ← recommended for data stores
  + No extra network hop, lowest latency, no router bottleneck
  − Every client needs the membership view + library (multi-language)

SERVER-SIDE (a proxy/router tier, e.g., Twemproxy, Envoy ring-hash):
  + Thin clients, central control, polyglot-friendly
  − Extra hop + the router tier must itself be scaled & made HA

LinkedIn-style data infra (Espresso/Venice clients) favors smart clients with
a routing layer fed by a membership service.
```

## Tools & Frameworks (with trade-offs)

```
ALGORITHMS:    Ring + vnodes (Dynamo/Cassandra) | Rendezvous (HRW) hashing
               (no ring, simple, good for small N) | Maglev (Google, fast
               lookup table, minimal disruption — used in L4 LBs) | Jump hash
               (tiny, fast, but only append/remove last node).
MEMBERSHIP:    gossip/SWIM (Serf/HashiCorp, AP, scalable) | etcd/ZK watch (CP view).
PROXY/LB:      Envoy (ring-hash, maglev LB) | Twemproxy/mcrouter (cache proxies).
FAILURE DETECT:phi-accrual detector; hysteresis to avoid flapping.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Algorithm | Consistent hash + vnodes | Rendezvous (HRW) | Ring for large dynamic fleets; HRW for small/simple |
| LB hashing | Maglev | Ring-hash | Maglev for L4 LB (fast table); ring for data stores |
| Routing | Smart client | Proxy (Envoy) | Client for latency; proxy for polyglot/central control |
| Membership | Gossip (AP) | etcd/ZK (CP) | Gossip for scale; CP when consistent view required |
| Hotspot | Bounded loads | Key split/replicate | Bounded loads for even fill; split for single hot key |

## Scaling
- Router is O(log n) in-memory — trivially fast; cost is membership propagation
- Scale backends by adding nodes/vnodes; only ~1/N keys move
- For very large fleets, partition the keyspace into "partitions" first, then
  assign partitions to nodes (two-level) for cheaper rebalancing (Kafka/Venice style)

## Reliability
- **Stale membership:** routing to a dead node → client retries next replica
- **Split views:** different clients briefly disagree → idempotent ops + read-repair reconcile
- **Node flapping:** dampen with failure detector hysteresis (phi-accrual)

## Security
- Authenticated membership updates (prevent malicious ring poisoning)
- mTLS client↔backend; routing decisions don't bypass authz at the backend

## Tradeoffs
| Decision | Tradeoff |
|----------|----------|
| Virtual nodes | Memory/lookup cost vs. even distribution |
| Bounded loads | Slightly more movement vs. hotspot protection |
| Client-side routing | No hop/latency vs. fat polyglot clients |
| Two-level (partitions) | Indirection vs. cheaper, controllable rebalancing |
| Gossip vs. ZK membership | AP/scalable vs. strongly-consistent view |

## Follow-up Questions
1. "Why virtual nodes? How many per physical node and why?"
2. "How do you keep load even when one key is 1000x hotter than the rest?"
3. "How do you avoid a cache-miss storm when adding a node to a 100-node cache?"
4. "Client-side vs. proxy-based routing — when would you pick each?"
5. "How do you make replicas land in different availability zones?"

---

# 19. Design Distributed Unique ID Generator (Bonus — very common at LinkedIn)

## Requirements

### Functional Requirements
- Generate globally unique 64-bit IDs
- Roughly time-ordered (k-sortable) for index locality and pagination
- High throughput, no central bottleneck, no coordination per ID
- Survive clock issues, node restarts, and data-center splits

### Non-Functional Requirements
- **Throughput:** 1M+ IDs/sec per node, 10M+ cluster-wide
- **Latency:** < 1ms, ideally in-process
- **Uniqueness:** Absolute — zero collisions
- **Ordering:** Monotonic-ish by time (not strict global order)

## Deep Dive

### Snowflake-style 64-bit Layout (recommended)

```
 0 │ 41 bits timestamp (ms since custom epoch) │ 10 bits node │ 12 bits seq
───┴───────────────────────────────────────────┴─────────────┴───────────
 1   ~69 years of ms                            1024 nodes     4096 IDs/ms/node

GENERATION (per node, lock-free):
  if now == last_ms:  seq = (seq + 1) & 0xFFF
                      if seq == 0: wait for next ms   # exhausted this ms
  else:               seq = 0
  last_ms = now
  id = (now - epoch) << 22 | node_id << 12 | seq

PROPERTIES:
- Time-ordered prefix → great for B-tree/index locality, range scans
- node_id makes IDs disjoint across machines → no coordination per ID
- 4096 IDs/ms/node = ~4M IDs/sec/node
```

### Clock Problems (the hard part)

```
CLOCK SKEW / NTP: nodes must be NTP-synced; node_id prevents cross-node
collision even if clocks differ.

CLOCK GOING BACKWARDS (NTP step, VM pause):
- NEVER generate with a timestamp < last_ms (would risk duplicates)
- Strategy: refuse + wait until clock catches up, OR
  use a monotonic clock and store last_ms; if now < last_ms by a small delta,
  spin-wait; if large, fail fast and alert (don't silently dup)

NODE_ID ASSIGNMENT:
- From config/ZK/etcd lease (ephemeral) so a restarted node can't reuse a
  live node's id; lease prevents two nodes sharing an id during a split
```

### Alternatives

```
- DB ticket server (auto-increment): simple, but a SPOF/bottleneck; mitigate
  with two servers (odd/even) or segment allocation (hand out ranges of 1000).
- UUIDv4 (random 128-bit): zero coordination, but NOT sortable → poor index
  locality; larger keys. UUIDv7 fixes ordering (time-prefixed) — modern choice.
- Range/segment allocation: each node leases a block of IDs from a central
  store, generates locally, leases more when low (used by many "ID services").
```

## Tools & Frameworks (with trade-offs)

```
IMPLEMENTATIONS: Twitter Snowflake (original) | Sonyflake | Instagram (DB shard
                 + sequence in PL/pgSQL) | Flickr ticket server | Boundary flake.
NODE-ID SOURCE:  ZooKeeper/etcd ephemeral lease (safe across restarts/partitions).
LIBRARY:         most languages have a Snowflake lib; UUIDv7 in modern stdlibs.
CLOCK:           NTP/chrony sync + monotonic clock guard against backward jumps.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Scheme | Snowflake (k-sorted) | UUIDv4 | Snowflake for index locality; UUIDv4 for zero-coordination |
| Modern UUID | UUIDv7 | Snowflake | UUIDv7 when you want sortable + no node-id management |
| Coordination | DB ticket server | Local + node-id | Ticket server tiny scale; node-id approach for high QPS |
| node-id assign | etcd/ZK lease | Static config | Lease for safety across restarts; static for fixed fleets |

## Tradeoffs
| Decision | Tradeoff |
|----------|----------|
| Snowflake | k-sorted + no coordination vs. clock-dependence & node_id mgmt |
| DB ticket server | Dead simple vs. central bottleneck/SPOF |
| UUIDv4 | No coordination vs. unsortable, larger keys |
| Segment allocation | Local + fast vs. ID gaps on restart |

## Follow-up Questions
1. "What happens when a node's clock moves backward? How do you avoid duplicates?"
2. "How many IDs/sec can one Snowflake node produce, and what limits it?"
3. "Why is k-sortability valuable for the database?"
4. "How do you assign node_id safely across restarts and partitions?"
5. "When would UUIDv7 beat Snowflake, and vice versa?"

---

# 20. Design Change Data Capture / Databus (Bonus — LinkedIn-built)

> LinkedIn invented **Databus** and later **Brooklin** for streaming database
> changes to many consumers. CDC is a frequent infra-track topic; it ties
> together Kafka, replication, and event-driven architecture.

## Requirements

### Functional Requirements
- Capture every committed row change (insert/update/delete) from source DBs
- Deliver changes in commit order per source partition to many consumers
- Allow consumers to bootstrap (read full snapshot) then stream incrementally
- Exactly-once / idempotent consumption; replay from an offset
- Schema evolution support; decouple producers from consumers

### Non-Functional Requirements
- **Latency:** source commit → consumer < 1–2s p99
- **Throughput:** 1M+ changes/sec across sources
- **Durability:** No change lost (drives search index, cache, derived stores)
- **Ordering:** Per-key/per-partition commit order preserved
- **Isolation:** A slow consumer must not affect the source DB or other consumers

## High-Level Design

```
┌──────────────┐   ┌──────────────────┐   ┌──────────────┐   ┌──────────────┐
│ Source DB    │──▶│  CDC Connector   │──▶│   Kafka      │──▶│ Consumers:   │
│ (binlog/WAL/ │   │ (read log, not   │   │ (per-table   │   │ search index,│
│  Espresso)   │   │  the table)      │   │  topic)      │   │ cache, derived│
└──────────────┘   └──────────────────┘   └──────────────┘   │ stores, ETL  │
                            │                                 └──────────────┘
                   ┌────────▼─────────┐
                   │ Bootstrap/Snapshot│  (full table copy for new consumers,
                   │ Service           │   then switch to streaming tail)
                   └───────────────────┘
```

## Deep Dive

### Log-Based CDC (the right way)

```
READ THE DB'S REPLICATION LOG, not the table:
- MySQL binlog, Postgres WAL, MongoDB oplog, or Espresso's transaction log
- The connector acts like a replica: it gets every committed change in order
- WHY: no impact on source query load, captures deletes, exact commit order,
  no missed updates (unlike query-based polling of "updated_at")

EACH CHANGE EVENT:
{ source, table, op: I|U|D, key, before{}, after{}, txn_id, scn/lsn, ts }
- scn/lsn (log sequence number) = the offset → ordering + replay anchor
```

### Bootstrap + Stream (consistency without gaps)

```
NEW CONSUMER needs full state + future changes WITHOUT missing any:
1. Record current log position L0
2. Take a consistent snapshot of the table (or read from a snapshot service)
3. Stream changes starting at L0
4. Dedup overlap by key + lsn (snapshot row vs. early change) → no gap, no dup

LinkedIn's Databus had exactly this "bootstrap" component so a consumer that
fell far behind (or a brand-new one) could catch up without hammering the
primary, then transition to the low-latency online stream.
```

### Ordering, Delivery & Idempotency

```
ORDERING: partition Kafka topic by primary key → per-key commit order preserved.
DELIVERY: at-least-once from Kafka; consumers idempotent (upsert by key + lsn,
ignore events with lsn <= last_applied). Gives exactly-once *effect*.
REPLAY: consumers store last lsn; reset to any offset to rebuild derived state.
TRANSACTIONS: group changes by txn_id so consumers can apply atomically if needed.
```

### Decoupling & Fan-out (why CDC instead of dual writes)

```
ANTI-PATTERN: app writes DB then also writes Kafka ("dual write") → they can
diverge on partial failure.
CDC FIX: app writes ONLY the DB; the change log is the single source of truth;
CDC publishes derived events. This is the Outbox/CDC pattern — the database
commit and the event are the same fact, so they can't diverge.

FAN-OUT: one change stream feeds many independent consumers (search index,
cache invalidation, analytics, notifications) — each at its own pace, isolated
from the source DB by Kafka.
```

## Tools & Frameworks (with trade-offs)

```
CDC CONNECTOR:  Debezium (OSS, MySQL/Postgres/Mongo → Kafka) | LinkedIn Brooklin
                (pluggable multi-source streaming) | Databus (LinkedIn original) |
                Maxwell (MySQL) | AWS DMS (managed).
TRANSPORT:      Kafka (durable, replay, per-key ordering). Schema: Schema Registry.
SNAPSHOT:       Debezium incremental snapshots (signal-based) for gap-free bootstrap.
SINKS:          Kafka Connect sink connectors (ES, JDBC, S3) | Flink for transforms.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Connector | Debezium | Brooklin/Databus | Debezium OSS standard; Brooklin for multi-source at scale |
| Capture | Log-based (binlog/WAL) | Query polling | Log-based always — captures deletes, no source load |
| Delivery | At-least-once + idempotent | Exactly-once | At-least-once + upsert-by-lsn (simpler, safe) |
| Bootstrap | Incremental snapshot | Stop-the-world dump | Incremental — no lock, gap-free catch-up |
| Ordering | Partition by PK | Global | Per-key ordering (global doesn't scale) |

## Scaling
- Connector per source partition/shard; Kafka partitions = parallelism
- Bootstrap service offloads big backfills from the primary
- Consumers scale independently via consumer groups

## Reliability
- **Consumer slow/down:** Kafka retains; it catches up — source DB unaffected
- **Connector crash:** resume from last committed lsn (no missed/duplicated commits beyond idempotent overlap)
- **Schema change:** schema registry + versioned events; consumers handle evolution
- **Source failover:** track lsn continuity across primary failover (gtid/lsn mapping)

## Security
- Read-only replication credentials for the connector
- PII filtering/redaction before publishing to broad topics
- Per-topic ACLs (who can subscribe to which table's changes)

## Tradeoffs
| Decision | Tradeoff |
|----------|----------|
| Log-based CDC | Connector complexity vs. zero source load + exact capture |
| CDC vs. dual-write | One source of truth vs. app simplicity (but unsafe) |
| At-least-once + idempotent | Consumer dedup logic vs. no data loss |
| Bootstrap service | Extra component vs. gap-free catch-up at scale |
| Partition-by-key ordering | Per-key order only vs. global ordering |

## Follow-up Questions
1. "Why read the binlog/WAL instead of polling an `updated_at` column?"
2. "How does a brand-new consumer get full state without missing changes or hammering the primary?"
3. "Why is CDC safer than having the app write to both the DB and Kafka?"
4. "How do you preserve ordering, and what ordering can you actually guarantee?"
5. "How do you handle a source schema change without breaking consumers?"

---

## Is the System-Design Coverage "Enough"?

**Short answer: yes, with this addition it's comprehensive for the LinkedIn infra track.** Mapping your list:

| You asked for | Where it now lives |
|---------------|--------------------|
| Distributed Rate Limiter | [05c #13](05c-SYSTEM-DESIGN-11-TO-15.md) ✅ |
| Distributed Cache | [05a #5](05a-SYSTEM-DESIGN-1-TO-5.md) ✅ |
| Job Scheduler | #16 (this file) ✅ |
| Log Ingestion Pipeline | #17 (this file) ✅ |
| Consistent-Hashing Router | #18 (this file) ✅ |
| *(bonus)* Unique ID Generator | #19 (this file) |
| *(bonus)* CDC / Databus | #20 (this file) |

**A few more that recruiters/candidates report for LinkedIn infra** — consider a light skim if you have time, most reuse primitives you already know:
- **Distributed Counter / View-count system** (sharded counters + CRDT-ish merge) — reuses consistent hashing + Kafka.
- **Top-K / Trending** (Count-Min Sketch + heavy hitters) — reuses metrics/stream concepts (#14, #17).
- **Distributed Tracing** (span collection, sampling) — close cousin of Log Ingestion (#17).
- **Object/Blob Store** (chunking, replication, erasure coding) — if storage-team aligned.
- **Geo-replicated KV / multi-region writes** (conflict resolution, CRDTs) — extends Cache (#5) + Quorum (06 §8).

These are listed so you can decide depth; the 20 designs above plus [06-DISTRIBUTED-SYSTEMS.md](06-DISTRIBUTED-SYSTEMS.md) cover the underlying primitives (CAP/PACELC, Raft/Paxos, quorum, consistent hashing, replication, backpressure) that every one of them is built from.
