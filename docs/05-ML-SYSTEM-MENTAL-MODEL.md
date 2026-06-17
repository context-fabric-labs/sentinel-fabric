# THE ML + SYSTEMS MENTAL MODEL (Funnel-First)

> Companion to [05-SENTINEL-FABRIC-REVERSE-DESIGN.md](05-SENTINEL-FABRIC-REVERSE-DESIGN.md).
> That doc says *"all five systems are the same fabric."* **This** doc is the **thinking order** —
> the sequence you run in your head so you arrive at that fabric on purpose, with every
> production-grade concern baked in instead of bolted on.

---

## 0. Why the old template fails for ML systems

Your old template was **API-engineer linear** — and it's still correct for a URL shortener or a
rate limiter. But it breaks for Search / Feed / Recommendation / any ML surface, because of **where
it puts three things**:

| Old template move | Why it's wrong for ML systems |
|---|---|
| **Leads with FRs / data storage** | For ML, the *prediction objective* and the *latency budget* decide the architecture — not the CRUD surface. You can't pick a data model before you know you need a recall tier + a rank tier. |
| **HLD appears at step 5** | By then you've over-committed to APIs and entities. The **funnel IS the HLD**, and it must come *before* the data model, because the data model (feature store, ANN index, candidate store) only makes sense once the tiers exist. |
| **"ML aspects" at step 8** | ML isn't an aspect — it's the **spine**. The training loop, feature freshness, and the offline↔online split are half the system. Putting them 8th guarantees you run out of time and sound like an API engineer who "added a model." |

**The one inversion that fixes it:**

> **NFR (latency budget) → forces the tier split → everything else hangs off the tiers.**
> A 200 ms p99 over millions of items is a *mathematical impossibility* for a single heavy model.
> That impossibility is what *creates* Tier 1 (cheap recall) and Tier 2 (expensive rank). Say that
> out loud early and you've already reframed yourself as an ML+systems engineer.

---

## 1. The new ordering (run this sequence in the room)

```
   OLD (API-first, linear)              NEW (funnel-first, ML+systems)
   ─────────────────────────           ─────────────────────────────────────
   1. Functional Reqs          ┐        0. FRAME THE ML PROBLEM      ← new, 60s
   2. Non-Functional Reqs      │            (objective, query, candidate, label)
   3. API                      │        1. SLA → TIER SPLIT          ← the pivot
   4. Data Model               ├──►     2. DRAW THE 3-TIER FUNNEL    ← HLD moves up
   5. HLD                      │            (+ Sentinel + Helios + Arrow spine)
   6. LLD                      │        3. SIZE EACH TIER            (capacity per stage)
   7. Tools/Frameworks         │        4. DATA & FEATURE MODEL      (FS, embeddings, labels)
   8. ML aspects               │        5. TIER CONTRACTS / API      (Arrow schema, interfaces)
   9. Observability            │        6. LLD PER TIER              (Rust / C++ CUDA / Triton)
  10. Infra/Govern             │        7. THE TRAINING LOOP         ← new, first-class
  11. Trade-offs               ┘        8. PRODUCTION-GRADE CROSS-CUTS (obs, reliability, govern, cost)
                                        9. TRADE-OFFS
```

Everything from the old list is still there — **FRs, NFRs, API, data model, observability,
governance all survive.** They're just **reordered around the funnel** and the ML loop is promoted
from a footnote to a phase.

---

## 2. The nine phases, with what to *say* and what to *bake in*

### Phase 0 — Frame the ML problem *(60 seconds, before anything else)*
The question that reframes you instantly. Answer four things:
- **Objective:** what are we predicting/optimizing? (retrieve top-K, rank by P(engage), classify, sequence-next)
- **Query / Candidate / Label:** what's the *query*, what's a *candidate*, what's the *feedback signal* that becomes a label? (from the fabric doc's reverse-engineering drill)
- **Online vs offline:** which part is request-time (serve) and which is batch (train/index)?
- **The value model:** single objective or multi-task? (like vs comment vs share vs dwell vs hide)

> **Say:** *"Before sizing, let me frame the ML problem — the objective is ranked relevance, the
> query is the user, a candidate is a post, and my label signal is implicit engagement. That tells
> me I need retrieval + ranking, not a single classifier."*

### Phase 1 — SLA → Tier split *(the pivot; this is the senior move)*
- State the **latency budget and corpus size** together, then show the contradiction.
- Derive the tiers **as the resolution** of that contradiction.

> **Say:** *"200 ms p99 against 10M candidates means I cannot score everything with a heavy model.
> So the budget itself forces a funnel: a sub-20 ms cheap-recall tier to get to ~1000 candidates,
> then an expensive ranker on only those. That split is the architecture; everything else hangs off
> it."*

NFRs become **tier-shaping constraints**, not a checklist:
- **Latency** → number of tiers + what's GPU vs CPU.
- **Freshness** → incremental vs full index build (the offline twin).
- **Availability** → degrade modes (Helios) + isolated GPU pools.
- **Scale** → per-core sharding + fan-out math.

### Phase 2 — Draw the 3-tier funnel *(this is your HLD, and it comes early)*
Draw the canonical fabric (don't re-invent — it's [§0 of the fabric doc](05-SENTINEL-FABRIC-REVERSE-DESIGN.md)):

```
Rust data plane (ingress, zero-copy, per-core, Arrow publish)
   → TIER 1 Retrieve/Decide (recall: ANN + BM25 / rules)         cheap, deterministic
   → SENTINEL (admission: budget, priority lanes, fail-closed)   governance, not compute
   → TIER 2 Rank/Reason (precision: heavy model on Triton)       expensive, GPU
   → HELIOS (predictive schedule + degrade modes)
   → TIER 3 Policy/Act (diversity, fairness, business rules)
   → Arrow spine → logging / features / retraining
```

The **production-grade aspects are already in the picture** here — admission control, fail-closed,
degrade modes, GPU isolation are *drawn into the HLD*, not added in a later "reliability" section.
That's the whole point of baking them in.

### Phase 3 — Size each tier *(capacity, but per stage)*
- Fan-out per stage: `10M → 1000 (recall) → 200 (pre-rank) → 50 (rank) → 20 (shown)`.
- QPS per tier, and the **cost asymmetry** (Tier 2 GPU is 100–1000× Tier 1 per item — that's *why*
  Tier 1 must shrink the set).
- Index RAM sizing (vectors must fit: float32 3 KB/vec → PQ ~100–200 B/vec; this sets shard count).

### Phase 4 — Data & Feature Model *(the ML entity model)*
The old "data model" step, ML-ified. Four stores, not one:
- **Feature store** — online (Redis/Couchbase, < 5 ms co-fetch) + offline (parity for training). *(see [05b §7](05b-SYSTEM-DESIGN-6-TO-10.md))*
- **Embedding / ANN index** — the candidate vectors (FAISS), with the build pipeline.
- **Candidate/document store** — the source-of-truth rows (Espresso/Cassandra).
- **Label / feedback log** — the engagement events that close the training loop (this is *data as a
  first-class artifact* — schema, distribution, lineage, reproducibility).

### Phase 5 — Tier contracts / API *(interfaces, including the Arrow spine)*
- External API the regular way (`GET /feed`, `GET /search`).
- **Internal contracts** are the senior part: the **Arrow record-batch schema** that flows tier→tier
  (publish once, read by reference), plus the model I/O contract at the Triton boundary. Consumer-
  driven contract tests so a producer can't silently break a consumer.

### Phase 6 — LLD per tier *(now the components have a home)*
- **Tier 1 / data plane:** Rust — per-core arena, borrowed `&[u8]` views, SPSC rings, no GC.
- **Tier 1 compute:** C++/CUDA — FAISS ANN, CUDA Graphs, pinned buffers, micro-batching.
- **Tier 2:** Triton + TensorRT-LLM — FP8, inflight batching, paged KV, tensor parallel, dynamic batching.
- **Tools/frameworks** fall out naturally here (the old step 7) — you're choosing them *per tier* with a reason, not listing them.

### Phase 7 — The Training Loop *(first-class; the old model had nothing here)*
The half of an ML system the API template forgets. Walk the **data flywheel**:
```
serve → log features+outcome (Arrow spine) → generate labels → retrain offline (Spark/GPU)
   → eval (NDCG/recall offline) → shadow → canary → A/B → model registry → promote → serve
```
- **Offline↔online parity:** same feature definitions both sides (skew = silent model decay).
- **Freshness:** streaming features (Flink) vs batch (Spark); embedding refresh cadence.
- **Safe rollout:** shadow → canary → A/B traffic split; instant rollback via registry.
- **Cold start** = the fabric's graceful-degradation reflex (lean on content/graph channels that need no clicks).

> **Say:** *"The serving funnel is only half of it — the other half is the flywheel: I log every
> ranking's features and outcome on the same Arrow buffer, that becomes training data, I retrain and
> ship behind a shadow→canary→A/B gate with registry rollback. Online/offline feature parity is the
> thing I guard hardest, because skew silently rots the model."*

### Phase 8 — Production-grade cross-cuts *(baked in, stated explicitly)*
Because they were drawn into Phase 2, here you just *name the guarantees*:
- **Observability — two layers:** *system* (p99 per tier, GPU util, queue depth) **and** *model*
  (recall@k, NDCG, calibration, **feature/prediction drift**, A/B deltas). The model layer is what
  separates ML observability from API observability.
- **Reliability / graceful degradation:** Helios degrade ladder per tier (drop heavy ranker →
  GBDT → chronological), circuit breakers, isolated GPU pools, N-1 with PodDisruptionBudgets.
- **Governance / compliance / cost:** PII filtering at the Sentinel boundary, fairness quotas in
  Tier 3, **GPU $ as a first-class budget** (FP8, Helios scheduling, debt-as-dollars).

### Phase 9 — Trade-offs *(unchanged, but tier-aware)*
Frame every trade-off **against the tier it lives in**: recall vs latency (Tier 1 ANN nprobe),
precision vs cost (Tier 2 model size / distillation), freshness vs build cost (incremental vs full),
consistency vs availability (feed tolerates eventual; messaging does not).

---

## 3. The one-screen run-sheet (what to actually do in 45 min)

| Min | Phase | The single sentence that proves you're ML+systems |
|---|---|---|
| 0–3 | **0 Frame ML** | "Objective is X; query/candidate/label are A/B/C; online serve vs offline train." |
| 3–8 | **1 SLA→tiers** | "This latency over this corpus is impossible in one model, so the budget *creates* a recall tier + a rank tier." |
| 8–18 | **2 Funnel HLD** | "Rust ingress → recall → **admission** → rank → **degrade** → policy → Arrow spine." |
| 18–22 | **3 Size** | "10M→1k→200→50→20; Tier-2 GPU is 100× Tier-1, that's why Tier-1 shrinks the set." |
| 22–28 | **4 Data/Features** | "Online feature store, ANN index, candidate store, **and a label log that closes the loop.**" |
| 28–32 | **5 Contracts** | "One Arrow schema flows tier-to-tier; publish once, read by reference." |
| 32–38 | **6 LLD + 7 Loop** | "Rust/CUDA/Triton per tier; **and the flywheel: log→retrain→shadow→canary→A/B→registry.**" |
| 38–43 | **8 Cross-cuts** | "Model *and* system observability, Helios degrade ladder, GPU $ as a budget, PII at Sentinel." |
| 43–45 | **9 Trade-offs** | "Each trade-off named against its tier." |

---

## 4. Old-template → new-home map (nothing is lost)

| Old template item | Where it lives now | Promoted / changed? |
|---|---|---|
| Functional Requirements | Phase 0 + 1 | Reframed as objective + surfaces |
| Non-Functional (scale/reliability/failures) | Phase 1 (shapes tiers) + Phase 8 (guarantees) | **Promoted** — now drives architecture |
| API | Phase 5 | + internal Arrow/tier contracts |
| Data Model / Entity | Phase 4 | **ML-ified** — 4 stores incl. feature store + label log |
| HLD | Phase 2 | **Moved up** — it *is* the funnel |
| LLD | Phase 6 | Per-tier (Rust / C++ CUDA / Triton) |
| Tools & Frameworks | Phase 6 | Chosen per tier with a reason |
| **ML aspects** | Phase 0 + Phase 7 | **Promoted to the spine** (objective up front, training loop first-class) |
| Observability | Phase 8 | **Split** into system + model layers |
| Infra / deploy / governance | Phase 2 (drawn in) + Phase 8 | **Baked in**, not bolted on |
| Trade-offs | Phase 9 | Now tier-aware |

> **Bottom line:** the old model asked *"what are the requirements, then what's the API?"* The new
> model asks *"what are we predicting, what latency budget breaks the naive design, and what funnel
> resolves it?"* — then hangs requirements, data, contracts, the training loop, and the
> production-grade guarantees off that funnel. Same ingredients, ML+systems order, every reliability
> concern drawn into the HLD instead of appended after it.
