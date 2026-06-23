# Hyperscale LLM & Transformer Production Engineering

## End-to-End Deployment (Infra + Monitoring) and Governance (Troubleshooting + Operations)

**Scope:** Running LLMs and Transformer models in production at the scale of Google Gemini / OpenAI — across both **air-gapped (on-prem bare metal)** and **cloud** environments.

**Audience:** Sr. Staff / Principal / Distinguished engineers owning AI inference platforms.

**Organizing principle:** Two halves of one lifecycle.

- **Part A — DEPLOYMENT:** how the platform is *built and observed* (Infrastructure + Monitoring).
- **Part B — GOVERNANCE:** how the platform is *operated and defended* (Troubleshooting + Operations).

The unifying themes are **deterministic latency**, **zero-copy data flow**, **topology-aware placement**, and **fail-closed safety** at every layer from the LM serving API down to PCIe ACS bits and IRQ affinity.

---

## Table of Contents

**Part A — Deployment (Infrastructure + Monitoring)**

1. [The 7-Layer Mental Model](#1-the-7-layer-mental-model)
2. [Layer 1 — Host Preparation (BIOS, Kernel, GPUDirect RDMA)](#2-layer-1--host-preparation)
3. [Layer 2 — Kubernetes Node Configuration](#3-layer-2--kubernetes-node-configuration)
4. [Layer 3 — Pod Spec for GPU + RDMA](#4-layer-3--pod-spec-for-gpu--rdma)
5. [Layer 4 — NCCL & Collective Communication](#5-layer-4--nccl--collective-communication)
6. [Layer 5 — Bare-Metal Provisioning & Autoscaling (3-Pool Model)](#6-layer-5--bare-metal-provisioning--autoscaling)
7. [Layer 6 — GitOps, Policy & Secrets](#7-layer-6--gitops-policy--secrets)
8. [Layer 7 — Serving Runtime & Inference Optimization (all levels)](#8-layer-7--serving-runtime--inference-optimization)
9. [Monitoring & Observability Stack](#9-monitoring--observability-stack)
10. [Air-Gapped vs Cloud Deltas](#10-air-gapped-vs-cloud-deltas)

**Part B — Governance (Troubleshooting + Operations)**

11. [The Master Debugging Flow](#11-the-master-debugging-flow)
12. [Universal Triage: First 5 Minutes (USE + RED)](#12-universal-triage-first-5-minutes)
13. [LLM Serving Issue Map (TTFT / TPOT / Util)](#13-llm-serving-issue-map)
14. [Layer-by-Layer Troubleshooting (CPU→NUMA→GPU→Storage→Network→K8s)](#14-layer-by-layer-troubleshooting)
15. [Incident Stories to Memorize](#15-incident-stories-to-memorize)
16. [Operations: Rollouts, Capacity, Cost, SLOs](#16-operations-rollouts-capacity-cost-slos)
17. [Responsible AI, Security & Isolation](#17-responsible-ai-security--isolation)
18. [Operational Maturity Model & Quick-Reference Cards](#18-operational-maturity-model--quick-reference-cards)

---

# PART A — DEPLOYMENT (INFRASTRUCTURE + MONITORING)

---

## 1. The 7-Layer Mental Model

Every production LLM platform decision lives in exactly one of these layers. When you deploy *or* debug, you walk this stack top-down for symptoms and bottom-up for root cause.

```
┌────────────────────────────────────────────────────────────────────┐
│ L7  Serving Runtime + Optimization                                 │
│     gateway, scheduler, vLLM/TRT-LLM/SGLang/Triton, KV cache,       │
│     batching, quantization, prefill/decode disaggregation           │
├────────────────────────────────────────────────────────────────────┤
│ L6  GitOps + Policy + Secrets                                       │
│     Argo CD, Cluster API, Kyverno/OPA, Vault, External Secrets      │
├────────────────────────────────────────────────────────────────────┤
│ L5  Bare-Metal Lifecycle + Autoscaling                             │
│     Metal3, BareMetalHost CRDs, 3-pool model, Cluster Autoscaler    │
├────────────────────────────────────────────────────────────────────┤
│ L4  Collective Communication (NCCL)                                │
│     IB/RoCE, GPUDirect RDMA, tensor/pipeline/data parallelism       │
├────────────────────────────────────────────────────────────────────┤
│ L3  Pod Spec                                                       │
│     IPC_LOCK, /dev/shm, hugepages, device requests, Multus net      │
├────────────────────────────────────────────────────────────────────┤
│ L2  Kubelet / Node Config                                          │
│     CPU Manager static, Topology Manager single-numa-node,          │
│     device plugins, Multus CNI                                      │
├────────────────────────────────────────────────────────────────────┤
│ L1  Host (BIOS, Kernel, Drivers)                                   │
│     OFED, nvidia-peermem, IOMMU=PT, ACS off, hugepages, isolcpus    │
└────────────────────────────────────────────────────────────────────┘
```

**Principal-level framing:**

> "Kubernetes schedules pods, but the *runtime* schedules tokens, and the *host* schedules cache lines and interrupts. A hyperscale platform is correct only when all three agree — GPU, NIC, CPU, and memory pinned to the same NUMA node, the runtime aware of prefill vs decode, and the host's BIOS/IRQ/hugepage state validated at boot. Most 'GPU problems' are actually host or topology problems."

---

## 2. Layer 1 — Host Preparation

This is the lowest, highest-leverage layer. Get it wrong and *nothing* above it can recover the performance.

### 2.1 OS, Drivers, and InfiniBand Verification

Use Mellanox **OFED** in production (richer than inbox `rdma-core`), but inbox works for simpler setups.

```bash
# Inbox path
sudo apt install rdma-core ibverbs-utils infiniband-diags perftest

# Verify the IB fabric BEFORE involving Kubernetes
ibstat                 # link State should be ACTIVE
ibstatus               # rate matches expectation (200 Gbps NDR, etc.)
ibv_devices            # lists mlx5_0, mlx5_1, ...
ibv_devinfo            # detailed per-device info

# Prove host-to-host RDMA works (eliminate K8s as a variable)
# Node A:
ib_write_bw -d mlx5_0
# Node B:
ib_write_bw -d mlx5_0 <node-A-IB-IP>
# Expect ~190+ Gbps NDR, ~95+ Gbps HDR
```

**Rule:** Never debug NCCL inside Kubernetes until raw `ib_write_bw` between two bare hosts hits line rate. If the fabric is broken, fix it here.

### 2.2 GPUDirect RDMA (The Critical Enabler)

Without GPUDirect RDMA, collective traffic flows **GPU → CPU bounce buffer → NIC**, doubling latency and halving bandwidth. With it, the path is **GPU → NIC** directly over PCIe peer-to-peer.

```bash
# nvidia-peermem ships with driver 470+
modprobe nvidia-peermem
lsmod | grep nvidia_peermem          # must be loaded

# Persist across reboots
echo "nvidia-peermem" | sudo tee /etc/modules-load.d/nvidia-peermem.conf

# Inspect GPU↔NIC topology
nvidia-smi topo -m
```

**Topology legend (memorize):**

```
PIX  = same PCIe switch          (best for GPU↔NIC)
PXB  = single PCIe bridge        (good)
PHB  = traverses host bridge     (OK)
SYS  = crosses NUMA boundary     (BAD: PCIe → QPI/UPI → PCIe)
NV#  = NVLink                    (best for GPU↔GPU)

GOAL: GPU↔NIC = PIX or PXB.  If SYS, pin the process to the NUMA node
nearest the NIC, or the GPUDirect path silently degrades 5–8×.
```

### 2.3 BIOS / Firmware Requirements

These are non-negotiable for GPUDirect peer-to-peer. They are also the most common cause of "performance varies by reboot."

```
✅ ACS (Access Control Services)  — DISABLED on PCIe switches
                                    (ACS forces P2P through the root complex → kills GPUDirect)
✅ IOMMU                          — PT (passthrough) mode, NOT strict
✅ ATS (Address Translation Svc)  — ENABLED on NIC
✅ Above 4G Decoding              — ENABLED
✅ PCIe Max Payload Size          — 256B or higher
✅ PCIe power management (ASPM)   — DISABLED (no L0s/L1 on links)
```

### 2.4 Boot Parameters (GRUB)

```bash
GRUB_CMDLINE_LINUX="intel_iommu=on iommu=pt \
    pcie_acs_override=downstream,multifunction \
    hugepagesz=2M hugepages=4096 \
    transparent_hugepage=never \
    isolcpus=4-63 nohz_full=4-63 rcu_nocbs=4-63"
```

| Flag | Purpose |
| --- | --- |
| `iommu=pt` | Passthrough IOMMU — avoids per-DMA translation overhead while keeping isolation |
| `pcie_acs_override` | Forces ACS off at OS level for GPUDirect P2P |
| `hugepagesz=2M hugepages=4096` | Reserve 8 GiB of 2M hugepages for RDMA buffers + KV pools |
| `transparent_hugepage=never` | THP compaction causes p99 latency spikes — disable, use explicit hugepages |
| `isolcpus / nohz_full / rcu_nocbs` | Isolate hot-path cores from the scheduler, the tick, and RCU callbacks |

### 2.5 Host Verification Checklist

```bash
ibstat | grep "State: Active"                         # IB link up
lsmod | grep -E "mlx5_core|mlx5_ib|ib_core|rdma_cm"   # OFED loaded
lsmod | grep nvidia_peermem                           # GPUDirect module
nvidia-smi topo -m                                    # GPU↔NIC = PIX/PXB
numactl --hardware                                    # confirm NUMA layout
cat /proc/meminfo | grep HugePages                    # hugepages reserved
```

---

## 3. Layer 2 — Kubernetes Node Configuration

### 3.1 RDMA Device Plugin

Two strategies — shared device (most common) vs dedicated VF per pod (SR-IOV, hard isolation).

**Option A — Mellanox/NVIDIA RDMA Shared Device Plugin (DaemonSet):**

```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: rdma-shared-dp
  namespace: kube-system
spec:
  selector:
    matchLabels: { name: rdma-shared-dp }
  template:
    metadata:
      labels: { name: rdma-shared-dp }
    spec:
      hostNetwork: true
      nodeSelector:
        feature.node.kubernetes.io/pci-15b3.present: "true"   # Mellanox vendor ID
      containers:
      - name: k8s-rdma-shared-dp
        image: harbor.internal/mellanox/k8s-rdma-shared-dev-plugin:v1.5.1
        securityContext:
          privileged: true
          capabilities: { add: ["IPC_LOCK", "SYS_ADMIN"] }
        volumeMounts:
        - { name: device-plugin, mountPath: /var/lib/kubelet/device-plugins }
        - { name: config,        mountPath: /k8s-rdma-shared-dev-plugin }
        - { name: devs,          mountPath: /dev/ }
      volumes:
      - { name: device-plugin, hostPath: { path: /var/lib/kubelet/device-plugins } }
      - { name: config, configMap: { name: rdma-devices } }
      - { name: devs, hostPath: { path: /dev/ } }
---
apiVersion: v1
kind: ConfigMap
metadata: { name: rdma-devices, namespace: kube-system }
data:
  config.json: |
    { "configList": [{
        "resourceName": "hca_shared_devices_a",
        "rdmaHcaMax": 1000,
        "selectors": { "vendors": ["15b3"], "deviceIDs": ["1017","101b","101d"] }
    }]}
```

Nodes then advertise:

```bash
kubectl describe node gpu-node-1 | grep -i rdma
# rdma/hca_shared_devices_a: 1000
```

**Option B — SR-IOV (dedicated VF per pod, for tenant isolation):**

```yaml
apiVersion: sriovnetwork.openshift.io/v1
kind: SriovNetworkNodePolicy
metadata: { name: ib-sriov-policy }
spec:
  nodeSelector: { feature.node.kubernetes.io/network-sriov.capable: "true" }
  numVfs: 8
  nicSelector: { vendor: "15b3", deviceID: "1017", pfNames: ["ib0","ib1"] }
  deviceType: netdevice
  isRdma: true
  linkType: ib
  resourceName: mellanox_ib_sriov
```

> **Shared vs SR-IOV tradeoff:** Shared device plugin = simple, fine for trusted single-tenant training. SR-IOV = per-pod VF with real isolation, needed for multi-tenant inference where one tenant must not see another's RDMA traffic or starve their bandwidth.

### 3.2 Multus CNI (Dual-Network Pods)

GPU pods need **two** networks: primary (Cilium) for K8s services, secondary (IB) for RDMA.

```yaml
apiVersion: k8s.cni.cncf.io/v1
kind: NetworkAttachmentDefinition
metadata: { name: ib-network, namespace: training }
spec:
  config: |
    { "cniVersion": "0.3.1", "name": "ib-network", "type": "ib-sriov",
      "ipam": { "type": "whereabouts", "range": "10.56.0.0/16" } }
```

### 3.3 Kubelet Configuration (The Single Most Important Node Setting)

```yaml
apiVersion: kubelet.config.k8s.io/v1beta1
kind: KubeletConfiguration
cpuManagerPolicy: static
topologyManagerPolicy: single-numa-node      # CRITICAL for GPUDirect
topologyManagerScope: container
reservedSystemCPUs: "0-3"
featureGates:
  CPUManager: true
  TopologyManager: true
  DevicePlugins: true
```

> `topologyManagerPolicy: single-numa-node` is **non-negotiable**. Without it, kubelet may place the GPU on NUMA 0 and the NIC on NUMA 1. Every RDMA op then crosses QPI/UPI → you get 20–30 GB/s instead of 180+ GB/s. With single-numa-node, kubelet **rejects** the pod rather than silently misplacing it. Fail loud, not slow.

### 3.4 NVIDIA Network Operator (Automates L1+L2)

```bash
helm install network-operator nvidia/network-operator \
  --namespace nvidia-network-operator --create-namespace \
  --set deployCR=true --set nfd.enabled=true \
  --set ofedDriver.deploy=true \
  --set rdmaSharedDevicePlugin.deploy=true \
  --set sriovDevicePlugin.deploy=false \
  --set secondaryNetwork.deploy=true \
  --set secondaryNetwork.multus.deploy=true \
  --set secondaryNetwork.ipoib.deploy=true
```

Installs OFED driver, RDMA device plugin, Multus, NV-IPAM, IPoIB CNI in one shot.

### 3.5 Additional Node Resource Plumbing

| Concern | Mechanism |
| --- | --- |
| GPU exposure | NVIDIA GPU Operator (driver, device plugin, DCGM, NFD) |
| GPU partitioning | MIG (`nvidia.com/mig-3g.40gb`) for hard isolation; time-slicing for soft sharing |
| Hugepages volume | `emptyDir: { medium: HugePages-2Mi }` |
| Low-latency NIC path | SR-IOV CNI, or AF_XDP (preferred over DPDK for ops simplicity) |
| Node telemetry | eBPF exporters, node-exporter, DCGM-exporter DaemonSets |
| Storage | Local NVMe for model cache; GPFS/Lustre for shared checkpoints/datasets |

---

## 4. Layer 3 — Pod Spec for GPU + RDMA

### 4.1 Complete Pod Spec

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: nccl-training-worker
  annotations:
    k8s.v1.cni.cncf.io/networks: ib-network        # Multus: attach secondary IB net
spec:
  nodeSelector: { node-role/gpu-rdma: "true" }
  containers:
  - name: trainer
    image: harbor.internal/nvcr.io/nvidia/pytorch:24.10-py3
    securityContext:
      capabilities:
        add: ["IPC_LOCK"]            # MANDATORY: allow mlock() for RDMA mem registration
    resources:
      requests:
        cpu: "16"
        memory: "256Gi"
        nvidia.com/gpu: "4"
        rdma/hca_shared_devices_a: "1"
        hugepages-2Mi: "8Gi"
      limits:                         # exact-match limits → Guaranteed QoS, static CPU pinning
        cpu: "16"
        memory: "256Gi"
        nvidia.com/gpu: "4"
        rdma/hca_shared_devices_a: "1"
        hugepages-2Mi: "8Gi"
    env:
    - { name: NCCL_DEBUG,         value: "INFO" }      # WARN in steady state
    - { name: NCCL_IB_DISABLE,    value: "0" }
    - { name: NCCL_IB_HCA,        value: "mlx5_0,mlx5_1" }
    - { name: NCCL_IB_GID_INDEX,  value: "3" }         # RoCE only; ignore for pure IB
    - { name: NCCL_NET_GDR_LEVEL, value: "5" }         # aggressive GPUDirect RDMA
    - { name: NCCL_P2P_LEVEL,     value: "NVL" }       # NVLink for intra-node GPU↔GPU
    - { name: NCCL_SOCKET_IFNAME, value: "^docker,lo" }
    - { name: NCCL_TOPO_FILE,     value: "/etc/nccl/topo.xml" }
    volumeMounts:
    - { name: dshm,       mountPath: /dev/shm }
    - { name: hugepages,  mountPath: /hugepages }
    - { name: ib-devices, mountPath: /dev/infiniband }
  volumes:
  - name: dshm
    emptyDir: { medium: Memory, sizeLimit: "32Gi" }    # default 64MB is far too small
  - name: hugepages
    emptyDir: { medium: HugePages-2Mi }
  - name: ib-devices
    hostPath: { path: /dev/infiniband }
```

### 4.2 The Four Non-Negotiable Items

| Item | Why | Symptom if missing |
| --- | --- | --- |
| **`IPC_LOCK` capability** | Allows `mlock()` for RDMA memory registration | `ibv_reg_mr() failed: Cannot allocate memory` |
| **Large `/dev/shm` (32Gi)** | NCCL uses shared memory for intra-node coordination; default 64MB starves it | NCCL hangs on init or `NCCL: shared memory error` |
| **Hugepages request** | Fewer pinned pages, larger RDMA registration regions | Degraded RDMA bandwidth, more TLB pressure |
| **Multus network annotation** | Attaches the IB interface; without it the pod only has Cilium pod-net | No `ib0`; NCCL falls back to TCP sockets |

> The `IPC_LOCK` omission is the **#1 thing teams forget**. It is silent until the first `ibv_reg_mr()` call fails — which can be deep into a training run.

---

## 5. Layer 4 — NCCL & Collective Communication

### 5.1 Critical NCCL Environment Variables

```bash
NCCL_IB_DISABLE=0                  # 0 = enable IB, 1 = disable
NCCL_IB_HCA=mlx5_0,mlx5_1          # multiple HCAs for bandwidth
# NCCL_IB_HCA=mlx5_0:1,mlx5_1:1    # pin specific ports
NCCL_NET_GDR_LEVEL=5               # 0–5; 5 = always use GPUDirect RDMA when possible
NCCL_P2P_LEVEL=NVL                 # NVL > SYS > PHB > PIX for intra-node GPU↔GPU
NCCL_IB_TIMEOUT=22                 # raise for large clusters
NCCL_IB_RETRY_CNT=7
NCCL_IB_AR_THRESHOLD=8192          # adaptive routing threshold (matters at 8+ nodes)
NCCL_BUFFSIZE=8388608              # 8 MB
NCCL_DEBUG=INFO                    # INFO on first deploy, WARN in prod
NCCL_DEBUG_SUBSYS=INIT,NET,GRAPH
NCCL_IB_GID_INDEX=3                # RoCE v2 only (check `show_gids`)
```

### 5.2 Verifying NCCL Actually Uses RDMA

```bash
# inside the pod
/opt/nccl-tests/build/all_reduce_perf -b 8M -e 1G -f 2 -g 4
```

```
GOOD (RDMA active):
  NCCL INFO Channel 00 : 0[1c000] -> 1[1c000] [send] via NET/IB/mlx5_0
  NCCL INFO NET/IB : Using [0]mlx5_0:1/IB
  NCCL INFO Using network IB

BAD (silent TCP fallback — 10× slower):
  NCCL INFO Using network Socket
  NCCL INFO NET/Socket : Using [0]eth0
```

If you see `NET/Socket`, RDMA is NOT being used. Check in order:
1. Is `IPC_LOCK` granted?
2. Is `/dev/infiniband` mounted?
3. Is OFED installed *inside* the container image?
4. Does the image ship `rdma-core` userspace libs?

### 5.3 Expected Bandwidth

```
all_reduce_perf, 8 nodes × 8 H100, 1 GB tensor:
  Good   : 180–200 GB/s busbw  on NDR400
  Great  : ~340 GB/s busbw     on dual-rail NDR
  TCP fb : 10–20 GB/s busbw    ← something is broken
< 100 GB/s on NDR = misconfiguration. Stop and fix before scaling.
```

### 5.4 Parallelism Strategies (Serving + Training)

| Strategy | What splits | Comm pattern | Use when |
| --- | --- | --- | --- |
| **Tensor Parallel (TP)** | Each matmul split across GPUs | AllReduce per layer (NVLink) | Single model too big for one GPU; keep TP within an NVLink island |
| **Pipeline Parallel (PP)** | Layers split into stages | Point-to-point activations | Very deep models; tolerate bubble overhead |
| **Data Parallel (DP)** | Replicas of full model | AllReduce of gradients (training) | Throughput scaling; inference replica fan-out |
| **Expert Parallel (EP)** | MoE experts across GPUs | All-to-all routing | Mixture-of-Experts (Gemini-class) |

> **Topology rule:** keep TP inside an NVLink mesh (`NCCL_P2P_LEVEL=NVL`). Crossing PCIe for TP AllReduce is the classic "TP=4 only gives 2.1× speedup" trap — the cross-pair AllReduce traverses PCIe instead of NVLink.

---

## 6. Layer 5 — Bare-Metal Provisioning & Autoscaling

### 6.1 The 3-Pool Hardware Mental Model

```
Pool 1: ACTIVE       → joined to cluster, running workloads
Pool 2: STANDBY      → powered on, provisioned, cordoned/unjoined → ready in ~30s–2m
Pool 3: COLD SPARE   → racked, powered off, BMC-accessible       → joins in 5–15m
```

**Operational goal:** make pool transitions automated and auditable so capacity grows without ticket-driven manual processes.

### 6.2 Provisioning Stack (Metal3 + Cluster API)

Bare-metal hosts become Kubernetes CRDs. Metal3 drives BMCs over Redfish/IPMI.

```yaml
apiVersion: metal3.io/v1alpha1
kind: BareMetalHost
metadata: { name: gpu-node-spare-01, namespace: metal3 }
spec:
  online: false
  bootMACAddress: aa:bb:cc:dd:ee:01
  bootMode: UEFI
  bmc:
    address: redfish://10.0.1.101/redfish/v1/Systems/1
    credentialsName: bmc-secret-gpu-01
  image:
    url: http://internal-image-server/rhel-9.4-gpu-node.qcow2
    checksum: sha256:abc123...
  rootDeviceHints: { deviceName: /dev/nvme0n1 }
  userData: { name: gpu-node-cloud-init, namespace: metal3 }
```

```
              [BareMetalHost CRDs in K8s]
                        │
              ┌─────────┴─────────┐
              │  Metal3 Controller │  (Redfish/IPMI → power, image, inventory)
              └─────────┬─────────┘
       ┌────────────────┼────────────────┐
       ▼                ▼                ▼
   ACTIVE           STANDBY          COLD SPARE
 consumerRef set  consumerRef nil   online=false
 cordoned=false   cordoned=true     state=available
```

### 6.3 Golden Image + cloud-init (Air-Gapped CI)

Build one hardened, signed image per node role; per-node config via cloud-init.

```
Golden image (built in airgap CI):
  - Base RHEL 9.4 / Ubuntu 24.04 / Talos
  - Kernel boot params (isolcpus, hugepages, iommu=pt)
  - NVIDIA driver + OFED pre-installed (matched to GPU SKU / NIC firmware)
  - containerd configured with Harbor as registry mirror
  - kubelet present but NOT enabled
  - CIS/STIG hardening, Cosign signature, SHA256 published
  Roles: control-plane / gpu-inference-h100 / cpu-general / storage-node
```

```yaml
# Image build pipeline (Tekton/GitLab CI, airgap)
build-gpu-node-image:
  steps:
  - { name: build,   image: harbor.internal/packer:1.10.0,   script: "packer build gpu-node.pkr.hcl" }
  - { name: harden,  image: harbor.internal/openscap:1.4,    script: "oscap-vm xccdf eval --profile cis-rhel9-l2 ..." }
  - { name: sign,    image: harbor.internal/cosign:2.4,      script: "cosign sign-blob --key vault://kv/cosign-key ..." }
  - { name: publish, image: harbor.internal/skopeo:1.16,     script: "curl -X PUT http://image-server.internal/images/ ..." }
```

### 6.4 Autoscaling: Cluster Autoscaler + Cluster API Provider

Standard cloud autoscalers do not fit on-prem HPC. The working combination:

```
Cluster Autoscaler (--cloud-provider=clusterapi)
   └─ scales MachineDeployment replicas
       └─ Cluster API creates Machine objects
           └─ Metal3 provisions/deprovisions bare metal
HPA scales pod replicas within available node capacity
PriorityClass + preemption protect critical workloads
PDB controls disruption during drains
```

```yaml
# Cluster Autoscaler tuned for bare metal
- ./cluster-autoscaler
- --cloud-provider=clusterapi
- --node-group-auto-discovery=clusterapi:namespace=cluster-namespace
- --scale-down-enabled=true
- --scale-down-utilization-threshold=0.5
- --scale-down-delay-after-add=10m
- --scale-down-unneeded-time=10m
- --max-node-provision-time=15m          # bare metal takes longer than cloud
```

```yaml
# HPA on GPU utilization (not CPU)
metrics:
- type: Resource
  resource:
    name: nvidia.com/gpu
    target: { type: Utilization, averageUtilization: 70 }
behavior:
  scaleUp:   { stabilizationWindowSeconds: 60 }     # quick up
  scaleDown: { stabilizationWindowSeconds: 600 }    # slow down (avoid churn)
```

### 6.5 Fast Path: Standby Pool Activation (~30s)

```bash
# Node sits in cluster, cordoned + tainted
kubectl taint node gpu-standby-01 standby=true:NoSchedule
kubectl cordon gpu-standby-01
# Activate:
kubectl uncordon gpu-standby-01
kubectl taint node gpu-standby-01 standby=true:NoSchedule-
kubectl label node gpu-standby-01 node-role/gpu-inference=true
```

### 6.6 End-to-End Scale-Up Timeline

```
T+0     Git commit: MachineDeployment 12 → 16
T+30s   Argo CD reconciles
T+45s   Cluster API creates 4 Machine objects
T+1m    Metal3 powers on 4 BareMetalHosts via Redfish
T+2m    PXE/local boot → cloud-init runs
T+5m    Drivers loaded, kubelet joins (kubeadm join + bootstrap token)
T+5m30s Cilium installs → node Ready
T+6m    Argo CD applies labels/taints; NFD labels GPU/RDMA
T+6m30s GPU Operator + RDMA device plugin register resources
T+7m    Node fully resourced (nvidia.com/gpu, rdma/..., hugepages)
T+8m    Pending pods scheduled, NCCL/RDMA functional
        → ~1 minute if using the STANDBY pool instead
```

### 6.7 Self-Healing

| Failure | Mechanism |
| --- | --- |
| Unhealthy node | **Machine Health Checks** delete the Machine → Cluster API + Metal3 reprovision |
| Kernel/FS corruption | **Node Problem Detector** → cordon + drain + replace |
| Hardware (PSU/disk) | BMC alert → Prometheus → runbook → activate cold spare |

---

## 7. Layer 6 — GitOps, Policy & Secrets

### 7.1 Everything in Git

```
clusters/
└── prod-llm-cluster/
    ├── cluster-api/         (cluster.yaml, machinedeployments/, metal3hosts/)
    ├── platform/            (argocd, cilium, nvidia-gpu-operator, rdma-device-plugin)
    ├── workloads/           (llm-serving/, feature-store/, gateway/)
    └── policies/            (kyverno/, opa-gatekeeper/)
```

### 7.2 Argo CD ApplicationSet (Multi-Cluster Fan-Out)

```yaml
apiVersion: argoproj.io/v1alpha1
kind: ApplicationSet
metadata: { name: cluster-platform-stack, namespace: argocd }
spec:
  generators:
  - clusters: { selector: { matchLabels: { environment: production } } }
  template:
    metadata: { name: 'platform-{{name}}' }
    spec:
      source:
        repoURL: ssh://git.internal/k8s-clusters.git
        path: clusters/{{name}}/platform
        targetRevision: main
      destination: { server: '{{name}}', namespace: kube-system }
      syncPolicy: { automated: { prune: true, selfHeal: true } }
```

### 7.3 Policy Enforcement (Kyverno) — Gate Dangerous Capabilities

```yaml
apiVersion: kyverno.io/v1
kind: ClusterPolicy
metadata: { name: rdma-capability-allowlist }
spec:
  validationFailureAction: enforce
  rules:
  - name: verify-rdma-workload
    match: { any: [{ resources: { kinds: ["Pod"] } }] }
    preconditions:
      any:
      - key: "{{ request.object.spec.containers[].securityContext.capabilities.add[] || [] }}"
        operator: AnyIn
        value: ["IPC_LOCK"]
    validate:
      message: "IPC_LOCK requires the workload-type: rdma-approved label"
      pattern: { metadata: { labels: { workload-type: rdma-approved } } }
```

### 7.4 Secrets (Vault + External Secrets)

Bootstrap tokens, BMC credentials, Cosign signing keys live in Vault. External Secrets Operator syncs them into K8s as Secrets. Every access is Vault-audited; rotation is automated.

---

## 8. Layer 7 — Serving Runtime & Inference Optimization

This is where the model meets traffic. Optimization spans from request routing down to CUDA kernel launches.

### 8.1 Reference Serving Architecture

```
Clients
  │
┌─▼──────────────────────────────────────────────────────────────┐
│ Gateway:  auth, tenant isolation, quotas, rate limiting,        │
│           PII redaction at ingest, request shaping              │
├─────────────────────────────────────────────────────────────────┤
│ Scheduler/Router: admission control, queue-depth priority,      │
│           prompt-length bucketing, KV-cache-aware sticky routing │
├─────────────────────────────────────────────────────────────────┤
│ Serving Runtime: vLLM / TensorRT-LLM / SGLang / Triton / ORT     │
│           continuous batching, PagedAttention, prefix cache      │
├─────────────────────────────────────────────────────────────────┤
│ GPU Workers: prefill/decode (optionally disaggregated),          │
│           KV cache mgmt, tensor parallel over NVLink             │
├─────────────────────────────────────────────────────────────────┤
│ Ops plane: metrics, canary, model registry, eval gates          │
└─────────────────────────────────────────────────────────────────┘
```

### 8.2 Runtime Selection Matrix

| Runtime | Strength | Use when |
| --- | --- | --- |
| **vLLM** | PagedAttention, continuous batching, flexible | General production LLM serving, variable shapes |
| **TensorRT-LLM** | Fused kernels, compiled static engines, highest NVIDIA perf | Fixed shapes, max throughput on NVIDIA |
| **SGLang** | RadixAttention/prefix-tree cache, structured/agentic flows | Multi-agent, heavy shared-prefix RAG |
| **Triton Inference Server** | Multi-model orchestration, dynamic batching, version control | Mixed model zoo, ensembles |
| **ONNX Runtime** | Cross-platform, edge/device portability | Tree models, edge export, hardware-agnostic |

> **Key trap (from production):** TensorRT-LLM can be *slower* than vLLM when sequence lengths vary, because variable shapes trigger engine/graph recompilation. Route stable-shape traffic to TRT-LLM and variable-shape traffic to vLLM, or bucket shapes.

### 8.3 The Two Phases — Prefill vs Decode

```
PREFILL                              DECODE
- all prompt tokens at once          - one token per step
- attention O(T²)                    - attention O(T) with KV cache
- COMPUTE-bound (FLOPs)              - MEMORY-BANDWIDTH-bound (HBM)
- produces KV cache                  - reuses KV cache
- metric: TTFT                       - metric: TPOT / ITL
- high GPU utilization                - low utilization at batch=1
```

Every serving optimization below maps to *which phase it helps*.

### 8.4 Optimization Catalog — From Runtime Down to Kernels

**Tier 1 — Request/Scheduling level**

| Technique | Helps | Mechanism |
| --- | --- | --- |
| Continuous (in-flight) batching | Throughput | Admit/retire requests at decode-step granularity; no batch-boundary idle (2–4×, up to 10× vs static) |
| Chunked prefill | TTFT, fairness | Slice long prefills so they don't block other requests' decode |
| Admission control + queue priority | p99, fairness | Reject early instead of unbounded queueing; protect interactive traffic |
| Prompt-length bucketing | TTFT variance | Route long-context traffic to separate pools; stabilize shapes |
| Prefill/decode disaggregation | TTFT + TPOT | Separate compute-bound prefill workers from memory-bound decode workers |

**Tier 2 — KV cache level**

| Technique | Helps | Mechanism |
| --- | --- | --- |
| PagedAttention | Capacity | KV cache as paged blocks → 60–80% memory utilization vs 20–40% |
| Prefix caching / RadixAttention | TTFT, cost | Share KV of common system prompts / few-shot prefixes (copy-on-write) |
| Session-aware sticky routing | TTFT | Keep multi-turn KV warm on same pod → 70%+ hit rate |
| FP8/INT8 KV cache | TPOT, capacity | Halve KV footprint → larger batches, less HBM traffic |
| GPU-headroom scheduling | Stability | Route only to pods with KV headroom → avoid OOM evictions |

**Tier 3 — Attention/model level**

| Technique | Helps | Mechanism |
| --- | --- | --- |
| FlashAttention v2/v3 | TTFT, memory | Tile attention in SRAM; never materialize T×T matrix (exact, not approximate) |
| Grouped-Query Attention (GQA) | Capacity, TPOT | Share KV heads (e.g., 64 Q-heads → 8 KV-heads = 8× smaller KV) |
| Sliding-window attention | Long context | Bound KV to window W regardless of sequence length |
| Speculative decoding | TPOT | Small draft model proposes K tokens; large model verifies in parallel (2–3× if accept >70%) |
| Quantization (FP8/INT8/INT4, GPTQ/AWQ) | Throughput, capacity | Smaller weights → faster memory-bound decode; fit larger models |

**Tier 4 — Kernel/GPU level (lowest)**

| Technique | Helps | Mechanism |
| --- | --- | --- |
| CUDA Graphs | TPOT | Capture decode kernel sequence once; replay as one launch (5µs vs 150µs of launch overhead) |
| Operator fusion (Triton / Inductor) | Throughput | Fuse RMSNorm/RoPE/activation into single kernels; 1 HBM round-trip instead of N |
| Pinned (page-locked) host memory | TTFT | Async DMA H2D without driver staging buffer (3.2× faster transfers) |
| CUDA streams + overlap | Util | Overlap H2D copy with compute |
| Mixed precision for reductions | Quality | Keep softmax/LayerNorm accumulators in BF16/FP32 even with FP8 weights |

> **Precision rule:** reductions (softmax, LayerNorm) always need higher-precision accumulators. FP8 E4M3 has only a 3-bit mantissa — softmax accumulation loses precision on long sequences. Keep weights FP8, attention math BF16.

### 8.5 GPU Memory Budget (the capacity equation)

```
H100 80GB, Llama-70B, FP8, TP=4 (per GPU):
  Weights (FP8)        ~17.5 GB
  CUDA ctx + activ.    ~3 GB
  CUDA Graph workspace ~4 GB        ← steals from KV pool; budget it
  KV cache pool        ~55.5 GB     ← determines max concurrent requests
  Per-token KV (FP8)   ~40 KB/token/GPU
  At max_model_len=4096 → ~160 MB/request → ~346 concurrent
```

> When KV OOMs at 60% of expected capacity, check three things first: CUDA Graph workspace stealing memory, `max_model_len` set too high, FP8 KV cache not enabled.

### 8.6 Model Lifecycle & Rollout

```
PyTorch (train / fine-tune / LoRA / eval gates)
   → export to production runtime (vLLM / TRT-LLM / ONNX)
   → automated eval + perf regression gate (CI)
   → model registry (versioned, signed)
   → canary (5–10% traffic) → blue-green swap → full rollout
```

Model readiness ≠ container started. **Readiness = weights loaded + KV cache initialized + CUDA Graphs captured + first inference warmed.** Gate the readiness probe on a real warm-up inference.

---

## 9. Monitoring & Observability Stack

### 9.1 What to Monitor at Each Layer

```
Hardware : BMC health (iDRAC/iLO/IPMI), power, temp, fan, ECC errors, IB link
Node     : Node Ready, kubelet, runtime, driver versions (NVIDIA/OFED), DCGM
Cluster  : etcd health/disk latency, API server latency/errors, pending pods, CA decisions
GPU      : SM util, HBM capacity + bandwidth, kernel gaps, copy overhead (DCGM, Nsight)
Runtime  : TTFT, TPOT, queue depth, batch size histogram, KV cache pressure, token rate
Workload : pod restarts, resource vs request, service p50/p99, network-policy drops
Quality  : eval scores, drift (PSI), refusal/guardrail trip rate, hallucination signals
```

### 9.2 Golden Signals for LLM Serving

| Signal | Metric | Target (interactive) |
| --- | --- | --- |
| TTFT | time to first token | < 500 ms at peak concurrency |
| TPOT | time per output token | maps to HBM bandwidth roofline (e.g., < 20–50 ms) |
| Throughput | tokens/sec, RPS | maximize at SLO |
| GPU efficiency | SM cycles on GEMM vs dispatch/stall | high "useful compute ratio" |
| Saturation | KV cache utilization, queue depth | headroom before eviction |
| Errors | timeout rate, refusal rate, 5xx | within SLO budget |

### 9.3 Tooling Layers

| Layer | Production telemetry | Deep profiling |
| --- | --- | --- |
| Cluster | DCGM-exporter, node-exporter, kube-state-metrics, Prometheus/Grafana/Loki | — |
| GPU | DCGM (`DCGM_FI_DEV_GPU_UTIL`, HBM) | Nsight Systems (timeline), Nsight Compute (kernels) |
| CPU | node-exporter, eBPF | perf, Intel VTune, strace -c |
| Runtime | vLLM/SGLang/Triton metrics, OpenTelemetry traces | PyTorch profiler |
| Network | ss, ethtool counters | NCCL tests, iperf3 |
| Storage | iostat | FIO |

### 9.4 Automated Performance Regression Gate (CI)

```
On every model/runtime/compilation change:
  1. Run multi-size synthetic batch (genai-perf style)
  2. Capture deterministic nsys timeline → parse cuda_gpu_kern_sum, cuda_api_sum
  3. Block deploy / page if:
       - steady-state GEMM kernel time slips > 2%, OR
       - unexpected cudaLaunchKernel appears (CUDA Graph → eager fallback)
  4. Enforce hard SLO gates: TTFT < 500ms @ peak batch, TPOT on memory-bound roofline
```

> "I treat performance validation as a scalable distributed system — automated benchmarking that traces metrics from the serving API down to individual CUDA kernel launch timelines, so every change maps to measurable hardware efficiency and Capex impact."

### 9.5 Capacity Alerts (Prometheus)

```yaml
- alert: GpuClusterHighUtilization
  expr: avg(DCGM_FI_DEV_GPU_UTIL) by (cluster) > 75
  for: 10m
  annotations: { runbook: "Scale up MachineDeployment" }

- alert: NoSpareGpuNodes
  expr: count(kube_node_labels{label_node_role_gpu_standby="true"}) == 0
  for: 5m
  labels: { severity: critical }
  annotations: { runbook: "Provision BareMetalHosts from cold spare pool" }

- alert: PendingPodsForResource
  expr: kube_pod_status_phase{phase="Pending"} > 0
        AND ON (pod) kube_pod_container_resource_requests{resource="nvidia_com_gpu"} > 0
  for: 5m
```

---

## 10. Air-Gapped vs Cloud Deltas

| Concern | Cloud | Air-Gapped (on-prem) |
| --- | --- | --- |
| Node supply | Managed node groups / Karpenter | Metal3 + Cluster API + 3-pool model |
| Images | Public registries (ECR/GCR) | Mirror **everything** to internal Harbor through DMZ pipeline |
| Drivers/OFED | Vendor AMIs | Offline OFED RPM/DEB bundles baked into golden image |
| Model weights | Pull from S3/HF at start | Pre-cache on local NVMe; **never** download at startup |
| Secrets | Cloud KMS / IAM | Vault + External Secrets, BMC creds, Cosign keys |
| Autoscale | VM creation (seconds) | Claim from standby pool (30s) or cold provision (5–15m) |
| Verification tooling | Cloud shell | Ship `ibv_devinfo`, `perftest`, `nccl-tests` in a debug image |
| Egress | Open | Fully closed; eval datasets and base images mirrored |

**Air-gap RDMA checklist (three extra things beyond standard airgap K8s):**
1. Mirror NVIDIA Network Operator images (OFED driver, RDMA plugin, Multus) to Harbor.
2. Mirror OFED offline bundles for host driver install.
3. Use NGC base images (`nvcr.io/nvidia/pytorch`) that already ship `rdma-core` + OFED userspace.

---

# PART B — GOVERNANCE (TROUBLESHOOTING + OPERATIONS)

---

## 11. The Master Debugging Flow

For any LLM / AI serving issue, first ask: **is this request-level, runtime-level, or infrastructure-level?** Then split into the five operational layers.

```
Application / Gateway   → request rate, errors, p99, queueing, tenant, model, prompt length
LLM / AI Runtime        → TTFT, TPOT, batching, KV cache, prefill/decode, token rate
GPU / Accelerator       → SM util, HBM capacity + bandwidth, kernel gaps, copy overhead
Host System             → CPU, NUMA, hugepages, memory bandwidth, scheduling, IRQs
Cluster / Infra         → K8s, storage, network, RDMA/NCCL, autoscaling, rollout, node health
```

> **The principal line:** "I do not start by assuming it is a GPU issue. I first split latency into queue wait, preprocessing, prefill, decode, GPU execution, memory copy, network, and downstream time. Then I attribute each part back to model, tenant, node, GPU, and input shape."

---

## 12. Universal Triage: First 5 Minutes

```
1. RED — Is the SERVICE sick?       Rate, Errors, Duration
   QPS, p95/p99, error rate, timeout rate, queue depth

2. USE — Which RESOURCE is sick?    Utilization, Saturation, Errors
   CPU, GPU, memory, network, disk

3. Attribution:
   which model? which tenant? which GPU/node? which input shape?
   which batch size? which runtime version?

4. Deep profile:
   Nsight Systems / Nsight Compute / perf / VTune / PyTorch profiler / runtime profile

5. Fix:
   tune workload, runtime, placement, resource, or architecture
```

**RED = service health (throughput view). USE = resource health (infra view).** Run both before touching a single config.

---

## 13. LLM Serving Issue Map

### A. TTFT is high (user waits too long for the first token)

| Likely cause | Debug | Fix |
| --- | --- | --- |
| Gateway queueing / loose admission | Break TTFT into gateway/router/runtime-queue/tokenize/RAG/prefill/first-token | Admission control, bounded queues |
| Long prompt / prefill bottleneck | Bucket by prompt length; separate queue wait from prefill compute | Chunked prefill, separate long-context pool, cap max prompt tokens |
| Cold model load | Time weight load, KV init, graph capture | Pre-warm popular models, readiness gating |
| Tokenization / RAG latency | Trace upstream | Pre-tokenize templates, optimize retrieval, prefix caching |

> "High TTFT usually means queueing, long prefill, cold start, or retrieval latency. I bucket by prompt length and separate queue wait from prefill execution before tuning anything."

### B. TPOT is high (each generated token is slow)

| Likely cause | Tool | Fix |
| --- | --- | --- |
| Decode memory-bandwidth / KV pressure | Nsight Compute (memory-bound attention), DCGM HBM | FP8/INT8 KV cache, reduce max context, prefix caching |
| No CUDA Graphs / small-kernel storms | Nsight Systems (kernel launch gaps) | Enable CUDA Graphs, operator fusion |
| Large active batch / HBM saturation | runtime token metrics | Tune `max_num_seqs`, `max_num_batched_tokens` |
| KV fragmentation | runtime KV stats | PagedAttention, continuous batching |

> "Decode is often HBM/KV-cache bound, not compute-bound. I check TPOT, KV usage, HBM bandwidth, kernel gaps, and whether CUDA Graphs or KV quantization are enabled."

### C. GPU utilization is LOW (do not just raise batch size)

Causes: CPU preprocessing/tokenization bottleneck, batching window too small, H2D/D2H copies blocking, scheduler gaps, too many small kernels, cold starts, upstream RAG latency.

Fixes: parallel tokenization, increase concurrency, dynamic/continuous batching, pinned memory + overlap H2D, CUDA streams/graphs, pre-tokenize templates, reduce serialization.

> "Low GPU utilization usually means the GPU is starving. I look upstream at CPU preprocessing, tokenization, batching, memory copies, and scheduler gaps."

### D. GPU utilization is HIGH but throughput is POOR

`nvidia-smi` says kernels are running, not that they are efficient. Causes: HBM bandwidth saturated, low SM occupancy, small kernels, memory stalls, bad input shapes, CPU-GPU sync.

Tools: Nsight Systems + Nsight Compute + DCGM. Fixes: operator fusion, TensorRT engine, FP16/BF16/FP8, shape stabilization, reduce padding, distillation, larger batches where latency allows, CUDA Graphs.

> "nvidia-smi tells me kernels are running, not whether the GPU is efficient. Nsight Compute tells me if the kernel is compute-bound, memory-bound, or stalled."

---

## 14. Layer-by-Layer Troubleshooting

### 14.1 CPU Problems

Bottlenecks: tokenization, JSON parsing, RAG orchestration, feature assembly, Python GIL, thread contention, CFS scheduling, IRQ storms, TLS/gRPC, serialization, logging.

Symptoms: CPU high / GPU low, high queue wait before GPU, high syscall time, high context switches, one core hot, p99 high but p50 fine.

Tools: htop, pidstat, perf top/record/stat, VTune, strace -c, eBPF.

Fixes: CPU pinning, thread-pool isolation, move tokenization to worker pool, Rust/C++ hot path, reduce JSON (use protobuf/Arrow/shared memory), disable noisy agents, pin IRQs to housekeeping cores, isolate latency-sensitive cores.

> "For CPU bottlenecks, I check whether the GPU is starving, then look at tokenization, serialization, thread contention, context switches, IRQ placement, and NUMA locality. For low-latency services I isolate cores, pin IRQs, separate housekeeping cores, and keep the hot path in Rust/C++."

### 14.2 NUMA & Host Memory

Issues: worker on NUMA 0 but memory on NUMA 1; GPU on one node, CPU memory on another; remote DRAM access; TLB misses; small pages instead of hugepages; page faults; fragmentation; swap.

Symptoms: p99 spikes, CPU busy but unproductive, perf shows dTLB misses, numastat shows remote memory, GPU copy latency rises.

Tools: `numactl --hardware`, `numastat -p <pid>`, `perf stat -e dTLB-load-misses,cache-misses`, VTune memory-access, free, vmstat.

Fixes: `numactl --cpunodebind/--membind`, K8s CPU Manager static + Topology Manager single-numa-node, HugePages, preallocated pools, pinned host memory for GPU transfers, avoid cross-NUMA GPU placement.

> "A GPU workload can look slow because the CPU thread and memory are on the wrong NUMA node. Pinned host memory, NIC, and GPU locality all affect p99."

### 14.3 GPU Bottleneck — Four Buckets

```
1. Compute-bound          SMs/Tensor cores saturated
2. Memory-bandwidth-bound HBM reads/writes dominate
3. Capacity-bound         GPU memory full / OOM / fragmentation
4. Launch/scheduling      many small kernels, CPU gaps
```

| Bucket | Tools | Fixes |
| --- | --- | --- |
| Compute | Nsight Compute | FP16/BF16/FP8, TRT-LLM, better batch, tensor parallel |
| Bandwidth | Nsight Compute, DCGM | quantization, KV quant, FlashAttention/PagedAttention, shorter context |
| Capacity | DCGM, runtime KV | lower max seq len / batch / `max_num_seqs`, sharding, MIG, quantization |
| Launch | Nsight Systems | CUDA Graphs, operator fusion, TRT, larger batches |

> "I don't just ask whether GPU utilization is high. I ask whether we are compute-bound, HBM-bandwidth-bound, memory-capacity-bound, or launch-bound."

### 14.4 Storage

Matters for: model/adapter loading, embedding index load, RAG refresh, checkpoint restore, cold start, rolling deploys, node recovery.

Symptoms: slow pod ready, model load takes minutes, rollout traffic impact, NVMe underutilized, remote download on restart.

Tools: `iostat -x 1`, FIO, iotop, dmesg, startup logs.

Fixes: local NVMe cache, preload via DaemonSet, separate image/model artifacts, lazy adapter load, warm pools, readiness gate after warmup, parallel load, GPUDirect Storage, never download from HF/S3 at startup.

> "Slow model loading is not always disk throughput. It may be remote download, deserialization, CPU staging, GPU transfer, or CUDA Graph capture. I time each stage separately."

### 14.5 Network / RDMA / NCCL

Separate three network classes:

```
Service network   : HTTP/gRPC (gateway, RAG, feature store, model service)
Storage/data net  : object store, vector DB, Redis, Kafka, model download
GPU collective net: NCCL, RDMA, RoCE, InfiniBand, NVLink
```

**Ethernet/TCP** symptoms: p99 spikes, retransmits, TIME_WAIT explosion, packet drops, pool exhaustion, Redis spikes. Tools: `ss -s`, `netstat -s`, `ethtool -S`, `ip -s link`, tcpdump, iperf3. Fixes: connection pooling, larger NIC ring buffers, interrupt coalescing, pin IRQs, SR-IOV, AF_XDP, separate traffic classes.

**RDMA/RoCE/NCCL** symptoms: slow all-reduce, multi-GPU stalls, tail latency during collectives, PFC/ECN misconfig. Tools: nccl-tests (`all_reduce_perf`, `all_gather_perf`), ibstat/ibv_devinfo, rdma link, `nvidia-smi topo -m`, `NCCL_DEBUG=INFO`. Fixes: verify topology, check GPUDirect path, set NCCL env vars, configure PFC/ECN, validate MTU, pin NIC/GPU NUMA, isolate bad node.

> "Before blaming Tensor Parallelism or the model, I run NCCL tests to verify the interconnect baseline. If all-reduce bandwidth is below expected, the bottleneck is fabric/topology, not the LLM runtime."

### 14.6 Kubernetes / Orchestration

Issues: wrong GPU node placement, no NUMA awareness, CPU-limit throttling, noisy-neighbor nodes, cold starts during rollout, autoscaler too slow for GPU nodes, readiness too early, driver/runtime mismatch.

Tools: `kubectl top`, `kubectl describe pod/node`, events, device-plugin logs, DCGM-exporter, Prometheus/Grafana.

Fixes: node pools by GPU type/model size, warm pools, taints/tolerations, pod anti-affinity, Topology Manager, CPU Manager static, readiness after model load + warmup, canary/blue-green, separate online serving from batch/fine-tuning.

> "Kubernetes schedules pods, but the runtime schedules tokens. K8s handles placement, isolation, rollout, and capacity; vLLM/SGLang handles request batching and KV-cache scheduling."

---

## 15. Incident Stories to Memorize

### Story A — P99 spike that was NOT a GPU issue
**Symptom:** hot-path p99 jumped 8ms → 47ms on one node.
**Investigation:** htop showed housekeeping core saturated; ps showed irqbalance + monitoring agents; perf showed high TLB misses; numastat showed remote NUMA memory; VTune confirmed remote DRAM; STREAM showed hardware bandwidth healthy.
**Root cause:** IRQ affinity broken + HugePages missing after reboot + memory allocated cross-NUMA.
**Fix:** disable irqbalance, pin NIC IRQs to housekeeping cores, restore HugePages, bind CPU/memory to correct NUMA node, add boot-time validation.
**Lesson:** A GPU/AI p99 issue can be caused by CPU scheduling, interrupts, TLB misses, or NUMA. Validate the host before blaming the model.

### Story B — LLM decode slow
**Symptom:** vLLM throughput low, TPOT high, `nvidia-smi` looked fine.
**Investigation:** runtime metrics showed high TPOT; Nsight Systems showed kernel launch gaps; logs showed `enforce_eager` (CUDA Graphs off); Nsight Compute showed memory-bound paged attention; KV cache was FP16 not FP8; DCGM showed hardware healthy.
**Fix:** enable CUDA Graphs, FP8 KV cache, prefix caching, chunked prefill; tune `max_num_seqs` / `max_num_batched_tokens`.
**Lesson:** `nvidia-smi` utilization is not enough. Runtime metrics for symptoms, Nsight Systems for timeline gaps, Nsight Compute for kernel bottlenecks.

### Story C — Model loading slow
**Symptom:** pod restart took 5 minutes before ready.
**Investigation:** startup logs timed weight load; FIO showed NVMe healthy; logs showed model downloading remotely on every restart; iostat showed NVMe underutilized after local cache.
**Fix:** pre-cache on local NVMe, verify in init container, readiness only after load + warmup, warm pools, consider GPUDirect Storage.
**Lesson:** model readiness means weights loaded + KV initialized + CUDA Graphs captured + first inference warmed — not just container started.

### Story D — Shared-GPU OOM with BERT/ONNX models
**Symptom:** multiple models on one GPU intermittently OOM.
**Investigation:** tag requests by model/version/tenant/input-length/batch; run each alone and combined; DCGM/Nsight showed memory spike timing; PyTorch profiler showed activation memory + padding/sequence-length issue.
**Fix:** cap max sequence length, dynamic padding, bucket by sequence length, lower max batch for heavy model, FP16/BF16 or TensorRT, separate batching policy, dedicated GPU/MIG, peak-memory canary gate.
**Lesson:** shared-GPU OOM is controlled by *peak memory per model*, not average GPU memory or weight size.

---

## 16. Operations: Rollouts, Capacity, Cost, SLOs

### 16.1 Final Troubleshooting Framework (apply to any question)

```
1. Clarify symptom   : latency, throughput, OOM, error rate, cost, or quality?
2. Localize          : which model/tenant/node/GPU/input shape/version?
3. Split latency     : queue, preprocess, runtime, GPU, copy, network, downstream
4. Apply USE/RED     : service health + infra health
5. Choose profiler   : PyTorch (ops), Nsight Systems (timeline), Nsight Compute (kernels),
                       perf/VTune (CPU), FIO/STREAM/NCCL (baselines)
6. Fix safely        : immediate mitigation → root-cause fix → prevention gate
7. Validate          : p99, throughput, cost, error rate, quality regression
```

### 16.2 Safe Rollouts

| Pattern | Use |
| --- | --- |
| Canary (5–10%) | New model/runtime; compare quality + latency before promotion |
| Blue-green | Atomic swap of model graph pointer; instant rollback |
| Shadow scoring | Run new model in parallel, log-only, zero user impact |
| PDB + rolling | Never drop below SLO replica count during node ops |
| Eval gates | Block promotion on quality regression or perf >2% slip |

### 16.3 Capacity Planning

```
Required replicas = (Peak TPS × Safety Factor) / Instance Capacity@p99

Example (interactive LLM):
  Peak     = 50,000 tokens/s demand
  Safety   = 2.0
  Capacity = measured tokens/s/replica at p99 < SLO
  → size for 2× peak, keep standby pool warm for burst
```

### 16.4 Cost Levers (Capex/Opex)

| Lever | Effect |
| --- | --- |
| Quantization (FP8/INT8/INT4) | More requests per GPU, smaller fleet |
| Continuous batching + prefix cache | Higher useful-compute ratio per GPU-hour |
| Prefill/decode disaggregation | Right-size compute vs memory-bound fleets independently |
| Speculative decoding | Fewer large-model forward passes per token |
| Standby pool vs always-on | Pay for warm capacity only at burst |
| Useful-compute audit | Measure SM cycles on GEMM vs dispatch/stall waste |

### 16.5 SLO Definition

```
Hard production gates:
  TTFT  < 500 ms at peak concurrent batch
  TPOT  maps to HBM-bandwidth roofline of the accelerator
  Error budget : timeout + 5xx + guardrail-failure within monthly budget
  Quality      : eval score no regression vs prior release
```

---

## 17. Responsible AI, Security & Isolation

| Principle | Implementation |
| --- | --- |
| **Policy outside the model** | Input validation, syntax/threat checks in the gateway — never trust the model to self-police |
| **Explicit access authorization** | Per-tenant access control for tools and vector indices; tenants cannot cross-query partitions |
| **Zero-overhead redaction** | Strip PII at the ingest proxy with deterministic rules *before* tokenization |
| **Fail-closed** | If a guardrail or validation loop times out, the transaction fails closed — no unverified generation |
| **Tenant isolation** | SR-IOV VFs, namespaces, network policies, MIG slices; no shared RDMA/KV across tenants |
| **Supply chain** | Cosign-signed images, Kyverno/OPA admission, Vault secrets, audited bootstrap tokens |
| **Air-gap posture** | All images/datasets mirrored through DMZ; no startup egress |

---

## 18. Operational Maturity Model & Quick-Reference Cards

### 18.1 Maturity Model

```
L1 Manual       SSH + scripts + kubeadm join              hours–days
L2 Scripted     Ansible playbooks                         30–60 min
L3 Imaged       Golden images via PXE, automated join     10–20 min
L4 Declarative  Cluster API + Metal3 + GitOps + autoscaler 5–10 min (standby: 1–2 min)
L5 Self-healing Machine Health Checks, auto-replace,       predictive
                predictive capacity growth
```

### 18.2 K8s GPU + RDMA Quick Reference

```
HOST:
  - OFED drivers; modprobe nvidia-peermem
  - IOMMU=PT, ACS disabled; hugepages reserved
KUBELET:
  - cpuManagerPolicy: static
  - topologyManagerPolicy: single-numa-node   ← CRITICAL
  - RDMA device plugin (DaemonSet); Multus CNI
POD:
  - securityContext.capabilities.add: [IPC_LOCK]   ← CRITICAL
  - requests: nvidia.com/gpu, rdma/hca_shared_devices_a, hugepages-2Mi
  - annotation: k8s.v1.cni.cncf.io/networks: ib-network
  - emptyDir /dev/shm sizeLimit: 32Gi              ← CRITICAL
  - mount /dev/infiniband
NCCL:
  NCCL_IB_DISABLE=0; NCCL_IB_HCA=mlx5_0,mlx5_1
  NCCL_NET_GDR_LEVEL=5; NCCL_P2P_LEVEL=NVL
  NCCL_SOCKET_IFNAME=^docker,lo
VERIFY:
  NCCL_DEBUG=INFO shows "NET/IB" (not "NET/Socket")
  all_reduce_perf > 180 GB/s on NDR400
  ibv_devices works inside the pod
```

### 18.3 Common Pitfalls → Fixes

| Symptom | Root cause | Fix |
| --- | --- | --- |
| `ibv_reg_mr failed: Cannot allocate memory` | Missing IPC_LOCK | Add to securityContext capabilities |
| `Using network Socket` in NCCL log | RDMA device not visible | Check device plugin, `/dev/infiniband` mount, OFED in image |
| NCCL hangs on init | `/dev/shm` too small | Increase Memory emptyDir sizeLimit |
| Bandwidth far below expected | No GPUDirect RDMA | Load nvidia-peermem, check GPU-NIC topology |
| Performance varies by run | NUMA misalignment | `topologyManagerPolicy: single-numa-node` |
| Works for 2 nodes, fails at 8+ | Adaptive routing not configured | `NCCL_IB_AR_THRESHOLD`, check switch config |
| Intermittent timeouts | Default timeout too low | `NCCL_IB_TIMEOUT=22` |
| GPU↔GPU through CPU | `NCCL_P2P_LEVEL` too low | `NCCL_P2P_LEVEL=NVL` |
| VFs but no isolation | Shared device plugin | Use SR-IOV device plugin for VF isolation |

### 18.4 The One-Page Mental Model

```
CPU:        tokenization, preprocessing, scheduling, IRQs, NUMA
            tools: htop, perf, VTune, numastat
            fixes: pinning, isolation, parallelism, zero-copy

MEMORY:     TLB, HugePages, remote NUMA, pinned memory
            tools: free, vmstat, perf, numastat
            fixes: HugePages, NUMA bind, memory pools

GPU:        compute, HBM bandwidth, capacity, launch overhead
            tools: DCGM, Nsight Systems, Nsight Compute
            fixes: batching, quantization, CUDA Graphs, TensorRT, KV tuning

STORAGE:    model load, index load, checkpoint, cold start
            tools: iostat, FIO
            fixes: local NVMe, warm pools, readiness gates

NETWORK:    TCP, Redis/RAG, RDMA/NCCL
            tools: ss, ethtool, iperf3, NCCL tests
            fixes: pooling, ring buffers, SR-IOV, RDMA config, topology

RUNTIME:    TTFT, TPOT, KV cache, batching, prefill/decode
            tools: vLLM/SGLang/Triton metrics, traces
            fixes: chunked prefill, prefix cache, quantization, routing

K8S:        placement, scaling, rollout, isolation
            tools: kubectl, events, DCGM, Prometheus
            fixes: node pools, warm pods, affinity, canary, topology manager
```

> **The posture to project:** "I know how to localize the bottleneck, pick the right tool, prove the cause, make a safe mitigation, and then add a prevention gate so it does not recur — across the whole stack from the serving API down to PCIe ACS bits and IRQ affinity, in both cloud and air-gapped environments."

---

## Appendix — Cross-Reference Map

| Topic | Deeper reference in this workspace |
| --- | --- |
| Inference engine internals (vLLM/TRT-LLM/Triton/ONNX) | [docs-1/Inference-Deep-Dive.md](../docs-1/Inference-Deep-Dive.md) |
| CUDA / Nsight / kernel profiling | [docs-1/CUDA-Deep-Dive.md](../docs-1/CUDA-Deep-Dive.md) |
| Layered architecture (OS→Network→Platform→GPU→Inference) | [docs-1/0-Layer-Architecture.md](../docs-1/0-Layer-Architecture.md) |
| LLM Ops & governance | [docs-1/8-LLM-Ops-Governance.md](../docs-1/8-LLM-Ops-Governance.md) |
| K8s deployment topologies | [docs/k8s_deployment_topologies_deep_dive.md](k8s_deployment_topologies_deep_dive.md) |
| Multi-region on-prem runbook | [docs/k8s_multiregion_onprem_deployment_runbook.md](k8s_multiregion_onprem_deployment_runbook.md) |
| Troubleshooting crash course | [docs/troubleshooting_crash_course.md](troubleshooting_crash_course.md) |
| Master syllabus | [docs/MASTER_SYLLABUS.md](MASTER_SYLLABUS.md) |
