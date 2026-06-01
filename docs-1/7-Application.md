
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

---
---

# SECTION 7: CAPACITY PLANNING & LOAD TESTING

---

## 7.1 Capacity Planning Framework

```
Step 1: Determine traffic shape
  Peak QPS (queries per second)
  Diurnal pattern (peak:trough ratio — typically 3-5×)
  Growth rate (month-over-month)
  Burst factor (flash sales, breaking news — up to 10×)

Step 2: Single-instance capacity
  Load test single replica to saturation
  Find: max QPS at target p99
  Record: CPU, GPU, memory at that QPS

Step 3: Calculate replicas
  Replicas = Peak_QPS / Single_Instance_QPS × Safety_Factor
  Safety factor: 1.3-1.5 (accounts for imbalance, rolling updates)
  
  Example (fraud scoring):
    Peak QPS: 15,000 transactions/sec
    Single instance: 2,000 transactions/sec at p99 < 10 ms
    Replicas: 15,000 / 2,000 × 1.4 = 11 replicas
    Plus 1 for rolling update: 12 replicas
    
Step 4: Account for failure scenarios
  Node failure: lose 1 node worth of replicas
  AZ failure: lose 33% of replicas (3 AZ setup)
  Design for: full capacity with one AZ down
  
  Final: 12 / 0.67 = 18 replicas (across 3 AZs)

Step 5: Plan for growth
  Current: 18 replicas
  6-month projection (20% growth/month): 18 × 1.2^6 = 54 replicas
  Infrastructure lead time: 3-6 months for GPU procurement
  → Order capacity NOW for 6-month projected need
```

## 7.2 Load Testing Methodology

```
Tool selection:
  HTTP/gRPC: ghz, vegeta, k6, locust
  Custom protocol: custom load generator (your C++ service)
  
Load test types:

1. BASELINE (establish normal performance)
   Constant load at expected peak for 30 minutes
   Record: p50, p99, p99.9, error rate, resource usage
   This is your reference for regression detection

2. RAMP (find saturation point)
   Start at 50% expected load, increase 10% every 2 minutes
   Find: QPS where p99 exceeds SLA
   This is your single-instance capacity number

3. SPIKE (test burst handling)
   Normal load → 5× spike for 30 seconds → back to normal
   Verify: service recovers, no crash, acceptable degradation
   Test: auto-scaling trigger time, load shedding activation

4. SOAK (find memory leaks, degradation)
   Constant high load for 24-72 hours
   Monitor: memory growth, latency drift, error rate growth
   Catches: memory leaks, connection leaks, cache staleness

5. CHAOS (test resilience)
   Normal load + random failures:
   - Kill random pods
   - Inject network latency
   - Simulate GPU OOM
   - Redis failover during load
   Verify: fallbacks activate, service degrades gracefully

Load test checklist:
  □ Use realistic request distribution (not uniform!)
  □ Include think time between requests (not back-to-back)
  □ Test with production-like feature data
  □ Run against staging environment (same hardware)
  □ Capture metrics at BOTH client and server side
  □ Test cold start: restart during load
```

## 7.3 Little's Law (The Fundamental Queuing Formula)

```
L = λ × W

L = average number of requests in system (in-flight)
λ = arrival rate (requests/second)
W = average time each request spends in system (seconds)

Example:
  λ = 1,000 req/s, W = 10 ms = 0.01 s
  L = 1,000 × 0.01 = 10 requests in-flight at any time
  
  If each request uses 1 CPU core:
  Need: 10 cores minimum (at 100% utilization)
  Practical: 10 / 0.7 = 15 cores (at 70% target utilization)

GPU version:
  λ = 1,000 req/s, W = 5 ms GPU time per request
  L = 1,000 × 0.005 = 5 concurrent GPU requests
  With batch=5: need 1 GPU
  With batch=16 (more efficient): need 1 GPU at lower utilization

Thread count:
  Optimal threads = λ × W = in-flight requests
  More threads than this: CFS cliff
  Fewer threads: underutilization (requests queue)
```

---

# SECTION 8: RATE LIMITING & BACKPRESSURE

---

## 8.1 Rate Limiting Algorithms

```
TOKEN BUCKET (most common):
  Bucket holds B tokens, refills at rate R tokens/sec
  Each request consumes 1 token
  If bucket empty → reject (429)
  
  Properties:
  - Allows burst up to B
  - Sustained rate limited to R
  - Simple, O(1) per request
  
  Parameters for fraud scoring:
    Per-merchant: R=100 req/s, B=200 (allow 2-sec burst)
    Per-card: R=5 req/s, B=10
    Global: R=20,000 req/s, B=30,000

SLIDING WINDOW (more precise):
  Count requests in sliding window of W seconds
  If count > limit → reject
  
  Implementation: Redis ZADD with timestamp scores
    ZADD key timestamp member
    ZRANGEBYSCORE key (now - window) now
    If count > limit: reject
    
  More memory than token bucket, but exact count guarantee

LEAKY BUCKET (constant output rate):
  Queue with fixed drain rate
  If queue full → reject
  Smooths burst into constant output
  
  Good for: database writes, external API calls (need constant rate)
```

## 8.2 Backpressure Propagation

```
The problem without backpressure:
  Upstream (fast) → Queue → Downstream (slow)
  Queue grows unboundedly → OOM → crash

With backpressure:
  Upstream (fast) → Bounded Queue → Downstream (slow)
  Queue full → upstream blocks or sheds → system stable

Backpressure patterns:

1. BOUNDED QUEUE (simplest):
   if (queue.size() >= max) {
       return Status::ResourceExhausted;  // 429
   }
   queue.push(request);

2. SEMAPHORE (limit concurrency):
   sem_t inflight;  // initialized to max_concurrent
   sem_wait(&inflight);  // blocks if at limit
   process(request);
   sem_post(&inflight);

3. REACTIVE (measure and react):
   Monitor downstream latency
   If latency > threshold → reduce incoming rate
   Auto-adjust sending rate based on downstream health

4. CREDIT-BASED (like TCP flow control):
   Downstream grants credits (N more requests OK)
   Upstream sends only if credits available
   Downstream replenishes credits as it drains
   
   Used in: InfiniBand, NCCL (hardware-level flow control)

Your fraud scoring system:
  Per-stage bounded queues (SPSC ring buffers)
  Ring buffer full → upstream worker spins or logs metrics
  Each stage processes at its own rate
  No unbounded queuing → predictable memory usage
```

## 8.3 Circuit Breaker Pattern

```
Protect your service from cascading failure when a dependency is down.

States:
  CLOSED (normal): requests flow through, failures counted
  OPEN (tripped): ALL requests immediately rejected (fast-fail)
  HALF-OPEN (testing): allow ONE request through to test recovery

State transitions:
  CLOSED → OPEN: when failure_count > threshold in window
  OPEN → HALF-OPEN: after cooldown period (e.g., 30 sec)
  HALF-OPEN → CLOSED: if test request succeeds
  HALF-OPEN → OPEN: if test request fails

Example: feature store circuit breaker
  Normal: fetch features from Redis (3 ms)
  Redis slow: failures accumulate → circuit OPENS
  Open: immediately return cached/default features (0.1 ms)
  After 30 sec: try ONE request to Redis (HALF-OPEN)
  If Redis recovered: close circuit, resume normal
  If still down: stay open, retry in 30 sec

Code pattern:
  class CircuitBreaker {
      enum State { CLOSED, OPEN, HALF_OPEN };
      State state = CLOSED;
      int failures = 0;
      time_point last_failure;
      
      Result call(Function f) {
          if (state == OPEN) {
              if (now() - last_failure > cooldown) state = HALF_OPEN;
              else return fallback();
          }
          try {
              auto result = f();
              if (state == HALF_OPEN) state = CLOSED;
              failures = 0;
              return result;
          } catch (...) {
              failures++;
              last_failure = now();
              if (failures > threshold) state = OPEN;
              return fallback();
          }
      }
  };
```

---

# SECTION 9: CONSISTENCY AND DATA FRESHNESS

---

## 9.1 Feature Freshness Requirements by Use Case

```
| Feature Type | Freshness | Source | Impact if Stale |
|---|---|---|---|
| Transaction velocity | < 1 sec | Streaming (Flink/Kafka) | Miss burst fraud |
| Account status (blocked) | < 5 sec | CDC from DB | Approve on blocked card |
| Device fingerprint | < 1 min | Event-driven | Miss device takeover |
| Merchant risk score | < 1 hour | Batch pipeline | Slightly worse accuracy |
| Customer embeddings | < 4 hours | Batch pipeline | Slightly worse matching |
| Model features (30-day agg) | < 24 hours | Batch pipeline | Minimal impact |

The rule: freshness requirement = inverse of attack speed
  Burst fraud (seconds): need real-time velocity
  Account takeover (minutes): need near-real-time status
  Subtle pattern shifts (days): batch features sufficient
```

## 9.2 Consistency Models for Distributed Features

```
STRONG CONSISTENCY (linearizable):
  All readers see the latest write IMMEDIATELY
  Implementation: synchronous replication, consensus (Raft/Paxos)
  Cost: higher latency (must wait for quorum)
  Use for: account blocks (MUST stop fraud immediately)

EVENTUAL CONSISTENCY:
  Readers may see stale data for a window
  Implementation: async replication, last-writer-wins
  Cost: lower latency, higher throughput
  Use for: merchant risk scores (minutes stale is OK)

READ-YOUR-OWN-WRITES:
  The writer always sees its own latest write
  Others may see stale data
  Implementation: sticky sessions, version vectors
  Use for: customer profile updates (customer sees their changes)

CAUSAL CONSISTENCY:
  If A caused B, anyone who sees B also sees A
  Implementation: vector clocks, dependency tracking
  Use for: fraud investigation (see events in causal order)

For your fraud scoring:
  Hot path → eventual consistency (features cached locally, async refresh)
  WHY: 3-5 ms for Redis lookup is acceptable freshness
  RISK: stale data for ~1 sec max (Redis replication lag)
  MITIGATION: critical signals (account block) use pub/sub with < 100 ms delivery
```

## 9.3 Cache Stampede Prevention

```
Problem: cache miss + high traffic = thundering herd to backing store

Scenario:
  Popular key expires → 1,000 concurrent requests all miss →
  1,000 requests all query database → database overloaded → cascade

Solutions:

1. LOCK-BASED (only one fetcher):
   if cache_miss(key):
       if acquire_lock(key, timeout=100ms):
           value = fetch_from_source(key)
           cache_set(key, value, ttl)
           release_lock(key)
       else:
           wait_for_value(key, timeout=200ms)  # someone else is fetching
   
2. PROBABILISTIC EARLY REFRESH:
   if (now > ttl - random(0, early_window)):
       refresh_in_background(key)
   # Some requests trigger refresh BEFORE expiry
   # Reduces probability of mass-expiry

3. STALE-WHILE-REVALIDATE:
   Always return cached value (even if stale)
   Trigger async refresh in background
   Next request gets fresh value
   
   Similar to HTTP Cache-Control: stale-while-revalidate
```

---

# SECTION 10: INCIDENT RESPONSE & PRODUCTION OPERATIONS

---

## 10.1 Incident Severity Levels

```
SEV-1 (Critical): System-wide outage or data integrity breach
  - ALL fraud scoring down (approving everything = massive fraud risk)
  - Customer data exposed
  - Response time: IMMEDIATE (page on-call within 5 min)
  - Resolution target: 30 minutes

SEV-2 (High): Major feature degradation
  - LLM warm path down (false positives not being recovered)
  - p99 latency 5× normal (some transactions timing out)
  - Response time: 15 minutes
  - Resolution target: 2 hours

SEV-3 (Medium): Partial degradation
  - One model version degraded (fallback active)
  - One AZ capacity reduced
  - Response time: 1 hour
  - Resolution target: 8 hours

SEV-4 (Low): Minor issue
  - Monitoring gap, non-critical alert
  - Response time: next business day
```

## 10.2 Runbook: Hot Path Latency Spike

```
ALERT: fraud_scoring_p99_latency > 15 ms (SLA: 30 ms, threshold: 15 ms)

Step 1: SCOPE (30 seconds)
  - Is it all replicas or one? → Grafana dashboard
  - Is it all traffic or specific merchants? → log filter
  - When did it start? → correlate with deployments/changes

Step 2: IMMEDIATE MITIGATION (2 minutes)
  If one replica:
    → kubectl delete pod (replace unhealthy replica)
  If all replicas:
    → Check recent deploy → rollback if < 30 min ago
    → Check feature store → circuit breaker status
    → Check GPU → nvidia-smi (throttling? errors?)

Step 3: DIAGNOSE (5-10 minutes)
  - Tracing: which stage is slow? (feature fetch vs model vs search)
  - Metrics: GPU util, CPU util, memory pressure, queue depth
  - eBPF: runqlat (scheduler delay?), tcpretrans (network?)
  - DCGM: thermal throttle? ECC errors?

Step 4: ROOT CAUSE (varies)
  Common causes and fixes:
  | Symptom | Likely Cause | Fix |
  |---------|-------------|-----|
  | All stages slow | CPU throttling | Check CFS, frequency gov |
  | Feature fetch slow | Redis latency | Circuit breaker, cache fallback |
  | Model inference slow | GPU throttle | Check temp, check clocks |
  | Network between stages | Retransmits | Check NIC, PFC counters |
  | Spiky (not steady) | GC pause | (if Java) tune GC |

Step 5: RESOLVE + POSTMORTEM
  - Verify metrics returned to normal
  - Write postmortem within 48 hours
  - Action items to prevent recurrence
```

## 10.3 Deployment Safety

```
Pre-deployment checklist:
  □ Load test passed (no regression in p99)
  □ Model accuracy validated (offline evaluation)
  □ Feature compatibility verified (schema match)
  □ Canary config ready (1% → 5% → 25% → 100%)
  □ Rollback plan documented
  □ On-call aware of deployment

Deployment sequence:
  1. Deploy to staging → run integration tests (10 min)
  2. Deploy canary (1% traffic) → monitor 15 min
     - Guardrails: p99 < 2× baseline, error rate < 0.1%
  3. If guardrails pass: expand to 25% → monitor 30 min
  4. Expand to 100% → monitor 2 hours
  5. Previous version kept warm for 24 hours (instant rollback)

Automatic rollback triggers:
  - p99 latency > 2× baseline for 5 minutes
  - Error rate > 1% for 2 minutes
  - GPU OOM events > 3 in 5 minutes
  - Model accuracy metric drops > 5% (requires delayed outcome join)
```

---

# SECTION 11: ML-SPECIFIC SYSTEM DESIGN PATTERNS

---

## 11.1 Feature Store Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      FEATURE STORE                            │
├─────────────────────────────────────────────────────────────┤
│ OFFLINE STORE (batch features, historical data)              │
│  - Parquet/Delta Lake on S3/GCS                             │
│  - Used for: training data, backfill, point-in-time joins   │
│  - Freshness: hours to days                                 │
├─────────────────────────────────────────────────────────────┤
│ ONLINE STORE (real-time serving)                             │
│  - Redis/DynamoDB/Bigtable                                  │
│  - Used for: inference-time feature lookup                  │
│  - Freshness: seconds to minutes                            │
│  - Latency: < 5 ms p99                                     │
├─────────────────────────────────────────────────────────────┤
│ STREAMING ENGINE (real-time features)                        │
│  - Flink/Kafka Streams                                      │
│  - Computes: velocity, session features, running aggregates │
│  - Freshness: < 1 second                                   │
│  - Writes to online store                                   │
├─────────────────────────────────────────────────────────────┤
│ FEATURE REGISTRY (metadata + lineage)                        │
│  - Feature definitions (schema, owner, SLA)                 │
│  - Lineage (which models use which features)                │
│  - Monitoring (drift detection, freshness alerts)           │
└─────────────────────────────────────────────────────────────┘

Your fraud scoring feature sources:
  Real-time: transaction velocity (Flink → Redis, < 1 sec)
  Near-real-time: device fingerprint (event → Redis, < 1 min)
  Batch: customer embeddings (Spark → Redis, every 4 hours)
  Static: merchant category codes (config file, updated monthly)
```

## 11.2 Training → Serving Consistency

```
The training-serving skew problem:
  Training computes features one way (batch, historical)
  Serving computes features another way (real-time, slightly different logic)
  → Model sees DIFFERENT feature distributions at inference time
  → Silent accuracy degradation!

Solutions:
  1. Shared feature computation code:
     Same code computes features for training AND serving
     Feature store handles: batch access (training) and online access (serving)
     
  2. Feature logging at serving time:
     Log actual features used at inference (not just predictions)
     Use logged features for next training cycle
     Guarantees: model trains on exactly what it will see in production

  3. Point-in-time correct training:
     When creating training data: use features AS THEY EXISTED at prediction time
     Not current features (that would leak future information)
     
     Example: training on fraud decision from Jan 15
       Use features that EXISTED on Jan 15 (not today's aggregates)
       Feature store provides temporal snapshots

  4. Feature validation at serving:
     Compare incoming feature distribution to training distribution
     Alert if KL-divergence > threshold (feature drift detected)
```

## 11.3 Online Learning / Continuous Training

```
For fraud detection: threat landscape changes DAILY

Static model (retrained monthly):
  - Good for first week
  - Starts missing new fraud patterns by week 2
  - By week 4: 10-15% accuracy degradation

Continuous training loop:
  1. Serve predictions (hot path)
  2. Log predictions + features
  3. Wait for labels (chargebacks arrive in 2-14 days)
  4. Join predictions with labels
  5. Retrain on recent data (sliding window: last 90 days)
  6. Validate: offline metrics + shadow mode
  7. Deploy new model (canary)
  8. Repeat (daily or weekly cadence)

Champion-Challenger pattern:
  Champion: current production model (serving 100% traffic)
  Challenger: newly trained model (shadow mode, 0% traffic, evaluated offline)
  
  When challenger beats champion on holdout set:
    Promote challenger to canary (1% traffic)
    If canary passes guardrails → promote to champion
    Old champion → retire
```

---

# SECTION 12: DISTRIBUTED SYSTEM PATTERNS FOR HPC

---

## 12.1 Leader Election for Singleton Tasks

```
Some tasks must run as exactly ONE instance:
  - Feature pipeline coordinator
  - Model training job scheduler
  - Cache invalidation broadcaster
  
Using K8s Lease (built-in):
  apiVersion: coordination.k8s.io/v1
  kind: Lease
  metadata:
    name: fraud-feature-coordinator
  spec:
    holderIdentity: "pod-abc123"
    leaseDurationSeconds: 15
    acquireTime: "2026-06-01T10:00:00Z"
    renewTime: "2026-06-01T10:00:10Z"

  Pod acquires lease → becomes leader
  Leader renews every 10 sec (< 15 sec duration)
  If leader dies → lease expires → new pod acquires
  
  Failover time: leaseDurationSeconds (15 sec)
```

## 12.2 Idempotency for Exactly-Once Processing

```
Problem: network failures cause retries → duplicate processing

Example: fraud decision sent to card network
  First attempt: timeout (but actually succeeded!)
  Retry: sends SAME decision again → duplicate block!

Solution: idempotency keys

  // Request includes idempotency_key (e.g., transaction_id)
  if (cache.contains(idempotency_key)) {
      return cache.get(idempotency_key);  // return cached result
  }
  auto result = process(request);
  cache.set(idempotency_key, result, ttl=24h);
  return result;

Properties:
  - First call: process and cache
  - Subsequent calls with same key: return cached result
  - Safe to retry without side effects
  - TTL prevents unbounded growth
```

## 12.3 Event Sourcing for Audit Trail

```
Fraud detection requires COMPLETE audit trail:
  - Every decision must be explainable
  - Regulators can ask "why was this approved?" years later
  - Model version, features used, scores — all logged

Event sourcing:
  Every state change is an IMMUTABLE EVENT in an append-only log:
  
  events:
    - {type: "transaction_received", ts: T1, data: {...}}
    - {type: "features_computed", ts: T2, data: {features: [...]}}
    - {type: "model_scored", ts: T3, data: {model: "v2.1", score: 0.73}}
    - {type: "decision_made", ts: T4, data: {action: "3DS", reason: "..."}}
    - {type: "3ds_completed", ts: T5, data: {result: "success"}}
    - {type: "warm_path_review", ts: T6, data: {overturn: true, explanation: "..."}}
  
  Benefits:
  - Complete audit trail (regulatory compliance)
  - Can replay events to debug decisions
  - Can retrain models on exact feature snapshots
  - Immutable (can't be tampered with post-hoc)
  
  Storage: Kafka (real-time) → S3/GCS (long-term archival, Parquet)
  Retention: 7 years (BSA/AML requirement)
```

---

# SECTION 13: SYSTEM DESIGN INTERVIEW DEEP PATTERNS

---

## 13.1 "Design a real-time fraud detection system"

```
Requirements gathering:
  - QPS: 10,000 transactions/second
  - Latency: < 30 ms p99
  - Accuracy: < 0.1% false negative (miss rate), < 5% false positive
  - Availability: 99.99% (< 52 min downtime/year)
  - Regulatory: full audit trail, explainable decisions

High-level design:
  [Visa/Mastercard] → [API Gateway] → [Fraud Scoring Service]
                                              ↓
                                     [Decision Engine]
                                              ↓
                                     [Response to Network]

Deep dive areas (pick based on interviewer interest):
  
  A) Data path (how features arrive):
     - Real-time: Kafka → Flink → Redis (velocity, session features)
     - Batch: Spark → Redis (aggregates, embeddings)
     - Request: parsed from transaction message
  
  B) Scoring architecture:
     - Multi-model ensemble (XGBoost + BERT + rules)
     - In-process, zero-copy, parallel CPU/GPU
     - CUDA Graphs for GPU kernel launch optimization
     - Pre-allocated buffers (arena allocator)
  
  C) Infrastructure:
     - K8s with GPU nodes, CPU Manager static, topology manager
     - Cilium for low-latency pod networking
     - Redis cluster for feature store (3-5 ms lookup)
     - Kafka for event streaming and audit log
  
  D) Reliability:
     - Multi-AZ deployment (survive AZ failure)
     - Circuit breakers for dependencies
     - Fallback models (XGBoost-only if GPU fails)
     - PDB + high priority class (survive eviction)
  
  E) Monitoring:
     - p50/p99 latency per stage
     - Model accuracy (joined with delayed labels)
     - Feature freshness and drift
     - GPU health (DCGM)
```

## 13.2 "Design a search ranking system for 1B documents"

```
Requirements:
  - Corpus: 1 billion documents
  - QPS: 50,000 queries/second
  - Latency: < 100 ms p99
  - Ranking quality: optimize for engagement (clicks, time spent)

Architecture:
  [Query] → [Query Understanding] → [Retrieval] → [Ranking] → [Results]

Retrieval (recall-focused, < 30 ms):
  L0: Keyword match (BM25 inverted index, Elasticsearch)
      Retrieve: top 1,000 by keyword relevance
  
  L1: Semantic match (embedding ANN, FAISS/ScaNN)
      Retrieve: top 1,000 by embedding similarity
  
  Fusion: RRF or learned combination → top 200 candidates

Ranking (precision-focused, < 50 ms):
  L2: Light ranker (MLP, features: BM25 score, embed score, freshness)
      Score all 200 candidates → top 50
  
  L3: Heavy ranker (transformer, cross-encoder or sequential model)
      Score top 50 with full features → top 10
  
  Final: diversity, deduplication, policy filters → show 10

Scale considerations:
  - Inverted index: sharded across 100+ nodes (partition by doc_id hash)
  - Embedding index: replicated on GPU nodes (entire index in GPU HBM)
  - Ranking model: on GPU, batched forward pass for 50-200 candidates
  - Embedding model: pre-computed offline, stored in index
  
  QPS distribution:
    50,000 QPS ÷ 100 retrieval shards = 500 QPS per shard (manageable)
    50,000 QPS ÷ 20 ranking GPUs = 2,500 QPS per GPU
    Per GPU: 2,500 QPS × 50 candidates = 125,000 candidates/sec scored
    At batch=64, 5 ms/batch: 12,800 candidates/sec → need ~10 GPUs
```

## 13.3 "Design an LLM serving platform for 1,000 concurrent users"

```
Requirements:
  - Model: 70B parameter LLM
  - Concurrent users: 1,000
  - TTFT (time to first token): < 500 ms p99
  - TPOT (time per output token): < 50 ms p99
  - Average response: 200 tokens

Capacity math:
  1,000 users × 200 tokens / 50 ms per token = 1,000 users need
  Time per user: 200 × 50 ms = 10 sec per response
  Throughput: 1,000 users / 10 sec = 100 new requests/sec
  Total tokens/sec: 100 × 200 = 20,000 tokens/sec needed

  70B model on H100 (TP=4, FP8):
    Throughput: ~3,000-5,000 tokens/sec per 4-GPU group
  
  Groups needed: 20,000 / 4,000 = 5 groups = 20 GPUs
  With headroom: 7 groups = 28 GPUs

Architecture:
  [Load Balancer (KV-aware routing)]
       ↓
  [vLLM/TRT-LLM workers × 7] (each with 4×H100, TP=4)
       ↓
  [KV cache: paged, FP8 quantized]
  [Prefix cache: shared system prompt]

Key optimizations:
  - KV-aware routing: route follow-ups to same worker (reuse KV cache)
  - Prefix caching: system prompt (500 tokens) computed once, shared
  - FP8 KV: 2× more concurrent sequences in same memory
  - Chunked prefill: don't block decode while prefilling new requests
  - Speculative decoding: 1.5-2× decode throughput
  
Cost:
  28 × H100 at $8/hr = $224/hour = $5,376/day
  Cost per user per month: $5,376 × 30 / 1,000 = $161/user/month
  
  vs OpenAI API:
  200 tokens × 100 req/user/day × 30 days = 600,000 tokens/user/month
  At $5/M tokens = $3/user/month (much cheaper for low usage)
  
  Self-hosted wins when: high per-user volume, data privacy, customization
```

---

# SECTION 14: ADVANCED APPLICATION INTERVIEW QUESTIONS (Grind-Proof)

## "How do you handle a dependency (Redis) going down during peak traffic?"

→ Layered defense:
1. **Circuit breaker** opens after 5 failures in 10 sec → stop calling Redis
2. **Local cache** (in-process, 60-sec TTL) serves stale features
3. **Fallback model** (XGBoost with basic features only, no Redis features)
4. **Load shedding** if even fallback is overwhelmed → 429 for excess
5. **Alert** fires → on-call investigates Redis failure
6. **Auto-recovery**: circuit breaker half-opens every 30 sec, tests Redis
7. When Redis recovers: cache warms up, full model resumes

Impact during outage:
  - Accuracy: drops ~10% (missing some features, using stale cache)
  - Latency: actually DECREASES (no Redis call, cached response faster)
  - Availability: maintained (degraded quality, but still serving)
  - This is CORRECT behavior: worse answer > no answer

## "How do you ensure model changes don't cause production incidents?"

→ Multi-gate validation:
1. **Offline evaluation**: holdout set accuracy, calibration, fairness metrics
2. **Shadow mode**: run new model on production traffic, compare outputs, no serving
3. **Canary deployment**: 1% traffic, 15-min bake, guardrail metrics monitored
4. **Progressive rollout**: 1% → 5% → 25% → 100% over 24 hours
5. **Automatic rollback**: any guardrail breach → instant revert
6. **Feature compatibility check**: verify feature schema matches model expectation
7. **A/B test**: for accuracy improvements, measure business metric lift

Timeline: model trained → 2 days validation → 1 day shadow → 1 day canary → full rollout
Fastest (emergency fix): 2 hours (skip shadow, aggressive canary)

## "What's the most complex production incident you've debugged?"

→ Structure for answer:
```
SITUATION: what was failing (p99 spikes, intermittent)
DETECTION: how we found it (alert, dashboard correlation)
DIAGNOSIS: systematic narrowing
  Layer 7: is it traffic pattern? → no (constant QPS)
  Layer 6: is it model? → no (same model, same accuracy)
  Layer 5: is it data path? → no (shared memory working)
  Layer 4: is it GPU? → yes! (SM utilization dropping periodically)
  Layer 3: is it scheduling? → yes! (kernel compaction on GPU-feeding CPU)
  Layer 1: root cause: THP compaction on NUMA node with GPU
ROOT CAUSE: khugepaged scanning memory on CPUs 4-11, causing
  GPU-feeding thread to be delayed by 5-10 ms periodically
FIX: disabled THP, switched to explicit hugepages
VERIFICATION: p99 spikes eliminated, perf stat confirmed no more compaction
PREVENTION: added to boot config for all GPU nodes, added monitoring
```

## "How do you reason about the cost vs accuracy tradeoff for models?"

→ Framework:
```
For each accuracy improvement ΔA:
  Cost of improvement: ΔC (more compute, larger model, more features)
  Business value of improvement: ΔV (less fraud, more revenue, better UX)
  
  If ΔV > ΔC: invest
  If ΔV < ΔC: don't (or find cheaper path to same accuracy)

Example:
  FP16 → FP8: -2% accuracy, -50% cost
  Is 2% accuracy worth 2× the GPU cost? Usually NO.
  
  XGBoost → Transformer: +13% accuracy, +5× GPU cost
  Is 13% accuracy worth 5× GPU cost?
  For fraud detection: each 1% accuracy = ~$100M/year in prevented fraud
  13% × $100M = $1.3B value vs 5× GPU cost ($5M/year → $25M/year)
  ROI: $1.3B / $25M = 52× return. ABSOLUTELY YES.
  
  This is how you justify EVERY technical investment.
```

## "How do you make your system observable without adding latency?"

→ Zero-overhead observability architecture:
1. **eBPF (kernel-level)**: no application code changes, < 0.1% overhead
   - Trace latency: auto-instrumented HTTP/gRPC spans
   - Network: TCP retransmits, DNS failures, connection pools
   - System: scheduler delays, disk I/O, memory pressure

2. **NVTX markers (GPU)**: < 1 µs per marker, compiled out in release
   - Annotate CUDA stream operations with names
   - Visible in Nsight and exported as OTel spans
   
3. **Lock-free metrics**: atomics, per-core counters, merged on scrape
   - No lock contention on hot path
   - Prometheus scrapes every 15 sec (not on request path)
   
4. **Async event publishing**: SPSC ring → background flush thread
   - Request path writes event to ring (< 100 ns)
   - Background thread flushes to Kafka (off critical path)
   - If ring full: drop event, increment counter (acceptable)
   
5. **Sampling for expensive traces**: 1% of requests get full trace
   - 100% of requests get basic metrics (latency, status)
   - 1% get: feature values, model inputs, full span tree
   - Always trace: errors, high-latency requests (tail sampling)
