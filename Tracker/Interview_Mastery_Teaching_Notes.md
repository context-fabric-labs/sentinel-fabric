# Interview Mastery - Teaching Notes (TTS Replay Ready)
## Coupang (AI Security) + Socure (AI Identity/Fraud)
## Created: May 29, 2026

---

## SECTION 1: THE TWO ROLES — SAME ENGINEER, DIFFERENT LENS

### The Core Difference

These two roles are like two sides of the same coin. You are the same engineer — systems background, Rust gateways, GPU serving, FAISS, TigerGraph, LLM orchestration, circuit breakers. But each role looks at your skills through a different lens.

**Coupang asks:** "Can you build the cage around the AI so it doesn't hurt anyone?"
**Socure asks:** "Can you build the AI that catches the bad guys?"

At Coupang, the AI is the thing being controlled. You're the security architect who ensures the LLM can't leak data, can't call tools it shouldn't, can't expose one tenant's data to another.

At Socure, the AI is the weapon you're building. You're the systems engineer who makes fraud detection fast, accurate, explainable, and reliable at scale.

### What's Common Between Both

Here's what's beautiful about your position: about 70% of the work is the same infrastructure.

1. Rust gateway that parses, validates, and shapes requests — both roles need this.
2. Online feature cache with freshness tracking — both roles need bounded, pre-materialized signals.
3. Parallel model branches with deadlines — both need Transformer, FAISS, and graph running concurrently.
4. Deterministic policy/decision layer — Coupang calls it "policy engine," Socure calls it "XGBoost calibrated scorer."
5. Circuit breakers and degradation — both need fail-safe behavior under partial failure.
6. Audit and observability — both need immutable decision logs with full lineage.
7. Tool/action mediation — at Coupang it's "tool broker for agent actions," at Socure it's "threshold policy for approve/decline/step-up."

### The 30% That's Different

**Coupang-specific:**
- Prompt injection defense (direct and indirect)
- Cross-tenant isolation in shared LLM inference
- Model lifecycle security (signed artifacts, registry RBAC)
- RAG retrieval authorization (ACL-aware vector search)
- Agent tool mediation with capability tokens
- Security SLOs and degradation ladders

**Socure-specific:**
- Feature engineering for identity attributes (email age, phone tenure, device reuse)
- Graph-based fraud ring detection (shared devices, synthetic clusters)
- Vector similarity to known fraud patterns
- Score calibration and threshold governance
- Explainability and reason codes for compliance
- Label delay, leakage prevention, and point-in-time correctness
- PR-AUC, recall at fixed FPR evaluation

### Your CapitalOne Architecture Maps to BOTH

Think about your three-tier system:

**Tier 1 (sub-5ms fraud scoring)** → Maps to Socure's hot-path XGBoost decisioning
**Tier 2 (13B reasoning with Sentinel)** → Maps to Coupang's guardrail pipeline
**Tier 3 (70B agent with tool calls)** → Maps to Coupang's tool broker / action authorizer

The Sentinel gateway you built IS both:
- A security PEP (Coupang framing): admission control, token budgets, circuit breakers
- A serving reliability layer (Socure framing): p99 protection, degradation modes, GPU governance

Apache Arrow shared buffers you built IS both:
- Zero-copy data flow for secure context propagation (Coupang: signed request context)
- Zero-copy feature flow for low-latency scoring (Socure: FeatureBlock without serialization)

### Your Siri Search System Maps to BOTH

FAISS with ACL-aware retrieval IS both:
- Secure RAG (Coupang): enforce data authorization before context reaches model
- Similarity search for fraud patterns (Socure): find nearest known-bad profiles

TigerGraph IS both:
- Identity graph for relationship-based access control (Coupang: ReBAC, tenant relationships)
- Fraud ring detection (Socure: shared devices, fraud neighbors, synthetic clusters)

### Your Broadcom Experience Maps to BOTH

Network security and proxy architecture IS both:
- Gateway security enforcement (Coupang: AI gateway as PEP)
- High-throughput request processing with policy (Socure: real-time decisioning under SLA)

---

## SECTION 2: THE UNIFIED MENTAL MODEL

### One Architecture, Two Framings

```
Request arrives
    |
    v
[GATEWAY] — Coupang: "Policy Enforcement Point" / Socure: "Rust scoring gateway"
    |
    v
[IDENTITY/CONTEXT] — Coupang: "User + tenant binding" / Socure: "Entity keys + FeatureBlock"
    |
    v
[INPUT VALIDATION] — Coupang: "Prompt injection detection" / Socure: "Schema validation + normalization"
    |
    v
[FEATURE/CONTEXT RETRIEVAL] — Coupang: "ACL-aware RAG" / Socure: "Online feature cache"
    |
    v
[PARALLEL BRANCHES]
    |--- Transformer: Coupang: "Input guardrail classifier" / Socure: "Behavioral sequence model"
    |--- FAISS: Coupang: "Injection pattern detection" / Socure: "Fraud pattern similarity"
    |--- Graph: Coupang: "Tenant relationship check" / Socure: "Fraud ring detection"
    |
    v
[DECISION LAYER] — Coupang: "Deterministic policy engine" / Socure: "Calibrated XGBoost"
    |
    v
[ACTION/OUTPUT] — Coupang: "Tool broker + output guardrails" / Socure: "Approve/decline/step-up + reason codes"
    |
    v
[AUDIT] — Both: "Immutable decision log with full lineage"
```

The architecture is the same. The vocabulary changes. The concerns shift. But the engineering is identical.

---

## SECTION 3: SOCURE DEEP-DIVE — THE REQUEST FLOW

### Why Identity Fraud is Different From Transaction Fraud

Real banks (CapitalOne, Chase, Amex) deploy identity scoring and transaction scoring as **sibling services on a shared platform** — same feature store, same model registry, same audit bus — but separate scoring services with independent latency SLAs and blast radii.

- **Transaction fraud** fires at every swipe: 20K+ TPS, 5ms budget, PCI-DSS scope. You KNOW who the cardholder is.
- **Identity fraud** fires at account opening: lower TPS but 100-200ms budget, KYC/AML scope. The whole QUESTION is "who is this person?"
- **Call center step-up** is a third variant: even more generous 500ms-1s budget, human on the line.

Same platform thinking. Separate services. Independent failure domains. (This is your Broadcom multi-tenant architecture — shared proxy infra, isolated per-service policies.)

### The Request Flow

A fintech (Cash App, Chime, Coinbase) has a new user signing up. They collect name, DOB, SSN-last-4, email, phone, IP, device fingerprint. Then fire a single API call to Socure: "Score this identity."

**Step 1: Parse and Validate (Gateway)**
The Rust gateway deserializes JSON into a typed `IdentityRequest` struct — same pattern as CapitalOne's `TransactionEvent` becoming a zero-copy Arrow record. Schema validation rejects malformed payloads immediately.

**Step 2: Entity Resolution (Identity-Specific)**
The system asks: "Have we seen this email before? This phone? This device?" Queries a fast KV store (Redis cluster or Arrow-backed feature cache) to resolve incoming signals to known entity IDs. This is where you discover the "new" phone was used in 47 applications last month across 12 names.

This step is what makes identity fraud different: in transaction fraud, the card number identifies the user. In identity fraud, the whole question IS "who is this?" — and they might be entirely synthetic.

**Step 3: Feature Assembly (FeatureBlock)**
Once entity resolution completes, the system assembles a FeatureBlock — your familiar pattern from CapitalOne's Tier 1. But instead of transaction velocity and merchant category, the features are:
- Email domain age (gmail created 2 days ago vs 8 years ago)
- Phone-to-name match confidence (carrier lookup)
- Device fingerprint reuse count across applications
- Address deliverability score
- SSN first-seen date and usage count
- IP geolocation vs stated address distance
- Behavioral signals: typing speed, form fill time, copy-paste detection

These features come from 3 sources in parallel (like Broadcom's 6 model branches):
1. Internal feature cache (pre-computed, sub-1ms)
2. Third-party enrichment APIs (carrier lookup, email intel — 20-50ms each)
3. Graph query for relationship features (TigerGraph — 10-30ms)

All fire in parallel with independent deadlines. Circuit breaker per source. If carrier API is slow, degrade gracefully — score with available features.

**Step 4: Parallel Model Scoring**
With the assembled FeatureBlock, run scoring branches in parallel:
- **XGBoost**: Primary risk scorer. 200+ hand-crafted features. Sub-1ms inference. This is your Tier 1 hot-path model.
- **Transformer**: Sequence model over the applicant's behavioral pattern (application velocity, time-of-day patterns). 5-10ms.
- **FAISS**: Vector similarity search against known fraud embeddings. "Does this application look like a cluster of confirmed fraud we've seen?" 2-5ms.
- **TigerGraph**: Fraud ring detection. "Is this device/phone/address connected to confirmed fraud entities within 2 hops?" 10-30ms.

All parallel. All with deadlines. Exactly like Broadcom's 6-model fan-out with shared RequestContext.

**Step 5: Ensemble and Calibration**
Individual model scores feed into a calibrated meta-model (usually another XGBoost or logistic regression). This produces a single score 0-1000 where the score MEANS something: "A score of 700 means 70% probability this identity is legitimate."

Calibration is critical — Socure's customers set thresholds ("reject below 300, approve above 700, manual review in between"). If your scores aren't calibrated, their thresholds are meaningless.

**Step 6: Decision + Reason Codes**
The calibrated score hits the customer's configured policy:
- Score > 700 → Auto-approve
- Score 300-700 → Step-up verification (document upload, OTP)
- Score < 300 → Auto-reject

Plus: generate top-3 reason codes (SHAP-based or rule-based): "High risk because: (1) email created 2 days ago, (2) phone carrier mismatch, (3) device seen in 12 prior applications."

**Step 7: Audit Trail**
Every decision logged with full feature snapshot, model versions, scores, reason codes. Immutable. Queryable. Compliant with FCRA/ECOA for adverse action notices.

### The CapitalOne Parallel (Your Story)

"At CapitalOne, I built this exact architecture for transaction fraud. The Rust gateway parsed card-swipe events into Arrow records. The FeatureBlock assembled 150+ velocity features from our online cache. The Tier 1 XGBoost scored in under 5ms. We had circuit breakers per feature source and degraded gracefully when upstream services were slow. The difference at Socure is the domain — identity signals instead of transaction signals — but the systems pattern is identical: parse, resolve, enrich in parallel with deadlines, score with ensemble, decide with calibrated thresholds, audit everything."

## [Sections 4-10 will be added as we progress through the teaching session]

