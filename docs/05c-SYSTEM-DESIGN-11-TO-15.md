# PART 5C — SYSTEM DESIGN: Questions 11-15

---

# 11. Design Distributed Lock Service

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

# 12. Design Configuration Management System

## Requirements

### Functional Requirements
- Centralized configuration storage for all services
- Dynamic configuration updates (no redeploy needed)
- Configuration versioning and rollback
- Environment-specific configs (dev, staging, production)
- Feature flags with targeting rules
- Configuration inheritance and overrides (global → service → instance)
- Audit trail for all changes

### Non-Functional Requirements
- **Latency:** Config reads < 5ms p99
- **Availability:** 99.999% (config unavailable = services can't start)
- **Consistency:** Strongly consistent writes, eventually consistent reads (< 5s propagation)
- **Scale:** 10,000 services, 1M+ config keys, 10M+ reads/sec
- **Safety:** Bad config shouldn't take down production

## Capacity Estimation

```
Services: 10,000
Config keys: 1M total (100 per service average)
Config value average size: 500 bytes
Total config data: 1M × 500B = 500MB (small, easily replicated)
Reads: 10M/sec (services constantly reading config)
Writes: 100/sec (config changes are infrequent)
Watch subscriptions: 100K (services watching for changes)
Feature flag evaluations: 50M/sec
```

## APIs

```
// Read config
GET /v1/config/{namespace}/{key}?environment=production
Response: {key, value, version, last_modified}

// Batch read
GET /v1/config/{namespace}?keys=key1,key2,key3&environment=production
Response: {configs: [{key, value, version}]}

// Write config (with approval workflow)
PUT /v1/config/{namespace}/{key}
Body: {value, environment, comment, change_ticket}
Response: {version, status: "pending_approval" | "applied"}

// Feature flag evaluation
POST /v1/flags/evaluate
Body: {flag_name, context: {user_id, segment, percentage_key}}
Response: {enabled: bool, variant: "control" | "treatment_a"}

// Watch for changes
GET /v1/config/{namespace}/watch?since_version={version}
SSE stream: {key, new_value, version, change_type}

// Rollback
POST /v1/config/{namespace}/{key}/rollback
Body: {target_version}
```

## High-Level Design

```
┌──────────────────┐     ┌────────────────────────────────────────────────┐
│  Admin UI /      │────▶│           Config Service                        │
│  CLI / API       │     │                                                │
└──────────────────┘     │  ┌─────────────┐    ┌───────────────────────┐ │
                         │  │  Write Path │    │  Read Path            │ │
                         │  │  (Leader)    │    │  (Any replica)        │ │
                         │  └──────┬──────┘    └──────┬────────────────┘ │
                         │         │                  │                   │
                         │  ┌──────▼──────────────────▼────────────────┐ │
                         │  │    Replicated Config Store (etcd/Raft)   │ │
                         │  └─────────────────────┬────────────────────┘ │
                         │                        │                       │
                         │  ┌─────────────────────▼────────────────────┐ │
                         │  │    Change Notification (Watch/Push)       │ │
                         │  └──────────────────────────────────────────┘ │
                         └────────────────────────────────────────────────┘
                                              │
                         ┌────────────────────┼────────────────────┐
                         ▼                    ▼                    ▼
                  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
                  │ Service A    │  │ Service B    │  │ Service C    │
                  │ ┌──────────┐│  │ ┌──────────┐│  │ ┌──────────┐│
                  │ │Local Cache││  │ │Local Cache││  │ │Local Cache││
                  │ │(in-memory)││  │ │(in-memory)││  │ │(in-memory)││
                  │ └──────────┘│  │ └──────────┘│  │ └──────────┘│
                  └──────────────┘  └──────────────┘  └──────────────┘
```

## Deep Dive

### Safe Config Deployment

```
CONFIGURATION SAFETY PIPELINE:

1. VALIDATION (before write):
   ├── Schema validation (type checking, range checking)
   ├── Syntax validation (JSON/YAML parseable)
   ├── Dependency validation (referenced configs exist)
   ├── Size limits (prevent accidentally writing 100MB config)
   └── Conflict detection (concurrent modification)

2. APPROVAL WORKFLOW:
   ├── Low-risk changes: auto-approve (non-production, non-critical keys)
   ├── Medium-risk: single reviewer approval
   ├── High-risk: multiple reviewers + designated approver
   └── Critical: additional SRE approval required

3. PROGRESSIVE ROLLOUT:
   ├── Canary: Apply to 1% of instances, monitor for 5 minutes
   ├── Regional: Apply to one region, monitor for 15 minutes
   ├── Global: Apply to all instances
   └── At each stage: automated rollback if error rate increases

4. ROLLBACK:
   ├── Every config change is versioned (immutable versions)
   ├── One-click rollback to any previous version
   ├── Automatic rollback if health check fails post-change
   └── Emergency: global freeze (prevent all config changes)

5. AUDIT:
   ├── Who changed what, when, why
   ├── Approval chain recorded
   ├── Before/after values stored
   └── Linked to incident if change caused issue
```

### Feature Flags System

```
FEATURE FLAG ARCHITECTURE:

Flag Definition:
├── flag_name: "new_recommendation_model"
├── type: boolean | string | number | json
├── default_value: false
├── targeting_rules: [
│     {condition: "user.segment == 'beta'", value: true, priority: 1},
│     {condition: "user.id IN percentage(10%)", value: true, priority: 2},
│     {condition: "user.country == 'US'", value: true, priority: 3}
│   ]
├── kill_switch: false (overrides all rules to default)
└── environment_overrides: {staging: always_true}

EVALUATION LOGIC:
1. Check kill switch → if active, return default
2. Evaluate targeting rules in priority order
3. First matching rule → return its value
4. No rules match → return default value
5. Log evaluation for analytics

PERCENTAGE ROLLOUT:
- Hash(user_id + flag_name) % 100 → bucket
- "10% rollout" = buckets 0-9 get treatment
- Deterministic: same user always gets same result
- Increasing percentage adds buckets (existing users not flipped)

PERFORMANCE:
- Flag definitions cached locally in each service (< 1KB per flag)
- Evaluation is pure in-memory computation (no network call)
- Flag changes propagated via watch within 5 seconds
- 50M evaluations/sec at negligible CPU cost (simple rules)
```

### Configuration Inheritance

```
HIERARCHY:
Global defaults → Service defaults → Environment override → Instance override

EXAMPLE:
Global:     timeout_ms = 5000
Service A:  timeout_ms = 3000  (overrides global)
Production: timeout_ms = 2000  (overrides service default for prod)
Instance-1: timeout_ms = 1000  (overrides for specific instance, e.g., testing)

RESOLUTION:
For Service A, Instance-1, Production:
1. Check instance override → 1000 (found, use this)
2. If not found: check environment override → 2000
3. If not found: check service default → 3000
4. If not found: check global default → 5000

IMPLEMENTATION:
- Store at each level independently
- Resolve at read time (or pre-compute and cache resolved view)
- Pre-computed "effective config" refreshed on any change in hierarchy
- Override visibility: UI shows inheritance chain for any key
```

## Tools & Frameworks (with trade-offs)

```
CONFIG STORE:    etcd / ZooKeeper / Consul KV (watch + strong consistency) |
                 in-house (LinkedIn). Small data, watch-driven propagation.
FEATURE FLAGS:   LaunchDarkly (managed) | Unleash/Flagsmith (OSS) | in-house.
DISTRIBUTION:    long-poll/watch (etcd watch, ZK watcher) vs. polling (simpler,
                 staler). gRPC streaming for low-latency push.
CLIENT CACHE:    local in-memory snapshot + background refresh (survives outage).
SECRETS:         Vault / cloud KMS — keep OUT of plain config (separate system).
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Store | etcd/ZK (CP) | Git-backed | CP store for dynamic; Git for audited, review-gated config |
| Propagation | Watch/stream | Polling | Watch for <5s freshness; polling for simplicity |
| Flags | LaunchDarkly | OSS/in-house | Managed for speed; in-house for scale/cost/control |
| Secrets | Vault/KMS | Inline (never) | Always a dedicated secrets manager |
| Rollout | Progressive + auto-rollback | All-at-once | Progressive for prod safety |

## Scaling

- **Reads:** Local client cache (10M/sec is satisfied from memory)
- **Writes:** Leader-based (100/sec is easily handled by single leader)
- **Watch:** Fan-out from any replica, chunked by namespace
- **Multi-region:** Replicated store with local read replicas

## Reliability

- **Config service failure:** Clients use cached config (stale but available)
- **Bad config deployed:** Automatic rollback on health regression
- **Network partition:** Clients use cached config, writes queue until partition heals
- **Cache corruption:** Pull fresh config from service on next heartbeat

## Security

- RBAC: who can read/write which configs
- Secrets stored encrypted (separate secrets manager integration)
- Config values masked in audit logs if sensitive
- Change approval workflow prevents unauthorized changes

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Client-side cache | Staleness (seconds) for availability |
| Approval workflow | Speed of change for safety |
| Progressive rollout | Deployment time for risk reduction |
| Inheritance hierarchy | Complexity for flexibility |
| Strong write consistency | Write latency for correctness |

## Follow-up Questions

1. "How would you handle configuration for a multi-tenant system?"
2. "How would you implement configuration drift detection?"
3. "How would you handle secrets alongside configuration?"
4. "How would you test configuration changes before deployment?"
5. "How would you handle configuration dependencies (config A depends on config B)?"

---

# 13. Design Rate Limiter

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

# 14. Design Metrics Platform

## Requirements

### Functional Requirements
- Ingest metrics from 100,000+ service instances
- Support metric types: counter, gauge, histogram, summary
- Query and aggregate metrics across dimensions (service, host, region, endpoint)
- Dashboarding and visualization
- Alerting based on metric thresholds and patterns
- Long-term storage with automatic downsampling
- Custom metric creation and ad-hoc queries

### Non-Functional Requirements
- **Ingestion latency:** Metrics queryable within 30 seconds of emission
- **Query latency:** < 1 second for dashboard queries, < 10 seconds for ad-hoc
- **Availability:** 99.9% (for ingestion), 99.99% (for alerting)
- **Scale:** 100M+ active time series, 10M+ samples/sec
- **Retention:** Full resolution 15 days, downsampled 1 year, aggregated 5 years

## Capacity Estimation

```
Active time series: 100M
Sample rate: 15-second intervals = 100M / 15 = 6.7M samples/sec
With burst: 10M samples/sec
Sample size: 16 bytes (timestamp: 8, value: 8) + labels ~200 bytes
Ingestion bandwidth: 10M × 216 bytes = 2.16 GB/sec
Storage (full resolution, 15 days): 10M × 16B × (86400/15) × 15 = 8.6PB
With compression (Gorilla encoding): ~10:1 → 860TB for 15 days
Queries: 10,000/sec (dashboards refreshing every 30s)
Alert evaluations: 1M alert rules evaluated every 15 seconds
```

## High-Level Design

```
┌──────────────────┐     ┌───────────────────────────────────────────────────┐
│ Service          │────▶│                Metrics Platform                    │
│ Instances        │     │                                                   │
│ (Prometheus      │     │  ┌──────────┐    ┌──────────────┐               │
│  exporters)      │     │  │ Ingestion│───▶│  Write Path  │               │
└──────────────────┘     │  │ (Push or │    │  (Partitioned│               │
                         │  │  Pull)   │    │   by metric) │               │
                         │  └──────────┘    └──────┬───────┘               │
                         │                         │                        │
                         │  ┌──────────────────────▼───────────────────┐   │
                         │  │         Time-Series Database              │   │
                         │  │  (Hot: NVMe SSD | Cold: Object Storage)  │   │
                         │  └──────────────────────┬───────────────────┘   │
                         │                         │                        │
                         │  ┌──────────────────────▼───────────────────┐   │
                         │  │         Query Engine                      │   │
                         │  │  (PromQL-compatible, distributed)        │   │
                         │  └──────────────────────┬───────────────────┘   │
                         │                         │                        │
                         │         ┌───────────────┼───────────────┐       │
                         │         ▼               ▼               ▼       │
                         │  ┌───────────┐  ┌───────────┐  ┌───────────┐   │
                         │  │Dashboards │  │ Alerting  │  │  API      │   │
                         │  │(Grafana)  │  │ Engine    │  │           │   │
                         │  └───────────┘  └───────────┘  └───────────┘   │
                         └─────────────────────────────────────────────────┘
```

## Deep Dive

### Time-Series Storage Engine

```
DATA MODEL:
Metric: http_requests_total{service="feed", endpoint="/api/v1/feed", status="200"}
       at timestamp T = value V

Series = unique combination of metric name + label set
Sample = (timestamp, value) pair within a series

STORAGE LAYOUT (Columnar, time-partitioned):
├── Block: 2-hour time window
│   ├── Index: series_id → {labels, chunk_references}
│   ├── Chunks: compressed (timestamp, value) pairs per series
│   └── Tombstones: deleted time ranges
├── Compaction: Merge blocks → larger blocks over time
└── Retention: Drop blocks older than retention policy

COMPRESSION (Gorilla encoding - Facebook):
Timestamps:
- Delta-of-delta encoding
- Most samples are exactly 15s apart → delta-of-delta = 0 → 1 bit
- Achieves ~1.4 bits per sample for timestamps

Values:
- XOR with previous value
- If values change slowly → XOR has many leading/trailing zeros
- Encode only the meaningful bits
- Achieves ~8 bits per sample for values (vs. 64 bits raw)

TOTAL: ~1.4 + 8 = 9.4 bits per sample vs. 128 bits raw = 13.6x compression

INGESTION PATH:
1. Samples arrive at ingestion service
2. Routed to correct TSDB node by metric hash (consistent hashing)
3. Buffered in WAL (Write-Ahead Log) for durability
4. Accumulated in memory (head block)
5. When head block reaches 2 hours → flush to disk as immutable block
6. Background compaction merges small blocks → larger blocks
```

### Query Engine (Distributed)

```
QUERY LANGUAGE (PromQL-like):
- rate(http_requests_total{service="feed"}[5m])
- histogram_quantile(0.99, rate(http_request_duration_seconds_bucket[5m]))
- sum by (service) (rate(http_requests_total[1m]))

DISTRIBUTED QUERY EXECUTION:
1. Parse query → execution plan
2. Identify which TSDB nodes have relevant data (by metric + time range)
3. Fan-out sub-queries to relevant nodes
4. Each node:
   - Find matching series (label index lookup)
   - Fetch chunks for time range
   - Decompress and apply function (rate, avg, etc.)
   - Return partial result
5. Query coordinator merges partial results
6. Apply final aggregation (sum, avg, max, percentile)

OPTIMIZATIONS:
- Predicate pushdown: filter labels at storage layer
- Time range pruning: only scan relevant blocks
- Caching: Recent query results cached (30s TTL for dashboard queries)
- Pre-aggregation: For common queries, maintain pre-computed aggregates
- Parallel execution: Multiple series evaluated in parallel
```

### Alerting Engine

```
ALERT EVALUATION PIPELINE:

1. Alert Rule:
   alert: HighErrorRate
   expr: rate(http_errors_total{service="feed"}[5m]) > 0.01
   for: 2m  (must be true for 2 minutes to fire)
   labels: {severity: "critical"}
   annotations: {summary: "Feed service error rate > 1%"}

2. Evaluation:
   - Every 15 seconds: evaluate all alert rules
   - For each rule: execute PromQL query
   - Compare result to threshold
   - Track state: inactive → pending → firing

3. State Machine:
   INACTIVE → PENDING (condition true, "for" duration counting)
   PENDING → FIRING (condition true for full "for" duration)
   FIRING → INACTIVE (condition becomes false)
   PENDING → INACTIVE (condition becomes false before "for" expires)

4. Notification:
   - FIRING → Send alert to notification channels
   - Deduplication: Don't re-send if already firing
   - Grouping: Batch related alerts (same service, same time)
   - Silencing: Suppress during maintenance windows
   - Routing: Different severities → different channels

MULTI-CONDITION ALERTS:
- Compound conditions: A AND B AND NOT C
- Anomaly detection: value > predicted_value + 3*stddev
- Rate of change: deriv() > threshold
- Missing data: absent(metric{service="feed"}) for 5 minutes
```

### Downsampling Strategy

```
RETENTION TIERS:
├── Full resolution (15s): 15 days
├── 1-minute aggregates: 90 days
├── 5-minute aggregates: 1 year
├── 1-hour aggregates: 5 years
└── 1-day aggregates: forever

AGGREGATES STORED AT EACH TIER:
For each time window: {min, max, sum, count, avg}
This allows computing any aggregate from any tier:
- Average: sum / count
- Rate: (sum_end - sum_start) / time_delta
- Percentiles: Lost after downsampling (use histograms for long-term)

DOWNSAMPLING PIPELINE:
- Background job runs continuously
- Reads full-resolution data approaching retention boundary
- Computes aggregates for next tier
- Writes to cold storage (S3 + columnar format)
- Deletes full-resolution data past retention
```

## Tools & Frameworks (with trade-offs)

```
TSDB:        Prometheus (pull, single-node) + Thanos/Cortex/Mimir (HA, long-term,
             horizontally scalable) | VictoriaMetrics (fast, cost-efficient) |
             InfluxDB | M3DB (Uber, built for scale).
INGESTION:   Pull (Prometheus scrape) vs. push (OpenTelemetry / StatsD / Graphite).
COLLECTION:  OpenTelemetry Collector (vendor-neutral) is the modern standard.
VISUALIZATION:Grafana (de-facto). QUERY: PromQL.
ALERTING:    Alertmanager (routing, dedup, silences).
COLD STORE:  S3 + Parquet via Thanos/Mimir blocks; Gorilla compression.
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Backend | Prometheus + Thanos/Mimir | VictoriaMetrics | Thanos for Prom ecosystem; VM for cost/perf at scale |
| Ingestion | Pull (scrape) | Push (OTel) | Pull for service discovery; push for batch/serverless |
| Collector | OpenTelemetry | Vendor agent | OTel for neutrality; vendor for turnkey features |
| Long-term | Object store blocks | Hosted SaaS | Self-host for cost at scale; SaaS for low ops |
| Cardinality | Limit labels | Pre-aggregate | Control cardinality — #1 cause of TSDB blowups |

## Scaling

- **Ingestion:** Partition by metric name hash across TSDB nodes
- **Storage:** Tiered storage (hot: NVMe, warm: SSD, cold: S3)
- **Query:** Distributed execution, result caching
- **Multi-region:** Per-region metrics clusters with federated queries

## Reliability

- **Ingestion node failure:** WAL replay from replica, no data loss
- **Query node failure:** Retry on another node (stateless)
- **Storage failure:** Replication (3 copies of hot data)
- **Alerting failure:** Multi-instance alert evaluator (HA pair)

## Security

- Metric data access control (per team/service)
- PII not stored in metric labels (validation at ingestion)
- Encryption at rest for stored metrics
- Rate limiting on query API (prevent expensive queries from DoS)

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Pull-based (Prometheus-style) | Service discovery dependency for simplicity |
| Gorilla compression | CPU for storage efficiency |
| Downsampling | Precision loss for storage cost |
| 15s resolution | Storage for granularity |
| Eventual consistency on reads | Query accuracy for write performance |

## Follow-up Questions

1. "How would you handle metrics cardinality explosion (too many unique label combinations)?"
2. "How would you implement anomaly detection on metrics at scale?"
3. "How would you handle metrics during a network partition between data centers?"
4. "How would you implement custom metrics without impacting platform stability?"
5. "How would you support both push and pull ingestion models?"

---

# 15. Design LLM Serving Platform

## Requirements

### Functional Requirements
- Serve multiple LLM models (GPT-scale, 7B-175B parameters)
- Support inference APIs: completion, chat, embedding
- Multi-tenant serving (multiple teams share GPU infrastructure)
- Model versioning and A/B testing
- Streaming token generation (server-sent events)
- Request routing (different models for different use cases)
- Prompt management and caching

### Non-Functional Requirements
- **Latency:** Time-to-first-token < 500ms, inter-token < 50ms
- **Throughput:** 10,000+ requests/sec across all models
- **Availability:** 99.9% (with graceful degradation)
- **GPU Utilization:** > 80% (GPUs are expensive)
- **Scale:** Support models from 7B to 175B parameters
- **Cost Efficiency:** Minimize $/token across workloads

## Capacity Estimation

```
Models deployed: 20 models (various sizes)
GPU cluster: 500 H100 GPUs (80GB each)
Requests: 10,000/sec aggregate across models
Tokens generated: 1M tokens/sec (avg 100 tokens/request)
KV cache memory: 40% of GPU memory dedicated
Model weights: 7B = 14GB FP16, 70B = 140GB FP16 (multi-GPU)
Batch size: 32-256 concurrent requests per GPU (continuous batching)
Cost: ~$3/hour per H100 = $1.5M/month for 500 GPUs
```

## APIs

```
// Chat completion
POST /v1/chat/completions
Body: {
  model: "llama-70b-v2",
  messages: [{role: "user", content: "..."}],
  max_tokens: 256,
  temperature: 0.7,
  stream: true,
  metadata: {team: "recommendation", use_case: "content_generation"}
}
Response (streaming): 
  data: {"choices": [{"delta": {"content": "Hello"}}]}
  data: {"choices": [{"delta": {"content": " world"}}]}
  data: [DONE]

// Embedding
POST /v1/embeddings
Body: {model: "embedding-large", input: ["text1", "text2"]}
Response: {data: [{embedding: [0.1, 0.2, ...], index: 0}]}

// Model management (admin)
POST /v1/models/deploy
Body: {model_id, model_artifact_path, gpu_requirements, scaling_config}

GET /v1/models
Response: {models: [{id, status, gpu_count, current_load, queue_depth}]}
```

## Data Model

```
Model Registry:
├── model_id
├── model_name, version
├── artifact_path (S3/model store)
├── architecture (transformer, moe)
├── parameter_count
├── gpu_requirements {count, memory_per_gpu, tensor_parallel_degree}
├── serving_config {max_batch_size, max_seq_len, quantization}
├── routing_config {traffic_weight, canary_percentage}
└── status (deploying, active, draining, retired)

Inference Request:
├── request_id
├── model_id
├── tenant_id (team/service)
├── input_tokens (prompt)
├── generated_tokens (output)
├── latency_ms {queue_time, prefill_time, decode_time, total}
├── gpu_node_id
└── metadata (use_case, experiment_id)

Tenant Quota:
├── tenant_id
├── rate_limit (requests/sec)
├── token_budget (tokens/day)
├── priority (high, medium, low)
├── model_access (which models allowed)
└── current_usage {requests_today, tokens_today}
```

## High-Level Design

```
┌──────────────┐     ┌──────────────────────────────────────────────────────────┐
│  Client      │────▶│              LLM Serving Platform                         │
│  Services    │     │                                                           │
└──────────────┘     │  ┌─────────────┐     ┌──────────────────────────────┐   │
                     │  │   Router /   │────▶│     Request Queue            │   │
                     │  │   Gateway    │     │   (Priority + Fair-share)    │   │
                     │  └─────────────┘     └──────────────┬───────────────┘   │
                     │                                      │                    │
                     │  ┌───────────────────────────────────▼──────────────────┐│
                     │  │              Inference Engine (vLLM / TensorRT-LLM)   ││
                     │  │                                                       ││
                     │  │  ┌─────────┐  ┌─────────┐  ┌─────────┐            ││
                     │  │  │GPU Node │  │GPU Node │  │GPU Node │   ...       ││
                     │  │  │(8×H100) │  │(8×H100) │  │(8×H100) │            ││
                     │  │  │         │  │         │  │         │            ││
                     │  │  │Model A  │  │Model A  │  │Model B  │            ││
                     │  │  │(TP=8)   │  │(TP=8)   │  │(TP=4)   │            ││
                     │  │  └─────────┘  └─────────┘  └─────────┘            ││
                     │  └──────────────────────────────────────────────────────┘│
                     │                                                           │
                     │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
                     │  │   KV Cache   │  │   Prompt     │  │   Model      │   │
                     │  │   Manager    │  │   Cache      │  │   Registry   │   │
                     │  └──────────────┘  └──────────────┘  └──────────────┘   │
                     └──────────────────────────────────────────────────────────────┘
```

## Deep Dive

### Continuous Batching (Key Innovation)

```
PROBLEM WITH NAIVE BATCHING:
- Static batching: wait for N requests, process together
- Short requests finish early but wait for longest request in batch
- GPU idle while short requests pad to max length → low utilization

CONTINUOUS BATCHING (Iteration-Level Scheduling):
- Each decode iteration: check for new requests to add to batch
- Each iteration: check for completed requests to remove from batch
- GPU always processing maximum batch size
- No padding waste

EXAMPLE:
Iteration 1: [Request A (prompt:50, gen:0), Request B (prompt:30, gen:0)]
  → Prefill A and B
Iteration 5: [A (gen:5), B (gen:5), C (NEW, prompt:40, gen:0)]
  → C joins mid-batch, prefill C while generating A, B
Iteration 20: [A (gen:20, DONE), B (gen:20), C (gen:15)]
  → A completes, removed. Slot available for Request D.
Iteration 21: [B (gen:21), C (gen:16), D (NEW)]
  → D joins immediately in A's slot

RESULT: 
- GPU utilization: 70% (static) → 95% (continuous)
- Throughput: 2-3x improvement
- Latency: Reduced waiting time for new requests
```

### KV Cache Management

```
PROBLEM: 
70B model, sequence length 4096, FP16
KV cache per layer: 2 × 4096 × 8192 × 2 bytes = 128MB
80 layers: 80 × 128MB = 10GB PER REQUEST
Batch of 32: 320GB (exceeds single GPU memory!)

SOLUTION: PagedAttention (vLLM)

KEY INSIGHT: KV cache doesn't need contiguous memory

PAGED ATTENTION:
- Divide KV cache into fixed-size blocks (pages)
- Each page holds KV for 16 tokens
- Pages allocated on-demand as tokens are generated
- Pages can be non-contiguous in GPU memory (like virtual memory)
- Pages can be shared (for common prefixes)

MEMORY EFFICIENCY:
- No pre-allocation of max_seq_len per request
- No internal fragmentation
- Shared prefix pages: if 100 requests share same system prompt
  → 1 copy of system prompt KV cache, shared across all 100

PREFIX CACHING:
- Common system prompts → cache their KV values
- First request: compute KV for system prompt (prefill)
- Subsequent requests with same prefix: skip prefill, reuse cached KV
- Saves 50-80% of prefill compute for templated prompts

KV CACHE EVICTION:
- When GPU memory full: evict least recently used KV pages
- Or: offload to CPU memory (swap), bring back when needed
- Or: recompute (drop old KV, recompute from prompt if needed)
```

### Model Parallelism Strategies

```
FOR LARGE MODELS (70B+):

TENSOR PARALLELISM (TP):
- Split each layer's weight matrices across GPUs
- Each GPU computes partial result, all-reduce to combine
- Requires high-bandwidth interconnect (NVLink: 900 GB/s)
- Latency-optimal for serving (all GPUs work on same request)
- Typical: TP=8 for 70B model on 8×H100

PIPELINE PARALLELISM (PP):
- Different layers on different GPUs
- GPU 1: layers 0-19, GPU 2: layers 20-39, etc.
- Micro-batching to keep all GPUs busy
- Good for throughput, adds latency (pipeline depth)
- Useful when NVLink not available (cross-node)

EXPERT PARALLELISM (EP) - for MoE models:
- Different experts on different GPUs
- Router sends tokens to relevant expert GPU
- Efficient for sparse models (only 2/8 experts active per token)

TYPICAL DEPLOYMENT:
- 7B model: 1 GPU, no parallelism
- 13B model: 2 GPUs, TP=2
- 70B model: 8 GPUs, TP=8 (single node)
- 175B model: 16+ GPUs, TP=8 + PP=2 (multi-node)
```

### Multi-Tenant Scheduling

```
CHALLENGE: Multiple teams share GPU cluster with different priorities

SCHEDULING POLICY:
├── Priority queues: P0 (production) > P1 (interactive) > P2 (batch)
├── Fair-share: Each tenant gets guaranteed minimum throughput
├── Preemption: P0 can preempt P2 requests (gracefully)
└── Quota enforcement: Per-tenant rate limits and daily token budgets

REQUEST ROUTING:
1. Authenticate tenant, check quota
2. Select model (based on request or default)
3. Find GPU nodes serving that model
4. Route to node with shortest queue (least-loaded)
5. If all nodes overloaded: queue with backpressure signal
6. If queue too deep: reject with 429 + retry-after

AUTOSCALING:
- Monitor: GPU utilization, queue depth, latency
- Scale up: If queue depth > threshold for 5 minutes
- Scale down: If GPU utilization < 50% for 15 minutes
- Model-specific scaling: Popular models get more replicas
- Cost optimization: Use spot instances for P2 (batch) traffic

FISERV EXAMPLE:
- Multi-model orchestration platform
- H100 GPUs on EKS with NVIDIA device plugin
- vLLM as inference engine with continuous batching
- Priority-based routing: real-time underwriting (P0) vs. document processing (P2)
- Auto-scaling based on queue depth with 2-minute reaction time
```

### Prompt Caching

```
OBSERVATION: Many requests share common prefixes (system prompts, few-shot examples)

PROMPT CACHE ARCHITECTURE:
├── Hash prompt text → cache key
├── Store: KV cache values for cached prompts
├── Lookup: Before prefill, check if prefix is cached
├── Hit: Skip prefill for cached portion (huge latency win)
└── Miss: Compute normally, cache for future requests

CACHE EVICTION: LRU by last access time
CACHE SIZE: Typically 20-40% of GPU memory dedicated to prompt cache

IMPACT:
- System prompt (500 tokens): saves 200ms per request
- Few-shot examples (2000 tokens): saves 800ms per request
- For template-based workloads: 60-80% prefill time saved
```

## Tools & Frameworks (with trade-offs)

```
INFERENCE ENGINE: vLLM (PagedAttention, continuous batching) | TensorRT-LLM (NVIDIA,
                  fastest on NVIDIA) | TGI (HuggingFace) | SGLang (fast, RadixAttention
                  prompt cache) | LMDeploy.
SERVING/ROUTER:   Triton Inference Server | KServe / Ray Serve (autoscaling, multi-model).
ORCHESTRATION:    Kubernetes + NVIDIA device plugin; Ray for distributed.
PARALLELISM:      tensor parallel (split layer across GPUs) + pipeline parallel
                  (split layers across nodes) for models bigger than one GPU.
KV-CACHE/QUANT:   PagedAttention (vLLM); quantization (FP8/INT8/AWQ/GPTQ) for memory.
GATEWAY:          token-based rate limit, priority routing, semantic cache (GPTCache).
```

| Decision | Option A | Option B | When to pick |
|----------|----------|----------|--------------|
| Engine | vLLM | TensorRT-LLM | vLLM for flexibility/throughput; TRT-LLM for max NVIDIA perf |
| Batching | Continuous (in-flight) | Static | Continuous — 5-10x throughput for variable-length gen |
| Big models | Tensor parallel | Pipeline parallel | TP within node (NVLink); PP across nodes |
| Memory | PagedAttention + quant | FP16 full | Quantize (FP8/AWQ) to fit more concurrent requests |
| Autoscale | Queue-depth based | GPU-util based | Queue depth reacts faster for bursty LLM traffic |
| Cache | Prefix/KV cache | None | Cache shared system prompts/few-shot for big latency win |

## Scaling

- **Horizontal:** Add GPU nodes, distribute model replicas
- **Model scaling:** Auto-scale replicas based on queue depth
- **Multi-model:** Pack smaller models on same GPU (model multiplexing)
- **Multi-region:** Deploy models in regions closest to users

## Reliability

- **GPU failure:** Request retried on another node (idempotent for non-streaming)
- **Model corruption:** Reload from model registry (checksummed artifacts)
- **Overload:** Priority-based shedding (P2 first), graceful 429 responses
- **OOM:** KV cache eviction, request preemption, queue management
- **Fallback:** Smaller model fallback when primary unavailable

## Security

- Prompt injection detection (input validation)
- Output filtering (safety classifiers on generated text)
- Tenant isolation (no cross-tenant data leakage through KV cache)
- Model access control (which tenants can use which models)
- Audit logging for compliance (all prompts and completions stored)
- PII detection and redaction in logs

## Tradeoffs

| Decision | Tradeoff |
|----------|----------|
| Continuous batching | Implementation complexity for GPU utilization |
| Tensor parallelism | NVLink requirement for latency |
| PagedAttention | Memory management complexity for efficiency |
| Prompt caching | Memory for latency |
| Multi-tenant sharing | Isolation for cost efficiency |
| Quantization (INT8/FP8) | Quality for throughput and cost |

## Follow-up Questions

1. "How would you implement model A/B testing without doubling GPU cost?"
2. "How would you handle a model that's too large for a single node?"
3. "How would you implement speculative decoding to reduce latency?"
4. "How would you handle long-context requests (100K+ tokens)?"
5. "How would you implement cost attribution (chargebacks) per team?"
