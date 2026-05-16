
# Layer 7: Application & Business Logic — Complete Deep Dive
## "What are we actually building and why?"

> **Why this layer matters:** Layers 1-6 are TOOLS. Layer 7 is the REASON
> those tools exist. A perfect GPU kernel is worthless if it solves the
> wrong problem. A zero-copy pipeline is pointless if the architecture
> doesn't match the business need. Layer 7 is where systems engineering
> meets business impact — and it's what interviewers care about most
> when they ask "Tell me about a system you built."
>
> **The core insight:** Every technical decision at Layers 1-6 must be
> JUSTIFIED by a Layer 7 requirement. "Why shared memory?" → because
> the fraud hot path has a 10-30 ms budget. "Why GPU FAISS?" → because
> 35% of voice queries need semantic search. "Why prefix caching?" →
> because every LLM request shares the same system prompt. The best
> systems engineers reason top-down from business needs, not bottom-up
> from cool technology.

---

# SECTION 1: PAYMENT FRAUD DETECTION (CapitalOne / Fiserv)

---

## 1.1 The Business Context

### What the system does
Every credit card transaction in the world goes through a fraud check
before approval. The issuing bank (CapitalOne) must decide in real time:
**approve, decline, or challenge** — while the customer is standing at
the checkout counter or clicking "Buy Now."

### The numbers that matter
```
Transaction volume:     ~10 million/day (CapitalOne scale)
Fraud rate:             ~0.1-0.5% of transactions
False positive rate:    ~30-50% of flagged transactions are legitimate
Each false positive:    ~$50 customer lifetime value erosion
Each missed fraud:      ~$500-5,000 direct loss + regulatory risk
Authorization window:   ~50-200 ms (CapitalOne's total internal budget)
Fraud scoring budget:   ~10-30 ms within that window
```

### The business tension
```
Too aggressive (block too much):
  → legitimate customers blocked → lost revenue → customer churn
  → "I'm switching banks, they always decline my card"

Too lenient (approve too much):
  → fraud losses → regulatory scrutiny → fines
  → "My card was used fraudulently and the bank didn't catch it"

The sweet spot:
  → catch real fraud (high recall)
  → don't block legitimate customers (high precision)
  → do it in 10-30 ms (real-time)
  → explain every decision (regulatory compliance)
```

---

## 1.2 The Three-Path Architecture

### Why three paths, not one

```
Path 1 (Hot): "Should we approve?"
  → EVERY transaction, 10-30 ms
  → binary decision, no explanation needed
  → speed matters most

Path 2 (Warm): "Was the flag correct?"
  → ONLY flagged transactions (1-5%), 2-5 sec
  → reduce false positives, generate explanation
  → intelligence matters most

Path 3 (Cold): "What do we do about confirmed fraud?"
  → ONLY confirmed fraud (0.1-0.5%), 5-30 sec
  → investigate, generate case packet, take action
  → thoroughness matters most
```

Different questions need different tools at different speeds.

---

### HOT PATH (Tier 0) — "Should we approve?"

**Latency:** 10-30 ms p99
**Volume:** 100% of transactions
**Models:** XGBoost (CPU) ∥ transformer classifier (GPU) + rules
**LLM:** No
**Output:** float score → Go / No-Go / 3DS challenge

```
Request arrives from Visa
    ↓
[Per-core worker, isolated CPU, NUMA-aligned]
    ↓
Feature extraction:
  ├── Request fields (amount, merchant, MCC, country)     ~0 ms
  ├── Hot counters from Redis/MemoryDB                    ~3-8 ms
  ├── Velocity windows (local shard state)                ~0.1 ms
  ├── Account/device/merchant status (cached)             ~0.5 ms
  └── Precomputed embeddings (cached from offline LLM)    ~0.5 ms
    ↓
Parallel scoring (CPU ∥ GPU):
  ├── [CPU] XGBoost C API                                 ~0.3-1 ms
  ├── [CPU] Rules engine                                  ~0.1 ms
  └── [GPU] Transformer classifier (CUDA Graph)           ~2-5 ms
       └── Embedding → FAISS GPU search (zero D2H)        ~0.7 ms
    ↓
Weighted combination:
  final_score = w1*xgb + w2*transformer + w3*rules + w4*velocity
    ↓
Decision:
  score > 0.95 → HARD DECLINE (immediate)
  score 0.65-0.95 → 3DS CHALLENGE (customer verifies)
  score 0.40-0.65 → APPROVE + FLAG (post-auth review)
  score < 0.40 → APPROVE (clean)
    ↓
Publish event (Arrow IPC, shared memory → warm/cold/audit)
```

**Technical optimizations (Layers 1-6 justified by Layer 7):**

| L7 need | Technical solution | Layer |
|---|---|---|
| 10-30 ms budget | In-process models, no network hops | L6 |
| No serialization waste | Shared memory arena + SPSC rings | L5 |
| GPU utilization | CUDA Graphs + micro-batching | L4 |
| No scheduler jitter | isolcpus + CPU Manager static | L3/L1 |
| Fast feature access | Redis + local cache, NUMA-aligned | L2/L3 |

---

### WARM PATH (Tier 1) — "Was the flag correct?"

**Latency:** 2-5 sec p99
**Volume:** 1-5% of transactions (flagged only)
**Models:** Fine-tuned 7B LLM
**Output:** JSON: explanation + confidence + overturn/confirm

```
Flagged event from hot path (Arrow IPC, shared memory)
    ↓
Context assembly (Arrow zero-copy):
  ├── Transaction details (from hot path event)
  ├── Account history 30 days (pre-materialized Arrow table, mmap)
  ├── Merchant risk profile (pre-materialized Arrow table, mmap)
  ├── Device fingerprint history (pre-materialized Arrow table, mmap)
  └── System prompt (cached — prefix-cached by LLM server)
    ↓
Build token-efficient prompt from Arrow columns
    ↓
Send to co-located TensorRT-LLM / vLLM (Unix Domain Socket)
    ↓
LLM generates structured explanation:
  {
    "confidence_fraud": 0.25,
    "explanation": "While amount is anomalous, cardholder has
      prior Expedia history and VPN usage pattern...",
    "recommendation": "OVERTURN",
    "evidence_tags": ["amount_anomaly", "known_merchant", "vpn_pattern"]
  }
    ↓
Decision:
  confidence > threshold → CONFIRM FRAUD → cold path
  confidence < threshold → OVERTURN → release hold, notify customer
```

**Why warm path exists (business justification):**
```
Without warm path:
  300,000 flagged/day × 40% false positive = 120,000 wrongly blocked
  120,000 × $50 customer value erosion = $6M/day

With warm path (70% false positive recovery):
  120,000 × 70% recovered × $50 = $4.2M/day saved
  Annual impact: ~$1.5 billion
```

**When warm path runs relative to 3DS:**
```
Hot path flags → responds "3DS REQUIRED" to Visa (~10 ms)
Customer gets 3DS challenge on phone
Warm path runs DURING 3DS (2-5 sec, while customer takes 10-30 sec)
By the time customer responds, warm path is done
Final decision uses BOTH: 3DS result + warm path analysis
```

---

### COLD PATH (Tier 2) — "What do we do about this?"

**Latency:** 5-30 sec p99
**Volume:** 0.1-0.5% of transactions (confirmed/uncertain fraud)
**Models:** 7B-13B LLM with multi-turn + tool calling
**Output:** Case packet + recommended actions

```
Confirmed fraud from warm path
    ↓
Deep context assembly:
  ├── Full transaction history (60-90 days)
  ├── Account lifecycle events
  ├── Merchant network graph (shared devices, addresses)
  ├── Device/IP reputation data
  ├── Prior fraud cases for related entities
  └── Regulatory requirements (SAR thresholds)
    ↓
LLM investigation (multi-turn, tool calling):
  ├── query_transaction_db(account, date_range)
  ├── check_sanctions_list(entity)
  ├── get_merchant_risk_score(merchant_id)
  └── check_dispute_history(account)
    ↓
Output: case packet
  {
    "case_id": "FRD-2026-0429-7842",
    "severity": "HIGH",
    "fraud_type": "ACCOUNT_TAKEOVER_CNP",
    "summary": "Account takeover via compromised device...",
    "recommended_actions": [
      {"action": "BLOCK_CARD", "priority": "IMMEDIATE"},
      {"action": "NOTIFY_CUSTOMER", "priority": "IMMEDIATE"},
      {"action": "FILE_SAR", "priority": "24_HOURS"},
      {"action": "ESCALATE_TO_FRAUD_RING_TEAM", "priority": "HIGH"}
    ]
  }
    ↓
Action execution + analyst review + feedback loop
```

**Regulatory context:**
- **BSA/AML:** Suspicious Activity Reports (SARs) required within 24 hours
  for suspected fraud exceeding $5,000
- **Reg E / Zero Liability:** customer not liable for unauthorized charges
- **PCI DSS:** card data must be tokenized/encrypted in all logs
- Regulators examine fraud detection effectiveness during audits

---

## 1.3 The Payment Flow (End-to-End)

```
T+0 ms      Customer clicks "Book Now" on Expedia
T+200 ms    Expedia → Fiserv → Visa → CapitalOne
T+205 ms    Hot path starts (10-30 ms)
T+220 ms    Decision: APPROVE / 3DS / DECLINE
T+220 ms    Response → Visa → Fiserv → Expedia

IF 3DS:
T+220 ms    Visa sends "3DS required" to Expedia
T+500 ms    Customer sees "Verify in your CapitalOne app"
T+220 ms    Warm path starts (runs DURING 3DS)
T+2 sec     Warm path complete (before customer responds)
T+15 sec    Customer taps "Yes, this was me"
T+15 sec    Final decision: APPROVE (warm path overturned) or DECLINE

IF CONFIRMED FRAUD:
T+2 sec     Cold path starts
T+5 sec     Card blocked, customer notified
T+15 sec    Case packet generated
T+24 hrs    SAR filed if required
T+weeks     Feedback → model retraining
```

---

# SECTION 2: CONVERSATIONAL AI (Siri-like)

---

## 2.1 The Business Context

### What the system does
Process natural language voice queries from hundreds of millions of
users and return relevant, accurate, fast responses — spanning factual
questions, local search, device actions, media playback, and more.

### The numbers that matter
```
Active users:           ~500M+ devices with Siri
Queries per day:        hundreds of millions
Cloud processing budget: ~250-300 ms
User-perceived target:  < 800 ms - 1 sec (to feel conversational)
Query types:            factual, action, search, media, multi-turn
```

---

## 2.2 The Multi-Stage Pipeline

```
Edge Device
    ↓ Encrypted streaming gRPC (session-affine routing)
Cloud Host (bare metal)
┌──────────────────────────────────────────────┐
│ Stage A: Streaming Ingress         ~5-10 ms  │
│    ↓ shared memory (zero-copy)               │
│ Stage B: ASR (Conformer/RNN-T)    ~100-150 ms│
│    ↓ shared memory (zero-copy)               │
│ Stage C: NLU + Search/Ranking      ~12 ms    │
│    ├── Multi-task BERT (intent+NER+embed) 8ms│
│    ├── BM25 ∥ FAISS GPU search    ~8ms ∥ 0.7ms│
│    └── GPU-batched ranking         ~0.8 ms   │
│    ↓ shared memory (zero-copy)               │
│ Stage D: Response Orchestration    ~5-15 ms  │
│    ├── Template selection + slot filling      │
│    ├── Multi-turn context management          │
│    └── Tool/API invocation                    │
│    ↓ shared memory (zero-copy)               │
│ Stage E: TTS                       ~50-80 ms │
└──────────────────────────────────────────────┘
    ↓ Streaming gRPC response
Edge Device
```

### The two major upgrades during my tenure
```
Upgrade 1: NLU model migration
  Statistical NLP (MaxEnt/CRF) → Transformer encoder (BERT-class)
  Intent accuracy: 78% → 91% (+13 points)
  Entity F1: 72% → 87% (+15 points)
  Latency: 3 ms → 8 ms (absorbed by infrastructure savings)

Upgrade 2: Search architecture migration
  BM25 keyword-only → Hybrid (BM25 + FAISS semantic)
  NDCG@5: 0.62 → 0.78 (+26%)
  Semantic query coverage: 0% → 35% of queries benefit
  Search p99: 45 ms → 22 ms (GPU acceleration)
```

### Why hybrid search matters for voice
Voice queries are naturally conversational and fuzzy:
```
Typed: "iPhone Focus mode setup"         → BM25 works fine
Voice: "How do I set up that thing       → BM25 fails completely
        that silences my phone?"           FAISS matches semantically
```
~35% of voice queries benefited from semantic search.

---

## 2.3 Pre-LLM Response Generation

In 2022-2024, Siri did NOT use a generative LLM for responses.
It used five mechanisms:

```
1. Template + slot filling (~70% of responses):
   "It's currently {temp} degrees and {condition} in {location}."
   → "It's currently 62 degrees and cloudy in Seattle."

2. Canned responses (fallback):
   "Here's what I found on the web."
   "I'm not sure I understand."

3. Direct data presentation (cards/UI):
   Weather card, Maps directions, Photos grid

4. State machine dialogue (multi-step):
   "Who would you like to message?" → "What should I say?" → "Send?"

5. Knowledge snippet extraction (factual):
   Wikipedia/Wolfram Alpha → extract fact → template
```

Every word Siri spoke was either PRE-WRITTEN by a human (template)
or EXTRACTED from a data source (snippet). No generation.

---

# SECTION 3: SEARCH & RECOMMENDATION (LinkedIn-like)

---

## 3.1 The Business Context

### What the system does
Rank and recommend content (posts, jobs, people, ads) for 1.3 billion
members, personalized to each member's professional context, interests,
and engagement history.

### The numbers that matter
```
Members:            1.3 billion
Feed impressions:   billions per day
Latency target:     < 200 ms end-to-end
Candidate pool:     millions of posts per member
Final results:      10-20 items shown
```

---

## 3.2 LinkedIn's Three-Part Rebuild (2024-2026)

### A. Unified Retrieval (5 systems → 1)
```
BEFORE: 5 separate retrieval systems
  Chronological, trending, collaborative filtering,
  industry-specific, embedding-based
  Each with own infrastructure, own team, own optimization

AFTER: Single unified dual-encoder pipeline
  LLM-generated embeddings for members and posts
  One index, one retrieval call
  Retrieval in < 50 ms
  15% improvement in Recall@10
  Cold-start solved (LLM understands content from text alone)
```

### B. Feed SR (Sequential Recommender)
```
BEFORE: DCNv2 pointwise ranker (CPU, Java)
  Scores each candidate INDEPENDENTLY
  Cannot see member's engagement trajectory
  "Feature factory" of 30+ hand-built models

AFTER: Transformer sequential ranker
  Models 1,000 past interactions as a sequence
  Understands engagement TRAJECTORY, not just individual posts
  80x forward-pass speedup via shared-context batching
  Custom Flash Attention kernel (2x over SDPA)
  +2.10% time spent (massive at LinkedIn scale)
```

### C. LiNR (GPU Neural Retrieval)
```
BEFORE: CPU-based ANN indexes (HNSW/IVF)
  Approximate search → imperfect recall
  Post-filtering reduces quality

AFTER: GPU-based exhaustive search
  Entire index on GPU
  Matrix multiplication for scoring
  Pre-filtering THEN exhaustive search → 100% recall within filter
  Live-updated index (minutes, not hours)
  +3% daily active users
```

---

## 3.3 How LinkedIn Maps to Your Experience

| LinkedIn | Your equivalent | Bridge |
|---|---|---|
| Unified retrieval | Hybrid BM25 + FAISS | Same unification philosophy |
| LLM embeddings | Transformer query embeddings | Same embedding-based retrieval |
| LiNR GPU search | FAISS GPU / custom CUDA kernel | Same GPU-powered retrieval |
| Shared-context batching (80x) | CUDA Graphs / micro-batching | Same "compute invariant once" |
| Custom Flash Attention | Metal command buffer batching | Same kernel-level optimization |
| Feed SR sequential model | Transaction sequence scoring | Same "history as sequence" |
| Pre-filtering on GPU | Feature-gated scoring | Same "filter first, score survivors" |

---

# SECTION 4: SECURITY TELEMETRY (Broadcom/Symantec)

---

## 4.1 The Business Context

### What the system does
Inspect, classify, and enforce policies on enterprise data flows —
email attachments, web uploads, cloud storage, network traffic — to
prevent data loss, detect threats, and ensure compliance.

### The numbers that matter
```
Events per day:     billions (across enterprise endpoints + cloud)
Inspection latency: ~10-50 ms per content item
False positive rate: must be low (every false positive = user friction)
Compliance:         PCI, HIPAA, SOX, GDPR — penalties for failures
```

---

## 4.2 DLP Content Inspection Pipeline

```
Content arrives (email attachment, web upload, file share)
    ↓
[Content extraction] (C++, streaming parsers)
  Parse PDF, Office docs, archives, images
  Extract text, metadata, embedded objects
    ↓
[Pattern matching] (C++, compiled automata)
  Regex engines, Aho-Corasick
  Credit card (Luhn), SSN, PII detection
  Keyword proximity matching
    ↓
[ML classification] (lightweight models)
  Document type classification
  Sensitivity scoring
  False positive reduction
    ↓
[Policy evaluation]
  Match against DLP policies
  Generate "explainability artifacts" (which rule triggered, evidence)
    ↓
[Action] Block / Quarantine / Allow / Log
```

### My contributions (2015-2019)
```
1. High-throughput scanning engine (C++):
   Streaming content inspection without full buffering
   Ring buffers, memory pooling, pipeline parallelism
   30-70% throughput improvement

2. ML-assisted classification:
   Lightweight classifiers to reduce false positives
   Integrated into C++ scanning pipeline
   Feature extraction + threshold tuning

3. Multi-tenant backpressure:
   Per-tenant quotas and rate limits
   Bounded queues with 429 semantics
   Audit logging for compliance
```

---

## 4.3 CASB Telemetry Ingestion

```
Cloud activity events (identity, access, SaaS usage)
    ↓
[Ingestion gateway] (C++, high-throughput)
  Normalization, enrichment, compression
  Backpressure handling for burst conditions
    ↓
[Policy enforcement]
  Real-time policy matching
  Allow/block/alert decisions
    ↓
[Analytics pipeline]
  Anomaly detection, reputation scoring
  Entity resolution across data sources
    ↓
[Storage + search]
  Indexed for investigation and compliance
```

### The Sentinel connection
```
Broadcom security gateway (2015-2019) → CapitalOne fraud gateway (2022-2026)

Same patterns:
  Backpressure + bounded queues
  Per-tenant quotas
  Policy enforcement
  Audit trails
  ML-assisted classification
  Multi-stage pipeline

Different domain, same systems engineering.
```

---

# SECTION 5: CROSS-CUTTING DESIGN PRINCIPLES

---

## 5.1 Latency Budget Allocation

Every pipeline should have an explicit latency budget per stage:

```
Fraud hot path (30 ms total):
  Feature fetch:    8 ms  (27%)
  Model scoring:    5 ms  (17%)
  GPU search:       1 ms  (3%)
  Rules + combine:  1 ms  (3%)
  Infrastructure:   2 ms  (7%)
  Headroom:        13 ms  (43%)

Siri cloud (250 ms total):
  ASR:            120 ms  (48%)
  NLU + Search:    12 ms  (5%)
  Orchestration:   10 ms  (4%)
  TTS:             60 ms  (24%)
  Infrastructure:   2 ms  (1%)
  Headroom:        46 ms  (18%)
```

### The rule
**Always allocate headroom.** If you use 100% of the budget in testing,
you will exceed it in production (traffic spikes, GC, network jitter).

---

## 5.2 Fallback and Degradation

Every critical path needs a fallback:

```
Fraud hot path:
  Transformer fails → use XGBoost + rules only (lower accuracy, still safe)
  Feature store slow → use cached features (slightly stale)
  GPU fails → CPU fallback models
  Everything fails → rule-based conservative decline

Siri:
  Semantic search fails → BM25 only
  BERT NLU fails → old statistical NLP models
  All search fails → "Here's what I found on the web"
  Cloud unreachable → on-device processing

LLM warm path:
  LLM slow → use pre-computed template explanation
  LLM fails → escalate to human analyst directly
  KV cache full → reject and retry with smaller context
```

### The principle
**Degrade gracefully, not catastrophically.** The user should get
a WORSE answer, not NO answer.

---

## 5.3 Connecting Optimization to Business Impact

Every technical optimization should map to a business metric:

| Technical optimization | Business impact |
|---|---|
| Zero-copy pipeline (-160 ms) | Created headroom for transformer upgrade → +13 pt accuracy |
| Multi-task BERT (3→1 model) | 5x less GPU memory → more replicas → higher availability |
| GPU FAISS search (+26% NDCG) | Fewer "Here's what I found on the web" → better UX |
| Prefix caching (-45 ms TTFT) | Faster warm path → quicker false positive recovery |
| 3-path architecture | Reduced false positives 70% → ~$1.5B annual savings |
| CUDA Graphs (-145 µs) | Kept scoring within SLA → no fallback to slower models |

### The interview rule
**Never present an optimization without its business impact.**
"I reduced latency by 40%" is good.
"I reduced latency by 40%, which allowed us to upgrade to a more
accurate model that improved fraud detection by 13 points" is GREAT.

---

# SECTION 6: INTERVIEW CHEAT SHEET

---

## "Tell me about a system you built"
→ Choose based on the role:
  GPU/CUDA role → Siri GPU search platform or CapitalOne CUDA stories
  Systems/infra role → Siri zero-copy pipeline or fraud 3-path architecture
  Search/ranking role → Siri hybrid search or LinkedIn-style retrieval
  ML platform role → fraud 3-path + warm path LLM integration

## "Why three paths for fraud detection?"
→ Different questions need different tools at different speeds.
  Hot path: binary decision, 10-30 ms, no LLM.
  Warm path: explain + overturn, 2-5 sec, LLM reasoning.
  Cold path: investigate + act, 5-30 sec, LLM + tools.

## "How do you decide what runs on GPU vs CPU?"
→ GPU for: parallel math (transformers, GEMM, vector search, batched ranking).
  CPU for: tree traversal (XGBoost), rules, feature lookups, orchestration.
  Run both IN PARALLEL — wall clock = max(CPU, GPU), not sum.

## "What is your most impactful optimization?"
→ "Replacing gRPC between pipeline stages with shared memory.
  It saved 160 ms of infrastructure overhead, which created the
  headroom to deploy a transformer model that improved accuracy
  by 13 points. The infrastructure optimization enabled the
  capability upgrade — that's the most impactful kind of optimization."

## "How do you measure success?"
→ Technical: p50/p99 latency, throughput, GPU utilization, cache hit rates
  Business: false positive rate, fraud detection rate, NDCG, user engagement
  Operational: error rate, fallback activation rate, deployment success rate
  Always connect technical metrics to business outcomes.

## "How does your experience map to what we do at LinkedIn?"
→ "My GPU vector search parallels LiNR. My multi-task BERT parallels
  your unified embedding approach. My shared-context reuse (BERT once
  for three tasks) parallels Feed SR's shared-context batching. My
  hybrid retrieval (BM25 + FAISS) parallels your unified retrieval
  pipeline. The principles are the same: eliminate redundant computation,
  move parallelizable work to GPU, and share invariant state."
