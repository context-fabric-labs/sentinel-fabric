# PART 4 — CRAFTSMANSHIP ROUND

## Overview

The Craftsmanship round evaluates your technical depth, engineering excellence, and operational maturity. At Senior Staff level, you're expected to demonstrate:
- Deep systems thinking about reliability and operations
- Mature perspectives on build vs. buy decisions
- Sophisticated monitoring and observability strategies
- Testing philosophies that scale
- Postmortem culture and learning organizations

---

## Build vs. Buy Discussions

### Discussion 1: Message Queue — Build Kafka-like System vs. Use Managed Service

**Scenario:** "Your organization needs a high-throughput event streaming platform. Do you build your own or use a managed service like Confluent/AWS MSK?"

**Senior Staff Answer Framework:**

```
EVALUATION CRITERIA:
├── Scale Requirements
│   ├── Throughput: >1M events/sec → custom may be justified
│   ├── Latency: sub-ms → custom may be needed
│   └── Retention: years of data → cost optimization matters
├── Organizational Capability  
│   ├── Do we have Kafka experts? (3+ engineers minimum)
│   ├── Can we maintain 24/7? (on-call rotation)
│   └── Can we keep up with security patches?
├── Total Cost of Ownership (5-year horizon)
│   ├── Build: engineers × salary + infrastructure + operational cost
│   ├── Buy: license + infrastructure + integration cost
│   └── Hidden costs: recruitment, knowledge loss, incident cost
├── Strategic Value
│   ├── Is this a differentiator? (probably not for most companies)
│   ├── Does this give us capabilities we can't buy?
│   └── Does our scale make managed services cost-prohibitive?
└── Risk
    ├── Vendor lock-in risk (can we migrate?)
    ├── Operational risk (bus factor of internal team)
    └── Feature gap risk (vendor doesn't support what we need)
```

**Your Position (with Capital One context):**
"At Capital One's scale (24,500 TPS fraud decisioning), we chose managed Kafka (MSK) for our event backbone but built custom consumers with exactly-once processing semantics because the managed consumer groups couldn't meet our latency requirements. The key insight: buy the commodity layer, build the differentiation layer."

---

### Discussion 2: Observability Platform — Build vs. Datadog/New Relic

**Scenario:** "You have 2000+ microservices. Should you build an internal observability platform or use Datadog?"

**Senior Staff Analysis:**

| Factor | Build | Buy (Datadog) |
|--------|-------|---------------|
| Cost at scale | $2-5M/year (team + infra) | $5-15M/year (at 2000+ services) |
| Time to value | 6-12 months | 2-4 weeks |
| Customization | Unlimited | Limited by vendor roadmap |
| Maintenance burden | High (24/7 ops team needed) | Low (vendor responsibility) |
| Data sovereignty | Full control | Vendor has your data |
| Integration | Perfect fit for your stack | Generic adapters |
| Talent | Need specialized hires | Standard tooling |

**Recommendation Framework:**
- **< 100 services:** Buy. Not worth the investment.
- **100-500 services:** Buy, but plan for cost negotiation leverage.
- **500-2000 services:** Hybrid. Buy the UI/query layer, build the data pipeline.
- **2000+ services:** Evaluate building core platform with OSS (Prometheus + Thanos + Grafana + Jaeger), custom data pipeline.

**LinkedIn Context:** LinkedIn built their own (inGraphs, inTrace) because at their scale (hundreds of thousands of metrics endpoints), vendor costs would exceed $50M+/year. But they've been building this for 10+ years.

---

### Discussion 3: Service Mesh — Istio vs. Custom

**Scenario:** "You're standardizing service-to-service communication. Service mesh or custom middleware?"

**Senior Staff Answer:**
"The question isn't 'build vs. buy service mesh' — it's 'do we need a service mesh at all?'

Assessment criteria:
1. **Number of services:** < 50 services → no mesh needed. Library-based approach (gRPC interceptors).
2. **Polyglot services:** If all Java → library approach. If 5+ languages → sidecar approach.
3. **Security requirements:** mTLS everywhere mandatory? Mesh simplifies this significantly.
4. **Traffic management complexity:** Advanced routing, canary, mirroring? Mesh excels here.

If mesh IS justified:
- Istio: Powerful but operationally complex. Requires dedicated team of 3-5.
- Linkerd: Simpler, less features. Good for organizations that want mesh without the operational tax.
- Custom: Only if you have a very specific requirement no mesh provides (sub-100μs overhead, custom protocols).

At Capital One, we chose Istio with a dedicated platform team because regulatory requirements mandated mTLS everywhere and full audit trails of service-to-service communication."

---

### Discussion 4: Feature Store — Build vs. Feast/Tecton

**Scenario:** "Your ML teams need a feature store. Build custom or adopt existing solution?"

**Senior Staff Answer:**

"Key evaluation dimensions:
1. **Online serving latency:** Do you need <5ms? Custom store on Redis/DynamoDB may be needed.
2. **Feature freshness:** Real-time features (seconds) vs. batch (hours)?
3. **Scale:** Number of features, QPS for serving, volume of historical data.
4. **Team maturity:** Do your ML teams have strong engineering skills?

At Fiserv, we built a custom feature store for the AI underwriting platform because:
- Required sub-3ms serving latency for real-time decisioning
- Needed tight integration with our vLLM inference pipeline
- Regulatory requirement for feature lineage and auditability
- The commercial options didn't support our compliance constraints

We used Feast's offline store concepts but built custom online serving on Redis Cluster with purpose-built serialization. Total investment: 3 engineers × 4 months."

---

## Monitoring Strategy Discussions

### Discussion 1: Monitoring Philosophy for a Platform

**Question:** "How do you design monitoring for a platform serving 20+ teams?"

**Senior Staff Answer:**

```
MONITORING STRATEGY
═══════════════════

Layer 1: Business Metrics (WHY we exist)
├── Revenue-impacting metrics
├── User-facing SLOs  
├── Business KPIs that depend on our platform
└── Example: "Fraud decisions processed per second", "Decision accuracy"

Layer 2: Service Metrics (HOW we're performing)
├── RED metrics (Rate, Errors, Duration) per service
├── SLI/SLO tracking with error budgets
├── Dependency health
└── Example: "Inference latency p99", "Model serving error rate"

Layer 3: Infrastructure Metrics (WHAT we're running on)
├── Resource utilization (CPU, memory, disk, network)
├── Capacity headroom
├── Infrastructure SLOs
└── Example: "GPU utilization", "Kafka consumer lag"

Layer 4: Operational Metrics (HOW we're operating)
├── Deployment frequency and success rate
├── Change failure rate
├── MTTR, MTTD
└── Example: "Deployments per day", "Rollback rate"

ALERTING PHILOSOPHY:
- Alert on symptoms (user impact), not causes (CPU high)
- Every alert must be actionable (if you can't act, don't alert)
- Page only for customer-impacting issues
- Use error budgets to avoid over-alerting on transient issues
- Multi-signal alerting (don't page on single metric anomaly)

OWNERSHIP MODEL:
- Platform team owns Layer 2-4
- Product teams own Layer 1 (with platform team support)
- Shared on-call for cross-cutting issues
- Clear escalation paths with runbooks
```

---

### Discussion 2: SLO-Based Monitoring

**Question:** "Walk me through implementing SLO-based monitoring for a critical service."

**Senior Staff Answer:**

```
STEP 1: Define SLIs (Service Level Indicators)
─────────────────────────────────────────────
- Availability: % of requests served successfully
- Latency: % of requests served within threshold
- Correctness: % of requests returning correct results
- Freshness: % of data within acceptable age

STEP 2: Set SLOs (Service Level Objectives)
─────────────────────────────────────────────
- Based on user tolerance, not technical capability
- Example: 99.9% of requests < 100ms, 99.95% availability
- Set aspirational AND minimum SLOs
- Window: 30-day rolling (not monthly calendar)

STEP 3: Calculate Error Budget
─────────────────────────────────────────────
- 99.9% availability = 43.2 minutes of downtime per 30 days
- Track burn rate: how fast are we consuming budget?
- Fast burn (100x) = page immediately
- Slow burn (10x) = ticket for investigation

STEP 4: Implement Error Budget Policies
─────────────────────────────────────────────
- Budget remaining > 50%: Normal development velocity
- Budget remaining 20-50%: Increased caution, more testing
- Budget remaining < 20%: Feature freeze, reliability focus
- Budget exhausted: All hands on reliability until recovered

STEP 5: Alerting on Burn Rate (Multi-Window)
─────────────────────────────────────────────
- 1-hour burn rate > 14.4 AND 5-min burn rate > 14.4 → PAGE
- 6-hour burn rate > 6 AND 30-min burn rate > 6 → PAGE  
- 3-day burn rate > 1 AND 6-hour burn rate > 1 → TICKET

CAPITAL ONE EXAMPLE:
For our fraud decisioning platform:
- SLI: Decisions rendered within 5ms at p99
- SLO: 99.99% of decisions within SLO (4.3s of violation per 12 hours)
- Error budget policy: Any budget consumption >50% in 1 hour triggers
  automatic traffic failover to backup decisioning path
```

---

## Reliability Discussions

### Discussion 1: Designing for Failure

**Question:** "How do you design a system that handles partial failures gracefully?"

**Senior Staff Answer:**

```
RELIABILITY DESIGN PRINCIPLES
═══════════════════════════════

1. ISOLATION (Blast Radius Reduction)
   ├── Bulkheads: Separate thread pools per dependency
   ├── Cell-based architecture: Independent failure domains
   ├── Sharding: Limit impact to fraction of users
   └── Example: "At Apple Siri, each pipeline stage had independent
       failure domains. ASR failure degraded gracefully to text-only,
       not total outage."

2. REDUNDANCY (No Single Points of Failure)
   ├── Data: Multi-region replication, quorum writes
   ├── Compute: Multiple AZs, auto-scaling
   ├── Dependencies: Multiple providers where possible
   └── Example: "Fraud platform: primary GPU cluster in us-east-1,
       warm standby in us-west-2, CPU fallback as last resort."

3. GRACEFUL DEGRADATION (Fail Soft, Not Hard)
   ├── Define degradation hierarchy
   │   Level 0: Full functionality
   │   Level 1: Reduced features (disable non-critical)
   │   Level 2: Read-only / cached responses
   │   Level 3: Static fallback
   │   Level 4: Maintenance page (last resort)
   ├── Each level has automatic triggers and manual overrides
   └── Example: "Under load, fraud system degrades: ML model → 
       rule-based fallback → default-allow with logging"

4. TIMEOUTS AND DEADLINES (Don't Wait Forever)
   ├── Every network call has a timeout
   ├── Deadline propagation (budget remaining decreases through chain)
   ├── Timeout < retry budget (allow at least one retry)
   └── Example: "5ms total budget: 2ms for feature fetch, 2ms for
       inference, 1ms for response assembly"

5. CIRCUIT BREAKERS (Stop Cascading)
   ├── Monitor failure rate per dependency
   ├── Open circuit when failure rate exceeds threshold
   ├── Half-open: periodically test if dependency recovered
   ├── Fallback behavior when circuit is open
   └── Example: "Circuit breaker on model serving: if >5% error rate
       for 30s, switch to rule-based fallback"

6. LOAD SHEDDING (Protect Yourself)
   ├── Admission control: reject requests at system boundary
   ├── Priority-based: shed low-priority first
   ├── Client-side backoff: 429 + Retry-After header
   └── Example: "Fraud platform prioritizes: real-time transactions >
       batch scoring > analytics queries"
```

---

### Discussion 2: Multi-Region Architecture

**Question:** "Design a multi-region strategy for a latency-sensitive platform."

**Senior Staff Answer:**

```
MULTI-REGION ARCHITECTURE DECISION TREE
═════════════════════════════════════════

QUESTION 1: Why multi-region?
├── Latency: Users in multiple geographies need < Xms
├── Availability: Survive region-level failures
├── Compliance: Data sovereignty requirements
└── Capacity: Single region can't handle load

QUESTION 2: What's the consistency model?
├── Strong consistency across regions: Expensive, high latency
├── Eventual consistency: Easy, but application must handle
├── Per-entity consistency: Assign entities to primary region
└── Causal consistency: Good middle ground (session-scoped)

ARCHITECTURE OPTIONS:
─────────────────────
Option A: Active-Passive
- One primary region, others are hot standby
- Simple, strong consistency possible
- Wastes standby capacity
- Failover takes minutes

Option B: Active-Active (with region affinity)
- All regions serve traffic
- Requests routed to "owner" region for writes
- Cross-region reads (eventual consistency)
- Complex but efficient

Option C: Active-Active (multi-master)
- All regions accept writes
- Conflict resolution needed (LWW, CRDTs, application logic)
- Highest availability, highest complexity
- Best for eventually consistent workloads

CAPITAL ONE FRAUD PLATFORM APPROACH:
- Active-Active with region affinity
- Transaction decisioning: served in nearest region
- Model state: replicated from training region
- Feature store: multi-region Redis with local reads
- Fallback: if primary region fails, secondary assumes all traffic
  within 30 seconds (DNS failover + connection draining)
```

---

### Discussion 3: Capacity Planning

**Question:** "How do you approach capacity planning for a high-growth platform?"

**Senior Staff Answer:**

```
CAPACITY PLANNING FRAMEWORK
════════════════════════════

1. UNDERSTAND CURRENT STATE
   ├── Current peak utilization (CPU, memory, network, disk I/O)
   ├── Growth rate (weekly/monthly trends)
   ├── Seasonality (daily, weekly, annual patterns)
   ├── Headroom (current spare capacity)
   └── Bottleneck analysis (which resource exhausts first?)

2. PROJECT FUTURE DEMAND
   ├── Organic growth: extrapolate from trends (linear/exponential)
   ├── Planned growth: new features, user growth targets, marketing
   ├── Unplanned peaks: viral events, incidents, load spikes (2-3x)
   └── Always plan for 2x expected peak (safety margin)

3. IDENTIFY SCALING LIMITS
   ├── What breaks first? (usually: database, network, specific service)
   ├── At what load level? (stress test to find actual limits)
   ├── What's the remediation time? (can you scale fast enough?)
   └── Are there hard ceilings? (single-node limits, license limits)

4. PLAN CAPACITY ADDITIONS
   ├── Lead time for capacity (instant for cloud, weeks for hardware)
   ├── Cost curve (linear cost? step function? volume discounts?)
   ├── Auto-scaling where possible (elastic capacity)
   ├── Pre-provisioned for known events (holiday, launch)
   └── Reserved capacity for critical workloads

5. MONITOR AND ITERATE
   ├── Weekly capacity dashboards
   ├── Alerts at 60% utilization (action) and 80% (critical)
   ├── Monthly capacity review with projected runway
   ├── Quarterly planning for infrastructure budget
   └── Continuous load testing to validate projections

EXAMPLE (FRAUD PLATFORM):
- Current: 24,500 TPS peak, 40% GPU utilization
- Growth: 15% QoQ in transaction volume
- Ceiling: Current GPU fleet saturates at ~45,000 TPS  
- Plan: Pre-provision additional GPU capacity at 70% utilization
- Failsafe: CPU-based fallback can absorb 20% overflow at degraded latency
```

---

## Observability Discussions

### Discussion 1: Three Pillars + Beyond

**Question:** "What's your observability philosophy for complex distributed systems?"

**Senior Staff Answer:**

```
OBSERVABILITY MATURITY MODEL
═════════════════════════════

Level 1: Monitoring (Reactive)
├── Metrics: CPU, memory, disk, basic app metrics
├── Logs: Centralized log aggregation
├── Alerts: Threshold-based alerting
└── Limitation: Can only find known failure modes

Level 2: Observability (Proactive)
├── Distributed tracing: End-to-end request flow
├── Structured logging: Queryable, correlated
├── High-cardinality metrics: Per-endpoint, per-tenant
├── SLO-based alerting: Business-impact focused
└── Limitation: Still requires human investigation

Level 3: Intelligent Observability (Predictive)  
├── Anomaly detection: ML-based baseline deviation
├── Correlation: Automatic root cause candidates
├── Dependency mapping: Auto-discovered service topology
├── Predictive alerts: "Will breach SLO in 2 hours at current rate"
└── Limitation: Requires significant investment

TRACING STRATEGY (AT APPLE SIRI SCALE):
════════════════════════════════════════
- 100% tracing for errors
- 10% sampling for normal traffic
- Head-based sampling for pre-committed traces
- Tail-based sampling for interesting traces (slow, error, new deploy)
- Trace context propagation through all async boundaries
- Custom spans for business logic (not just RPC boundaries)
- Baggage items for cross-cutting context (user segment, experiment)

COST MANAGEMENT:
════════════════
- Observability data grows faster than production data
- Tiered storage: hot (7 days, fast query) → warm (30 days) → cold (1 year)
- Sampling strategies to control volume without losing visibility
- Aggregation at edge to reduce central storage
- Team-based quotas with chargeback model
```

---

### Discussion 2: Debugging Production Issues

**Question:** "Walk through how you debug a latency regression that only affects 1% of users."

**Senior Staff Answer:**

```
SYSTEMATIC DEBUGGING APPROACH
══════════════════════════════

Step 1: Characterize the Problem (5 minutes)
├── When did it start? (correlate with deploys, config changes)
├── Which users? (geography, device type, account age, segment)
├── Which paths? (specific endpoints, features)
├── How bad? (p99 latency increase, absolute numbers)
└── Pattern? (constant? periodic? growing?)

Step 2: Hypothesis Generation (5 minutes)
├── Deploy correlation → recent code change
├── Time correlation → external dependency issue
├── User segmentation → specific data pattern
├── Infrastructure → capacity, noisy neighbor, GC
└── 1% = often the long tail: large payloads, cold caches, specific shards

Step 3: Investigate (guided by hypotheses)
├── Pull traces for affected vs. unaffected users
├── Compare: what's different about the slow requests?
├── Look at span breakdown: which component is slow?
├── Check dependency latencies: upstream? downstream?
├── Examine host distribution: same hosts? or random?

Step 4: Root Cause
├── Verify with counterfactual: "If I'm right, then X should also be true"
├── Reproduce in staging if possible
└── Quantify impact precisely before fixing

Step 5: Fix and Prevent
├── Immediate mitigation (if possible)
├── Root cause fix
├── Add monitoring for this specific failure mode
├── Retrospective: why didn't we catch this sooner?

REAL EXAMPLE (FRAUD PLATFORM):
1% of transactions had 12ms latency (SLO: 5ms).
- Characterization: Only for new merchants (< 30 days).
- Hypothesis: Feature store cache miss for new entities.
- Investigation: Traces showed 8ms feature hydration for cold entities.
- Root cause: New merchants had no pre-warmed feature vectors.
- Fix: Background job to pre-compute features at merchant onboarding.
- Prevention: Alert on cache miss rate by entity age cohort.
```

---

## Testing Strategy Discussions

### Discussion 1: Testing Philosophy for Infrastructure

**Question:** "What's your testing strategy for a critical infrastructure platform?"

**Senior Staff Answer:**

```
TESTING PYRAMID FOR INFRASTRUCTURE
═══════════════════════════════════

              ┌──────────┐
              │ Chaos/   │  ← Quarterly game days
              │ Gamedays │
              ├──────────┤
              │ Load/    │  ← Weekly automated
              │ Perf     │
           ┌──┴──────────┴──┐
           │ Integration/   │  ← Daily in staging
           │ Contract Tests │
        ┌──┴────────────────┴──┐
        │   Component Tests    │  ← Every PR
     ┌──┴──────────────────────┴──┐
     │      Unit Tests            │  ← Every commit
     └────────────────────────────┘

KEY PRINCIPLES:
1. Test at the right level (don't integration-test what should be unit-tested)
2. Contract tests between services (consumer-driven contracts)
3. Performance tests as gates (not just functional correctness)
4. Chaos engineering for unknown-unknowns
5. Test in production (carefully): canary deploys, feature flags, shadowing

INFRASTRUCTURE-SPECIFIC TESTING:
════════════════════════════════
- Configuration testing: Validate configs against schema before deploy
- Upgrade testing: Test upgrade path, not just clean install
- Failure injection: Network partitions, disk full, clock skew
- Data migration testing: Validate data integrity post-migration
- Rollback testing: Verify you can actually roll back
- Scale testing: Test at 2x current load regularly

WHAT I DO NOT TEST:
════════════════════
- Third-party libraries' internal logic (trust, but verify at boundary)
- Every permutation of configuration (test boundaries and defaults)
- Things that can't fail in production (type-safe code, compiler-enforced)

FRAUD PLATFORM TESTING STRATEGY:
════════════════════════════════
- Unit: Model inference correctness, feature transformation logic
- Integration: End-to-end decision pipeline with test transactions
- Performance: Nightly load test at 2x peak (49,000 TPS)
- Chaos: Monthly: kill GPU nodes, network partition feature store
- Shadow: New models shadow-score real traffic before promotion
- Canary: New code serves 1% traffic for 1 hour before full rollout
```

---

### Discussion 2: Testing in Production

**Question:** "When and how do you test in production?"

**Senior Staff Answer:**

```
TESTING IN PRODUCTION SPECTRUM
══════════════════════════════

SAFE ←────────────────────────────────────→ RISKY

Monitoring  Canary  Shadow   Feature   A/B    Chaos
  Only     Deploy  Traffic   Flags    Test   Engineering

WHEN TO TEST IN PRODUCTION:
- Can't replicate production scale in staging
- Can't replicate production data patterns
- Can't replicate production traffic mix
- Need to validate real user behavior
- Need to verify production configuration

HOW TO DO IT SAFELY:
1. Shadow Traffic: Replay production requests to new system, compare results
   - No user impact (read-only against new system)
   - Great for validating correctness at scale
   
2. Canary Deploys: Route small % of traffic to new version
   - Automated rollback on SLO violation
   - Gradually increase: 1% → 5% → 25% → 50% → 100%
   - Bake time at each stage (minimum 30 minutes per stage)

3. Feature Flags: Ship code dark, enable for specific cohorts
   - Internal users first
   - Beta users second
   - General availability last
   - Kill switch for immediate disable

4. Chaos Engineering: Deliberately inject failures
   - Start in staging, graduate to production
   - Start during business hours (experts available)
   - Have automatic stop conditions
   - Document expected vs. actual behavior

NEVER DO IN PRODUCTION:
- Destructive data operations without backup
- Untested code without a rollback path
- Load testing without stakeholder awareness
- Experiments that could affect financial transactions without safeguards
```

---

## Postmortem Discussions

### Discussion 1: Postmortem Culture

**Question:** "How do you build a blameless postmortem culture?"

**Senior Staff Answer:**

```
BLAMELESS POSTMORTEM PRINCIPLES
═══════════════════════════════

1. BLAME THE SYSTEM, NOT THE PERSON
   - "A human made an error" → "The system allowed an error to be made"
   - Focus on: What enabled the failure? What safeguards were missing?
   - People are not root causes. They are part of the system.

2. PSYCHOLOGICAL SAFETY IS PREREQUISITE
   - Leaders go first (share their own mistakes publicly)
   - No punishment for honest disclosure
   - Reward reporting near-misses
   - "Thank you for finding this" not "Why didn't you prevent this?"

3. STRUCTURED POSTMORTEM PROCESS
   ├── Timeline: What happened, when (facts only, no judgments)
   ├── Impact: Who was affected, how much, for how long
   ├── Root Cause: Why? (5 whys, but avoid stopping too early)
   ├── Contributing Factors: What made it worse?
   ├── What Went Well: What prevented greater impact?
   ├── Action Items: Specific, assigned, deadline, tracked
   └── Lessons Learned: What do we know now that we didn't before?

4. ACTION ITEMS THAT ACTUALLY GET DONE
   - Each action item has: owner, deadline, priority, tracking ticket
   - Review action items in team meetings until complete
   - Categorize: immediate mitigation vs. systemic prevention
   - Track completion rate as an org health metric
   - If action items consistently don't get done, escalate as staffing issue

5. SHARING AND LEARNING
   - Publish postmortems widely (not just within team)
   - Monthly "failure review" for cross-team learning
   - Pattern recognition across postmortems (systemic issues)
   - New hire reading: recent postmortems as onboarding material

WHAT I'VE BUILT (AT CAPITAL ONE):
- Weekly "incident review" open to all engineers
- Postmortem template that guides blameless language
- "Contributing factors" section that captures systemic issues
- Quarterly "patterns" report identifying repeat themes
- Tied reliability improvements to postmortem trends (data-driven roadmap)
```

---

### Discussion 2: A Detailed Postmortem Example

**Question:** "Walk me through a significant production incident you managed."

**Senior Staff Story (Capital One Fraud Platform):**

```
INCIDENT: Fraud Decisioning Latency Degradation
════════════════════════════════════════════════

TIMELINE:
- T+0: Automated alert fires: p99 latency > 8ms (SLO: 5ms)
- T+2min: On-call investigates. GPU utilization normal. Network normal.
- T+5min: Identified: Feature store response time 4x normal
- T+8min: Root cause identified: Feature store compaction job
         running during peak hours due to timezone config error
- T+10min: Mitigation: Paused compaction job manually
- T+12min: Latency recovered to 3.2ms p99
- T+30min: Verified all queued transactions processed successfully

IMPACT:
- Duration: 12 minutes
- Transactions affected: ~18,000 (12 min × 24,500 TPS × affected %)
- SLO violation: 0.003% of daily error budget consumed
- Customer impact: ~200 transactions experienced >10ms delay
- Financial impact: Zero (no incorrect decisions, only delayed)

ROOT CAUSE:
Compaction job scheduled in UTC, but peak traffic is EST.
Config migration from previous DC retained UTC timezone without adjustment.
No guard preventing compaction during peak traffic windows.

CONTRIBUTING FACTORS:
1. Config migration validation didn't include timezone verification
2. No automated test for "maintenance jobs don't run during peak"
3. Feature store didn't have IO priority scheduling
4. Alert fired at 8ms, but impact started at 6ms (threshold too generous)

WHAT WENT WELL:
- Automated alerting detected within 2 minutes
- On-call had clear runbook for feature store issues
- Mitigation was fast and effective
- No data loss or incorrect decisions

ACTION ITEMS:
1. [P0] Add peak-hour guard to all maintenance jobs (Owner: Platform, 1 week)
2. [P1] Implement IO priority scheduling in feature store (Owner: Data, 2 weeks)
3. [P1] Lower alert threshold to 6ms (Owner: SRE, 2 days)
4. [P2] Add timezone validation to config migration tool (Owner: Platform, 1 sprint)
5. [P2] Create chaos test: "compaction during peak" (Owner: Reliability, 1 sprint)

LESSONS LEARNED:
- Timezone handling in configs is a class of bugs, not a one-off
- Maintenance operations should be treated as potential incidents
- "Works in staging" != "Works in production" when timezones differ
- Created team-wide "config migration checklist" including timezone verification
```

---

## Deep Craftsmanship Examples

### Example 1: Deployment Strategy at Scale

**Question:** "How do you deploy to 10,000 instances without impacting users?"

```
PROGRESSIVE DEPLOYMENT STRATEGY
════════════════════════════════

Phase 0: Pre-deployment
├── Automated tests pass (unit, integration, performance)
├── Security scan clean
├── Config validation pass
├── Deployment plan reviewed (for high-risk changes)
└── Rollback plan validated

Phase 1: Canary (1% — 100 instances)
├── Deploy to canary fleet
├── Compare all metrics against baseline fleet
├── Automated rollback if: error rate > baseline + 0.1%, latency > baseline + 10%
├── Bake time: 30 minutes minimum
└── Human approval for Phase 2

Phase 2: Regional Rollout (25% — one region)
├── Full region deployment
├── Monitor cross-region comparison
├── Bake time: 1 hour
├── Verify region health dashboards
└── Automated rollback if SLO violated

Phase 3: Global Rollout (100%)
├── Remaining regions in sequence (not parallel)
├── 15 minutes between each region
├── Monitoring continues for 4 hours post-completion
└── Rollback remains available for 24 hours

ROLLBACK CRITERIA (AUTOMATED):
- Error rate increase > 0.5% sustained for 5 minutes
- Latency p99 increase > 20% sustained for 5 minutes
- Any single error type rate > 1%
- Memory leak detected (monotonic growth without plateau)

ROLLBACK MECHANISM:
- Kubernetes: revert to previous ReplicaSet (instant)
- Configuration: feature flag disable (sub-second)
- Data migration: backward-compatible only (no destructive changes)
- Database schema: additive only, remove later

WHAT SENIOR STAFF DEMONSTRATES:
- Zero-downtime deployment is not just tooling, it's architecture
- Backward compatibility is a design requirement, not an afterthought
- Deployment is a feature that requires engineering investment
- Fast rollback is more important than perfect canary detection
```

---

### Example 2: On-Call Excellence

**Question:** "How do you build a sustainable on-call rotation for a critical platform?"

```
ON-CALL PHILOSOPHY
══════════════════

PRINCIPLES:
1. On-call should be boring (well-automated systems don't page often)
2. Every page should be actionable (if you can't act, don't page)
3. On-call burden should decrease over time (invest in automation)
4. On-call is a tax on the team — minimize it ruthlessly
5. Good on-call = good systems (on-call pain drives reliability investment)

STRUCTURE:
├── Primary: First responder (responds within 5 minutes)
├── Secondary: Backup + escalation (responds within 15 minutes)
├── Rotation: 1 week per engineer, minimum 6 people in rotation
├── Follow-the-sun for global services (no one wakes up)
├── Compensation: Additional compensation for on-call burden
└── Cap: Max 2 pages per week average (signal of healthy system)

RUNBOOKS:
├── Every alert has a linked runbook
├── Runbook format: Symptom → Diagnosis Steps → Mitigation → Escalation
├── Runbooks are tested regularly (chaos engineering validates them)
├── Runbooks evolve after every incident
└── If you can write it in a runbook, you should automate it

TOIL REDUCTION:
├── Track time spent on manual operations
├── Automate the top 3 toil items each quarter
├── Goal: <30% of on-call time is manual work
├── Auto-remediation for known issues (self-healing)
└── If same issue pages 3+ times without fix, escalate as P1 reliability debt

METRICS:
├── Pages per week (target: <2)
├── MTTA (mean time to acknowledge): <5 min
├── MTTR (mean time to resolve): <30 min for P1
├── Time spent on-call per rotation (target: <4 hours active)
├── Toil percentage (target: <30%)
└── Happiness survey (quarterly, on-call satisfaction)
```

---

### Example 3: Technical Debt Management

**Question:** "How do you systematically manage technical debt in a large platform?"

```
TECHNICAL DEBT MANAGEMENT FRAMEWORK
════════════════════════════════════

CATEGORIZATION:
├── Deliberate + Prudent: "We know this is debt, shipping now, will address in sprint 2"
├── Deliberate + Reckless: "We don't have time for tests" (NOT acceptable)
├── Inadvertent + Prudent: "Now we know better, should refactor"
└── Inadvertent + Reckless: "What's encapsulation?" (hiring/culture problem)

ASSESSMENT (DEBT REGISTER):
For each debt item:
├── Interest rate: How much ongoing cost? (incidents, velocity drag, onboarding pain)
├── Principal: How much to fix? (effort estimate)
├── Blast radius: What breaks if we don't fix it? (risk)
├── Trend: Getting worse, stable, or diminishing?
└── Payoff: What's unlocked by fixing? (beyond just removing pain)

PRIORITIZATION:
┌─────────────────┬───────────────────┬────────────────────┐
│ High Interest +  │ High Interest +   │ Track and plan     │
│ Low Principal    │ High Principal    │ (schedule in       │
│ = FIX NOW        │ = PLAN & INVEST   │  roadmap)          │
├─────────────────┼───────────────────┼────────────────────┤
│ Low Interest +   │ Low Interest +    │ Leave alone        │
│ Low Principal    │ High Principal    │ (don't fix what    │
│ = Boy scout rule │ = DON'T TOUCH     │  isn't broken)     │
└─────────────────┴───────────────────┴────────────────────┘

STRATEGIES:
1. 20% rule: Reserve 20% of sprint capacity for debt reduction
2. Boy scout rule: Leave code better than you found it (incremental)
3. Strangler fig: Replace components incrementally, not big-bang
4. Debt sprints: Occasional full-sprint focus on debt reduction
5. Tie to incidents: Every postmortem generates debt items
6. Make it visible: Dashboard showing debt trends to leadership
```
