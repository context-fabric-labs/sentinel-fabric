# Layer 8: LLM Platform Operations, Governance & Security — Complete Deep Dive

## "How do we operate, govern, and secure LLM serving at enterprise scale?"

**Prerequisite docs:**
- 6-Inference.md → technical serving optimization
- coupang_principal_ai_security_interview_prep.md → deep security architecture
- 3-Platform.md → K8s GPU orchestration, multi-tenancy

**This doc covers:** the operational envelope around inference — everything needed to
run LLM serving as a governed, multi-tenant, auditable, secure enterprise platform.

---

# SECTION 1: MULTI-TENANT LLM PLATFORM ARCHITECTURE

---

## 1.1 The Multi-Tenant Challenge for LLM Serving

```
Enterprise LLM platform serves MULTIPLE consumers:
  - Internal teams (search, fraud, customer support)
  - External customers (API access, white-label)
  - Environments (prod, staging, dev, shadow)

Each tenant has different:
  - SLA (latency, availability)
  - Security posture (PII handling, data residency)
  - Cost budget (GPU hours/month)
  - Model access (which models they can use)
  - Rate limits (tokens/min, requests/sec)
  - Compliance requirements (SOC2, PCI, HIPAA)

The platform must enforce ALL of these simultaneously
without one tenant impacting another.
```

## 1.2 Tenant Isolation Levels

```
LEVEL 1: LOGICAL ISOLATION (shared infrastructure)
  Multiple tenants share GPU nodes
  Isolation via: namespaces, network policies, resource quotas
  KV cache: separate per tenant (no shared state)
  
  Pros: cost efficient, simple operations
  Cons: noisy neighbor risk, shared blast radius
  Use for: internal teams, low-security workloads, dev/staging

LEVEL 2: DEDICATED COMPUTE (shared control plane)
  Each tenant gets dedicated GPU nodes
  Shared K8s control plane, separate node pools
  
  Pros: no noisy neighbor, predictable performance
  Cons: more expensive, some shared control plane risk
  Use for: production inference with SLA, different cost centers

LEVEL 3: FULL ISOLATION (separate clusters)
  Separate K8s clusters per tenant
  Separate networking, storage, secrets
  
  Pros: maximum isolation, independent blast radius
  Cons: operational overhead, expensive
  Use for: regulated industries (healthcare, finance), external customers

Decision matrix:
  | Factor | Level 1 | Level 2 | Level 3 |
  |--------|---------|---------|---------|
  | Cost per tenant | $ | $$ | $$$$ |
  | Noisy neighbor risk | medium | none | none |
  | Blast radius | shared | partial | isolated |
  | Compliance | internal only | SOC2 | PCI/HIPAA |
  | Operational overhead | low | medium | high |
```

## 1.3 Tenant Context Propagation

```
Every request MUST carry tenant context — from gateway to audit log:

  Request arrives → Gateway extracts:
    tenant_id:        "acme-corp"          (mandatory, non-removable)
    user_id:          "user-12345"
    roles:            ["inference.read", "model.llama-70b"]
    clearance:        "internal"
    cost_center:      "cc-4567"
    rate_limit_tier:  "enterprise"
    session_id:       "sess-abc123"

  This context is:
    1. Injected by gateway (never from client)
    2. Propagated via gRPC metadata / HTTP headers
    3. Validated at every service boundary
    4. Used for: routing, authorization, metering, audit
    5. Cannot be overridden by downstream services

  If tenant context is missing or invalid:
    → REJECT request immediately (fail-closed)
    → Never default to "system" or "admin" tenant
```

---

# SECTION 2: AI GATEWAY — POLICY ENFORCEMENT POINT

---

## 2.1 Gateway Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  AI GATEWAY (mandatory hop for ALL inference requests)           │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. AUTHENTICATION                                               │
│     • JWT validation (signature, expiry, issuer)                 │
│     • mTLS for service-to-service                               │
│     • API key validation (for external customers)               │
│     • Extract identity → resolve tenant context                  │
│                                                                  │
│  2. RATE LIMITING                                                │
│     • Per-tenant: tokens/min, requests/sec                      │
│     • Per-user: burst control                                    │
│     • Per-model: separate limits for expensive models            │
│     • Token budget enforcement (estimated before sending)        │
│                                                                  │
│  3. REQUEST CLASSIFICATION                                       │
│     • Route to correct model/endpoint                           │
│     • Priority assignment (latency-sensitive vs batch)           │
│     • Cost estimation (input tokens × model pricing)             │
│                                                                  │
│  4. ADMISSION CONTROL                                            │
│     • Check: does tenant have quota for this model?             │
│     • Check: is tenant within budget for this billing period?   │
│     • Check: is model available (not in maintenance)?           │
│                                                                  │
│  5. INPUT GUARDRAILS (pre-inference)                             │
│     • Prompt injection detection                                 │
│     • PII/secrets scanning                                       │
│     • Content policy enforcement                                 │
│     • Token length validation                                    │
│                                                                  │
│  6. FORWARDING                                                   │
│     • Route to inference backend with tenant context            │
│     • Attach trace ID, request ID, timing metadata              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘

Gateway requirements:
  Latency overhead: < 5 ms p99 (most checks are O(1) lookups)
  Availability: 99.99% (it's the front door)
  Throughput: handles full cluster traffic (10K+ req/s)
  
  Implementation options:
    Rust (Axum/Hyper): predictable latency, no GC pauses
    Envoy + WASM filter: standard, but heavier
    Go: decent latency, large ecosystem
```

## 2.2 Rate Limiting for LLM Inference

```
LLM rate limiting is DIFFERENT from traditional API rate limiting:

Traditional: limit requests/second (all requests ≈ same cost)
LLM: limit TOKENS/minute (requests vary 100× in cost!)

  Request A: 50 input tokens + 20 output tokens = 70 tokens
  Request B: 4,000 input tokens + 2,000 output = 6,000 tokens
  Request B is 85× more expensive!

Token-based rate limiting:
  1. ESTIMATE cost before forwarding:
     estimated_tokens = input_tokens + estimated_output_tokens
     (estimate output from max_tokens parameter or model default)
  
  2. CHECK budget:
     if (tenant.tokens_used_this_minute + estimated_tokens > tenant.limit):
         return 429 (Too Many Requests)
         include Retry-After header
  
  3. RECONCILE after response:
     actual_tokens = response.usage.total_tokens
     tenant.tokens_used_this_minute += actual_tokens
     (may differ from estimate — track and adjust)

Hierarchical rate limits:
  Per-tenant per-minute:   100,000 tokens/min (sustained)
  Per-tenant burst:        500,000 tokens (bucket allows burst)
  Per-user per-minute:     10,000 tokens/min
  Per-model:               separate limits for expensive models
  Global platform:         5,000,000 tokens/min (capacity protection)
```

## 2.3 Model Access Control

```
Not all tenants can access all models:

Model access matrix:
  | Model | Team A (Fraud) | Team B (Support) | External API |
  |-------|----------------|------------------|--------------|
  | Llama-70B | ✓ | ✓ | ✓ |
  | Internal-Fine-Tuned | ✓ | ✗ | ✗ |
  | GPT-4 (proxy) | ✓ | ✓ | ✗ |
  | Code-Llama | ✗ | ✗ | ✓ |

Enforcement:
  Gateway checks: tenant.allowed_models.contains(request.model)
  If not allowed: 403 (Forbidden) with clear error message
  
  Model permissions stored in:
    - OPA policy bundle (fast evaluation)
    - Or: Redis-backed permission store (< 1 ms lookup)
    - Synced from central IAM/RBAC system
```

---

# SECTION 3: INPUT/OUTPUT GUARDRAILS FOR PRODUCTION

---

## 3.1 Input Guardrail Pipeline

```
Every inference request passes through (in order):

1. TOKEN LENGTH CHECK (< 0.1 ms)
   if input_tokens > model.max_context - reserved_output:
       reject("Input too long")

2. PII / SECRETS DETECTION (1-5 ms)
   Scan for: credit cards, SSN, API keys, passwords, emails
   Action: REDACT (replace with [REDACTED]) or REJECT
   
   Implementation: regex + NER model (lightweight BERT)
   Configurable per-tenant:
     Tenant A (finance): reject on any PII
     Tenant B (support): allow PII (agent needs customer context)

3. PROMPT INJECTION DETECTION (2-10 ms)
   Layer 1: Regex patterns (fast, catches known attacks)
     "ignore previous", "disregard instructions", "you are now"
   Layer 2: ML classifier (trained on injection corpus)
     Score > 0.8 → BLOCK
     Score 0.5-0.8 → FLAG for review + allow with monitoring
   Layer 3: Structural analysis (instruction boundary violation)
   
   Fail-safe: if classifier unavailable → regex-only + conservative deny

4. CONTENT POLICY (1-3 ms)
   Block: hate speech, violence, illegal content
   Configurable per-tenant (some tenants have broader allowance)
   
5. COST ESTIMATION (< 0.1 ms)
   Estimate output tokens, check against budget
   If over budget: 429 with "token budget exceeded"
```

## 3.2 Output Guardrail Pipeline

```
Model response passes through BEFORE delivery to client:

1. PII / SECRETS DETECTION (1-5 ms)
   Same as input but on model OUTPUT
   Why: model might generate PII from training data
   Or: model might echo PII from retrieved documents
   Action: REDACT before sending to client

2. DATA CLASSIFICATION CHECK (< 1 ms)
   If response references data above user's clearance:
       redact or block
   Example: model generates internal-only pricing → external user → BLOCK

3. CROSS-TENANT LEAK DETECTION (1-3 ms)
   If response contains identifiers from ANOTHER tenant:
       BLOCK + ALERT (critical security event)
   Detection: entity matching against known tenant-specific identifiers
   
4. CONTENT POLICY (1-3 ms)
   Harmful/toxic content in response
   Model hallucinating inappropriate content
   
5. CITATION VALIDATION (for RAG) (< 1 ms)
   If response cites a document:
     Verify user has access to cited document
     If not: strip citation + re-generate or block

6. RESPONSE LENGTH ENFORCEMENT (< 0.1 ms)
   Hard cap on response tokens (billing protection)
   If streaming: cut off at limit + append "[truncated]"

Total guardrail overhead:
  Worst case: 15-25 ms (ML classifiers + NER)
  Best case: 2-5 ms (regex + fast checks only)
  Within inference latency budget (model takes 100-500 ms)
```

## 3.3 Guardrail Failure Modes

```
CRITICAL PRINCIPLE: guardrails that silently fail are WORSE than no guardrails
(they create false confidence)

| Guardrail | If Unavailable | Behavior |
|-----------|---------------|----------|
| PII detector (input) | Timeout | Allow + flag for async review |
| PII detector (output) | Timeout | HOLD response, return error to client |
| Injection classifier | Timeout | Regex-only + conservative deny |
| Content policy | Timeout | Allow + elevated logging |
| Cross-tenant check | Timeout | BLOCK (fail-closed, too dangerous) |
| Cost estimation | Failure | Allow + alert (metering may be off) |

Degradation ladder (same principle as Coupang doc):
  Level 0: Full guardrails active
  Level 1: ML classifiers down → regex/deterministic only + stricter thresholds
  Level 2: All guardrails down → deny all requests (fail-closed for sensitive tenants)
  Level 3: Gateway itself failing → circuit breaker activates at load balancer
```

---

# SECTION 4: BUILD & DEPLOY — MODEL LIFECYCLE FOR INFERENCE

---

## 4.1 Model Onboarding Pipeline

```
New model request → Onboarding pipeline:

1. MODEL ASSESSMENT
   - Size: will it fit on our GPU fleet? (memory requirements)
   - License: can we serve it commercially?
   - Security: scan for known vulnerabilities (model supply chain)
   - Performance: baseline benchmarks (latency, throughput, quality)

2. SECURITY SCAN
   - Artifact integrity verification (hash, signature if available)
   - Training data lineage check (any poisoning risk?)
   - Prompt injection resistance evaluation
   - Red-team test suite (automated adversarial prompts)
   
3. QUANTIZATION & OPTIMIZATION
   - Apply quantization (FP8/INT4) if latency allows
   - Build TensorRT engine (NVIDIA) or tune GEMMs (AMD)
   - Benchmark quantized vs base quality
   - If quality delta > threshold → reject quantization

4. DEPLOYMENT CONFIGURATION
   - Define: TP degree, batch size, max_seq_len
   - Set: rate limits, cost per token, tenant access list
   - Configure: guardrail thresholds specific to this model
   - Write: runbook for on-call

5. CANARY DEPLOYMENT
   - Deploy to staging → run eval suite
   - Deploy canary (1% traffic) with auto-rollback
   - Monitor: latency, error rate, guardrail trigger rate
   - Progressive rollout: 1% → 10% → 50% → 100%
```

## 4.2 CI/CD for Model Deployment

```
┌──────────────────────────────────────────────────────────────────┐
│  MODEL DEPLOYMENT PIPELINE                                        │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  [Model Registry]                                                 │
│       │                                                           │
│       ▼                                                           │
│  [Artifact Verification]──── signature check (cosign/sigstore)    │
│       │                      hash verification                    │
│       ▼                      provenance attestation               │
│  [Security Gate]──────────── injection resistance eval            │
│       │                      bias audit                           │
│       ▼                      red-team test suite                  │
│  [Performance Gate]───────── latency benchmark                    │
│       │                      throughput benchmark                 │
│       ▼                      quality eval (perplexity, accuracy)  │
│  [Staging Deploy]─────────── full integration test                │
│       │                      guardrail compatibility check        │
│       ▼                                                           │
│  [Canary (1%)]────────────── guardrails: latency < 2× baseline   │
│       │                      error rate < 0.1%                    │
│       ▼                      quality score >= baseline            │
│  [Progressive Rollout]─────  10% → 25% → 50% → 100%             │
│       │                      15-min bake per stage                │
│       ▼                                                           │
│  [Production (100%)]                                              │
│       │                                                           │
│  [Previous Version]────────  Hot standby for 24h (instant rollback│
│                                                                   │
└──────────────────────────────────────────────────────────────────┘

Rollback triggers (automatic):
  - p99 latency > 2× baseline for 3 minutes
  - Error rate > 1% for 2 minutes
  - Guardrail trigger rate > 3× baseline (model generating more harmful content)
  - GPU OOM events > 3 in 5 minutes
  - Quality score drops > 5% (if online eval available)

Rollback time: < 60 seconds (previous version is already loaded on standby)
```

## 4.3 Model Registry and Supply Chain Security

```
Model registry requirements:
  - Immutable storage (versions cannot be overwritten)
  - Access control (read: inference service; write: CI/CD only)
  - Integrity verification (hash + signature per artifact)
  - Provenance tracking (source → training → evaluation → approval)
  - Vulnerability scanning (known model exploits)

Supply chain attack vectors and mitigations:
  | Attack | Mitigation |
  |--------|-----------|
  | Tampered weights (backdoor) | Hash verification at load time |
  | Malicious code in model format (pickle exploit) | SafeTensors format (no arbitrary code execution) |
  | Compromised training pipeline | Signed training runs, reproducible builds |
  | Unauthorized model push | Write access via CI/CD only, approval gate |
  | Stale/vulnerable dependencies | Dependency scanning, pinned versions |
  | Model exfiltration | Encryption at rest, access audit log |

Best practice: SafeTensors format
  - No pickle (pickle allows arbitrary code execution on load!)
  - Pure tensor data + metadata (no executable code)
  - Fast memory-mapped loading
  - Standard across HuggingFace ecosystem
  
  NEVER deploy a model from pickle format in production without sandboxed loading
```

---

# SECTION 5: OPERATIONAL SLOS AND MONITORING

---

## 5.1 SLO Framework for LLM Serving

```
| Metric | Target | Error Budget (30-day) | Alert Threshold |
|--------|--------|----------------------|-----------------|
| Availability | 99.9% | 43 min downtime | 3 consecutive 5xx |
| TTFT p99 | < 500 ms | < 1% over target | p99 > 750 ms for 5 min |
| TPOT p99 | < 50 ms/token | < 1% over target | p99 > 75 ms for 5 min |
| Guardrail latency p99 | < 30 ms | < 0.5% over target | p99 > 50 ms |
| Error rate | < 0.1% | < 100 errors/day | > 0.5% for 2 min |
| Injection detection | 99.5% recall | < 5 misses/1000 attempts | new bypass pattern |
| Cross-tenant isolation | 100% (zero tolerance) | 0 events allowed | any event = P1 |

SLO hierarchy:
  User-facing SLO (what customers see):
    "95% of requests complete within 2 seconds end-to-end"
  
  Platform SLO (internal targets to achieve user-facing):
    TTFT < 500 ms (p99)
    TPOT < 50 ms (p99)
    Guardrails < 30 ms (p99)
    Queue wait < 100 ms (p99)
    = Total: 500 + (50 × 20 tokens avg) + 30 + 100 = 1,630 ms typical
    Budget remaining: 370 ms for network + client-side
```

## 5.2 Operational Metrics Dashboard

```
┌────────────────────────────────────────────────────────────────────┐
│  LLM PLATFORM OPERATIONS DASHBOARD                                  │
├────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  SERVING HEALTH                                                     │
│  ──────────────                                                     │
│  Active requests:     342/500 capacity                              │
│  TTFT p50/p99:        120ms / 380ms                                │
│  TPOT p50/p99:        28ms / 42ms                                  │
│  Throughput:          45,000 tokens/sec                             │
│  Error rate:          0.03%                                         │
│  GPU utilization:     72% avg                                       │
│  KV cache util:       68%                                           │
│                                                                     │
│  SECURITY & GUARDRAILS                                              │
│  ────────────────────                                               │
│  Injection attempts blocked:     23/hr                              │
│  PII redacted (input):           145/hr                             │
│  PII redacted (output):          12/hr                              │
│  Cross-tenant blocks:            0 (critical if >0)                 │
│  Guardrail p99 latency:         18ms                                │
│  Guardrail fallback active:     NO                                  │
│                                                                     │
│  TENANT HEALTH (top 5 by traffic)                                   │
│  ─────────────                                                      │
│  fraud-team:     8,200 tok/s  │ p99: 340ms │ budget: 62% used      │
│  support-team:   12,400 tok/s │ p99: 890ms │ budget: 45% used      │
│  search-team:    18,000 tok/s │ p99: 120ms │ budget: 78% used      │
│  external-api:   4,200 tok/s  │ p99: 450ms │ budget: 23% used      │
│  dev-staging:    2,200 tok/s  │ p99: varies│ budget: unlimited      │
│                                                                     │
│  COST                                                               │
│  ────                                                               │
│  GPU-hours today:      192 GPU-hrs ($1,536)                         │
│  Projected monthly:    $46K (budget: $50K)                          │
│  Cost per 1M tokens:   $0.34                                        │
│                                                                     │
│  ALERTS                                                             │
│  ──────                                                             │
│  [OK]  Availability: 99.97% (30-day)                               │
│  [OK]  TTFT SLO: 0.4% budget consumed                             │
│  [WARN] search-team approaching rate limit (92%)                   │
│                                                                     │
└────────────────────────────────────────────────────────────────────┘
```

## 5.3 Cost Governance and Chargeback

```
Multi-tenant cost attribution:

Per-request cost calculation:
  cost = (input_tokens × model_input_price_per_token) +
         (output_tokens × model_output_price_per_token) +
         (gpu_seconds × gpu_cost_per_second)

  Example:
    Llama-70B on H100:
      GPU cost: $8/hr = $0.0022/sec
      Request: 500 input + 200 output tokens, 1.2 sec GPU time
      Cost: (500 × $0.000001) + (200 × $0.000003) + (1.2 × $0.0022) = $0.0033

Chargeback model:
  Option A: Token-based (simple)
    Bill by tokens consumed, flat rate per model tier
    Pro: simple, predictable
    Con: doesn't reflect actual GPU time (short vs long sequences differ)
  
  Option B: GPU-time based (accurate)
    Bill by actual GPU seconds used
    Pro: accurate reflection of resource consumption
    Con: unpredictable for consumers
  
  Option C: Hybrid (recommended)
    Base charge: per-token (covers model depreciation, platform ops)
    Compute charge: per-GPU-second (reflects actual resource usage)
    With monthly commitment discount (reserved capacity)

Budget enforcement:
  Soft limit: alert at 80% → "You're approaching your monthly budget"
  Hard limit: reject at 100% → 429 "Budget exceeded, contact platform team"
  Exception: critical production workloads bypass hard limit (alert only)
```

---

# SECTION 6: ACCESS CONTROL & AUTHORIZATION

---

## 6.1 RBAC for LLM Platform

```
Role hierarchy:

platform-admin:
  - Manage all models, tenants, quotas
  - Access audit logs
  - Modify guardrail configuration
  - Emergency kill switch

tenant-admin:
  - Manage their tenant's users, budgets
  - View their tenant's usage metrics
  - Configure tenant-specific guardrail thresholds
  - Request model access

inference-user:
  - Send inference requests to allowed models
  - View own usage and billing
  - Cannot modify platform configuration

inference-service (machine identity):
  - Send inference requests programmatically
  - Read model endpoints
  - Scoped to specific models and rate limits

model-deployer (CI/CD):
  - Push model artifacts to registry
  - Trigger deployment pipeline
  - Cannot modify access control or guardrails

auditor (read-only):
  - View all audit logs
  - View all metrics and dashboards
  - Cannot modify anything
```

## 6.2 Policy Engine (OPA) for Inference Authorization

```rego
# OPA policy: inference request authorization

package inference.authz

default allow = false

# Allow if user has correct role AND model access AND within budget
allow {
    valid_identity
    model_access_granted
    within_rate_limit
    within_budget
    not blocked_by_guardrail
}

valid_identity {
    input.tenant_id != ""
    input.user_id != ""
    token_not_expired
}

model_access_granted {
    input.model in data.tenant_models[input.tenant_id]
}

within_rate_limit {
    current_rate := data.rate_counters[input.tenant_id][input.model]
    limit := data.rate_limits[input.tenant_id][input.model]
    current_rate < limit
}

within_budget {
    used := data.budgets[input.tenant_id].tokens_used_this_month
    limit := data.budgets[input.tenant_id].monthly_limit
    used < limit
}

# Deny reasons (for observability):
deny_reasons[reason] {
    not valid_identity
    reason := "invalid_identity"
}
deny_reasons[reason] {
    not model_access_granted
    reason := "model_not_authorized"
}
deny_reasons[reason] {
    not within_rate_limit
    reason := "rate_limit_exceeded"
}
```

## 6.3 Secrets Management for Inference

```
What secrets exist in LLM serving:
  - Model weights encryption keys (at-rest protection)
  - API keys for external model providers (OpenAI, Anthropic proxy)
  - Database credentials (feature store, vector DB)
  - TLS certificates (inter-service mTLS)
  - Tenant-specific encryption keys

Best practices:
  1. NO secrets in environment variables (visible in k8s API, ps, /proc)
  2. Use: K8s Secrets + CSI Secret Store Driver + Vault/AWS Secrets Manager
  3. Rotation: automatic, without pod restart (mounted volumes update in-place)
  4. Scope: each pod gets ONLY the secrets it needs (least privilege)
  5. Audit: all secret access logged in Vault audit log

  # CSI Secret Store Driver (auto-mounts secrets from Vault):
  apiVersion: secrets-store.csi.x-k8s.io/v1
  kind: SecretProviderClass
  metadata:
    name: inference-secrets
  spec:
    provider: vault
    parameters:
      roleName: "inference-service"
      objects: |
        - objectName: "model-encryption-key"
          secretPath: "secret/data/inference/model-key"
        - objectName: "feature-store-password"
          secretPath: "secret/data/inference/feature-store"

For multi-tenant:
  Tenant-specific secrets (API keys, encryption):
    Stored in separate Vault paths per tenant
    Access controlled by tenant identity
    Never shared across tenants even accidentally
```

---

# SECTION 7: AUDIT, COMPLIANCE & GOVERNANCE

---

## 7.1 Audit Log Requirements

```
Every inference request generates an audit event:

{
  "timestamp": "2026-06-01T10:00:00.000Z",
  "request_id": "req-abc123",
  "trace_id": "trace-xyz789",
  "tenant_id": "acme-corp",
  "user_id": "user-12345",
  "model": "llama-70b",
  "action": "inference",
  "input_tokens": 500,
  "output_tokens": 200,
  "latency_ms": 1200,
  "guardrails": {
    "injection_score": 0.02,
    "pii_detected_input": false,
    "pii_detected_output": true,
    "pii_action": "redacted",
    "content_policy": "pass"
  },
  "decision": "allow",
  "cost_usd": 0.0033,
  "ip_address": "10.0.1.42",
  "user_agent": "fraud-scoring-service/2.1"
}

Audit log properties:
  - Immutable (append-only, no modification/deletion)
  - Durable (replicated, survives node failure)
  - Complete (every request, every decision)
  - Tamper-evident (hash chain or signed entries)
  - Retained: 90 days hot (queryable), 7 years cold (archival)

Storage:
  Hot path: Kafka → Elasticsearch (queryable within seconds)
  Cold path: Kafka → S3 Parquet (cost-effective long-term)
  
  Volume: 10K req/s × 1KB per event = 10 MB/s = 864 GB/day
  Retention cost at S3: ~$0.023/GB/month = $600/month for 90-day hot
```

## 7.2 Compliance Frameworks

```
| Framework | Relevant Requirements | How Platform Addresses |
|-----------|----------------------|----------------------|
| SOC 2 Type II | Access control, monitoring, incident response | RBAC, audit logs, alerting, runbooks |
| PCI DSS | Protect cardholder data, access control | PII redaction, encryption, tenant isolation |
| HIPAA | PHI protection, minimum necessary | Data classification, output guardrails |
| GDPR | Data minimization, right to erasure | PII scanning, data retention policies |
| AI Act (EU) | Transparency, human oversight, risk mgmt | Audit trail, guardrails, human-in-loop |
| NIST AI RMF | Risk identification, governance | Threat model, model lifecycle, monitoring |

Platform compliance checklist:
  □ All access authenticated and authorized
  □ All actions logged immutably
  □ PII detected and handled appropriately
  □ Tenant data isolated (provably)
  □ Models vetted before production deployment
  □ Incident response plan documented and tested
  □ Regular security assessments (penetration testing)
  □ Data retention and deletion policies enforced
  □ Human oversight capability (kill switch, escalation)
  □ Model decisions explainable (audit replay)
```

## 7.3 Governance Workflows

```
MODEL APPROVAL WORKFLOW:
  Request: "Deploy model X for tenant Y"
  
  1. ML Engineer submits request (model artifact + eval results)
  2. Automated checks:
     - Security scan passed?
     - Performance benchmark met?
     - Injection resistance above threshold?
     - Bias audit within bounds?
  3. Review:
     - Platform team: resource impact, operational readiness
     - Security team: threat assessment for model capabilities
     - Compliance team: data handling, regulatory impact
  4. Approval (2 reviewers minimum for production)
  5. Deployment via CI/CD pipeline (no manual deploys)

TENANT ONBOARDING:
  1. Legal: contract, DPA (Data Processing Agreement)
  2. Technical: define isolation level, model access, rate limits
  3. Security: penetration test of tenant integration
  4. Configuration: create namespace, quotas, guardrail config
  5. Validation: smoke test with tenant's application
  6. Go-live: enable production traffic

GUARDRAIL THRESHOLD CHANGE:
  1. Request: "Relax PII detection for tenant X (they process customer data)"
  2. Risk assessment: what's the impact if relaxed?
  3. Compensating control: enhanced output scanning instead?
  4. Approval: security team + tenant admin
  5. Change: update OPA policy, audit log the change
  6. Monitor: watch for increased PII in outputs (anomaly detection)
```

---

# SECTION 8: INCIDENT RESPONSE FOR LLM PLATFORMS

---

## 8.1 LLM-Specific Incident Types

```
| Incident Type | Severity | Detection | Response |
|---|---|---|---|
| Prompt injection bypass | P1 | Canary detection, output guardrail trigger | Kill affected model, switch to restricted mode |
| Cross-tenant data leak | P1 | Output guardrail, anomaly detection | Isolate tenant, halt serving, forensics |
| Model hallucinating PII | P2 | Output PII scanner spike | Enable strict redaction, investigate source |
| Performance degradation | P2 | SLO breach alert | Scale up, shed load, identify cause |
| Token budget exhaustion | P3 | Budget alert | Notify tenant, temporary increase if justified |
| Model quality regression | P2 | Online eval metrics | Rollback to previous version |
| GPU hardware failure | P3 | DCGM alerts, pod restarts | Auto-failover, replace node |
| Guardrail service failure | P1 | Health check, latency spike | Activate degradation ladder |
```

## 8.2 Runbook: Prompt Injection Bypass Detected

```
ALERT: prompt_injection_bypass_detected (output contains suspicious patterns)

IMMEDIATE (< 5 min):
  1. Verify: is this a true positive? (check audit log for the request)
  2. If confirmed: activate restricted mode for affected model
     - Block similar request patterns (add to regex blocklist)
     - Enable maximum output guardrails (strictest thresholds)
  3. Assess blast radius:
     - How many requests used this bypass?
     - What data was potentially exposed?
     - Which tenants affected?

CONTAINMENT (< 30 min):
  4. If data exposure: notify affected tenants
  5. Add injection pattern to fast-path blocklist (< 5 min deploy)
  6. Review: are other models vulnerable to same pattern?
  7. If systemic: halt all inference pending fix

RESOLUTION:
  8. Root cause: why did input guardrail miss this?
  9. Update detection: add pattern to all detection layers
  10. Re-train classifier if ML-based detection failed
  11. Verify fix: replay the injection against updated guardrails
  12. Resume normal operations

POST-MORTEM:
  - Document: what happened, timeline, blast radius, fix
  - Red-team: run full injection test suite against updated system
  - Process improvement: what could have caught this earlier?
```

## 8.3 Runbook: Cross-Tenant Data Leak

```
ALERT: cross_tenant_data_detected_in_response (CRITICAL P1)

THIS IS THE WORST-CASE SCENARIO. Respond immediately.

IMMEDIATE (< 2 min):
  1. HALT all inference for affected tenant(s)
  2. Preserve evidence: snapshot audit logs, request/response pairs
  3. Page: security on-call + engineering lead + legal

INVESTIGATION (< 30 min):
  4. Determine source:
     a. KV cache contamination? (shared cache across tenants)
     b. Vector DB query missing tenant filter?
     c. Model training data leak? (memorized other tenant's data)
     d. Feature store ACL failure?
  5. Determine scope:
     - How many requests affected?
     - Which tenant's data was leaked to which other tenant?
     - What data categories? (PII? credentials? business data?)

CONTAINMENT:
  6. If KV cache: flush all caches, restart with isolated pools
  7. If vector DB: verify tenant filters, rebuild indexes if needed
  8. If model: quarantine model version, rollback
  9. If feature store: fix ACL, audit all recent queries

NOTIFICATION:
  10. Legal/compliance team: data breach assessment
  11. Affected tenants: transparent disclosure (per DPA/contract)
  12. If PII involved: regulatory notification (GDPR: 72 hours)

PREVENTION:
  13. Add canary tokens per tenant (synthetic data that should NEVER appear for other tenants)
  14. Continuous cross-tenant isolation testing (automated)
  15. Review: what isolation level should these tenants have? (upgrade if needed)
```

---

# SECTION 9: PLATFORM OPERATIONS PATTERNS

---

## 9.1 Capacity Management

```
GPU capacity planning for multi-tenant:

1. MEASURE per-tenant demand:
   Peak tokens/sec per tenant × growth rate
   Model size requirements (which models, how many GPUs each)
   Burst patterns (when do spikes happen?)

2. RESERVE vs SHARED capacity:
   Reserved: guaranteed GPUs for production SLA tenants
   Shared: pool available for burst/best-effort
   
   Example:
     Total cluster: 64 GPUs
     Reserved (fraud-team): 16 GPUs (always available)
     Reserved (support-team): 8 GPUs
     Shared pool: 40 GPUs (burst for anyone, preemptible for dev)

3. SCALING TRIGGERS:
   Scale up: queue depth > 10 for 1 min OR GPU util > 80%
   Scale down: GPU util < 30% for 15 min (conservative)
   Never scale below: reserved capacity per tenant

4. CAPACITY ALERTS:
   Warning: cluster at 70% utilization (2-week procurement lead time)
   Critical: cluster at 85% (start rejecting non-critical traffic)
   Emergency: cluster at 95% (page platform team, emergency procurement)
```

## 9.2 Model Version Management

```
Production model lifecycle states:
  
  STAGING → CANARY → PRODUCTION → DEPRECATED → ARCHIVED
  
  STAGING:
    Deployed in staging environment
    Running automated eval suite
    Not serving any production traffic
    
  CANARY:
    Serving 1-5% of production traffic
    Monitored against baseline model
    Auto-rollback if SLO breach
    Duration: 1-24 hours (configurable per model)
    
  PRODUCTION:
    Serving majority/all production traffic
    Previous version in hot standby
    Monitored continuously
    
  DEPRECATED:
    No longer receiving traffic
    Available for emergency rollback (7 days)
    Kept loaded on standby GPUs
    
  ARCHIVED:
    Offloaded from GPU memory
    Model artifact preserved in registry
    Can be re-deployed but requires full onboarding

Version pinning for tenants:
  Some tenants may need specific model versions:
    Tenant A: pinned to llama-70b-v2.3 (regulatory evaluation done for this version)
    Tenant B: tracking latest (always gets newest version)
  
  Platform supports: per-tenant version pinning with upgrade approval flow
```

## 9.3 Disaster Recovery

```
DR scenarios for LLM platform:

SCENARIO 1: Single GPU node failure
  Impact: lose 1 node (4-8 GPUs)
  Recovery: automatic (K8s reschedules pods to other nodes)
  RTO: 2-5 minutes (model reload on new node)
  Prevention: PodDisruptionBudget ensures min replicas always available

SCENARIO 2: Full AZ failure
  Impact: lose 33% of cluster (3-AZ setup)
  Recovery: remaining 2 AZs absorb traffic (pre-provisioned headroom)
  RTO: 0 seconds (active-active across AZs)
  Requirement: cluster sized to survive 1 AZ loss at peak
  
  Capacity math:
    Peak demand: 48 GPUs needed
    3 AZs: 48 / 0.67 = 72 GPUs total (24 per AZ, survive 1 AZ loss)

SCENARIO 3: Model corruption / security incident
  Impact: model is compromised, serving bad results
  Recovery: immediate rollback to last-known-good version
  RTO: < 60 seconds (hot standby version)
  Prevention: canary deployment catches issues before full rollout

SCENARIO 4: Complete platform failure (control plane down)
  Impact: can't schedule new pods, can't update config
  Recovery: running pods continue serving (data plane independent)
  RTO: depends on control plane recovery (target: 15 min)
  Prevention: etcd backup + rapid control plane restoration playbook

Backup strategy:
  Model artifacts: replicated to 2 regions (S3 cross-region replication)
  Configuration (K8s manifests): GitOps repo (multiple clones)
  Audit logs: replicated storage + cross-region backup
  Feature store: Redis with persistence + periodic snapshot to S3
```

---

# SECTION 10: ADVANCED INTERVIEW QUESTIONS (Governance & Operations)

## "How would you design a multi-tenant LLM platform for 50 enterprise customers?"

→ Architecture:
```
Layer 1: Shared AI Gateway (Rust, < 5 ms overhead)
  Per-tenant: auth, rate limit, model access, cost metering
  
Layer 2: Tenant-aware routing
  Isolation level per tenant (shared/dedicated/isolated)
  Route to correct GPU pool based on tenant tier
  
Layer 3: Inference layer
  Tier 1 tenants: dedicated GPU nodes (guaranteed SLA)
  Tier 2 tenants: shared pool with quotas (best-effort SLA)
  
Layer 4: Guardrails (configurable per tenant)
  Strict PII: financial tenants
  Relaxed PII: customer service tenants (need to see customer data)
  Custom content policy: per-tenant configuration
  
Layer 5: Observability & Billing
  Per-tenant dashboards, cost attribution, SLO tracking
  
Layer 6: Governance
  Model approval workflow per tenant
  Tenant-specific model versioning
  Compliance reporting per tenant

Key design choices:
  - Gateway is the SINGLE enforcement point (no bypass possible)
  - Tenant context is infrastructure-level (not application-level)
  - Guardrails are configurable per-tenant via policy (not code changes)
  - Cost attribution is per-request (not estimated/allocated)
  - Isolation level is contractual (higher tier = more isolation = higher price)
```

## "What happens when a tenant reports that the model leaked another tenant's data?"

→ Incident response (reference Section 8.3):
1. **Immediate**: halt serving for both tenants, preserve evidence
2. **Verify**: confirm leak from audit logs (request/response pairs)
3. **Scope**: how many requests affected, what data categories
4. **Root cause**: KV cache sharing? Vector DB filter missing? Model memorization?
5. **Fix**: depends on root cause — flush caches, fix filters, or quarantine model
6. **Notify**: legal team, affected tenants (per DPA/contract obligations)
7. **Prevent**: add canary tokens, upgrade isolation level, continuous testing

What makes this a principal-level answer:
- Think about legal/contractual obligations FIRST (not just technical fix)
- Communicate transparently (even before full root cause)
- Design prevention (canary tokens, isolation upgrade) not just fix

## "How do you balance security control latency with user experience?"

→ Latency budget approach:
```
Total SLA: 2,000 ms end-to-end

Budget allocation:
  Gateway + auth:       3 ms (< 0.2% of budget)
  Input guardrails:    15 ms (< 1% of budget)
  Inference (model):  1,500 ms (75% of budget — the core value)
  Output guardrails:   20 ms (1% of budget)
  Network/overhead:    62 ms (3% of budget)
  Headroom:           400 ms (20% — for variance)

Guardrails: 35 ms total = 1.75% of the user's SLA
  → Invisible to user, massive security value
  → If guardrails took 200 ms: would reconsider (10% of budget)

Optimization tricks:
  - Parallel execution: input guardrails run WHILE routing decision happens
  - Streaming guardrails: check output tokens as they stream (not batch)
  - Fast-path: if request matches cached policy → skip ML classifier
  - Async for non-blocking: audit logging is async (zero impact on latency)
  
Rule: security controls should consume < 5% of total latency budget
  If they consume more: optimize the guardrail, not remove it
```

## "How do you handle model updates without downtime?"

→ Blue-green + canary:
```
Current: model-v2 serving 100% traffic (BLUE)
New: model-v3 deployed alongside (GREEN)

Step 1: v3 passes all gates (security, performance, quality)
Step 2: v3 goes canary (1% traffic) for 1 hour
  Monitor: latency, errors, guardrail triggers, quality metrics
Step 3: If clean: progressive rollout (10% → 25% → 50% → 100%)
  Each step: 15-min bake time with SLO monitoring
Step 4: v2 becomes hot standby (loaded but 0% traffic)
Step 5: After 24h stable: v2 transitions to deprecated → archived

Zero-downtime guarantees:
  - v3 is fully loaded + warmed BEFORE receiving traffic
  - At no point is capacity reduced (v2 + v3 both serving during transition)
  - Rollback: instant (just shift traffic back to v2)
  - Tenant-pinned versions: don't move until tenant approves
```

## "Design the observability stack for a production LLM platform"

→ Three pillars + AI-specific:
```
1. METRICS (Prometheus + Grafana):
   Serving: TTFT, TPOT, throughput, GPU util, KV cache util, batch size
   Security: injection blocks, PII detections, cross-tenant blocks
   Business: cost/tenant, tokens/tenant, error rate/tenant
   Platform: node health, DCGM, network, storage

2. TRACES (OpenTelemetry → Jaeger/Tempo):
   Full request trace: gateway → guardrails → routing → inference → output
   Per-span: latency, model version, batch position, cache hit/miss
   Sampling: 100% for errors/slow, 1% for normal

3. LOGS (structured → ELK/Loki):
   Audit events (every request)
   Guardrail decisions (every check)
   Error details (full context for debugging)
   
4. AI-SPECIFIC:
   Model quality metrics (online evaluation)
   Feature drift detection
   Guardrail effectiveness (false positive/negative rates)
   Cost attribution accuracy
   Tenant SLO compliance per-period

Alert hierarchy:
  P1: cross-tenant leak, complete outage, security bypass
  P2: SLO breach, model quality drop, capacity critical
  P3: single tenant issue, approaching limits, degraded mode
  P4: cosmetic, informational, trending warnings
```

## "How do you ensure compliance (SOC2/PCI/HIPAA) for an LLM platform?"

→ Control framework:
```
Principle: treat the LLM platform like any production financial system:

ACCESS CONTROL (SOC2 CC6):
  - RBAC with least privilege
  - MFA for human access to platform
  - Machine identities (SPIFFE) for service-to-service
  - Quarterly access reviews
  - JIT (just-in-time) access for production troubleshooting

CHANGE MANAGEMENT (SOC2 CC8):
  - All changes via GitOps (auditable, reviewable)
  - Model deploys require 2 approvals
  - Guardrail changes require security team approval
  - Emergency changes require post-hoc review within 24h

MONITORING (SOC2 CC7):
  - Continuous monitoring of all access and actions
  - Alerting on anomalous patterns
  - Quarterly penetration testing
  - Annual audit (external)

DATA PROTECTION (PCI DSS / HIPAA):
  - PII never stored in model weights (verified at training)
  - PII in prompts: redacted in logs, encrypted in transit
  - Output guardrails prevent PII leakage
  - Data classification enforced at every boundary
  - Encryption: at-rest (AES-256) + in-transit (TLS 1.3)

EVIDENCE (for auditors):
  - Immutable audit logs (prove what happened)
  - Access review records (prove who had access)
  - Change records (prove what changed and who approved)
  - Test results (prove controls were tested)
  - Incident records (prove responses were timely)
```
