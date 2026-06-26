# CapitalOne Internal AI/LLM Serving Platform — Finalized Architecture

**Codename:** Sentinel Inference Fabric (SIF)
**Author / Owner:** Principal Engineer, AI Inference Platform
**Audience:** Senior Leadership, Platform Engineering, LOB Architecture Councils
**Status:** Finalized internal architecture — production-grade
**Platform analogue:** A fully self-hosted, AWS-EKS-native equivalent of AWS Bedrock (shared, provisioned, and dedicated tiers) under CapitalOne governance, compliance, and cost-attribution controls.

---

## Section 1 — Executive Summary

CapitalOne runs dozens of Lines of Business (LOBs) that all need LLM inference — fraud narrative generation, loan document reasoning, contact-center assist, internal developer copilots, marketing content, and regulated decisioning support. Today each LOB either reaches for an external API (a compliance and data-residency problem) or stands up its own GPU stack (a cost and operational-sprawl problem). Neither is acceptable for a regulated financial institution operating at our scale.

**Sentinel Inference Fabric (SIF)** is our internal, self-hosted LLM serving platform built on AWS EKS. It delivers a Bedrock-class consumption experience — register, pick a model, get an endpoint — while keeping every token, every KV cache byte, and every log inside CapitalOne's security and compliance boundary. SIF exposes three consumption tiers that map directly to how LOBs actually buy capacity:

- **Tier 1 — Pay-as-you-Go:** a shared, maximally utilized multi-tenant GPU pool billed per million tokens. Zero commitment. This is where most experimentation and bursty workloads live.
- **Tier 2 — Committed Capacity:** provisioned throughput with contractual SLAs, guaranteed token/sec, predictable latency, and reserved KV-cache headroom. Logically isolated, physically shared.
- **Tier 3 — Dedicated Ultra-Low-Latency:** dedicated GPU nodes with host-level tuning (NUMA pinning, CPU isolation, IRQ affinity, GPUDirect Storage, NVLink-validated topology). The LOB owns and pays for the hardware envelope and gets deterministic tail latency.

SIF solves four problems simultaneously:

1. **Compliance & data residency** — inference never leaves our VPC, KMS keys, and audit perimeter (PCI-DSS, SOC2).
2. **Cost efficiency** — one shared GPU substrate amortized across LOBs instead of dozens of stranded private clusters; cost is attributed back to the cost center that consumed it.
3. **Predictable performance** — a tiered SLA model so a fraud hot path is never starved by a batch summarization job.
4. **Operational leverage** — a single control plane, a single observability pane, and one set of golden runbooks across all tiers.

The platform is operated as a product: a control plane with an internal UI and APIs handles tenant registration, quota, endpoint provisioning, model lifecycle, capacity allocation, observability wiring, and billing. LOBs self-serve; the platform team owns the substrate.

---

## Section 2 — Mental Model

SIF is a layered system. A request enters at the top; capacity, caching, and silicon are organized beneath it. The control plane and the observability/cost plane wrap the entire stack.

```
                     ┌──────────────────────────────────────────────────────────┐
                     │                  CONTROL PLANE                           │
                     │  Internal UI + APIs                                      │
                     │  Tenant registration · Quota · Endpoint provisioning ·   │
                     │  Model lifecycle · Capacity allocation · Billing ·       │
                     │  Compliance tagging · Cost-center attribution            │
                     └───────────────┬──────────────────────────────────────────┘
                                     │ declarative intent (CRDs, GitOps)
   ┌─────────────────────────────────▼─────────────────────────────────────────┐
   │ L1  TENANT ROUTING  (Sentinel Router)                                      │
   │     authn/z (SPIRE) · tenant identity · tier classification · quota gate · │
   │     model+LoRA selection · region pin · PII redaction · fail-closed        │
   ├────────────────────────────────────────────────────────────────────────────┤
   │ L2  INFERENCE RUNTIME  (KServe + vLLM Router + vLLM)                        │
   │     continuous batching · prefill/decode disaggregation · LoRA mux ·        │
   │     admission control · request shaping                                     │
   ├────────────────────────────────────────────────────────────────────────────┤
   │ L3  KV CACHE LAYER  (LMCache)                                               │
   │     prefix cache · tenant-isolated KV · HBM→NVMe spill · KV-aware routing   │
   ├────────────────────────────────────────────────────────────────────────────┤
   │ L4  STORAGE & MODEL CACHE                                                   │
   │     S3 registry → FSx for Lustre → LocalModelCache → NVMe → GPUDirect       │
   ├────────────────────────────────────────────────────────────────────────────┤
   │ L5  GPU RESOURCE LAYER  (Karpenter NodePools + GPU Operator)                │
   │     Tier1 shared pool · Tier2 provisioned pool · Tier3 dedicated tuned pool │
   │     NVLink islands · MIG/time-slice · NUMA-aligned placement                │
   └────────────────────────────────────────────────────────────────────────────┘
                     ┌──────────────────────────────────────────────────────────┐
                     │         OBSERVABILITY & COST PLANE                       │
                     │  OTel traces · Prometheus · DCGM · Grafana ·             │
                     │  per-tenant TTFT/TPOT · token metering · cost events     │
                     │  single pane of glass                                    │
                     └──────────────────────────────────────────────────────────┘
```

**The one-line model:** *The control plane turns LOB intent into capacity; the Sentinel Router turns identity into a route; the runtime turns a route into tokens; LMCache turns repeated context into saved compute; the GPU layer turns tiers into silicon; and the cost plane turns every token back into a charge-back line.*

---

## Section 3 — Tenant Tier Architecture

### Tier 1 — Pay-as-you-Go (Shared Pool)

- **Use cases:** experimentation, internal copilots, low-criticality summarization, bursty/unpredictable demand, "I just want an endpoint today."
- **Isolation level:** logical only. Shared GPU pool, shared vLLM replicas, tenant separation enforced at the Sentinel Router (identity, quota) and in LMCache (prefix-cache namespacing). No hardware isolation.
- **SLA model:** best-effort with fair-share guarantees. Soft latency targets (TTFT p99 target, not contractual). Throttled by token-rate quota. Subject to preemption by higher tiers under contention.
- **GPU & node-pool strategy:** a single large multi-model shared pool on Karpenter, packed for **maximum utilization**. Continuous batching across all tenants on the same replicas. MIG or full-GPU depending on model size; small models time-slice. Aggressive bin-packing; Karpenter consolidates idle capacity.
- **KV cache strategy:** shared LMCache pool with **per-tenant prefix-cache namespaces**. Global prefix cache for common system prompts (shared, read-only, non-sensitive). KV blocks evicted under LRU; no headroom guarantee — a Tier 1 tenant may see prefill recompute under pressure.
- **Cost model:** pure **per-million-tokens** (input + output priced separately). No floor, no commitment. Cost = metered tokens × tier-1 unit rate, attributed to cost center. The platform absorbs idle-capacity risk and prices it into the token rate.

### Tier 2 — Committed Capacity (Provisioned Throughput)

- **Use cases:** production workloads needing guaranteed throughput and predictable latency — contact-center assist, document-reasoning pipelines, anything with an SLA to its own users but not requiring physical isolation.
- **Isolation level:** **logical isolation with reservation**. Multi-tenant nodes, but each Tier 2 tenant gets a reserved slice of throughput and KV headroom enforced by the scheduler and router. Noisy-neighbor protection via reserved capacity, not dedicated metal.
- **SLA model:** **contractual** — guaranteed tokens/sec, TTFT and TPOT p99 targets, reserved KV-cache headroom so prefill is never evicted out from under the tenant. Admission control rejects over-quota bursts rather than degrading the committed floor.
- **GPU & node-pool strategy:** a **provisioned pool** sized to the sum of committed throughput plus a safety factor. Karpenter maintains this pool at a fixed floor (does not consolidate below commitment). vLLM replicas pinned per committed model; GPU-headroom-aware routing guarantees each tenant's reserved batch slots.
- **KV cache strategy:** **reserved KV headroom per tenant**. LMCache partitions allocate a guaranteed block budget per Tier 2 tenant so their working set stays resident. Prefix caching enabled and tenant-isolated. KV-aware routing keeps a tenant's sessions sticky to replicas holding their warm KV.
- **Cost model:** **provisioned-capacity billing** — the tenant pays for the reserved throughput envelope (per "model unit / hour" or committed TPS) whether or not fully used, plus overage tokens above commitment. Predictable monthly cost; the tenant owns the utilization risk on their reservation.

### Tier 3 — Dedicated, Ultra-Low-Latency

- **Use cases:** latency-critical, regulated, or high-throughput-deterministic workloads — real-time fraud narrative at the transaction edge, ultra-low-tail-latency decisioning support, workloads requiring custom hardware tuning.
- **Isolation level:** **physical**. Dedicated GPU nodes, dedicated NUMA domains, CPU isolation, IRQ tuning, dedicated NVMe with GPUDirect Storage, NVLink-validated topology. No other tenant shares the node. The LOB owns the hardware envelope.
- **SLA model:** **strict, deterministic** — hard TTFT/TPOT p99 ceilings backed by host-level tuning. The node is admission-gated: it does not accept production traffic until NUMA alignment, hugepages, IRQ affinity, GPUDirect, and NVLink topology all pass validation.
- **GPU & node-pool strategy:** a **dedicated, tuned Karpenter NodePool per Tier 3 tenant** (taints/tolerations + node labels). Whole H100/H200 nodes, NVLink-meshed, NUMA-aligned GPU↔NIC↔NVMe↔CPU. No bin-packing, no consolidation. A warm standby node per dedicated pool for fast failover.
- **KV cache strategy:** **dedicated LMCache instance** on dedicated NVMe. Full HBM KV budget plus NVMe spill on the tenant's own disks via GPUDirect Storage. Prefix and prefill caching tuned for the tenant's specific prompt shapes. Zero cross-tenant KV surface by construction (no other tenant on the node).
- **Cost model:** **dedicated cost passthrough**. The LOB pays the fully-loaded cost of the reserved nodes (GPU-hours, NVMe, network) regardless of utilization, plus a platform-operations fee. This is the most expensive and most predictable tier; the LOB owns 100% of idle risk.

---

## Section 4 — End-to-End Flow

The following is the canonical lifecycle from registration to live traffic and cost attribution.

```
LOB ──► Control Plane UI/API ──► Sentinel Router ──► KServe/vLLM ──► GPU ──► Cost Plane
```

**1. LOB registers via Control Plane UI.** The LOB creates a tenant: org/sub-org identity, cost-center code, compliance class (PCI / SOC2 / internal), data-residency region, and a SPIRE workload identity is issued. The tenant record is the root of all downstream quota, routing, and billing.

**2. LOB requests a tier.** Through the UI/API they request Tier 1, 2, or 3; select model(s) (Llama-3, Mistral, Mixtral, Claude-class, etc.); attach LoRA adapters; set region pin and compliance tags; and confirm cost-center attribution.

**3. Control plane validates quota, compliance, capacity.** It checks: the LOB's quota envelope, that the requested model is approved for the LOB's compliance class, that region pinning is satisfiable, and that capacity exists (or can be provisioned) for the requested tier. Failures are rejected with actionable reasons. The validated request becomes a declarative `TenantEndpoint` CRD committed to Git.

**4. Capacity allocation per tier.**
- *Tier 1:* no new capacity — the tenant is admitted onto the existing shared pool; only quota and router config change.
- *Tier 2:* the control plane reserves throughput in the provisioned pool, expands the Karpenter floor if needed, and allocates reserved KV-cache budget in LMCache.
- *Tier 3:* the control plane provisions a **dedicated tuned NodePool** (taints, labels, host-tuning profile), triggers Karpenter to bring up dedicated nodes, and gates them behind node-admission validation.

**5. Model selection and routing.** The control plane resolves the model + LoRA set to concrete artifacts in the S3 model registry and writes the routing table entry (tenant → tier → model → endpoint → LoRA) consumed by the Sentinel Router and vLLM Router.

**6. Endpoint provisioning.** A KServe `InferenceService` is rendered (per tier template) and applied via GitOps. The endpoint is namespaced to the tenant; network policy, mTLS identity (SPIRE), and IAM role are wired.

**7. Pod & node bootstrap.** KServe schedules vLLM serving pods (with the vLLM Router and LMCache sidecars/services). For Tier 3, pods land only on the tenant's tainted dedicated nodes. GPU Operator ensures driver/DCGM readiness; pods request `nvidia.com/gpu`, hugepages, and (Tier 3) RDMA resources.

**8. Storage attachment.** The KServe **LocalModelCache** CRD ensures the model is staged: S3 → FSx for Lustre (warm shared cache) → node-local NVMe → (Tier 3) GPUDirect Storage path into GPU memory. Weights are pre-staged before readiness; nothing is downloaded from S3 in the hot path.

**9. Networking & observability wiring.** The control plane wires Prometheus scrape targets, DCGM exporter, OTel trace export, and per-tenant Grafana dashboards. Token-metering hooks and cost-event emitters are attached to the endpoint.

**10. Endpoint goes live.** Readiness gates on: weights loaded, KV cache initialized, CUDA Graphs captured, first warm inference completed, and (Tier 3) node-admission validation green. Only then does the endpoint receive traffic.

**11. Traffic flow through vLLM Router.** Client → Sentinel Router (identity, tier, quota, redaction) → vLLM Router (load balancing across replicas) → vLLM worker. The Sentinel Router fails closed on any guardrail/identity timeout.

**12. KV-cache-aware routing.** The vLLM Router consults LMCache hit metadata and routes a request to the replica already holding the warm prefix/session KV, maximizing prefix-cache reuse and minimizing prefill.

**13. Prefill/decode disaggregation.** For large models and long contexts, KServe disaggregated serving splits compute-bound **prefill workers** from memory-bound **decode workers**; KV is handed off via LMCache. This protects TPOT on decode while prefill scales independently — critical for Tier 2/3 SLAs.

**14. Tenant-aware caching with prefix isolation.** LMCache namespaces prefix and KV blocks by tenant identity. Shared, non-sensitive system prefixes may be globally cached; all tenant-specific context is isolated — no cross-tenant KV reuse is possible.

**15. Telemetry pipeline.** Every request emits an OTel trace spanning router → prefill → decode → token stream, with TTFT/TPOT, batch size, KV pressure, and model/tenant/tier labels. DCGM and node metrics join on node/GPU labels.

**16. Cost attribution events.** Token counts (input/output), provisioned-capacity-seconds (Tier 2), and dedicated-node-hours (Tier 3) are emitted as immutable cost events keyed by tenant + cost center, flowing into the billing pipeline and cost dashboards.

---

## Section 5 — Components Used

| Component | Role in SIF |
| --- | --- |
| **AWS EKS** | The Kubernetes substrate for the entire platform. Hosts control plane, routers, serving runtime, and all GPU node pools. Managed control plane, self-managed GPU data plane. |
| **KServe** | The serving abstraction. Renders `InferenceService` endpoints per tier, drives **LocalModelCache** for weight staging, and orchestrates **disaggregated (prefill/decode) serving**. Provides canary/blue-green endpoint mechanics. |
| **vLLM** | The core inference engine on every GPU worker. PagedAttention, continuous batching, LoRA multiplexing, FP8 weights + FP8 KV cache, CUDA Graphs for decode. |
| **vLLM Router** | Replica-level load balancer in front of vLLM workers. KV-cache-aware routing, session stickiness, prefill/decode dispatch. |
| **LMCache** | The KV cache tier. Prefix caching, tenant-isolated KV namespaces, HBM→NVMe KV spill, cross-replica KV sharing, KV-hit metadata feeding the router. |
| **NVIDIA GPU Operator** | Manages GPU driver, container runtime, device plugin, MIG config, and DCGM lifecycle on all GPU nodes. |
| **DCGM Exporter** | Per-GPU telemetry — SM utilization, HBM capacity/bandwidth, NVLink traffic, power, thermals — into Prometheus. |
| **NVMe-backed PVC + GPUDirect Storage (GDS)** | Node-local NVMe for model cache and KV spill; GDS provides a direct NVMe→GPU DMA path (Tier 3) bypassing the CPU bounce buffer. |
| **Karpenter NodePools** | Tier-aware node provisioning: Tier 1 shared (bin-packed, consolidating), Tier 2 provisioned (fixed floor), Tier 3 dedicated tuned pools (tainted, no consolidation) plus warm standby. |
| **KEDA** | Event-driven scaling for Tier 1/2 replica counts off runtime signals (queue depth, KV pressure, token rate) where HPA-on-GPU-util is insufficient. Used only where request-driven scaling beats node-driven scaling. |
| **AWS FSx for Lustre** | High-throughput shared warm cache between S3 and node-local NVMe. Parallel filesystem for fast multi-node model staging and large checkpoint reads. |
| **AWS S3** | The authoritative model & adapter registry — versioned, KMS-encrypted base weights, quantized artifacts, and LoRA adapters. Source of truth, never the hot path. |
| **Prometheus + Grafana + OpenTelemetry** | Metrics store, dashboards, and distributed tracing. The backbone of the single pane of glass. |
| **Sentinel / Tenant-aware Router** | The platform's front door. Tenant identity, tier classification, quota enforcement, model/LoRA selection, PII redaction at ingest, fail-closed guardrails. |
| **IAM, KMS, SPIRE** | Identity and crypto. IAM for AWS resource scoping, KMS for at-rest encryption of weights/KV-spill/logs, SPIRE for per-workload mTLS identity across the mesh. |

---

## Section 6 — Host / NUMA / Kernel Tuning (Tier 3)

Tier 3 nodes are tuned and **admission-gated**: a node does not join the tenant's serving pool until every check below passes. This is what converts "dedicated hardware" into "deterministic latency."

### CPU isolation

```bash
# GRUB on the Tier 3 node image
GRUB_CMDLINE_LINUX="isolcpus=8-47,56-95 nohz_full=8-47,56-95 rcu_nocbs=8-47,56-95 \
   intel_iommu=on iommu=pt transparent_hugepage=never \
   hugepagesz=2M hugepages=8192 default_hugepagesz=2M"
```

- **Why:** isolates inference worker cores from the kernel scheduler, the timer tick, and RCU callbacks. Eliminates CFS preemption jitter that shows up as TPOT tail spikes. Housekeeping cores (0–7) absorb OS, IRQs, and telemetry.

### IRQ affinity

```bash
systemctl disable --now irqbalance
# Pin NIC/NVMe IRQs to housekeeping cores only
for irq in $(grep -E 'mlx5|nvme' /proc/interrupts | awk '{print $1}' | tr -d :); do
  echo 0-7 > /proc/irq/$irq/smp_affinity_list
done
```

- **Why:** keeps device interrupts off the isolated inference cores. An IRQ storm landing on a hot core is a classic cause of p99 spikes that look like "GPU problems" but are host scheduling problems.

### Hugepages

```bash
# Per-NUMA-node reservation (not global)
echo 4096 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 4096 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
```

- **Why:** reduces TLB pressure for large KV pools and pinned DMA buffers, and removes THP compaction stalls. Reserved per-node so pages are physically local.

### NUMA pinning (the core of Tier 3)

```bash
# Discover ground truth
numactl --hardware
nvidia-smi topo -m
cat /sys/class/infiniband/mlx5_0/device/numa_node
cat /sys/block/nvme0n1/device/device/numa_node
```

Goal: **inference threads, host memory, GPU, RDMA NIC, and NVMe model cache all on the same NUMA node.** Enforced via kubelet:

```yaml
# KubeletConfiguration on Tier 3 nodes
cpuManagerPolicy: static
memoryManagerPolicy: Static
topologyManagerPolicy: single-numa-node   # all-or-nothing, fail-closed
topologyManagerScope: container
reservedSystemCPUs: "0-7"
```

- **Why:** cross-NUMA access (PCIe→UPI→PCIe) adds tens of ns per access and degrades GPUDirect bandwidth 5–8×. `single-numa-node` makes kubelet *reject* a misaligned pod rather than silently split it.

### GPUDirect RDMA

```bash
modprobe nvidia-peermem && lsmod | grep nvidia_peermem
nvidia-smi topo -m      # GPU↔NIC must be PIX or PXB, never SYS
```

- **Why:** direct GPU→NIC DMA for any cross-node tensor/KV movement, bypassing the CPU bounce buffer.

### GPUDirect Storage (GDS)

```bash
# Validate cuFile / GDS path on the dedicated NVMe
gdscheck -p
/usr/local/cuda/gds/tools/gdsio -f /mnt/nvme0/model.safetensors -d 0 -w 8 -s 1G -x 0
```

- **Why:** direct NVMe→GPU DMA for weight load and KV spill, removing CPU staging from cold-start and spill paths — the difference between sub-30s and multi-minute warm-up on large models.

### NVLink topology validation

```bash
nvidia-smi topo -m            # intra-node GPU↔GPU must show NV# links
nvidia-smi nvlink --status    # all links up at full rate
```

- **Why:** tensor-parallel AllReduce must traverse NVLink, not PCIe. A node with a degraded NVLink link silently halves TP scaling — caught here, not in production.

### Node admission gating

A node-admission DaemonSet runs the full check suite (isolcpus active, hugepages reserved per-node, IRQs off isolated cores, GPU↔NIC = PIX/PXB, GDS green, NVLink full-rate, NUMA alignment). It applies a `tier3-validated=true` label only on full pass. KServe pods tolerate the Tier 3 taint **and** require `tier3-validated=true`, so traffic never lands on an unvalidated node.

```bash
# Verification bundle (run by the admission DaemonSet)
cat /sys/devices/system/cpu/isolated
grep HugePages_Total /proc/meminfo
nvidia-smi topo -m | grep -E 'PIX|PXB'
nvidia-smi nvlink --status | grep -c 'inactive' # must be 0
gdscheck -p | grep -i 'supported'
numastat -p $(pgrep -f vllm) # ~0 remote pages
```

---

## Section 7 — Storage Architecture

The storage path is a tiered cache hierarchy from cold authoritative store to GPU memory, designed so model weights are **always local before readiness**.

```
S3 model registry  (authoritative, KMS-encrypted, versioned)
        │  pull on provision (never in hot path)
        ▼
FSx for Lustre     (warm shared cache, parallel FS, multi-node fan-out)
        │  stage to node
        ▼
LocalModelCache    (KServe CRD: ensures node-local copy exists)
        │
        ▼
Node-local NVMe    (per-node model cache + KV spill)
        │  Tier 3: GPUDirect Storage (NVMe → GPU DMA)
        ▼
GPU HBM            (resident weights + KV cache)
```

- **S3 model registry:** the source of truth. Base weights, quantized (FP8/AWQ) artifacts, and LoRA adapters, all versioned and KMS-encrypted. Pulled only at provisioning/staging time.
- **FSx for Lustre cache layer:** a high-throughput parallel filesystem warmed from S3. When many nodes need the same large model (Tier 1/2 scale-out), Lustre fans it out far faster than each node pulling from S3 independently.
- **LocalModelCache CRD:** KServe's mechanism that guarantees the model is present on the node's local disk before the serving pod is marked ready. It is the readiness barrier that prevents cold-start downloads.
- **NVMe binding:** each GPU node has local NVMe for the model cache and for LMCache KV spill. On Tier 3, the NVMe is NUMA-aligned to the GPU.
- **GPUDirect Storage:** Tier 3 only — direct NVMe→GPU DMA for weight load and KV spill, removing CPU staging.
- **Multi-tenant model staging:** shared base models are staged once per node and reused across Tier 1/2 tenants; per-tenant LoRA adapters are layered on top without re-staging the base. Tier 3 stages exclusively on the tenant's dedicated nodes.
- **Cold-start strategy:** Karpenter keeps a warm standby node per Tier 2/3 pool with the model already staged. New replicas prefer warm nodes; the readiness gate (weights loaded + KV init + CUDA Graphs captured + warm inference) ensures no half-ready endpoint takes traffic.

---

## Section 8 — Observability Architecture

SIF observability is layered to answer "is the user happy → is the runtime healthy → is the silicon healthy → is the tenant within budget" in one continuous drill-down.

**1. User-experience metrics** — TTFT, TPOT/ITL, end-to-end latency, throughput (tokens/sec, RPS), error rate, timeout rate. Labeled by tenant, tier, model, region. These are the SLA metrics.

**2. Runtime metrics** — KV-cache pressure/utilization, active batch size, queue depth, admission rejects, prefill vs decode time split, preemption events. From vLLM.

**3. Inference-layer metrics** — vLLM Router routing decisions, replica load distribution, KV-hit ratio and prefix-cache hit rate from LMCache, prefill/decode dispatch balance.

**4. GPU & node metrics** — DCGM SM utilization, HBM capacity + bandwidth, NVLink traffic, power, thermals; node CPU/NUMA/hugepage/IRQ telemetry. Joinable to runtime metrics on node/GPU labels.

**5. Kubernetes metrics** — Karpenter provisioning decisions and node lifecycle, KServe revision/rollout state, pod readiness, HPA/KEDA scaling actions, pending pods (capacity signal).

**6. Tenant-level metrics** — per-tier, per-tenant dashboards: consumed tokens, SLA attainment, KV headroom usage (Tier 2), node utilization (Tier 3), and live cost.

```
┌─────────────────────── SINGLE PANE OF GLASS (Grafana) ───────────────────────┐
│ Tenant ▸ Tier ▸ Model ▸ Region selector  (one set of labels across all data)  │
│                                                                               │
│  UX:      TTFT p99 │ TPOT p99 │ errors │ throughput                           │
│  Runtime: KV pressure │ batch │ queue depth │ preemptions                     │
│  Router:  KV-hit% │ prefix-hit% │ replica balance                            │
│  GPU:     SM util │ HBM │ NVLink │ power      (DCGM)                          │
│  K8s:     Karpenter nodes │ KServe revisions │ pending pods                   │
│  Cost:    live $ │ tokens │ SLA attainment │ budget burn                      │
└───────────────────────────────────────────────────────────────────────────────┘
```

A single OTel trace stitches router → prefill → decode → token stream, so any latency regression drills from a tenant SLA breach straight to a memory-bound decode kernel or a cross-NUMA stall — without switching tools. Every panel shares the same tenant/tier/model/region label set, which is what makes it one pane rather than six dashboards.

---

## Section 9 — Cost Attribution Model

Cost responsibility is explicit and tier-shaped. Every unit of consumption maps to a cost center.

- **Per-tenant attribution:** every cost event carries `tenant_id` + `cost_center` + `tier` + `model` + `region`. There is no unattributed GPU spend on the platform.
- **Tier-level cost responsibility:**
  - *Tier 1* — tenants pay **per million tokens** (input/output priced separately). The platform owns idle-capacity risk and prices it into the unit rate.
  - *Tier 2* — tenants pay for **provisioned throughput** (committed model-units/hour or TPS), plus overage tokens. The tenant owns utilization risk on their reservation.
  - *Tier 3* — **dedicated cost passthrough**: fully-loaded dedicated-node-hours (GPU, NVMe, network) + platform-ops fee. The tenant owns 100% of idle risk.
- **Tier 3 dedicated cost passthrough:** dedicated nodes are billed continuously to the owning LOB from provision to deprovision regardless of utilization — the price of guaranteed isolation and deterministic latency.
- **Token-level billing pipeline:** vLLM emits per-request input/output token counts → metering collector → immutable cost events → billing store → charge-back. Token events are the billing system of record for Tier 1 and Tier 2 overage.
- **Idle capacity rules:** Tier 1 idle is platform-absorbed (it's shared). Tier 2 reserved-but-unused throughput is billed to the reserving tenant (that's the point of a reservation). Tier 3 idle nodes are billed in full to the owning LOB. Karpenter consolidates only Tier 1 idle capacity.
- **Cost guardrails:** per-tenant budget thresholds with soft alerts and hard caps. Hitting a hard cap throttles Tier 1 token rate or blocks new Tier 2/3 provisioning. Anomaly detection flags sudden token-burn spikes per cost center.
- **Cost dashboards:** live spend, token burn, budget burn-down, cost-per-1M-tokens by model, and Tier 3 node-hour utilization — all on the single pane, filterable by cost center for finance review.

---

## Section 10 — Multi-Tenant Security

- **Tenant-aware caching:** all cache state (prefix cache, KV blocks) is keyed by tenant identity in LMCache. There is no global mutable cache that mixes tenant data.
- **Prefix cache isolation:** only explicitly-marked, non-sensitive shared system prefixes are globally cacheable. All tenant-supplied context lives in a tenant-namespaced prefix cache; a cache lookup for tenant A can never return tenant B's blocks.
- **KV cache isolation (the cross-tenant story):** KV blocks are allocated within per-tenant namespaces in HBM and on NVMe spill. Block tables never span tenants; eviction and reuse stay within a tenant's namespace. On Tier 3 there is no cross-tenant surface at all because the node is single-tenant. This closes the KV side-channel: one tenant's generated context can never be reconstructed from another tenant's cache.
- **IAM + KMS + SPIRE:** IAM scopes each endpoint's access to only its own S3 model paths and FSx mounts. KMS encrypts weights at rest, KV spill on NVMe, and all logs. SPIRE issues per-workload SVIDs so every hop (router → vLLM Router → vLLM → LMCache) is mutually authenticated mTLS — no implicit trust inside the mesh.
- **Compliance posture (PCI, SOC2):** inference and all data stay inside the CapitalOne VPC and audit perimeter. Model access is gated by the tenant's compliance class (a PCI-scoped LOB can only use PCI-approved models). PII is redacted at the Sentinel Router ingress before tokenization. Network policy default-denies cross-tenant traffic.
- **Audit logging:** every control-plane action (registration, quota change, provisioning), every routing decision, and every cost event is logged immutably with tenant/identity attribution for SOC2/PCI audit and forensics. Guardrails fail closed — a guardrail or identity-service timeout blocks generation rather than allowing unverified output.

---

## Section 11 — Operational Excellence

- **Deployment lifecycle:** everything is declarative and GitOps-driven. A `TenantEndpoint` CRD commit flows through CI (policy checks, eval gates) to Argo CD, which renders KServe `InferenceService` revisions. No imperative kubectl in production.
- **Canary rollouts:** new model or runtime versions take 5–10% of a tenant's traffic via KServe canary. Promotion gates on SLA metrics (TTFT/TPOT) and quality eval; regression auto-rolls-back.
- **Blue/green endpoint swap:** model upgrades load into a green revision; traffic switches atomically at the router once the green endpoint passes readiness + warm-up. The blue revision stays drainable for instant rollback.
- **Pod readiness & warm-up:** readiness gates on weights loaded + KV cache initialized + CUDA Graphs captured + a real warm inference (and Tier 3 node-admission green). A container that is merely "started" never receives traffic.
- **Karpenter standby-pool strategy:** each Tier 2/3 pool keeps a warm standby node with the model pre-staged, so scale-up or failover is replica-activation (seconds) rather than cold node provisioning (minutes). Tier 1 relies on consolidation + fast bin-packing.
- **Rolling updates:** node-image and driver updates roll per-pool behind PodDisruptionBudgets that never drop a tier below its committed replica floor. Tier 3 updates drain to standby first.
- **Failure isolation:** tiers are isolated by NodePool and taints — a Tier 1 incident cannot consume Tier 2/3 capacity. Per-tenant network policy and quota prevent a single tenant from cascading. Disaggregated prefill/decode means a prefill stall doesn't freeze decode for other tenants.
- **Cluster-wide circuit breakers:** the Sentinel Router enforces bounded queues and circuit breakers per model/tenant — on sustained KV pressure or queue overflow it sheds load (reject early) for the offending tenant rather than degrading the whole fleet, and fails closed on guardrail timeouts.

---

## Section 12 — Future Improvements

- **CXL memory pooling:** introduce CXL-attached memory as a pooled, disaggregated DRAM tier to expand KV-cache capacity beyond per-node HBM+DRAM limits, raising achievable concurrency per GPU without more silicon.
- **3-tier KV cache (HBM → CXL → NVMe):** extend LMCache into an explicit three-level KV hierarchy — hot blocks in HBM, warm in CXL, cold spilled to NVMe via GDS — with the router promoting/demoting blocks based on reuse, dramatically increasing effective context capacity and prefix-cache hit rates.
- **KV-aware routing improvements:** evolve the vLLM Router toward predictive, cost-aware KV placement — anticipating session continuation and pre-warming KV on the optimal replica, and balancing KV-hit gains against load-balancing fairness.
- **Multi-LoRA orchestration:** dynamic, high-density LoRA multiplexing — hundreds of adapters hot-swapped per base model with per-tenant adapter routing, so Tier 1/2 tenants share base-model GPU memory while keeping isolated fine-tunes.
- **Agentic workload optimization:** first-class support for multi-turn, multi-tool agent loops — RadixAttention-style shared-prefix caching for agent system prompts, tool-call disaggregation, and state-aware sticky routing across cyclic agent graphs.
- **Hybrid cloud bursting:** burst Tier 1 overflow from the EKS substrate into additional capacity under the same control plane, router, and cost model — preserving compliance attribution and the single pane of glass while absorbing demand spikes beyond the committed fleet.

---

*End of finalized architecture — Sentinel Inference Fabric (SIF).*
