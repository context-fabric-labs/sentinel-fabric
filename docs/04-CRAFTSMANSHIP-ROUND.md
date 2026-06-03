# PART 4 — CRAFTSMANSHIP ROUND (Grouped by Story)

## Strategy

The Craftsmanship round evaluates technical depth, engineering excellence, and operational maturity through **discussion-based questions** (not behavioral STAR). Group into **5 themes**, each grounded in your real project experience.

| Theme | Discussion Topics | Primary Project Context |
|-------|-------------------|------------------------|
| **A** | Build vs. Buy Decisions | Capital One (Rust gateway, Triton, managed Kafka) + Broadcom (TF Serving vs. custom) |
| **B** | Monitoring & Observability | Capital One (SLO-based, multi-tier tracing) + Apple (unified pipeline observability) |
| **C** | Reliability & Failure Design | Capital One (99.999% uptime, feature store incident) + Apple (graceful degradation) |
| **D** | Testing & Deployment Strategy | Capital One (shadow deployment, canary, chaos) + Broadcom (evaluation framework) |
| **E** | Technical Debt & On-Call | Capital One (Python→Rust migration) + Broadcom (systematic process improvement) |

---

## THEME A: Build vs. Buy Decisions

### Your Framework (Used in Practice)

```
DECISION FRAMEWORK:
1. Is this a differentiator? → Build only where you differentiate
2. Can managed services meet the SLA? → Buy if they can
3. Total Cost of Ownership (5-year) → Include: team, maintenance, knowledge loss
4. Organizational capability → Can you maintain 24/7 with current team?
5. Vendor lock-in risk → Can you migrate later?
```

### Real Decisions Made

| Decision | Build | Buy | Rationale |
|----------|-------|-----|-----------|
| **Event streaming** | Custom consumers (exactly-once) | Managed Kafka (MSK) | Commodity backbone doesn't differentiate; custom consumer semantics do |
| **LLM serving** | Rust gateway + guardrails | Triton Inference Server | Gateway is our differentiation (< 2ms guardrails); GPU serving is NVIDIA's strength |
| **Observability** | Custom cross-tier trace correlation | Prometheus + Grafana (open-source) | Cross-tier Arrow buffer tracing doesn't exist off-the-shelf; metrics collection is commodity |
| **Feature store** | Custom online serving (Redis + purpose-built serialization) | N/A — nothing met < 3ms requirement | Sub-3ms serving with regulatory lineage didn't exist commercially |
| **Service mesh** | N/A | Istio (managed by platform team) | mTLS everywhere + audit trails for PCI-DSS; 5 languages in stack makes sidecar necessary |
| **Vector search** | In-process FAISS GPU | N/A — no vector DB meets 0.4ms in-process | Can't afford network hop for sub-ms latency; index rebuilt daily anyway |

### How to Discuss (Senior Staff Level)

**Message Queue — Build vs. Managed:**
"At Capital One's scale (24,500 TPS fraud decisioning), we chose managed Kafka (MSK) for our event backbone but built custom consumers with exactly-once processing semantics. The managed consumer groups couldn't meet our ordering + latency requirements for idempotent transaction processing. Key insight: **buy the commodity layer, build the differentiation layer.** Total cost comparison: building our own Kafka cluster would require 3 engineers × $300K fully-loaded = $900K/year + hardware + on-call burden vs. MSK at ~$180K/year. Not close."

**Observability — Build vs. Datadog:**
"For the fraud platform, we use open-source (Prometheus + Thanos + Grafana + Jaeger) for standard metrics/tracing, but built custom cross-tier trace correlation because no commercial tool understands our Arrow shared memory boundaries. At our scale (~50 services), Datadog would cost ~$500K/year. The open-source stack costs ~$150K/year in infrastructure + 1 engineer's 20% time. The custom piece (cross-tier correlation) was 3 engineer-weeks to build. If we were at LinkedIn's scale (thousands of services, hundreds of thousands of metrics endpoints), I'd evaluate building a full internal platform — at that scale, vendor costs become absurd ($50M+/year)."

**Feature Store — Build vs. Feast/Tecton:**
"We needed sub-3ms online serving for real-time fraud features + regulatory lineage for audit. Evaluated Feast and Tecton: Feast's online serving was ~8ms (Redis-backed but with serialization overhead); Tecton was ~5ms but lacked our compliance audit trail requirements. Built custom: Redis Cluster with purpose-built serialization (Arrow-compatible format, zero-copy into feature block). Investment: 3 engineers × 4 months. Justified because the feature store is on the critical path of every fraud decision — any latency here directly impacts the 5ms SLA."

---

## THEME B: Monitoring & Observability

### Your Monitoring Philosophy (Applied at Capital One)

```
FOUR-LAYER MONITORING STRATEGY:

Layer 1: Business Metrics (WHY we exist)
├── Fraud decisions per second (24,500 TPS target)
├── False positive rate by merchant category
├── LLM analysis approval recovery rate (medium-risk txns)
├── Agent deflection rate (22% target)
└── $ fraud prevented per day

Layer 2: Service Metrics (HOW we're performing)
├── RED per tier: Rate, Errors, Duration
├── Tier 1 p99 latency vs. 5ms SLO
├── Tier 2/3 p99 vs. respective SLOs
├── KV-cache hit rate (70% target)
├── Guardrail overhead (< 3ms target)
└── Error budget burn rate (multi-window)

Layer 3: Infrastructure Metrics (WHAT we're running on)
├── GPU SM utilization (72% target)
├── GPU memory headroom (> 15% free)
├── Feature store cache hit rate
├── Arrow buffer occupancy
├── NUMA cross-node access rate (should be 0)
└── Kafka consumer lag

Layer 4: Operational Metrics (HOW we're operating)
├── Deployment frequency (daily target)
├── Rollback rate (< 5% target)
├── MTTD (< 2 min, verified: feature store incident detected in 2 min)
├── MTTR (< 15 min for P1)
└── On-call pages per week (< 2 target)

ALERTING PHILOSOPHY:
• Alert on SYMPTOMS (user impact), not CAUSES (CPU high)
• Every alert must be actionable
• Multi-signal alerting (single metric spike → don't page)
• Error budget burn rate alerting (Google SRE approach):
  - 1h burn rate > 14.4 AND 5-min > 14.4 → PAGE
  - 6h burn rate > 6 AND 30-min > 6 → PAGE
  - 3-day burn rate > 1 AND 6h > 1 → TICKET
```

### SLO Implementation (Real Example — Fraud Platform)

```
SLI: % of decisions rendered within 5ms at p99
SLO: 99.99% of decisions within SLO (4.3 seconds of violation per 12 hours)

Error Budget Policy:
• Budget > 50%: Normal development velocity
• Budget 20-50%: Increased caution, extra testing required
• Budget < 20%: Feature freeze, reliability focus
• Budget exhausted: Traffic failover to backup decisioning path

Real Incident Application:
Feature store compaction caused 12-minute violation.
• Consumed 0.003% of daily error budget
• Well within tolerance — no feature freeze triggered
• But still warranted post-mortem because pattern could recur
```

### Debugging Production Issues (Real Example)

**1% of transactions had 12ms latency (SLO: 5ms):**

```
Step 1 — Characterize (2 min):
• When: started after Tuesday model deployment
• Which users: only for NEW merchants (< 30 days old)
• How bad: 12ms (2.4× SLO) affecting ~1% of traffic
• Pattern: constant, not periodic

Step 2 — Hypotheses (2 min):
• New model added latency? (checked: model serving time unchanged)
• Feature store cold cache? (checked: YES — miss rate 95% for new merchants)
• Network issue? (checked: no — same host)

Step 3 — Root cause (5 min):
• Traces showed 8ms feature hydration for cold entities
• New merchants had no pre-warmed feature vectors
• Feature store falls through to database for cache miss (8ms vs. 0.3ms hit)

Step 4 — Fix:
• Immediate: pre-warm feature store at merchant onboarding (background job)
• Prevention: alert on cache miss rate by entity age cohort
• Systematic: add feature warm-up to merchant provisioning pipeline

Total MTTI: 9 minutes. Total MTTR: 14 minutes (immediate mitigation)
```

### Observability at Apple (Cross-Pipeline Tracing)

"At Apple, each Siri pipeline stage (ASR, NLU, Search, TTS) had independent monitoring. End-to-end latency debugging required correlating across 4 different dashboards manually — taking 45+ minutes per incident.

I built unified tracing that propagated trace context through shared memory boundaries (not just RPC calls). Custom spans for business logic (intent classification confidence, FAISS recall@10, TTS prosody selection). Tail-based sampling for interesting traces (slow > 200ms, error, new deployment).

Result: MTTI from 45 min to 3 min. Teams stopped blaming each other because the trace showed exactly which stage was slow."

---

## THEME C: Reliability & Failure Design

### Your Reliability Principles (Applied Across Projects)

```
1. ISOLATION (Blast Radius Reduction):
   Capital One: Three tiers physically isolated — GPU pressure on Tier 3
              can't affect Tier 1 scoring. Per-core ownership prevents
              cross-contamination.
   Apple: Each pipeline stage independent failure domain. ASR failure
         degrades to text-only, doesn't take down entire Siri.

2. REDUNDANCY:
   Capital One: Multi-region (US-East, US-West, EU-West). Primary region fails →
              secondary assumes traffic in 30s (DNS failover + connection drain).
   Feature store: Redis Cluster with replicas. If primary fails, replica promotes
              automatically. Zero data loss due to synchronous replication.

3. GRACEFUL DEGRADATION (Helios degrade modes):
   Capital One:
   Level 0: Full service (all 3 tiers operational)
   Level 1: Tier 3 disabled (triage deferred to batch) — frees GPU for Tiers 1-2
   Level 2: Tier 2 simplified (rules-only reasoning, no LLM) — frees GPU for Tier 1
   Level 3: CPU-only scoring (XGBoost only, no neural) — survives complete GPU failure
   Level 4: Emergency fallback (static rules + block-list) — survives everything

   Each level automatically triggered by health signal. No human decision needed.

4. CIRCUIT BREAKERS:
   KV-cache routing: per-backend circuit breaker.
   Closed → Open after 5 consecutive failures.
   Half-open probe after 30s.
   Prevents cascading failures to healthy backends.

5. BACKPRESSURE:
   Bounded SPSC queues between tiers.
   If Tier 2/3 queues fill: requests dropped gracefully (not blocking Tier 1).
   Admission control at gateway: reject > capacity with 503 (fast fail).
```

### Multi-Region Architecture (Real Implementation)

```
Capital One Fraud Platform:
• US-East: 4× H100 nodes (primary, highest traffic)
• US-West: 2× H100 nodes (cost-optimized, MIG instances)
• EU-West: 2× L40S nodes (GDPR compliance, FP8 quantization)

Consistency model: Active-Active with region affinity.
• Transaction decisioning: served in nearest region (< 5ms is latency-sensitive)
• Model state: replicated from training region (eventual consistency, minutes)
• Feature store: multi-region Redis with LOCAL reads (no cross-region for scoring)
• Failover: if primary region fails, secondary assumes in 30s
  - DNS failover (Route53 health checks)
  - Connection draining on failing region
  - KV-cache cold start accepted (first few seconds at lower hit rate)

Why Active-Active (not Active-Passive):
• Can't afford to waste 50% capacity on standby
• Latency requires regional serving (US-East → EU-West round trip too slow)
• GDPR requires EU data stays in EU anyway
```

### Capacity Planning (Real Numbers)

```
Current: 24,500 TPS peak, 72% GPU utilization at peak
Growth: 15% QoQ transaction volume increase
Ceiling: Current GPU fleet saturates at ~45,000 TPS (GPU-bound)
Plan:
• Provision additional capacity at 70% sustained utilization
• 12-week lead time for H100 procurement → order 2 quarters ahead
• CPU-only fallback absorbs 20% overflow at degraded latency (Level 3 degrade)
• Black Friday: pre-scale 3× (temporary burst nodes with spot/preemptible)

Safety margins:
• GPU: alert at 70%, plan capacity at 60%, reject at 90%
• Feature store: alert at 80% memory, evict LRU at 85%, reject at 95%
• Network: alert at 60% bandwidth (NIC saturation is catastrophic, must prevent)
```

---

## THEME D: Testing & Deployment Strategy

### Testing Pyramid (Fraud Platform — Real)

```
Unit Tests (fastest, most numerous):
• Model inference correctness (deterministic outputs for fixed inputs)
• Feature transformation logic (normalize_amount, velocity calculations)
• Arrow serialization/deserialization correctness
• Circuit breaker state machine transitions
• Run on every PR. < 30 seconds.

Integration Tests:
• End-to-end decision pipeline with synthetic transactions
• Tier 1 → Arrow buffer → Tier 2 read — verifies zero-copy path
• Rust gateway → Triton ensemble → response — verifies guardrails
• Run on every PR. < 5 minutes.

Performance Tests (CI gate):
• Nightly load test at 2× peak (49,000 TPS)
• Latency regression gate: if p99 increases > 10%, block merge
• GPU utilization gate: if drops > 15%, investigate before merge
• CUDA Graph replay correctness at all batch sizes

Shadow Testing:
• New models shadow-score real production traffic
• Compare decisions: new vs. current model
• Alert if disagreement rate > threshold (model drift detection)
• Required for ALL model promotions — learned from Broadcom failure

Chaos Engineering:
• Monthly: kill GPU nodes (verify Level 3 fallback activates)
• Monthly: network partition feature store (verify timeout + fallback)
• Monthly: fill Tier 2/3 queues (verify backpressure doesn't block Tier 1)
• Quarterly: full region failover exercise
• Each chaos test has documented expected vs. actual behavior

What I DON'T Test:
• Third-party library internals (trust NVIDIA Triton, verify at boundary)
• Every config permutation (test boundaries + defaults)
• Type-safe Rust code that can't fail at runtime (compiler enforces correctness)
```

### Deployment Strategy (10,000 Instances)

```
Real approach at Capital One (multi-region):

Phase 0: Pre-deployment
├── All tests pass (unit, integration, performance)
├── Security scan clean (Snyk, custom CUDA memory safety checks)
├── Config validation against schema
├── Rollback tested in staging
└── Deployment plan reviewed for high-risk changes

Phase 1: Canary (1% — single pod per region)
├── Deploy to canary pod in each region
├── Compare ALL metrics against baseline fleet (not just error rate)
├── Automated rollback: error rate > baseline + 0.1% OR latency > baseline + 10%
├── Bake time: 30 minutes minimum
└── Verify KV-cache behavior (new binary must handle existing cached state)

Phase 2: Regional rollout (25% — one region fully)
├── Full region deployment (US-West — lowest traffic, least risk)
├── Cross-region metric comparison
├── Bake time: 1 hour
├── Human approval for Phase 3

Phase 3: Global rollout (100%)
├── Remaining regions sequentially (not parallel)
├── 15 minutes between each region
├── Final bake: 4 hours monitoring post-completion
└── Rollback available for 24 hours

ROLLBACK MECHANISM:
• Kubernetes: revert to previous ReplicaSet (instant, < 30 seconds)
• Configuration: feature flag disable (sub-second)
• Model: serving previous model version (always kept warm in GPU memory)
• Database schema: additive only — never destructive in same release
```

### Testing in Production (When and How)

```
Shadow Traffic (safest):
• Every new fraud model runs in shadow mode for 2 weeks minimum
• Scores real transactions but doesn't affect decisions
• Compare: new model decisions vs. current production model
• Graduate to canary only after shadow metrics pass
• Learned this from Broadcom — shadow deployment would have caught FP spike

Canary Deploys:
• 1% traffic for 30 minutes (automated)
• Monitoring: latency, error rate, GPU utilization, model accuracy
• Auto-rollback threshold: any metric > 2σ from baseline

Feature Flags:
• New LLM capabilities shipped dark, enabled per-tenant
• Internal users first, then 5% customers, then 50%, then 100%
• Kill switch: < 1 second to disable (config flag, not deployment)

NEVER in Production:
• Untested model without shadow period (Broadcom lesson)
• Destructive data changes without backup verified
• Load testing without Ops awareness
• Changes to PCI-DSS-scope components without compliance review
```

---

## THEME E: Technical Debt & On-Call

### Technical Debt Management (Python → Rust Migration)

```
THE REAL DEBT SITUATION (Capital One):

Deliberate + Prudent debt:
• "We shipped Tier 2/3 in Python for speed — will migrate guardrails to Rust
  once we validate the product value." (Migrated in Q4 after proving value)

High-interest debt (fixed):
• Python serialization tax: 15-40ms per transaction
  - Interest: $Y/month in wasted GPU idle time
  - Principal: 3 engineers × 6 weeks to build Rust gateway
  - ROI: paid back in < 2 months from GPU cost savings
  - Decision: FIX NOW (high interest, moderate principal)

Low-interest debt (left alone):
• Legacy monitoring scripts (bash + awk for some metrics)
  - Interest: 10 minutes/week of engineer time for manual correlation
  - Principal: 2 weeks to rewrite in proper observability stack
  - Decision: BOY SCOUT (improve incrementally when touching nearby code)

Debt we deliberately took:
• KV-cache routing uses consistent hashing without virtual node rebalancing
  - Interest: slightly uneven load distribution (< 5% variance)
  - Will fix when: scaling to 5× current capacity requires better balance
  - Decision: TRACK (acceptable now, plan for future)

DEBT REGISTER PROCESS:
• Every post-mortem generates debt items (tied to real incidents)
• Quarterly "interest rate review" — is debt getting worse?
• 20% of sprint capacity reserved for debt reduction
• Dashboard showing debt trends visible to leadership
• Never frame as "debt vs. features" — frame as "sustainable velocity"
```

### On-Call Excellence (Real Implementation)

```
FRAUD PLATFORM ON-CALL:

Structure:
• Primary: responds within 5 minutes (pager + phone)
• Secondary: backup + escalation (responds within 15 minutes)
• Rotation: 1 week, 8 engineers in rotation
• Follow-the-sun: US-East (EST hours) + US-West (PST hours)
• Compensation: on-call pay + comp day after rotation

Current metrics:
• Pages per week: ~1.5 (target: < 2) ✓
• MTTA: 3 minutes average (target: < 5) ✓
• MTTR: 11 minutes for P1 (target: < 30) ✓
• Active time per rotation: ~3 hours (target: < 4) ✓
• Toil percentage: ~25% (target: < 30) ✓

What makes it sustainable:
• Every alert has a linked runbook
• 3 common issues are auto-remediated (no human needed):
  1. Feature store compaction during peak → auto-pause and reschedule
  2. GPU memory > 85% → auto-evict lowest-priority KV-cache entries
  3. Single backend unhealthy → circuit breaker + auto-remove from routing
• If same issue pages 3+ times without fix → escalated as P1 reliability debt

Runbook format:
├── Symptom: "Tier 1 p99 > 8ms for > 2 minutes"
├── Likely causes (ordered by probability):
│   1. Feature store cache miss spike (check miss rate dashboard)
│   2. GPU thermal throttling (check nvidia-smi)
│   3. Kafka consumer lag (check consumer lag metric)
├── Diagnosis steps: [specific commands and dashboards]
├── Mitigation: [specific actions, e.g., "restart feature store compaction"]
├── Escalation: "If not resolved in 15 min → page secondary + team lead"
└── Post-incident: "File post-mortem template within 24h"

Quarterly review:
• Review all pages — were they actionable? (if not, delete the alert)
• Identify top 3 toil items → automate next quarter
• Update runbooks with new incident patterns
• On-call happiness survey (anonymous)
```

### Postmortem Culture (Built at Capital One)

```
BLAMELESS POSTMORTEM PROCESS:

Real example — Feature Store Compaction Incident:

Timeline (facts only, no judgments):
• T+0: Alert fires: p99 > 8ms
• T+2min: On-call investigates. GPU normal. Network normal.
• T+5min: Feature store response 4× normal
• T+8min: Root cause: compaction job running during peak (timezone config error)
• T+10min: Mitigation: paused compaction manually
• T+12min: Recovered to 3.2ms p99

Contributing Factors (NOT "root causes"):
1. Config migration from previous DC retained UTC timezone
2. No automated test for "maintenance jobs during peak"
3. Feature store lacked IO priority scheduling
4. Alert threshold at 8ms (impact started at 6ms)

Action Items (all have owner + deadline + ticket):
1. [P0] Peak-hour guard on maintenance jobs — Platform team, 1 week
2. [P1] IO priority scheduling in feature store — Data team, 2 weeks
3. [P1] Lower alert threshold to 6ms — SRE, 2 days
4. [P2] Timezone validation in config migration tool — Platform, 1 sprint
5. [P2] Chaos test: "compaction during peak" — Reliability, 1 sprint

Cultural practices I built:
• Weekly "incident review" open to ALL engineers (30 min, optional)
• Postmortem template that guides blameless language
• "Contributing factors" (not "root cause") — forces systemic thinking
• Quarterly "patterns" report: "3 of last 5 incidents relate to maintenance scheduling"
• New hires read recent postmortems as onboarding material
• I shared MY OWN failures first (Broadcom model incident) — leaders go first

Key principle: "A human made an error" → "The system allowed an error"
The person who caused the incident often has the best insight into how to prevent it.
Punishing them destroys that insight.
```

---

## Discussion Guide: How to Navigate Each Topic

### When Asked "Build vs. Buy?"

1. State your framework (differentiator? SLA? TCO? team capability?)
2. Give your real decision with specific numbers
3. Acknowledge what you'd do differently at different scale
4. Show you've considered the 5-year horizon, not just launch

### When Asked About Monitoring

1. Start with Layer 1 (business metrics) — shows you think from user impact down
2. Show your real SLO implementation with specific numbers
3. Walk through a real debugging example (feature store → 1% of traffic → 9 min to identify)
4. Discuss cost management (tiered storage, sampling strategies)

### When Asked About Reliability

1. Lead with your degradation hierarchy (5 levels at Capital One)
2. Show multi-region with specific consistency model and failover timing
3. Discuss capacity planning with real growth numbers
4. Give the feature store incident as a concrete example of the philosophy in action

### When Asked About Testing/Deployment

1. Start with what you DON'T test (shows maturity)
2. Walk through your real deployment pipeline (canary → regional → global)
3. Emphasize the Broadcom lesson: shadow deployment is non-negotiable for ML
4. Discuss chaos engineering with specific monthly exercises

### When Asked About Debt/On-Call

1. Categorize debt with real examples (high interest → fix, low interest → leave)
2. Show the Python → Rust migration as a "high interest, justified principal" example
3. Give real on-call metrics (1.5 pages/week, 11 min MTTR)
4. Discuss postmortem culture — share the feature store example in detail

---

## Red Flags to Avoid

| Red Flag | Fix |
|----------|-----|
| Textbook answers without real examples | Ground every answer in Capital One / Apple / Broadcom |
| "We should monitor everything" | Show prioritization: what you DON'T alert on matters |
| Over-engineering for hypothetical scale | Right-size: "At our scale X is appropriate; at LinkedIn's scale I'd do Y" |
| Ignoring organizational factors in build/buy | Always include: team capability, on-call burden, knowledge loss risk |
| Claiming zero incidents | Share the feature store incident — shows maturity and real operational experience |
| No cost awareness | Include dollar amounts: $2.1M GPU, $500K Datadog alternative, etc. |
