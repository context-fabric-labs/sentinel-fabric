# PART 5 — SYSTEM DESIGN, REVERSE-ENGINEERED ONTO THE SENTINEL FABRIC

> **Why this document exists.** The generic write-ups in `05a–05d` answer each design from
> scratch. That's a lot to hold in your head under pressure. This document does the opposite:
> it teaches **ONE architecture** — the three-tier, zero-copy, Sentinel-governed fabric you
> already built at **Capital One** (fraud), **Apple Siri** (conversational), and in the
> **Search Deep Dive** — and then shows that **LinkedIn Feed, Search, Recommendation,
> Notification, and Messaging are all the same machine with different nouns.**
>
> **How to use it in the room.** You still *walk the interviewer through the regular flow*
> (requirements → capacity → API → data model → HLD → deep dive). You never say "I'm pattern-
> matching." But every deep dive **converges on the same skeleton**, so you're reasoning from
> one well-understood system instead of memorizing five. The convergence is the senior signal:
> *"notice this is the same retrieve → rank → act funnel with an admission controller in front
> and a zero-copy spine underneath."*
>
> **Read this first:** [05-ML-SYSTEM-MENTAL-MODEL.md](05-ML-SYSTEM-MENTAL-MODEL.md) is the
> *thinking order* — the funnel-first sequence (SLA → tier split → funnel → data → training loop)
> that gets you to this fabric on purpose. This doc is the *five worked instances* of that model.

---

## 0. The One Pattern (memorize this once, reuse five times)

Everything you've shipped reduces to a **governed, multi-tier funnel over a zero-copy data spine.**

```
            ┌──────────────────────────────────────────────────────────────┐
            │  INGRESS — RUST DATA PLANE                                    │
            │  • per-core sharding: worker = hash(key) % N (no cross-core)  │
            │  • zero-copy parse: borrowed &[u8] views into a per-core arena│
            │  • SPSC ring buffers (lock-free), no GC → deterministic tail  │
            │  • publishes ONE Apache Arrow record batch (the spine)        │
            └───────────────────────────────┬──────────────────────────────┘
                                            │  (same physical bytes from here down)
                                            ▼
            ┌──────────────────────────────────────────────────────────────┐
            │  TIER 1 — RETRIEVE / DECIDE   (cheap, deterministic, sub-ms…ms)│
            │  C++/CUDA + CPU: FAISS ANN recall, BM25, GBDT, rules, velocity│
            │  → a candidate set (search/feed/rec) OR a fast verdict (fraud)│
            └───────────────────────────────┬──────────────────────────────┘
                                            ▼
            ┌──────────────────────────────────────────────────────────────┐
            │  SENTINEL — GOVERNANCE PROXY (admission, not compute)         │
            │  • admission control + per-tenant/-user rate & COST budget    │
            │  • priority lanes (high-value first) • deadline propagation   │
            │  • circuit breaker, FAIL-CLOSED on stale signals • PII filter │
            └───────────────────────────────┬──────────────────────────────┘
                                            ▼
            ┌──────────────────────────────────────────────────────────────┐
            │  TIER 2 — RANK / REASON       (expensive, GPU, 10s ms…s)      │
            │  Triton + TensorRT-LLM: heavy ranker / cross-encoder / LLM    │
            │  FP8, inflight batching, paged KV cache, CUDA Graphs, TP      │
            └───────────────────────────────┬──────────────────────────────┘
                                            ▼
            ┌──────────────────────────────────────────────────────────────┐
            │  HELIOS — PREDICTIVE SCHEDULER                                │
            │  • protects Tier-2 capacity • predictive autoscale            │
            │  • DEGRADE MODES under load (drop to cheaper path gracefully) │
            └───────────────────────────────┬──────────────────────────────┘
                                            ▼
            ┌──────────────────────────────────────────────────────────────┐
            │  TIER 3 — POLICY / ACT        (async, seconds)                │
            │  re-rank • diversity • fairness • business rules • ad insert  │
            │  OR agentic tool dispatch (block card, send, create case)     │
            └───────────────────────────────┬──────────────────────────────┘
                                            ▼
            ARROW SPINE → logging • analytics • feature logging • retraining
            (publish once at ingress; every downstream consumer reads by reference)
```

### 0.1 The seven reusable parts (your "alphabet")

| # | Component | What it is | Where you've shipped it |
|---|-----------|-----------|--------------------------|
| **A** | **Rust data plane** | Per-core arena, borrowed zero-copy views, SPSC rings, no-GC deterministic tail | Capital One Tier 1 ingress; Search gateway |
| **B** | **C++/CUDA compute** | FAISS ANN, GPU scoring, CUDA Graphs, pinned buffers, micro-batching | Capital One GPU scorers; Search FAISS; Siri ASR/NLU |
| **C** | **Apache Arrow spine** | Columnar, zero-copy publish/subscribe at every tier boundary | Capital One cross-tier buffer; Search candidate batches |
| **D** | **Sentinel** | Admission + cost/token budget + priority lanes + circuit breaker + PII, *in front of* any expensive backend | Capital One Tier 2/3 gateway |
| **E** | **Helios** | Predictive scheduler + degrade modes that protect the high-priority tier | Capital One Tier 3 |
| **F** | **Triton + TensorRT-LLM** | Heavy-model serving: FP8, inflight batching, paged KV, tensor parallel | Capital One 13B/70B; Siri transformers |
| **G** | **Multi-tier latency isolation** | Hot/warm/cold split with **separate GPU pools** so cheap traffic never starves expensive | All three projects |

### 0.2 The universal claim (your one-line thesis)

> **Every large-scale serving system is a recall → precision funnel: cheaply retrieve a few
> hundred candidates, expensively rank them, then apply policy — all behind an admission
> controller and over a zero-copy spine.** Fraud is `score → reason → triage`. Search/Feed/Rec
> are `retrieve → rank → re-rank`. Messaging is `route → sequence → deliver`. Notification is
> `eligibility → relevance → channel`. **Same machine. Different nouns.**

### 0.3 The Rosetta Stone (this is the whole document in one table)

| Canonical tier (Capital One) | **Search** | **Feed** | **Recommendation** | **Notification** | **Messaging** |
|---|---|---|---|---|---|
| **Ingress** (Rust data plane) | query broker, per-shard fan-out | feed assembler | request router | trigger consumer (Kafka) | WebSocket gateway (stateful) |
| **Tier 1 — Retrieve/Decide** | FAISS ANN + BM25 (recall) | fan-out index + two-tower ANN | two-tower ANN + TigerGraph 2-hop | eligibility + dedup + velocity | route + Redis-INCR sequence + persist |
| **Sentinel — admission** | per-tenant QPS, deadline, ACL | per-user req, ad budget | per-surface candidate budget | **per-user notify budget (anti-spam)** | per-user msg rate, spam, abuse |
| **Tier 2 — Rank/Reason** | light GBDT → cross-encoder | DLRM/DCN multi-task | MMoE / DCNv2 multi-task | send/hold relevance model | (light — delivery path) |
| **Helios — schedule/degrade** | typeahead vs heavy-search lanes | degrade → chronological | pre-rank budget under load | **send-time optimization** | connection backpressure |
| **Tier 3 — Policy/Act** | diversity, ACL, snippet | diversity + ad insert + integrity | diversity + fairness quotas | channel route + digest batching | offline→push, receipts |
| **Heavy serving** (Triton+TRT) | cross-encoder GPU | DLRM GPU | MMoE GPU | relevance GPU/GBDT | — |
| **Zero-copy spine** (Arrow) | candidate record batch | feed record batch | candidate record batch | event record batch | Kafka WAL + Arrow fan-out |
| **One-liner** | "fraud Tier 1 = recall, Tier 2 = re-rank" | "search + a fan-out candidate source" | "search where the query is a *user*" | "fraud admission control as a product" | "fraud Tier 1 made stateful + durable" |

> **Reverse-engineering drill:** before you design any of the five, say to yourself —
> *"What is the query? What is a candidate? What is the expensive model? What must Sentinel
> protect? What does Helios degrade to?"* Answer those five and the design writes itself.

---

## 1. Search — the reference instance (everything else is a variation of this)

Search is the cleanest mapping because the **Search Deep Dive** already *is* the fabric.

### 1.1 Regular framing (what you say first)
- **FRs:** federated search over members/posts/jobs/companies; typeahead; facets; personalized ranking.
- **NFRs:** search < 100 ms p99, typeahead < 50 ms; 1B+ docs; ~18K search QPS peak, ~35K typeahead QPS; freshness < 5 min.
- **Capacity / API / data model:** identical to [05a §2](05a-SYSTEM-DESIGN-1-TO-5.md) — don't re-derive, reference it.

### 1.2 Collapse to the fabric (what makes it senior)

```
RUST DATA PLANE (broker)                A
  hash(query_shard) fan-out, deadline propagation, scatter-gather
        │ Arrow candidate batch          C
        ▼
TIER 1 — RETRIEVE (recall, ~10–20 ms)   B
  ├── Lexical: BM25 / Galene inverted index (names, IDs, rare tokens)
  └── Semantic: two-tower query encoder → FAISS ANN (HNSW ≤100M/node,
        IVF-PQ/DiskANN at 1B) — GPU for build, CPU shards for serve
  → union + dedup → ~1000 candidates
        ▼
SENTINEL                                 D
  per-tenant QPS, ACL/visibility filter, deadline (drop slow shard →
  serve partial, mark degraded), circuit breaker on ranker
        ▼
TIER 2 — RANK (precision, top ~100–1000) F
  light GBDT/LambdaMART  →  cross-encoder on Triton+TensorRT (GPU)
        ▼
HELIOS                                   E
  isolates the 35K-QPS typeahead lane from the heavy-search lane;
  degrade = skip cross-encoder, serve GBDT order
        ▼
TIER 3 — RE-RANK / POLICY               G
  diversity (no 5 from one company), freshness boost, ACL re-check, snippet
```

**Index build = the offline twin of the fabric:** full build (Spark + **GPU embed fleet**, B)
rebuilds inverted + ANN from source → atomic blue/green swap; incremental (Kafka + Flink) layers
a small live segment on top; query = base ∪ live. *Same hot/warm split, just on the write path.*

### 1.3 One-liner to land it
> "Search is literally Capital One's Tier 1 used for **recall** instead of a verdict, Tier 2 used
> for **re-ranking** instead of decline reasoning, and the Arrow buffer carrying candidates instead
> of a transaction. The ANN index is the only genuinely new part, and that's the FAISS work from my
> search platform."

### 1.4 Interview narration (regular → converge)
"I'll start lexical because names and IDs need exact match… then I'll add semantic retrieval with a
two-tower encoder and ANN, because keyword misses meaning… now I have two candidate streams, so I
fuse them — RRF or a learned blend — and that fused set goes to a cheap ranker then an expensive
cross-encoder only on the survivors. **Notice the shape:** cheap-recall, expensive-precision, with an
admission layer enforcing deadlines and ACLs. That's the same funnel I run for fraud; here the
'decision' is a ranked list."

---

## 2. Feed — Search plus a fan-out candidate source

### 2.1 Regular framing
- **FRs:** personalized ranked feed (connections, follows, out-of-network), real-time updates, engagement actions.
- **NFRs:** feed < 200 ms p99; 100M DAU; ~35K QPS peak; new posts visible < 30 s.
- **Capacity / API / data model:** reference [05a §1](05a-SYSTEM-DESIGN-1-TO-5.md).

### 2.2 Collapse to the fabric

The **only** structural addition over Search is a **write-side fan-out** that pre-stages one
candidate source. After that, it's the identical funnel.

```
WRITE PATH (fan-out, hybrid push/pull):
  new post → Kafka → Rust fan-out workers          A
    if author.followers < 10K → push post_id into each follower's feed index (Arrow rows)
    else (celebrity)          → store once, PULL & merge at read time

READ PATH (the funnel):
  RUST FEED ASSEMBLER                               A
        │ Arrow feed batch                          C
        ▼
  TIER 1 — CANDIDATE SOURCES (recall)               B
    ├── fan-out feed index (pushed connection posts)
    ├── two-tower ANN: out-of-network posts (fresh ANN shard, rebuilt every few min)
    ├── follows (companies/topics/creators) + trending
    └── ads candidate pool (separate auction)
        ▼
  SENTINEL: per-user admission, ad-load budget, integrity pre-filter   D
        ▼
  TIER 2 — HEAVY RANKER                             F
    DLRM / Wide&Deep / DCN, multi-task heads:
    P(like), P(comment), P(share), P(dwell), P(hide) → value-model blend
    served on Triton GPU with dynamic batching
        ▼
  HELIOS: degrade = drop heavy ranker → chronological/last-good scores   E
        ▼
  TIER 3 — RE-RANK / POLICY                          G
    diversity, dedup-seen, freshness, ad insertion at fixed slots, integrity
```

### 2.3 One-liner
> "Feed = Search where one of the candidate sources is a **precomputed fan-out index**, and the
> ranker is **multi-task** because I'm balancing like/comment/share/dwell instead of one relevance
> score. Fan-out is just Capital One's async publish — Kafka workers writing Arrow rows — pointed at
> follower inboxes instead of reasoning tiers."

### 2.4 Interview narration
"Reads must be cheap, so I pre-assemble connection posts via fan-out on write — push for normal users,
pull for celebrities to avoid write amplification. But connections alone make a thin feed, so at read
time I add an out-of-network candidate source with a two-tower ANN. Now I've got a few thousand
candidates from multiple sources — **same as search** — so I run a light filter then a heavy
multi-task ranker on GPU, and finish with diversity and ad policy. The admission layer protects the
ranker and lets me degrade to chronological if the GPU tier is unhealthy."

---

## 3. Recommendation — Search where the query is a *user*

PYMK, Jobs-You-May-Like, People-Also-Viewed. The Recommendation Platform in
[05b §8](05b-SYSTEM-DESIGN-6-TO-10.md) is the fabric with a **graph candidate source** bolted on.

### 3.1 Regular framing
- **FRs:** multi-surface recs (people, jobs, content), explainable, freshness-aware.
- **NFRs:** rec < 150 ms p99; multi-stage funnel; cold-start handling.
- **The funnel is explicit here:** candidate gen (10M→1000) → pre-rank (1000→200) → rank (200→50) → re-rank (50→20).

### 3.2 Collapse to the fabric

```
RUST REQUEST ROUTER                                 A
        │ Arrow candidate batch                     C
        ▼
TIER 1 — CANDIDATE GENERATION (recall, multi-channel) B
    ├── two-tower ANN (user tower vs item tower, FAISS)        ← "query = the user"
    ├── TigerGraph 2-hop traversal (shared connections/skills) ← structural channel
    ├── collaborative (ALS/MF embeddings) + trending
    └── union of channels → ~1000
        ▼
SENTINEL: per-surface candidate budget, freshness/ACL filter   D
        ▼
TIER 2 — RANK                                       F
    pre-rank (light DNN) → MMoE / DCNv2 multi-task (GPU, Triton)
        ▼
HELIOS: under load, shrink pre-rank budget / cap experts        E
        ▼
TIER 3 — RE-RANK / POLICY                           G
    diversity, fairness quotas, dedup, position-bias correction
```

**Cold start = the fabric's graceful-degradation reflex:** no interaction history → lean on the
**content/LLM-embedding tower** and **graph** channels (which need no clicks), exactly how Tier 1
falls back when a signal is missing.

### 3.3 One-liner
> "Recommendation is Search with the **user as the query** and a **TigerGraph hop as a second
> candidate source**. The graph+vector hybrid is straight from my Search Deep Dive — vectors give
> semantic similarity, the graph gives structural proximity, and I fuse them before ranking, same as
> lexical+semantic fusion in search."

### 3.4 Interview narration
"Candidate generation can't score 10M items, so I retrieve a thousand cheaply from several channels —
a two-tower ANN where the query embedding is the user, a graph traversal for shared-connection
signals, and a trending channel for freshness. Then it's the standard precision stack: a light
pre-ranker, a heavy multi-task model on GPU, and a re-rank pass for diversity and fairness. It's the
same recall→precision funnel as feed and search; the interesting part is fusing the graph and vector
candidates."

---

## 4. Notification — Sentinel admission control *as the product*

This is the most fun reverse-engineering: in fraud, **Sentinel is plumbing**. In Notification,
**Sentinel's job — budgeted, prioritized, deduped admission — IS the feature.** Sending everything
is easy; *deciding what's worth interrupting a member for* is the system.

### 4.1 Regular framing
- **FRs:** multi-channel (push/email/in-app), relevance + volume control, send-time optimization, dedup/aggregation, user preferences.
- **NFRs:** trigger→deliverable decision low-latency; massive fan-in of source events; strict per-user rate caps.
- **Capacity / API / data model:** reference [05a §4](05a-SYSTEM-DESIGN-1-TO-5.md).

### 4.2 Collapse to the fabric

```
TRIGGER EVENTS (likes, jobs, connections, news) → Kafka
        ▼
RUST AGGREGATOR (per-user arena)                    A
    dedup, collapse ("3 people liked your post"), join user prefs — zero-copy
        │ Arrow event batch                         C
        ▼
TIER 1 — ELIGIBILITY / DECIDE (fast)                B
    hard filters (prefs, channel opt-in), velocity check, candidate notifications
        ▼
SENTINEL = THE CORE                                 D
    • per-user notification BUDGET (the token-budget analog: N per day, anti-fatigue)
    • priority lanes: a security alert preempts a "someone viewed you"
    • dedup/idempotency; fail-closed = when unsure, DON'T spam
        ▼
TIER 2 — RELEVANCE / SEND-OR-HOLD                   F
    model scores P(open)·P(positive-action) − fatigue cost → send / hold / digest
    (GBDT or small DNN on Triton)
        ▼
HELIOS = SEND-TIME OPTIMIZATION                     E
    predict the per-user moment of peak receptiveness; schedule/defer;
    degrade under load = collapse more aggressively into digests
        ▼
TIER 3 — CHANNEL ROUTER / DELIVERY                  G
    choose push vs email vs in-app → APNs/FCM/email; batch low-priority into digest
```

### 4.3 One-liner
> "Notification is my **Sentinel governance layer turned inside-out**: the per-user cost budget,
> priority lanes, dedup, and fail-closed posture that protected my GPU tiers are *exactly* the
> anti-spam, preemption, and 'when in doubt don't interrupt' logic a notification system needs.
> Helios's predictive scheduling becomes send-time optimization."

### 4.4 Interview narration
"The hard part isn't delivery — it's restraint. I aggregate raw triggers per user and dedup them, then
the central question is a budgeted admission decision: is this worth one of the member's limited
interruptions right now? That's a relevance score against a fatigue budget, with priority lanes so an
account-security alert always beats a low-value nudge, and a fail-closed default that holds rather than
spams. Then I predict the best send time and pick a channel. I've built that admission-and-budget
engine before for inference governance; here it's the product surface itself."

---

## 5. Messaging — Capital One Tier 1 made *stateful and durable*

Messaging trades ANN/ranking for **connection state, strict ordering, and zero loss.** But the hot
path is the same deterministic, per-core, zero-copy Tier-1 discipline — now holding a socket and a
durable log.

### 5.1 Regular framing
- **FRs:** 1:1 + group (≤50), read receipts, typing, in-convo search, attachments, offline delivery.
- **NFRs:** delivery < 200 ms online; 99.99%; ~36K msg/s peak; **strict per-conversation ordering; zero loss.**
- **Capacity / API / data model:** reference [05a §3](05a-SYSTEM-DESIGN-1-TO-5.md).

### 5.2 Collapse to the fabric

```
RUST CONNECTION GATEWAY (stateful)                  A
    ~30K WebSocket conns/instance, per-core ownership, presence in Redis,
    heartbeat, resume-from-last-msg-id  (per-core arena, no GC = stable tail)
        ▼
DURABLE LOG: Kafka write-ahead BEFORE deliver       C-ish (the spine = the WAL)
        ▼
TIER 1 — ROUTE / SEQUENCE / PERSIST (deterministic, the hot path)   A/B
    • server-assigned monotonic message_id  (Redis INCR per conversation)
      = Capital One's atomic sequencing, now per-conversation
    • persist to Cassandra/Scylla (partition by conversation_id, wide rows)
    • route to recipient gateway via presence lookup
        ▼
SENTINEL                                            D
    per-user send rate, spam/abuse admission, circuit breaker,
    at-least-once + client_msg_id dedup (idempotency = exactly the fraud
    "no double-processing" guarantee)
        ▼
OFFLINE PATH                                        G
    recipient offline → pending queue → APNs/FCM via the Notification fabric (§4)
        ▼
ASYNC: ES index for in-convo search (never blocks send)   C
```

**Ordering & idempotency are the fraud guarantees renamed:** "strict total order per conversation"
= Capital One's per-request monotonic sequencing; "zero double-send" = its "no double-processing,"
solved the same way (server-assigned IDs + `client_msg_id` dedup).

### 5.3 One-liner
> "Messaging is my Capital One Tier 1 hot path made **stateful** — it holds a WebSocket and writes a
> durable Kafka log before delivering. The per-core, no-GC, zero-copy discipline that gave me a
> predictable 3.8 ms fraud tail is exactly what keeps message delivery and ordering tight, and
> server-assigned monotonic IDs are the same sequencing trick that prevented double-processing."

### 5.4 Interview narration
"Two non-negotiables drive everything: strict ordering and zero loss. So I persist to a durable log
before I attempt delivery, and I assign message IDs on the server — a per-conversation atomic counter
— so all participants converge on one order regardless of arrival timing. Connections live on stateful
gateways with per-core ownership so the latency tail is predictable, and on reconnect a client resumes
from its last message ID. Idempotency is a client-supplied message ID deduped server-side. That
ordering-plus-idempotency core is the same guarantee I enforced on the fraud path; here it's wrapped in
connection state instead of a scoring ensemble."

---

## 6. The five-system cheat sheet (one screen before you walk in)

| Ask yourself… | Search | Feed | Rec | Notification | Messaging |
|---|---|---|---|---|---|
| **What is the query?** | text | the user's network | the user | a trigger event | a message |
| **What is a candidate?** | doc | post | item/person | a possible notification | recipient route |
| **Tier-1 recall tool** | BM25 + FAISS ANN | fan-out + ANN | ANN + TigerGraph | eligibility + velocity | sequence + persist |
| **Expensive Tier-2 model** | cross-encoder | DLRM multi-task | MMoE/DCN | relevance/fatigue | (delivery, light) |
| **What must Sentinel protect?** | shards/deadline/ACL | the ranker + ad load | candidate budget | **the member's attention** | ordering + anti-abuse |
| **Helios degrades to…** | GBDT order | chronological | smaller pre-rank | aggressive digests | backpressure |
| **The reused guarantee** | zero-copy candidates | async fan-out | graph+vector fusion | budgeted admission | monotonic IDs + dedup |

### 6.1 Things to say out loud (they signal staff-level)
- "This is a **recall→precision funnel**; let me size each stage's fan-out."
- "I'll put an **admission controller** in front of the expensive tier and **fail closed**."
- "I publish **once** in a columnar buffer and let ranking, logging, and retraining share the bytes."
- "I keep the hot tier **deterministic** — no GC, per-core, zero-copy — and isolate its GPU pool."
- "Under load I **degrade gracefully** to the cheaper path rather than drop the request."

### 6.2 Things to NOT do (the anti-patterns, straight from Capital One "lessons learned")
- ❌ A generative model or unbounded call **in the hot path** — keep Tier 1 deterministic.
- ❌ **Re-serializing** at every tier boundary — publish Arrow once, read by reference.
- ❌ **MPMC + locks** where per-core **SPSC** suffices — own the request end-to-end on one core.
- ❌ **JSON/protobuf in the hot path** — fixed-layout structs / borrowed views.
- ❌ One shared GPU pool for cheap + expensive traffic — **isolate** so recall never starves ranking.

---

## 7. How to drive the interview (the meta-script)

1. **Clarify + size** the regular way (FRs/NFRs/capacity). Never mention the pattern yet.
2. **Draw the funnel** as if discovering it: "let me retrieve cheaply, then rank expensively."
3. **Name the admission layer** when you hit scale/SLA pressure: "I'll protect the expensive tier."
4. **Introduce the zero-copy spine** when serialization/throughput comes up.
5. **Land the convergence** near the end — *one sentence*: "this ended up being the same
   retrieve→rank→act funnel with admission control I'd use for feed or fraud; the domain-specific part
   was X." That single observation is what reads as *system maturity*, and it's true, because you
   built it that way on purpose.

> **Bottom line:** you are not memorizing five designs. You are carrying **one** — the Sentinel
> Fabric — and renaming its parts. Search names them recall/rank; Feed adds fan-out; Rec adds a graph;
> Notification promotes Sentinel to the product; Messaging makes Tier 1 stateful. Same machine.
