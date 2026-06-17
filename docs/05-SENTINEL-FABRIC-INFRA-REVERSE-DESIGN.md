# PART 5 (INFRA) — SYSTEM DESIGN, REVERSE-ENGINEERED ONTO THE SENTINEL FABRIC

> **Companion to** [05-SENTINEL-FABRIC-REVERSE-DESIGN.md](05-SENTINEL-FABRIC-REVERSE-DESIGN.md)
> (the ML systems) and [05-ML-SYSTEM-MENTAL-MODEL.md](05-ML-SYSTEM-MENTAL-MODEL.md) (the thinking order).
>
> **What's different here.** The ML doc collapsed Search/Feed/Rec/Notification/Messaging onto a
> *recall → rank → act* funnel where the expensive tier is a **GPU model**. These five —
> **Distributed Cache, Rate Limiter, Job Scheduler, Lock Service, Pub/Sub** — are **not AI/ML
> problems.** There is no model to serve. So the question is: *does the same fabric still hold?*
>
> **Answer: yes, but with ONE substitution.** Replace "expensive GPU rank tier" with **"the
> consistency / durability tier"** (consensus, replication, fsync). Everything else — the Rust/C++
> deterministic data plane, per-core sharding, zero-copy, SPSC, admission control, degrade modes —
> transfers *unchanged*. In fact **two of these five services ARE your fabric's own components
> extracted as standalone products**: the Rate Limiter is **Sentinel**, and the Job Scheduler is
> **Helios**. That's the punchline you land in the room.

---

## THE INFRA MENTAL MODEL (Consistency-First) — say this in the first 2 minutes

> Mirror of [05-ML-SYSTEM-MENTAL-MODEL.md](05-ML-SYSTEM-MENTAL-MODEL.md). For ML the pivot was the
> **latency budget**; for infra the pivot is the **consistency requirement**. This is the **thinking
> order** — the sequence you run in your head so you arrive at the control-plane/data-plane fabric on
> purpose, with durability, coordination, and failure behavior baked in instead of bolted on.

### The one inversion that drives everything

> **Consistency requirement (CP or AP?) → forces the control-plane / data-plane split → everything
> else hangs off the two planes.**
>
> For ML the impossibility was *"can't score 10M items with a heavy model in 200 ms."* For infra the
> impossibility is *"can't pay a consensus round-trip or an fsync on every request and still be
> fast."* That impossibility is what *creates* the split: a **fast AP data plane** (serve cheaply and
> locally) in front of a **slow CP control/durability plane** (pay coordination only when correctness
> demands it). Say that out loud first and you've framed yourself as a systems engineer, not a CRUD
> engineer.

### The ordering (old API-first vs. new consistency-first)

```
   OLD (API-first, linear)              NEW (consistency-first, systems)
   ─────────────────────────           ─────────────────────────────────────────
   1. Functional Reqs          ┐        0. FRAME THE INFRA PRIMITIVE   ← new, 60s
   2. Non-Functional Reqs      │            (cheap local op? expensive coord op?)
   3. API                      │        1. CONSISTENCY → CP/AP SPLIT   ← the pivot
   4. Data Model               ├──►     2. DRAW THE TWO PLANES (HLD)   ← control vs data plane
   5. HLD                      │        3. SIZE EACH PLANE             (data-plane QPS vs CP low-vol)
   6. LLD                      │        4. DURABILITY / DATA MODEL     (log / WAL / replication)
   7. Tools/Frameworks         │        5. PLANE CONTRACTS / API       (leases, epochs, offsets)
   8. (reliability)            │        6. LLD PER PLANE               (Rust data plane / consensus CP)
   9. Observability            │        7. FAILURE & COORDINATION MODEL ← new, first-class
  10. Infra/Govern             │        8. PRODUCTION CROSS-CUTS        (Sentinel quota, obs, cost)
  11. Trade-offs               ┘        9. TRADE-OFFS + STATE THE EXCEPTION
```

Everything from the old list survives — it's just **reordered around the two planes**, and the
**failure/coordination model is promoted from a footnote to a phase** (it's half of what makes infra
hard).

### The nine phases — what to *say* and what to *bake in*

**Phase 0 — Frame the infra primitive *(60s, before anything else).*** Answer four things:
- **Cheap local op:** what does the fast path do with no coordination? (cache get, token take, log append, follower read)
- **Expensive coordination op:** what forces consensus / fsync / a replication quorum? (durable write, lock acquire, leader election)
- **CP or AP?** is the data plane allowed to act without the control plane?
- **What does the control plane own?** (membership, slot/partition map, leader election, metadata)

> **Say:** *"Before sizing, let me name the primitive: the cheap local op is X, the expensive
> coordination op is Y, and the decisive question is whether the data plane is CP or AP. That one
> answer shapes the whole design."*

**Phase 1 — Consistency → CP/AP split *(the pivot; the senior move).*** State the throughput target
and the consistency requirement *together*, then show the contradiction and resolve it into two planes.

> **Say:** *"A million ops/sec with a sub-millisecond p99 means I cannot pay consensus or fsync per
> request. So I split into a fast AP data plane that serves locally, and a CP control/durability plane
> I touch only when correctness requires it. That split is the architecture."*

NFRs become **plane-shaping constraints**, not a checklist: *consistency* → CP vs AP; *durability* →
sync vs async replication + fsync policy; *availability* → partition behavior (fail-closed vs
fail-open); *throughput* → per-core sharding + zero-copy.

**Phase 2 — Draw the two planes *(this is your HLD, and it comes early).*** Control plane (CP,
consensus, low-volume, off the hot path) on top; fast Rust/C++ AP data plane (per-core, zero-copy,
SPSC) below; the durable log/WAL as the spine. Admission (Sentinel) and degrade (Helios) are drawn
*into* the picture, not added later. *(This is [§0.2](#02-control-plane-vs-data-plane--the-master-split-for-infra) of this doc — don't re-invent it.)*

**Phase 3 — Size each plane.** Data plane: ops/sec, shard count, RAM/disk bandwidth, fan-out. Control
plane: it's *low-volume by design* (membership changes, leader elections) — say so, and keep it off the
hot path.

**Phase 4 — Durability / data model.** The "data model" step, infra-ified: the **append-only log / WAL**
(the spine), replication factor + ISR/quorum, persistence policy (fsync cadence, snapshot+log), and the
recovery/replay story.

**Phase 5 — Plane contracts / API.** External API the regular way; the senior part is the **internal
contracts** between planes — **leases/sessions** (liveness), **epochs/fencing tokens** (split-brain
safety), and **monotonic sequence/offset** (ordering + dedup, the `client_msg_id` trick reused).

**Phase 6 — LLD per plane.** Data plane = Rust/C++: per-core event loop, lock-free rings, `io_uring`/
`sendfile`, no-GC tail. Control plane = the consensus engine (Raft/ZAB/KRaft) or a managed etcd/ZK.

**Phase 7 — Failure & coordination model *(first-class, not a footnote).*** What happens on a network
partition? Which side keeps serving? **Fail-closed where safety wins, fail-open where availability
wins** — and state it as a *per-operation policy*. Exactly-once via single-sequencer + idempotency.

**Phase 8 — Production cross-cuts.** Sentinel admission (quota, backpressure, circuit-break), observability
(p99 of the local op *and* the coordination op separately), and cost (memory/disk tiering).

**Phase 9 — Trade-offs + STATE THE EXCEPTION.** Name where the cheap-AP-shortcut does *not* apply
(a lock service: consensus is the request) and recommend buying it (etcd/ZK) and architecting to need
it rarely. **Naming the limit of your own pattern is a stronger signal than forcing it everywhere.**

### One-screen run-sheet (minute-by-minute)

| Min | Phase | Out loud |
|---|---|---|
| 0–2 | 0 Frame primitive | "cheap local op = …, expensive coord op = …, CP or AP?" |
| 2–5 | 1 Consistency → split | "can't pay consensus/fsync per request → fast AP data plane + CP control plane" |
| 5–12 | 2 Draw two planes | control plane on top, Rust data plane below, log as spine |
| 12–18 | 3–4 Size + durability | shard/QPS math; log/WAL + replication + fsync policy |
| 18–28 | 5–6 Contracts + LLD | leases/epochs/offsets; Rust hot path; consensus engine |
| 28–38 | 7 Failure model | partition behavior; fail-closed vs fail-open; exactly-once |
| 38–43 | 8 Cross-cuts | Sentinel quota, split p99 observability, storage tiering |
| 43–45 | 9 Trade-offs | **state the exception + recommendation** |

> **The whole mental model in one sentence:** *Name the cheap local op and the expensive coordination
> op, decide CP or AP, split into a fast AP data plane and a CP control plane, hang durability/
> contracts/LLD off that split, make the failure model explicit, and call out the one service where
> the cheap shortcut doesn't apply.*

---

## 0. The Infra Variant of the Fabric (the one substitution)

### 0.1 The reframe: the expensive tier is *coordination*, not *compute*

In ML systems the asymmetry is **compute**: a GBDT is ~100–1000× cheaper than a cross-encoder, so you
filter cheaply then rank expensively. In **infra** systems the asymmetry is **coordination/durability**:
a local in-memory operation is ~100–1000× cheaper than a **consensus round-trip or an fsync+replication
quorum**. Same shape, different cost axis:

```
   ML FABRIC                          INFRA FABRIC
   ─────────────────                  ─────────────────────────────
   cheap recall (ANN/BM25)    ──►     cheap LOCAL op (in-mem, per-core, zero-copy)   ← Tier 1, AP
   expensive rank (GPU model) ──►     expensive COORDINATION (consensus / fsync /    ← Tier 2, CP
                                       replication quorum)                              "pay only when
                                                                                         correctness demands"
```

> **The universal infra claim:** *Every distributed infra system is a **fast deterministic data
> plane** sitting in front of a **slow strongly-consistent control/durability plane**. You serve the
> overwhelming majority of traffic from the cheap deterministic tier, and only pay the coordination
> cost (consensus, fsync, quorum) when the operation's correctness actually requires it. The art is
> making that boundary as cheap and as rare as possible.*

### 0.2 Control plane vs. data plane — the master split for infra

Almost every infra system decomposes into two planes. **This split IS the Sentinel fabric for infra:**

```
┌────────────────────────────────────────────────────────────────────────┐
│  CONTROL PLANE  (low volume, strongly consistent, CP)                    │
│  • membership, metadata, partition/slot assignment, leader election      │
│  • consensus: Raft / ZAB / KRaft  — the "expensive tier" for infra       │
│  • rarely on the request hot path; changes are infrequent but must be    │
│    linearizable                                                          │
└───────────────────────────────┬────────────────────────────────────────┘
                                │ (assignments, leases, epochs pushed down)
                                ▼
┌────────────────────────────────────────────────────────────────────────┐
│  DATA PLANE  (high volume, AP, deterministic — THE RUST/C++ FABRIC)      │
│  • per-core sharding: worker = hash(key) % N  (no cross-core)            │
│  • zero-copy: borrowed &[u8] views, io_uring/sendfile, no GC tail        │
│  • SPSC rings, lock-free; this is the Capital One Tier-1 discipline      │
│  • Tier 1 = serve the request locally & fast (cache get, token take,     │
│    log append, lock read)                                                │
│  • escalates to the control/durability tier ONLY when required           │
└────────────────────────────────────────────────────────────────────────┘
```

### 0.3 Which fabric parts transfer, and which change

| Fabric component (from ML doc)                                    | Infra status                                            | Note                                                                                                                                                                        |
| ----------------------------------------------------------------- | ------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A — Rust data plane** (per-core, zero-copy, SPSC, no-GC) | ✅**transfers 100%**                              | The reason to pick Rust/C++ over Go/Java is*the same*: deterministic tail, no GC pause, predictable p99. This is the strongest carry-over.                                |
| **B — C++/CUDA compute**                                   | ⚠️**CUDA drops**, C++ stays                     | No GPU. But C++ for `sendfile`/`io_uring`/mmap/lock-free still applies (Redis, Kafka broker).                                                                           |
| **C — Arrow spine**                                        | 🔄**becomes the durable log / WAL**               | The "publish once, read by reference" idea becomes the**append-only log** (Kafka) or **WAL** (everything). Kafka literally *is* the spine the ML fabric used. |
| **D — Sentinel (admission)**                               | ✅**transfers; IS the product for Rate Limiter**  | Quota, backpressure, fail-closed, circuit-break — central to cache overload, pub/sub quota, lock sessions.                                                                 |
| **E — Helios (schedule/degrade)**                          | ✅**transfers; IS the product for Job Scheduler** | Predictive scheduling + degrade modes = exactly a job scheduler's core.                                                                                                     |
| **F — Triton + TensorRT-LLM**                              | ❌**drops entirely**                              | No model serving.**This is the only part that fully disappears for infra.**                                                                                           |
| **G — Multi-tier latency isolation**                       | 🔄**becomes CP/AP plane isolation**               | Hot data plane isolated from the slow consensus plane, so coordination never stalls the fast path.                                                                          |

> **The one-line summary of the substitution:** *Delete the GPU tier (F). Rename the Arrow spine to
> "the durable log" (C). Rename "hot/warm/cold GPU pools" to "data plane vs. control plane" (G).
> Everything else — Rust data plane, Sentinel, Helios — is unchanged.*

### 0.4 The decisive axis for infra: **CP or AP?** (this is where exceptions live)

The single most important question for each of these five — and the one that decides whether the
fabric applies cleanly or has an exception — is **what the data plane is allowed to do without the
control plane:**

| System                      | Data-plane consistency               | Can serve during partition?                     | Fabric fit                             |
| --------------------------- | ------------------------------------ | ----------------------------------------------- | -------------------------------------- |
| **Distributed Cache** | **AP** (eventual)              | Yes — stale/miss is recoverable                | ✅ clean fit                           |
| **Rate Limiter**      | **AP** (sloppy local counters) | Yes — slightly over/under-limit is fine        | ✅ clean fit (it*is* Sentinel)       |
| **Pub/Sub (Kafka)**   | **tunable** (acks=0/1/all)     | Data plane AP-ish; metadata CP                  | ✅ clean fit (the knob*is* the tier) |
| **Job Scheduler**     | **CP control, AP workers**     | Workers run; scheduling pauses                  | ⚠️ control-plane-heavy               |
| **Lock Service (ZK)** | **CP — mandatory**            | **No** — must reject writes to stay safe | 🟥**the exception**              |

> **Reverse-engineering drill for infra:** before designing any of these, ask —
> *"What is the cheap local op? What is the expensive coordination op? Is the data plane CP or AP?
> What does the control plane own? What does Sentinel throttle, and what does Helios schedule?"*

### 0.5 The Rosetta Stone (whole doc in one table)

| Canonical fabric part                   | **Cache (Redis)**           | **Rate Limiter**          | **Job Scheduler (Airflow)**     | **Lock Service (ZK)**        | **Pub/Sub (Kafka)**       |
| --------------------------------------- | --------------------------------- | ------------------------------- | ------------------------------------- | ---------------------------------- | ------------------------------- |
| **Data plane (Rust/C++, A)**      | per-slot in-mem store, io_uring   | per-core token buckets          | worker executors                      | follower read path                 | broker log append, sendfile     |
| **Tier 1 — cheap local op**      | `GET`/`SET` from RAM hash     | take from**local** bucket | dispatch ready task                   | **local read** from follower | append to leader segment        |
| **Expensive tier = COORDINATION** | replication + AOF/RDB persist     | **global** counter sync   | metadata-DB state txn                 | **quorum write (ZAB)**       | **ISR ack (acks=all)**    |
| **Control plane (CP)**            | cluster slot map, failover epoch  | (global store membership)       | **scheduler / leader election** | **the ENTIRE service**       | **KRaft** metadata/leader |
| **Sentinel (admission, D)**       | maxmemory evict + per-client cap  | **IS the product**        | pool/slot quota, priority             | session & watch quota              | produce quota, backpressure     |
| **Helios (schedule/degrade, E)**  | eviction policy (LRU/LFU)         | degrade → local-only           | **IS the product**              | —                                 | partition rebalance throttle    |
| **Durable spine (C → log/WAL)**  | AOF / RDB snapshot                | (counter store)                 | metadata DB + WAL                     | **txn log + snapshots**      | **the log == the spine**  |
| **CP/AP**                         | AP                                | AP (sloppy)                     | CP control / AP exec                  | **CP (mandatory)**           | tunable per-write               |
| **One-liner**                     | "fabric data plane with eviction" | "Sentinel as a product"         | "Helios as a product"                 | "the consensus core, no AP escape" | "the durable spine itself"      |

---

## 1. Distributed Cache (Redis) — the fabric data plane with an eviction policy

The cleanest infra fit: a distributed cache is **exactly the Rust data plane** of the fabric, standing
alone, with persistence/replication as its (optional) expensive tier.

### 1.1 Regular framing (say this first)

- **FRs:** `GET/SET/DEL`, TTL/expiry, atomic ops (`INCR`, `CAS`), data structures (hash/set/zset), pub/sub, eviction.
- **NFRs:** `GET` < 1 ms p99; 99.99%; millions of ops/sec/node; horizontal scale; tunable durability.
- **Capacity / API / data model:** reference [05a §5 (Distributed Cache)](05a-SYSTEM-DESIGN-1-TO-5.md).

### 1.2 Collapse to the fabric

```
RUST/C++ DATA PLANE (A, B)                      ← this is ~the whole product
  • hash-slot sharding: 16,384 slots, slot = CRC16(key) % 16384
  • per-core event loop (single-threaded-per-shard, like Redis) → no locks
  • io_uring / epoll, zero-copy reads, pipelining, no GC tail
        ▼
TIER 1 — CHEAP LOCAL OP (sub-ms, AP)
  in-memory hashtable GET/SET/INCR; O(1); served entirely from RAM
        ▼
SENTINEL (D) — overload protection IS the interesting part
  • maxmemory + eviction (LRU/LFU/TTL) = admission when full
  • per-client rate / max connections / slowlog; reject (fail-closed) on pressure
  • request-cost accounting (a big MGET/Lua script costs more than a GET)
        ▼
EXPENSIVE TIER — COORDINATION / DURABILITY (only if configured)
  • async replication to replicas (AP; replica may lag)
  • persistence: AOF (append-only log, fsync policy) + RDB (point-in-time snapshot)
  • this is the "pay only when durability is required" tier
        ▼
CONTROL PLANE (CP-lite)
  • Cluster slot assignment, gossip (membership), failover via epoch/quorum-of-masters
  • NOT on the hot path — only changes on topology events
```

### 1.3 Where it maps to your stories

- **Capital One feature store** already served **200 features in 2.1 ms p99 from a Redis cluster with
  pre-joined feature groups** — that *is* this design in production. Reuse that number.
- **Helios analog = the eviction policy:** under memory pressure, the cache "degrades gracefully" by
  evicting cold keys instead of falling over — the same instinct as dropping to a cheaper tier.

### 1.4 One-liner

> "A distributed cache is my fabric's Rust data plane standing alone: per-core, hash-slot-sharded,
> zero-copy, no-GC for a predictable sub-millisecond tail. The only genuinely cache-specific parts are
> the **eviction policy** (which is just admission control on memory) and **optional persistence**
> (the durability tier I pay for only when I need it). It's AP — a stale read or a miss is recoverable
> with a database fallback, exactly like my feature store's Redis→Dynamo→default fallback ladder."

### 1.5 Exception / recommendation

- **No exception — cleanest fit of the five.** The fabric applies almost verbatim.
- **Recommendation:** keep it **AP**. Don't try to make a cache linearizable — if you need strong
  consistency, that's a database, not a cache. The cache's job is to be *fast and disposable*; lean
  into eviction + a source-of-truth fallback rather than consensus.

---

## 2. Distributed Rate Limiter — **Sentinel extracted as a standalone product**

This is the one where you say, *grinning*, "I've built this — it's the admission controller from my
inference fabric, turned into the whole product." Same move as Notification = Sentinel-as-product, but
even more literal.

### 2.1 Real-time example (lead with this)

> **Scenario:** A payment/checkout API (or LinkedIn's login/auth endpoint) during a flash event. You
> must cap each API key to **1,000 requests/minute** and each user to **5 login attempts/minute**,
> globally, across **200 gateway nodes**, adding **< 1 ms** to every request, and **failing closed on
> the auth limiter** (security) but **failing open on the soft API limiter** (availability).

That single scenario forces every interesting decision: local-vs-global accuracy, the latency budget,
and **two different fail postures** — which is the senior nuance.

### 2.2 Regular framing

- **FRs:** fixed-window / sliding-window / **token-bucket** (burst-friendly) / leaky-bucket; per-key, per-user, per-IP, tiered; distributed across N nodes.
- **NFRs:** decision < 1 ms p99; 99.99%; millions of checks/sec; accuracy vs. latency tunable.
- **Capacity / API / data model:** reference [05c §13 (Rate Limiter)](05c-SYSTEM-DESIGN-11-TO-15.md).

### 2.3 Collapse to the fabric — it's a two-tier accuracy funnel

```
RUST DATA PLANE (A) — per-core, per-key token buckets in shared memory
        ▼
TIER 1 — CHEAP LOCAL DECISION (the fast path, AP)         ← ~99% of requests decided here
  • node-local "sloppy" token bucket; atomic decrement; lock-free; sub-µs
  • each node owns a LOCAL allowance = global_limit / N (+ a burst margin)
        ▼
SENTINEL  ==  THE PRODUCT (D)
  • this tier IS Sentinel: admission decision, priority lanes (paid tier vs free),
    cost-weighted tokens (a heavy endpoint costs more tokens), circuit-break
  • FAIL POSTURE is a per-limiter policy:
      - auth/security limiter → FAIL-CLOSED (deny on uncertainty)
      - soft API limiter      → FAIL-OPEN  (allow on uncertainty; availability > precision)
        ▼
EXPENSIVE TIER — GLOBAL COUNTER SYNC (only for accuracy)  ← the "precision" stage
  • periodic async reconcile to a global store (Redis INCR / a CRDT counter)
  • corrects local drift; nodes lease/borrow allowance from the global pool
  • this is the cheap-local → accurate-global asymmetry = recall→precision in disguise
```

**The key insight to state:** *exact* global rate limiting requires a coordination round-trip on every
request (too slow). So you do the **fabric move**: decide cheaply and locally (sloppy, fast, AP), and
reconcile to a global counter asynchronously. You trade a few percent of accuracy at the window edge
for a 100× latency win — the *identical* trade as cheap-recall/expensive-rank.

### 2.4 One-liner

> "A distributed rate limiter is my **Sentinel admission layer shipped as a product**. The fast path
> is a per-core, lock-free local token bucket — sub-microsecond, AP — and global accuracy comes from
> an async reconcile to a shared counter, so I never pay a coordination round-trip on the hot path.
> The senior detail is that **fail posture is per-limiter policy**: the auth limiter fails closed
> because security beats availability, while the soft API limiter fails open because a brief
> over-admit is cheaper than a false denial. That fail-closed/fail-open judgment is exactly what I
> tuned on Sentinel for the fraud tiers."

### 2.5 Exception / recommendation

- **No exception — it IS the fabric's Sentinel.** Cleanest possible mapping.
- **Recommendation:** default to **token bucket** (handles bursts gracefully) + **local-sloppy +
  async-global**. Only go fully synchronous/global if you have a hard regulatory cap that cannot be
  exceeded even briefly — and call that out as the rare CP case.

---

## 3. Distributed Job Scheduler (Airflow) — **Helios extracted as a standalone product**

If the rate limiter is Sentinel-as-product, the job scheduler is **Helios-as-product**: predictive
scheduling, priority pools, and degrade-under-load are *exactly* what Helios did to protect your GPU
tiers. But this one is **control-plane-heavy**, which is the honest caveat.

### 3.1 Regular framing

- **FRs:** define DAGs (tasks + dependencies), schedule (cron/event/sensor), retries/backfill, priorities & pools, observe runs.
- **NFRs:** scheduling latency low; **exactly-once task dispatch** (no double-run); survive scheduler crash; scale to 100k+ tasks.
- **Capacity / API / data model:** reference [05d §16 (Job Scheduler)](05d-SYSTEM-DESIGN-16-TO-20.md).

### 3.2 Collapse to the fabric — note the plane split is pronounced here

```
CONTROL PLANE (CP — the brain, must be consistent)        ← bigger than usual for infra
  • DAG parse + dependency resolution: which tasks are READY now?
  • SCHEDULER = leader-elected (Raft/DB-lock) so only ONE entity dispatches
    → this is the exactly-once guarantee; double-dispatch = double-run = bad
  • state of every task in a metadata DB (Postgres) — the durable source of truth
        │ (ready tasks pushed to a queue)
        ▼
SENTINEL (D) — admission between brain and muscle
  • pool/slot quotas (only K concurrent tasks per pool), priority lanes,
    per-queue backpressure; reject/defer when workers saturated
        ▼
HELIOS  ==  THE PRODUCT (E)
  • this IS Helios: decide WHAT runs WHEN given finite worker capacity,
    predict load, pack work, and DEGRADE under pressure
    (shed/defer low-priority DAGs, honor SLAs of high-priority ones first)
        ▼
DATA PLANE — WORKERS (A — Rust/C++ executors, AP)
  • pull ready task from queue, execute, report status; stateless & scalable
  • a worker dying just means the task is retried — workers are disposable
        ▼
DURABLE SPINE (C → log)
  • task state transitions written to metadata DB + an event log for replay/audit
```

### 3.3 The exactly-once subtlety (this is your Capital One guarantee again)

"No double-run" is the **same guarantee as "no double-processing"** on the fraud path. Solve it the
same way: **a single authoritative sequencer.** Here it's a **leader-elected scheduler** (only one node
holds the dispatch lock) + **idempotent task execution keyed by `(dag_id, task_id, execution_date)`**
so even a duplicate dispatch is deduped at the worker — the `client_msg_id` trick, renamed.

### 3.4 One-liner

> "A job scheduler is **Helios as a product** — finite worker capacity, priority pools, predictive
> packing, and graceful degradation of low-priority work under load, which is precisely how Helios
> protected my Tier-2 GPU capacity. The honest difference from my serving systems is that it's
> **control-plane-heavy**: the scheduler must be leader-elected and consistent so I get exactly-once
> dispatch, while the workers are a cheap, disposable, AP data plane. The exactly-once property is the
> same single-sequencer trick I used to prevent double-processing in fraud, keyed by
> `(dag, task, run-date)`."

### 3.5 Exception / recommendation

- **Partial exception — the control plane is heavy.** Unlike cache/rate-limiter, the *brain*
  (scheduler + metadata) is a non-trivial CP system, not a thin slice. The data plane (workers) still
  maps cleanly to the fabric.
- **Recommendation:** keep the **scheduler stateless except for a leader lease + a consistent metadata
  DB**; never let two schedulers dispatch concurrently. Make workers **idempotent and disposable** so
  the AP data plane can scale and fail freely. Use the metadata DB as the WAL/spine; don't invent a
  second source of truth.

---

## 4. Distributed Lock Service (Zookeeper) — **the exception: consensus is the product, no AP escape**

This is the one system where you **cannot** lean on the cheap-AP-data-plane to avoid the expensive
tier. Correctness *is* the expensive tier. Be honest about it — naming the exception is itself a senior
signal.

### 4.1 Regular framing

- **FRs:** mutual-exclusion locks, leader election, ephemeral nodes (auto-release on session death), watches, sequential nodes, config metadata.
- **NFRs:** **linearizable writes** (the whole point); reads fast; survive minority failure; convergence in seconds.
- **Capacity / API / data model:** reference [05c §11 (Distributed Lock Service)](05c-SYSTEM-DESIGN-11-TO-15.md).

### 4.2 Collapse to the fabric — and where it breaks

```
CONTROL PLANE == THE ENTIRE PRODUCT (CP, mandatory)
  • consensus protocol (ZAB / Raft): leader + majority quorum
  • EVERY WRITE (acquire lock, create ephemeral node) goes through quorum
    → this is the "expensive tier", and it is NOT optional or skippable
  • linearizable: a lock is worthless if two holders can both think they own it
        ▲
        │ writes MUST pay the quorum cost
        │
DATA PLANE (the ONLY place the fabric's fast path survives)
  • READS can be served locally from any follower (fast, in-memory tree)
    → optionally stale; use sync() before read for linearizable reads
  • Rust/C++ still buys you a tight, no-GC read path + efficient watch fan-out
        ▼
SENTINEL (D) — still useful at the edges
  • session/connection admission, watch-count quota (watch storms are a real DoS),
    per-client request caps; protects the consensus core from overload
```

**Why the fabric partially breaks:** the whole value proposition of the cheap AP data plane is
"escalate to coordination only *rarely*." For a lock service, **the coordination IS the request** —
you can't acquire a lock "locally and sloppily" and reconcile later, because a sloppy lock is not a
lock. So the asymmetry collapses: writes are *always* expensive. The fabric still describes it (fast
local reads vs. expensive quorum writes), but the "make the expensive tier rare" optimization is gone.

### 4.3 The honest mapping

- **Fast path survives only for reads** (followers, local tree, watch fan-out) — Rust/C++ still wins on
  read tail latency and memory.
- **`fail-closed` is not a tunable here — it's mandatory.** On partition, the minority side **must
  reject writes** to preserve safety. This is your fraud "fail-closed on stale signals" instinct, but
  *non-negotiable* rather than a policy choice.
- **Ephemeral-node-on-session-death** = a lease/heartbeat, the same liveness mechanism as your service-
  discovery and connection-gateway designs.

### 4.4 One-liner

> "A lock service is the **honest exception** to my fabric. My usual move — serve cheaply and locally,
> pay for coordination rarely — doesn't apply, because for a lock *the coordination is the request*: a
> sloppy, eventually-consistent lock isn't a lock. So writes always pay the consensus quorum cost
> (ZAB/Raft), and the only place my fast Rust data plane survives is the **read path** from followers
> plus efficient watch fan-out. Fail-closed stops being a tunable and becomes mandatory — the minority
> side of a partition must refuse writes to stay safe. I'd flag that explicitly and **recommend not
> building this yourself** — use ZooKeeper/etcd and put your engineering into *minimizing* how often
> the hot path needs a lock at all."

### 4.5 Exception / recommendation (the important one)

- **🟥 This is THE exception.** Consensus is mandatory; there is no AP data-plane shortcut for writes.
- **Recommendation 1 — don't build it:** use **etcd (Raft)** or **ZooKeeper (ZAB)**. A hand-rolled
  consensus system is the classic way to ship a subtle, catastrophic safety bug.
- **Recommendation 2 — design *around* it:** the senior move is to **reduce lock dependence** on the
  hot path entirely — partition by key so each shard is single-writer (no lock needed), use optimistic
  concurrency / CAS, or fence with epoch tokens. Treat the lock service as a rare control-plane
  primitive, **never** as a per-request data-plane call. That keeps the *rest* of your system on the
  fast fabric and confines consensus to where it's unavoidable.

---

## 5. Distributed Pub/Sub (Kafka) — **the durable spine itself**

Beautiful closure: in the **ML fabric, the Arrow spine / durable log was a *component*.** Kafka **is
that component as a standalone product.** Designing Kafka is designing the spine the rest of the fabric
rides on.

### 5.1 Regular framing

- **FRs:** topic pub/sub, partition ordering, consumer groups, configurable retention, replay-from-offset, delivery semantics (at-least/at-most/exactly-once).
- **NFRs:** 10M+ msg/s; produce ack < 10 ms; **zero loss** (acked); 99.99%; days-to-years retention.
- **Capacity / API / data model:** reference [05b §6 (Kafka)](05b-SYSTEM-DESIGN-6-TO-10.md).

### 5.2 Collapse to the fabric

```
CONTROL PLANE (CP — KRaft / Raft metadata quorum)
  • topic/partition metadata, broker membership, partition→leader assignment,
    leader election on broker failure → linearizable, low-volume
        │
        ▼
RUST/C++ DATA PLANE (A, B) — the broker hot path
  • per-partition append-only log; sequential writes saturate disk bandwidth
  • zero-copy: sendfile() kernel→NIC (no user-space copy) — your zero-copy instinct, literally
  • OS page cache for hot reads; batching + compression; no GC tail
        ▼
TIER 1 — CHEAP LOCAL OP (fast, AP-ish)
  • append record to LEADER's local segment, assign monotonic offset → ack (acks=1)
        ▼
SENTINEL (D) — admission / quota / backpressure
  • per-client produce quota, throttle (Kafka quotas), connection caps;
    protects brokers from a runaway producer (a real outage cause)
        ▼
EXPENSIVE TIER — DURABILITY QUORUM (the acks knob == the tier dial)
  • acks=0  → no wait        (cheapest, may lose)   ← skip the expensive tier
  • acks=1  → leader only     (fast, may lose on leader crash)
  • acks=all + min.insync.replicas → ISR quorum ack (durable, slower)  ← pay the tier
  • this knob is LITERALLY "how much coordination do I pay per write" — the fabric dial exposed
        ▼
THE LOG == THE SPINE (C)
  • the partition log is the append-once, read-by-many durable spine; replay = re-read offset
```

### 5.3 The unifying observation (land this)

- **`acks=0/1/all` is the fabric's tier dial made into a config knob.** `acks=1` = stay on the cheap
  local tier; `acks=all` = pay the expensive coordination tier. The *system itself* lets the user
  choose where on the asymmetry they sit, per write. That's the whole infra-fabric thesis, productized.
- **Monotonic offset per partition** = the **same server-assigned sequencing** that gave you ordering
  and no-double-processing in Capital One and Messaging. **Idempotent producer (PID + sequence)** =
  the `client_msg_id` dedup trick. You've now used this exact mechanism in four designs — say so.

### 5.4 One-liner

> "Designing Kafka is designing **the durable spine my whole fabric rides on** — in my ML systems the
> append-once log was a *component*; here it's the product. The broker hot path is pure fabric: a
> per-partition append-only log with **zero-copy `sendfile`** and no GC, so writes saturate disk
> sequentially. The elegant part is that Kafka **exposes the fabric's tier dial as `acks`**:
> `acks=1` stays on the cheap local tier, `acks=all` pays the ISR durability quorum — the user picks
> where on the cheap-vs-coordination asymmetry they want to sit, per message. Metadata/leader election
> is the CP control plane (KRaft); the data plane is AP and fast. And the monotonic per-partition
> offset is the same server-assigned sequencing I used for fraud ordering and message dedup."

### 5.5 Exception / recommendation

- **No exception for the data plane** (clean fabric fit); the **metadata plane is CP** (KRaft) but it's
  off the hot path, so it doesn't compromise throughput.
- **Recommendation:** default **`acks=all` + `min.insync.replicas=2`** for anything that matters
  (zero-loss), and reserve `acks=1`/`acks=0` for lossy-tolerant high-throughput telemetry. Use
  **tiered storage** (hot NVMe + cold S3) so retention cost doesn't force you to under-retain — the
  "hot/warm/cold" instinct from the GPU world, applied to bytes.

---

## 6. The five-system cheat sheet + exceptions

| Ask yourself…                        | Cache                 | Rate Limiter                | Job Scheduler                    | Lock Service             | Pub/Sub                  |
| ------------------------------------- | --------------------- | --------------------------- | -------------------------------- | ------------------------ | ------------------------ |
| **Cheap local op (Tier 1)**     | RAM get/set           | local token take            | dispatch ready task              | follower read            | log append               |
| **Expensive coordination tier** | replicate + persist   | global counter sync         | metadata txn + leader            | **quorum write**   | ISR ack                  |
| **Control plane owns**          | slot map, failover    | (global counter)            | scheduler/leader                 | **everything**     | KRaft metadata           |
| **Sentinel's job**              | eviction + client cap | **the whole product** | pool/slot quota                  | session/watch quota      | produce quota            |
| **Helios's job**                | eviction policy       | degrade→local              | **the whole product**      | —                       | rebalance throttle       |
| **CP or AP?**                   | AP                    | AP (sloppy)                 | CP brain / AP workers            | **CP (mandatory)** | tunable (`acks`)       |
| **Fabric fit**                  | ✅ clean              | ✅ = Sentinel               | ⚠️ control-heavy               | 🟥**exception**    | ✅ = the spine           |
| **Reused guarantee**            | Redis fallback ladder | fail-closed/open policy     | single-sequencer (no double-run) | mandatory fail-closed    | monotonic offset + dedup |

### 6.1 The exceptions, stated plainly (and the recommendations)

1. **Lock Service (Zookeeper) — 🟥 the real exception.** The cheap-AP-data-plane optimization does not
   apply to *writes*; consensus is the request. **Recommendation: don't build it — use etcd/ZooKeeper —
   and architect to need it rarely** (partition for single-writer, CAS, epoch fencing).
2. **Job Scheduler (Airflow) — ⚠️ partial exception.** Control-plane-heavy: the scheduler must be
   leader-elected and consistent. **Recommendation: thin consistent brain (leader lease + metadata DB)
   + fat disposable AP worker fleet**; idempotent tasks for exactly-once.
3. **Cache, Rate Limiter, Pub/Sub — ✅ clean fits.** Rate Limiter *is* Sentinel; Pub/Sub *is* the
   durable spine; Cache *is* the data plane. No exceptions; the fabric applies almost verbatim.

### 6.2 Things to say out loud (staff-level signals for infra)

- "The expensive tier here isn't a model — it's **coordination**: consensus, fsync, or a replication
  quorum. I'll serve cheaply and locally and pay that cost only when correctness demands it."
- "I'll split this into a **consistent control plane** and a **fast AP data plane**, and keep
  coordination off the request hot path."
- "Rust/C++ here for the same reason as my fraud path: **no GC pause, deterministic p99**, zero-copy
  via `io_uring`/`sendfile`."
- "Let me state the **CP/AP** choice explicitly — it's the decision that drives everything else."
- "I'll make the fail posture a **policy**: fail-closed where safety wins, fail-open where availability
  wins."

### 6.3 Things to NOT do (the infra anti-patterns)

- ❌ Consensus on the **hot path** when an AP local op + async reconcile would do (rate limiter, cache).
- ❌ Re-inventing a **consensus protocol** by hand (lock service) — use etcd/ZK.
- ❌ **Two schedulers** dispatching concurrently — leader-elect, or you get double-runs.
- ❌ **Synchronous global** coordination per request when sloppy-local + reconcile meets the SLA.
- ❌ Treating a **cache as a database** — keep it AP and disposable with a source-of-truth fallback.

---

## 7. How to drive the interview (infra meta-script)

1. **Clarify + size** the regular way (FRs/NFRs/capacity). Reference the existing `05a–05d` numbers.
2. **State the CP/AP choice early** — "is the data plane allowed to act without the control plane?"
   This single decision shapes the whole design and signals maturity.
3. **Draw the plane split** — fast Rust/C++ AP **data plane** in front of a consistent **control /
   durability plane**; name the cheap local op and the expensive coordination op.
4. **Name where the fabric's own parts appear** — "this admission layer is a rate limiter is Sentinel";
   "this scheduling/degrade logic is Helios"; "this durable log is the spine my serving systems ride."
5. **Call out the exception if there is one** — for a lock service, *say* "consensus is mandatory here,
   the AP shortcut doesn't apply, and I'd use etcd rather than build it." Naming the limit of your own
   pattern is a stronger signal than pretending it fits everywhere.

> **Bottom line:** for infra, **delete the GPU model tier, rename the Arrow spine to "the durable
> log," and rename "GPU-pool isolation" to "control-plane vs data-plane."** What remains — the Rust/C++
> deterministic data plane, Sentinel admission, Helios scheduling — carries every one of these five.
> Two of them (Rate Limiter, Job Scheduler) *are* your fabric's own components shipped standalone; one
> (Pub/Sub) *is* the spine itself; one (Cache) is the bare data plane; and exactly one (Lock Service)
> is the honest exception where consensus can't be avoided. **Same machine, minus the model, plus a
> consensus core where correctness is non-negotiable.**
