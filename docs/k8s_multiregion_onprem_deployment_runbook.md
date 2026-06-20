# Multi-Region On-Prem Enterprise Kubernetes for LLM + Generic Workloads
## End-to-End Deployment & Operations Runbook (Open-Source First)

> **Goal of this guide:** Hand this to a competent platform engineer and they
> can take a pile of bare-metal servers in two+ datacenters and end up with a
> production-grade, multi-region, hub-spoke Kubernetes platform that serves
> both **LLM/GPU inference** and **generic enterprise workloads**, with robust
> observability, security, and DR — enterprise-ready.
>
> **Reference use case:** Capital One-style **fraud detection** platform.
> Hot-path fraud scoring (sub-30ms), warm/cold-path LLM serving, feature
> stores, training, and a PCI-scoped audit plane.
>
> **Stack philosophy:** Open-source first. Each section has a short
> **"Enterprise counterpart"** note where a commercial product is commonly
> swapped in.

---

## How to read this runbook

The guide is ordered as a real deployment program, not a feature list:

```text
Phase 0  Capacity Planning            ← size everything before buying/racking
Phase 1  Reference Architecture       ← the target picture
Phase 2  Tool & Framework Selection   ← decide the stack once
Phase 3  Host & OS Preparation        ← BIOS, kernel, NUMA, drivers
Phase 4  Control Plane Bootstrap      ← HA kubeadm + external etcd + LB
Phase 5  Core Platform Layer          ← CNI, CSI, DNS, GPU Operator
Phase 6  Multi-Region Hub-Spoke       ← GitOps, registry, secrets, Cluster API
Phase 7  LLM / GPU Serving Stack      ← KServe + vLLM + gateway + autoscaling
Phase 8  Generic Workload Plane       ← ingress, stateful, policies
Phase 9  Observability                ← metrics, logs, traces, GPU, SLOs
Phase 10 Security & Compliance        ← supply chain, runtime, PCI isolation
Phase 11 Multi-Region DR & Failover   ← backups, replication, traffic routing
Phase 12 Day-2 Operations             ← upgrades, scaling, runbooks
Phase 13 Enterprise-Readiness Gate    ← the checklist that says "go live"
```

Each phase lists **what**, **why**, **the open-source tool**, **the exact
steps/commands**, and **validation** ("done when…").

---

# PHASE 0 — CAPACITY PLANNING (Capital One Fraud Platform)

> **Principle:** You cannot rack hardware you have not sized. Capacity planning
> drives node counts, GPU counts, etcd sizing, network fabric, and power/cooling.
> Get this wrong and every later phase compounds the error.

## 0.1 Characterize the workload first

Capture these numbers before any sizing math. Example values for the Capital
One fraud use case:

```text
WORKLOAD DIMENSIONS (gather real numbers; these are worked examples)

Hot-path fraud scoring (tree/GBDT + small NN):
  - Peak TPS:                 24,500 transactions/sec
  - Latency SLO:              p99 < 30 ms end-to-end
  - Model type:               XGBoost + small DNN (CPU or light GPU)
  - Statefulness:             stateless scoring, feature lookups external

Warm/cold-path LLM (explanations, case narratives, analyst assist):
  - Peak QPS:                 ~400 req/sec
  - Latency SLO:              p99 < 3 s (seconds-class)
  - Model:                    7B–70B class, quantized
  - Tokens:                   ~512 in / ~256 out average

Feature store:
  - Read QPS:                 ~150,000 lookups/sec (fan-out from scoring)
  - Latency:                  p99 < 5 ms
  - Backing:                  Redis (hot) + Postgres/Cassandra (cold)

Training / retraining:
  - Cadence:                  daily + ad-hoc
  - GPUs:                     4–8 for distributed retrain
  - Duration:                 hours

Audit / compliance (PCI scope):
  - Event rate:               every scored txn emits an audit record
  - Retention:                7 years (object store, immutable)
```

## 0.2 Size the LLM/GPU tier (the expensive part)

Work backwards from throughput and the serving engine's measured tokens/sec.

```text
STEP 1 — Per-GPU throughput (measure, do not guess)
  Benchmark your model on the target GPU with vLLM:
    e.g. Llama-3-8B (FP8) on H100 → ~3,000 output tok/s sustained
    (continuous batching, PagedAttention)

STEP 2 — Required aggregate throughput
  400 req/s × 256 out tok = 102,400 output tok/s peak

STEP 3 — GPUs for steady state
  102,400 / 3,000 ≈ 35 GPU-equivalents of output capacity
  BUT batching efficiency + prefill cost → apply 1.5× → ~52 "busy" GPUs

STEP 4 — Headroom + HA
  + 30% burst headroom
  + N+1 per failure domain (zone) so one zone loss is survivable
  → round to node boundaries

STEP 5 — Node packing
  Node = 8× H100 (SXM, NVLink) → ~7 usable GPUs after system reserve
  ~52 × 1.3 / 7 ≈ 10 GPU nodes for LLM inference
  + spread across 3 zones (so ≥1 node per zone can fail) → 12 nodes

RESULT (LLM inference spoke): ~12× 8-GPU nodes, multi-zone
```

> **MIG note:** For the smaller fraud-explanation models, slice H100s with
> **MIG** (e.g., 7×1g.10gb) to raise density and isolate tenants. Reserve full
> GPUs only for the largest models.

## 0.3 Size the hot-path (latency-bound, CPU-heavy) tier

Latency-bound tiers are sized by **concurrency and tail latency**, not average
utilization.

```text
Little's Law:  concurrency = throughput × latency
  24,500 TPS × 0.030 s = ~735 in-flight requests at the SLO boundary

Per-core capacity (measured): ~1,200 scoring ops/sec/core at p99 budget
  24,500 / 1,200 ≈ 21 cores busy at peak

Add: tail headroom (2×) + HA (N+1 per zone) + sidecars/feature-lookup cost
  → ~64 dedicated vCPU of scoring capacity, isolcpus-pinned
  → 3–4 nodes (16–32 pinned cores each) across 3 zones
```

> **Key**: hot-path nodes use **CPU Manager `static`** + **Topology Manager
> `single-numa-node`** + `isolcpus`/`nohz_full` so scoring threads never share
> cores with system noise. (Detailed kernel config in Phase 3.)

## 0.4 Size the control plane & etcd

```text
CONTROL PLANE
  3 control-plane nodes per cluster (quorum, survive 1 loss)
  5 nodes only for very large clusters (>500 workers)

ETCD SIZING (critical — etcd is the cluster's heartbeat)
  - DB size quota:      default 2 GB; raise to max recommended 8 GB
  - Disk:               dedicated NVMe, p99 fsync < 10 ms (etcd is fsync-bound)
  - Members:            odd number (3 or 5); quorum = (N/2)+1
  - External etcd:      use when worker count > 500 OR compliance needs it
  - Network:            etcd peers < 10 ms RTT (ideally < 5 ms) across zones

OBJECT BUDGET RULE OF THUMB
  Pods + Secrets + ConfigMaps + CRs drive etcd size.
  Keep total objects < ~1M; watch large Secrets and CRD sprawl.
```

> **Decision (per Rule 6 of topology):** Below 500 nodes use **stacked** etcd
> (cheaper, simpler). Above 500, or for PCI isolation, use **external etcd** on
> dedicated NVMe nodes.

## 0.5 Network & storage fabric sizing

```text
NETWORK
  - Pod network CNI:        Cilium (eBPF) — sized for E-W microservice traffic
  - LLM/NCCL training:      InfiniBand or RoCEv2 + GPUDirect RDMA (separate fabric)
  - Hot-path low-latency:   Multus + SR-IOV VF for the scoring path if needed
  - Inter-zone:             redundant links, < 10 ms, headroom for etcd + PV repl
  - Inter-region:           WAN; async only (never run etcd across regions)

STORAGE
  - Block (RWO):            local NVMe via local-path / OpenEBS / TopoLVM
                            (model cache, etcd, databases)
  - Shared (RWX):           Ceph (Rook) or NFS for datasets/checkpoints
  - Object (S3 API):        MinIO (on-prem S3) for models, backups, audit, logs
  - Training datasets:      parallel FS (Lustre/BeeGFS) if multi-node training
```

## 0.6 Capacity planning output (the BOM)

The deliverable of Phase 0 is a **bill of materials per region**:

```text
PER REGION (mirror in region 2 for DR)

Hub cluster (shared services):
  3× control-plane     (8 vCPU, 32 GB, NVMe)
  3× worker            (32 vCPU, 128 GB, NVMe)   ← Argo, Harbor, Vault, Thanos

Spoke: hot-path inference:
  3× control-plane
  4× worker            (32 pinned cores, 256 GB, NVMe, tuned kernel)

Spoke: LLM inference:
  3× control-plane
  12× GPU worker       (8× H100, 2 TB RAM, NVMe, RoCE)

Spoke: feature store:
  3× control-plane
  6× worker            (CPU-heavy, lots of RAM, fast NVMe) ← Redis + Postgres

Spoke: training:
  3× control-plane
  4× GPU worker        (8× H100, InfiniBand, shared FS)

Spoke: audit/PCI:
  3× control-plane
  3× worker            (isolated, immutable object store)

Shared fabric: MinIO cluster, Ceph cluster, IB/RoCE switches, LB pair
```

> **Enterprise counterpart:** Capacity planning tooling — open-source
> **Goldilocks** (VPA recommendations) + **Karpenter/Cluster Autoscaler** for
> elasticity; enterprise often adds **Densify** or cloud-provider sizing tools.

**Done when:** you have per-region node counts, GPU counts, etcd disk specs,
network fabric plan, and storage plan signed off against the workload numbers.

---

# PHASE 1 — REFERENCE ARCHITECTURE (the target picture)

The target is **hub-spoke per region**, with two regions for DR.

```text
        REGION A (primary)                         REGION B (DR / active-active)
┌──────────────────────────────────┐      ┌──────────────────────────────────┐
│  HUB CLUSTER (shared services)    │      │  HUB CLUSTER (mirror)             │
│   Argo CD · Harbor · Vault        │◄────►│   Argo CD · Harbor · Vault        │
│   Thanos · Keycloak · Cluster API │ Git  │   Thanos · Keycloak · Cluster API │
└──────┬───────────────────────────┘ repl └──────┬───────────────────────────┘
       │ GitOps to spokes                         │
   ┌───┼─────────┬──────────┬─────────┐       (same spoke set mirrored)
   ▼   ▼         ▼          ▼         ▼
 hot- LLM-     feature-  training-  audit/
 path infer    store              PCI
 spoke spoke    spoke    spoke     spoke

Global traffic:  GSLB / Anycast (e.g., PowerDNS + keepalived, or F5/NS1)
                 routes users to nearest healthy region; sticky by region
                 for KV-cache / session locality.
```

**Why hub-spoke (recap of the decision rules):**
- Match topology to **failure domains**, not org chart (Rule 1).
- HPC/LLM and generic workloads in **separate spokes** (Rule 2).
- **Compliance scope = cluster boundary** → PCI audit is its own spoke (Rule 4).
- **Multi-zone single cluster before multi-cluster** within each spoke (Rule 5).

**Done when:** the architecture diagram is agreed and each spoke's purpose,
isolation level, and failure domain are written down.

---

# PHASE 2 — TOOL & FRAMEWORK SELECTION (decide once)

Pick the stack now; changing core components later is expensive.

| Layer | Open-Source Choice | Why | Enterprise Counterpart |
|-------|--------------------|-----|------------------------|
| Distro / installer | **kubeadm** (+ Kubespray for fleet) | Vanilla, portable, no lock-in | OpenShift, Rancher RKE2, Tanzu |
| OS | **Ubuntu LTS** or **Flatcar/RHEL-clone** | Stable, tuned kernels | RHEL CoreOS |
| Container runtime | **containerd** | CRI standard, lean | CRI-O (OpenShift) |
| CNI | **Cilium** (eBPF) | Perf, NetworkPolicy, Hubble, no kube-proxy | Cilium Enterprise, Calico Ent |
| CSI block | **OpenEBS / TopoLVM** + **Rook-Ceph** | Local NVMe + shared | Portworx, Pure, NetApp Trident |
| Object store | **MinIO** | On-prem S3 API | Dell ECS, NetApp StorageGRID |
| GPU | **NVIDIA GPU Operator** + **DCGM** | Drivers, device plugin, MIG, metrics | NVIDIA AI Enterprise |
| GitOps | **Argo CD** (+ ApplicationSet) | Multi-cluster, declarative | Akuity, Codefresh GitOps |
| Cluster lifecycle | **Cluster API (CAPI)** | Declarative cluster provisioning | Rancher, ACM |
| Registry | **Harbor** | Scan, sign, replicate, RBAC | JFrog Artifactory, Quay |
| Secrets | **Vault (OSS)** + **External Secrets Operator** | Central secrets, rotation | Vault Enterprise, CyberArk |
| Identity | **Keycloak** (OIDC) | SSO for API + apps | Okta, Ping, Azure AD |
| Ingress / GW | **Envoy Gateway / Gateway API** + **Cilium** | Modern L7, gRPC | NGINX+, Kong, F5 |
| LLM serving | **KServe** + **vLLM** (+ TensorRT-LLM) | Autoscale, canary, batching | NVIDIA NIM, Anyscale |
| Gen scheduling | default scheduler + **Kueue** | Queueing, quotas | Run:ai, Volcano Ent |
| GPU/job scheduling | **Volcano** / **Kueue** | Gang scheduling for training | Run:ai |
| Metrics | **Prometheus** + **Thanos** | HA, long-term, federation | Grafana Cloud, Chronosphere |
| Logs | **Loki** + **Vector/Fluent Bit** | Cheap, label-based | Elastic, Splunk |
| Traces | **Tempo** + **OpenTelemetry** | Distributed tracing | Honeycomb, Datadog |
| Dashboards/alerts | **Grafana** + **Alertmanager** | Unified panes | Grafana Enterprise |
| Policy | **Kyverno** (+ OPA Gatekeeper) | Admission policy as code | Styra DAS |
| Runtime security | **Falco** + **Tetragon** | eBPF threat detection | Sysdig Secure, Aqua |
| Image/IaC scan | **Trivy** + **Cosign** | SBOM, CVE, signing | Snyk, Prisma Cloud |
| Backup/DR | **Velero** + **etcd snapshots** | Resource + volume backup | Kasten K10, Portworx PX |
| Autoscaling | **HPA/VPA** + **KEDA** + **Cluster Autoscaler** | Event + resource scaling | — |
| Service mesh (opt) | **Cilium Service Mesh / Istio** | mTLS, traffic mgmt | Istio (Solo.io), Linkerd Ent |

> **Guiding rule:** prefer CNCF-graduated, vendor-neutral projects so the
> platform stays portable across DCs and (if ever) cloud.

**Done when:** every layer above has a chosen tool and a named owner.

---

# PHASE 3 — HOST & OS PREPARATION (bare metal)

> Do this on **every node** via Ansible/Kickstart so it is reproducible. GPU and
> hot-path nodes get extra tuning.

## 3.1 Base OS prep (all nodes)

```bash
# Disable swap (kubelet requirement)
sudo swapoff -a
sudo sed -i '/ swap / s/^/#/' /etc/fstab

# Kernel modules + sysctls for k8s networking
cat <<EOF | sudo tee /etc/modules-load.d/k8s.conf
overlay
br_netfilter
EOF
sudo modprobe overlay && sudo modprobe br_netfilter

cat <<EOF | sudo tee /etc/sysctl.d/k8s.conf
net.bridge.bridge-nf-call-iptables  = 1
net.bridge.bridge-nf-call-ip6tables = 1
net.ipv4.ip_forward                 = 1
EOF
sudo sysctl --system

# Time sync (etcd/raft demand it)
sudo apt-get install -y chrony && sudo systemctl enable --now chrony
```

## 3.2 containerd runtime (all nodes)

```bash
sudo apt-get install -y containerd
sudo mkdir -p /etc/containerd
containerd config default | sudo tee /etc/containerd/config.toml >/dev/null
# Use the systemd cgroup driver (matches kubelet)
sudo sed -i 's/SystemdCgroup = false/SystemdCgroup = true/' /etc/containerd/config.toml
sudo systemctl restart containerd && sudo systemctl enable containerd
```

## 3.3 Hot-path & GPU node kernel tuning (latency nodes only)

Isolate cores so scoring/inference threads run undisturbed (NUMA + IRQ):

```bash
# GRUB: isolate cores 4-31, quiet ticks, keep IRQs off perf cores
sudo sed -i 's/GRUB_CMDLINE_LINUX="\(.*\)"/GRUB_CMDLINE_LINUX="\1 isolcpus=4-31 nohz_full=4-31 rcu_nocbs=4-31 irqaffinity=0-3 default_hugepagesz=1G hugepagesz=1G hugepages=64"/' /etc/default/grub
sudo update-grub && sudo reboot
```

Then enforce alignment at the kubelet level (this is the **Topology Manager**
config your earlier docs reference):

```yaml
# /var/lib/kubelet/config.yaml (latency + GPU node pools)
apiVersion: kubelet.config.k8s.io/v1beta1
kind: KubeletConfiguration
cpuManagerPolicy: static                # exclusive CPU pinning for Guaranteed pods
topologyManagerPolicy: single-numa-node # align CPU, memory, GPU, NIC on one NUMA node
memoryManagerPolicy: Static
reservedSystemCPUs: "0-3"               # housekeeping cores
systemReserved:
  cpu: "2"
  memory: "4Gi"
kubeReserved:
  cpu: "1"
  memory: "2Gi"
```

> **Why:** `cpuManagerPolicy: static` + `topologyManagerPolicy: single-numa-node`
> turns CPU requests into real pinned cores and keeps GPU+NIC+memory on the same
> NUMA node — the difference between stable and jittery p99. Latency pods must be
> **Guaranteed QoS** (integer CPU, requests == limits) to benefit.

## 3.4 NVIDIA prerequisites (GPU nodes)

Let the **GPU Operator** manage drivers (don't hand-install). Just ensure:

```bash
# Confirm GPUs are visible and IOMMU is on for RDMA
lspci | grep -i nvidia
# Add 'intel_iommu=on iommu=pt' (or amd_iommu=on) to GRUB if using GPUDirect RDMA
```

**Done when:** `containerd` runs with systemd cgroups, swap is off, latency nodes
boot with `isolcpus`/hugepages, and kubelet config is staged for tuned pools.

---

# PHASE 4 — CONTROL PLANE BOOTSTRAP (HA per cluster)

> Repeat per cluster (hub + each spoke). Automate with **Kubespray** for fleets;
> shown here with **kubeadm** for clarity.

## 4.1 API server load balancer (VIP)

Place an HA VIP in front of the 3 API servers. Open-source: **HAProxy +
keepalived** (or **MetalLB** for the data plane LBs).

```haproxy
# /etc/haproxy/haproxy.cfg (on 2 LB nodes, VIP via keepalived)
frontend k8s-api
    bind *:6443
    mode tcp
    default_backend k8s-api-backends
backend k8s-api-backends
    mode tcp
    balance roundrobin
    option tcp-check
    server cp1 10.0.0.11:6443 check
    server cp2 10.0.0.12:6443 check
    server cp3 10.0.0.13:6443 check
```

## 4.2 Initialize first control-plane node

```bash
# kubeadm-config.yaml
cat <<EOF > kubeadm-config.yaml
apiVersion: kubeadm.k8s.io/v1beta4
kind: ClusterConfiguration
kubernetesVersion: v1.31.0
controlPlaneEndpoint: "k8s-api.region-a.internal:6443"  # the VIP
networking:
  podSubnet: "10.244.0.0/16"
  serviceSubnet: "10.96.0.0/12"
# --- external etcd (use for >500 nodes / PCI); omit block for stacked etcd ---
etcd:
  external:
    endpoints:
      - https://10.0.1.11:2379
      - https://10.0.1.12:2379
      - https://10.0.1.13:2379
    caFile: /etc/etcd/pki/ca.crt
    certFile: /etc/etcd/pki/apiserver-etcd-client.crt
    keyFile: /etc/etcd/pki/apiserver-etcd-client.key
EOF

sudo kubeadm init --config kubeadm-config.yaml --upload-certs
```

> **Stacked vs external:** for spokes under 500 nodes, delete the `etcd.external`
> block and kubeadm runs stacked etcd on the control-plane nodes (simpler). Use
> external etcd for the largest spokes and the PCI spoke.

## 4.3 Join remaining control-plane + worker nodes

```bash
# control-plane join (uses certificate key from --upload-certs)
sudo kubeadm join k8s-api.region-a.internal:6443 \
  --token <token> --discovery-token-ca-cert-hash sha256:<hash> \
  --control-plane --certificate-key <cert-key>

# worker join
sudo kubeadm join k8s-api.region-a.internal:6443 \
  --token <token> --discovery-token-ca-cert-hash sha256:<hash>
```

## 4.4 Spread control plane across zones

Label and verify control-plane nodes land in different zones/racks so one zone
loss keeps etcd quorum:

```bash
kubectl label node cp1 topology.kubernetes.io/zone=zone-a
kubectl label node cp2 topology.kubernetes.io/zone=zone-b
kubectl label node cp3 topology.kubernetes.io/zone=zone-c
```

**Done when:** `kubectl get nodes` shows 3 Ready control-plane nodes across 3
zones, etcd reports a healthy quorum (`etcdctl endpoint health`), and the API VIP
survives killing one control-plane node.

---

# PHASE 5 — CORE PLATFORM LAYER

Install in this order: **CNI → CoreDNS verify → CSI → GPU Operator → metrics**.

## 5.1 Cilium CNI (eBPF, kube-proxy-free)

```bash
helm repo add cilium https://helm.cilium.io && helm repo update
helm install cilium cilium/cilium --namespace kube-system \
  --set kubeProxyReplacement=true \
  --set k8sServiceHost=k8s-api.region-a.internal --set k8sServicePort=6443 \
  --set hubble.relay.enabled=true --set hubble.ui.enabled=true \
  --set loadBalancer.l7.backend=envoy \
  --set bandwidthManager.enabled=true

cilium status --wait
```

> **Why Cilium:** eBPF dataplane (no iptables blowup), NetworkPolicy + L7,
> Hubble flow visibility for debugging, and it replaces kube-proxy. For the
> low-latency hot-path, add **Multus + SR-IOV** as a secondary network.

## 5.2 MetalLB (bare-metal service LBs)

```bash
helm install metallb metallb/metallb -n metallb-system --create-namespace
kubectl apply -f - <<EOF
apiVersion: metallb.io/v1beta1
kind: IPAddressPool
metadata: { name: prod-pool, namespace: metallb-system }
spec: { addresses: ["10.0.20.100-10.0.20.200"] }
---
apiVersion: metallb.io/v1beta1
kind: L2Advertisement
metadata: { name: l2, namespace: metallb-system }
EOF
```

## 5.3 Storage (CSI)

```bash
# Local NVMe block (RWO) for etcd, DBs, model cache
helm install openebs openebs/openebs -n openebs --create-namespace

# Shared/object: Rook-Ceph (RWX + S3) OR MinIO for S3-only
helm install rook-ceph rook-release/rook-ceph -n rook-ceph --create-namespace
# MinIO (on-prem S3 for models, backups, audit, logs)
helm install minio minio/minio -n minio --create-namespace \
  --set mode=distributed --set replicas=4 --set persistence.size=2Ti
```

Set a default StorageClass and a fast class for latency workloads:

```bash
kubectl patch storageclass openebs-hostpath \
  -p '{"metadata":{"annotations":{"storageclass.kubernetes.io/is-default-class":"true"}}}'
```

## 5.4 NVIDIA GPU Operator (GPU spokes)

```bash
helm repo add nvidia https://helm.ngc.nvidia.com/nvidia && helm repo update
helm install gpu-operator nvidia/gpu-operator -n gpu-operator --create-namespace \
  --set driver.enabled=true \
  --set toolkit.enabled=true \
  --set dcgmExporter.enabled=true \
  --set mig.strategy=mixed            # enable MIG slicing for inference density
```

Enable MIG on a node (example 7×1g for small-model inference):

```bash
kubectl label node gpu-node-1 nvidia.com/mig.config=all-1g.10gb
```

## 5.5 Node pool taints/labels (placement contract)

This is the **taint + toleration + affinity** trinity that reserves nodes:

```bash
# Reserve GPU inference nodes
kubectl taint nodes gpu-node-1 workload=llm-inference:NoSchedule
kubectl label nodes gpu-node-1 node-role/llm-inference=true
# Reserve hot-path nodes
kubectl taint nodes hot-1 workload=hot-path:NoSchedule
kubectl label nodes hot-1 node-role/hot-path=true
```

**Done when:** Cilium is healthy, a test `LoadBalancer` Service gets an IP,
`kubectl get pods -n gpu-operator` shows DCGM + device-plugin running, and a
GPU test pod sees `nvidia.com/gpu`.

---

# PHASE 6 — MULTI-REGION HUB-SPOKE (GitOps backbone)

> Build the **hub** first; it then provisions and governs all spokes.

## 6.1 Argo CD on the hub (GitOps engine)

```bash
helm install argocd argo/argo-cd -n argocd --create-namespace \
  --set configs.params."application\.namespaces"="*"
# Register each spoke cluster
argocd cluster add spoke-hotpath-ctx
argocd cluster add spoke-llm-ctx
argocd cluster add spoke-feature-ctx
```

Use **ApplicationSet** to fan the same platform config across all spokes:

```yaml
apiVersion: argoproj.io/v1alpha1
kind: ApplicationSet
metadata: { name: platform-baseline, namespace: argocd }
spec:
  generators:
    - clusters: {}                       # every registered spoke
  template:
    metadata: { name: 'baseline-{{name}}' }
    spec:
      project: platform
      source:
        repoURL: https://git.internal/platform/baseline.git
        targetRevision: main
        path: overlays/{{metadata.labels.spoke-type}}
      destination: { server: '{{server}}', namespace: platform }
      syncPolicy: { automated: { prune: true, selfHeal: true } }
```

> **Why GitOps:** every spoke converges to Git. A wiped spoke is rebuilt by
> pointing Argo CD at it again — the basis of the DR story in Phase 11.

## 6.2 Harbor registry (scan + sign + replicate)

```bash
helm install harbor harbor/harbor -n harbor --create-namespace \
  --set expose.type=ingress --set externalURL=https://harbor.region-a.internal \
  --set trivy.enabled=true --set notary.enabled=false
```

Set up **replication** hub→region-B and hub→PCI spoke so every cluster pulls from
a local Harbor (airgap-friendly).

## 6.3 Vault + External Secrets

```bash
helm install vault hashicorp/vault -n vault --create-namespace \
  --set server.ha.enabled=true --set server.ha.raft.enabled=true
helm install external-secrets external-secrets/external-secrets -n external-secrets --create-namespace
```

Spokes consume secrets via `ExternalSecret` CRs — secrets never live in Git.

## 6.4 Keycloak (OIDC SSO) wired into the API server

```bash
helm install keycloak bitnami/keycloak -n keycloak --create-namespace
```

Point each cluster's API server at Keycloak for OIDC auth, then bind groups to
RBAC `ClusterRole`s (platform-admin, app-dev, auditor, sre).

## 6.5 Cluster API (provision/upgrade spokes declaratively)

```bash
clusterctl init --infrastructure metal3   # bare-metal provider
# New spokes become Git-managed Cluster objects → reproducible, upgradeable
```

**Done when:** hub runs Argo CD/Harbor/Vault/Keycloak, every spoke is a
registered Argo cluster receiving the baseline ApplicationSet, and a test app
deployed via Git appears on the correct spoke.

---

# PHASE 7 — LLM / GPU SERVING STACK

> Deployed to the **LLM inference spoke** via GitOps. Stack: **KServe** (model
> serving CRDs + autoscaling + canary) running **vLLM** (continuous batching,
> PagedAttention), with **Gateway API** out front.

## 7.1 Install KServe + Knative (scale-to-N, canary)

```bash
# KServe brings InferenceService CRD, autoscaling, canary traffic splitting
kubectl apply -f https://github.com/kserve/kserve/releases/download/v0.13/kserve.yaml
```

## 7.2 Deploy an LLM with vLLM runtime

```yaml
apiVersion: serving.kserve.io/v1beta1
kind: InferenceService
metadata:
  name: fraud-explainer-llm
  namespace: llm-serving
spec:
  predictor:
    minReplicas: 3                 # N+1 per zone, never scale to zero for hot models
    maxReplicas: 12
    tolerations:
      - key: workload
        operator: Equal
        value: llm-inference
        effect: NoSchedule
    nodeSelector:
      node-role/llm-inference: "true"
    model:
      modelFormat: { name: vllm }
      runtime: kserve-vllmserver
      storageUri: s3://models/llama3-8b-fp8     # from MinIO
      resources:
        limits:   { nvidia.com/gpu: "1", cpu: "8", memory: "64Gi" }
        requests: { nvidia.com/gpu: "1", cpu: "8", memory: "64Gi" }   # Guaranteed QoS
      args:
        - --quantization=fp8
        - --max-model-len=8192
        - --enable-prefix-caching          # reuse KV for repeated prefixes
```

> **TensorRT-LLM note:** once model + shape buckets are stable, swap the runtime
> to **TensorRT-LLM** for higher throughput. Keep vLLM as the flexible default.

## 7.3 Autoscaling on the right signal

Scale LLM pods on **GPU/queue depth**, not CPU, via **KEDA + DCGM/vLLM metrics**:

```yaml
apiVersion: keda.sh/v1alpha1
kind: ScaledObject
metadata: { name: llm-autoscale, namespace: llm-serving }
spec:
  scaleTargetRef: { name: fraud-explainer-llm-predictor }
  minReplicaCount: 3
  maxReplicaCount: 12
  triggers:
    - type: prometheus
      metadata:
        serverAddress: http://prometheus.monitoring:9090
        query: avg(vllm_num_requests_waiting)   # queue depth = real pressure
        threshold: "5"
```

## 7.4 Gang scheduling for training (training spoke)

```bash
helm install volcano volcano-sh/volcano -n volcano-system --create-namespace
# Volcano PodGroup ensures all distributed-training pods start together or not at all
# Kueue adds quota/queueing so training never starves inference capacity
helm install kueue kueue/kueue -n kueue-system --create-namespace
```

## 7.5 Inference gateway (token-aware routing)

Front all models with **Gateway API** (Envoy Gateway), enforcing per-tenant rate
limits, retries, and circuit breaking; route hot models with session affinity for
KV-cache locality.

**Done when:** an `InferenceService` is Ready, serves tokens under SLO, scales on
queue depth, and a canary (10% traffic to a new model version) works via KServe.

---

# PHASE 8 — GENERIC WORKLOAD PLANE

For the non-GPU enterprise apps (APIs, feature store, web), on untainted nodes.

## 8.1 Ingress / Gateway API

```bash
helm install eg oci://docker.io/envoyproxy/gateway-helm -n envoy-gateway-system --create-namespace
```

## 8.2 Stateful services (feature store spoke)

- **Redis** (hot features) via the Redis Operator, multi-zone, RWO local NVMe.
- **Postgres/Cassandra** (cold features) via CloudNativePG / K8ssandra, with
  PodAntiAffinity to spread replicas across zones.

```yaml
# Spread DB replicas across zones (HA)
affinity:
  podAntiAffinity:
    requiredDuringSchedulingIgnoredDuringExecution:
      - labelSelector: { matchLabels: { app: postgres } }
        topologyKey: topology.kubernetes.io/zone
```

## 8.3 Workload autoscaling & disruption protection

```yaml
# Protect availability during node drains/upgrades
apiVersion: policy/v1
kind: PodDisruptionBudget
metadata: { name: fraud-scoring-pdb }
spec:
  minAvailable: 80%
  selector: { matchLabels: { app: fraud-scoring } }
```

Use **HPA** for stateless APIs, **VPA/Goldilocks** for right-sizing
recommendations, **Cluster Autoscaler** (or Karpenter-style) for node elasticity.

## 8.4 Multi-zone spread for all critical Deployments

```yaml
topologySpreadConstraints:
  - maxSkew: 1
    topologyKey: topology.kubernetes.io/zone
    whenUnsatisfiable: DoNotSchedule
    labelSelector: { matchLabels: { app: fraud-scoring } }
```

**Done when:** generic apps are reachable through the gateway, DBs are HA across
zones, and PDBs + topology spread are in place for every critical workload.

---

# PHASE 9 — OBSERVABILITY (the part that makes it operable)

> Three pillars + GPU + SLOs. Deploy a **local stack per cluster**, federate to
> the **hub** via Thanos so the platform team has one global pane.

## 9.1 Metrics: Prometheus + Thanos (HA + long-term)

```bash
helm install kube-prometheus-stack prometheus-community/kube-prometheus-stack \
  -n monitoring --create-namespace \
  --set prometheus.prometheusSpec.replicas=2 \
  --set prometheus.prometheusSpec.thanos.objectStorageConfig.existingSecret.name=thanos-objstore
# Thanos sidecar ships blocks to MinIO; Thanos Querier on hub fans out to all spokes
helm install thanos bitnami/thanos -n monitoring \
  --set query.enabled=true --set storegateway.enabled=true \
  --set compactor.enabled=true
```

## 9.2 GPU metrics: DCGM Exporter (already from GPU Operator)

Scrape `dcgm-exporter` for per-GPU utilization, memory, temperature, ECC errors,
SM activity. Essential for LLM capacity and saturation alerts.

## 9.3 Logs: Loki + Vector/Fluent Bit

```bash
helm install loki grafana/loki-distributed -n logging --create-namespace \
  --set loki.storage.bucketNames.chunks=loki --set loki.storage.type=s3   # MinIO
helm install fluent-bit fluent/fluent-bit -n logging   # DaemonSet log shipper
```

## 9.4 Traces: Tempo + OpenTelemetry

```bash
helm install tempo grafana/tempo-distributed -n tracing --create-namespace
helm install otel-collector open-telemetry/opentelemetry-collector -n tracing
```

Instrument the request path (gateway → scoring → feature lookup → LLM) so a slow
p99 can be traced to the exact hop.

## 9.5 Grafana + Alertmanager (dashboards & SLOs)

```bash
# Grafana ships with kube-prometheus-stack; add dashboards from Git (airgap-safe)
```

Define **SLOs and burn-rate alerts** that match the capacity plan:

```yaml
# Example PrometheusRule: hot-path latency SLO burn
groups:
  - name: slo-hotpath
    rules:
      - alert: HotPathP99Budget
        expr: histogram_quantile(0.99, sum(rate(scoring_latency_bucket[5m])) by (le)) > 0.030
        for: 5m
        labels: { severity: page }
        annotations: { summary: "Fraud scoring p99 > 30ms SLO" }
      - alert: LLMQueueSaturation
        expr: avg(vllm_num_requests_waiting) > 10
        for: 10m
        labels: { severity: page }
```

## 9.6 The 4 golden signals per tier

```text
HOT-PATH:   latency p99, error rate, scoring throughput, CPU throttling (cpu.stat)
LLM:        tokens/sec, queue depth, GPU util/mem (DCGM), KV-cache hit rate
FEATURE:    lookup p99, Redis hit rate, replication lag
PLATFORM:   etcd fsync p99, apiserver latency, node pressure, PV saturation
```

> **Airgap reminder:** mirror all observability images to Harbor and commit
> Grafana dashboards as JSON in Git — grafana.com is unreachable in airgap.

**Done when:** the hub Grafana shows every spoke's golden signals, GPU metrics
flow from DCGM, traces span the full request path, and SLO burn alerts page.

---

# PHASE 10 — SECURITY & COMPLIANCE HARDENING

> Tie compliance scope to cluster boundary (PCI spoke is isolated). Layer
> supply-chain, admission, runtime, and network controls.

## 10.1 Supply chain: scan + sign + verify

```bash
# Trivy scans in Harbor on push; Cosign signs approved images
cosign sign --key cosign.key harbor.internal/fraud/scorer:v1.4.2
# Kyverno admission: only signed images from internal Harbor may run
```

```yaml
apiVersion: kyverno.io/v1
kind: ClusterPolicy
metadata: { name: require-signed-images }
spec:
  validationFailureAction: Enforce
  rules:
    - name: verify-signature
      match: { any: [{ resources: { kinds: [Pod] } }] }
      verifyImages:
        - imageReferences: ["harbor.internal/*"]
          attestors: [{ entries: [{ keys: { publicKeys: "<cosign-pub>" } }] }]
```

## 10.2 Admission policy as code (Kyverno)

Enforce baseline hardening cluster-wide:

```text
- disallow privileged / hostNetwork / hostPath (except approved system DaemonSets)
- require runAsNonRoot, drop ALL capabilities, readOnlyRootFilesystem
- require resource requests/limits (so QoS + scheduling are predictable)
- require Pod Security Standards: 'restricted' for app namespaces
```

## 10.3 Runtime security (eBPF)

```bash
helm install falco falcosecurity/falco -n falco --create-namespace \
  --set tetragon.enabled=true   # Tetragon for eBPF process/network enforcement
```

## 10.4 Network segmentation (default-deny)

```yaml
# Cilium default-deny per namespace, then allow only required flows
apiVersion: cilium.io/v2
kind: CiliumNetworkPolicy
metadata: { name: default-deny, namespace: fraud }
spec:
  endpointSelector: {}
  ingress: []
  egress:
    - toEndpoints: [{ matchLabels: { app: feature-store } }]
```

## 10.5 PCI spoke isolation

```text
- Dedicated cluster (hardware isolation, separate etcd, separate observability)
- Immutable audit object store (MinIO with object-lock / WORM, 7-yr retention)
- Separate Keycloak realm + auditor RBAC; no platform-admin cross-access
- Network diode / one-way replication for audit egress to analytics
```

## 10.6 RBAC & Access Control (Cluster vs Namespace, Admins vs Developers)

> **Principle: least privilege + identity from one source.** Humans never get
> static kubeconfig certs. They authenticate via **Keycloak OIDC** (Phase 6.4)
> and land in **groups**; groups are bound to Kubernetes roles. Workloads use
> **ServiceAccounts**, never user identities.

### The access model at a glance

```text
                 Keycloak (OIDC groups)                Kubernetes RBAC
  ┌───────────────────────────────────┐     ┌────────────────────────────────┐
  platform-admins   ──────────────────┼────►│ ClusterRoleBinding → cluster-admin (break-glass)
  platform-sre      ──────────────────┼────►│ ClusterRoleBinding → platform-operator
  auditors          ──────────────────┼────►│ ClusterRoleBinding → view (+ audit logs)
  team-fraud-admins ──────────────────┼────►│ RoleBinding (ns: fraud-*) → namespace-admin
  team-fraud-devs   ──────────────────┼────►│ RoleBinding (ns: fraud-dev) → developer
  └───────────────────────────────────┘     └────────────────────────────────┘

  Scope rule:  ClusterRole + ClusterRoleBinding = whole cluster
               ClusterRole + RoleBinding         = that ONE namespace only  ← reuse roles
               Role + RoleBinding                = that ONE namespace only
```

> **Key RBAC fact interviewers probe:** a `ClusterRole` bound with a
> **RoleBinding** grants its permissions **only inside the binding's namespace**.
> This lets you define a role once and reuse it per namespace — the basis of the
> developer model below.

### Tier 1 — Cluster-level: Platform Admin (break-glass)

Full `cluster-admin` is **break-glass only** — not a daily identity. Keep the
group tiny, require MFA, and audit every use.

```yaml
# Cluster admin = built-in cluster-admin, bound to a SMALL OIDC group
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: platform-admins
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: cluster-admin          # built-in superuser
subjects:
  - apiGroup: rbac.authorization.k8s.io
    kind: Group
    name: platform-admins      # Keycloak group; MFA-enforced, break-glass
```

### Tier 2 — Cluster-level: Platform SRE / Operator (daily ops, not god)

SREs run the platform but should **not** read every tenant Secret or bypass
policy. Define a scoped `ClusterRole` instead of `cluster-admin`.

```yaml
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: platform-operator
rules:
  # Manage nodes, namespaces, platform addons across the cluster
  - apiGroups: [""]
    resources: ["nodes", "namespaces", "persistentvolumes", "events"]
    verbs: ["get", "list", "watch", "patch", "update"]
  - apiGroups: ["apps"]
    resources: ["deployments", "daemonsets", "statefulsets"]
    verbs: ["get", "list", "watch", "update", "patch"]
  - apiGroups: ["storage.k8s.io"]
    resources: ["storageclasses"]
    verbs: ["*"]
  # Explicitly NO blanket access to secrets in tenant namespaces
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: platform-sre
roleRef: { apiGroup: rbac.authorization.k8s.io, kind: ClusterRole, name: platform-operator }
subjects:
  - { apiGroup: rbac.authorization.k8s.io, kind: Group, name: platform-sre }
```

### Tier 3 — Cluster-level: Auditor (read-only + audit)

```yaml
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: auditors-readonly
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: view                  # built-in read-only (no secrets)
subjects:
  - { apiGroup: rbac.authorization.k8s.io, kind: Group, name: auditors }
```

> Use the built-in **`view`** ClusterRole (read-only, excludes Secrets) rather
> than writing your own. Built-ins: `cluster-admin`, `admin`, `edit`, `view`.

### Tier 4 — Namespace-level: Team / Namespace Admin

A team admin owns their namespaces (manage RBAC, quotas, workloads) but has
**zero** cluster-wide power. Bind the built-in **`admin`** ClusterRole with a
**RoleBinding** (scopes it to the namespace).

```yaml
# Gives full control of the 'fraud-prod' namespace ONLY (incl. managing
# RoleBindings within it), but nothing cluster-wide.
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: fraud-namespace-admins
  namespace: fraud-prod
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: admin                 # built-in: full namespace control, no cluster scope
subjects:
  - { apiGroup: rbac.authorization.k8s.io, kind: Group, name: team-fraud-admins }
```

### Tier 5 — Namespace-level: Developer (deploy + debug, no privilege escalation)

Developers deploy and troubleshoot in **their** namespace(s). Two common options:

- **Quick path:** bind the built-in **`edit`** ClusterRole (read/write workloads,
  no RBAC editing) via RoleBinding.
- **Hardened path:** a custom `developer` Role that adds `pods/log`,
  `pods/exec`, `pods/portforward` for debugging but **denies** Secrets write and
  RBAC changes.

```yaml
# Custom developer role — deploy + debug, least privilege
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: developer
  namespace: fraud-dev
rules:
  - apiGroups: ["apps"]
    resources: ["deployments", "replicasets", "statefulsets"]
    verbs: ["get", "list", "watch", "create", "update", "patch", "delete"]
  - apiGroups: [""]
    resources: ["pods", "services", "configmaps", "persistentvolumeclaims"]
    verbs: ["get", "list", "watch", "create", "update", "patch", "delete"]
  - apiGroups: [""]
    resources: ["pods/log", "pods/exec", "pods/portforward"]  # debugging
    verbs: ["get", "list", "create"]
  - apiGroups: [""]
    resources: ["secrets"]
    verbs: ["get", "list"]      # read app secrets, but NOT create/modify
  # No access to: rolebindings, roles, resourcequotas, nodes, namespaces
---
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: fraud-developers
  namespace: fraud-dev
roleRef: { apiGroup: rbac.authorization.k8s.io, kind: Role, name: developer }
subjects:
  - { apiGroup: rbac.authorization.k8s.io, kind: Group, name: team-fraud-devs }
```

### Permission matrix (who can do what)

```text
Capability                      │ Plat │ Plat │ Audit │ NS    │ Dev
                                │ Admin│ SRE  │ -or   │ Admin │
────────────────────────────────┼──────┼──────┼───────┼───────┼─────
Manage nodes / cluster config   │  ✔   │  ✔   │   ─   │   ─   │  ─
Create/delete namespaces        │  ✔   │  ✔   │   ─   │   ─   │  ─
Manage StorageClasses/CRDs      │  ✔   │  ✔   │   ─   │   ─   │  ─
Read any namespace (no secrets) │  ✔   │  ✔   │   ✔   │ own   │ own
Manage RBAC in a namespace      │  ✔   │  ─   │   ─   │   ✔   │  ─
Deploy/patch workloads          │  ✔   │  ✔   │   ─   │   ✔   │  ✔(own)
exec / logs / port-forward      │  ✔   │  ✔   │   ─   │   ✔   │  ✔(own)
Create/modify Secrets           │  ✔   │  ─   │   ─   │   ✔   │  ─
Edit ResourceQuota/LimitRange   │  ✔   │  ✔   │   ─   │   ─   │  ─
cluster-admin (break-glass)     │  ✔   │  ─   │   ─   │   ─   │  ─
```

### Per-namespace guardrails (pair RBAC with quotas + policy)

RBAC says *who*; pair it with **ResourceQuota**, **LimitRange**, and Kyverno so a
developer cannot exhaust the cluster or escalate privilege.

```yaml
apiVersion: v1
kind: ResourceQuota
metadata: { name: fraud-dev-quota, namespace: fraud-dev }
spec:
  hard:
    requests.cpu: "64"
    requests.memory: 256Gi
    requests.nvidia.com/gpu: "4"
    pods: "100"
---
apiVersion: v1
kind: LimitRange
metadata: { name: fraud-dev-limits, namespace: fraud-dev }
spec:
  limits:
    - type: Container
      default:        { cpu: "500m", memory: 512Mi }
      defaultRequest: { cpu: "250m", memory: 256Mi }
```

### Workloads use ServiceAccounts, not humans

```yaml
# App pods run as a dedicated SA with only the RBAC they need (often none)
apiVersion: v1
kind: ServiceAccount
metadata: { name: fraud-scorer, namespace: fraud-prod }
automountServiceAccountToken: false   # turn off unless the app calls the API
```

> Bind a `Role` to a ServiceAccount **only** if the workload genuinely calls the
> Kubernetes API (operators, controllers). Most app pods need **no** API access —
> disable token automount to shrink the attack surface.

### GitOps + RBAC: make access itself declarative

All Roles/Bindings above live in Git and are reconciled by **Argo CD**
(ApplicationSet, Phase 6.1). Benefits:

```text
- Access changes go through PR review + audit trail (who granted what, when)
- Self-healing: manual `kubectl` privilege grants get reverted by Argo CD
- Per-spoke overlays: dev spokes looser, prod/PCI spokes stricter
- The PCI spoke uses a SEPARATE Keycloak realm → no platform-admin cross-access
```

### Verify access (test before trusting)

```bash
# Impersonate to confirm boundaries hold
kubectl auth can-i create deployments -n fraud-dev \
  --as=alice --as-group=team-fraud-devs            # → yes
kubectl auth can-i delete nodes \
  --as=alice --as-group=team-fraud-devs            # → no
kubectl auth can-i get secrets -n fraud-prod \
  --as=bob --as-group=platform-sre                 # → no (SRE has no tenant secrets)
```

> **Enterprise counterpart:** open-source uses Keycloak groups → RBAC. Enterprise
> shops often add **Okta/Azure AD** for SSO, **Styra DAS / OPA** for centralized
> authorization policy across clusters, and **Teleport** for short-lived,
> session-recorded `kubectl` access with per-command audit.

**Done when:** humans authenticate via OIDC groups (no static certs), break-glass
`cluster-admin` is a tiny audited group, SREs/auditors have scoped cluster roles,
each team gets namespace-admin + developer roles via RoleBindings, quotas/limits
are enforced per namespace, and `kubectl auth can-i` checks confirm the
boundaries.

---

# PHASE 11 — MULTI-REGION DR & FAILOVER

> Three levels of resilience: **in-cluster (zone)** → **cluster rebuild** →
> **cross-region failover**. Treat clusters as cattle.

## 11.1 Level 1 — In-cluster (zone failure)

Already built: multi-zone control plane (etcd quorum survives 1 zone),
topologySpread + PodAntiAffinity, multi-zone PVs. **One zone down = cluster keeps
serving.**

## 11.2 Level 2 — Cluster rebuild (etcd + Velero + GitOps)

```bash
# Automated etcd snapshots every 30 min to MinIO
etcdctl snapshot save /backup/etcd-$(date +%s).db
# Velero for resource + PV snapshots
velero install --provider aws --bucket velero --backup-location-config \
  region=minio,s3ForcePathStyle=true,s3Url=http://minio.internal:9000
velero schedule create daily --schedule="0 2 * * *"
```

Recovery: provision nodes (Cluster API) → `kubeadm init` → restore etcd snapshot
→ Argo CD reconciles **everything** from Git. **Cluster rebuilt in hours, not
heroics.**

## 11.3 Level 3 — Cross-region failover

```text
DATA REPLICATION (async; never run etcd across regions)
  - Postgres streaming replication / Patroni standby in region B
  - Kafka MirrorMaker 2 for audit + event streams
  - MinIO bucket replication for models + backups
  - Redis: warm standby (accept cold-start on failover, or active-active CRDT)

TRAFFIC ROUTING
  - Global LB / GSLB (PowerDNS+keepalived OSS, or NS1/F5 enterprise)
  - Health-checked; shift users to region B on region A failure
  - Sticky-by-region for KV-cache / session locality

RPO/RTO TARGETS (from capacity plan)
  - Stateless (scoring, LLM): RTO minutes (GitOps redeploy + traffic shift)
  - Stateful (feature/audit): RPO minutes (async replication lag)
```

## 11.4 Chaos validation

Regularly **drain a zone** and **fail a region** in staging. The drain-time
regression (sessions losing affinity during node drain) must be discovered in
staging, never production. Wire chaos into CI against the staging hub.

**Done when:** killing a zone is a non-event, a spoke can be rebuilt from
Git+etcd backup, and a region-failover drill meets RTO/RPO targets.

---

# PHASE 12 — DAY-2 OPERATIONS

## 12.1 Cluster & node upgrades (zero-downtime)

```text
ORDER: upgrade control plane first, then workers, one zone at a time.
  1. kubeadm upgrade plan / apply on cp1 → cp2 → cp3
  2. Drain + upgrade kubelet per worker, respecting PDBs:
       kubectl drain <node> --ignore-daemonsets --delete-emptydir-data
  3. Argo CD reconciles platform addons to matching versions
  4. Stagger across spokes (canary spoke first: dev, then prod)
```

## 12.2 Scaling operations

```text
- Add GPU capacity: provision node via Cluster API → it joins tainted/labeled
  pool → KServe maxReplicas raised → no disruption.
- Right-size with Goldilocks/VPA recommendations monthly.
- Watch etcd object growth; defragment etcd quarterly:
    etcdctl defrag --cluster
```

## 12.3 Core runbooks to write (and rehearse)

```text
RB-1  etcd member down / quorum loss          → replace member, restore snapshot
RB-2  GPU node NVRM/Xid errors                → cordon, drain, RMA, DCGM health
RB-3  LLM p99 regression                      → check queue depth, KV-cache, batch
RB-4  Hot-path p99 breach                     → CPU throttling? NUMA? feature lag?
RB-5  Harbor/registry outage                  → spokes keep running; block deploys
RB-6  Hub (Argo) down                         → workloads run; no new deploys
RB-7  Region failover                         → GSLB shift + data promote
RB-8  Cert rotation / Vault unseal            → automate; alert before expiry
```

> **Hub-failure philosophy:** hub down is *annoying, not catastrophic* — spokes
> keep serving; only new deploys/policy pushes pause. Design and test for this.

## 12.4 Cost & utilization hygiene

```text
- Kubecost (OSS) for showback per spoke/tenant.
- Bin-pack generic workloads; keep GPU nodes at high utilization (MIG, Kueue).
- Idle-model scale-down for cold LLMs; never scale hot models to zero.
```

**Done when:** an upgrade has been run end-to-end in staging without SLO breach,
runbooks exist and are rehearsed, and utilization/cost dashboards are live.

---

# PHASE 13 — ENTERPRISE-READINESS GATE (go-live checklist)

Only declare the platform production-ready when **every** box is checked:

```text
CONTROL PLANE & ETCD
  [ ] 3+ control-plane nodes across 3 zones; API VIP HA-tested
  [ ] etcd quorum survives 1-zone loss; fsync p99 < 10 ms on NVMe
  [ ] etcd snapshots every 30 min to MinIO; restore drill passed

PLATFORM
  [ ] Cilium healthy; NetworkPolicies default-deny per namespace
  [ ] CSI block + shared + S3 (MinIO) all provisioning; default SC set
  [ ] GPU Operator + DCGM running; MIG configured where needed
  [ ] GitOps: every spoke reconciled by Argo CD ApplicationSet
  [ ] Harbor scanning + Cosign signing + replication to all clusters
  [ ] Vault + External Secrets; no secrets in Git
  [ ] Keycloak OIDC wired to all API servers; RBAC groups bound

WORKLOADS
  [ ] Hot-path: Guaranteed QoS, isolcpus, CPU+Topology Manager, p99 < 30 ms
  [ ] LLM: KServe+vLLM Ready, scales on queue depth, canary works
  [ ] Stateful: multi-zone, PodAntiAffinity, PDBs in place
  [ ] topologySpread on all critical Deployments

OBSERVABILITY
  [ ] Prometheus HA + Thanos federation to hub
  [ ] Loki logs + Tempo traces across full request path
  [ ] DCGM GPU metrics; golden signals per tier
  [ ] SLO burn-rate alerts paging; Alertmanager routes to on-call

SECURITY & COMPLIANCE
  [ ] Kyverno: signed-images-only, restricted PSS, resource limits enforced
  [ ] Falco/Tetragon runtime detection live
  [ ] PCI spoke fully isolated; immutable WORM audit store; 7-yr retention

ACCESS CONTROL (RBAC)
  [ ] Humans authenticate via Keycloak OIDC groups; no static kubeconfig certs
  [ ] cluster-admin is a tiny, MFA-enforced, audited break-glass group
  [ ] Platform SRE + auditor have scoped ClusterRoles (no tenant secrets)
  [ ] Each team has namespace-admin + developer RoleBindings (least privilege)
  [ ] ResourceQuota + LimitRange enforced per namespace
  [ ] Workloads use dedicated ServiceAccounts; token automount off by default
  [ ] RBAC objects in Git, reconciled by Argo CD; `kubectl auth can-i` verified

DR & OPS
  [ ] Zone-drain drill: non-event
  [ ] Spoke rebuild-from-Git drill passed
  [ ] Cross-region failover drill meets RTO/RPO
  [ ] Upgrade rehearsed in staging without SLO breach
  [ ] Runbooks RB-1..RB-8 written and rehearsed
```

---

# APPENDIX A — OPEN-SOURCE ↔ ENTERPRISE QUICK MAP

```text
kubeadm/Kubespray   → OpenShift / RKE2 / Tanzu
Cilium              → Cilium Enterprise (Isovalent)
Rook-Ceph/OpenEBS   → Portworx / Pure / NetApp Trident
MinIO               → Dell ECS / NetApp StorageGRID
Harbor              → JFrog Artifactory / Red Hat Quay
Vault OSS           → Vault Enterprise / CyberArk
Keycloak            → Okta / Ping / Azure AD
Argo CD             → Akuity / Codefresh
Cluster API         → Rancher / Red Hat ACM
KServe+vLLM         → NVIDIA NIM / Anyscale
Volcano/Kueue       → Run:ai
Prometheus+Thanos   → Grafana Cloud / Chronosphere
Loki                → Elastic / Splunk
Falco/Tetragon      → Sysdig Secure / Aqua
Velero              → Kasten K10 / Portworx PX-Backup
Trivy               → Snyk / Prisma Cloud
```

> **When to pay:** buy enterprise for **24×7 support SLAs**, **certified
> compliance artifacts**, and **scale/HA features** (e.g., Vault Enterprise
> replication, Cilium Enterprise mesh). Keep the architecture portable so the
> swap is a drop-in, not a redesign.

---

# APPENDIX B — DEPLOYMENT ORDER CHEAT SHEET

```text
0. Capacity plan → BOM signed off
1. Rack + OS image (Ansible) → kernel tune latency/GPU pools
2. containerd + kubeadm prerequisites on all nodes
3. HAProxy/keepalived VIP for API
4. kubeadm init hub CP → join CP×3 (zones) → join workers
5. Cilium → MetalLB → CSI (OpenEBS/Rook/MinIO) → GPU Operator
6. Hub services: Argo CD → Harbor → Vault/ESO → Keycloak → Cluster API
7. Provision spokes via Cluster API; register in Argo CD
8. ApplicationSet pushes baseline (CNI policy, CSI, GPU, observability agents)
9. LLM spoke: KServe + vLLM + KEDA + Volcano/Kueue
10. Generic spoke: Gateway API + stateful operators + PDB/HPA
11. Observability: Prometheus+Thanos, Loki, Tempo, DCGM, Grafana, SLO alerts
12. Security: Kyverno, Cosign verify, Falco, default-deny, PCI isolation
13. DR: Velero + etcd snapshots + cross-region replication + GSLB
14. Run the Phase 13 readiness gate → go live
```
