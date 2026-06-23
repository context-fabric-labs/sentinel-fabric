# Layer 9: Production LLM & Transformer Platform Blueprint
## "How do we run LLMs and transformer models at global production scale?"

**Prerequisite docs:**
- `0-Layer-Architecture.md` for the silicon-to-serving mental model
- `1-OS.md`, `2-Network.md`, `3-Platform.md`, `4-GPU.md` for the lower layers
- `5-Service-Integration.md` and `6-Inference.md` for data movement and inference runtimes
- `8-LLM-Ops-Governance.md` for tenant, security, and policy depth

**This doc covers:** the full operating model for serving LLMs, embedding
models, rerankers, classifiers, and other transformer models in production at
large scale. It combines deployment, infrastructure, monitoring, governance,
troubleshooting, operations, and low-level optimization.

The target mental model is:

```
I can design the platform, deploy it in cloud or airgap, prove the hardware
baseline, tune the runtime, observe every token, govern every request, respond
to incidents, and prevent the same failure from recurring.
```

---

# SECTION 0: THE PRODUCTION MENTAL MODEL

---

## 0.1 What "OpenAI / Gemini scale" really means

At large scale, an LLM platform is not "a model behind an API." It is a
multi-plane distributed system:

```
CONTROL PLANE
  model registry, fleet configuration, rollout state, capacity state,
  tenant policy, quotas, routing tables, eval gates

DATA PLANE
  gateway, tokenizer workers, safety filters, routers, inference workers,
  RAG services, embedding/reranking services, streaming responses

ACCELERATOR PLANE
  GPU nodes, RDMA/NVLink/NVSwitch fabric, CUDA/NCCL, KV cache,
  prefill/decode workers, model shards

GOVERNANCE PLANE
  identity, model access, audit logs, cost metering, safety decisions,
  data residency, incident records, change approvals
```

The critical production rule:

```
Every request must carry:
  tenant_id
  user/service identity
  model_id and model_version
  trace_id and request_id
  policy decision
  token budget
  cost center
  data classification
  routing decision
  audit record
```

If any of that context is missing, the platform should fail closed before
the request reaches the model.

## 0.2 The three service classes

Not every transformer model should be operated the same way.

```
CLASS A: AUTOREGRESSIVE LLMs
  Examples: chat, agents, summarization, code, tool use
  Bottlenecks: TTFT, TPOT, KV cache, long context, decode bandwidth
  Runtimes: vLLM, TensorRT-LLM, SGLang, llama.cpp, custom engines

CLASS B: ENCODER / RERANKER / CLASSIFIER TRANSFORMERS
  Examples: BERT, DeBERTa, cross-encoders, toxicity classifiers
  Bottlenecks: batching, padding, sequence length, launch overhead
  Runtimes: TensorRT, ONNX Runtime, Triton, PyTorch compiled path

CLASS C: EMBEDDING / RETRIEVAL MODELS
  Examples: text embedding, multimodal embedding, dense retrieval
  Bottlenecks: throughput, vector index refresh, memory bandwidth
  Runtimes: Triton, ONNX Runtime, TensorRT, custom C++/CUDA
```

The serving choice follows the model class:

```
Large stateful LLM with KV cache:
  separate model service, continuous batching, KV-aware routing

Small stateless transformer in a latency-critical hot path:
  in-process or same-host UDS, TensorRT/ONNX, preallocated buffers

High-throughput embedding model:
  batched service with queue-aware admission and index pipeline integration
```

## 0.3 The one-sentence architecture

```
Authenticated requests enter an AI gateway, pass policy and safety checks,
get token-budgeted and routed by model/tenant/session/cache locality, execute
on isolated GPU fleets with observed prefill/decode/token metrics, stream
responses, and emit audit/cost/safety/quality records for governance.
```

---

# SECTION 1: REFERENCE ARCHITECTURE

---

## 1.1 High-level system

```
Users / Apps / Internal Services
        |
        v
+-----------------------+
| Global / Regional LB  |
+-----------------------+
        |
        v
+---------------------------------------------------------------+
| AI Gateway                                                     |
| auth, mTLS, tenant context, model ACL, token budget, safety     |
+---------------------------------------------------------------+
        |
        v
+---------------------------------------------------------------+
| Request Router                                                  |
| model routing, tenant routing, priority, KV/session locality,    |
| prompt length buckets, prefill/decode placement                 |
+---------------------------------------------------------------+
        |
        +--------------------+--------------------+---------------+
        |                    |                    |
        v                    v                    v
+----------------+   +----------------+   +----------------------+
| LLM Pool        |   | Embedding Pool |   | Rerank / Classifier |
| vLLM/TRT/SGLang |   | Triton/ONNX    |   | TensorRT/ONNX       |
+----------------+   +----------------+   +----------------------+
        |                    |                    |
        v                    v                    v
+---------------------------------------------------------------+
| Data Services                                                  |
| vector indexes, BM25, feature store, prompt cache, object store |
+---------------------------------------------------------------+
        |
        v
+---------------------------------------------------------------+
| Observability and Governance                                   |
| traces, metrics, logs, audit, cost, evals, incident records      |
+---------------------------------------------------------------+
```

## 1.2 Request path for LLM generation

```
1. Gateway authenticates request and injects tenant context.
2. Gateway estimates input tokens and reserved output tokens.
3. Policy engine checks model access, quota, data class, and region.
4. Safety layer scans input for PII, secrets, prompt injection, and policy.
5. Router selects model fleet by model, tenant, prompt length, priority,
   context size, and session/KV locality.
6. Runtime queues request for prefill.
7. Prefill computes prompt KV cache.
8. Decode emits tokens through continuous batching.
9. Streamed output passes output policy and logging controls.
10. Usage, latency, cost, safety, and audit records are emitted.
```

## 1.3 SLO vocabulary

For LLMs, generic API latency is not enough.

```
TTFT
  Time to first token. User-perceived responsiveness.

TPOT
  Time per output token. User-perceived streaming speed.

E2E latency
  Full request latency, including gateway, retrieval, prefill, decode,
  safety, network, and downstream processing.

Tokens/sec/GPU
  Primary throughput efficiency metric.

Queue wait
  Time spent waiting before runtime execution.

Prefill latency
  Prompt processing time. Scales with input length and context.

Decode latency
  Token generation time. Often HBM/KV-cache bandwidth bound.

KV cache utilization
  How much of GPU memory is consumed by active sequence state.

Admission rejection rate
  Requests rejected because capacity, quota, policy, or context limits
  would be violated.
```

## 1.4 Production SLO examples

```
Interactive chat:
  availability: 99.9% or 99.99%
  TTFT p95: < 1.5 s
  TPOT p95: < 80 ms/token
  error rate: < 0.1%
  safety policy enforcement: fail-closed

Low-latency transformer classifier:
  p99: < 10-30 ms
  error rate: < 0.01%
  deployment rollback: < 5 min

Embedding batch API:
  throughput: tokens/sec or docs/sec
  p95: seconds to minutes depending on batch size
  cost per 1M tokens/docs tracked by tenant
```

---

# SECTION 2: CLOUD, AIRGAP, AND HYBRID DEPLOYMENT PATTERNS

---

## 2.1 Cloud production pattern

Cloud is optimized for elastic infrastructure and managed dependencies.

```
Core stack:
  managed Kubernetes or self-managed Kubernetes
  GPU node pools by GPU type and workload class
  cluster autoscaler or Karpenter-style node provisioning
  cloud object store for model artifacts
  managed key management and secrets
  managed load balancers
  managed logs/metrics or self-hosted Prometheus/Grafana
  private container registry
  private networking and private endpoints
```

Cloud advantages:

```
fast capacity changes
managed regional networking
managed identity primitives
easier blue/green and multi-region rollout
object storage durability
```

Cloud risks:

```
GPU quota shortages
noisy neighbor or placement variability if not using reserved capacity
egress surprises
managed service limits
region-specific GPU availability
weaker control over firmware/BIOS/RDMA details
```

Cloud design rules:

```
Use separate node pools for:
  latency-critical LLM serving
  embedding/reranking
  batch inference
  fine-tuning/training
  general CPU services

Keep model artifacts close to compute:
  region-local object storage
  node-local NVMe cache
  prefetch DaemonSet
  readiness only after model warmup

Treat autoscaling as capacity orchestration:
  HPA scales pods
  cluster autoscaler/Karpenter adds nodes
  runtime scheduler batches tokens
```

## 2.2 Airgapped / on-prem production pattern

Airgap is optimized for control, compliance, and deterministic supply chain.

```
Core stack:
  Metal3 + Cluster API for bare-metal lifecycle
  Packer or equivalent golden-image pipeline
  Harbor or internal registry
  internal model artifact repository
  Argo CD or Flux for GitOps
  Vault + External Secrets Operator
  Cilium for primary networking
  Multus + RDMA/SR-IOV for accelerator network
  NVIDIA GPU Operator
  NVIDIA Network Operator or RDMA device plugin
  Prometheus, Grafana, Loki, Tempo/Jaeger
  offline vulnerability database and package mirrors
```

Airgap non-negotiables:

```
No runtime dependency on public registries.
No model download from public endpoints during startup.
No external package resolution during deployment.
No manual node configuration as the normal path.
Every artifact has checksum, signature, provenance, and owner.
Every cluster change flows through GitOps.
```

Airgap node pool model:

```
ACTIVE POOL
  nodes joined to cluster, schedulable, running workloads

STANDBY POOL
  provisioned, powered on or quickly powerable, joined or ready to join,
  cordoned/tainted, activatable in about 1-2 minutes

COLD SPARE POOL
  racked hardware, BMC reachable, provisioned by Metal3 when needed,
  activatable in minutes to hours depending on image/install path
```

Airgap operational maturity:

```
Level 1: manual SSH and scripts
Level 2: scripted Ansible-style provisioning
Level 3: golden image plus automated kubeadm join
Level 4: Cluster API + Metal3 + GitOps + autoscaler
Level 5: self-healing MachineHealthChecks and predictive capacity
```

## 2.3 Hybrid pattern

Hybrid is useful when sensitive data or regulated workloads must stay
on-prem, while less sensitive workloads can use cloud capacity.

```
Hybrid split examples:
  PII-heavy enterprise chat: on-prem inference
  public documentation Q&A: cloud inference
  offline batch summarization: cloud burst
  regulated embedding indexes: on-prem
  non-sensitive model evaluation: cloud
```

Hybrid rules:

```
Data residency is part of routing, not an afterthought.
Gateway policy must know which tenants/models can leave the boundary.
Audit logs must record execution region and data classification.
Model versions and policy bundles must be synchronized intentionally.
Cloud failover cannot silently violate residency or compliance.
```

---

# SECTION 3: CAPACITY AND WORKLOAD DESIGN

---

## 3.1 Capacity is token-shaped, not request-shaped

LLM requests vary dramatically in cost.

```
Request A:
  input: 50 tokens
  output: 20 tokens
  total: 70 tokens

Request B:
  input: 8,000 tokens
  output: 2,000 tokens
  total: 10,000 tokens

Request B is not "one request." It can cost more than 100 small requests.
```

Capacity planning must separately model:

```
input tokens/sec
output tokens/sec
max context length
concurrent sequences
KV cache memory
prefill GPU occupancy
decode GPU memory bandwidth
tenant priority
burst factor
headroom
```

## 3.2 Basic capacity formula

```
required_gpus =
  peak_required_tokens_per_sec
  / sustained_tokens_per_sec_per_gpu
  * headroom_factor

headroom_factor:
  1.2 for stable internal workloads
  1.5 for bursty interactive workloads
  2.0+ for external multi-tenant API workloads
```

Do this separately for:

```
prefill capacity
decode capacity
embedding throughput
reranker throughput
batch inference throughput
```

## 3.3 Fleet partitioning

Do not put all traffic in one runtime pool.

```
Partition by:
  model family
  model size
  context length
  tenant class
  latency sensitivity
  data classification
  adapter/LoRA usage
  GPU type
  quantization mode
```

Example fleet split:

```
chat-7b-short-context
chat-7b-long-context
chat-70b-enterprise
embedding-high-throughput
rerank-low-latency
batch-summarization
fine-tuning-isolated
```

Why this matters:

```
Long prompts inflate TTFT for everyone if mixed with short prompts.
Large contexts consume KV cache and reduce concurrency.
Batch jobs can starve interactive requests unless isolated.
Different quantization modes have different quality/performance tradeoffs.
External tenants may need stronger isolation and audit policy.
```

## 3.4 Admission control

Admission control is the difference between graceful degradation and
a platform-wide latency collapse.

Before sending a request to the runtime, check:

```
tenant token budget
tenant request rate
model availability
input token limit
reserved output tokens
queue deadline
current KV cache pressure
current runtime queue wait
priority class
regional capacity
policy and data residency
```

Typical actions:

```
accept
route to another pool
lower priority
truncate or summarize context if policy allows
reject with 429 when quota/capacity is exceeded
reject with 400 when context is invalid
reject with 403 when model access is denied
```

## 3.5 Graceful degradation

Plan degradation before incidents.

```
If GPU capacity is constrained:
  prefer smaller model for low-priority tenants
  cap max_output_tokens
  disable expensive tools
  lower batch job priority
  route batch to delayed queue
  preserve premium / safety-critical traffic

If RAG is degraded:
  serve cached retrieval results
  use lexical fallback
  answer with lower confidence label
  disable generation if grounding is mandatory

If safety classifier is down:
  fail closed for regulated/external traffic
  use regex/static policy fallback only where allowed
```

---

# SECTION 4: MODEL SUPPLY CHAIN AND LIFECYCLE

---

## 4.1 Model artifact requirements

Every production model bundle should contain:

```
model weights
tokenizer files
model config
runtime config
generation defaults
quantization metadata
adapter metadata if LoRA/adapters are used
expected input/output schema
license and usage constraints
owner and on-call team
training/fine-tuning data lineage
evaluation report
safety evaluation report
performance benchmark
checksum
signature
SBOM or dependency manifest
rollback version
```

## 4.2 Promotion path

```
1. Train or fine-tune model.
2. Freeze model artifact.
3. Run offline quality evaluation.
4. Run safety and policy evaluation.
5. Quantize or compile runtime engine.
6. Benchmark latency, throughput, memory, and cost.
7. Package with tokenizer/config/eval metadata.
8. Sign and publish to model registry.
9. Deploy to shadow environment.
10. Canary by tenant, region, or traffic percentage.
11. Promote or roll back based on SLO and quality gates.
```

## 4.3 Airgap import bundle

For airgapped environments, import a complete bundle:

```
container images
Helm charts / Kustomize overlays
model artifacts
tokenizer artifacts
runtime engine files
NVIDIA/CUDA compatibility metadata
policy bundles
eval reports
CVE scan results
license report
checksums and signatures
release notes
rollback instructions
```

The target cluster should not need internet access to start, scale, roll
back, or recover the service.

## 4.4 Model versioning

Use immutable model versions.

```
model_id: llama-70b-instruct
model_version: 2026-06-15.fp8.trtllm.v3
runtime_version: tensorrt-llm-x.y.z
tokenizer_version: tokenizer-sha256-...
policy_bundle_version: safety-policy-2026-06-20
```

Every response should be attributable to:

```
model version
runtime version
prompt template version
policy bundle version
retrieval index version
gateway version
```

This is essential for rollback, audit, quality regressions, and incident
reconstruction.

---

# SECTION 5: HOST AND ACCELERATOR FOUNDATION

---

## 5.1 Host firmware and BIOS

For GPU/RDMA serving nodes, firmware settings can decide whether the
platform is fast or mysterious.

```
BIOS / firmware checklist:
  ACS disabled on PCIe switches for peer-to-peer paths
  IOMMU enabled in passthrough mode
  ATS enabled on capable NICs
  Above 4G decoding enabled
  PCIe max payload size 256B or higher
  PCIe ASPM / power-saving link states disabled
  CPU frequency governor set for performance
  hyperthreading decision documented per workload
  BMC firmware pinned and monitored
  GPU, NIC, NVMe firmware versions pinned
```

Why this matters:

```
ACS can break GPU/NIC peer-to-peer traffic.
IOMMU strict mode can add translation overhead.
Power-saving link states can add p99 latency spikes.
Firmware drift makes "same node type" behave differently.
```

## 5.2 OS and kernel baseline

Node images should be built, not hand-mutated.

```
Golden GPU image includes:
  supported Linux kernel
  NVIDIA driver
  CUDA compatibility libraries
  container toolkit
  OFED or rdma-core
  nvidia-peermem for GPUDirect RDMA
  kubelet/containerd binaries
  CNI prerequisites
  monitoring agents
  compliance hardening
  internal registry mirrors
```

Kernel and boot tuning examples:

```
hugepagesz=2M hugepages=<planned_count>
transparent_hugepage=never
isolcpus=<latency_cores>
nohz_full=<latency_cores>
rcu_nocbs=<latency_cores>
iommu=pt
intel_iommu=on or amd_iommu=on
```

Operational note:

```
Do not blindly copy boot parameters. Validate against CPU topology,
NUMA layout, kernel version, workload type, and cluster policy.
```

## 5.3 NUMA and topology

The platform must know which CPU cores, memory nodes, GPUs, NICs, and
NVMe devices are close to each other.

```
Good placement:
  inference worker CPU threads
  host memory
  GPU
  NIC for RDMA
  local NVMe model cache
all live on the same NUMA side when possible.

Bad placement:
  CPU on NUMA 0
  memory allocated on NUMA 1
  GPU behind NUMA 1
  NIC behind NUMA 0
Every copy or RDMA path crosses UPI/QPI.
```

Validate:

```bash
nvidia-smi topo -m
numactl --hardware
lstopo
numastat -p <pid>
```

## 5.4 Host validation checklist

Before Kubernetes is blamed, prove the node.

```bash
nvidia-smi -L
nvidia-smi topo -m
dcgmi diag -r 1
ibstat
ibv_devices
ibv_devinfo
lsmod | grep -E "mlx5_core|mlx5_ib|ib_core|rdma_cm"
lsmod | grep nvidia_peermem
numactl --hardware
iostat -x 1
```

Expected outcomes:

```
GPU visible and healthy
NIC visible and active
RDMA verbs available
nvidia-peermem loaded when GPUDirect RDMA is required
GPU/NIC topology is PIX/PXB where possible, not SYS
local NVMe performance matches baseline
no ECC storm, thermal throttle, or power cap
```

## 5.5 Local storage and model cache

Slow startup often looks like a GPU problem but is really a model artifact
path problem.

Production pattern:

```
remote registry/object store
  -> node-local NVMe model cache
  -> runtime memory load
  -> GPU transfer
  -> KV cache init
  -> CUDA graph capture / engine warmup
  -> readiness
```

Do not mark the pod ready when the container starts. Mark it ready only
after:

```
model weights loaded
tokenizer loaded
KV cache initialized
runtime engine loaded or compiled
CUDA graphs captured if used
first inference warmed
metrics endpoint healthy
```

---

# SECTION 6: KUBERNETES PLATFORM DESIGN

---

## 6.1 Node pool taxonomy

Use node pools as operational boundaries.

```
gpu-inference-h100-short
gpu-inference-h100-long-context
gpu-inference-a100-embedding
gpu-training-isolated
cpu-gateway
cpu-rag
cpu-observability
storage-index
```

Each pool has:

```
labels
taints
GPU SKU
driver/runtime version
NUMA policy
network capability
RDMA capability
local storage layout
allowed workload classes
```

## 6.2 Kubelet configuration

For latency-sensitive GPU serving:

```yaml
cpuManagerPolicy: static
topologyManagerPolicy: single-numa-node
topologyManagerScope: container
reservedSystemCPUs: "0-3"
memoryManagerPolicy: Static
```

Why:

```
CPU Manager static gives exclusive CPUs to Guaranteed pods.
Topology Manager single-numa-node prevents GPU/NIC/CPU mismatch.
Reserved CPUs keep kubelet, interrupts, and agents away from hot cores.
Memory Manager can improve hugepage and NUMA allocation determinism.
```

## 6.3 Pod spec principles

Production inference pods should usually be:

```
Guaranteed QoS
integer CPU request
request == limit for CPU and memory
explicit GPU request
explicit hugepage request if used
large /dev/shm for NCCL/runtime shared memory
IPC_LOCK when RDMA memory registration is required
node affinity for correct GPU/node pool
pod anti-affinity for replica spread
readiness gate tied to model warmup
preStop hook for draining streams
```

Minimal GPU + RDMA serving pod shape:

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: llm-worker
  labels:
    app: llm-worker
    workload-type: rdma-approved
  annotations:
    k8s.v1.cni.cncf.io/networks: ib-network
spec:
  terminationGracePeriodSeconds: 120
  nodeSelector:
    node-role/gpu-inference: "true"
  containers:
  - name: worker
    image: harbor.internal/llm-runtime:prod
    securityContext:
      capabilities:
        add: ["IPC_LOCK"]
      allowPrivilegeEscalation: false
    resources:
      requests:
        cpu: "16"
        memory: "192Gi"
        nvidia.com/gpu: "4"
        rdma/hca_shared_devices_a: "1"
        hugepages-2Mi: "8Gi"
      limits:
        cpu: "16"
        memory: "192Gi"
        nvidia.com/gpu: "4"
        rdma/hca_shared_devices_a: "1"
        hugepages-2Mi: "8Gi"
    env:
    - name: NCCL_IB_DISABLE
      value: "0"
    - name: NCCL_IB_HCA
      value: "mlx5_0,mlx5_1"
    - name: NCCL_NET_GDR_LEVEL
      value: "5"
    - name: NCCL_P2P_LEVEL
      value: "NVL"
    - name: NCCL_SOCKET_IFNAME
      value: "^docker,lo"
    volumeMounts:
    - name: dshm
      mountPath: /dev/shm
    - name: hugepages
      mountPath: /hugepages
  volumes:
  - name: dshm
    emptyDir:
      medium: Memory
      sizeLimit: "32Gi"
  - name: hugepages
    emptyDir:
      medium: HugePages-2Mi
```

## 6.4 Platform components

```
NVIDIA GPU Operator:
  driver, container toolkit, device plugin, DCGM exporter, MIG manager

NVIDIA Network Operator or RDMA plugin:
  OFED, RDMA shared device plugin, SR-IOV device plugin, Multus integration

Node Feature Discovery:
  labels GPU SKU, NIC, CPU flags, NUMA, storage, RDMA capability

Cilium:
  eBPF service routing, network policy, Hubble observability

Multus:
  secondary network for RDMA or dedicated high-performance interfaces

Argo CD / Flux:
  declarative platform and workload reconciliation

Vault / External Secrets:
  BMC credentials, registry credentials, signing keys, bootstrap tokens
```

## 6.5 Autoscaling

Cloud pattern:

```
HPA/KEDA:
  scales workload replicas based on queue, tokens/sec, latency, or custom metrics

Cluster autoscaler / Karpenter:
  adds GPU nodes when pending pods need capacity

Runtime scheduler:
  performs token-level batching inside a worker
```

Airgap/on-prem pattern:

```
HPA/KEDA:
  creates pending pods when more serving capacity is needed

Cluster Autoscaler with Cluster API provider:
  scales MachineDeployment

Cluster API:
  creates Machine objects

Metal3:
  claims BareMetalHost, powers on via Redfish/IPMI, provisions image

Argo CD:
  applies labels, taints, platform components, and workload config
```

Fast standby activation:

```bash
kubectl uncordon gpu-standby-01
kubectl taint node gpu-standby-01 standby=true:NoSchedule-
kubectl label node gpu-standby-01 node-role/gpu-inference=true --overwrite
```

## 6.6 Rollouts

LLM rollout is not the same as stateless web rollout.

Safe rollout sequence:

```
1. Load model on new pool.
2. Warm tokenizer and runtime.
3. Initialize KV cache and memory pools.
4. Capture CUDA graphs or load compiled engine.
5. Run synthetic inference.
6. Run canary traffic.
7. Compare SLO, cost, safety, and quality metrics.
8. Increase traffic gradually.
9. Keep rollback pool warm until confidence window passes.
```

Readiness means:

```
container healthy
runtime initialized
model loaded
first inference succeeded
metrics exported
policy bundle loaded
audit sink reachable or buffered
```

---

# SECTION 7: NETWORKING, RDMA, AND NCCL

---

## 7.1 Three networks

Separate the network problem into three types.

```
SERVICE NETWORK
  gateway, router, model service, RAG, feature store, observability
  protocols: HTTP, gRPC, UDS on same host

STORAGE / DATA NETWORK
  model downloads, vector index refresh, object storage, Kafka, Redis
  bottlenecks: throughput, retries, tail latency, connection pools

GPU COLLECTIVE NETWORK
  tensor parallelism, pipeline parallelism, distributed serving/training
  protocols: NCCL over NVLink/NVSwitch/InfiniBand/RoCE
```

Do not debug them as one generic "network problem."

## 7.2 Service network defaults

For Kubernetes:

```
Use Cilium eBPF datapath for lower overhead than iptables-heavy paths.
Prefer direct routing for same-datacenter clusters where possible.
Avoid overlays on latency-critical paths when the network supports routing.
Use Hubble/eBPF telemetry for flow visibility.
Use connection pooling aggressively.
Use UDS for same-node gateway-to-runtime paths when practical.
```

Service mesh caution:

```
Sidecars can add latency, CPU overhead, mTLS cost, and failure modes.
Use mesh where policy/identity justifies it.
For hot inference paths, benchmark with and without sidecars.
```

## 7.3 RDMA prerequisites

Host:

```
IB/RoCE link active
OFED or rdma-core installed
nvidia-peermem loaded for GPUDirect RDMA
GPU/NIC topology validated
PFC/ECN configured for RoCE if used
MTU consistent
firmware compatible
```

Kubernetes:

```
RDMA shared device plugin or SR-IOV device plugin
Multus secondary network
Topology Manager single-numa-node
IPC_LOCK capability
large /dev/shm
hugepages when required
container image includes RDMA libraries
```

Application:

```
NCCL_IB_DISABLE=0
NCCL_IB_HCA=<expected_hcas>
NCCL_NET_GDR_LEVEL=5 where appropriate
NCCL_P2P_LEVEL=NVL for NVLink preference
NCCL_SOCKET_IFNAME excludes docker/lo
NCCL_DEBUG=INFO during validation, WARN in normal production
```

## 7.4 NCCL validation

Run NCCL tests before blaming the model runtime.

```bash
/opt/nccl-tests/build/all_reduce_perf -b 8M -e 1G -f 2 -g 4
```

Good signs in logs:

```
NET/IB
Using network IB
mlx5_0 or expected HCA
GDR path used where expected
NVLink/NVSwitch used intra-node
```

Bad signs:

```
Using network Socket
NET/Socket on eth0
unexpected HCA
bandwidth far below fabric baseline
one rank consistently slow
```

Debug order:

```
1. Is IB/RoCE link active on host?
2. Is RDMA device visible inside pod?
3. Does pod have IPC_LOCK?
4. Is /dev/shm large enough?
5. Is nvidia-peermem loaded?
6. Is GPU/NIC topology local?
7. Are NCCL env vars correct?
8. Does nccl-tests hit expected bus bandwidth?
9. Are switch PFC/ECN/MTU settings correct?
```

---

# SECTION 8: INFERENCE RUNTIME ARCHITECTURE

---

## 8.1 Runtime choice matrix

| Runtime | Best for | Strength | Watch out for |
|---|---|---|---|
| vLLM | broad LLM serving | PagedAttention, continuous batching, fast adoption | Python control path, version behavior |
| TensorRT-LLM | max NVIDIA performance | compiled engines, FP8, low overhead | build complexity, engine compatibility |
| SGLang | prefix-heavy LLM workloads | radix/prefix cache, structured generation | operational maturity by version |
| Triton | mixed model serving | ensemble, metrics, batching | config complexity |
| ONNX Runtime | encoder/classifier models | portable optimized graph | operator support and shape tuning |
| llama.cpp | edge/small deployments | simple C/C++, GGUF | not the first choice for large GPU fleets |

## 8.2 LLM execution split

```
PREFILL
  processes input prompt
  parallel over prompt tokens
  compute-heavy for long prompts
  determines TTFT together with queue and retrieval

DECODE
  generates one token at a time
  often HBM/KV-cache bandwidth bound
  determines TPOT and streaming feel

KV CACHE
  stores attention keys/values for active sequences
  consumes GPU memory proportional to context and concurrency
  controls maximum active sessions
```

High-scale platforms often separate:

```
prefill workers
decode workers
KV transfer/cache layer
KV-aware router
session affinity
```

Disaggregation helps when long prompts and token streaming have very
different resource profiles.

## 8.3 LLM optimization ladder

Start with runtime-visible metrics, then optimize.

```
1. Continuous batching
   Keeps GPU busy as sequences enter/leave.

2. Chunked prefill
   Prevents long prompts from blocking decode traffic.

3. Prefix caching
   Reuses KV for shared system prompts and repeated context.

4. Paged attention / paged KV
   Reduces fragmentation and improves KV memory utilization.

5. KV cache quantization
   FP8/INT8 KV can increase concurrency when quality is acceptable.

6. CUDA Graphs
   Reduces kernel launch overhead for stable shapes.

7. TensorRT-LLM or compiled engines
   Improves kernel fusion, graph optimization, and NVIDIA-specific paths.

8. Weight quantization
   FP8, INT8, AWQ, GPTQ, or other method based on quality gate.

9. Speculative decoding
   Uses a smaller draft model to reduce effective decode latency.

10. Disaggregated prefill/decode
   Scales prompt processing and streaming decode independently.
```

## 8.4 Encoder / classifier / reranker optimization ladder

For BERT-style transformer models:

```
1. Export to ONNX or TensorRT where possible.
2. Use FP16/BF16/INT8 with quality validation.
3. Bucket by sequence length.
4. Use dynamic padding instead of padding everything to max length.
5. Batch within latency budget.
6. Capture CUDA graphs for stable shapes.
7. Fuse preprocessing where possible.
8. Pre-tokenize templates or common text.
9. Use multi-task heads instead of multiple full backbones.
10. Distill large models into smaller production variants.
```

Shared-GPU OOM for transformer fleets is usually about peak memory:

```
model weights
activation memory
temporary buffers
padding/sequence length
batch size
allocator fragmentation
concurrent models
```

Average GPU memory is not enough.

## 8.5 Router responsibilities

The router is part of performance, not just traffic management.

It should consider:

```
model id/version
tenant
priority
prompt length bucket
context length
session id
KV cache locality
adapter/LoRA id
GPU memory pressure
queue wait
prefill/decode saturation
regional policy
```

Bad routing causes:

```
low prefix cache hit rate
unnecessary prefill
long-context traffic hurting short-context traffic
tenant noisy neighbor effects
uneven GPU utilization
cost spikes
```

## 8.6 Runtime knobs to know

For vLLM/SGLang-like runtimes:

```
max_num_seqs
max_num_batched_tokens
max_model_len
gpu_memory_utilization
enable_prefix_caching
enable_chunked_prefill
kv_cache_dtype
enforce_eager / CUDA graph settings
tensor_parallel_size
pipeline_parallel_size
speculative decoding settings
```

Tune by measurement:

```
If TTFT high:
  check queue wait, prompt length, prefill, retrieval, batching delay.

If TPOT high:
  check decode batch, KV pressure, HBM bandwidth, kernel gaps.

If OOM/KV pressure:
  reduce max context, lower max_num_seqs, use KV quantization,
  separate long-context pool.

If GPU low:
  check CPU tokenization, router queue, request rate, network/RAG delay.
```

---

# SECTION 9: DATA, RAG, AND FEATURE INTEGRATION

---

## 9.1 RAG pipeline

Production RAG is a distributed system inside the request path.

```
request
  -> query normalization
  -> embedding generation
  -> lexical retrieval
  -> vector retrieval
  -> fusion
  -> reranking
  -> context packing
  -> prompt assembly
  -> generation
  -> citation/grounding checks
```

Measure each stage separately.

```
embedding latency
BM25 latency
vector search latency
reranker latency
context packing latency
prompt token count
retrieval cache hit rate
grounding failure rate
```

## 9.2 RAG failure modes

```
high TTFT caused by retrieval, not model
vector index stale
embedding model version mismatch
reranker batch too large
context packing overfills prompt
retrieval returns PII across tenant boundary
cache returns old policy-sensitive content
citations not aligned with answer
```

RAG governance rule:

```
Retrieval results must carry tenant, data classification, document version,
index version, ACL decision, and trace context into the prompt assembly step.
```

## 9.3 Data movement optimizations

For same-host hot paths:

```
shared memory
memory-mapped Arrow
Unix Domain Sockets
preallocated arenas
SPSC descriptor rings
zero-copy views
```

For cross-service APIs:

```
gRPC/protobuf for operational APIs
Arrow IPC for columnar batches
compressed payloads only when CPU cost is justified
connection pools
bounded queues and backpressure
```

For transformer preprocessing:

```
parallel tokenization
pre-tokenized templates
cached system prompts
avoid repeated JSON parsing
avoid per-request allocation
pin tokenizer CPU pools away from GPU runtime hot cores
```

---

# SECTION 10: OBSERVABILITY

---

## 10.1 Golden signals by layer

Use RED for services and USE for resources.

```
RED:
  Rate
  Errors
  Duration

USE:
  Utilization
  Saturation
  Errors
```

LLM-specific signals:

```
request rate
input tokens/sec
output tokens/sec
TTFT p50/p95/p99
TPOT p50/p95/p99
queue wait
prefill latency
decode latency
active sequences
waiting sequences
KV cache usage
KV evictions/preemptions
prefix cache hit rate
batch size histogram
max context rejection count
stream disconnect count
tokens per GPU
cost per tenant
```

Transformer/classifier signals:

```
batch size
sequence length histogram
padding ratio
GPU kernel latency
CPU preprocessing latency
model latency
end-to-end p99
OOM count
fallback count
```

## 10.2 Infrastructure signals

GPU:

```
SM utilization
HBM utilization
HBM bandwidth
GPU memory used/free
power draw
temperature
throttling
ECC errors
NVLink bandwidth/errors
PCIe replay/errors
MIG utilization if used
```

CPU and memory:

```
CPU utilization by core
runqueue length
context switches
CPU throttling
syscall rate
page faults
TLB misses
cache misses
NUMA remote memory
hugepage availability
memory bandwidth
```

Network:

```
service p99 latency
connection pool saturation
TCP retransmits
packet drops
NIC errors
ring buffer drops
RDMA counters
NCCL allreduce baseline
RoCE PFC/ECN counters
```

Storage:

```
model load time by stage
NVMe utilization
read/write latency
IOPS
object store latency
index load time
cache hit rate
```

Kubernetes:

```
pending pods
unschedulable reasons
node readiness
GPU device plugin health
RDMA device plugin health
image pull latency
pod restart count
OOMKilled
CPU throttling
deployment rollout status
PDB blocking drains
```

## 10.3 Trace schema

Every request trace should expose:

```
gateway.auth
gateway.policy
gateway.safety_input
gateway.token_estimate
router.select_pool
router.queue
rag.embedding
rag.retrieve
rag.rerank
rag.pack_context
runtime.queue_wait
runtime.prefill
runtime.decode_first_token
runtime.decode_stream
gateway.safety_output
audit.write
metering.write
```

Trace attributes:

```
tenant_id
model_id
model_version
runtime_version
gpu_node
gpu_id or MIG id
prompt_tokens
output_tokens
max_tokens
context_length
priority_class
request_shape_bucket
route_pool
policy_bundle_version
retrieval_index_version
```

## 10.4 Dashboards

Minimum dashboards:

```
Executive:
  availability, p95/p99, cost, token volume, safety blocks

LLM runtime:
  TTFT, TPOT, queue, prefill, decode, KV, cache hit rate

GPU fleet:
  GPU utilization, memory, bandwidth, errors, throttling

Tenant:
  usage, quota, errors, latency, model mix, cost

RAG:
  retrieval latency, index freshness, reranker latency, grounding failures

Kubernetes:
  node readiness, pending pods, rollouts, restarts, autoscaler decisions

Network/RDMA:
  retransmits, drops, NCCL baseline, RDMA counters, NIC health

Model rollout:
  canary vs baseline latency, quality, safety, cost, error deltas
```

## 10.5 Alert examples

Alert on symptoms first, then layer-specific causes.

```
User impact:
  TTFT p95 above SLO for 10 minutes
  TPOT p95 above SLO for 10 minutes
  5xx rate above threshold
  streaming disconnect rate spike

Runtime:
  queue wait high
  KV cache usage above 90%
  prefix cache hit rate drops unexpectedly
  rejected requests spike

Infrastructure:
  GPU ECC errors
  GPU throttling
  node NotReady
  RDMA device missing
  NCCL bandwidth below baseline
  pending GPU pods for more than N minutes

Governance:
  audit sink unavailable
  policy bundle stale
  safety classifier unavailable
  tenant quota store unavailable
```

---

# SECTION 11: GOVERNANCE, SECURITY, AND SAFETY

---

## 11.1 Policy enforcement points

```
Gateway:
  authentication, tenant context, model ACL, quota, input policy

Router:
  regional/data-residency routing, tenant isolation, model version routing

Runtime:
  tenant-specific cache separation, adapter authorization, output tagging

Tools / agents:
  tool allowlist, scoped credentials, approval gates, output validation

Audit:
  immutable record of request, model, policy, usage, and decision
```

## 11.2 Tenant controls

Per tenant:

```
allowed models
allowed regions
allowed data classes
tokens/minute
requests/second
monthly budget
max context length
max output tokens
tool permissions
logging/redaction policy
retention policy
SLA tier
isolation level
```

Tenant isolation levels:

```
logical isolation:
  namespaces, network policies, quotas, per-tenant cache separation

dedicated node pool:
  shared control plane, dedicated GPU nodes

dedicated cluster:
  strongest isolation, highest cost and operational overhead
```

## 11.3 Safety pipeline

Input:

```
schema validation
token length validation
PII/secrets detection
prompt injection detection
content policy classification
tool-use policy
data residency check
```

Output:

```
content policy classification
PII/secrets leakage check
grounding/citation check for RAG
tool output validation
structured output schema validation
tenant logging/redaction policy
```

Failure stance:

```
For regulated/external traffic, safety dependency failure usually fails closed.
For internal low-risk traffic, a documented degraded policy may be allowed.
The behavior must be explicit, tested, and audited.
```

## 11.4 Cost governance

Track:

```
input tokens
output tokens
cached tokens
prefill GPU seconds
decode GPU seconds
embedding calls
retrieval calls
reranker calls
tool calls
model size and GPU type
tenant and cost center
```

Controls:

```
token quotas
budget alerts
per-model access tiers
max output caps
batch workload scheduling windows
lower-cost fallback models
showback/chargeback reports
```

## 11.5 Evaluation gates

No model promotion without:

```
quality benchmark
task-specific regression suite
safety benchmark
red-team prompt suite
latency benchmark
throughput benchmark
memory benchmark
cost estimate
RAG grounding evaluation if applicable
tenant-specific acceptance where required
rollback plan
```

Canary compare:

```
baseline model vs candidate model
latency delta
cost delta
error delta
safety block delta
quality score delta
human review sample
```

---

# SECTION 12: OPERATIONS RUNBOOKS

---

## 12.1 Daily operations

Daily health review:

```
SLO burn rate
top tenants by tokens/cost/errors
TTFT and TPOT trend
GPU utilization and memory trend
KV pressure
queue wait
pending pods
node health
model rollout status
policy bundle freshness
audit pipeline health
```

## 12.2 New model deployment runbook

```
1. Verify model artifact is signed and complete.
2. Verify tokenizer/config compatibility.
3. Run offline quality and safety gates.
4. Build or select runtime engine.
5. Benchmark on target GPU SKU.
6. Publish immutable model version.
7. Deploy to staging/shadow.
8. Warm model and run synthetic traffic.
9. Enable canary for one tenant or small percentage.
10. Compare SLO, cost, quality, and safety.
11. Expand gradually.
12. Keep rollback pool and previous version warm.
13. Record final promotion decision.
```

## 12.3 GPU node maintenance runbook

```
1. Confirm spare capacity exists.
2. Cordon node.
3. Drain only after respecting PDB and runtime drain state.
4. Let active streams finish or hit graceful timeout.
5. Stop workload pods.
6. Perform maintenance.
7. Reboot if required.
8. Validate host GPU/NIC/NVMe baseline.
9. Validate Kubernetes device plugins.
10. Run runtime smoke test.
11. Uncordon node.
12. Monitor canary traffic.
```

## 12.4 Airgap scale-up runbook

Standby pool:

```
1. Capacity alert fires or Git change increases desired capacity.
2. Select standby node matching GPU SKU and network capability.
3. Uncordon and remove standby taint.
4. Confirm NFD labels, GPU plugin, RDMA plugin.
5. Schedule warm worker.
6. Load and warm model.
7. Router adds worker to pool.
8. Monitor TTFT/TPOT and GPU health.
```

Cold pool:

```
1. Increase MachineDeployment desired replicas.
2. Argo CD applies change.
3. Cluster API creates Machine.
4. Metal3 claims BareMetalHost.
5. BMC powers on server.
6. Golden image boots and cloud-init/kubeadm join runs.
7. Node becomes Ready.
8. NFD/GPU/RDMA operators finish setup.
9. Workload schedules and warms.
10. Node enters active pool.
```

## 12.5 Disaster recovery

Back up:

```
GitOps repos
model registry metadata
model artifacts
container registry
policy bundles
tenant configuration
audit logs
cost records
retrieval indexes
vector index build inputs
Kubernetes cluster state required for restore
Vault metadata and recovery process
```

Recovery priorities:

```
1. Gateway and policy enforcement
2. Critical tenant/model pools
3. Audit and metering buffering
4. RAG indexes for critical workloads
5. Batch and non-critical workloads
```

---

# SECTION 13: TROUBLESHOOTING FRAMEWORK

---

## 13.1 Master debugging flow

For any AI serving incident, first ask:

```
Is this a request-level issue, runtime-level issue, or infrastructure-level issue?
```

Then split into layers:

```
Application / Gateway
  request rate, errors, p99, queueing, tenant, model, prompt length

LLM / AI Runtime
  TTFT, TPOT, batching, KV cache, prefill/decode, model instance, token rate

GPU / Accelerator
  SM utilization, HBM capacity, HBM bandwidth, kernel gaps, copy overhead

Host System
  CPU, NUMA, hugepages, memory bandwidth, kernel scheduling, IRQs

Cluster / Infra
  Kubernetes, storage, network, RDMA/NCCL, autoscaling, rollout, node health
```

Strong incident line:

```
I do not start by assuming it is a GPU issue. I split latency into queue
wait, preprocessing, prefill, decode, GPU execution, memory copy, network,
and downstream time. Then I attribute each part to model, tenant, node,
GPU, runtime version, and input shape.
```

## 13.2 First five minutes

```
1. RED: Is the service sick?
   QPS, p95/p99, error rate, timeout rate, queue depth

2. USE: Which resource is sick?
   CPU, GPU, memory, network, disk utilization/saturation/errors

3. Attribution:
   Which model, tenant, GPU/node, input shape, batch size, runtime version?

4. Deep profile only after metrics narrow the scope:
   Nsight, perf, VTune, PyTorch profiler, ORT/Triton profiler

5. Fix:
   tune workload, runtime, placement, resource, or architecture
```

## 13.3 Symptom map

| Symptom | Likely causes | First checks | Typical fixes |
|---|---|---|---|
| High TTFT | queueing, long prompt, prefill bottleneck, cold start, RAG latency | queue wait, prompt length, retrieval spans, prefill latency | chunked prefill, prompt buckets, prefix cache, admission control, scale prefill |
| High TPOT | decode bandwidth, KV pressure, large batch, kernel gaps | decode latency, KV usage, HBM bandwidth, Nsight timeline | KV quantization, tune max_num_seqs, CUDA Graphs, paged attention |
| Low GPU utilization | CPU/tokenization bottleneck, batching gap, upstream latency | tokenizer CPU, runtime queue, batch histogram, traces | parallel tokenization, continuous batching, pinned memory, reduce serialization |
| High GPU util but poor throughput | inefficient kernels, memory stalls, launch overhead | Nsight Compute, kernel gaps, HBM bandwidth | TensorRT, fusion, FP8/INT8, shape stabilization, CUDA Graphs |
| OOM / KV pressure | context too long, too many sequences, fragmentation | KV metrics, active seqs, model len, allocator logs | reduce max context, lower max_num_seqs, KV quantization, long-context pool |
| Model load slow | remote download, slow deserialization, cold cache | startup stage logs, iostat, object store latency | local NVMe cache, preload DaemonSet, readiness after warmup |
| NCCL slow/hangs | TCP fallback, bad topology, RDMA config, /dev/shm | NCCL logs, nccl-tests, topology, RDMA devices | fix RDMA, IPC_LOCK, /dev/shm, GDR, NIC/GPU locality |
| p99 spikes | CPU scheduling, IRQs, NUMA, network drops, GC | per-node p99, perf, numastat, ethtool, traces | isolate cores, pin IRQs, NUMA bind, tune NIC, reduce GC/allocation |
| Quality regression | model change, prompt change, RAG index drift | model version, prompt version, index version, evals | rollback, freeze bad index, rerun eval, canary gate |
| Cost spike | long outputs, wrong model, cache miss, retry storm | tokens/tenant, model mix, prefix hit, error retries | quotas, output caps, routing fix, cache repair |

## 13.4 Profiler selection

```
PyTorch profiler:
  model operation attribution in custom PyTorch paths

ONNX/Triton profiler:
  encoder/classifier runtime behavior

Nsight Systems:
  timeline, kernel gaps, memcpy, CPU-GPU sync, NCCL delays

Nsight Compute:
  kernel-level compute vs memory, occupancy, HBM behavior

perf:
  CPU hotspots, syscalls, context switches, locks

VTune:
  CPU threading, memory access, NUMA, cache behavior

FIO:
  storage baseline

STREAM:
  host memory bandwidth baseline

nccl-tests:
  GPU collective fabric baseline
```

## 13.5 Troubleshooting answer pattern

Use this structure in interviews and incidents:

```
1. Clarify symptom:
   latency, throughput, OOM, error rate, cost, or quality?

2. Localize:
   model, tenant, node, GPU, input shape, deployment version?

3. Split latency:
   queue, preprocessing, runtime, GPU, copy, network, downstream?

4. Apply USE/RED:
   service health and infrastructure health.

5. Pick profiler:
   Nsight for GPU timeline/kernels, perf/VTune for CPU, FIO/STREAM/NCCL
   for baselines.

6. Fix safely:
   immediate mitigation, root-cause fix, prevention gate.

7. Validate:
   p99, throughput, cost, error rate, and quality regression.
```

---

# SECTION 14: LOW-LEVEL OPTIMIZATION MAP

---

## 14.1 From application to silicon

| Layer | Bottleneck | Optimization |
|---|---|---|
| Gateway | auth/policy overhead | local policy cache, O(1) quota checks, Rust/Go hot path |
| Tokenization | CPU bottleneck | parallel tokenizer pool, pre-tokenized templates, cache common prompts |
| RAG | retrieval latency | hybrid retrieval, cache, index locality, batch rerank |
| Runtime queue | head-of-line blocking | priority queues, prompt length buckets, admission deadlines |
| Prefill | long prompt compute | chunked prefill, prefix cache, prompt compression, prefill pool |
| Decode | HBM/KV bound | KV quantization, paged attention, continuous batching |
| Kernel launch | CPU gaps | CUDA Graphs, TensorRT, operator fusion |
| GPU compute | Tensor Core underuse | FP16/BF16/FP8, shape stabilization, fused kernels |
| GPU memory | OOM/fragmentation | max context caps, max_num_seqs tuning, KV paging |
| Host CPU | scheduling jitter | CPU Manager static, isolcpus, pinning, thread isolation |
| Host memory | TLB/NUMA | hugepages, NUMA bind, memory pools |
| Network | drops/retransmits | Cilium, pooling, NIC rings, IRQ pinning |
| RDMA/NCCL | slow collectives | GPUDirect RDMA, topology, NCCL tests, PFC/ECN |
| Storage | cold starts | local NVMe cache, preload, staged readiness |
| Kubernetes | bad placement | node pools, taints, affinity, Topology Manager |

## 14.2 The GPU bottleneck split

Always classify GPU problems as:

```
COMPUTE-BOUND
  Tensor cores/SMs saturated
  Fix: precision, TensorRT, better batching, tensor parallelism

MEMORY-BANDWIDTH-BOUND
  HBM reads/writes dominate
  Fix: quantization, KV quantization, Flash/Paged Attention, locality

CAPACITY-BOUND
  GPU memory full or fragmented
  Fix: reduce context, lower concurrency, sharding, MIG, quantization

LAUNCH/SCHEDULING-BOUND
  many small kernels or CPU launch gaps
  Fix: CUDA Graphs, fusion, compiled engine, shape stability
```

Strong line:

```
I do not ask only whether GPU utilization is high. I ask whether we are
compute-bound, HBM-bandwidth-bound, memory-capacity-bound, or launch-bound.
```

## 14.3 CPU and host optimization

Common CPU bottlenecks:

```
tokenization
JSON parsing
RAG orchestration
feature assembly
Python GIL
thread contention
CFS scheduling
IRQ interrupt storms
TLS/gRPC overhead
serialization/deserialization
logging/metrics overhead
```

Fixes:

```
CPU pinning
thread-pool isolation
move tokenization to worker pool
use Rust/C++ hot path
reduce JSON serialization
protobuf/Arrow/shared memory
disable noisy background agents
pin IRQs to housekeeping cores
isolate latency-sensitive cores
```

## 14.4 Network optimization

Service network:

```
Cilium eBPF
direct routing
connection pooling
keepalive tuning
avoid unnecessary sidecars
UDS for same-node calls
```

Storage/data network:

```
local cache
parallel artifact fetch
dedicated bandwidth for model loads
avoid rollout stampedes
```

GPU collective network:

```
NVLink/NVSwitch intra-node
InfiniBand/RoCE inter-node
GPUDirect RDMA
NCCL topology validation
PFC/ECN for RoCE
dual-rail where available
```

---

# SECTION 15: PRODUCTION READINESS CHECKLIST

---

## 15.1 Platform readiness

```
[ ] GPU node image is reproducible and signed
[ ] driver/CUDA/runtime compatibility matrix is documented
[ ] GPU Operator healthy
[ ] RDMA device plugin healthy where required
[ ] Cilium/Hubble healthy
[ ] kubelet CPU/Topology Manager configured
[ ] node pools labeled and tainted
[ ] NFD labels correct
[ ] Prometheus/Grafana dashboards live
[ ] logs/traces/audit pipelines live
[ ] alert routes tested
```

## 15.2 Model readiness

```
[ ] artifact signed and checksummed
[ ] tokenizer included
[ ] runtime config included
[ ] quality eval passed
[ ] safety eval passed
[ ] latency benchmark passed
[ ] throughput benchmark passed
[ ] memory benchmark passed
[ ] cost estimate approved
[ ] rollback version available
[ ] canary plan defined
```

## 15.3 Inference readiness

```
[ ] TTFT and TPOT dashboards live
[ ] queue wait visible
[ ] prefill/decode split visible
[ ] KV usage visible
[ ] prefix cache hit rate visible
[ ] GPU metrics visible
[ ] model warmup readiness implemented
[ ] graceful drain implemented
[ ] admission control enabled
[ ] tenant quotas enforced
```

## 15.4 Governance readiness

```
[ ] tenant identity propagated
[ ] model access control enforced
[ ] token budgets enforced
[ ] audit logs immutable or tamper-evident
[ ] data residency policy enforced
[ ] PII/secrets handling defined
[ ] prompt injection policy defined
[ ] output safety policy defined
[ ] cost attribution implemented
[ ] policy bundle version tracked
```

## 15.5 Airgap readiness

```
[ ] no public registry dependency
[ ] no public model download dependency
[ ] internal package mirrors available
[ ] internal image registry available
[ ] internal model registry available
[ ] Metal3/Cluster API tested
[ ] standby pool tested
[ ] cold spare provisioning tested
[ ] BMC credentials managed in Vault
[ ] offline CVE process defined
[ ] artifact import process tested
```

## 15.6 Incident readiness

```
[ ] TTFT runbook
[ ] TPOT runbook
[ ] OOM/KV pressure runbook
[ ] GPU node failure runbook
[ ] RDMA/NCCL runbook
[ ] model rollback runbook
[ ] RAG/index rollback runbook
[ ] audit pipeline degradation runbook
[ ] capacity exhaustion runbook
[ ] on-call owns dashboards and alerts
```

---

# SECTION 16: THE FINAL MEMORIZATION FRAMEWORK

---

Memorize this stack:

```
REQUEST
  identity, tenant, quota, policy, safety, token estimate

ROUTER
  model, version, priority, context length, cache locality, region

RUNTIME
  queue, prefill, decode, KV cache, batching, streaming

GPU
  compute, HBM bandwidth, memory capacity, launch overhead

HOST
  CPU, NUMA, hugepages, IRQs, pinned memory, local NVMe

NETWORK
  service traffic, storage traffic, RDMA/NCCL collective traffic

KUBERNETES
  placement, isolation, rollout, autoscaling, device plugins

GOVERNANCE
  access, audit, cost, safety, data residency, eval gates

OPERATIONS
  monitor, triage, mitigate, root cause, prevent recurrence
```

The strongest summary answer:

```
At production scale, I treat LLM serving as a governed GPU operating
platform, not a single model endpoint. I isolate tenants and traffic classes,
size capacity by tokens and KV cache, validate the host/RDMA/NCCL baseline,
optimize prefill and decode separately, observe TTFT/TPOT and infrastructure
counters together, enforce policy at the gateway and router, and operate
rollouts with canaries, warm pools, rollback, and incident runbooks.
```

