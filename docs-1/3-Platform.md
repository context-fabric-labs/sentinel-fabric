# Layer 3: Platform & Orchestration — Complete Deep Dive

## "How do we deploy, isolate, observe, and manage workloads?"

> **Why this layer matters:** Layer 1 is the FOUNDATION. If you deploy
> a perfectly optimized GPU inference service on the wrong NUMA node,
> with shared CPUs, no hugepages, and no observability, it will perform
> poorly and you won't know why. Layer 1 ensures workloads get the
> right resources, are isolated from noise, and are observable.
>
> **The core insight:** For HPC/AI workloads on Kubernetes, the platform
> layer must be configured MUCH more carefully than for typical web
> services. Default K8s settings (no CPU pinning, no NUMA awareness,
> no hugepages, default networking) are designed for general-purpose
> workloads, not for microsecond-sensitive GPU inference.

---

# SECTION 1: KUBERNETES FOR HPC/AI WORKLOADS

---

## 1.1 The Three Critical Kubelet Settings

### CPU Manager (static policy)

```yaml
cpuManagerPolicy: static
```

When set to `static`, Guaranteed QoS pods with INTEGER CPU requests get
EXCLUSIVE CPU cores. The kubelet pins those containers to specific CPUs
using the cpuset cgroup controller.

**Without static policy:** your inference pod shares CPUs with kubelet,
other pods, system daemons → CFS cliff, context switch storms.

**With static policy:** your pod gets dedicated cores that nothing else
can use → deterministic, isolated execution.

### Topology Manager (single-numa-node)

```yaml
topologyManagerPolicy: single-numa-node
```

Ensures that CPU, memory, and devices (GPUs) allocated to a pod all
come from the SAME NUMA node. If alignment is impossible, the pod
is REJECTED (not silently misplaced).

**Without topology manager:** your pod might get CPUs from node 0 and
GPU from node 1 → every GPU data transfer crosses NUMA → 2-3x slower.

**With single-numa-node:** everything is co-located → local memory,
local GPU, local NIC.

### Reserved System CPUs

```yaml
reservedSystemCPUs: "0-3"
```

Reserves CPUs 0-3 for kubelet, system daemons, and OS housekeeping.
Workload pods CANNOT use these CPUs.

**Why this matters:** without reservation, kubelet itself competes with
your inference pods for CPU time.

---

## 1.2 QoS Classes and Why They Matter

Kubernetes assigns every pod a QoS class based on resource requests/limits:

### Guaranteed (what you want for HPC)

```yaml
resources:
  requests:
    cpu: "8"         # integer! triggers CPU pinning
    memory: "16Gi"
  limits:
    cpu: "8"         # must equal request
    memory: "16Gi"   # must equal request
```

- requests = limits for ALL containers
- Gets EXCLUSIVE CPU cores (with static CPU Manager)
- Last to be evicted under memory pressure
- Can use hugepages and GPU device plugins

### Burstable (avoid for latency-sensitive)

```yaml
resources:
  requests:
    cpu: "4"
  limits:
    cpu: "8"    # limit > request = Burstable
```

- Gets CPU time proportional to request, can burst to limit
- NO exclusive CPU pinning
- Evicted before BestEffort under pressure

### BestEffort (never for HPC)

```yaml
resources: {}    # no requests or limits = BestEffort
```

- Gets whatever is left over
- First to be evicted
- No resource guarantees at all

### The rule for HPC

**ALWAYS use Guaranteed QoS with integer CPU requests** for
latency-sensitive inference workloads.

---

## 1.3 GPU Scheduling

### NVIDIA Device Plugin

```yaml
resources:
  requests:
    nvidia.com/gpu: "1"
  limits:
    nvidia.com/gpu: "1"
```

The NVIDIA device plugin advertises GPU resources to the kubelet.
When a pod requests a GPU, the plugin assigns a specific GPU device
and sets CUDA_VISIBLE_DEVICES.

### MIG (Multi-Instance GPU)

Splits one physical GPU into multiple isolated instances:

```
A100 80GB → 7 × MIG 1g.10gb (each with its own memory and SMs)
```

Useful when multiple small models can share one GPU.

### GPU Operator

NVIDIA GPU Operator automates:

- GPU driver installation
- Container toolkit
- Device plugin
- DCGM monitoring
- MIG configuration

---

## 1.4 Volumes for HPC

### Hugepages

```yaml
volumes:
- name: hugepages
  emptyDir:
    medium: HugePages-2Mi

containers:
- resources:
    requests:
      hugepages-2Mi: "512Mi"
    limits:
      hugepages-2Mi: "512Mi"
  volumeMounts:
  - name: hugepages
    mountPath: /hugepages
```

Host must pre-allocate hugepages (boot param or sysctl).
K8s can schedule hugepage requests but cannot create them.

### Shared Memory (expanded /dev/shm)

```yaml
volumes:
- name: dshm
  emptyDir:
    medium: Memory
    sizeLimit: "16Gi"

containers:
- volumeMounts:
  - name: dshm
    mountPath: /dev/shm
```

Default /dev/shm is only 64 MB. For shared memory IPC between
containers in the same pod, you need to expand this.

### Model Storage

```yaml
volumes:
- name: models
  hostPath:
    path: /data/models
  # or PersistentVolumeClaim for networked storage
```

---

## 1.5 Scheduling Controls

### Node Affinity (which NODE)

```yaml
affinity:
  nodeAffinity:
    requiredDuringSchedulingIgnoredDuringExecution:
      nodeSelectorTerms:
      - matchExpressions:
        - key: node-role/gpu-inference
          operator: In
          values: ["true"]
```

Ensures pods land on nodes with the right hardware/labels.

### Pod Anti-Affinity (spread replicas)

```yaml
affinity:
  podAntiAffinity:
    requiredDuringSchedulingIgnoredDuringExecution:
    - labelSelector:
        matchLabels:
          app: fraud-scoring
      topologyKey: kubernetes.io/hostname
```

Ensures replicas land on DIFFERENT nodes for HA.

### PriorityClass (which pods survive under pressure)

```yaml
apiVersion: scheduling.k8s.io/v1
kind: PriorityClass
metadata:
  name: inference-critical
value: 1000000
preemptionPolicy: PreemptLowerPriority
```

Higher value = higher priority. When resources are scarce,
lower-priority pods are preempted to make room.

### PodDisruptionBudget (safe maintenance)

```yaml
apiVersion: policy/v1
kind: PodDisruptionBudget
metadata:
  name: fraud-scoring-pdb
spec:
  maxUnavailable: 1
  selector:
    matchLabels:
      app: fraud-scoring
```

During node drains/upgrades, at most 1 pod can be disrupted at a time.
Prevents simultaneous eviction of all replicas.

---

# SECTION 2: HOST-LEVEL CONFIGURATION (BOOT PARAMS)

---

## 2.1 The Four Boot Parameters for CPU Isolation

```bash
GRUB_CMDLINE_LINUX="isolcpus=4-31 nohz_full=4-31 rcu_nocbs=4-31 irqaffinity=0-3"
```

| Parameter           | What it does                      | Why needed                          |
| ------------------- | --------------------------------- | ----------------------------------- |
| `isolcpus=4-31`   | Remove CPUs from scheduler domain | No random tasks on your CPUs        |
| `nohz_full=4-31`  | Disable timer tick (single task)  | No 1 ms jitter from scheduler tick  |
| `rcu_nocbs=4-31`  | Offload RCU callbacks             | No kernel housekeeping on your CPUs |
| `irqaffinity=0-3` | Route interrupts to CPUs 0-3      | No NIC/disk interrupts on your CPUs |

### How boot params interact with K8s

- Boot params remove KERNEL NOISE from CPUs
- K8s CPU Manager ASSIGNS pods to those quiet CPUs
- Together: your pod gets dedicated, quiet, isolated cores

### Hugepage reservation

```bash
GRUB_CMDLINE_LINUX="... hugepagesz=2M hugepages=1024"
```

Pre-allocates 1024 × 2 MB = 2 GB of hugepages at boot.
K8s pods can then request these via the hugepage volume.

---

# SECTION 3: CGROUPS v2

---

## 3.1 What cgroups are

Control groups organize processes into hierarchical groups and apply
resource controls: CPU, memory, I/O, PIDs, cpuset.

### The key controllers for HPC

**cpuset controller:**

```
cpuset.cpus = 4-11    → this cgroup can only use CPUs 4-11
cpuset.mems = 0       → this cgroup can only allocate on NUMA node 0
```

K8s CPU Manager uses this automatically for Guaranteed pods.

**cpu controller:**
CPU time limits and shares.

**memory controller:**
Memory limits and OOM behavior.

**pids controller:**
Limits process/thread count (prevents fork bombs).

### How K8s uses cgroups

When you create a Guaranteed pod with `cpu: "8"`, the kubelet:

1. Creates a cgroup for the pod
2. Sets `cpuset.cpus` to 8 specific CPUs
3. Sets `cpuset.mems` to the correct NUMA node (with topology manager)
4. Writes the container PID to `cgroup.procs`

You do NOT need to manage cgroups manually in K8s.

---

# SECTION 4: OBSERVABILITY WITH eBPF

---

## 4.1 The Three-Layer eBPF Stack

### Layer 1: Cilium Hubble (network)

- TCP flows, DNS, HTTP, packet drops
- Service dependency maps
- No application changes needed

### Layer 2: OTel eBPF Agent / Beyla (application)

- HTTP/gRPC distributed traces
- RED metrics (Rate, Errors, Duration)
- Database query latency
- Works on C++, Rust, Go, Python without code changes

### Layer 3: BCC/bpftrace (deep debugging)

- Scheduler latency (runqlat, runqslower)
- Block I/O latency (biolatency)
- TCP retransmits (tcpretrans)
- Off-CPU analysis (offcputime)

### Deployment model

```
ALL deployed as DaemonSets (one per node):
  → Zero sidecars per pod
  → Zero application code changes
  → Automatic discovery of all pods on the node
  → ~0.2% node CPU overhead (vs ~14% for sidecar mesh)
```

---

## 4.2 The Correlation Dashboard

The most powerful use of eBPF: correlate kernel-level signals with
application and GPU metrics:

```
Panel 1: p99 latency (from OTel eBPF traces)
Panel 2: TCP retransmits (from Hubble)
Panel 3: GPU utilization (from DCGM/NVML)
Panel 4: Run queue latency (from BCC runqlat)
Panel 5: Feature store latency (from OTel eBPF)

Correlation: "p99 spike at 14:23 caused by TCP retransmits at 14:22
caused by GPU memory pressure at 14:20 causing Redis on same node
to slow down"

Time to root cause: ~60 seconds (vs hours without eBPF)
```

---

# SECTION 5: DEPLOYMENT MODEL CHOICES

## Bare Metal + K8s (recommended for HPC)

```
Pros: best performance, full hardware access, GPU direct
Cons: more infra management, no cloud elasticity
Best for: latency-sensitive inference, GPU training
```

## VM + K8s (enterprise standard)

```
Pros: familiar, cloud-portable, strong isolation
Cons: ~10-15% overhead, NUMA/GPU passthrough complexity
Best for: general ML serving, when cloud portability matters
```

## Bare Metal, no K8s (dedicated appliance)

```
Pros: absolute lowest overhead, full control
Cons: no automated deployment/scaling/healing
Best for: DPDK packet processing, matching engines
```

---

# SECTION 6: THE COMPLETE PLATFORM CHECKLIST

```
HOST LEVEL (infra team, once per node):
  □ Boot params: isolcpus, nohz_full, rcu_nocbs, irqaffinity
  □ Hugepage reservation
  □ NVIDIA GPU driver + container toolkit
  □ NIC tuning (ring buffers, coalescing, IRQ affinity)
  □ TCP tuning (sysctls)

KUBELET LEVEL (platform team, per cluster):
  □ CPU Manager: static
  □ Topology Manager: single-numa-node
  □ Reserved System CPUs: 0-3
  □ GPU Operator installed

POD LEVEL (app team, per workload):
  □ Guaranteed QoS (integer CPUs, requests=limits)
  □ GPU request (nvidia.com/gpu: "1")
  □ Hugepage volume + request
  □ Shared memory volume (expanded /dev/shm)
  □ Node affinity (correct node labels)
  □ Pod anti-affinity (spread replicas)
  □ PriorityClass (inference-critical)
  □ PDB (maxUnavailable: 1)
  □ Startup/liveness/readiness probes

OBSERVABILITY (monitoring team):
  □ Cilium Hubble (network flows)
  □ OTel eBPF agent (application traces)
  □ DCGM exporter (GPU metrics)
  □ Prometheus + Grafana (dashboards)
  □ Correlation dashboard (p99 ↔ retransmits ↔ GPU ↔ scheduler)
```

---

# SECTION 7: INTERVIEW CHEAT SHEET

## "How do you configure K8s for GPU inference?"

→ CPU Manager static + Topology Manager single-numa-node +
   Guaranteed QoS with integer CPUs + GPU device plugin +
   hugepage volumes + node affinity to GPU nodes.

## "What is the difference between isolcpus and CPU Manager?"

→ isolcpus removes KERNEL NOISE from CPUs (boot param, system-wide).
   CPU Manager ASSIGNS pods to CPUs (kubelet, per-pod). You need BOTH:
   isolcpus makes CPUs quiet, CPU Manager places your pod there.

## "How do you observe K8s workloads without sidecars?"

→ eBPF DaemonSets: Cilium Hubble for network, OTel eBPF agent for
   application traces, BCC tools for deep debugging. Zero application
   code changes, ~0.2% node CPU overhead.

## "How do you handle GPU node maintenance?"

→ PDB with maxUnavailable: 1 ensures at most one replica is disrupted.
   PriorityClass ensures inference pods survive resource pressure.
   Pod anti-affinity ensures replicas are on different nodes.
