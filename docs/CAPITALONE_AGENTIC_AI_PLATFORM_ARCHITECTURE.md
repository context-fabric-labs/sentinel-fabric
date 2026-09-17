# Capital One Agentic AI Platform — Complete Architecture Design

**Codename:** Sentinel Agent Fabric (SAF)
**Author / Owner:** Principal Engineer, AI Agent Platform
**Audience:** Senior Leadership, Platform Engineering, LOB Architecture Councils, Security & Compliance
**Status:** Architecture Design — Production-ready Blueprint
**Platform analogue:** Internal Agent Building & Orchestration Platform layered on SIF (Sentinel Inference Fabric)

---

## Section 1 — Executive Summary

Capital One customers (both internal LOBs and external banking customers) need to build, deploy, and operate **agentic AI systems** that can autonomously reason about fraud detection, transaction monitoring, customer service workflows, compliance checks, and multi-step financial operations. Unlike simple LLM inference, agents require **stateful multi-turn execution**, **tool integration**, **memory management**, **RAG pipelines**, and **orchestration across multiple models**.

**Sentinel Agent Fabric (SAF)** is Capital One's end-to-end agentic AI platform that enables:

1. **Agent Building:** Low-code/no-code agent builder with visual workflow designer, tool registry, memory configuration, and RAG pipeline assembly
2. **Agent Deployment:** One-click deployment to SIF's three-tier architecture (Pay-as-you-Go, Committed Capacity, Dedicated Ultra-Low-Latency)
3. **Agent Execution:** Stateful orchestration engine with checkpointing, retry logic, parallel tool execution, and human-in-the-loop gates
4. **Agent Governance:** Compliance validation, audit trails, cost attribution, and security guardrails specific to agentic workflows

### Key Differentiators

- **Fraud-Detection Optimized:** Pre-built agent templates for transaction monitoring, narrative generation, case investigation, and regulatory reporting
- **Stateful by Design:** Every agent session maintains conversation state, tool execution history, and intermediate reasoning traces
- **Memory-Aware:** Three-tier memory architecture (working memory, episodic memory, semantic memory) with tenant isolation
- **RAG-Native:** Built-in retrieval pipelines connecting to Capital One's knowledge bases, policy documents, transaction histories, and fraud patterns
- **Tool Ecosystem:** Secure tool registry with PCI-compliant integrations to core banking systems, fraud databases, customer profiles, and external data sources
- **SIF Integration:** Leverages existing three-tier GPU infrastructure with KV-cache optimization for agent session continuity

---

## Section 2 — Requirements Gathering Framework

### 2.1 Stakeholder Analysis

| Stakeholder                    | Needs                                                                                    | Constraints                                           |
| ------------------------------ | ---------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| **Fraud Operations**     | Real-time fraud detection agents, case narrative generation, pattern recognition         | < 500ms latency, 99.9% uptime, PCI-DSS compliance     |
| **Customer Service**     | Conversational agents for account inquiries, dispute resolution, product recommendations | PII protection, audit trails, escalation to humans    |
| **Compliance & Risk**    | Regulatory reporting agents, policy enforcement, anomaly detection                       | SOC2, audit logging, explainability, model governance |
| **Software Engineering** | Developer copilot agents for code review, testing, deployment automation                 | Internal network only, no external API calls          |
| **Data Science**         | Custom agent training, fine-tuning pipelines, evaluation frameworks                      | GPU access, experiment tracking, A/B testing          |

### 2.2 Agent Use Case Taxonomy

**Tier A — Real-Time Fraud Detection (Latency-Critical)**

- Transaction scoring agents (sub-100ms decision)
- Account takeover detection (multi-signal correlation)
- Synthetic identity detection (cross-referencing databases)
- Money laundering pattern recognition (graph-based reasoning)

**Tier B — Customer-Facing Conversational (Latency-Sensitive)**

- Fraud alert explanation agents (customer notification)
- Dispute intake and triage agents
- Account security recommendation agents
- Financial literacy and education agents

**Tier C — Back-Office Operations (Throughput-Optimized)**

- Case investigation agents (multi-hour workflows)
- Regulatory report generation (SAR, CTR filing support)
- Policy compliance checking agents
- Training and simulation agents for fraud analysts

**Tier D — Developer & Internal Tools (Best-Effort)**

- Code review and security scanning agents
- Test generation agents
- Documentation and runbook creation agents
- Incident response orchestration agents

### 2.3 Capacity Planning Methodology

**Step 1: Demand Forecasting**

```
Daily Active Agents (DAA) = Σ(LOB-subscribed agents)
Concurrent Sessions = DAA × Session Frequency × Session Duration
Token Budget = Concurrent Sessions × Avg Tokens per Turn × Turns per Session
GPU Hours = Token Budget / (Tokens per Second per GPU × Utilization Factor)
```

**Example Calculation for Fraud Detection:**

- 50 fraud detection agents deployed
- Each handles 200 sessions/day with 5-minute avg duration
- Peak concurrency: 50 × 200 × (5/1440) = ~35 concurrent sessions
- Token budget: 35 sessions × 8K context × 10 turns = 2.8M tokens/hour peak
- GPU requirement: 2.8M / (150K tokens/sec × 0.7 util × 3600) = ~7.4 H100 GPUs

**Step 2: Tier Allocation**

| Use Case                               | Tier 1 (Shared) | Tier 2 (Committed) | Tier 3 (Dedicated) |
| -------------------------------------- | --------------- | ------------------ | ------------------ |
| **Experimentation**              | 100%            | 0%                 | 0%                 |
| **Dev/Test**                     | 80%             | 20%                | 0%                 |
| **Production (Internal)**        | 30%             | 60%                | 10%                |
| **Production (Customer-Facing)** | 10%             | 50%                | 40%                |
| **Fraud Real-Time**              | 0%              | 20%                | 80%                |

**Step 3: Memory & Storage Planning**

```
Working Memory (HBM) = Concurrent Sessions × Max Context Length × KV Cache Size
  = 1000 sessions × 32K tokens × 2 bytes/token = 64 GB HBM per replica

Episodic Memory (NVMe) = Sessions per Day × Avg Session Size × Retention Days
  = 50K sessions × 50KB × 30 days = 75 TB NVMe (compressed)

Semantic Memory (Vector DB) = Total Documents × Embedding Size × Redundancy
  = 10M docs × 1536 dims × 4 bytes × 3 replicas = 180 GB
```

**Step 4: RAG Throughput Planning**

```
Retrieval QPS = Concurrent Sessions × Retrieval Calls per Turn
  = 1000 × 2 = 2000 queries/sec

Vector DB Capacity = Retrieval QPS × Latency Target × Parallelism Factor
  = 2000 × 50ms × 3 = 300 concurrent connections

Embedding GPU = Retrieval QPS / (Embeddings per Second per GPU)
  = 2000 / 5000 = 0.4 → 1 dedicated embedding GPU
```

---

## Section 3 — Architecture Planes

### 3.1 Control Plane — Agent Lifecycle Management

```
┌─────────────────────────────────────────────────────────────────┐
│                  AGENT CONTROL PLANE                             │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────────┐    │
│  │ Agent Studio│  │ Agent Registry│  │ Policy & Governance │    │
│  │ (UI/CLI)    │  │ (Metadata DB) │  │ (Compliance Engine) │    │
│  └──────┬──────┘  └──────┬───────┘  └──────────┬──────────┘    │
│         │                 │                      │               │
│         └─────────────────┴──────────────────────┘               │
│                           │                                      │
│                  ┌────────▼────────┐                             │
│                  │ Agent Compiler  │                             │
│                  │ (Workflow → DAG)│                             │
│                  └────────┬────────┘                             │
│                           │                                      │
│                  ┌────────▼────────┐                             │
│                  │ Deployment Mgr  │                             │
│                  │ (GitOps → SIF)  │                             │
│                  └─────────────────┘                             │
└─────────────────────────────────────────────────────────────────┘
```

**Agent Studio Components:**

1. **Visual Workflow Designer**

   - Drag-and-drop node editor for agent logic (LLM calls, tool invocations, conditionals, loops)
   - Pre-built templates for fraud detection patterns (transaction scoring, narrative generation, case escalation)
   - Version control with diff visualization and rollback capability
   - Collaborative editing with role-based access (developer, reviewer, approver)
2. **Tool Configuration**

   - Tool registry browser with search and categorization
   - Tool parameter mapping and schema validation
   - Authentication configuration (OAuth, API keys, service accounts)
   - Rate limiting and quota settings per tool
3. **Memory & RAG Configuration**

   - Working memory size and eviction policy selection
   - Episodic memory retention period and compression settings
   - Semantic memory index selection (which knowledge bases to connect)
   - Retrieval strategy (top-K, similarity threshold, re-ranking)
4. **Model & Tier Selection**

   - Base model selection (Llama-3-70B, Mistral-Large, Claude-3, etc.)
   - LoRA adapter attachment for domain fine-tuning
   - SIF tier assignment (Tier 1/2/3) with cost estimation
   - Fallback model configuration for degraded modes

**Agent Registry Schema:**

```yaml
agent_id: fraud-detection-transaction-scorer-v3
tenant_id: fraud-operations-lob
version: 3.2.1
status: production

metadata:
  name: Real-Time Transaction Scorer
  description: Scores transactions for fraud risk in <100ms
  owner: fraud-platform-team
  compliance_class: PCI-DSS
  data_residency: us-east-1

spec:
  workflow:
    entry_point: score_transaction
    nodes:
      - id: fetch_customer_profile
        type: tool_call
        tool: customer-profile-service
        timeout_ms: 50
      - id: analyze_transaction_pattern
        type: llm_call
        model: llama-3-70b-instruct
        max_tokens: 500
        temperature: 0.1
      - id: query_fraud_graph
        type: tool_call
        tool: neo4j-fraud-graph
        parallel: true
      - id: generate_risk_score
        type: llm_call
        model: llama-3-70b-instruct
        depends_on: [analyze_transaction_pattern, query_fraud_graph]
  
  memory:
    working_memory:
      max_context_tokens: 8192
      eviction_policy: lru
    episodic_memory:
      enabled: true
      retention_days: 90
      compression: true
    semantic_memory:
      indices:
        - fraud-patterns-kb
        - regulatory-policies
        - historical-cases
  
  tools:
    - name: customer-profile-service
      auth_type: spiFFE
      rate_limit: 1000/min
    - name: neo4j-fraud-graph
      auth_type: service_account
      rate_limit: 500/min
  
  deployment:
    sif_tier: tier3_dedicated
    min_replicas: 3
    max_replicas: 10
    gpu_type: h100
    latency_sla_ms: 100
  
  guardrails:
    input_validation: strict
    output_filtering: pci-compliant
    pii_redaction: enabled
    audit_logging: full
```

### 3.2 Data Plane — Agent Execution Runtime

```
┌──────────────────────────────────────────────────────────────────────┐
│                     AGENT DATA PLANE                                  │
│                                                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │              Agent Orchestrator (Per-Agent Instance)          │   │
│  │  ┌─────────────┐  ┌──────────────┐  ┌─────────────────────┐  │   │
│  │  │ State Mgr   │  │ Checkpointing│  │ Retry & Backoff     │  │   │
│  │  │ (Session)   │  │ (S3 + NVMe)  │  │ (Exponential)       │  │   │
│  │  └─────────────┘  └──────────────┘  └─────────────────────┘  │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐       │
│  │ LLM Executor │  │ Tool Executor│  │ RAG Executor         │       │
│  │ (SIF vLLM)   │  │ (Secure RPC) │  │ (Vector + Keyword)   │       │
│  └──────┬───────┘  └──────┬───────┘  └──────────┬───────────┘       │
│         │                  │                      │                   │
│         └──────────────────┴──────────────────────┘                   │
│                            │                                          │
│                  ┌─────────▼─────────┐                                │
│                  │  Memory Manager   │                                │
│                  │  ┌───────────────┐│                                │
│                  │  │ Working Memory││ (HBM - KV Cache)              │
│                  │  │ Episodic Mem  ││ (NVMe - Session Store)        │
│                  │  │ Semantic Mem  ││ (Vector DB - Knowledge)       │
│                  │  └───────────────┘│                                │
│                  └───────────────────┘                                │
└──────────────────────────────────────────────────────────────────────┘
```

**Agent Orchestrator:**

The orchestrator is the brain of each agent instance, managing the execution lifecycle:

1. **State Management**

   - Maintains session state machine (initialized → running → waiting → completed → failed)
   - Tracks node execution progress in the workflow DAG
   - Manages variable bindings and context accumulation
   - Handles human-in-the-loop pauses and resumptions
2. **Checkpointing Strategy**

   - **Micro-checkpoints:** After every tool call and LLM invocation (sub-second)
   - **Macro-checkpoints:** At workflow branch points and decision gates
   - **Persistent storage:** Checkpoints written to S3 via async I/O (non-blocking)
   - **Recovery:** On failure, reloads last checkpoint and replays from that point
3. **Retry & Circuit Breaker**

   - Exponential backoff with jitter for transient failures
   - Per-tool circuit breakers (open after 5 consecutive failures)
   - Fallback tool substitution (e.g., cached response if primary tool unavailable)
   - Graceful degradation (skip non-critical nodes, continue execution)

**Execution Modes:**

| Mode                   | Description                                        | Use Case                              |
| ---------------------- | -------------------------------------------------- | ------------------------------------- |
| **Synchronous**  | Blocking execution, wait for each node to complete | Real-time fraud scoring (<100ms)      |
| **Asynchronous** | Non-blocking, fire-and-forget with callbacks       | Case investigation workflows          |
| **Streaming**    | Token-by-token streaming with intermediate results | Customer-facing conversational agents |
| **Batch**        | Bulk processing of multiple inputs                 | Overnight transaction re-scoring      |

### 3.3 Memory Plane — Three-Tier Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    MEMORY ARCHITECTURE                           │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              WORKING MEMORY (HBM - GPU)                  │   │
│  │  • Current conversation context (up to 32K tokens)       │   │
│  │  • Active tool call parameters and results               │   │
│  │  • Intermediate reasoning traces (CoT, ToT)              │   │
│  │  • KV cache for fast attention lookup                    │   │
│  │  • Eviction: LRU when context window full                │   │
│  └─────────────────────────────────────────────────────────┘   │
│                             ▲                                   │
│                             │ spill/fill                        │
│  ┌──────────────────────────▼────────────────────────────────┐ │
│  │              EPISODIC MEMORY (NVMe - Session Store)        │ │
│  │  • Full conversation history (beyond context window)       │ │
│  │  • Tool execution logs and outcomes                        │ │
│  │  • User preferences and interaction patterns               │ │
│  │  • Compressed session summaries (LLM-generated)            │ │
│  │  • Retention: 30-90 days, then archived to S3              │ │
│  └──────────────────────────┬────────────────────────────────┘ │
│                             │ retrieval                         │
│  ┌──────────────────────────▼────────────────────────────────┐ │
│  │              SEMANTIC MEMORY (Vector DB - Knowledge)       │ │
│  │  • Fraud pattern knowledge base                            │ │
│  │  • Regulatory policies and procedures                      │ │
│  │  • Historical case studies and precedents                  │ │
│  │  • Product documentation and FAQs                          │ │
│  │  • Embeddings: 1536-dim, HNSW index, GPU-accelerated       │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

**Working Memory (HBM):**

- **Capacity:** 8K-128K tokens per session (configurable per agent)
- **Implementation:** vLLM PagedAttention with LMCache integration
- **Isolation:** Per-tenant KV cache namespaces (no cross-tenant leakage)
- **Optimization:** Prefix caching for repeated system prompts and tool schemas
- **Spill Strategy:** When HBM full, oldest KV blocks evicted to NVMe episodic memory

**Episodic Memory (NVMe):**

- **Schema:**

  ```json
  {
    "session_id": "uuid",
    "agent_id": "fraud-detection-v3",
    "tenant_id": "fraud-ops",
    "user_id": "customer-12345",
    "started_at": "2026-07-06T10:30:00Z",
    "last_accessed": "2026-07-06T10:35:00Z",
    "turns": [
      {
        "turn_id": 1,
        "role": "user",
        "content": "Transaction declined, why?",
        "timestamp": "2026-07-06T10:30:05Z",
        "tool_calls": [...],
        "llm_response": {...}
      }
    ],
    "metadata": {
      "total_tokens": 15420,
      "tool_invocations": 8,
      "outcome": "resolved",
      "satisfaction_score": 0.9
    }
  }
  ```
- **Compression:** LLM-generated summaries for long sessions (e.g., "Customer asked about 3 declined transactions, all resolved as false positives")
- **Indexing:** Session metadata indexed in PostgreSQL for fast lookup
- **Archival:** Sessions older than 90 days compressed and moved to S3 Glacier

**Semantic Memory (Vector DB):**

- **Technology:** Weaviate or Pinecone with GPU-accelerated indexing
- **Indices:**

  - `fraud-patterns-kb`: 500K documents (fraud typologies, red flags, investigation techniques)
  - `regulatory-policies`: 50K documents (FFIEC, OCC, CFPB guidelines, internal policies)
  - `historical-cases`: 2M documents (anonymized past fraud cases, SAR filings)
  - `product-docs`: 100K documents (credit cards, loans, accounts, features)
  - `customer-faqs`: 10K documents (common inquiries, troubleshooting steps)
- **Retrieval Pipeline:**

  1. Query embedding (1536-dim, E5-large-v2 model)
  2. HNSW approximate nearest neighbor search (top-50 candidates)
  3. Cross-encoder re-ranking (ColBERT, top-10 final)
  4. Metadata filtering (compliance class, date range, LOB ownership)
  5. Context assembly (chunk concatenation with overlap handling)

### 3.4 RAG Plane — Retrieval-Augmented Generation

```
┌─────────────────────────────────────────────────────────────────────┐
│                       RAG PIPELINE                                   │
│                                                                      │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐      │
│  │  Query   │    │  Query   │    │  Query   │    │  Query   │      │
│  │ Analysis │    │ Rewrite  │    │ Routing  │    │ Execution│      │
│  └────┬─────┘    └────┬─────┘    └────┬─────┘    └────┬─────┘      │
│       │               │               │               │              │
│       ▼               ▼               ▼               ▼              │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Query Router                              │   │
│  │  • Intent classification (factual vs analytical vs creative) │   │
│  │  • Index selection (fraud-kb vs policies vs cases)           │   │
│  │  • Multi-index fusion (union/intersection strategies)        │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                      │
│         ┌────────────────────┼────────────────────┐                │
│         ▼                    ▼                    ▼                │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐          │
│  │ Vector DB   │     │ Keyword DB  │     │ SQL DB      │          │
│  │ (Semantic)  │     │ (BM25)      │     │ (Structured)│          │
│  └──────┬──────┘     └──────┬──────┘     └──────┬──────┘          │
│         │                   │                   │                  │
│         └───────────────────┴───────────────────┘                  │
│                             │                                      │
│                    ┌────────▼────────┐                             │
│                    │  Re-Ranker      │                             │
│                    │  (Cross-Encoder)│                             │
│                    └────────┬────────┘                             │
│                             │                                      │
│                    ┌────────▼────────┐                             │
│                    │ Context Builder │                             │
│                    │ (Chunk Assembly)│                             │
│                    └────────┬────────┘                             │
│                             │                                      │
│                    ┌────────▼────────┐                             │
│                    │   LLM Prompt    │                             │
│                    │   (with RAG)    │                             │
│                    └─────────────────┘                             │
└─────────────────────────────────────────────────────────────────────┘
```

**RAG Strategies by Use Case:**

| Use Case                      | Retrieval Strategy                                   | Re-Ranking    | Context Size |
| ----------------------------- | ---------------------------------------------------- | ------------- | ------------ |
| **Fraud Pattern Match** | Vector (fraud-kb) + SQL (transaction history)        | ColBERT       | 8K tokens    |
| **Policy Compliance**   | Keyword (policy docs) + Vector (regulations)         | Cross-encoder | 16K tokens   |
| **Case Investigation**  | Multi-index fusion (cases + policies + transactions) | LLM-based     | 32K tokens   |
| **Customer FAQ**        | Vector (FAQ) + Keyword (product docs)                | Lightweight   | 4K tokens    |

**Advanced RAG Features:**

1. **Query Rewriting**

   - LLM-based query expansion (add synonyms, related concepts)
   - HyDE (Hypothetical Document Embeddings): Generate hypothetical answer, embed, retrieve
   - Step-back prompting: Extract broader concepts for better retrieval
2. **Retrieval Optimization**

   - Caching: Embedding cache for repeated queries (Redis, 24hr TTL)
   - Pre-fetching: Anticipate next retrieval based on workflow state
   - Parallel retrieval: Query multiple indices concurrently
3. **Context Optimization**

   - Chunking strategies: Semantic chunking (by topic) vs fixed-size
   - Overlap handling: Remove duplicate content across chunks
   - Compression: LLM-based summarization of retrieved chunks before injection

### 3.5 Security Plane — Zero-Trust Agent Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     SECURITY ARCHITECTURE                            │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              Identity & Access Control                       │   │
│  │  • SPIFFE/SPIRE workload identities (per-agent instance)     │   │
│  │  • OAuth 2.0 / OIDC for user authentication                  │   │
│  │  • RBAC + ABAC (role + attribute-based access control)       │   │
│  │  • Least-privilege tool permissions (per-agent, per-tool)    │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              Data Protection                                 │   │
│  │  • PII redaction at ingress (before LLM processing)          │   │
│  │  • Token-level encryption (KMS-managed keys)                 │   │
│  │  • Memory encryption (HBM, NVMe, Vector DB at-rest)          │   │
│  │  • Secure enclaves (Nitro Enclaves for sensitive workloads)  │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              Guardrails & Compliance                         │   │
│  │  • Input validation (schema, content, intent)                │   │
│  │  • Output filtering (toxic content, PII leakage, hallucination)│ │
│  │  • Tool call authorization (policy-based, real-time)         │   │
│  │  • Audit logging (immutable, tamper-evident)                 │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              Network Security                                │   │
│  │  • mTLS everywhere (SPIRE-issued certificates)               │   │
│  │  • Network policies (default-deny, per-agent isolation)      │   │
│  │  • VPC endpoints (private connectivity to AWS services)      │   │
│  │  • WAF + DDoS protection (agent-facing APIs)                 │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

**Security Controls by Layer:**

| Layer                    | Control           | Implementation                                      |
| ------------------------ | ----------------- | --------------------------------------------------- |
| **User Auth**      | MFA + SSO         | Okta integration, SAML 2.0                          |
| **Agent Identity** | Workload Identity | SPIFFE SVIDs, auto-rotated                          |
| **Tool Access**    | OAuth Scopes      | Per-tool OAuth tokens, scoped to agent              |
| **Data Ingress**   | PII Redaction     | Presidio NER, regex patterns, custom models         |
| **LLM Processing** | Content Filtering | Llama Guard, custom fraud-specific classifiers      |
| **Tool Egress**    | API Gateway       | Rate limiting, request signing, response validation |
| **Memory**         | Encryption        | AES-256 at-rest, TLS 1.3 in-transit                 |
| **Audit**          | Immutable Logs    | Write-once S3 buckets, CloudTrail integration       |

**Compliance Mappings:**

- **PCI-DSS:** Cardholder data never enters LLM context; redacted before processing
- **SOC2:** Full audit trail of agent actions, access logs, change management
- **FFIEC:** Model risk management (SR 11-7), model validation, explainability
- **GDPR/CCPA:** Right to deletion (episodic memory purge), data minimization
- **Reg B (ECOA):** Adverse action explanations, fair lending compliance

### 3.6 Tool Plane — Secure Integration Ecosystem

```
┌─────────────────────────────────────────────────────────────────────┐
│                       TOOL REGISTRY                                  │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              Tool Categories                                 │   │
│  │                                                              │   │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐│   │
│  │  │ Data Retrieval  │  │ Action Tools    │  │ Analysis     ││   │
│  │  │ • Customer Prof │  │ • Block Card    │  │ • Risk Score ││   │
│  │  │ • Transaction   │  │ • Freeze Account│  │ • Pattern Rec││   │
│  │  │ • Credit Report │  │ • Send Alert    │  │ • Anomaly Det││   │
│  │  └─────────────────┘  └─────────────────┘  └──────────────┘│   │
│  │                                                              │   │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐│   │
│  │  │ External APIs   │  │ Internal Svcs   │  │ Custom Code  ││   │
│  │  │ • Credit Bureaus│  │ • Core Banking  │  │ • Python UDF ││   │
│  │  │ • Law Enf       │  │ • Fraud Platform│  │ • Lambda Fn  ││   │
│  │  │ • Identity Verify│  │ • Case Mgmt     │  │ • Notebook   ││   │
│  │  └─────────────────┘  └─────────────────┘  └──────────────┘│   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              Tool Security Model                             │   │
│  │  • Tool schema validation (OpenAPI spec)                     │   │
│  │  • Authentication config (OAuth, API key, mTLS, IAM role)    │   │
│  │  • Rate limiting (per-agent, per-tenant, global)             │   │
│  │  • Input/output transformation (schema mapping)              │   │
│  │  • Error handling (retry policy, fallback, circuit breaker)  │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

**Tool Definition Schema:**

```yaml
tool_id: customer-profile-service
version: 2.1.0
category: data_retrieval

metadata:
  name: Customer Profile Service
  description: Retrieve customer profile, accounts, and relationship data
  owner: customer-data-platform
  compliance_class: PCI-DSS
  data_classification: confidential

spec:
  openapi_spec: |
    openapi: 3.0.0
    paths:
      /customers/{customer_id}/profile:
        get:
          parameters:
            - name: customer_id
              in: path
              required: true
              schema:
                type: string
          responses:
            200:
              description: Customer profile
              content:
                application/json:
                  schema:
                    $ref: '#/components/schemas/CustomerProfile'
  
  authentication:
    type: spiffe
    workload_identity: customer-profile-svc
    allowed_audiences:
      - agent-platform
  
  authorization:
    policy: |
      allow if {
        input.agent_tier in ["tier2", "tier3"]
        input.compliance_class == "PCI-DSS"
        input.purpose in ["fraud_detection", "customer_service"]
      }
  
  rate_limits:
    - scope: per_agent
      limit: 100 requests/min
    - scope: per_tenant
      limit: 1000 requests/min
    - scope: global
      limit: 50000 requests/min
  
  input_transformation:
    pre_hook: |
      def transform(input, context):
        # Redact PII from input before sending to tool
        input.customer_id = redact_ssn(input.customer_id)
        return input
  
  output_transformation:
    post_hook: |
      def transform(output, context):
        # Filter sensitive fields from output
        filtered = {k: v for k, v in output.items() if k not in ['ssn', 'full_dob']}
        return filtered
  
  error_handling:
    retry_policy:
      max_retries: 3
      backoff: exponential
      retry_on: [503, 429, network_error]
    fallback:
      type: cached_response
      max_age: 5 minutes
    circuit_breaker:
      failure_threshold: 5
      recovery_timeout: 60 seconds
```

---

## Section 4 — Execution Model

### 4.1 Agent Workflow Execution

**Execution Lifecycle:**

```
1. INITIALIZATION
   ├─ Load agent definition from registry
   ├─ Initialize working memory (allocate KV cache)
   ├─ Load system prompts and tool schemas into prefix cache
   └─ Validate deployment tier capacity

2. SESSION START
   ├─ Generate session_id and checkpoint key
   ├─ Load episodic memory (if returning customer)
   ├─ Retrieve relevant semantic memory (RAG pre-fetch)
   └─ Enter workflow entry point

3. NODE EXECUTION (per workflow node)
   ├─ Evaluate preconditions (guardrails, data availability)
   ├─ Execute node type:
   │   ├─ LLM Call → SIF inference endpoint (with KV-aware routing)
   │   ├─ Tool Call → Secure RPC with retry/circuit breaker
   │   ├─ Conditional → Evaluate expression, branch accordingly
   │   └─ Parallel → Spawn concurrent executions, wait for all
   ├─ Capture output and update working memory
   ├─ Write micro-checkpoint (async to S3)
   └─ Evaluate postconditions (output validation, guardrails)

4. HUMAN-IN-THE-LOOP (if configured)
   ├─ Pause execution at gate node
   ├─ Notify human reviewer (Slack, email, dashboard)
   ├─ Wait for approval/rejection/modification
   └─ Resume execution with human input

5. COMPLETION
   ├─ Generate final response
   ├─ Write session summary to episodic memory
   ├─ Emit telemetry (latency, tokens, tool calls, outcome)
   ├─ Emit cost attribution event
   └─ Cleanup working memory (or retain for continuation)

6. FAILURE HANDLING
   ├─ On error: write failure checkpoint with stack trace
   ├─ Retry node (if retry policy allows)
   ├─ Fallback path (if configured)
   ├─ Escalate to human (if critical failure)
   └─ Emit alert (PagerDuty, Slack)
```

### 4.2 Parallel Execution Patterns

**Pattern 1: Fan-Out / Fan-In**

```yaml
node: investigate_transaction
type: parallel
nodes:
  - id: check_customer_history
    type: tool_call
    tool: transaction-history-service
  - id: check_device_fingerprint
    type: tool_call
    tool: device-intelligence-service
  - id: check_merchant_risk
    type: tool_call
    tool: merchant-risk-service
  - id: analyze_transaction_pattern
    type: llm_call
    model: llama-3-70b

join_strategy: all_complete  # wait for all branches
timeout_ms: 500
```

**Pattern 2: Map-Reduce**

```yaml
node: analyze_multiple_transactions
type: map_reduce
input_list: "{{session.pending_transactions}}"
map_node:
  id: score_single_transaction
  type: llm_call
  prompt: "Score this transaction for fraud risk: {{item}}"
reduce_node:
  id: aggregate_scores
  type: llm_call
  prompt: "Given these scores: {{map_outputs}}, determine overall risk level"
```

**Pattern 3: Sequential with Conditional Branching**

```yaml
entry_point: initial_assessment

nodes:
  - id: initial_assessment
    type: llm_call
    next_condition:
      - if: "{{llm_output.risk_level}} == 'high'"
        goto: deep_investigation
      - if: "{{llm_output.risk_level}} == 'medium'"
        goto: standard_review
      - else:
        goto: auto_approve
  
  - id: deep_investigation
    type: parallel
    nodes: [...]
    next: generate_sar
  
  - id: standard_review
    type: tool_call
    tool: case-management-service
    next: human_review
  
  - id: auto_approve
    type: tool_call
    tool: transaction-approval-service
    next: end
  
  - id: human_review
    type: human_input
    next: final_decision
```

### 4.3 State Management & Checkpointing

**Checkpoint Strategy:**

```python
class AgentCheckpoint:
    session_id: str
    workflow_state: dict  # Current node, variable bindings
    memory_snapshot: dict  # Working memory state
    execution_log: list  # Tool calls, LLM responses, timestamps
    s3_path: str  # Persistent storage location
    created_at: datetime

# Checkpoint frequency
MICRO_CHECKPOINT_INTERVAL = "after_every_node"  # Default
MACRO_CHECKPOINT_INTERVAL = "at_branch_points"   # Conditionals, loops
PERSISTENT_CHECKPOINT_INTERVAL = "every_5_minutes"  # Async to S3

# Recovery logic
def recover_from_checkpoint(session_id):
    latest_checkpoint = load_latest_checkpoint(session_id)
    restore_memory(latest_checkpoint.memory_snapshot)
    replay_execution_log(latest_checkpoint.execution_log)
    resume_from_node(latest_checkpoint.workflow_state.current_node)
```

**State Machine:**

```
┌─────────────┐
│ Initialized │
└──────┬──────┘
       │ start()
       ▼
┌─────────────┐
│   Running   │◄──────────────────────┐
└──────┬──────┘                       │
       │                              │
       ├─► wait_for_human() ──►┌──────┴──────┐
       │                       │   Waiting   │
       │                       └──────┬──────┘
       │                              │ resume()
       │                              ▼
       │                       ┌─────────────┐
       │                       │   Running   │
       │                       └─────────────┘
       │
       ├─► on_error() ──►┌─────────────┐
       │                 │   Failed    │
       │                 └─────────────┘
       │
       ▼
┌─────────────┐
│  Completed  │
└─────────────┘
```

---

## Section 5 — Fraud Detection Agent Blueprints

### 5.1 Real-Time Transaction Scoring Agent

**Use Case:** Score each transaction for fraud risk in <100ms at authorization time

**Architecture:**

- **Tier:** Tier 3 (Dedicated Ultra-Low-Latency)
- **Model:** Llama-3-70B-Instruct (FP8 quantized)
- **Context:** 8K tokens (customer profile + recent transactions + merchant data)
- **Tools:** 4 parallel tool calls (customer profile, device fingerprint, merchant risk, velocity checks)
- **Memory:** Working memory only (no episodic persistence needed for sub-second decisions)

**Workflow:**

```yaml
agent_id: real-time-transaction-scorer
version: 1.0.0
deployment:
  sif_tier: tier3_dedicated
  latency_sla_ms: 100
  min_replicas: 5

workflow:
  entry_point: fetch_context

  nodes:
    - id: fetch_context
      type: parallel
      timeout_ms: 30
      nodes:
        - id: get_customer_profile
          tool: customer-profile-service
        - id: get_device_fingerprint
          tool: device-intelligence-service
        - id: get_merchant_risk
          tool: merchant-risk-score-service
        - id: get_velocity_checks
          tool: transaction-velocity-service

    - id: score_transaction
      type: llm_call
      depends_on: [fetch_context]
      model: llama-3-70b-instruct-fp8
      max_tokens: 100
      temperature: 0.1
      prompt: |
        You are a fraud detection expert. Score this transaction:
      
        Transaction: {{transaction}}
        Customer Profile: {{customer_profile}}
        Device: {{device_fingerprint}}
        Merchant: {{merchant_risk}}
        Velocity: {{velocity_checks}}
      
        Output JSON: {"risk_score": 0-100, "risk_level": "low|medium|high", "reasoning": "..."}

    - id: apply_guardrails
      type: validation
      schema:
        type: object
        required: [risk_score, risk_level, reasoning]
      rules:
        - risk_score must be between 0 and 100
        - risk_level must match score range

  output:
    risk_score: "{{score_transaction.risk_score}}"
    risk_level: "{{score_transaction.risk_level}}"
    reasoning: "{{score_transaction.reasoning}}"
    decision: "{{approve if risk_score < 50 else decline}}"
```

**Performance Optimization:**

- **KV Cache Pre-Warming:** Customer profile prefix cached across multiple transactions
- **CUDA Graphs:** Capture decode graph for fixed-shape batches
- **NUMA Pinning:** GPU, NIC, and NVMe all on same NUMA node
- **GPUDirect Storage:** Model weights loaded directly from NVMe to GPU
- **Tool Call Parallelization:** All 4 tool calls execute concurrently (30ms budget)

### 5.2 Fraud Case Investigation Agent

**Use Case:** Multi-hour autonomous investigation of suspicious activity patterns

**Architecture:**

- **Tier:** Tier 2 (Committed Capacity)
- **Model:** Llama-3-70B-Instruct + LoRA (fraud domain fine-tuned)
- **Context:** 32K tokens (full case file, transaction history, customer interactions)
- **Tools:** 15+ tools (internal databases, external APIs, document generation)
- **Memory:** Full three-tier memory (working + episodic + semantic)

**Workflow:**

```yaml
agent_id: fraud-case-investigator
version: 2.0.0
deployment:
  sif_tier: tier2_committed
  min_replicas: 2
280


workflow:
  entry_point: intake_case

  nodes:
    - id: intake_case
      type: llm_call
      prompt: |
        Review this fraud alert and create an investigation plan:
        Alert: {{alert_data}}
        Customer: {{customer_info}}
  
        Output: {"hypotheses": [...], "investigation_steps": [...], "priority": "high|medium|low"}

    - id: gather_evidence
      type: parallel
      nodes:
        - id: pull_transaction_history
          tool: transaction-history-service
          params:
            customer_id: "{{customer_info.id}}"
            date_range: "last_90_days"
        - id: check_account_changes
          tool: account-change-log-service
        - id: search_similar_cases
          type: rag_call
          indices: [historical-cases, fraud-patterns-kb]
          query: "{{intake_case.hypotheses}}"
        - id: verify_customer_contact
          tool: customer-contact-service
        - id: check_external_databases
          tool: lexisnexis-fraud-database

    - id: analyze_patterns
      type: llm_call
      depends_on: [gather_evidence]
      prompt: |
        Analyze all evidence and identify fraud patterns:
        Transactions: {{pull_transaction_history}}
        Account Changes: {{check_account_changes}}
        Similar Cases: {{search_similar_cases}}
  
        Output: {"patterns": [...], "confidence": 0-1, "recommended_actions": [...]}

    - id: human_review_gate
      type: human_input
      condition: "{{analyze_patterns.confidence}} < 0.8"
      reviewers: ["fraud-analyst-oncall"]
      input_schema:
        type: object
        properties:
          analyst_decision:
            type: string
            enum: [proceed, request_more_info, close_case]
          notes: string

    - id: generate_narrative
      type: llm_call
      prompt: |
        Write a comprehensive fraud investigation narrative:
        Case Summary: {{intake_case}}
        Evidence: {{gather_evidence}}
        Analysis: {{analyze_patterns}}
  
        Format: SAR-ready narrative with sections:
        1. Introduction
        2. Suspicious Activity Description
        3. Timeline of Events
        4. Evidence Summary
        5. Conclusion and Recommendations

    - id: file_sar_if_needed
      type: tool_call
      condition: "{{analyze_patterns.confidence}} > 0.9"
      tool: sar-filing-service
      params:
        narrative: "{{generate_narrative}}"
        evidence: "{{gather_evidence}}"

    - id: update_case_status
      type: tool_call
      tool: case-management-service
      params:
        case_id: "{{case_id}}"
        status: "{{'investigating' or 'filed_sar' or 'closed'}}"
        notes: "{{generate_narrative}}"
```

**Memory Configuration:**

```yaml
memory:
  working_memory:
    max_context_tokens: 32768
    eviction_policy: lru_with_summary
  
  episodic_memory:
    enabled: true
    retention_days: 365  # Long retention for case audits
    compression: true
    compression_trigger: "every_10_turns"
  
  semantic_memory:
    indices:
      - historical-cases
      - fraud-patterns-kb
      - regulatory-policies
      - sar-filing-guidelines
    retrieval_strategy: "multi_index_fusion"
    rerank: true
```

### 5.3 Customer Fraud Alert Agent

**Use Case:** Proactive customer notification and interaction for suspected fraud

**Architecture:**

- **Tier:** Tier 2 (Committed Capacity)
- **Model:** Llama-3-70B-Instruct (customer-facing tone)
- **Context:** 16K tokens (customer profile, transaction details, conversation history)
- **Tools:** Notification services, account management, verification APIs
- **Memory:** Episodic memory for conversation continuity across multiple interactions

**Workflow:**

```yaml
agent_id: customer-fraud-alert-agent
version: 1.5.0
deployment:
  sif_tier: tier2_committed

workflow:
  entry_point: initiate_contact

  nodes:
    - id: initiate_contact
      type: parallel
      nodes:
        - id: send_push_notification
          tool: mobile-push-service
          params:
            customer_id: "{{customer_id}}"
            message: "Security Alert: Please verify recent transaction"
        - id: send_sms
          tool: sms-gateway-service
        - id: create_case_record
          tool: case-management-service

    - id: wait_for_customer_response
      type: human_input
      timeout_minutes: 30
      channels: [mobile_app, sms_reply, phone_call]

    - id: verify_customer_identity
      type: llm_call
      prompt: |
        Verify customer identity through conversation:
        Customer Response: {{wait_for_customer_response}}
        Expected Profile: {{customer_profile}}
      
        Ask verification questions if needed.
        Output: {"identity_verified": bool, "confidence": 0-1, "notes": "..."}

    - id: present_transaction_details
      type: llm_call
      depends_on: [verify_customer_identity]
      condition: "{{verify_customer_identity.identity_verified}} == true"
      prompt: |
        Present the suspicious transaction to customer:
        Transaction: {{transaction_details}}
      
        Use clear, non-technical language. Ask if they recognize it.
        Output: {"customer_message": "...", "recognized": "yes|no|unsure"}

    - id: branch_on_recognition
      type: conditional
      branches:
        - if: "{{present_transaction_details.recognized}} == 'yes'"
          goto: mark_false_positive
        - if: "{{present_transaction_details.recognized}} == 'no'"
          goto: confirm_fraud
        - if: "{{present_transaction_details.recognized}} == 'unsure'"
          goto: provide_more_details

    - id: mark_false_positive
      type: tool_call
      tool: transaction-approval-service
      params:
        transaction_id: "{{transaction_id}}"
        reason: "customer_verified"
      next: update_case_and_close

    - id: confirm_fraud
      type: parallel
      nodes:
        - id: block_card
          tool: card-management-service
          params:
            action: block
            reason: suspected_fraud
        - id: initiate_chargeback
          tool: chargeback-service
        - id: issue_replacement_card
          tool: card-fulfillment-service
      next: update_case_and_escalate

    - id: provide_more_details
      type: llm_call
      prompt: |
        Provide additional context to help customer remember:
        - Merchant location
        - Transaction time
        - Amount
        - Similar past transactions
      
        Then re-ask if they recognize it.

    - id: update_case_and_close
      type: tool_call
      tool: case-management-service
      params:
        status: "closed_false_positive"
        notes: "Customer verified transaction as legitimate"

    - id: update_case_and_escalate
      type: tool_call
      tool: case-management-service
      params:
        status: "confirmed_fraud"
        notes: "Customer denied transaction, card blocked, chargeback initiated"
        next_steps: "assign_to_fraud_analyst"
```

---

## Section 6 — Capacity Planning & Cost Model

### 6.1 Capacity Sizing by Agent Tier

**Tier 1 (Pay-as-you-Go) — Experimentation & Dev/Test:**

| Metric              | Value                                          | Calculation                                                   |
| ------------------- | ---------------------------------------------- | ------------------------------------------------------------- |
| Concurrent Sessions | 500                                            | 2000 daily sessions × 15 min avg / 1440 min × peak factor 2 |
| Context Size        | 8K tokens                                      | Average for dev/test workloads                                |
| Token Throughput    | 4M tokens/hour                                 | 500 sessions × 8K tokens × 10 turns/hour                    |
| GPU Requirement     | 3 H100 GPUs                                    | 4M / (150K tokens/sec/GPU × 3600 sec × 0.7 util)            |
| Memory (HBM)        | 96 GB                                          | 500 sessions × 8K tokens × 2 bytes/KV                       |
| Memory (NVMe)       | 15 TB                                          | 2000 sessions/day × 50KB × 30 days × compression           |
| Vector DB           | 50 GB                                          | Dev/test indices only                                         |
| Monthly Cost        | $15K | 3 GPUs × $5K/month + storage + network |                                                               |

**Tier 2 (Committed Capacity) — Production Internal & Customer-Facing:**

| Metric              | Value                                                       | Calculation                                         |
| ------------------- | ----------------------------------------------------------- | --------------------------------------------------- |
| Concurrent Sessions | 2000                                                        | 20K daily sessions × 10 min avg × peak factor 1.5 |
| Context Size        | 16K tokens                                                  | Production workloads need more context              |
| Token Throughput    | 20M tokens/hour                                             | 2000 sessions × 16K × 10 turns/hour               |
| GPU Requirement     | 12 H100 GPUs                                                | 20M / (150K × 3600 × 0.75 util)                   |
| Memory (HBM)        | 512 GB                                                      | 2000 × 16K × 2 bytes                              |
| Memory (NVMe)       | 90 TB                                                       | 20K sessions/day × 50KB × 90 days                 |
| Vector DB           | 200 GB                                                      | Production indices                                  |
| Monthly Cost        | $85K | 12 GPUs × $5K + reserved capacity premium + storage |                                                     |

**Tier 3 (Dedicated) — Real-Time Fraud Detection:**

| Metric              | Value                                                    | Calculation                                          |
| ------------------- | -------------------------------------------------------- | ---------------------------------------------------- |
| Concurrent Sessions | 5000                                                     | 100K transactions/day × 100ms × peak factor 5      |
| Context Size        | 8K tokens                                                | Optimized for low latency                            |
| Token Throughput    | 50M tokens/hour                                          | 5000 × 8K × 10 turns (sub-second)                  |
| GPU Requirement     | 25 H100 GPUs                                             | 50M / (150K × 3600 × 0.85 util) + redundancy       |
| Memory (HBM)        | 800 GB                                                   | 5000 × 8K × 2 bytes                                |
| Memory (NVMe)       | 30 TB                                                    | 100K tx/day × 10KB × 30 days (minimal persistence) |
| Vector DB           | N/A                                                      | Not used for real-time scoring                       |
| Monthly Cost        | $180K | 25 dedicated GPUs × $6K + host tuning + standby |                                                      |

### 6.2 Cost Attribution Model

**Per-Agent Cost Breakdown:**

```
Total Agent Cost = GPU Compute + Memory Storage + RAG Retrieval + Tool Calls + Platform Fee

GPU Compute:
  Tier 1: $0.50 per million tokens (shared pool, pay-per-use)
  Tier 2: $2000 per GPU-month (reserved capacity) + $0.30 per million overage tokens
  Tier 3: $6000 per GPU-month (dedicated, fully-loaded cost passthrough)

Memory Storage:
  Working Memory (HBM): Included in GPU cost
  Episodic Memory (NVMe): $0.10 per GB-month
  Semantic Memory (Vector DB): $0.20 per GB-month + $0.01 per 1000 queries

RAG Retrieval:
  Embedding Generation: $0.0001 per embedding
  Vector Search: $0.01 per 1000 queries
  Re-Ranking: $0.05 per 1000 queries

Tool Calls:
  Internal Tools: $0.001 per call (cost attribution to tool owner)
  External APIs: Passthrough cost + 10% platform fee

Platform Fee:
  Tier 1: 20% of GPU compute (covers idle capacity risk)
  Tier 2: 10% of GPU compute (operations overhead)
  Tier 3: 15% of GPU compute (dedicated support)
```

**Example: Fraud Case Investigation Agent (Tier 2)**

```
Monthly Usage:
  - 5000 sessions/month
  - 20K tokens/session average
  - 100M total tokens/month
  - 50K RAG queries/month
  - 200K tool calls/month

Cost Calculation:
  GPU Compute: $2000 × 2 GPUs = $4000 (reserved)
  Token Overage: 0 (within committed throughput)
  Episodic Memory: 5000 sessions × 50KB × 30 days = 7.5 GB × $0.10 = $0.75
  Vector DB: 10 GB × $0.20 = $2 + 50K queries × $0.01/1000 = $0.50
  RAG Embeddings: 50K × $0.0001 = $5
  Re-Ranking: 50K × $0.05/1000 = $2.50
  Tool Calls: 200K × $0.001 = $200
  Platform Fee: 10% × $4000 = $400

Total Monthly Cost: $4,610.75
Cost per Session: $0.92
```

### 6.3 Scaling Triggers & Auto-Scaling Policies

**Tier 1 (Shared Pool) — KEDA-Driven Scaling:**

```yaml
scaledObject:
  minReplicaCount: 1
  maxReplicaCount: 10
  triggers:
    - type: prometheus
      metric: agent_session_queue_depth
      threshold: 10
    - type: prometheus
      metric: token_generation_rate
      threshold: 100000  # tokens/sec
  scaleInCooldown: 300  # 5 minutes
  scaleOutCooldown: 60  # 1 minute
```

**Tier 2 (Committed) — HPA + Manual Floor:**

```yaml
horizontalPodAutoscaler:
  minReplicas: 2  # Committed floor
  maxReplicas: 20
  metrics:
    - type: Pods
      pods:
        metric: kv_cache_utilization
        target:
          type: Utilization
          averageUtilization: 75
  behavior:
    scaleUp:
      stabilizationWindowSeconds: 60
    scaleDown:
      stabilizationWindowSeconds: 300
      policies:
        - type: Percent
          value: 10
          periodSeconds: 60
```

**Tier 3 (Dedicated) — Manual Scaling Only:**

- No auto-scaling (dedicated capacity reserved)
- Karpenter maintains warm standby node for failover
- Scaling requires LOB request and capacity planning review
- 48-hour lead time for new GPU node provisioning

---

## Section 7 — Observability & Monitoring

### 7.1 Metrics Hierarchy

**Level 1: User Experience Metrics (Business SLAs)**

| Metric                          | Description                                | Target       | Alert Threshold |
| ------------------------------- | ------------------------------------------ | ------------ | --------------- |
| `agent_session_success_rate`  | % sessions completing without error        | >99.5%       | <99%            |
| `agent_latency_p50_p99`       | End-to-end session latency                 | <500ms / <2s | >1s / >5s       |
| `agent_first_response_time`   | Time to first token (conversational)       | <200ms       | >500ms          |
| `agent_task_completion_rate`  | % workflows reaching successful completion | >95%         | <90%            |
| `customer_satisfaction_score` | Post-interaction CSAT (1-5)                | >4.2         | <3.8            |

**Level 2: Runtime Metrics (Platform Health)**

| Metric                             | Description               | Target  | Alert Threshold |
| ---------------------------------- | ------------------------- | ------- | --------------- |
| `agent_kv_cache_utilization`     | % HBM used for KV cache   | <80%    | >90%            |
| `agent_tool_call_error_rate`     | % tool calls failing      | <1%     | >5%             |
| `agent_checkpoint_write_latency` | Time to write checkpoint  | <50ms   | >200ms          |
| `agent_memory_spill_rate`        | KV blocks spilled to NVMe | <10/sec | >100/sec        |
| `agent_retry_rate`               | % nodes requiring retry   | <5%     | >20%            |

**Level 3: Infrastructure Metrics (Silicon Health)**

| Metric                  | Description                | Target       | Alert Threshold |
| ----------------------- | -------------------------- | ------------ | --------------- |
| `gpu_sm_utilization`  | GPU compute utilization    | 60-85%       | <40% or >95%    |
| `gpu_hbm_utilization` | GPU memory capacity        | <90%         | >95%            |
| `gpu_hbm_bandwidth`   | GPU memory bandwidth       | >1TB/s       | <500GB/s        |
| `nvlink_traffic`      | GPU-GPU interconnect usage | Varies       | Degraded links  |
| `numa_remote_access`  | Cross-NUMA memory access   | ~0% (Tier 3) | >5%             |

### 7.2 Distributed Tracing

**Trace Structure:**

```
Trace: agent-session-abc123
├─ Span: agent_initialization (5ms)
│  ├─ load_agent_definition
│  ├─ initialize_memory
│  └─ validate_capacity
│
├─ Span: node_fetch_context (35ms)
│  ├─ Span: tool_customer_profile (12ms)
│  ├─ Span: tool_device_fingerprint (15ms)
│  ├─ Span: tool_merchant_risk (10ms)
│  └─ Span: tool_velocity_checks (8ms)
│
├─ Span: node_score_transaction (45ms)
│  ├─ Span: llm_prefill (20ms)
│  ├─ Span: llm_decode_first_token (15ms)
│  └─ Span: llm_decode_remaining (10ms)
│
├─ Span: node_apply_guardrails (5ms)
│
└─ Span: checkpoint_write (10ms)

Total Latency: 100ms
Token Count: 1250 (input: 800, output: 450)
Tool Calls: 4
KV Cache Hit: true (prefix cached)
```

**Trace Context Propagation:**

```python
# Inject trace context into tool calls
def call_tool(tool_name, params, trace_context):
    headers = {
        "traceparent": trace_context.traceparent,
        "x-agent-id": trace_context.agent_id,
        "x-session-id": trace_context.session_id,
        "x-tenant-id": trace_context.tenant_id,
    }
    return tool_client.invoke(tool_name, params, headers=headers)

# Inject trace context into LLM calls
def call_llm(prompt, trace_context):
    extra_fields = {
        "trace.traceparent": trace_context.traceparent,
        "agent.id": trace_context.agent_id,
        "session.id": trace_context.session_id,
    }
    return llm_client.generate(prompt, extra_fields=extra_fields)
```

### 7.3 Dashboards

**Executive Dashboard (LOB Leadership):**

```
┌─────────────────────────────────────────────────────────────────┐
│              AGENT PLATFORM — EXECUTIVE VIEW                     │
│  Tenant: Fraud Operations │ Period: Last 30 Days                │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  📊 AGENT ADOPTION                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Active Agents│  │ Daily Active │  │ Sessions     │          │
│  │     47       │  │    Users     │  │    /Month    │          │
│  │   +12 this   │  │    2,340     │  │   125,000    │          │
│  │    month     │  │   +15% MoM   │  │   +22% MoM   │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                  │
│  💰 COST & EFFICIENCY                                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Total Cost   │  │ Cost per     │  │ Auto-Resolved│          │
│  │   $287K      │  │   Session    │  │     Rate     │          │
│  │  -8% vs plan │  │    $2.30     │  │     73%      │          │
│  │              │  │  -12% MoM    │  │  +5% MoM     │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                  │
│  🎯 SLA ATTAINMENT                                               │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ Real-Time Scoring (Tier 3):  99.97% ✓                │       │
│  │ Case Investigation (Tier 2): 99.82% ✓                │       │
│  │ Customer Alerts (Tier 2):    99.65% ✓                │       │
│  │ Dev/Test (Tier 1):           98.40% ⚠                 │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
│  ⚠️  TOP ISSUES                                                  │
│  • Tier 1 GPU contention causing 2% session timeouts            │
│  • Vector DB latency spike on 7/4 (resolved)                    │
│  • Tool rate limiting on external fraud database                │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Agent Developer Dashboard:**

```
┌─────────────────────────────────────────────────────────────────┐
│           AGENT: fraud-case-investigator-v2                      │
│  Owner: fraud-platform-team │ Tier: 2 │ Status: Production      │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  📈 PERFORMANCE (Last 24 Hours)                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Sessions     │  │ Avg Duration │  │ Success Rate │          │
│  │     342      │  │   8.5 min    │  │    99.1%     │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                  │
│  🕐 LATENCY BREAKDOWN                                            │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ LLM Calls:      450ms (p50) / 1200ms (p99)           │       │
│  │ Tool Calls:     120ms (p50) / 350ms (p99)            │       │
│  │ RAG Retrieval:   80ms (p50) / 200ms (p99)            │       │
│  │ Checkpointing:   15ms (p50) /  50ms (p99)            │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
│  💾 MEMORY UTILIZATION                                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Working Mem  │  │ Episodic Mem │  │ KV Cache     │          │
│  │   18K/32K    │  │   45 GB      │  │   Hit: 67%   │          │
│  │   tokens     │  │   (90 days)  │  │              │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                  │
│  🔧 TOOL USAGE                                                   │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ transaction-history:      342 calls (100% success)   │       │
│  │ case-management:          342 calls (100% success)   │       │
│  │ historical-cases-rag:     340 calls (99% success)    │       │
│  │ sar-filing:                28 calls (100% success)   │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
│  ⚠️  ERRORS & RETRIES                                            │
│  • 3 sessions failed at node `human_review_gate` (timeout)      │
│  • 12 tool calls retried (historical-cases-rag, rate limit)     │
│  • 1 LLM call failed (SIF transient error, auto-recovered)      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**SRE Dashboard (Platform Operations):**

```
┌─────────────────────────────────────────────────────────────────┐
│              AGENT PLATFORM — SRE VIEW                           │
│  Cluster: prod-us-east-1 │ GPU Pool: h100-fleet                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  🖥️  GPU UTILIZATION                                             │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ Tier 1 (Shared):     73% SM / 81% HBM / 42 agents    │       │
│  │ Tier 2 (Committed):  68% SM / 74% HBM / 18 agents    │       │
│  │ Tier 3 (Dedicated):  85% SM / 88% HBM /  7 agents    │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
│  📊 CAPACITY HEADROOM                                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ GPU Hours    │  │ KV Cache     │  │ Vector DB    │          │
│  │ Remaining    │  │ Headroom     │  │ Query QPS    │          │
│  │   340 hrs    │  │    23%       │  │   2.3K/5K    │          │
│  │  (Tier 1)    │  │  (Tier 2)    │  │  (Capacity)  │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                  │
│  🚨 ACTIVE ALERTS                                                │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ [WARNING] Tier 1 GPU pool at 92% utilization         │       │
│  │ [INFO] Karpenter provisioning 2 additional nodes     │       │
│  │ [WARNING] Vector DB latency p99 > 100ms (degraded)   │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
│  📈 SCALING EVENTS (Last 6 Hours)                                │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ 10:32 - Tier 1 scaled 3→5 replicas (queue depth)     │       │
│  │ 09:15 - Tier 2 scaled 2→3 replicas (KV pressure)     │       │
│  │ 08:47 - Tier 1 scaled 5→3 replicas (scale-in)        │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
│  🛠️  DEPLOYMENT STATUS                                           │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ fraud-detection-v3:  Rolling update (2/5 pods)       │       │
│  │ customer-alert-v1:   Stable (100% healthy)           │       │
│  │ case-investigator-v2: Canary (10% traffic)           │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Section 8 — Security & Compliance Deep Dive

### 8.1 PII Protection Strategy

**Data Flow with Redaction:**

```
Customer Input → PII Redaction → LLM Processing → Output Filtering → Customer Response
                      ↑                              ↑
                Presidio NER                    Llama Guard
                Custom Rules                    Policy Check
```

**Redaction Pipeline:**

```python
def redact_input_for_llm(raw_input: str, context: AgentContext) -> str:
    # Step 1: Detect PII using Presidio
    analyzer_results = analyzer.analyze(
        text=raw_input,
        entities=[
            "US_SSN", "CREDIT_CARD", "PHONE_NUMBER", "EMAIL_ADDRESS",
            "US_PASSPORT", "DATE_OF_BIRTH", "PERSON_NAME", "ADDRESS"
        ],
        language="en"
    )
  
    # Step 2: Apply redaction based on compliance class
    redactor = PresidioRedactor()
  
    if context.compliance_class == "PCI-DSS":
        # Redact all cardholder data
        redacted = redactor.redact(
            raw_input,
            analyzer_results,
            replace_with="[REDACTED_CARD_DATA]"
        )
    elif context.compliance_class == "SOC2":
        # Redact PII but keep non-sensitive identifiers
        redacted = redactor.redact(
            raw_input,
            analyzer_results,
            replace_with=lambda entity: f"[REDACTED_{entity.entity_type}]"
        )
    else:
        # Default: redact all detected PII
        redacted = redactor.redact(raw_input, analyzer_results)
  
    # Step 3: Log redaction audit event
    audit_log.redaction_event(
        session_id=context.session_id,
        entities_redacted=len(analyzer_results),
        compliance_class=context.compliance_class
    )
  
    return redacted

def filter_llm_output(raw_output: str, context: AgentContext) -> str:
    # Step 1: Check for PII leakage
    leakage_check = analyzer.analyze(
        text=raw_output,
        entities=["US_SSN", "CREDIT_CARD", "PHONE_NUMBER"],
        language="en"
    )
  
    if leakage_check:
        # LLM leaked PII - redact before returning
        audit_log.alert_pii_leak(context.session_id)
        return redactor.redact(raw_output, leakage_check)
  
    # Step 2: Content safety check (toxic, harmful, biased)
    safety_result = llama_guard.evaluate(raw_output)
    if not safety_result.safe:
        audit_log.content_safety_violation(context.session_id, safety_result)
        return "I apologize, but I cannot provide that information. Let me connect you with a human agent."
  
    # Step 3: Policy compliance check
    policy_check = policy_engine.evaluate(
        content=raw_output,
        policies=context.applicable_policies
    )
    if not policy_check.compliant:
        audit_log.policy_violation(context.session_id, policy_check)
        return generate_policy_compliant_response(policy_check)
  
    return raw_output
```

### 8.2 Audit Logging Architecture

**Immutable Audit Trail:**

```json
{
  "audit_event_id": "uuid",
  "timestamp": "2026-07-06T14:32:15.234Z",
  "event_type": "agent_tool_call",
  "tenant_id": "fraud-operations",
  "agent_id": "fraud-case-investigator-v2",
  "session_id": "session-abc123",
  "user_id": "analyst-456",
  
  "event_details": {
    "tool_name": "transaction-history-service",
    "tool_input": {
      "customer_id": "[REDACTED]",
      "date_range": "last_90_days"
    },
    "tool_output_hash": "sha256:...",
    "latency_ms": 45,
    "success": true
  },
  
  "compliance_metadata": {
    "pci_scope": true,
    "data_residency": "us-east-1",
    "retention_period_days": 2555,  # 7 years for SAR-related
    "encryption_key_id": "arn:aws:kms:..."
  },
  
  "integrity": {
    "signature": "rsa-sha256:...",
    "previous_event_hash": "sha256:...",
    "merkle_root": "sha256:..."
  }
}
```

**Audit Log Storage:**

- **Hot Storage (30 days):** Elasticsearch for fast querying and dashboards
- **Warm Storage (1 year):** S3 Standard with Athena query access
- **Cold Storage (7 years):** S3 Glacier Deep Archive for compliance retention
- **Integrity:** Merkle tree hashing for tamper-evident logging

### 8.3 Model Risk Management (SR 11-7 Compliance)

**Model Governance Framework:**

| Requirement                      | Implementation                                                |
| -------------------------------- | ------------------------------------------------------------- |
| **Model Inventory**        | Central registry of all agent models, versions, LoRA adapters |
| **Model Validation**       | Pre-deployment evaluation on fraud detection benchmarks       |
| **Performance Monitoring** | Continuous tracking of precision, recall, F1 on live traffic  |
| **Bias Testing**           | Demographic parity testing across protected classes           |
| **Explainability**         | LLM reasoning traces stored with each decision                |
| **Change Management**      | GitOps workflow with approval gates for model updates         |
| **Fallback Procedures**    | Human-in-the-loop escalation on model uncertainty             |

**Model Card Example:**

```yaml
model_id: llama-3-70b-fraud-detection-v3
model_type: LLM with LoRA fine-tuning
owner: fraud-platform-team

intended_use:
  - Real-time transaction fraud scoring
  - Fraud case investigation support
  - SAR narrative generation

limitations:
  - Not approved for adverse action decisions without human review
  - Performance degrades on transactions > $50K (rare in training data)
  - May hallucinate on obscure fraud typologies not in training set

training_data:
  - 5M historical fraud cases (2020-2025)
  - Synthetic fraud scenarios (generated by fraud experts)
  - Regulatory guidance documents (FFIEC, OCC, FinCEN)
  - Excluded: Live customer PII, card numbers, SSNs

evaluation_metrics:
  precision: 0.94
  recall: 0.89
  f1_score: 0.915
  auc_roc: 0.96
  demographic_parity_difference: 0.03  # < 0.05 threshold

approval_status:
  - approved_for: [fraud_scoring, case_investigation]
  - requires_human_review: [adverse_action, account_closure]
  - approved_by: model-risk-committee
  - approval_date: 2026-06-15
  - next_review_date: 2026-12-15
```

---

## Section 9 — Future Enhancements

### 9.1 Agentic Workload Optimizations

**Multi-Agent Collaboration:**

```yaml
scenario: complex_fraud_ring_investigation

agents:
  - id: data_collector_agent
    role: Gather all relevant data from multiple sources
    tools: [transaction-history, customer-profile, device-intelligence]
  
  - id: pattern_analyst_agent
    role: Identify fraud patterns and connections
    tools: [graph-database, ml-anomaly-detection]
  
  - id: narrative_writer_agent
    role: Generate SAR-ready investigation narrative
    tools: [regulatory-guidelines, prior-sar-examples]
  
  - id: quality_reviewer_agent
    role: Review and validate investigation quality
    tools: [checklist-validator, compliance-rules]

orchestration:
  type: hierarchical
  manager: human_fraud_analyst
  communication_pattern: blackboard
  shared_memory: case_investigation_workspace
```

**Agent Memory Enhancements:**

- **Long-Term Memory Consolidation:** Nightly LLM-based summarization of episodic memories into semantic knowledge
- **Cross-Session Learning:** Extract patterns from successful investigations to improve future agent performance
- **Federated Memory:** Share anonymized fraud patterns across LOBs while preserving customer privacy

### 9.2 Advanced RAG Capabilities

**Graph-RAG Integration:**

```
Traditional RAG: Query → Vector Search → Retrieve Chunks → LLM

Graph-RAG: Query → Entity Extraction → Graph Traversal → 
           Retrieve Subgraph + Vector Chunks → LLM

Benefits:
- Capture relationships between entities (customer ↔ merchant ↔ device)
- Multi-hop reasoning (customer used device X at merchant Y, device linked to fraud ring Z)
- Explainable connections (show the graph path, not just similar text)
```

**Retrieval Optimization:**

- **Adaptive Retrieval:** Dynamically adjust top-K based on query confidence
- **Speculative Retrieval:** Pre-fetch likely-needed documents based on workflow state
- **Caching Strategies:** Embedding cache, chunk cache, LLM response cache

### 9.3 Platform Scalability

**Multi-Region Deployment:**

```
┌─────────────────┐         ┌─────────────────┐
│  US-East-1      │         │  US-West-2      │
│  (Primary)      │◄───────►│  (Secondary)    │
│                 │  Async  │                 │
│  • Active AG    │  Replication │  • Standby AG    │
│  • Full RAG     │         │  • RAG Replica  │
│  • All Tiers    │         │  • Tier 2/3 Only│
└─────────────────┘         └─────────────────┘
       ▲                           ▲
       │                           │
       └───────────┬───────────────┘
                   │
          ┌────────▼────────┐
          │  Global Traffic │
          │  Manager (GTM)  │
          └─────────────────┘
```

**Hybrid Cloud Bursting:**

- Burst Tier 1 workloads to AWS capacity during peak demand
- Maintain compliance boundary (data never leaves Capital One VPC)
- Unified control plane and cost attribution across on-prem and cloud burst

---

## Section 10 — Implementation Roadmap

### Phase 1: Foundation (Months 1-3)

- [ ] Deploy Agent Control Plane (Studio, Registry, Governance)
- [ ] Integrate with SIF Tier 1 (shared GPU pool)
- [ ] Implement basic agent orchestration (sequential workflows)
- [ ] Build working memory (HBM) and episodic memory (NVMe)
- [ ] Deploy initial tool registry (10 core fraud tools)
- [ ] Establish PII redaction pipeline
- [ ] Launch 2 pilot agents (dev/test use cases)

### Phase 2: Production Readiness (Months 4-6)

- [ ] Integrate with SIF Tier 2 (committed capacity)
- [ ] Implement RAG pipeline (vector DB, retrieval, re-ranking)
- [ ] Add parallel execution and conditional branching
- [ ] Deploy semantic memory (knowledge base indices)
- [ ] Expand tool registry to 50+ tools
- [ ] Implement full audit logging and compliance reporting
- [ ] Launch 5 production agents (fraud scoring, case investigation, customer alerts)

### Phase 3: Advanced Capabilities (Months 7-9)

- [ ] Integrate with SIF Tier 3 (dedicated ultra-low-latency)
- [ ] Implement multi-agent collaboration patterns
- [ ] Add Graph-RAG capabilities
- [ ] Deploy advanced guardrails (Llama Guard, custom classifiers)
- [ ] Implement human-in-the-loop workflows
- [ ] Launch real-time fraud scoring agent (Tier 3, <100ms SLA)

### Phase 4: Scale & Optimize (Months 10-12)

- [ ] Multi-region deployment (US-East-1, US-West-2)
- [ ] Advanced memory consolidation and cross-session learning
- [ ] Hybrid cloud bursting capability
- [ ] 20+ production agents across fraud, customer service, compliance
- [ ] Platform-wide cost optimization (KV cache tuning, model quantization)
- [ ] External customer-facing agent launch (mobile app, web portal)

---

## Appendix A — Glossary

| Term                      | Definition                                                                                       |
| ------------------------- | ------------------------------------------------------------------------------------------------ |
| **Agent**           | Autonomous AI system that can perceive, reason, act, and learn to achieve goals                  |
| **Workflow**        | Directed acyclic graph (DAG) of nodes (LLM calls, tool calls, conditionals) defining agent logic |
| **Working Memory**  | Short-term memory in GPU HBM holding current conversation context and KV cache                   |
| **Episodic Memory** | Medium-term memory on NVMe storing full session histories and interaction logs                   |
| **Semantic Memory** | Long-term memory in vector DB containing knowledge bases and retrieval indices                   |
| **RAG**             | Retrieval-Augmented Generation: retrieve relevant documents, inject into LLM context             |
| **Tool**            | External API or service that agent can invoke to perform actions or retrieve data                |
| **Checkpoint**      | Persistent snapshot of agent state for recovery after failures                                   |
| **Tier 1/2/3**      | SIF consumption tiers: shared, committed, dedicated GPU capacity                                 |
| **KV Cache**        | Key-Value cache in transformer attention mechanism, optimized by LMCache                         |

---

*End of Sentinel Agent Fabric (SAF) Architecture Design Document*
