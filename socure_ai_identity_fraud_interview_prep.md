# CapitalOne New Account Decisioning — Identity Fraud Detection
## (Socure AI Engineer Interview Prep — Primary Story)

---

## How To Use This Document

This document frames my **CapitalOne New Account Decisioning Service** experience as the primary interview story for Socure. Everything Socure expects to hear — entity resolution, feature engineering, graph-based fraud ring detection, real-time scoring, calibration, explainability, LLM-assisted verification — is covered through what I actually built.

**Supplementary projects (used where applicable):**
- **Apple Siri HomePod** → ACL-aware FAISS retrieval, TigerGraph 2-hop traversals, zero-copy feature serving, NUMA-aware architecture
- **Broadcom Cloud SWG** → Multi-model parallel scoring, shared RequestContext, multi-tenant SLA governance, compliance (PCI/HIPAA/GDPR), dynamic batching

**Tech stack deep-dives** (already covered in `docs-1/`) are referenced but not duplicated here. Anything NEW to identity fraud that's not in those docs is highlighted.

---

## 1. Business Problem

### What CapitalOne Needed

CapitalOne's Card Division receives millions of credit card applications annually through online, mobile, in-branch, and partner channels. The core business problem:

**"For every new card application, determine in real-time whether the applicant is a real person, whether they are who they claim to be, and whether they intend to use the product legitimately — before issuing a card that could generate tens of thousands in losses."**

The Fraud Detection Unit served multiple CapitalOne business lines. I spent my first year on **New Account Decisioning** (identity fraud) and then moved to **Transaction Fraud Scoring** (payment fraud). Both services shared platform infrastructure but had separate scoring pipelines.

### The Threat Landscape

| Threat | Description | Loss Profile |
|---|---|---|
| **Synthetic Identity Fraud** | Fabricated identities assembled from real/fake SSN, name, address, phone, email. Built up over months, then "bust out" with maxed credit. | $10K–$100K per identity. Industry: $6B/year |
| **First-Party Fraud** | Real person applies with intent to abuse — never plans to repay, or uses for money laundering | $5K–$50K per account |
| **Identity Theft** | Stolen real identity (from breaches, phishing, dark web) used to open accounts | Variable, + reputational damage |
| **Application Fraud Rings** | Coordinated groups sharing devices, addresses, phones to submit hundreds of applications | Clustered losses, hard to detect individually |
| **Document Fraud** | Forged/altered ID documents for KYC bypass | Enables all other fraud types |

### Why This Maps Directly to Socure

Socure's product answers the exact same question for their clients (banks, fintechs, crypto exchanges): "Is this applicant real and trustworthy?" The difference:
- CapitalOne answers it for ONE institution with deep internal data
- Socure answers it for 1800+ clients with cross-network visibility

**My interview pivot:** "I've built this exact system for a single institution. What excites me about Socure is the structural data advantage — cross-network entity resolution and velocity that no single bank can achieve alone."

---

## 2. Business Objectives

| Objective | Target | Measurement |
|---|---|---|
| **Fraud Loss Prevention** | Reduce identity fraud losses by 40%+ | Dollars of fraud prevented vs. prior year |
| **Auto-Approval Rate** | ≥70% of legitimate applicants approved instantly | Decision mix: approve / step-up / review / decline |
| **False Positive Rate** | <2% of legitimate applicants incorrectly declined or sent to review | False positive rate at operational threshold |
| **Latency SLA** | p99 < 200ms for hot-path scoring | End-to-end request latency monitoring |
| **Regulatory Compliance** | FCRA adverse action notices, ECOA fair lending, BSA/AML | Audit completeness, reason code accuracy |
| **Scalability** | Handle peak application volume (holiday seasons, partner launches) | TPS capacity, degradation under load |
| **Explainability** | Every decline must have defensible reason codes | SHAP-based explanations mapped to reason categories |
| **Adaptability** | Detect new fraud patterns within days, not months | Time-to-detection for new fraud vectors |

### How These Map to Socure's Product Requirements

Every objective above is what Socure sells to their clients. When Socure's interviewer asks "how would you measure success?" — these ARE the metrics. Same objectives, same governance, same compliance requirements.

---

## 3. Data Required

### Internal Data (CapitalOne)

| Data Source | What It Provides | Freshness |
|---|---|---|
| Application payload | Name, DOB, SSN, email, phone, address, employment, income | Real-time (current request) |
| Device intelligence SDK | Device fingerprint, OS, browser, emulator detection, rooted flag | Real-time |
| Session telemetry | Form fill time, typing cadence, copy-paste events, mouse movement | Real-time |
| Internal customer database | Existing accounts, payment history, prior applications | Sub-second (cache) |
| Prior application history | Same SSN/email/phone/device seen before? Outcomes? | Sub-second (cache + graph) |
| Manual review outcomes | Analyst decisions on flagged cases | Hours–days (label source) |
| Chargeback/bust-out data | Confirmed fraud accounts | Days–weeks (delayed labels) |

### External/Cross-Bank Data

| Data Source | What It Provides | Access Method | Latency |
|---|---|---|---|
| **Credit Bureau** (Experian, Equifax, TransUnion) | Credit history, inquiries at other banks, thin-file indicator | Hard-pull API at application time | 50–100ms |
| **Shared Fraud Consortium** (Early Warning Services) | Confirmed fraud flags from other institutions | API query | 20–50ms |
| **Phone Intelligence** (carrier lookup) | Phone tenure, line type (VoIP/prepaid/postpaid), carrier, name match | Third-party API | 30–60ms |
| **Email Intelligence** (Emailage/LexisNexis) | Email age, domain risk, prior fraud association | Third-party API | 20–50ms |
| **Device Reputation Network** | Device seen in fraud at other institutions | Third-party API | 20–40ms |
| **Address Verification** (USPS, address intelligence) | Deliverability, residential vs. commercial, vacancy | Cached reference data | Sub-1ms |
| **IP Intelligence** | Geo, proxy/VPN/TOR detection, datacenter flag, risk score | Cached + API | 5–20ms |
| **Document Verification** (for warm path) | ID document authenticity, face match | VLM pipeline | 2–5 seconds |

### Cross-Network Visibility Gap (Why Socure Wins)

At CapitalOne, each external enrichment was a **separate paid API call** adding latency. And each vendor only had partial coverage. A phone number seen in fraud at another bank might NOT appear in our consortium if that bank uses a different consortium.

**Socure's structural advantage:** All their clients' data flows into ONE platform. Entity resolution and velocity happen natively, without API calls. One phone number appearing across 5 banks' applications is instantly visible.

**My interview line:** "At CapitalOne, we approximated cross-network visibility by orchestrating 5+ external enrichment calls in parallel within our 50ms feature-assembly budget. Each had coverage gaps and added latency. Socure's architecture eliminates that — the cross-network view is the product. The engineering challenge shifts from fighting for data access to making scoring faster, models sharper, and calibration tighter."

---

## 4. Data Pre-Processing and Entity Resolution

### Schema Normalization (Hot Path, <2ms)

When a raw application hits the gateway:

```
Raw: "JOHN   D.  SMITH JR.", "123 Main St, Apt 4B, New York NY 10001", "+1(212) 555-0147"

Normalized:
  name_first: "john"
  name_middle: "d"
  name_last: "smith"
  name_suffix: "jr"
  address_line1: "123 main st"
  address_unit: "apt 4b"
  address_city: "new york"
  address_state: "ny"
  address_zip5: "10001"
  phone_e164: "+12125550147"
  email_normalized: "john.smith@gmail.com" → "johnsmith@gmail.com" (Gmail dot-stripping)
```

Normalization rules: case folding, whitespace collapse, suffix extraction, address standardization (USPS format), phone E.164, email canonicalization (provider-specific rules).

### Entity Resolution — Two-Stage Pipeline

**This is the step that makes identity fraud different from transaction fraud.** In transaction fraud, the card number IS the identity. In identity fraud, the whole question is "who is this?"

**Stage 1: Deterministic Key Lookup (sub-1ms)**
```
SSN_hash → candidate entity IDs
Phone_E164 → candidate entity IDs
Device_fingerprint → candidate entity IDs
Email_normalized → candidate entity IDs
```
Simple Redis lookup. Returns a UNION of all candidate entity IDs that share at least one exact key with the incoming application.

**Stage 2: Probabilistic Entity Matching (2–5ms)**

For each candidate from Stage 1, compute pairwise similarity:
```
Signals:
  Name: Jaro-Winkler("John D. Smith", "John David Smith") = 0.92
  Address: geocoded → same lat/lng ± 50m = 0.95
  Phone: exact match = 1.0
  Email: different but same name pattern = 0.4
  DOB: exact match = 1.0

Weighted logistic regression → match confidence: 0.89
```

Decision:
- Confidence > 0.75 → LINK to existing entity (append to their history)
- Confidence < 0.30 → CREATE new entity
- Between → UNCERTAIN (flag, may create tentative link)

**Why This Matters Downstream:**

Once you resolve to an entity, you unlock their entire application history. Without entity resolution, every application looks "new" and you lose all temporal context. This transforms the feature space from ~50 static features about the current application to 200+ temporal features spanning the entity's full history.

### Applicability to Socure

Socure calls this their "Identity Graph." Same two-stage concept — deterministic keys then probabilistic matching — but across ALL their clients' applications. My implementation detail is the same; their coverage is wider.

### Supplement: Apple Siri Parallel

At Siri, entity resolution existed for user profiles — matching a voice query to the correct user account across devices. Same pattern: deterministic key first (device_id → user), then probabilistic matching for ambiguous cases (shared family devices). The TigerGraph traversal pattern I built for Siri (2-hop entity lookup with confidence scoring) directly transferred to identity fraud graph queries.

---

## 5. Training Pipeline (Brief Background)

### Label Strategy

Labels for identity fraud are **delayed and noisy** — this is a critical difference from transaction fraud where chargebacks arrive in days.

| Label Source | Delay | Quality | Volume |
|---|---|---|---|
| **Manual review decision** | Hours | High for reviewed cases, but biased (only borderline cases get reviewed) |Low (only ~5-10% of applications) |
| **60-day bust-out** | 60 days | High confidence for confirmed synthetic fraud | Low |
| **90-day default** | 90 days | Medium (could be credit risk, not fraud) | Medium |
| **Consortium confirmed fraud** | Days–weeks | High | Low |
| **Never-activated accounts** | 30 days | Weak signal (many legitimate reasons) | Medium |

### Point-in-Time Correctness (Critical)

Every training example must use ONLY features that existed BEFORE the decision was made:
- Entity history up to (but not including) the current application
- Graph edges created before the current event
- FAISS index built from fraud confirmed BEFORE the current event
- No future chargebacks, no future bust-outs, no post-decision manual review outcomes

**My implementation:** The feature pipeline had a `as_of_timestamp` parameter. Every feature computation was gated by `WHERE event_ts < as_of_timestamp`. Training data generation replayed the feature store state at each historical decision point.

### Model Training Stack

| Component | Framework | Notes |
|---|---|---|
| XGBoost (primary scorer) | XGBoost + custom objective | Trained on time-based splits, calibrated with isotonic regression |
| Transformer (behavior model) | PyTorch → ONNX → TensorRT | Sequence model over last-N application/session events |
| Entity matching model | Scikit-learn logistic regression | Pairwise similarity features → match confidence |
| FAISS embeddings | PyTorch embedding model | Trained with contrastive loss on fraud/non-fraud pairs |
| Calibration | Isotonic regression | Post-hoc calibration on holdout set, per-segment |

### Evaluation Metrics (Not Accuracy)

| Metric | Why |
|---|---|
| **PR-AUC** | Primary metric for imbalanced fraud detection |
| **Recall @ 1% FPR** | "How much fraud do we catch if we're willing to inconvenience 1% of good applicants?" |
| **Calibration (ECE)** | Score of 0.8 must mean 80% probability of legitimacy — thresholds depend on this |
| **Fraud dollars prevented** | Business metric — not all fraud is equal in $$ |
| **Time-to-detection** | How quickly new fraud patterns are caught after they start |

### Supplement: Broadcom Parallel for Training

At Broadcom, we faced the same delayed-label problem with web threat detection. Threats confirmed by SOC analysts arrived days later. Same solution: time-based training splits, explicit label-delay windows, and conservative evaluation that accounts for labels not yet matured. The Broadcom multi-model ensemble approach (6 parallel models → meta-scorer) is architecturally identical to our parallel-branch → XGBoost pattern.

---

## 6. Three-Tier Implementation: Hot / Warm / Cold Paths

This is the heart of the system and my primary interview talking point.

### Architecture Overview

```
                    Applicant Applies (online / mobile / in-branch / partner)
                                        │
                                        ▼
                ┌───────── HOT PATH (p99 < 200ms) ─────────────┐
                │  Rust Gateway → Entity Resolution              │
                │  → Feature Assembly (parallel enrichment)      │
                │  → Parallel Model Scoring                      │
                │     (XGBoost + Transformer + FAISS + Graph)    │
                │  → Calibrated Score → Policy Decision          │
                │  → Reason Codes → Audit Log                    │
                └──────────────────┬────────────────────────────┘
                                   │
              ┌────────────────────┼────────────────────────┐
              ▼                    ▼                         ▼
        Score > 700          300 ≤ Score ≤ 700         Score < 300
        AUTO-APPROVE         STEP-UP PATH              AUTO-DECLINE
              │                    │                         │
              │                    ▼                         │
              │    ┌── WARM PATH (2-5 seconds) ────────┐    │
              │    │  Applicant uploads ID + selfie      │    │
              │    │  13B Vision-Language Model:          │    │
              │    │   • OCR: extract DL/passport fields  │    │
              │    │   • Face match: selfie vs ID photo   │    │
              │    │   • Tampering: font/edge artifacts   │    │
              │    │   • Cross-reference vs application   │    │
              │    │  Re-score with document features     │    │
              │    │  Updated decision: approve/escalate  │    │
              │    └────────────────┬───────────────────┘    │
              │                     │                        │
              │              ┌──────┴──────┐                 │
              │              ▼             ▼                  │
              │         Approve       Escalate               │
              │                           │                  │
              │                           ▼                  │
              │    ┌── COLD PATH (minutes-hours) ───────┐    │
              │    │  70B Reasoning Agent with tools:     │    │
              │    │   • Pull full credit bureau report   │    │
              │    │   • Search internal fraud database   │    │
              │    │   • Deep graph traversal (5-hop)     │    │
              │    │   • Cross-reference consortium data  │    │
              │    │   • Generate investigation summary   │    │
              │    │  Analyst sees: evidence + reasoning  │    │
              │    │  Analyst makes final decision        │    │
              │    └────────────────────────────────────┘    │
              │                                              │
              ▼                                              ▼
        CARD ISSUED                                  ADVERSE ACTION
        (monitoring begins)                          NOTICE (FCRA)
```

### Hot Path — Real-Time Scoring (Handles ~70% of applications instantly)

**Latency Budget Breakdown (200ms total p99):**

| Stage | Budget | What Happens |
|---|---|---|
| Gateway + validation | 5ms | TLS termination, JSON parsing, schema validation, rate limiting |
| Entity resolution | 15ms | Deterministic key lookup + probabilistic matching |
| Feature assembly | 50ms | Parallel: internal cache (1ms) + 3rd-party enrichment (50ms) + graph features (30ms). Bounded by slowest enrichment call with circuit breaker |
| Parallel model scoring | 30ms | XGBoost (<1ms) + Transformer (10ms) + FAISS (5ms) + TigerGraph (15ms) — all parallel, bounded by graph |
| Calibration + decision | 2ms | Isotonic calibration → policy threshold → decision |
| Reason codes + audit | 5ms | SHAP top-3 → reason code mapping → async audit emit |
| **Total p99** | **~110ms** | Well within 200ms SLA |

**Feature Assembly — Parallel with Deadlines:**

Three sources fire simultaneously, each with its own circuit breaker and deadline:
1. **Internal feature cache** (sub-1ms): Pre-computed velocity, entity history, last-N events
2. **Third-party enrichment APIs** (20-50ms each): Phone intelligence, email age, device reputation, credit bureau. All fire in parallel. If any is slow, degrade gracefully.
3. **Graph query** (10-30ms): TigerGraph bounded 2-hop traversal for fraud-ring features

If a source misses its deadline: use explicit `source_timeout=1` flag in the FeatureBlock. XGBoost is trained with these flags so it knows when data is missing vs. absent.

**Parallel Model Scoring:**

| Model | Purpose | Latency | Fallback |
|---|---|---|---|
| XGBoost | Primary calibrated scorer, 200+ features | <1ms | Last-known-good model |
| Transformer | Behavioral sequence model (last-N events) | 5-10ms (microbatched) | Lightweight behavioral features from cache |
| FAISS | Vector similarity to known fraud patterns | 2-5ms | Cached similarity aggregates |
| TigerGraph | Fraud ring / synthetic cluster features | 10-30ms | Pre-cached graph aggregates |

All parallel, all deadlined. Exactly like Broadcom's 6-model fan-out with shared RequestContext.

**Supplement: CapitalOne ↔ Broadcom Parallel**

At Broadcom Cloud SWG, I ran 6 models in parallel for web security: URL classifier, content analyzer, behavioral model, reputation lookup, DGA detector, and encrypted traffic analyzer. Same pattern: shared RequestContext, independent deadlines, graceful degradation per branch, final meta-scorer combines all outputs. The identity fraud system uses 4 branches instead of 6, but the orchestration is identical.

### Warm Path — Document Verification with Vision-Language Model (2-5 seconds)

Triggered when hot-path score falls in the "gray zone" (300-700). Applicant is asked to upload ID document + selfie.

**13B Vision-Language Model (TensorRT-LLM optimized):**
- **OCR Extraction:** Name, DOB, address, document number, expiration from driver's license or passport
- **Face Matching:** Embedding similarity between selfie and ID photo (threshold: cosine > 0.85)
- **Tampering Detection:** Font inconsistency, edge artifacts, metadata anomalies, Photoshop indicators
- **Cross-Reference:** Extracted fields vs. stated application fields (name match, DOB match, address match)

**Output:** Document confidence score + extracted fields → injected into FeatureBlock as additional features → XGBoost re-scored with augmented feature set.

**Sentinel Gateway mediates this path:**
- Token budget on VLM inference call
- Circuit breaker if VLM latency exceeds 5s
- Fallback: route to manual review if VLM unavailable
- All VLM outputs logged for audit trail

**Supplement: CapitalOne Transaction System Parallel**

Same architecture as my Tier 2 transaction fraud system: 13B TensorRT-LLM model with Sentinel gateway governance. The difference: transaction Tier 2 reasons about transaction patterns; identity warm path reasons about document images. Same serving infrastructure, same circuit breaker pattern, different domain.

### Cold Path — Agentic AI Investigation (Minutes to Hours)

For escalated cases, disputed declines, or cases where warm path is inconclusive.

**70B Reasoning Agent with Tool Access:**

| Tool | What It Does | Why |
|---|---|---|
| `pull_credit_bureau_full` | Full credit report with inquiry history, trade lines, public records | Complete credit picture |
| `search_fraud_database` | Internal confirmed fraud DB + consortium alerts | Prior confirmed fraud on this entity or linked entities |
| `deep_graph_traversal` | 5-hop TigerGraph query (vs. 2-hop in hot path) | Find distant but meaningful connections to fraud |
| `cross_reference_consortium` | Query shared fraud networks (Early Warning, etc.) | Cross-institutional fraud intelligence |
| `document_forensics` | Deep document analysis (metadata, pixel-level) | For suspected forgeries warm path missed |
| `generate_investigation_summary` | Structured risk narrative with evidence | Analyst-ready output |

**Agent Output to Fraud Analyst:**
```
INVESTIGATION SUMMARY — Application #A7829341
Risk Score: 285 (high risk)
Entity: E-4521 (linked to 3 prior applications in last 90 days)

KEY FINDINGS:
1. SSN first appeared in credit bureau 18 months ago (thin file, no prior history)
2. Phone number shared with entity E-3892 (confirmed bust-out, Dec 2024)
3. Device fingerprint seen in 8 applications across 4 different names
4. Address is a commercial mail receiving agency (CMRA)
5. Credit bureau shows 6 new inquiries in last 30 days across institutions

GRAPH VISUALIZATION: [Entity cluster diagram]
CONFIDENCE: High risk of synthetic identity
RECOMMENDATION: Decline

Evidence trail: [linked documents, graph paths, bureau excerpts]
```

**Tool Broker Pattern (from my Sentinel gateway):**
- Agent can ONLY call pre-approved tools (capability tokens)
- Each tool call is logged with input/output
- Agent is ADVISORY — never autonomously declines (regulatory requirement)
- Human analyst makes final decision

**Supplement: CapitalOne Transaction Tier 3 Parallel**

Same architecture as my Tier 3 transaction system: 70B agent with tool access, governed by Sentinel gateway's tool broker. The transaction Tier 3 investigates suspicious transaction patterns; the identity cold path investigates suspicious applicant identities. Same tool mediation, same audit trail, same human-in-the-loop requirement.

---

## 7. 360-Degree View: Identity Trust Score

### Concept

Instead of a single binary "fraud/not-fraud" decision, the system produces a **360-degree identity trust profile** — multiple scores across dimensions that can be consumed by different downstream systems.

```
┌─────────────────── 360° Identity Trust Profile ───────────────────┐
│                                                                     │
│  Identity Confidence:  0.82  (Is this a real person?)              │
│  Attribute Trust:      0.71  (Do the pieces fit together?)         │
│  Behavior Risk:        0.35  (Is the behavior suspicious?)         │
│  Network Risk:         0.62  (Are connections suspicious?)         │
│  Document Confidence:  0.91  (Is the document legitimate?)         │
│  Velocity Risk:        0.28  (Is there unusual activity?)          │
│  Similarity Risk:      0.45  (Similar to known bad patterns?)      │
│                                                                     │
│  COMPOSITE SCORE: 715 / 1000                                       │
│  DECISION: Approve                                                  │
│  REASON CODES: None (approved)                                      │
│                                                                     │
│  If declined, REASON CODES would be:                               │
│   R01: Device associated with multiple identities                  │
│   R07: Limited credit history (thin file)                          │
│   R12: Network connection to high-risk entities                    │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Why 360° Matters for Socure

Socure's product is literally a suite of scores (Sigma Fraud, Sigma Identity, Email Risk, Phone Risk, Address Risk, Device Risk). My 360° implementation maps directly:

| My CapitalOne Dimension | Socure Product |
|---|---|
| Identity Confidence | Sigma Identity |
| Composite Fraud Score | Sigma Fraud |
| Email Trust | EmailRisk |
| Phone Trust | PhoneRisk |
| Address Trust | AddressRisk |
| Device Trust | DeviceRisk |

**Interview line:** "We didn't produce a single score — we produced a profile of trust across dimensions. Each downstream consumer (credit decisioning, account monitoring, call center authentication) could use the dimensions relevant to their use case. This is architecturally identical to Socure's multi-score product suite."

### How Each Dimension Is Computed

Each dimension is a **mini-model** fed by its relevant feature subset:

| Dimension | Features Used | Model |
|---|---|---|
| Identity Confidence | Name-SSN-DOB consistency, bureau match, identity age, first-seen | Logistic regression (simple, interpretable) |
| Attribute Trust | Email-name match, phone-address geo, consistency signals | XGBoost (lightweight) |
| Behavior Risk | Form fill time, copy-paste, typing cadence, session anomaly | Transformer → score |
| Network Risk | Graph fraud-neighbor ratio, shared device count, cluster score | Graph features → XGBoost |
| Document Confidence | VLM extraction confidence, face match, tampering score | VLM output (warm path only) |
| Velocity Risk | Application velocity, device reuse rate, email/phone churn | Streaming counters → threshold |
| Similarity Risk | FAISS nearest-fraud distance, top-k fraud rate | Vector features → calibrated score |

The **Composite Score** is the final XGBoost that takes all dimension scores + raw features + branch health flags → calibrated 0-1000 score.

---

## 8. Feature Engineering Deep-Dive (Five Families)

### Family 1: Freshness Signals — "How old is this identity element?"

| Feature | Computation | Why It Catches Fraud |
|---|---|---|
| `email_age_days` | First-seen timestamp (internal + email intelligence) | Synthetic identities use freshly created emails |
| `phone_tenure_days` | Carrier lookup: activation date | Fraudsters use newly provisioned numbers |
| `address_first_seen_bureau_days` | Credit bureau first-reported date | Synthetic addresses appear recently |
| `identity_first_seen_days` | Internal entity first-seen timestamp | Entirely new entities have no history |
| `credit_file_age_months` | Bureau: oldest trade line age | Thin files are high risk for synthetic fraud |

**CapitalOne example:** "We found that applications where email + phone + address were ALL less than 90 days old had a 12x higher fraud rate than average. This 'freshness cluster' feature became our strongest single signal."

### Family 2: Consistency Signals — "Do the pieces fit together?"

| Feature | Computation | Why It Catches Fraud |
|---|---|---|
| `phone_geo_vs_address_distance_km` | Carrier region vs. stated address | Fraudsters assemble pieces from different regions |
| `email_name_match_score` | Token similarity: email local-part vs legal name | Stolen emails rarely match the fake name |
| `ip_geo_vs_address_distance_km` | IP geolocation vs. stated address | VPN/proxy from different country |
| `phone_carrier_vs_stated_country` | Phone country code vs. address country | Mismatched origin |
| `employment_income_vs_credit_profile` | Stated income vs. bureau-known capacity | Inflated income claims |

**CapitalOne example:** "We built a 'consistency vector' — a 5-dimensional mismatch score across geo/name/phone/email/income. When 3+ dimensions were inconsistent, fraud rate jumped 8x."

### Family 3: Velocity Signals — "How fast is this being reused?"

| Feature | Computation | Why It Catches Fraud |
|---|---|---|
| `device_applications_48h` | Rolling count: applications from this device | Fraud farms reuse devices |
| `phone_distinct_names_30d` | Distinct applicant names using this phone | One phone, many "identities" |
| `address_applications_7d` | Applications to this normalized address | Fraud rings cluster at addresses |
| `email_applications_24h` | Applications from this email | Automated application submission |
| `ssn_applications_30d` | Applications using this SSN (hashed) | Identity being shopped around |
| `ip_subnet_applications_1h` | Applications from /24 subnet | Botnet or fraud farm |

**CapitalOne example:** "Same sliding window pattern I used for transaction velocity (1h/6h/24h/7d) — but keyed by identity attributes instead of card/merchant. The implementation was the same streaming counters in our feature store."

### Family 4: Graph/Network Signals — "Who is this entity connected to?"

| Feature | Computation | Why It Catches Fraud |
|---|---|---|
| `graph_fraud_neighbor_ratio_2hop` | Fraud-labeled neighbors / total neighbors (2-hop) | Proximity to confirmed bad actors |
| `graph_shared_device_fraud_count` | Fraud entities sharing current device | Device contamination |
| `graph_synthetic_cluster_score` | Cluster of weak-history entities sharing infrastructure | Synthetic fraud ring pattern |
| `graph_min_distance_to_fraud` | Shortest graph path to confirmed fraud (capped at 3) | Direct or indirect connection |
| `identities_per_device` | Distinct entities on this device | Fraud farm indicator |

**Apple Siri parallel:** "At Siri, I built 2-hop TigerGraph traversals for relationship-aware retrieval — finding related content through entity connections. Same GSQL patterns, same bounded expansion, same hot-node capping. The query template transferred directly to identity fraud ring detection."

### Family 5: Behavioral Signals — "How did they fill out the form?"

| Feature | Computation | Why It Catches Fraud |
|---|---|---|
| `form_completion_seconds` | Time from form load to submit | Bots are fast, humans deliberate |
| `ssn_field_copy_paste` | Copy-paste event on SSN field | Real people type their SSN from memory |
| `typing_cadence_variance` | Keystroke interval standard deviation | Automated entry has low variance |
| `field_correction_count` | Backspace/correction events | Humans make typos; scripts don't |
| `tab_focus_events` | Times user left and returned to tab | Cross-referencing stolen data from another tab |

**NEW for identity (not in other docs):** Behavioral biometrics is the one area that didn't exist in my transaction or web security systems. The serving infrastructure is the same (streaming these signals, computing features in real-time) but the feature extraction from browser events is domain-specific to identity applications.

---

## 9. Parallel Model Scoring Architecture

### XGBoost — Primary Calibrated Scorer

- **Input:** 200+ features from all 5 families + branch outputs from Transformer/FAISS/Graph
- **Inference:** <1ms (CPU, no GPU needed)
- **Output:** Calibrated probability 0-1, top-N feature contributions (SHAP)
- **Why final layer:** Fast, handles heterogeneous tabular data, tolerates missing values, produces reason codes, easy to calibrate/audit/rollback

### Transformer — Behavioral Sequence Model

- **Input:** Last 16 events for this entity (application events, login events, device events), each encoded as categorical+numerical embedding
- **Architecture:** 4-layer compact Transformer, hidden_dim=128, trained with contrastive loss
- **Inference:** 5-10ms (GPU microbatched, TensorRT optimized)
- **Output:** behavior_score, sequence_anomaly_score, pooled_embedding (fed to FAISS and XGBoost)
- **Microbatching:** Strict 3ms max wait window, bucket sizes [1, 4, 8, 16, 32], CUDA Graph per bucket

**Broadcom parallel:** Same GPU microbatching with strict max-wait I built for Broadcom's DNN content classifier. Same CUDA Graph bucketing. Same deadline-aware queue discipline.

### FAISS — Vector Similarity Search

- **Indexes:** 
  - `known_fraud_patterns` — embeddings of confirmed fraud applications (HNSW, refreshed daily)
  - `recent_hot` — last 24h applications (IVF-Flat, refreshed every 5 minutes)
  - `synthetic_clusters` — cluster centroids from offline community detection (Flat, small)
- **Query:** Transformer's pooled_embedding → L2 normalized → search top-10 per index
- **Output:** nearest_fraud_distance, top_k_fraud_rate, synthetic_centroid_distance, recent_neighbor_density
- **Inference:** 2-5ms (GPU FAISS for hot index, CPU for small cluster index)

**Apple Siri parallel:** Same FAISS architecture I built for Siri — ACL-aware retrieval with HNSW indexes, NUMA-pinned, zero-copy query vectors. The difference: Siri searched content embeddings; this searches fraud pattern embeddings. Same infra.

### TigerGraph — Identity Graph Queries

- **Schema:** Identity, Email, Phone, Address, Device, IP, Document, BankAccount, Application, FraudCase (vertices) with timestamped edges
- **Hot-path queries:** Bounded 2-hop traversals, capped at 500 expansion per hop
- **Output:** graph_fraud_neighbor_ratio, graph_shared_device_count, graph_synthetic_cluster_score, graph_min_distance_to_fraud
- **Inference:** 10-30ms depending on neighborhood density
- **Fallback:** Pre-cached graph aggregates (stale but available in <1ms)

**Apple Siri parallel:** Same TigerGraph I deployed for Siri's knowledge graph — entity traversal for relationship-aware results. Same GSQL query patterns, same hot-node capping strategy, same approach to bounding traversal for latency control.

---

## 10. Score Calibration and Threshold Governance

### Why Calibration Matters

Raw XGBoost output (probability) must be **calibrated** so that thresholds are meaningful across segments:
- Score of 0.7 must MEAN "70% probability this identity is legitimate"
- Without calibration, thresholds set on one population drift on another

### Calibration Method

**Isotonic regression** on a held-out validation set:
- Train XGBoost on time-split train data
- Generate raw probabilities on validation set (different time period)
- Fit isotonic regression: raw_probability → calibrated_probability
- Evaluate calibration with reliability plots and Expected Calibration Error (ECE)
- Segment-level calibration: by channel (online vs mobile vs in-branch), by product, by applicant segment

### Threshold Governance

Thresholds are **business decisions**, not ML decisions:

```
Score > 700  →  Auto-Approve     (target: 70% of applications)
300-700      →  Step-Up           (target: 20% — document upload, OTP, knowledge questions)
Score < 300  →  Auto-Decline      (target: 10% — FCRA adverse action notice required)
```

Threshold governance committee: Fraud operations + Risk + Compliance + Product. Reviews weekly: fraud-loss rate at each band, false-positive rate, customer friction, regulatory complaints.

### Applicability to Socure

Socure's customers set their OWN thresholds on Socure's scores. If Socure's scores aren't calibrated, their customers' thresholds are meaningless. Same governance challenge at a platform level instead of single-institution level.

---

## 11. Explainability and Compliance

### FCRA Adverse Action Requirements

When we decline an application, federal law (Fair Credit Reporting Act) requires:
- Specific reasons for the adverse action
- Reasons must be understandable to the consumer
- Must be based on the factors that actually drove the decision

### Reason Code Generation

SHAP values → mapped to consumer-friendly reason codes:

| Top SHAP Feature | Reason Code | Consumer-Facing Text |
|---|---|---|
| `email_age_days` (low) | R03 | "Limited email history" |
| `graph_shared_device_fraud_count` (high) | R12 | "Device associated with high-risk activity" |
| `phone_tenure_days` (low) | R05 | "Recently provisioned phone number" |
| `credit_file_age_months` (low) | R08 | "Limited credit history" |
| `ip_geo_vs_address_distance_km` (high) | R15 | "Location inconsistency detected" |

Top-3 SHAP contributors → sorted by absolute impact → mapped to reason codes → included in adverse action notice.

### Score Decomposition for Operations

Internal fraud analysts see the full breakdown:
```
Composite Score: 285 (High Risk)
  Identity Confidence: 0.45 ← Low (thin file, 18-month SSN)
  Attribute Trust: 0.62 ← Medium (phone geo mismatch)
  Behavior Risk: 0.72 ← High (fast form fill, copy-paste SSN)
  Network Risk: 0.88 ← Very High (device linked to fraud)
  Velocity Risk: 0.65 ← Medium (3 applications in 30 days)
  Similarity Risk: 0.78 ← High (near known synthetic cluster)
  
Top contributing features:
  1. graph_shared_device_fraud_count = 3 (SHAP: +0.28)
  2. email_age_days = 12 (SHAP: +0.22)
  3. ssn_field_copy_paste = 1 (SHAP: +0.15)
```

---

## 12. Monitoring, Drift Detection, and Feedback Loop

### Real-Time Monitoring

| Signal | Alert Threshold | Response |
|---|---|---|
| Score distribution shift | KS > 0.05 from baseline | Investigate feature pipeline |
| Decision mix drift | >5% change in approve/decline ratio | Check model + threshold |
| Feature null rate spike | >2x normal for any critical feature | Cache/pipeline failure |
| Branch timeout rate | >1% for any model branch | Capacity or dependency issue |
| Latency p99 breach | >200ms | Traffic spike or degradation |
| Graph query expansion | >10x normal neighbor count | Hot-node or graph data issue |

### Fraud Pattern Drift

Fraudsters adapt. Monitor for:
- New device types appearing in fraud (emulators, new browser fingerprints)
- Fraud shifting to new geographic regions
- New synthetic identity construction patterns
- Feature importance shifts (previously weak features becoming strong)

### Feedback Loop

```
Decision → Outcome (60-day window for bust-out labels)
  → Label quality checks
  → Point-in-time training data assembly
  → Model retrain (monthly cadence, or triggered by drift)
  → Calibration refresh
  → Shadow scoring (new model scores live traffic, old model still decides)
  → A/B test or canary rollout
  → Full deployment
  → Monitoring continues
```

---

## 13. Interview Talking Points — How to Frame Each Requirement

### "Tell me about a real-time ML system you built"

> "At CapitalOne, I built the New Account Decisioning Service in the Fraud Detection Unit. When someone applies for a credit card, my system had 200ms to determine if the identity is real, trustworthy, and non-fraudulent. The architecture: Rust gateway → entity resolution → parallel feature assembly from internal cache, third-party enrichment, and TigerGraph — all with independent deadlines and circuit breakers → parallel model scoring with XGBoost, a compact Transformer, FAISS similarity search, and graph features → calibrated score → policy decision → FCRA-compliant reason codes. We achieved p99 under 110ms with 70% auto-approval rate."

### "How do you handle feature engineering for fraud?"

> "I organize features into five families: freshness signals (how old is each identity element), consistency signals (do the pieces fit together), velocity signals (how fast is this being reused), graph/network signals (who is this connected to), and behavioral signals (how did they interact with the form). Each family targets a different fraud typology. I used the same sliding-window streaming infrastructure from our transaction fraud system but keyed by identity attributes — device, phone, email, address — instead of card and merchant."

### "Tell me about graph-based fraud detection"

> "I deployed TigerGraph with a schema covering Identity, Email, Phone, Address, Device, IP, Document, and BankAccount as vertices, with timestamped edges. Hot-path queries were bounded 2-hop traversals producing features like fraud_neighbor_ratio, shared_device_count, and synthetic_cluster_score. We capped expansion at 500 per hop to avoid high-degree node latency — apartment buildings and corporate NAT could have thousands of edges. Graph features improved recall on fraud rings by 35% at the same false-positive rate because individual applications in a ring look clean, but their connections don't."

### "How did you use LLMs in fraud detection?"

> "We had a three-tier architecture. The hot path was classical ML — XGBoost with Transformer features — scoring in 200ms. The warm path used a 13B Vision-Language Model for document verification when applicants uploaded ID documents — it extracted fields, matched faces, and detected tampering in 2-5 seconds. The cold path deployed a 70B reasoning agent with tool access for fraud analyst investigations — it could pull credit reports, run deep graph traversals, query consortium databases, and generate structured investigation summaries. The agent was advisory only; human analysts made final decisions."

### "Why are you interested in Socure?"

> "I've built this exact system for a single institution and seen its limitations — specifically the cross-network visibility gap. At CapitalOne, we orchestrated 5 external enrichment APIs in parallel to approximate cross-network data, each adding latency and having coverage gaps. Socure solves this structurally — the cross-network view IS the product. That means I can focus on what I do best — making scoring faster, models sharper, calibration tighter, and the platform more reliable — without fighting for data access."

---

## 14. STAR Stories (Account Opening Focused)

### STAR 1: Real-Time Identity Fraud Scoring Engine

**Situation:** CapitalOne's card division was seeing $40M+ annual losses from synthetic identity fraud. The existing rule-based system caught only obvious cases and had a 6% false-positive rate that frustrated legitimate applicants.

**Task:** Build a real-time ML scoring system that could detect synthetic identities, fraud rings, and first-party fraud at the point of application — within 200ms — while reducing false positives and maintaining FCRA compliance.

**Action:**
- Designed the 3-tier architecture: hot path (XGBoost + Transformer + FAISS + TigerGraph, 200ms), warm path (13B VLM for document verification, 2-5s), cold path (70B agent for analyst assistance)
- Built entity resolution pipeline (deterministic keys + probabilistic matching) to link applications to historical entities
- Engineered 200+ features across 5 families (freshness, consistency, velocity, graph, behavioral)
- Deployed TigerGraph for 2-hop fraud ring detection with bounded traversals
- Built FAISS indexes for similarity to known fraud patterns
- Implemented calibrated scoring with isotonic regression + SHAP reason codes for FCRA

**Result:** 42% reduction in identity fraud losses. Auto-approval rate improved from 55% to 72% (less friction for good applicants). False-positive rate dropped from 6% to 1.8%. p99 latency: 110ms. Fraud ring detection recall improved 35% from graph features alone.

### STAR 2: Feature Engineering and Streaming Infrastructure

**Situation:** The initial identity scoring model used only static application attributes — name, address, income. It missed temporal and relational signals that distinguish synthetic from real identities.

**Task:** Build a streaming feature platform that could provide real-time velocity, freshness, consistency, and graph features without adding synchronous API calls to the scoring path.

**Action:**
- Deployed Kafka-based streaming pipeline consuming application, session, and verification events
- Built sliding-window counters keyed by device, phone, email, address, and IP (same infrastructure pattern as transaction velocity)
- Materialized features into Redis with freshness timestamps and schema versions
- Added 5 feature families (freshness, consistency, velocity, graph, behavioral) — 200+ features total
- Implemented point-in-time correct training data generation with as_of_timestamp gating
- Added feature_age and feature_missing indicators so the model could distinguish "new entity" from "data unavailable"

**Result:** Feature expansion from 50 → 200+ features improved PR-AUC from 0.72 to 0.89. The freshness-cluster feature (email + phone + address all <90 days) alone caught 18% of synthetic fraud that the previous model missed. Cache hit rate: 99.7%. Feature staleness p95: <500ms.

### STAR 3: Cross-Bank Data Integration Under Latency Constraints

**Situation:** Internal data alone wasn't sufficient — we needed cross-institution signals like credit bureau inquiries, consortium fraud flags, phone tenure, and email reputation. But each external API added 20-50ms and could fail independently.

**Task:** Integrate 5+ external enrichment sources into the real-time scoring path without destroying latency or creating cascading failures.

**Action:**
- Fired all enrichment calls in parallel (not sequential) with independent circuit breakers per source
- Set per-source deadlines: credit bureau 80ms, phone intelligence 50ms, email intelligence 40ms
- If a source missed its deadline: use `source_timeout=1` flag, XGBoost trained to handle missing enrichment
- Pre-cached slowly-changing reference data (address deliverability, IP reputation databases) to avoid API calls
- Implemented fallback hierarchy: cached value → degraded score → step-up (never hard-decline on missing enrichment alone)
- Monitored per-source p99, error rate, and feature lift to identify when sources degraded or stopped adding value

**Result:** 5 enrichment sources integrated within the existing 50ms feature-assembly budget (p99). No single source failure could cause a system-wide outage. When phone intelligence provider had a 2-hour outage, the system auto-degraded: affected 3% of scores, which were 0.02 less accurate but still within acceptable bands. Cross-bank features (credit bureau inquiries, consortium flags) improved synthetic fraud detection by 28%.

### STAR 4: Document Verification with Vision-Language Model (Warm Path)

**Situation:** Hot-path scoring sent ~20% of applications to step-up (score 300-700). These applicants uploaded ID documents, but verification was manual — analysts reviewed each one, creating a 4-hour backlog during peak times.

**Task:** Automate document verification using a Vision-Language Model while maintaining accuracy and audit compliance.

**Action:**
- Deployed 13B VLM (TensorRT-LLM optimized) for document processing: OCR extraction, face matching, tampering detection
- Built the Sentinel gateway governance layer: token budgets, circuit breaker at 5s timeout, fallback to manual review
- Cross-referenced VLM-extracted fields (name, DOB, address from ID) against application-stated fields
- Fed document confidence features back into XGBoost for re-scoring
- Maintained human-in-the-loop for edge cases (VLM confidence < 0.7)
- Logged all VLM outputs for audit and model improvement

**Result:** Automated 78% of document reviews (from 100% manual). Average step-up resolution time: 12 seconds (from 4 hours). Document fraud detection accuracy: 94% (vs. 91% for manual review). VLM latency p99: 3.2 seconds. Remaining 22% still routed to analysts (ambiguous cases).

---

## 15. Rapid Reference — Socure Requirement Coverage

| Socure Requirement | My CapitalOne Implementation | Supplement |
|---|---|---|
| Real-time identity scoring | Hot path: 200ms p99, XGBoost + multi-model | Broadcom: same parallel scoring pattern |
| Entity resolution | Two-stage: deterministic keys + probabilistic matching | Siri: entity matching for user profiles |
| Feature engineering | 5 families, 200+ features, streaming infrastructure | — |
| Graph-based fraud ring detection | TigerGraph 2-hop, bounded, capped expansion | Siri: same GSQL patterns, same graph infra |
| Vector similarity (FAISS) | 3 indexes: known_fraud, recent_hot, synthetic_clusters | Siri: same FAISS HNSW, NUMA-pinned |
| XGBoost calibrated scoring | Isotonic calibration, SHAP reason codes | — |
| Transformer for behavior | 4-layer compact, microbatched, TensorRT | Broadcom: same GPU microbatching |
| LLM document verification | 13B VLM warm path for ID + selfie | CapitalOne Tier 2: same TensorRT-LLM |
| Agentic AI investigation | 70B agent cold path with tool access | CapitalOne Tier 3: same Sentinel broker |
| Explainability / FCRA | SHAP → reason codes, score decomposition | — |
| Calibration | Isotonic regression, per-segment, monitored | — |
| Drift detection | PSI/KS, score distribution, label drift | Broadcom: same monitoring patterns |
| Cross-network data | Parallel enrichment, circuit breakers, graceful degradation | Socure native advantage (my interview pivot) |
| Multi-tenant SLA governance | — | Broadcom: 10K tenant isolation, per-tenant policies |
| Low-latency serving | p99 110ms, strict deadlines, CUDA Graph | Siri: zero-copy, NUMA-aware; Broadcom: dynamic batching |

---

## 16. Technical Deep-Dive References (in docs-1/)

These topics are already covered in depth in other documents. Reference them for implementation details:

| Topic | Document | What's Covered |
|---|---|---|
| CUDA optimization, streams, CUDA Graph | `docs-1/CUDA-Deep-Dive.md` | Kernel optimization, memory management, profiling |
| TensorRT, model serving | `docs-1/Inference-Deep-Dive.md` | TensorRT-LLM, batching strategies, GPU serving |
| FAISS architecture | `docs-1/Search-Deep-Dive.md` | Index types, HNSW vs IVF, recall optimization |
| System programming | `docs-1/System_Programming_Interview.md` | Rust, memory safety, zero-copy, async |
| OS/Network layers | `docs-1/1-OS.md`, `docs-1/2-Network.md` | Kernel tuning, TCP optimization, NUMA |

### What's NEW in this document (not in docs-1/):
- Entity resolution (probabilistic matching, two-stage pipeline)
- Identity-specific feature engineering (5 families for identity fraud)
- Score calibration and threshold governance
- FCRA/ECOA compliance and reason code generation
- Label delay strategy for fraud (60-day bust-out window)
- Vision-Language Model for document verification
- 360° identity trust profile (multi-dimensional scoring)
- Cross-bank data integration under latency constraints
- Graph schema specifically for identity fraud (vs. general graph patterns)

---

