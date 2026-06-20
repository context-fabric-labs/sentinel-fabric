
# Kubernetes Deployment Topologies — Deep Dive
## For On-Prem HPC + Enterprise + Airgap Scenarios

> **Why topology matters:** Topology decisions are made ONCE and are hard to
> change later. Wrong topology = years of operational pain. Right topology =
> the platform scales gracefully and recovers cleanly from failures.
>
> **The core principle:** Topology should match your FAILURE DOMAINS,
> COMPLIANCE BOUNDARIES, and OPERATIONAL TEAMS. Not your org chart.

---

# PART 1: THE TOPOLOGY DECISION FRAMEWORK

## 1.1 The Five Driving Questions

Before choosing ANY topology, answer these:

```text
1. What is your BLAST RADIUS tolerance?
   "If one cluster dies, how much can be down?"
   → Drives: cluster count, isolation level

2. What are your FAILURE DOMAINS?
   "What fails together?" (rack, AZ, DC, region)
   → Drives: control plane placement, replica spread

3. What COMPLIANCE BOUNDARIES exist?
   "What must NEVER share infrastructure?"
   (e.g., PCI vs non-PCI, prod vs dev, tenant A vs tenant B)
   → Drives: cluster boundaries, network segmentation

4. What is your TEAM STRUCTURE?
   "Who operates what?"
   (Platform team vs app teams, SRE vs dev)
   → Drives: hub-spoke vs flat, RBAC model

5. What WORKLOAD MIX do you have?
   "Mostly stateless web? GPU training? Stateful databases?"
   → Drives: node pools, storage choices, scheduler tuning
```

---

## 1.2 The Topology Spectrum

```text
SIMPLE ←────────────────────────────────────────────────→ COMPLEX

Single        Multi-AZ        Hub-Spoke       Federation      Mesh
Cluster       Single          (1 mgmt +       (Many peer      (Many peer
              Cluster         N workload)     clusters)       + cross-
                                                              cluster
                                                              service)

Pros:         Pros:           Pros:           Pros:           Pros:
- Simple      - HA            - Centralized   - True          - Full
- Cheap         within DC       mgmt            isolation       autonomy
- One         - Zone          - Tenant        - Geographic    - Global
  thing to     failure         isolation       distribution    services
  manage       tolerance      - Standardized  - Compliance

Cons:         Cons:           Cons:           Cons:           Cons:
- Single      - Cannot        - Hub failure   - Operational   - Highest
  failure       survive DC      affects all     complexity     complexity
  domain       failure         spokes        - Drift          - Cross-
- Limited     - Capacity      - Cross-spoke    between        cluster
  scale        cap is one      coordination    clusters       network
                DC              hard          - Higher        complexity
                                              cost
```

---

# PART 2: SINGLE-CLUSTER TOPOLOGIES

## 2.1 Stacked Control Plane (Most Common)

### Architecture
```text
                    ┌─────────────────────┐
                    │  Load Balancer       │
                    │  (HAProxy/F5/MetalLB)│
                    └──────────┬──────────┘
                               │ API server VIP
              ┌────────────────┼────────────────┐
              │                │                │
        ┌─────▼─────┐    ┌─────▼─────┐    ┌─────▼─────┐
        │ Master 1  │    │ Master 2  │    │ Master 3  │
        │           │    │           │    │           │
        │ apiserver │    │ apiserver │    │ apiserver │
        │ scheduler │    │ scheduler │    │ scheduler │
        │ controller│    │ controller│    │ controller│
        │ etcd ◄────┼────┼─► etcd ◄──┼────┼─► etcd    │
        └───────────┘    └───────────┘    └───────────┘
              │                │                │
              └────────┬───────┴────────┬───────┘
                       │                │
              ┌────────▼────────────────▼────────┐
              │       Worker Node Pool            │
              │  worker-1, worker-2, ...worker-N  │
              └───────────────────────────────────┘
```

### When to use
- < 500 nodes
- Single datacenter or AZ
- Simplest production topology
- Tight budget (3 nodes can serve all control plane roles)

### Gotchas
- **etcd on same nodes as apiserver** — if a master goes down, you lose 1/3 etcd capacity
- **Network partition between masters** = split brain
- **etcd disk I/O competes with apiserver workload** on the same node

---

## 2.2 External etcd (Recommended for Large Clusters)

### Architecture
```text
              ┌─────────────────────┐
              │  Load Balancer       │
              └──────────┬──────────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
  ┌─────▼─────┐    ┌─────▼─────┐    ┌─────▼─────┐
  │ Master 1  │    │ Master 2  │    │ Master 3  │
  │ apiserver │    │ apiserver │    │ apiserver │
  │ scheduler │    │ scheduler │    │ scheduler │
  │ controller│    │ controller│    │ controller│
  └─────┬─────┘    └─────┬─────┘    └─────┬─────┘
        │                │                │
        └────────┬───────┴────────┬───────┘
                 │                │
        ┌────────▼────────────────▼────────┐
        │     External etcd Cluster        │
        │  ┌──────┐  ┌──────┐  ┌──────┐    │
        │  │etcd 1│  │etcd 2│  │etcd 3│    │
        │  │ NVMe │  │ NVMe │  │ NVMe │    │
        │  └──────┘  └──────┘  └──────┘    │
        │  Dedicated nodes, low-latency net │
        └──────────────────────────────────┘
```

### When to use
- > 500 nodes (etcd becomes the bottleneck)
- Compliance requires etcd isolation
- You can afford 3 more dedicated nodes
- You want to scale apiserver independently of etcd

### Benefits
- etcd can have dedicated NVMe + low-latency network
- Control plane node failure doesn't impact etcd
- Can scale apiserver replicas without etcd impact
- etcd backups don't interrupt API server

### Cost
- 3 extra dedicated nodes
- More complex backup/restore
- More operational surface area

---

## 2.3 Multi-Zone Single Cluster (Most Common for Production)

### Architecture
```text
       Datacenter / Region
       ┌──────────────────────────────────────────────────────┐
       │                                                       │
       │  Zone A (Rack/AZ)    Zone B (Rack/AZ)    Zone C       │
       │  ┌──────────┐        ┌──────────┐        ┌──────────┐ │
       │  │Master 1  │        │Master 2  │        │Master 3  │ │
       │  │etcd 1    │◄──────►│etcd 2    │◄──────►│etcd 3    │ │
       │  └──────────┘        └──────────┘        └──────────┘ │
       │                                                       │
       │  ┌──────────┐        ┌──────────┐        ┌──────────┐ │
       │  │workers   │        │workers   │        │workers   │ │
       │  └──────────┘        └──────────┘        └──────────┘ │
       │                                                       │
       └──────────────────────────────────────────────────────┘
```

### Key requirements
- **Inter-zone latency < 10ms** for etcd (ideally < 5ms)
- **Network bandwidth sufficient** for cross-zone replication
- **Storage zone-aware** — PVs must respect zone affinity
- **CNI supports topology** — Cilium does this well

### Pod placement
```yaml
# Topology spread constraints
topologySpreadConstraints:
- maxSkew: 1
  topologyKey: topology.kubernetes.io/zone
  whenUnsatisfiable: DoNotSchedule
  labelSelector:
    matchLabels:
      app: fraud-scoring
```

### When to use
- Single DC with multiple AZs (standard cloud-equivalent setup)
- On-prem with multiple racks and reliable cross-rack networking
- Need to survive AZ/rack failure without DC failover
- Most common for enterprise production

### Failure scenarios
- **One zone down**: cluster continues (2/3 etcd quorum), pods rescheduled
- **Two zones down**: etcd loses quorum, cluster read-only or unavailable
- **Inter-zone network split**: split brain — partial cluster on each side

---

# PART 3: MULTI-CLUSTER TOPOLOGIES

## 3.1 Hub-and-Spoke (Recommended for Enterprise)

### Architecture
```text
         ┌─────────────────────────────────┐
         │     HUB CLUSTER (mgmt plane)    │
         │  ┌──────────────────────────┐   │
         │  │ Argo CD                  │   │  ← GitOps for all spokes
         │  │ Harbor (registry)        │   │  ← Image source
         │  │ Vault                    │   │  ← Secret source
         │  │ Prometheus/Thanos        │   │  ← Metrics aggregator
         │  │ Cluster API              │   │  ← Cluster lifecycle
         │  │ External Secrets         │   │
         │  │ Identity provider (Keyc) │   │
         │  └──────────────────────────┘   │
         └────────┬────────────────────────┘
                  │
        ┌─────────┼────────────┬─────────────┐
        │         │            │             │
   ┌────▼────┐ ┌──▼────┐  ┌────▼─────┐  ┌────▼─────┐
   │ Spoke   │ │ Spoke │  │ Spoke    │  │ Spoke    │
   │ Prod-A  │ │ Prod-B│  │ Dev      │  │ GPU/HPC  │
   │         │ │       │  │          │  │          │
   │ Workload│ │Workload│ │ Workload │  │ Inference│
   │ tier    │ │ tier   │ │ tier     │  │ tier     │
   └─────────┘ └────────┘ └──────────┘  └──────────┘
   Region 1    Region 2   Lab            Bare metal
```

### Hub cluster runs:
- **GitOps engine** (Argo CD ApplicationSet pointing to spoke clusters)
- **Container registry** (Harbor, replicated for HA)
- **Secret store** (Vault, accessed by spokes via External Secrets)
- **Observability backend** (Prometheus federation, Loki, Tempo)
- **Identity provider** (Keycloak, OAuth proxy, OIDC)
- **Cluster lifecycle** (Cluster API for provisioning/upgrading spokes)
- **Policy engine** (OPA Gatekeeper, Kyverno policies pushed to spokes)

### Spoke clusters run:
- **Workloads** (your apps, your data)
- **Local CNI** (Cilium, Calico)
- **Local storage** (PVs, CSI)
- **Lightweight agents** (Argo CD agent, Prometheus node-exporter, log forwarders)

### Benefits
- **Centralized policy + observability** — one place to manage
- **Standardized config across spokes** — GitOps enforces consistency
- **Tenant isolation** — each spoke is fully separate
- **Independent failure domains** — hub failure doesn't break running spokes
- **Easier compliance** — per-spoke isolation for regulated workloads

### Hub HA considerations
```text
Hub cluster MUST be HA itself:
  - Run hub services with PDB
  - Multi-zone deployment
  - Backup of GitOps state + secrets

What happens if hub dies?
  - Spokes keep running their workloads (loose coupling)
  - No new deployments until hub is back
  - No new policies pushed
  - Observability backend unavailable (but local Prometheus still works)
  → Hub failure = annoying, not catastrophic
```

### Standard enterprise stack
```text
Hub:                           Spokes:
  Red Hat ACM or               Vanilla K8s or OpenShift
  SUSE Rancher Prime           Cilium CNI
  Argo CD ApplicationSet       External Secrets → Vault
  Harbor (replicated)          Local Prometheus
  Vault Enterprise             Local Loki (or remote write)
  Prometheus + Thanos          DCGM (for GPU spokes)
```

---

## 3.2 Multi-Cluster Federation (Less Common)

### Architecture
```text
       ┌─────────────────────────────────────────────────┐
       │             Federation Control Plane             │
       │  (KubeFed v2, Karmada, Liqo, Admiralty)         │
       └────────────┬─────────────┬──────────────────────┘
                    │             │
        ┌───────────┼─────────────┼───────────┐
        │           │             │           │
   ┌────▼────┐ ┌────▼────┐  ┌────▼────┐  ┌────▼────┐
   │Cluster 1│ │Cluster 2│  │Cluster 3│  │Cluster 4│
   │ Peer    │ │ Peer    │  │ Peer    │  │ Peer    │
   └─────────┘ └─────────┘  └─────────┘  └─────────┘
   US-East    US-West       EU         APAC
```

### What federation provides
- **Cross-cluster service discovery** — Service in cluster A can call Service in cluster B
- **Workload placement decisions** — federation engine decides which cluster runs what
- **Cross-cluster scaling** — bursting from one cluster to another
- **Cross-cluster DNS** — `service.namespace.svc.cluster.federation`

### When to use
- True multi-region active-active
- Workload mobility between clusters
- Cross-cluster failover requirements
- Hyper-scale (thousands of nodes, many clusters)

### When NOT to use
- Most enterprise cases — hub-spoke is simpler and good enough
- Federation has historically been complex and unreliable
- Operational overhead is high

### Modern alternatives
```text
Karmada (CNCF) — modern federation, simpler than KubeFed
Liqo — peer-to-peer cluster sharing
Admiralty — multi-cluster scheduler
Cluster API + ACM — provisioning-focused (not runtime federation)
```

---

## 3.3 Cluster-per-Tenant (Strong Isolation)

### Architecture
```text
     ┌────────────────────────────────────┐
     │     Shared Platform Layer           │
     │  Registry, GitOps, Observability   │
     └─────────────┬──────────────────────┘
                   │
        ┌──────────┼──────────────┬─────────────┐
        │          │              │             │
   ┌────▼────┐ ┌───▼────┐    ┌────▼────┐   ┌────▼────┐
   │Tenant A │ │Tenant B│    │Tenant C │   │Tenant D │
   │Cluster  │ │Cluster │    │Cluster  │   │Cluster  │
   │         │ │        │    │         │   │         │
   │PCI scope│ │HIPAA   │    │Public   │   │SOX      │
   │workloads│ │workload│    │workloads│   │workloads│
   └─────────┘ └────────┘    └─────────┘   └─────────┘
```

### When to use
- Strong regulatory isolation (PCI, HIPAA, FedRAMP, SOX)
- Different SLAs per tenant
- Untrusted tenants (multi-tenant SaaS)
- Different upgrade cadences per tenant

### Cost
- Each cluster has its own control plane overhead
- Cannot share spare capacity between tenants
- More to operate

### vs. namespace-based isolation
```text
Namespace isolation (cheap):
  - All tenants share API server, etcd, nodes
  - RBAC controls access
  - Network Policies control traffic
  - Pod Security Standards control privilege
  - FINE for trusted workloads
  - NOT enough for regulatory isolation

Cluster isolation (expensive):
  - Each tenant has own everything
  - Hardware-level isolation possible
  - REQUIRED for regulated/untrusted tenants
```

---

# PART 4: HPC-SPECIFIC TOPOLOGIES

## 4.1 Dedicated HPC Cluster (Recommended)

### Why a SEPARATE cluster for HPC
```text
HPC workloads have fundamentally different requirements:

General workloads:                HPC workloads:
- CPU shares OK                   - Exclusive CPU pinning required
- Default CNI fine                - Cilium/SR-IOV/RDMA
- Default scheduler               - Gang scheduling (Volcano)
- 1-4 GB RAM per pod              - 100-500 GB RAM per pod
- No GPU                          - 1-8 GPUs per pod
- Stateless mostly                - Long-running stateful jobs
- Quick startup                   - Long warmup (model loading)
- Default kernel                  - Tuned kernel (isolcpus, hugepages)

Trying to mix these in ONE cluster causes:
- Resource contention
- Scheduler complexity
- Operational difficulty (different SLAs)
- Capacity planning nightmares
```

### Standard architecture
```text
       ┌─────────────────────────────────────────────┐
       │     HPC Cluster (dedicated)                  │
       │                                              │
       │  ┌────────┐  ┌────────┐  ┌────────┐         │
       │  │Master 1│  │Master 2│  │Master 3│         │
       │  │ etcd   │  │ etcd   │  │ etcd   │         │
       │  └────────┘  └────────┘  └────────┘         │
       │  CPU pool (small, tuned for control plane)   │
       │                                              │
       │  ┌──────────────────────────────────────┐    │
       │  │  GPU Worker Pool                      │    │
       │  │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐    │    │
       │  │  │GPU 1│ │GPU 2│ │GPU 3│ │GPU N│    │    │
       │  │  │8x H100│ │8x H100│ │...│ │...│    │    │
       │  │  │NUMA   │ │NUMA   │ │   │ │   │    │    │
       │  │  │tuned  │ │tuned  │ │   │ │   │    │    │
       │  │  └─────┘ └─────┘ └─────┘ └─────┘    │    │
       │  └──────────────────────────────────────┘    │
       │                                              │
       │  ┌──────────────────────────────────────┐    │
       │  │  Storage Worker Pool (optional)       │    │
       │  │  Local NVMe, model cache, scratch     │    │
       │  └──────────────────────────────────────┘    │
       │                                              │
       │  ┌──────────────────────────────────────┐    │
       │  │  Networking                           │    │
       │  │  - Cilium CNI (primary)               │    │
       │  │  - Multus + SR-IOV (for low-latency)  │    │
       │  │  - InfiniBand/RoCE (for NCCL)         │    │
       │  │  - GPUDirect RDMA enabled             │    │
       │  └──────────────────────────────────────┘    │
       └─────────────────────────────────────────────┘
```

### Node pool design
```text
Pool 1: control-plane
  3 nodes, 8 vCPU, 32 GB RAM, NVMe
  taint: node-role.kubernetes.io/control-plane=:NoSchedule

Pool 2: gpu-inference
  8-16 nodes, 64 vCPU, 1 TB RAM, 4x H100 or 8x A100
  taint: workload=gpu-inference:NoSchedule
  label: node-role/gpu-inference=true
  Boot params: isolcpus + nohz_full + hugepages

Pool 3: gpu-training (optional, separate)
  4-8 nodes, similar specs but optimized for long jobs
  taint: workload=gpu-training:NoSchedule
  label: node-role/gpu-training=true

Pool 4: cpu-feature (optional)
  4-8 nodes, 128 vCPU, 512 GB RAM, no GPU
  For feature stores, embeddings cache, etc.
  taint: workload=cpu-feature:NoSchedule
```

---

## 4.2 Mixed General + HPC (When Separation Not Possible)

### Architecture (using node pools + taints)
```text
       ┌─────────────────────────────────────────────┐
       │     Mixed Cluster                            │
       │                                              │
       │  Control Plane (shared)                      │
       │                                              │
       │  ┌──────────────────────────────────────┐    │
       │  │  General Worker Pool                  │    │
       │  │  Default scheduling, no taints        │    │
       │  │  Runs: web apps, databases, etc.      │    │
       │  └──────────────────────────────────────┘    │
       │                                              │
       │  ┌──────────────────────────────────────┐    │
       │  │  GPU Worker Pool (tainted)            │    │
       │  │  Taint: nvidia.com/gpu=present:NoSch  │    │
       │  │  Only tolerant pods land here         │    │
       │  │  Tuned kernel, hugepages, isolcpus    │    │
       │  └──────────────────────────────────────┘    │
       └─────────────────────────────────────────────┘
```

### Pod targeting
```yaml
# GPU workload tolerates the taint
spec:
  tolerations:
  - key: nvidia.com/gpu
    operator: Exists
    effect: NoSchedule
  nodeSelector:
    node-role/gpu: "true"

# General workload — no toleration → automatically excluded from GPU nodes
spec:
  # nothing special, just regular pod
```

### When this works
- Limited budget for separate clusters
- Small to medium scale
- Single operations team
- Workloads have similar reliability/SLA requirements

### When this fails
- HPC workloads disrupt general workloads (or vice versa)
- Different upgrade cadences cause conflicts
- Compliance requires hardware isolation

---

## 4.3 GPU Training vs Inference (Different Patterns)

### Training cluster topology
```text
- Many GPUs (multi-node distributed training)
- InfiniBand fabric required for NCCL
- Shared filesystem for datasets (Lustre, BeeGFS, GPFS)
- Long-running jobs (hours to weeks)
- Checkpointing infrastructure (object storage, fast NVMe)
- Job scheduler (Volcano for gang scheduling, Kueue for queueing)
- Lower density per node (4-8 GPUs to maximize per-GPU bandwidth)
- Often integrated with Slurm for HPC compatibility
```

### Inference cluster topology
```text
- Fewer GPUs per node (1-4 GPUs, MIG for sharing)
- Standard Ethernet/RoCE for KV transfer (Dynamo)
- Local model cache or S3-backed
- Short-running requests (ms to seconds)
- No checkpointing (stateless mostly)
- Standard K8s scheduler with PriorityClass
- Higher density per node
- Often combined with serving frameworks (KServe, Triton, vLLM)
```

### Combined inference + training? Usually not
- Different priorities (training can wait, inference cannot)
- Different network requirements
- Different SLAs
- Solution: separate clusters, share container registry and observability
- Or: Run:ai / Volcano for workload management with preemption

---

# PART 5: AIRGAP TOPOLOGIES

## 5.1 The Three Airgap Architectures

### Architecture A: DMZ Sync Pattern (Most Common)
```text
Internet           DMZ Zone              Internal Network
                   ┌───────────┐         ┌──────────────────┐
                   │           │         │                  │
                   │ DMZ       │         │  Internal        │
                   │ Harbor    │ Pull    │  Harbor          │
[Public Registries]│ (staging) │◄────────┤  (production)    │
        │          │           │ via     │                  │
        │          │ DMZ Git   │ Harbor  │  Internal Git    │
        └─────────►│ (mirror)  │ replic. │  (source of      │
                   │           │         │   truth)         │
                   │           │ Manual  │                  │
                   │           │ approval│  K8s Clusters    │
                   └───────────┘ gates   │                  │
                       │                 │  ┌────────────┐  │
                       │                 │  │ Hub        │  │
                       │  Network        │  │ Cluster    │  │
                       │  diode/gate     │  └────────────┘  │
                       └────────────────►│                  │
                                         │  ┌────────────┐  │
                                         │  │ Spoke      │  │
                                         │  │ Cluster(s) │  │
                                         │  └────────────┘  │
                                         └──────────────────┘
```

### How it works
1. **DMZ Harbor** pulls images from public registries (controlled list)
2. **Vulnerability scan** in DMZ (Trivy in Harbor)
3. **Manual approval gate** (security team reviews + approves)
4. **Cosign signing** in DMZ
5. **Replication policy** copies approved images to Internal Harbor
6. **Internal K8s clusters** pull ONLY from Internal Harbor

### Same for Git
- DMZ Git mirrors selected public repos (Helm charts, etc.)
- Internal Git is source of truth, populated via DMZ Git pulls

### Network controls
- DMZ → Internal: one-way replication only (network diode or strict firewall)
- Internal → Internet: BLOCKED
- Internal clusters can only resolve internal DNS

---

### Architecture B: Sneaker-Net (Highest Security)
```text
Public         External         Removable          Internal
Registries     Build System     Media              Network
    │              │             ┌────┐                │
    └──────────────┤             │USB │                │
                   │             │DVD │                │
            [Pull images]        │HDD │           [Internal
            [Sign with offline   └────┘            Harbor]
             cert]                  ▲                  ▲
                   │                │                  │
                   ▼                │                  │
            [Burn to media]─────────┘                  │
                                                       │
                                                  [Import via
                                                   skopeo/oras]
                                                       │
                                                       ▼
                                                  [K8s clusters
                                                   pull from
                                                   internal
                                                   Harbor]
```

### When to use
- Highest classification (defense, intelligence)
- True air gap (no network path at all)
- Tempest-shielded environments
- OT/SCADA in critical infrastructure

### Operational reality
- Image promotion takes days/weeks (manual cycle)
- Burns physical media, transports to facility, ingests
- Emergency patches are painful
- Often paired with chain-of-custody documentation

---

### Architecture C: Restricted Egress (Least Strict)
```text
Internet  ─►  Egress Proxy ─►  K8s Clusters
              (allowlist)
              
Allowed:
- registry.k8s.io
- ghcr.io (specific repos)
- corp-approved domains
              
Blocked:
- Everything else
              
Audit:
- All egress logged
- Anomaly detection on egress
```

### When to use
- Less regulated (internal corporate workloads)
- Cloud provider equivalents (VPC service controls)
- Hybrid where cloud bursting is needed

---

## 5.2 The "Bootstrap" Problem in Airgap

### How do you get the FIRST images?

```text
Step 1: Provision DMZ jumpbox
  - VM with internet access (controlled)
  - skopeo, cosign, helm, oras tools installed

Step 2: Initial sync (run once per K8s version)
  ./airgap-sync.sh v1.31.0
  
  This script pulls:
    - kube-apiserver, kube-scheduler, etc.
    - All operator images
    - All Helm chart images
    - Storage class CSI drivers
    - Cilium, Calico, etc.
    - NVIDIA GPU Operator images
    - Cert-manager
    - Argo CD
    - Harbor itself!
    - Prometheus, Grafana, Loki

Step 3: Verify signatures and scan
Step 4: Push to staging Harbor
Step 5: Replication to internal Harbor

Step 6: Now you can bootstrap clusters
  Internal cluster pulls FROM internal Harbor
  No internet needed
```

### Image manifest discipline
```text
You need a CURATED list of images:
  - Pinned versions (no :latest)
  - SBOM for each image
  - Cosign signature
  - Trivy scan report
  - Justification for inclusion

Tools:
  - oras (for OCI artifacts)
  - skopeo (for image copy)
  - helm template + kustomize (to discover images in manifests)
  - tern (SBOM generation)
```

---

## 5.3 The Observability Challenge

### Standard observability requires egress
```text
Stack normally pulls from:
  - quay.io (Prometheus, Grafana)
  - docker.io (Loki, Tempo)  
  - ghcr.io (OpenTelemetry)
  - grafana.com (dashboards, plugins)

In airgap:
  ✗ Cannot install Helm charts (egress)
  ✗ Cannot import dashboards from grafana.com
  ✗ Cannot pull container images
  ✗ Plugin installation requires offline bundles
```

### Solution
```text
1. Mirror all images to internal Harbor
2. Bundle dashboards in Git (JSON files committed to repo)
3. Provision Grafana via GitOps with pre-bundled dashboards
4. Plugin offline bundles deployed via ConfigMap
5. Alertmanager config from Git (no external services)
6. Webhook receivers point to INTERNAL incident system
```

---

# PART 6: REAL-WORLD TOPOLOGIES FOR YOUR STORIES

## 6.1 Fraud Detection (CapitalOne / Fiserv)

### Recommended topology: Hub-Spoke + dedicated HPC + airgap
```text
┌─────────────────────────────────────────────────────────┐
│  HUB CLUSTER (corp DC, shared services)                  │
│  - Argo CD (manages all spoke clusters)                  │
│  - Harbor (internal registry, replicated to spokes)      │
│  - Vault (secret management)                             │
│  - Prometheus federation (Thanos)                        │
│  - Compliance scanning (Falco, Trivy)                    │
└─────────┬───────────────────────────────────────────────┘
          │
          ├── Spoke 1: Hot Path Inference Cluster
          │   - 12 bare-metal GPU nodes (H100)
          │   - Cilium CNI + eBPF
          │   - isolcpus + hugepages + GPU Operator
          │   - Fraud scoring workload (sub-30ms SLA)
          │
          ├── Spoke 2: Warm/Cold Path LLM Cluster  
          │   - 6 bare-metal GPU nodes
          │   - TensorRT-LLM serving
          │   - Slightly less aggressive tuning (sec-level SLA)
          │
          ├── Spoke 3: Feature Store Cluster
          │   - 8 CPU-heavy nodes  
          │   - Redis cluster, Postgres
          │   - High-availability stateful workloads
          │
          ├── Spoke 4: Training Cluster
          │   - 4 GPU nodes
          │   - Daily model retraining
          │   - Volcano gang scheduling
          │
          └── Spoke 5: Compliance/Audit Cluster
              - Isolated PCI scope
              - Kafka for audit events
              - Separate observability
```

### Why this topology
- **Isolation**: Hot path failure doesn't affect training, training doesn't impact hot path
- **Compliance**: PCI scope is in its own cluster, simpler audit
- **Operational**: Different teams own different spokes
- **Scaling**: Add more hot path nodes without touching training
- **Upgrades**: Stagger across spokes for safety

### Bridge in interview
> "For CapitalOne's fraud platform, I'd recommend a hub-spoke topology with
> dedicated spoke clusters for hot path inference, warm/cold path LLM serving,
> feature stores, training, and audit/compliance. Hot path inference gets a
> dedicated GPU cluster with aggressive kernel tuning — isolcpus, hugepages,
> Cilium eBPF — because sub-30ms latency is non-negotiable. Training has its
> own cluster because long-running jobs would interfere with inference SLAs.
> Compliance/audit is its own cluster for PCI scope isolation. The hub cluster
> runs Argo CD, Harbor (replicated to all spokes), Vault, and Thanos for
> federated observability. All airgap: image promotion through DMZ Harbor
> with Trivy scanning and Cosign signing."

---

## 6.2 Siri Cloud (Apple)

### Recommended topology: Multi-region clusters, regional autonomy
```text
US-East Region              US-West Region        EU/APAC Regions
┌────────────────┐         ┌────────────────┐    ┌────────────────┐
│ ASR Cluster    │         │ ASR Cluster    │    │ ASR Cluster    │
│ Multi-zone     │         │ Multi-zone     │    │ Multi-zone     │
│ Stateless      │         │ Stateless      │    │ Stateless      │
└────────┬───────┘         └────────────────┘    └────────────────┘
         │                                                
         │ (zero-copy pipeline on same hosts)
         │
┌────────▼───────┐
│ NLU+Search     │
│ Cluster        │
│ GPU-heavy      │
│ Co-located     │
│ with ASR       │
└────────────────┘

Hub (US-East):
  - Model registry
  - Global config (per-locale routing rules)
  - Cross-region monitoring
```

### Special characteristic: stage co-location
- ASR, NLU, Search, TTS pods scheduled on SAME node for zero-copy
- Uses K8s pod affinity + shared memory volumes
- Multi-zone for HA, but never cross-zone (zero-copy must be on same host)

---

## 6.3 Broadcom Security Telemetry

### Recommended topology: Hub-spoke, customer-isolated spokes
```text
Hub (Broadcom DC):
  - Tenant management
  - Central rule library
  - Cross-tenant analytics
  - Customer-facing API

Spokes:
  - Per-customer (large enterprises get dedicated clusters)
  - Or per-region (smaller customers share regional clusters)
  - Customer data NEVER leaves their spoke
  - Telemetry aggregated to hub (with PII stripped)
```

---

# PART 7: TOPOLOGY EVOLUTION

## 7.1 How Topologies Grow

### Phase 1: MVP (single cluster, single AZ)
```text
1 cluster, 3 control plane, N workers
Goal: prove the product works
Acceptable risk: full outage if cluster fails
```

### Phase 2: HA (single cluster, multi-AZ)
```text
1 cluster, 3 control planes across AZs, workers across AZs
Goal: survive single AZ failure
Acceptable risk: full DC outage takes us down
```

### Phase 3: Multi-cluster (hub-spoke)
```text
1 hub + multiple spokes
Goal: independent failure domains, tenant isolation
Acceptable risk: hub failure pauses deployments but workloads continue
```

### Phase 4: Multi-region (federated or independent)
```text
Multiple regions, each with own hub+spoke
Goal: geographic redundancy, latency optimization
Acceptable risk: limited (can survive region failure with traffic shift)
```

### Phase 5: Workload-specific specialization
```text
HPC clusters, general clusters, edge clusters, dev clusters
Each tuned for its workload pattern
Goal: optimal performance per workload type
```

### Don't skip phases
- Going from Phase 1 directly to Phase 5 = operational chaos
- Each phase teaches the team operational discipline
- Tooling matures as topology grows

---

# PART 8: INTERVIEW Q&A FOR TOPOLOGY

## Q: "Walk me through how you'd design a K8s topology for an HPC workload at a bank"

> "I'd start with the question: what's the blast radius tolerance? For
> a fraud detection workload where every transaction must be scored in
> 30ms, the answer is 'minimal' — we cannot afford a cluster-wide outage.
> 
> So I'd design hub-spoke with at least 3 spoke clusters: one for hot path
> inference (GPU, ultra-low-latency tuned), one for warm/cold path LLM
> serving (GPU, less aggressive), and one for compliance/audit (PCI scope
> isolation). The hub cluster runs Argo CD, Harbor (replicated to all
> spokes), Vault for secrets, and Thanos for federated metrics.
> 
> Each spoke is multi-zone within its DC for AZ failure tolerance, with
> Cilium CNI for low-latency networking. The hot path spoke runs with
> isolcpus, hugepages, GPU Operator, and PriorityClass+PDB to protect
> inference workloads.
> 
> Airgap considerations: DMZ Harbor pulls from public registries, Trivy
> scans, security approves, Cosign signs, then internal Harbor receives
> via replication. All clusters pull only from internal Harbor.
> 
> For multi-region, I'd replicate the entire hub+spoke topology to a
> second region with global traffic routing handling failover. Sessions
> would be sticky to a region for KV cache locality."

## Q: "When would you use multi-cluster vs namespace isolation?"

> "Namespace isolation is for TRUSTED workloads with similar SLAs that
> can share infrastructure. It's cheap — one cluster, RBAC for access,
> Network Policies for traffic, Pod Security Standards for privilege.
> Good for dev/staging environments, internal apps, microservice
> separation within a single product.
> 
> Multi-cluster is for UNTRUSTED workloads, REGULATORY isolation, or
> DIFFERENT SLAs. PCI scope must be its own cluster — hardware-level
> isolation, separate compliance scope, simpler audit. Multi-tenant SaaS
> often uses cluster-per-tenant for the largest customers. Workloads with
> very different upgrade cadences benefit from separate clusters so each
> can move independently.
> 
> The cost difference is significant — each cluster has its own control
> plane overhead (3 nodes minimum), operational surface area, and CSI
> drivers. So you want the simplest topology that meets your isolation
> requirements."

## Q: "How do you handle DR for K8s clusters?"

> "Three levels. First, within a cluster: multi-zone control plane,
> topology spread for workloads, PVs with cross-zone replication. This
> handles AZ failure.
> 
> Second, cluster recovery: etcd backups every 30 minutes to remote
> storage, Velero for resource snapshots, GitOps means cluster
> configuration is in Git. Combined, I can rebuild a cluster from
> scratch in hours — provision nodes, kubeadm init, restore etcd,
> Argo CD reconciles everything from Git.
> 
> Third, cross-DC failover: standby cluster in different DC, GitOps
> deploys same workloads, traffic routed via global LB. For stateful
> workloads, async replication of data (Postgres streaming replication,
> Kafka MirrorMaker). RPO target depends on workload — typically
> minutes for replicated state.
> 
> Key principle: treat the cluster as cattle, not pets. Cluster failure
> should be recoverable from Git + etcd backup + data replication, not
> from heroic operational efforts."

---

# PART 9: 7 TOPOLOGY DECISION RULES

```text
RULE 1: Match topology to FAILURE DOMAINS, not org chart.
        If two services must fail together, they can be in one cluster.
        If they must NOT fail together, separate clusters.

RULE 2: HPC and general workloads belong in SEPARATE clusters.
        Different scheduler tuning, different kernels, different SLAs.

RULE 3: Hub-spoke for ENTERPRISE, single cluster for STARTUP.
        Hub-spoke adds complexity that's worth it at scale.

RULE 4: Compliance scope = cluster boundary.
        PCI, HIPAA, FedRAMP — separate clusters, separate everything.

RULE 5: Multi-zone single cluster BEFORE multi-cluster.
        Get HA within one cluster first, then expand.

RULE 6: External etcd above 500 nodes.
        Below that, stacked is fine and simpler.

RULE 7: Airgap means CURATED, not COPIED.
        Mirror only what you've reviewed and approved.
        Never blindly mirror public registries.
```

---

# PART 10: YOUR SENTINEL POSITIONING

Tie topology decisions to Sentinel concerns:

> "Sentinel reinforces my view that topology matters enormously for
> reliability and performance. We learned that workload isolation —
> putting fraud scoring in its own cluster separate from general workloads
> — was essential because the hot path's latency requirements would have
> been impossible to maintain in a shared cluster. We learned that
> compliance isolation requires hardware separation, not just RBAC.
> And we learned that hub-spoke gives operational consistency without
> coupling workload failure domains. The right topology made Sentinel
> reliable; the wrong topology would have made it impossible to operate."
