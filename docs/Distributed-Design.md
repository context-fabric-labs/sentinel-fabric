# 1. Design Distributed Cache

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

# 2. Design Kafka (Distributed Event Streaming Platform)

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



# 3. Design Rate Limiter

## Requirements

### Functional Requirements
- Rate limit requests per client/API key/user/IP
- Multiple limit types (requests/second, requests/minute, requests/day)
- Multiple algorithms (fixed window, sliding window, token bucket)
- Custom limits per tier (free: 100/min, premium: 10,000/min)
- Rate limit headers in response (remaining, reset time)
- Distributed rate limiting (consistent across multiple instances)

### Non-Functional Requirements
- **Latency:** < 1ms added to each request
- **Availability:** 99.99% (rate limiter down = unprotected system)
- **Accuracy:** ±5% tolerance acceptable (not exact)
- **Scale:** 1M+ rate limit checks/sec
- **Fail-open:** If rate limiter unavailable, allow requests (prefer serving over protecting)

## Capacity Estimation

```
Rate limit checks: 1M/sec
Unique rate limit keys: 10M (users + API keys + IPs)
State per key: ~100 bytes (counter + window info)
Total state: 10M × 100B = 1GB (easily fits in Redis)
Rules: 1000 rate limit rules (per API, per tier, per region)
```

## APIs

```
// Check rate limit (internal, called by API gateway)
POST /v1/ratelimit/check
Body: {
  domain: "api",
  descriptors: [
    {key: "user_id", value: "user_123"},
    {key: "api_path", value: "/v1/search"},
    {key: "tier", value: "premium"}
  ]
}
Response: {
  allowed: true,
  remaining: 9500,
  limit: 10000,
  reset_at: "2024-01-15T10:01:00Z",
  retry_after_ms: null
}

// Configure rate limit rule (admin)
POST /v1/ratelimit/rules
Body: {
  domain: "api",
  descriptors: [{key: "tier", value: "free"}],
  limits: [
    {requests_per_unit: 100, unit: "minute"},
    {requests_per_unit: 1000, unit: "hour"}
  ]
}
```

## High-Level Design

```
┌──────────────┐     ┌──────────────┐     ┌──────────────────────────────┐
│   Client     │────▶│  API Gateway │────▶│   Rate Limit Service          │
│              │◀────│              │◀────│                              │
└──────────────┘     └──────────────┘     └──────────────┬───────────────┘
                            │                            │
                            │              ┌─────────────▼────────────────┐
                            │              │   Rate Limit State Store     │
                            │              │   (Redis Cluster)            │
                            │              └──────────────────────────────┘
                            │
                     ┌──────▼──────┐
                     │  Backend    │
                     │  Service    │
                     └─────────────┘
                     
ALTERNATIVE: Library-based (in-process)
┌───────────────────────────────────────────┐
│  API Gateway / Service                     │
│  ┌─────────────────────────────────────┐  │
│  │  Rate Limit Library (Local + Remote)│  │
│  │  ├── Local counter (approximate)    │  │
│  │  ├── Sync to Redis periodically     │  │
│  │  └── Fallback: local only if Redis  │  │
│  │       down                          │  │
│  └─────────────────────────────────────┘  │
└───────────────────────────────────────────┘
```

## Deep Dive

### Rate Limiting Algorithms

```
ALGORITHM 1: FIXED WINDOW COUNTER
─────────────────────────────────
- Divide time into fixed windows (e.g., 1-minute windows)
- Counter per key per window
- Reset counter at window boundary
- Pro: Simple, low memory
- Con: Burst at window boundaries (up to 2x limit)

Example: Limit 100/min
Window: 10:00-10:01 → counter = 95 (allowed)
Window: 10:01-10:02 → counter resets to 0

Boundary problem:
  10:00:59 → 100 requests (allowed, fills window)
  10:01:00 → 100 requests (allowed, new window)
  = 200 requests in 2 seconds (violates spirit of limit)

ALGORITHM 2: SLIDING WINDOW LOG
─────────────────────────────────
- Store timestamp of each request
- Count requests in last N seconds
- Pro: Exact, no boundary issues
- Con: Memory-intensive (store every timestamp)

ALGORITHM 3: SLIDING WINDOW COUNTER (Recommended)
─────────────────────────────────────────────────
- Combine fixed windows with weighted average
- current_count = prev_window_count × overlap_percentage + current_window_count
- Pro: Good accuracy, low memory (just 2 counters)
- Con: Approximate (but within ±5%)

Example: Limit 100/min, at 10:01:30 (halfway through current window)
  Previous window (10:00-10:01): 80 requests
  Current window (10:01-10:02): 30 requests
  Estimated: 80 × 0.5 + 30 = 70 requests → allowed (under 100)

ALGORITHM 4: TOKEN BUCKET (Best for Bursts)
─────────────────────────────────────────────
- Bucket holds N tokens (capacity)
- Tokens refill at rate R per second
- Each request consumes 1 token
- If bucket empty → reject
- Pro: Allows controlled bursts up to bucket capacity
- Con: Slightly more state (tokens + last_refill_time)

State per key: {tokens: float, last_refill: timestamp}
Check: tokens = min(capacity, tokens + (now - last_refill) × rate)
       if tokens >= 1: tokens -= 1, allow
       else: reject

ALGORITHM 5: LEAKY BUCKET
─────────────────────────────────────────────
- Requests enter a queue (bucket)
- Processed at fixed rate (leak rate)
- If queue full → reject
- Pro: Perfectly smooth output rate
- Con: Adds latency (queuing), more complex
```

### Distributed Rate Limiting

```
CHALLENGE: 10 API gateway instances, each seeing 1/10 of traffic
How to enforce a global limit of 100/min?

APPROACH 1: Centralized (Redis)
- All instances check/increment in Redis
- INCR + EXPIRE for fixed window
- Lua script for atomic sliding window
- Pro: Exact global count
- Con: Redis latency added to every request

APPROACH 2: Local + Sync (Recommended for high QPS)
- Each instance has local counter
- Limit per instance = global_limit / num_instances (approximately)
- Periodically sync with Redis (every 100ms)
- Between syncs: local counting is approximate
- Pro: Near-zero latency for checks
- Con: Slightly inaccurate (±10%)

APPROACH 3: Fixed allocation
- Divide limit equally among instances: 100/min ÷ 10 = 10/min each
- Pro: Simple, no coordination
- Con: Unfair if traffic is uneven across instances

REDIS LUA SCRIPT (Sliding Window Counter):
```lua
-- KEYS[1]: rate limit key
-- ARGV[1]: window size (seconds)
-- ARGV[2]: current timestamp
-- ARGV[3]: limit
local key = KEYS[1]
local window = tonumber(ARGV[1])
local now = tonumber(ARGV[2])
local limit = tonumber(ARGV[3])
local window_start = now - window
-- Remove expired entries
redis.call('ZREMRANGEBYSCORE', key, '-inf', window_start)
-- Count current entries
local count = redis.call('ZCARD', key)
if count < limit then
  redis.call('ZADD', key, now, now .. ':' .. math.random())
  redis.call('EXPIRE', key, window)
  return {1, limit - count - 1}  -- allowed, remaining
else
  return {0, 0}  -- rejected, 0 remaining
end
```

### Rate Limit Response Headers

```
HTTP/1.1 429 Too Many Requests
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1705312860
Retry-After: 30

OR (for allowed requests):
HTTP/1.1 200 OK
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 43
X-RateLimit-Reset: 1705312860
```

## Tools & Frameworks (with trade-offs)

```
COUNTER STORE:   Redis (INCR/EXPIRE, Lua for atomic sliding window) — the standard.
ENVOY/API GW:    Envoy global rate limit service, Kong, NGINX, AWS API Gateway,
                 LinkedIn in-house gateway — enforce at the edge.
LIBRARY:         Bucket4j, resilience4j, Guava RateLimiter (in-process token bucket).
DISTRIBUTED:     Redis Cell (CRDB), or local-count + periodic Redis sync.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Algorithm | Sliding-window counter | Token bucket | Sliding window for accuracy; token bucket for bursts |
| State | Centralized Redis | Local + sync | Redis for accuracy; local for <1ms p99 at high QPS |
| Enforcement point | API gateway/Envoy | In-app library | Gateway for cross-service; library for fine-grained |
| Failure mode | Fail-open | Fail-closed | Fail-open (serve over protect) unless abuse-critical |
| Identity | Authenticated key | Client IP | Auth key (unspoofable); IP only for anonymous |

## Scaling

- **Check throughput:** Redis Cluster with sharding by rate limit key
- **Rules storage:** In-memory cache (rules change rarely)
- **Multi-region:** Per-region rate limiting (simple) or global (complex, needs sync)
- **Hot keys:** Local counters with periodic sync

## Reliability

- **Redis failure:** Fail open (allow all requests — prefer service over protection)
- **Network partition:** Local rate limiting per instance (approximate but functional)
- **Stale rules:** Cache with background refresh (rules change slowly)
- **Thundering herd after limit resets:** Jitter in reset times

## Security

- Rate limiting itself is a security mechanism (DDoS protection)
- Rate limit key cannot be spoofed (use authenticated identity, not client-provided)
- Defense against distributed attacks (rate limit by IP subnet, not just individual IP)
- Separate limits for authenticated vs. unauthenticated requests

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Sliding window counter | Memory for accuracy |
| Centralized (Redis) | Latency for accuracy |
| Fail-open | Protection for availability |
| Token bucket | Complexity for burst tolerance |
| Per-instance local count | Accuracy for latency |

## Follow-up Questions

1. "How would you rate limit a distributed denial-of-service attack?"
2. "How would you implement adaptive rate limiting (auto-adjust limits based on system load)?"
3. "How would you handle rate limiting for WebSocket connections?"
4. "How would you implement hierarchical rate limiting (user → org → global)?"
5. "How would you implement rate limiting that's fair across time zones?"

---



# 4. Design Distributed Lock Service

## Requirements

### Functional Requirements
- Acquire/release named locks (mutex)
- Lock with TTL (automatic release on timeout)
- Reentrant locks (same owner can acquire multiple times)
- Read-write locks (multiple readers, exclusive writer)
- Lock queuing (fair ordering of waiters)
- Fencing tokens (prevent stale lock holders from making changes)

### Non-Functional Requirements
- **Latency:** Lock acquisition < 10ms p99
- **Availability:** 99.99%
- **Correctness:** No two clients hold the same lock simultaneously (safety)
- **Liveness:** Locks are eventually released (no deadlocks from failures)
- **Scale:** 1M+ active locks, 100K+ lock operations/sec

## Capacity Estimation

```
Active locks: 1M concurrent
Lock operations: 100K/sec (acquire + release + heartbeat)
Lock metadata: ~200 bytes per lock
Total memory: 1M × 200 bytes = 200MB (easily fits in memory)
Lock hold duration: average 500ms, max 30 seconds
Waiters per lock: average 2, max 100 for hot locks
```

## APIs

```
// Acquire lock
POST /v1/locks/{lock_name}/acquire
Body: {owner_id, ttl_ms: 30000, wait_timeout_ms: 5000}
Response: {acquired: true, fence_token: 42, expires_at}

// Release lock
POST /v1/locks/{lock_name}/release
Body: {owner_id, fence_token: 42}
Response: {released: true}

// Heartbeat (extend TTL)
POST /v1/locks/{lock_name}/heartbeat
Body: {owner_id, fence_token: 42, extend_ms: 30000}
Response: {extended: true, new_expires_at}

// Try acquire (non-blocking)
POST /v1/locks/{lock_name}/try-acquire
Body: {owner_id, ttl_ms: 30000}
Response: {acquired: bool, fence_token?, queue_position?}

// Read-write lock
POST /v1/locks/{lock_name}/acquire-read
POST /v1/locks/{lock_name}/acquire-write
```

## High-Level Design

```
┌───────────────┐     ┌──────────────────────────────────────────┐
│ Client        │────▶│       Lock Service Cluster                │
│ (Lock SDK)    │     │                                          │
└───────────────┘     │  ┌─────────┐  ┌─────────┐  ┌─────────┐ │
                      │  │ Node 1  │  │ Node 2  │  │ Node 3  │ │
                      │  │ (Leader)│──│(Follower)│──│(Follower)│ │
                      │  └────┬────┘  └─────────┘  └─────────┘ │
                      │       │                                  │
                      │  ┌────▼────────────────────────────────┐ │
                      │  │    Raft Consensus (Replicated Log)  │ │
                      │  └─────────────────────────────────────┘ │
                      │                                          │
                      │  ┌─────────────────────────────────────┐ │
                      │  │    Lock State Machine                │ │
                      │  │    (Lock table + Wait queues)        │ │
                      │  └─────────────────────────────────────┘ │
                      └──────────────────────────────────────────┘
```

## Deep Dive

### Fencing Tokens (Preventing Stale Locks)

```
PROBLEM: Client A acquires lock, gets paused by GC for 30 seconds.
Lock expires (TTL). Client B acquires lock, makes changes.
Client A resumes, thinks it still has lock, makes conflicting changes.

SOLUTION: Monotonically increasing fence tokens

1. Each lock acquisition gets a unique, monotonically increasing token
2. Client A gets token 41, Client B gets token 42
3. Protected resource checks: only accept writes with token >= stored token
4. Client A wakes up with token 41, resource rejects (current token is 42)

IMPLEMENTATION:
- Lock table stores: {lock_name: {owner, fence_token, expires_at}}
- fence_token is globally monotonic counter (per lock name)
- Every acquire increments fence_token
- Client MUST include fence_token in all operations on protected resource
- Resource MUST validate: if request.token < stored.token → reject

THIS IS WHY REDIS REDLOCK IS PROBLEMATIC:
- Redlock doesn't provide fencing tokens
- GC pauses, network delays can cause stale holders to act
- For correct distributed locks, need: consensus + fencing
```

### Consensus-Based Lock Implementation

```
USING RAFT FOR CORRECTNESS:

Lock State Machine (applied from Raft log):
- ACQUIRE(lock_name, owner_id, ttl) → success/queued
- RELEASE(lock_name, owner_id, fence_token) → success/error
- EXPIRE(lock_name) → triggered by TTL timer
- HEARTBEAT(lock_name, owner_id) → extends TTL

WHY RAFT (not Redis):
- Leader handles all lock operations → linearizable
- Replicated log → survives node failures
- Consistent state across cluster
- Leader election → automatic failover (< 5 seconds)

LOCK ACQUISITION FLOW:
1. Client sends ACQUIRE to leader (via any node, forwarded if needed)
2. Leader appends to Raft log
3. Raft replicates to majority (2/3 nodes)
4. Leader applies to state machine:
   - If lock is free: grant, assign fence_token, start TTL timer
   - If lock is held: add to wait queue, return queue position
5. Return result to client
6. If in wait queue: notify when lock becomes available

PERFORMANCE OPTIMIZATION:
- Read-only operations (check lock status) can be served from any node
- Write operations (acquire/release) must go through leader
- Batching: group multiple lock operations in single Raft entry
- Session-based: client maintains session, all locks released if session dies
```

### Wait Queue and Fairness

```
FAIR QUEUING:
- FIFO queue per lock name
- When lock released: grant to head of queue
- Wait timeout: if not granted within timeout, remove from queue
- Priority queues (optional): high-priority locks jump ahead

DEADLOCK PREVENTION:
- TTL on all locks (guaranteed eventual release)
- Wait timeouts (don't wait forever)
- Lock ordering (clients must acquire locks in consistent order)
- Deadlock detection (wait-for graph analysis, periodic)

READ-WRITE LOCK:
- Multiple readers can hold simultaneously
- Writer needs exclusive access
- Writer waits for all readers to release
- New readers wait if writer is queued (prevents writer starvation)
```

## Tools & Frameworks (with trade-offs)

```
CONSENSUS BACKEND:  etcd (Raft, native leases+fencing) | ZooKeeper (ZAB, ephemeral
                    znodes) | Consul (Raft, sessions). All give correct CP locks.
WEAK/FAST OPTION:   Redis Redlock — fast but NO fencing tokens → unsafe under GC
                    pauses/partitions. Use only for best-effort, non-critical locks.
DB-BASED:           Postgres advisory locks / SELECT...FOR UPDATE — simple, reuses
                    existing DB; limited throughput, ties locks to DB availability.
CLIENT SDK:         Curator (ZK recipes), jetcd, Consul SDK.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Backend | etcd/ZK/Consul (CP) | Redis Redlock | CP+fencing for correctness; Redlock only best-effort |
| Fencing | Monotonic token | None | Always use fencing for correctness-critical resources |
| Expiry | TTL + heartbeat | Session-based | Session (ZK ephemeral) auto-releases on client death |
| Granularity | Single Raft group | Sharded by name | Shard when lock op throughput exceeds one group |

## Scaling

- **Throughput:** Partition locks across multiple Raft groups (by lock name hash)
- **Read scaling:** Followers serve read-only operations (check lock status)
- **Geographic:** Lock service per region (locks are region-scoped)

## Reliability

- **Node failure:** Raft election within 5 seconds, no lock state lost
- **Network partition:** Only majority partition can grant locks (safety)
- **Client crash:** TTL auto-releases, session timeout releases all client locks
- **Leader failure:** New leader has full state (replicated log)

## Security

- Client authentication (mTLS)
- Lock namespace ACLs (who can acquire which locks)
- Audit logging for lock operations
- Rate limiting per client (prevent lock hoarding)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Raft consensus | Latency (majority write) for correctness |
| Fencing tokens | Client complexity for stale-lock safety |
| TTL-based expiry | Potential premature release for deadlock prevention |
| Fair queuing | Throughput for fairness |
| CP over AP | Unavailable during partition for lock correctness |

## Follow-up Questions

1. "How would you implement distributed lock with Redis (Redlock)? What are its limitations?"
2. "How would you handle a lock holder that's making progress but slowly?"
3. "How would you implement advisory locks vs. mandatory locks?"
4. "How would you scale locks beyond a single Raft group?"
5. "How would you implement lock-free alternatives where possible?"
# 5. Design Distributed Lock Service

## Requirements

### Functional Requirements
- Acquire/release named locks (mutex)
- Lock with TTL (automatic release on timeout)
- Reentrant locks (same owner can acquire multiple times)
- Read-write locks (multiple readers, exclusive writer)
- Lock queuing (fair ordering of waiters)
- Fencing tokens (prevent stale lock holders from making changes)

### Non-Functional Requirements
- **Latency:** Lock acquisition < 10ms p99
- **Availability:** 99.99%
- **Correctness:** No two clients hold the same lock simultaneously (safety)
- **Liveness:** Locks are eventually released (no deadlocks from failures)
- **Scale:** 1M+ active locks, 100K+ lock operations/sec

## Capacity Estimation

```
Active locks: 1M concurrent
Lock operations: 100K/sec (acquire + release + heartbeat)
Lock metadata: ~200 bytes per lock
Total memory: 1M × 200 bytes = 200MB (easily fits in memory)
Lock hold duration: average 500ms, max 30 seconds
Waiters per lock: average 2, max 100 for hot locks
```

## APIs

```
// Acquire lock
POST /v1/locks/{lock_name}/acquire
Body: {owner_id, ttl_ms: 30000, wait_timeout_ms: 5000}
Response: {acquired: true, fence_token: 42, expires_at}

// Release lock
POST /v1/locks/{lock_name}/release
Body: {owner_id, fence_token: 42}
Response: {released: true}

// Heartbeat (extend TTL)
POST /v1/locks/{lock_name}/heartbeat
Body: {owner_id, fence_token: 42, extend_ms: 30000}
Response: {extended: true, new_expires_at}

// Try acquire (non-blocking)
POST /v1/locks/{lock_name}/try-acquire
Body: {owner_id, ttl_ms: 30000}
Response: {acquired: bool, fence_token?, queue_position?}

// Read-write lock
POST /v1/locks/{lock_name}/acquire-read
POST /v1/locks/{lock_name}/acquire-write
```

## High-Level Design

```
┌───────────────┐     ┌──────────────────────────────────────────┐
│ Client        │────▶│       Lock Service Cluster                │
│ (Lock SDK)    │     │                                          │
└───────────────┘     │  ┌─────────┐  ┌─────────┐  ┌─────────┐ │
                      │  │ Node 1  │  │ Node 2  │  │ Node 3  │ │
                      │  │ (Leader)│──│(Follower)│──│(Follower)│ │
                      │  └────┬────┘  └─────────┘  └─────────┘ │
                      │       │                                  │
                      │  ┌────▼────────────────────────────────┐ │
                      │  │    Raft Consensus (Replicated Log)  │ │
                      │  └─────────────────────────────────────┘ │
                      │                                          │
                      │  ┌─────────────────────────────────────┐ │
                      │  │    Lock State Machine                │ │
                      │  │    (Lock table + Wait queues)        │ │
                      │  └─────────────────────────────────────┘ │
                      └──────────────────────────────────────────┘
```

## Deep Dive

### Fencing Tokens (Preventing Stale Locks)

```
PROBLEM: Client A acquires lock, gets paused by GC for 30 seconds.
Lock expires (TTL). Client B acquires lock, makes changes.
Client A resumes, thinks it still has lock, makes conflicting changes.

SOLUTION: Monotonically increasing fence tokens

1. Each lock acquisition gets a unique, monotonically increasing token
2. Client A gets token 41, Client B gets token 42
3. Protected resource checks: only accept writes with token >= stored token
4. Client A wakes up with token 41, resource rejects (current token is 42)

IMPLEMENTATION:
- Lock table stores: {lock_name: {owner, fence_token, expires_at}}
- fence_token is globally monotonic counter (per lock name)
- Every acquire increments fence_token
- Client MUST include fence_token in all operations on protected resource
- Resource MUST validate: if request.token < stored.token → reject

THIS IS WHY REDIS REDLOCK IS PROBLEMATIC:
- Redlock doesn't provide fencing tokens
- GC pauses, network delays can cause stale holders to act
- For correct distributed locks, need: consensus + fencing
```

### Consensus-Based Lock Implementation

```
USING RAFT FOR CORRECTNESS:

Lock State Machine (applied from Raft log):
- ACQUIRE(lock_name, owner_id, ttl) → success/queued
- RELEASE(lock_name, owner_id, fence_token) → success/error
- EXPIRE(lock_name) → triggered by TTL timer
- HEARTBEAT(lock_name, owner_id) → extends TTL

WHY RAFT (not Redis):
- Leader handles all lock operations → linearizable
- Replicated log → survives node failures
- Consistent state across cluster
- Leader election → automatic failover (< 5 seconds)

LOCK ACQUISITION FLOW:
1. Client sends ACQUIRE to leader (via any node, forwarded if needed)
2. Leader appends to Raft log
3. Raft replicates to majority (2/3 nodes)
4. Leader applies to state machine:
   - If lock is free: grant, assign fence_token, start TTL timer
   - If lock is held: add to wait queue, return queue position
5. Return result to client
6. If in wait queue: notify when lock becomes available

PERFORMANCE OPTIMIZATION:
- Read-only operations (check lock status) can be served from any node
- Write operations (acquire/release) must go through leader
- Batching: group multiple lock operations in single Raft entry
- Session-based: client maintains session, all locks released if session dies
```

### Wait Queue and Fairness

```
FAIR QUEUING:
- FIFO queue per lock name
- When lock released: grant to head of queue
- Wait timeout: if not granted within timeout, remove from queue
- Priority queues (optional): high-priority locks jump ahead

DEADLOCK PREVENTION:
- TTL on all locks (guaranteed eventual release)
- Wait timeouts (don't wait forever)
- Lock ordering (clients must acquire locks in consistent order)
- Deadlock detection (wait-for graph analysis, periodic)

READ-WRITE LOCK:
- Multiple readers can hold simultaneously
- Writer needs exclusive access
- Writer waits for all readers to release
- New readers wait if writer is queued (prevents writer starvation)
```

## Tools & Frameworks (with trade-offs)

```
CONSENSUS BACKEND:  etcd (Raft, native leases+fencing) | ZooKeeper (ZAB, ephemeral
                    znodes) | Consul (Raft, sessions). All give correct CP locks.
WEAK/FAST OPTION:   Redis Redlock — fast but NO fencing tokens → unsafe under GC
                    pauses/partitions. Use only for best-effort, non-critical locks.
DB-BASED:           Postgres advisory locks / SELECT...FOR UPDATE — simple, reuses
                    existing DB; limited throughput, ties locks to DB availability.
CLIENT SDK:         Curator (ZK recipes), jetcd, Consul SDK.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Backend | etcd/ZK/Consul (CP) | Redis Redlock | CP+fencing for correctness; Redlock only best-effort |
| Fencing | Monotonic token | None | Always use fencing for correctness-critical resources |
| Expiry | TTL + heartbeat | Session-based | Session (ZK ephemeral) auto-releases on client death |
| Granularity | Single Raft group | Sharded by name | Shard when lock op throughput exceeds one group |

## Scaling

- **Throughput:** Partition locks across multiple Raft groups (by lock name hash)
- **Read scaling:** Followers serve read-only operations (check lock status)
- **Geographic:** Lock service per region (locks are region-scoped)

## Reliability

- **Node failure:** Raft election within 5 seconds, no lock state lost
- **Network partition:** Only majority partition can grant locks (safety)
- **Client crash:** TTL auto-releases, session timeout releases all client locks
- **Leader failure:** New leader has full state (replicated log)

## Security

- Client authentication (mTLS)
- Lock namespace ACLs (who can acquire which locks)
- Audit logging for lock operations
- Rate limiting per client (prevent lock hoarding)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Raft consensus | Latency (majority write) for correctness |
| Fencing tokens | Client complexity for stale-lock safety |
| TTL-based expiry | Potential premature release for deadlock prevention |
| Fair queuing | Throughput for fairness |
| CP over AP | Unavailable during partition for lock correctness |

## Follow-up Questions

1. "How would you implement distributed lock with Redis (Redlock)? What are its limitations?"
2. "How would you handle a lock holder that's making progress but slowly?"
3. "How would you implement advisory locks vs. mandatory locks?"
4. "How would you scale locks beyond a single Raft group?"
5. "How would you implement lock-free alternatives where possible?"

---
