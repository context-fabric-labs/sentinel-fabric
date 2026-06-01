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

---
---

# SECTION 8: NUMA DEEP DIVE (The #1 Silent Performance Killer)

---

## 8.1 What NUMA Actually Is

```
Non-Uniform Memory Access: different CPUs have different "distances"
to different memory banks.

Dual-Socket Server (typical GPU node):

  Socket 0                              Socket 1
  ┌──────────────────────┐              ┌──────────────────────┐
  │ CPU cores 0-31       │              │ CPU cores 32-63      │
  │ L3 Cache (shared)    │              │ L3 Cache (shared)    │
  │                      │   UPI/QPI    │                      │
  │ Memory Controller ◄──┼──────────────┼──► Memory Controller │
  │        │             │   ~100ns     │        │             │
  │    DDR5 Bank 0       │  penalty     │    DDR5 Bank 1       │
  │    (256 GB)          │              │    (256 GB)          │
  │                      │              │                      │
  │  PCIe Root Complex   │              │  PCIe Root Complex   │
  │   │         │        │              │   │         │        │
  │  GPU 0    NIC 0      │              │  GPU 1    NIC 1      │
  └──────────────────────┘              └──────────────────────┘
  
  NUMA Node 0                           NUMA Node 1

Access latency:
  CPU 0 → Memory Bank 0 (local):   ~80 ns
  CPU 0 → Memory Bank 1 (remote):  ~180 ns (2.25× slower!)
  CPU 0 → GPU 0 (local PCIe):      direct, low latency
  CPU 0 → GPU 1 (remote PCIe):     crosses UPI, higher latency
```

## 8.2 NUMA Diagnostic Commands

```bash
# Show NUMA topology
numactl --hardware
# node distances:
# node   0   1
#   0:  10  21
#   1:  21  10
# Distance 21 vs 10 = 2.1× slower for remote access

# Visual topology (install hwloc)
lstopo --of png > topology.png
# Shows: cores, caches, memory, PCIe devices, GPUs per NUMA node

# Check which NUMA node a GPU is on
cat /sys/bus/pci/devices/0000:41:00.0/numa_node
# Output: 0 → GPU is on NUMA node 0

# Check which NUMA node a NIC is on
cat /sys/class/net/eth0/device/numa_node
# Output: 1 → NIC is on NUMA node 1

# Show per-NUMA memory stats
numastat -c
#                  Node 0    Node 1
# numa_hit       18324567  17654321
# numa_miss         12345    456789  ← BAD! Remote accesses happening
# numa_foreign     456789     12345

# Per-process NUMA allocation
numastat -p $(pidof my_inference_server)
```

## 8.3 NUMA Memory Policies

```bash
# Policy types:
# - default:     allocate on node where thread runs (can be anywhere)
# - bind:        ONLY allocate on specified nodes (fail if full)
# - preferred:   prefer specified node, fall back to others
# - interleave:  round-robin across nodes (best for init-once data)

# Run process pinned to NUMA node 0 (CPUs + memory):
numactl --cpunodebind=0 --membind=0 ./my_inference_server

# Interleave for hash tables / lookup data (accessed randomly):
numactl --interleave=all ./load_feature_store

# Check what policy is in effect:
cat /proc/$(pidof my_server)/numa_maps | head -20
# Shows per-page allocation: N0=1234 N1=5 (mostly on node 0, good)
```

## 8.4 NUMA + K8s Topology Manager

```
Without Topology Manager (single-numa-node):
  K8s might assign:
    CPUs 0-7 (NUMA 0) + GPU 1 (NUMA 1) + NIC 1 (NUMA 1)
    → CPU accesses GPU memory crossing UPI: +100 ns per access
    → DMA from NIC to GPU crosses socket: extra hop

With Topology Manager (single-numa-node):
  K8s ensures:
    CPUs 0-7 (NUMA 0) + GPU 0 (NUMA 0) + NIC 0 (NUMA 0)
    → All local: lowest latency, highest bandwidth

  If alignment impossible: pod FAILS to schedule (Topology Affinity Error)
  → Better to fail than to silently run 2× slower!
```

## 8.5 NUMA Gotchas

```
1. First-touch policy (default Linux behavior):
   Memory is allocated on the NUMA node where it's FIRST ACCESSED.
   If thread 0 on node 0 initializes data, then thread 32 on node 1
   uses it → all accesses are remote!
   
   Fix: initialize data with the thread that will use it, or use
   numa_alloc_onnode() / numactl --membind.

2. Memory migration:
   The kernel can migrate pages between NUMA nodes (AutoNUMA/NUMA balancing).
   For HPC: DISABLE this (unpredictable latency spikes):
   echo 0 > /proc/sys/kernel/numa_balancing

3. Huge page allocation:
   Hugepages allocated at boot may not be evenly distributed across NUMA nodes.
   Explicit per-node allocation:
   echo 512 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
   echo 512 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

4. PCIe topology matters:
   GPU and NIC should be on the SAME PCIe root complex (same NUMA node).
   If not: GPUDirect RDMA goes through CPU/UPI → defeats the purpose.
```

---

# SECTION 9: CPU SCHEDULING & LATENCY (Beyond isolcpus)

---

## 9.1 CFS Bandwidth Throttling — The Silent Killer

### What happens with CPU limits in K8s

```yaml
resources:
  requests:
    cpu: "4"
  limits:
    cpu: "4"    # This creates CFS bandwidth throttling!
```

Even with `requests == limits` (Guaranteed), the CFS bandwidth controller
enforces that the container doesn't exceed its limit within each
**CFS period (default 100 ms)**.

```
CFS Throttling explained:

Period: 100 ms
Quota: 400 ms (= 4 CPUs worth of time per period)

If your 8-thread burst uses all quota in 60 ms:
  → Throttled for remaining 40 ms of the period!
  → ALL threads in the cgroup are FROZEN
  → Even if the CPUs are completely idle

This shows as: /sys/fs/cgroup/.../cpu.stat
  nr_throttled: 12345   ← times throttled
  throttled_time: 5000000000  ← ns spent throttled (5 seconds total!)
```

### How to detect CFS throttling
```bash
# In the container or from host (cgroup path):
cat /sys/fs/cgroup/cpu,cpuacct/kubepods/pod<uid>/.../cpu.stat
# nr_periods: 100000
# nr_throttled: 5000     ← 5% of periods are throttled!
# throttled_time: 2500000000  ← 2.5 seconds total

# Prometheus metric:
# container_cpu_cfs_throttled_periods_total
# container_cpu_cfs_throttled_seconds_total
```

### Solutions
```
Option 1: Remove CPU limits entirely (Burstable QoS)
  pros: no throttling ever
  cons: loses Guaranteed QoS, no CPU pinning, noisy neighbor

Option 2: Static CPU Manager (BEST for HPC)
  - With static policy + integer CPUs + requests=limits
  - The CFS bandwidth controller is BYPASSED for pinned CPUs
  - No throttling possible because you OWN the cores
  - This is why CPU Manager static is CRITICAL for HPC

Option 3: Increase CFS period
  kubelet: --cpu-cfs-quota-period=10ms
  - Shorter period = throttle more often but for less time
  - Reduces tail latency spikes from throttling
```

## 9.2 Scheduling Priorities and Real-Time

```
Linux scheduler classes (highest to lowest priority):
  SCHED_DEADLINE    → Earliest-Deadline-First (real-time)
  SCHED_FIFO        → First-In-First-Out real-time (fixed priority)
  SCHED_RR          → Round-Robin real-time (time-sliced FIFO)
  SCHED_OTHER (CFS) → Completely Fair Scheduler (normal Linux tasks)
  SCHED_BATCH       → CFS but lower priority
  SCHED_IDLE        → only runs when nothing else wants CPU

For HPC inference:
  - Your inference threads: SCHED_FIFO priority 50 (never preempted by CFS)
  - System daemons: SCHED_OTHER (normal)
  - Result: inference thread ALWAYS runs immediately when ready

  How to set:
  chrt -f 50 ./inference_server
  OR: in container with CAP_SYS_NICE capability
```

## 9.3 CPU Frequency Governor

```bash
# Check current governor:
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
# powersave    ← DEFAULT on many clouds! BAD for HPC!
# performance  ← what you want

# Set performance governor (all CPUs):
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
  echo performance > "$cpu"
done

# OR boot parameter:
intel_pstate=disable cpufreq.default_governor=performance

# Impact:
#   powersave: CPU starts at lowest frequency, ramps up on load
#   Ramp-up takes 5-50 ms! Your first inference request is SLOW.
#   
#   performance: CPU always at max frequency
#   Every request gets full speed immediately
#   Cost: ~20-40W more power per CPU (acceptable for HPC)
```

## 9.4 C-States (CPU Idle States)

```bash
# C-states are CPU power-saving idle states:
#   C0: active (running code)
#   C1: halt (wake in ~1 µs)
#   C2: stop-clock (wake in ~50 µs)
#   C3: sleep (wake in ~100-200 µs)
#   C6: deep sleep (wake in ~500 µs - 2 ms!)

# For HPC: limit to C1 maximum
# Boot param:
intel_idle.max_cstate=1 processor.max_cstate=1

# OR per-CPU at runtime:
for cpu in /sys/devices/system/cpu/cpu*/cpuidle/state*/disable; do
  echo 1 > "$cpu"  # disable all states except C0/C1
done

# Impact:
#   With deep C-states: first request after idle wakes CPU = 200+ µs spike
#   With C1 max: wake time ~1 µs = predictable tail latency
#   Cost: ~5-15W more power per CPU (worth it for latency-sensitive)
```

## 9.5 Run Queue Latency — Measuring Scheduler Delay

```bash
# How long a thread waits AFTER becoming runnable BEFORE getting CPU:
# This is the KEY metric for CPU scheduling quality.

# Using BCC tool:
runqlat -m   # histogram in milliseconds
# Should see: 99.9% of waits < 50 µs for isolated cores
# If you see ms-level waits → CPU contention!

# Per-process:
runqslower 100   # Print any wait > 100 µs
# Shows: PID, COMM, wait_µs

# Prometheus (from eBPF exporter):
# scheduler_run_queue_latency_microseconds{quantile="0.99"}
# Target: < 50 µs for HPC workloads
```

---

# SECTION 10: MEMORY MANAGEMENT DEEP DIVE

---

## 10.1 OOM Killer Behavior in K8s

```
K8s memory limits → cgroup memory.max → kernel OOM killer

When a container exceeds memory.max:
  1. Kernel tries to reclaim (page cache, swap if enabled)
  2. If reclaim fails → OOM killer invokes
  3. OOM killer kills a process in the cgroup (not necessarily YOURS)
  4. K8s sees container exit with code 137 (SIGKILL)
  5. Container restarts (if restartPolicy allows)

The problem for inference:
  - Model weights loaded into memory
  - Under load, KV cache + activations grow
  - Suddenly OOM → killed → cold start (reload model) → 30-60 seconds down!
```

### Preventing OOM
```yaml
# Set memory limit 20-30% above actual peak usage:
resources:
  requests:
    memory: "32Gi"   # Actual model + baseline
  limits:
    memory: "40Gi"   # Headroom for burst / KV cache growth

# Monitor memory pressure BEFORE OOM:
# /sys/fs/cgroup/.../memory.pressure
# some avg10=5.00    ← 5% of time spent in memory reclaim (WARNING)
# full avg10=0.50    ← 0.5% of time ALL tasks stalled on memory (CRITICAL)
```

## 10.2 Transparent Huge Pages (THP) — Help or Hurt?

```
THP: kernel automatically promotes 4 KB pages to 2 MB pages.

HELPS:
  - Fewer TLB misses (2 MB page covers 512× more memory per TLB entry)
  - Large sequential allocations (model weight loading)

HURTS:
  - THP compaction causes unpredictable latency spikes (10-100 ms!)
  - The khugepaged kernel thread runs in background, compacting pages
  - If memory is fragmented, compaction stalls allocations

For HPC inference:
  DISABLE THP (use explicit hugepages instead):
  echo never > /sys/kernel/mm/transparent_hugepage/enabled
  echo never > /sys/kernel/mm/transparent_hugepage/defrag

  Use EXPLICIT hugepages (pre-allocated at boot, no compaction):
  - Boot param: hugepagesz=2M hugepages=4096
  - Application uses mmap with MAP_HUGETLB
  - No compaction, no defrag, deterministic allocation
```

## 10.3 Page Cache and Memory Pressure

```
Linux uses free memory as page cache (file system cache).
When a pod needs memory, kernel evicts page cache first.

The problem:
  Model loading reads 14 GB from disk → fills 14 GB of page cache
  If another process also loads a model → page cache eviction →
  First model needs to re-read from disk (cold cache) → SLOW

Solutions:
  1. Pin model in memory: mlock() / mlockall()
     - Prevents page-out of model weights
     - Requires CAP_IPC_LOCK capability in container
  
  2. tmpfs / ramdisk for model files:
     - Mount models from tmpfs (never hits disk)
     - No page cache eviction concern
  
  3. Memory-mapped files with MAP_POPULATE:
     - Pre-fault all pages at load time
     - Combined with mlock: guaranteed hot
```

## 10.4 Swap — NEVER for HPC

```bash
# Verify swap is disabled:
free -h | grep Swap
# Swap:          0B        0B        0B  ← GOOD

# If swap is enabled, disable it:
swapoff -a

# Why: swap adds 1-10 ms latency per page fault
# For inference: if model weights get swapped out, you're dead
# K8s default: swap disabled (required for kubelet)
# K8s 1.28+: swap can be enabled but should NOT be for HPC pods
```

---

# SECTION 11: CONTAINER RUNTIME INTERNALS

---

## 11.1 Container Creation Path

```
kubectl apply (pod spec)
    ↓
API Server → etcd (store spec)
    ↓
Scheduler → select node (considering affinity, resources, topology)
    ↓
Kubelet (on selected node):
    ↓
  1. Topology Manager: compute NUMA-aligned resource assignment
  2. CPU Manager: select specific CPUs from the assigned NUMA node
  3. Device Manager: assign GPU device
  4. CRI call → containerd
    ↓
containerd:
  5. Pull image (if not cached)
  6. Prepare snapshot (overlayfs layers)
  7. Create OCI runtime spec (config.json):
     - cgroup settings (cpuset, memory limits)
     - namespace settings (mount, PID, network, user)
     - seccomp filter
     - capabilities
    ↓
  8. OCI call → runc
    ↓
runc:
  9. Create namespaces (clone syscall with CLONE_NEWNS, CLONE_NEWPID, etc.)
  10. Set up cgroups (write to /sys/fs/cgroup/...)
  11. Configure seccomp BPF filter
  12. Set up rootfs (pivot_root)
  13. Drop capabilities
  14. exec the container entrypoint process
    ↓
Container is RUNNING
```

## 11.2 Namespace Isolation

```
Linux namespaces provide the ISOLATION that makes containers:

| Namespace | What it isolates | HPC relevance |
|---|---|---|
| mount (mnt) | Filesystem view | Model volume mounts |
| PID | Process IDs | Process isolation |
| network (net) | Network stack, interfaces | Pod networking |
| UTS | Hostname | Container identity |
| IPC | Shared memory, semaphores | /dev/shm isolation |
| user | UID/GID mapping | Rootless containers |
| cgroup | Cgroup view | Resource isolation |

Key HPC consideration:
  Shared memory (IPC namespace):
  - Containers in the SAME pod share IPC namespace
  - Can use /dev/shm for zero-copy data sharing
  - Containers in DIFFERENT pods are IPC-isolated
```

## 11.3 Security Contexts for HPC Pods

```yaml
# Minimal security context for GPU inference pod:
securityContext:
  runAsNonRoot: true
  runAsUser: 1000
  capabilities:
    drop: ["ALL"]
    add:
      - IPC_LOCK       # Required for: RDMA memory registration, mlock
      - SYS_NICE       # Required for: SCHED_FIFO, CPU affinity
      # - SYS_PTRACE   # Only for: nsight profiling (remove in prod)
      # - NET_ADMIN    # Only for: DPDK/AF_XDP (remove if not needed)
  seccompProfile:
    type: RuntimeDefault

# WHY each capability:
# IPC_LOCK: ibv_reg_mr() needs to pin memory. mlock() pins model weights.
# SYS_NICE: chrt/sched_setscheduler for real-time priority inference threads.
```

---

# SECTION 12: KUBELET EVICTION & RESOURCE PRESSURE

---

## 12.1 Eviction Signals and Thresholds

```yaml
# kubelet eviction configuration:
evictionHard:
  memory.available: "500Mi"        # hard evict if < 500 MB free
  nodefs.available: "10%"          # hard evict if disk < 10%
  imagefs.available: "15%"         # hard evict if image disk < 15%

evictionSoft:
  memory.available: "1Gi"          # soft evict if < 1 GB free
evictionSoftGracePeriod:
  memory.available: "30s"          # wait 30s before soft eviction
```

### Eviction order:
```
1. BestEffort pods (no requests/limits) — evicted FIRST
2. Burstable pods exceeding their request — evicted NEXT
3. Guaranteed pods — evicted LAST (only if node is truly exhausted)

Within each class, eviction priority considers:
  - Pod priority (PriorityClass value)
  - Memory usage relative to request
  - Time since creation (newer evicted first)
```

### Why this matters for GPU pods
```
GPU model loading: 14 GB into memory
Other pods on node also consuming memory
Node reaches memory.available < 500 Mi

If your inference pod is Burstable → MAY BE EVICTED
  → Cold start: 30-60 seconds to reload model
  → Traffic hits remaining replicas → cascading pressure

If your inference pod is Guaranteed with high PriorityClass → SAFE
  → Other pods evicted first
  → Your pod survives
```

## 12.2 Node Pressure and Taint-Based Eviction

```
When node is under pressure, kubelet sets taints:
  node.kubernetes.io/memory-pressure:NoSchedule
  node.kubernetes.io/disk-pressure:NoSchedule
  node.kubernetes.io/pid-pressure:NoSchedule

New pods won't schedule to this node.
Existing pods with tolerations can stay.

For GPU nodes:
  Add tolerations to your inference pods to survive pressure taints.
  Combined with high PriorityClass = maximum survivability.
```

---

# SECTION 13: AUTOSCALING FOR GPU INFERENCE

---

## 13.1 Horizontal Pod Autoscaler (HPA) for Inference

```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: inference-hpa
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: inference-server
  minReplicas: 3
  maxReplicas: 20
  behavior:
    scaleUp:
      stabilizationWindowSeconds: 30    # React quickly
      policies:
      - type: Pods
        value: 4
        periodSeconds: 30               # Add up to 4 pods per 30s
    scaleDown:
      stabilizationWindowSeconds: 300   # Wait 5 min before scaling down
      policies:
      - type: Pods
        value: 1
        periodSeconds: 60               # Remove 1 pod per minute
  metrics:
  - type: Pods
    pods:
      metric:
        name: gpu_inference_queue_depth  # Custom metric from DCGM/app
      target:
        type: AverageValue
        averageValue: "5"               # Scale when queue > 5
  - type: Pods
    pods:
      metric:
        name: inference_p99_latency_ms
      target:
        type: AverageValue
        averageValue: "25"              # Scale when p99 > 25 ms
```

### The GPU HPA problem
```
GPU pods take 30-60 seconds to start (pull image + load model).
Standard CPU metrics (utilization) don't reflect GPU queue depth.

Better metrics for GPU HPA:
  - inference queue depth (how many requests waiting)
  - p99 latency (are we meeting SLA?)
  - GPU memory utilization (are we running out of KV cache?)
  - batch fill rate (are batches full? if not, don't need more replicas)

Scale-down must be SLOW:
  - Each pod has a warm model in GPU memory
  - Evicting a pod = losing that warm capacity
  - If traffic spikes again, cold start penalty
  - stabilizationWindowSeconds: 300+ for GPU workloads
```

## 13.2 Cluster Autoscaler for GPU Nodes

```yaml
# GPU node pool auto-scaling:
# Cloud provider-specific (GKE example):
nodePool:
  name: gpu-inference-pool
  autoscaling:
    enabled: true
    minNodeCount: 2
    maxNodeCount: 16
  config:
    accelerators:
    - acceleratorType: nvidia-h100-80gb
      acceleratorCount: 8

# Key considerations:
# - GPU node provisioning: 3-10 minutes (much slower than CPU nodes)
# - Must account for model download time on new node
# - Use DaemonSets to pre-pull model images
# - Consider over-provisioning 1-2 warm spare nodes
```

## 13.3 Karpenter (Better for GPU Node Scaling)

```yaml
# Karpenter NodePool for GPU (more flexible than cluster-autoscaler):
apiVersion: karpenter.sh/v1beta1
kind: NodePool
metadata:
  name: gpu-inference
spec:
  template:
    spec:
      requirements:
      - key: node.kubernetes.io/instance-type
        operator: In
        values: ["p5.48xlarge", "p4d.24xlarge"]  # H100 or A100
      - key: karpenter.sh/capacity-type
        operator: In
        values: ["on-demand"]  # No spot for inference (must be reliable)
      nodeClassRef:
        name: gpu-node-class
  limits:
    nvidia.com/gpu: "128"  # Max 128 GPUs in this pool
  disruption:
    consolidationPolicy: WhenEmpty  # Only remove truly empty nodes
    expireAfter: 720h              # Replace nodes every 30 days (driver updates)
```

---

# SECTION 14: STORAGE FOR AI/HPC WORKLOADS

---

## 14.1 Storage Performance Hierarchy

```
FASTEST                                              SLOWEST
  ←────────────────────────────────────────────────────→

GPU HBM    tmpfs     Local NVMe   NVMe-oF/RDMA   EBS gp3    S3/GCS
3.35 TB/s  50 GB/s   7 GB/s       25 GB/s        1 GB/s     variable
80 GB      limited   1-4 TB       remote         16 TB      unlimited
```

## 14.2 Model Loading Strategies

```
Problem: Loading a 70B model (140 GB FP16) at pod startup

Strategy 1: Local NVMe (fastest cold start)
  - Pre-download models to local NVMe on each GPU node
  - Mount as hostPath volume
  - Load time: 140 GB / 7 GB/s = 20 seconds
  
  Pros: fastest startup
  Cons: model update requires node-level management

Strategy 2: Shared file system (EFS/Lustre/GPFS)
  - Single model store, mounted by all nodes
  - Load time depends on network: 140 GB / 1 GB/s = 140 seconds (EFS)
  
  Pros: single source of truth, easy updates
  Cons: slow cold start, network dependency

Strategy 3: Init container with model download
  - Init container: download from S3/GCS to emptyDir
  - Main container: load from emptyDir (tmpfs or disk-backed)
  
  Pros: portable, no pre-provisioning
  Cons: adds download time to startup, needs large emptyDir

Strategy 4: Container image with model baked in (for small models)
  - Model weights in container image layers
  - Image pull = model download
  
  Pros: simple, uses existing image infrastructure
  Cons: massive images (14+ GB), slow pulls, OCI layer limits

RECOMMENDED: Local NVMe + DaemonSet model syncer
  - DaemonSet runs on each GPU node
  - Syncs models from object store to local NVMe (background)
  - Inference pods mount hostPath to local models
  - Startup: 20 seconds (local NVMe read only)
```

## 14.3 Checkpoint Storage for Training

```
Training checkpoints: save model state periodically for fault tolerance

Requirements:
  - Write speed: 140 GB in < 30 seconds (< 5% training overhead)
  - Needed: 140 GB / 30 s = 4.7 GB/s write throughput

Options:
  1. Local NVMe (7 GB/s per drive):
     Fast but not durable (node failure = lost checkpoint)
  
  2. NVMe-oF over RDMA (25+ GB/s):
     Remote NVMe with RDMA = fast + durable
     NVIDIA Magnum IO uses this
  
  3. Parallel file system (Lustre/GPFS):
     Aggregate bandwidth from many OSTs
     Lustre: 100+ GB/s aggregate across stripe targets
  
  4. GPUDirect Storage:
     GPU → NVMe directly (bypass CPU)
     Fastest for GPU→storage path

Async checkpointing:
  - Overlap checkpoint write with next training step
  - Copy model state to host memory (fast, NVLink)
  - Write from host to storage in background (doesn't block training)
  - PyTorch FSDP supports this natively
```

---

# SECTION 15: MULTI-TENANCY IN GPU CLUSTERS

---

## 15.1 Resource Quotas

```yaml
# Limit GPU usage per namespace (team):
apiVersion: v1
kind: ResourceQuota
metadata:
  name: gpu-quota
  namespace: team-fraud
spec:
  hard:
    requests.nvidia.com/gpu: "16"     # Max 16 GPUs for this team
    requests.cpu: "128"
    requests.memory: "512Gi"
    persistentvolumeclaims: "20"
    pods: "50"
```

## 15.2 GPU Sharing Strategies

```
Strategy 1: Time-slicing (NVIDIA device plugin)
  - Multiple pods share one GPU via context switching
  - Each pod thinks it has the full GPU
  - NO memory isolation (one pod can OOM the others)
  - Good for: development, batch jobs, non-critical inference

  Configuration:
  devicePlugin:
    config:
      sharing:
        timeSlicing:
          replicas: 4  # 4 pods share each GPU

Strategy 2: MIG (Multi-Instance GPU)
  - Hardware-level partitioning (A100, H100)
  - Each instance has isolated memory AND compute
  - TRUE isolation (one instance cannot affect others)
  - Fixed partition sizes (e.g., 1g.10gb, 2g.20gb, 3g.40gb)
  
  Best for: multi-tenant inference, different models on same GPU

Strategy 3: MPS (Multi-Process Service)
  - Multiple processes share GPU compute (SMs) simultaneously
  - Better GPU utilization than time-slicing (no context switch)
  - Limited memory protection (soft quotas, not hardware isolation)
  - Good for: ensemble models, multiple small models

Strategy 4: Whole GPU allocation (default, recommended for HPC)
  - One pod = one GPU (or multiple GPUs)
  - Full isolation, predictable performance
  - Wastes capacity for small models
  - REQUIRED for: training, large model inference, latency-critical
```

## 15.3 Network Isolation Between Tenants

```yaml
# Cilium Network Policy: isolate GPU namespaces
apiVersion: cilium.io/v2
kind: CiliumNetworkPolicy
metadata:
  name: gpu-namespace-isolation
  namespace: team-fraud
spec:
  endpointSelector: {}
  ingress:
  - fromEndpoints:
    - matchLabels:
        k8s:io.kubernetes.pod.namespace: team-fraud
  - fromEndpoints:
    - matchLabels:
        k8s:io.kubernetes.pod.namespace: monitoring
  egress:
  - toEndpoints:
    - matchLabels:
        k8s:io.kubernetes.pod.namespace: team-fraud
  - toEndpoints:
    - matchLabels:
        k8s:io.kubernetes.pod.namespace: model-store
  - toEntities:
    - kube-apiserver
```

---

# SECTION 16: REAL-WORLD FAILURE SCENARIOS

---

## 16.1 "Pod restarting every 10 minutes, no application errors"

```
Diagnosis:
  1. kubectl describe pod → Last State: Terminated, Reason: OOMKilled
  2. But application memory usage looks fine (8 GB of 16 GB limit)
  3. Check: memory.stat in cgroup
     → cache: 9 GB  ← page cache counted against limit!
  4. Total: 8 GB (RSS) + 9 GB (cache) = 17 GB > 16 GB limit → OOM

Root cause: model file mmap'd → counted as page cache in cgroup
Fix:
  - Increase memory limit (requests + limits) to account for page cache
  - OR: use mlockall() to pin RSS, reduce cache reliance
  - OR: mount model from tmpfs (memory-backed, explicit accounting)
```

## 16.2 "Inference latency spikes every 100 ms"

```
Diagnosis:
  1. p99 is fine (8 ms), but p99.9 has 15 ms spikes
  2. Spikes happen with ~100 ms periodicity
  3. Check: cat /sys/fs/cgroup/.../cpu.stat
     → nr_throttled: 45000 (and climbing!)
  4. CFS bandwidth throttling!

Root cause: 
  Using static CPU Manager with integer CPUs (correct)
  BUT: pod has a sidecar container (logging agent) in same cgroup
  The sidecar triggers throttling for the whole pod cgroup
  
Fix:
  - Remove sidecar, use DaemonSet-based logging instead
  - OR: use separate cgroup for sidecar (not possible in same pod)
  - OR: increase CPU request to account for sidecar
```

## 16.3 "Training job fails after 2 hours: 'CUDA out of memory'"

```
Diagnosis:
  1. GPU memory fragmentation over time
  2. PyTorch allocator has non-contiguous free blocks
  3. A single large allocation (e.g., gradient buffer) can't find
     contiguous space even though total free memory is sufficient

Root cause: many alloc/free cycles fragment the GPU memory
Fix:
  - PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True (PyTorch 2.1+)
  - Pre-allocate maximum batch size at startup (warm up allocator)
  - Use memory pool: max_split_size_mb=512 to reduce fragmentation
  - Use gradient checkpointing to reduce peak memory
```

## 16.4 "Node NotReady after GPU driver update"

```
Diagnosis:
  1. Node goes NotReady 
  2. kubectl describe node → NodeNotReady: kubelet stopped posting status
  3. SSH to node: nvidia-smi shows "Driver/library version mismatch"
  4. GPU driver updated but container runtime has old driver loaded

Root cause: in-place driver update without draining node
Fix:
  - Always drain node before driver update: kubectl drain node --ignore-daemonsets
  - Use GPU Operator with driver auto-upgrade (handles drain automatically)
  - PDB ensures inference capacity maintained during rolling update
  - Rolling update: one node at a time, verify GPU health before proceeding
```

## 16.5 "Pods pending, message: TopologyAffinityError"

```
Diagnosis:
  1. Pod stuck in Pending state
  2. Events: "TopologyAffinityError" from topology manager
  3. Requesting: 8 CPUs + 1 GPU + 1 SR-IOV VF
  4. But: no single NUMA node has all three available

Root cause:
  Other pods already consumed CPUs on the NUMA node where GPU is attached.
  Topology Manager can't find aligned resources.

Fix:
  - Ensure GPU nodes have enough CPUs per NUMA node for your workload
  - Use nodeAffinity to target nodes with correct topology
  - Reserve more CPUs with reservedSystemCPUs (free up NUMA-aligned CPUs)
  - Reduce CPU request if possible
  - Check: are other pods consuming NUMA-0 CPUs unnecessarily?
```

---

# SECTION 17: KUBERNETES NETWORKING INTERNALS FOR HPC

---

## 17.1 CNI Plugin Flow

```
When a pod is created, the container runtime calls the CNI plugin:

1. Kubelet → CRI → containerd → create network namespace
2. containerd calls CNI plugins in order (defined in /etc/cni/net.d/):

Standard CNI chain:
  [main plugin: Cilium/Calico] → [IPAM: host-local] → [meta: bandwidth]

Cilium as CNI:
  a. Create veth pair (one end in pod, one end on host)
  b. Assign IP from Cilium IPAM
  c. Install eBPF programs on veth (datapath)
  d. Register endpoint in Cilium agent
  e. Return IP/route info to containerd

For HPC with Multus (multiple interfaces):
  [Multus meta-plugin] → [Cilium: default network] 
                        → [SR-IOV: secondary network]
  
  Pod gets TWO interfaces:
    eth0: Cilium-managed (normal K8s networking, cluster IP)
    net1: SR-IOV VF (high-performance, direct NIC access)
```

## 17.2 hostNetwork Mode (When to Use)

```yaml
spec:
  hostNetwork: true   # Pod uses the host's network namespace
```

```
What this does:
  - Pod shares the host's network stack (no veth, no bridge, no overlay)
  - Pod sees host's interfaces directly
  - No CNI overhead at all (0 added latency)

When to use for HPC:
  - DPDK pods (need direct NIC access)
  - High-performance storage daemons
  - When SR-IOV is not available but you need low latency

Risks:
  - Port conflicts (pod uses host ports directly)
  - No network isolation (pod sees all host traffic)
  - No Network Policy enforcement
  - Security concern: pod can sniff host traffic

Better alternative: SR-IOV (low latency + isolation)
```

---

# SECTION 18: ADVANCED PLATFORM INTERVIEW QUESTIONS (Grind-Proof)

## "Walk me through what happens from kubectl apply to the inference container running"

→ Full path:
1. kubectl sends pod spec to API server (TLS + RBAC auth)
2. API server validates, persists to etcd
3. Scheduler watches for unscheduled pods, runs filter/score:
   - Filter: nodeAffinity, resource capacity, taints/tolerations, topology
   - Score: resource balance, anti-affinity spread, locality
4. Scheduler binds pod to node (writes nodeName to pod spec)
5. Kubelet on target node sees new pod:
   - Topology Manager: compute NUMA-aligned resource allocation
   - CPU Manager: assign specific CPUs from correct NUMA node
   - Device Manager: assign GPU device (calls NVIDIA device plugin)
6. Kubelet calls CRI → containerd
7. containerd: pull image, prepare rootfs, generate OCI spec with:
   - cpuset.cpus from CPU Manager decision
   - memory.max from pod limits
   - device access (/dev/nvidia0)
8. containerd → runc: create namespaces, set cgroups, exec entrypoint
9. Init containers run (model download if needed)
10. Main container starts: loads model into GPU, warms up, passes readiness probe
11. Endpoint added to Service → traffic flows

## "How do you prevent noisy neighbor effects on GPU nodes?"

→ Layered defense:
1. **CPU isolation:** isolcpus + nohz_full + CPU Manager static (no sharing)
2. **Memory isolation:** Guaranteed QoS + appropriate limits (no OOM cascade)
3. **GPU isolation:** whole-GPU allocation or MIG (hardware-level isolation)
4. **Network isolation:** SR-IOV (per-pod VF) or at least Cilium bandwidth manager
5. **I/O isolation:** cgroup v2 io.max for disk bandwidth limits
6. **Scheduling:** dedicated GPU node pools with taints (only inference pods allowed)
7. **Priority:** high PriorityClass + PDB (survives eviction pressure)

## "What is the difference between cgroups v1 and v2?"

→ Key differences:
```
cgroups v1:
  - Multiple hierarchies (one per controller: cpu, memory, etc.)
  - A process can be in different groups for different controllers
  - Complex, inconsistent behavior
  - Memory controller overhead (40+ µs per allocation in some cases)

cgroups v2:
  - SINGLE unified hierarchy
  - Process belongs to one group, all controllers apply
  - Consistent interface (single set of files)
  - PSI (Pressure Stall Information) — accurate resource pressure metrics
  - memory.pressure, cpu.pressure, io.pressure files
  - Better delegation (rootless containers)
  - io.max works correctly (v1 blkio was broken for buffered I/O)

K8s requirement: cgroups v2 is DEFAULT since K8s 1.25
GPU Operator: supports cgroups v2 since v22.9
```

## "How do you debug a pod that's slow but not OOMing or throttling?"

→ Systematic approach:
1. Check CFS throttling: `cpu.stat` → nr_throttled
2. Check NUMA placement: `numastat -p PID` → numa_miss count
3. Check scheduler latency: `runqslower 100` → any long waits?
4. Check memory pressure: `memory.pressure` → PSI stall time
5. Check I/O wait: `biolatency` → any disk stalls?
6. Check CPU frequency: `cpufreq/scaling_cur_freq` → throttled?
7. Check cache contention: `perf stat -e LLC-load-misses` → L3 misses?
8. Check network: `ss -ti` → retransmits, window scaling issues?
9. Check GPU: `nvidia-smi` → utilization, throttling reasons?

## "How does the kubelet CPU Manager decide which CPUs to assign?"

→ CPU Manager static policy algorithm:
1. Kubelet maintains a "CPU pool" (set of all allocatable CPUs minus reserved)
2. When Guaranteed pod with integer CPUs arrives:
   a. Topology Manager provides NUMA hint (preferred NUMA node)
   b. CPU Manager selects CPUs from that NUMA node
   c. Selection prefers: full physical cores over HT siblings,
      contiguous cores (better cache sharing), cores on same L3
   d. Selected CPUs are removed from the shared pool
   e. Pod's cgroup gets cpuset.cpus = those specific CPUs
3. Non-Guaranteed pods get the REMAINING shared pool (all non-exclusive CPUs)
4. When pod terminates: CPUs return to pool

## "Explain PSI (Pressure Stall Information) and how you'd use it"

→ PSI shows what percentage of time tasks are STALLED waiting for resources:
```bash
cat /proc/pressure/cpu
# some avg10=2.50 avg60=1.20 avg300=0.80 total=12345678
# = 2.5% of time at least SOME tasks are waiting for CPU

cat /proc/pressure/memory  
# some avg10=0.10 ...
# full avg10=0.00 ...
# "some" = at least one task stalled
# "full" = ALL tasks stalled (severe)

cat /proc/pressure/io
# some avg10=5.00 ...
# = 5% of time tasks waiting for I/O

Use for:
  - early warning before OOM (memory some > 5% → investigate)
  - detecting I/O bottleneck (io some > 10% → disk is slow)
  - capacity planning (cpu some > 20% → node overcommitted)
  - available per-cgroup (/sys/fs/cgroup/.../cpu.pressure)
```

---

# SECTION 19: GPU OPERATOR & DEVICE PLUGINS FOR INFERENCE

---

## 19.1 NVIDIA GPU Operator Stack

```
The GPU Operator automates GPU driver + runtime lifecycle in K8s:

┌─────────────────────────────────────────────────────────────┐
│  GPU Operator (top-level controller)                        │
├─────────────────────────────────────────────────────────────┤
│  Components it deploys:                                     │
│  1. nvidia-driver-daemonset     (GPU kernel driver)         │
│  2. nvidia-container-toolkit    (container GPU access)      │
│  3. nvidia-device-plugin        (exposes GPUs to K8s sched) │
│  4. nvidia-dcgm-exporter        (GPU metrics → Prometheus)  │
│  5. nvidia-mig-manager          (MIG partition lifecycle)   │
│  6. gpu-feature-discovery       (labels nodes with GPU info)│
│  7. nvidia-validator            (health checks post-deploy) │
└─────────────────────────────────────────────────────────────┘

Why you need it for production inference:
  - Driver upgrades without draining all pods
  - Automatic MIG configuration changes (no node restart)
  - GPU health monitoring (DCGM) out of the box
  - Node labels for scheduling (gpu.count, gpu.product, mig.config)

Install:
  helm install gpu-operator nvidia/gpu-operator \
    --set driver.enabled=true \
    --set mig.strategy=mixed \
    --set dcgmExporter.enabled=true
```

## 19.2 K8s Device Plugin Interface

```
How GPUs become schedulable resources in K8s:

1. Device plugin registers with kubelet via gRPC:
   RegisterRequest { resource_name: "nvidia.com/gpu" }
   
2. Plugin reports available devices:
   ListAndWatch() → stream of DeviceList:
     [GPU-0: healthy, GPU-1: healthy, GPU-2: unhealthy, GPU-3: healthy]

3. Pod requests GPU:
   resources:
     limits:
       nvidia.com/gpu: 2    # request 2 whole GPUs
       
4. Kubelet calls Allocate(deviceIDs=[GPU-0, GPU-1]):
   Plugin returns:
     - Environment: NVIDIA_VISIBLE_DEVICES=GPU-0,GPU-1
     - Mounts: /dev/nvidia0, /dev/nvidia1
     - Annotations: topology hints

For AMD:
  ROCm device plugin: amd.com/gpu
  Same interface, different resource name
  kubectl get nodes -o json | jq '.items[].status.allocatable["amd.com/gpu"]'

MIG device plugin (multi-instance GPU):
  nvidia.com/mig-1g.5gb: 7    # 7 × 1g.5gb slices on A100
  nvidia.com/mig-3g.40gb: 2   # 2 × 3g.40gb slices
  
  For inference: small models → mig-1g.5gb (7 models per GPU!)
  For LLM: full GPU → nvidia.com/gpu: 1
```

## 19.3 GPU Feature Discovery (GFD) for Inference Scheduling

```
GFD labels nodes with GPU properties:
  nvidia.com/gpu.product=NVIDIA-H100-SXM5-80GB
  nvidia.com/gpu.memory=81920
  nvidia.com/gpu.count=8
  nvidia.com/mig.capable=true
  nvidia.com/gpu.compute.major=9
  nvidia.com/gpu.compute.minor=0
  nvidia.com/cuda.driver.major=535

Use in inference pod scheduling:
  nodeSelector:
    nvidia.com/gpu.product: "NVIDIA-H100-SXM5-80GB"
  
  # Or with AMD:
  nodeSelector:
    amd.com/gpu.product: "MI300X"

Topology-aware scheduling for multi-GPU inference:
  # Ensure TP pods land on GPUs connected via NVLink/XGMI (not PCIe)
  # TopologyManager + GPU device plugin cooperation:
  
  kubelet config:
    topologyManagerPolicy: single-numa-node
    topologyManagerScope: pod        # all containers in pod same NUMA
    
  This ensures:
  - 4 GPU request → gets 4 NVLink-connected GPUs on same NUMA
  - NOT 2 GPUs from NUMA 0 + 2 from NUMA 1 (PCIe cross-socket penalty!)
```

---

# SECTION 20: MULTI-TENANT INFERENCE ON SHARED GPU CLUSTERS

---

## 20.1 The Multi-Tenant Challenge

```
Enterprise GPU cluster shared across teams:
  Team A: Production fraud scoring (SLA: p99 < 10 ms, always-on)
  Team B: LLM inference (SLA: TTFT < 500 ms, bursty traffic)
  Team C: ML experimentation (best-effort, can be preempted)
  Team D: Batch embedding generation (throughput-oriented, off-peak)

Challenges:
  - Team A must NEVER be affected by Team C's experiments
  - Team B's burst shouldn't steal Team A's GPUs
  - Fair sharing when cluster is full
  - Efficient utilization when cluster is idle (no waste)
```

## 20.2 Quota and Priority System

```yaml
# ResourceQuota per namespace (team):
apiVersion: v1
kind: ResourceQuota
metadata:
  name: team-a-production
  namespace: fraud-scoring
spec:
  hard:
    nvidia.com/gpu: "16"        # guaranteed 16 GPUs
    requests.cpu: "128"
    requests.memory: "512Gi"

---
# PriorityClass hierarchy:
apiVersion: scheduling.k8s.io/v1
kind: PriorityClass
metadata:
  name: production-critical
value: 1000000          # highest: production inference (Team A)
preemptionPolicy: PreemptLowerPriority
---
apiVersion: scheduling.k8s.io/v1
kind: PriorityClass
metadata:
  name: production-standard
value: 500000           # standard production (Team B LLM)
---
apiVersion: scheduling.k8s.io/v1
kind: PriorityClass
metadata:
  name: best-effort
value: 100              # experiments (Team C — can be evicted)
preemptionPolicy: Never # won't preempt others, but CAN be preempted
```

## 20.3 GPU Sharing Strategies for Inference

```
Strategy 1: Whole GPU assignment (simplest)
  Pod gets exclusive GPU(s)
  Pros: no interference, predictable performance
  Cons: waste if model doesn't saturate GPU

Strategy 2: MIG partitioning (hardware isolation)
  H100/A100 split into isolated GPU instances
  Each instance: own memory, own SMs, own L2 cache
  Pros: hardware-level isolation, no noisy neighbor
  Cons: fixed partitions, can't dynamically resize
  
  Best for: multiple small inference models sharing one GPU
  Example: 7 × 1g.10gb slices → 7 small BERT classifiers

Strategy 3: MPS (Multi-Process Service) — shared GPU
  Multiple processes share one GPU simultaneously
  Pros: better utilization for low-compute models
  Cons: NO memory isolation (one OOM crashes all), interference
  
  Use ONLY for: trusted same-team workloads

Strategy 4: Time-slicing (K8s device plugin config)
  GPU timeshared across pods (context switching)
  Pros: more pods per GPU
  Cons: latency jitter from context switches (5-15 ms per switch)
  
  Device plugin config:
    sharing:
      timeSlicing:
        resources:
          - name: nvidia.com/gpu
            replicas: 4    # 4 pods "share" each GPU
  
  NEVER for production inference with SLA! Only for dev/test.

Strategy 5: Dynamic MIG (production recommendation for mixed workloads)
  MIG Manager reconfigures GPU partitions based on demand:
  - Peak hours: 1 large partition for LLM (7g.80gb)
  - Off-peak: split into 7 small partitions for batch jobs
  
  Requires: pod eviction during reconfiguration (30-60 sec window)
```

## 20.4 Kueue for Inference Workload Queuing

```yaml
# Kueue manages resource quotas across teams:
apiVersion: kueue.x-k8s.io/v1beta1
kind: ClusterQueue
metadata:
  name: gpu-cluster
spec:
  resourceGroups:
    - coveredResources: ["nvidia.com/gpu"]
      flavors:
        - name: h100-full
          resources:
            - name: "nvidia.com/gpu"
              nominalQuota: 64      # total cluster capacity
              borrowingLimit: 0     # can't borrow from other clusters
---
apiVersion: kueue.x-k8s.io/v1beta1
kind: LocalQueue
metadata:
  name: fraud-scoring-queue
  namespace: fraud-scoring
spec:
  clusterQueue: gpu-cluster
---
# Workload gets admitted only when quota available:
# If cluster full → workload queued (not scheduled)
# Priority determines queue ordering
# Preemption: lower-priority workloads evicted if needed for higher

# For inference specifically:
#   Production inference pods: never queued (always have guaranteed quota)
#   Batch inference (embedding generation): queued during peak, runs off-peak
```

## 20.5 Inference Observability for Multi-Tenant

```yaml
# Key metrics per tenant (namespace):
# Prometheus queries:

# GPU utilization per team:
avg(DCGM_FI_DEV_GPU_UTIL{namespace="fraud-scoring"})

# Inference latency per team:
histogram_quantile(0.99, 
  rate(inference_latency_seconds_bucket{namespace="llm-serving"}[5m]))

# GPU memory utilization per team:
sum(DCGM_FI_DEV_FB_USED{namespace="fraud-scoring"}) / 
sum(DCGM_FI_DEV_FB_FREE{namespace="fraud-scoring"} + DCGM_FI_DEV_FB_USED{namespace="fraud-scoring"})

# Preemption events (SLO breach indicator):
increase(kube_pod_preempted_total{namespace="experiments"}[1h])

# Queue wait time (time-to-schedule):
kueue_workload_wait_time_seconds{queue="fraud-scoring-queue"}

# SLO dashboard per team:
#   Team A: p99 < 10 ms ✓, GPU alloc: 16/16, preemptions: 0
#   Team B: TTFT p99 < 500 ms ✓, GPU alloc: 8/12 quota, queue: 0
#   Team C: GPU alloc: 4/8 quota, preempted: 3 times today
```
