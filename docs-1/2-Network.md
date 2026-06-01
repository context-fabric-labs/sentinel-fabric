
# Layer 2: Networking & Communication — Complete Deep Dive
## "How does data move between hosts and services?"

> **Why this layer matters:** In AI/HPC systems, networking is often the
> HIDDEN bottleneck. Your GPU kernels may run in microseconds, your models
> may be perfectly optimized, but if the data takes 10 ms to arrive over
> a poorly configured network path, none of that matters.
>
> **The core insight:** Networking overhead in Kubernetes can add 50-200 µs
> per hop through the default pod networking stack. For a fraud scoring
> hot path with a 10-30 ms budget, even ONE unnecessary network hop is
> significant. For distributed training where thousands of GPUs synchronize
> via All-Reduce, network bandwidth and latency directly determine training
> speed.
>
> A strong systems engineer knows: what transport to use, when to bypass
> the kernel, how to tune what remains, and how to measure it all.

---

# SECTION 1: TRANSPORT CHOICES — FROM SLOWEST TO FASTEST

---

## The complete spectrum

```
SLOWEST                                                        FASTEST
   ←──────────────────────────────────────────────────────────→

Standard     Cilium    TCP      UDS      io_uring   SR-IOV   AF_XDP   DPDK    RDMA
TCP/IP       eBPF      Tuning
50-200 µs    20-50 µs  varies   1-3 µs   varies     5-10 µs  2-5 µs   1-3 µs  1-2 µs

  ↑ easiest                                              hardest to operate ↑
  ↑ most features                                        fewest features ↑
```

The key insight: **you don't always need the fastest option.** You need
the right option for your latency budget and operational constraints.

---

## 1.1 Standard TCP/IP in Kubernetes (50-200 µs overhead)

### What happens when Pod A talks to Pod B

```
Pod A application
    → write() syscall (user → kernel transition)
    → TCP stack (build TCP header, checksum)
    → IP stack (build IP header, routing decision)
    → veth pair (virtual ethernet — cross network namespace)
    → Linux bridge or OVS (L2 forwarding)
    → iptables/nftables (kube-proxy rules: DNAT, SNAT, conntrack)
    → routing (determine output interface)
    → if overlay (VXLAN): encapsulate in outer UDP/IP header
    → physical NIC TX
    → wire
    → physical NIC RX (on destination node, or same node)
    → if overlay: decapsulate
    → iptables/nftables (reverse DNAT)
    → bridge → veth
    → IP → TCP → socket buffer
    → read() syscall
Pod B application
```

### The overhead breakdown

| Component | Latency | What it does |
|---|---|---|
| veth pair crossing | 5-10 µs | Cross network namespace boundary |
| Linux bridge | 5-10 µs | L2 forwarding decision |
| iptables/nftables | 10-50 µs | Rule evaluation (scales with rule count!) |
| conntrack | 5-20 µs | Connection tracking table lookup |
| SNAT/DNAT | 5-10 µs | Address translation for K8s services |
| VXLAN overlay | 20-50 µs | Encapsulation + decapsulation |
| **Total** | **50-200 µs** | **Per hop** |

### When this is acceptable
- general microservices with > 10 ms SLA
- warm/cold path LLM calls (2-5 sec budget)
- non-latency-critical communication

### When this is NOT acceptable
- hot path fraud scoring (10-30 ms budget)
- GPU-to-GPU communication
- high-frequency feature store access
- anything where 200 µs matters

---

## 1.2 Cilium eBPF (20-50 µs — recommended default for HPC K8s)

### What Cilium changes
Cilium replaces the entire iptables/conntrack/kube-proxy stack with
eBPF programs that run directly in the kernel at hook points:

```
WITHOUT Cilium (standard kube-proxy):
  Pod → veth → bridge → iptables (100+ rules) → conntrack → DNAT → routing
  = 50-200 µs per hop

WITH Cilium (eBPF datapath):
  Pod → veth → eBPF program (single lookup) → routing
  = 20-50 µs per hop

Savings: 30-150 µs per hop (iptables + conntrack eliminated)
```

### Key Cilium features for HPC

**kube-proxy replacement:**
Cilium completely replaces kube-proxy with eBPF-based service routing.
No iptables rules, no conntrack table, no DNAT chains.
```yaml
kubeProxyReplacement: true
```

**Direct routing (no overlay):**
For same-datacenter traffic, disable VXLAN overlay and use direct
node-to-node routing. Eliminates encapsulation overhead.
```yaml
tunnel: disabled
autoDirectNodeRoutes: true
```

**DSR (Direct Server Return):**
For load-balanced traffic, the response goes directly from the backend
to the client, bypassing the load balancer node on the return path.
```yaml
bpf:
  loadBalancer:
    mode: dsr
```

**XDP acceleration:**
Cilium can process packets at the NIC driver level (before the kernel
network stack), further reducing latency for known flows.

**Socket-level load balancing:**
For pod-to-service traffic on the SAME node, Cilium can short-circuit
the entire network stack and connect sockets directly.

### Cilium Helm install for HPC
```bash
helm install cilium cilium/cilium \
  --namespace kube-system \
  --set kubeProxyReplacement=true \
  --set tunnel=disabled \
  --set autoDirectNodeRoutes=true \
  --set bpf.masquerade=true \
  --set bpf.hostRouting=true \
  --set bpf.loadBalancer.mode=dsr \
  --set bandwidthManager.enabled=true \
  --set bandwidthManager.bbr=true \
  --set hubble.relay.enabled=true \
  --set hubble.ui.enabled=true
```

### When to use Cilium
- **Default choice for any HPC K8s cluster**
- Eliminates iptables overhead without requiring application changes
- Provides built-in network observability (Hubble)
- Supports network policies for multi-tenant isolation

---

## 1.3 TCP Tuning (varies — improves any TCP-based communication)

### Why TCP tuning matters
Even with Cilium, the TCP stack itself has tunable parameters that
affect throughput and latency. These apply to ANY TCP communication
in the cluster.

### The essential sysctls
```bash
# /etc/sysctl.d/99-network-hpc.conf

# ─── Buffer Sizes ───
# Controls how much data TCP can buffer per socket
# Larger buffers help high-bandwidth or high-latency paths
net.core.rmem_max = 134217728           # 128 MB max receive buffer
net.core.wmem_max = 134217728           # 128 MB max send buffer
net.core.rmem_default = 16777216        # 16 MB default receive
net.core.wmem_default = 16777216        # 16 MB default send
net.ipv4.tcp_rmem = 4096 1048576 134217728  # min, default, max
net.ipv4.tcp_wmem = 4096 1048576 134217728

# WHY: Default 4 KB buffers are far too small for 10-100 Gbps links.
# The bandwidth-delay product (BDP) for a 10 Gbps link with 0.1 ms RTT
# is 10 Gbps × 0.1 ms = 125 KB. You need buffers >= BDP.

# ─── Connection Handling ───
net.core.somaxconn = 65535              # Max listen backlog
net.core.netdev_max_backlog = 250000    # NIC ring buffer backlog
net.ipv4.tcp_max_syn_backlog = 65535    # SYN queue size

# WHY: Default somaxconn (4096) can cause connection drops under burst.

# ─── Low-Latency TCP ───
net.ipv4.tcp_low_latency = 1           # Prefer latency over throughput
net.ipv4.tcp_fastopen = 3              # TCP Fast Open (client + server)
net.ipv4.tcp_timestamps = 0            # Disable timestamps (saves 12 bytes/pkt)
net.ipv4.tcp_sack = 1                  # Keep Selective ACK

# WHY: tcp_low_latency tells the stack to deliver data immediately
# rather than waiting to batch. tcp_fastopen saves one RTT on new
# connections.

# ─── Congestion Control ───
net.ipv4.tcp_congestion_control = bbr  # Google BBR
net.core.default_qdisc = fq            # Fair Queue (required for BBR)

# WHY: BBR models bandwidth and RTT rather than reacting to packet
# loss. Much better for data center environments than CUBIC.

# ─── Busy Polling ───
net.core.busy_poll = 50                # µs to busy-poll before blocking
net.core.busy_read = 50                # µs to busy-read

# WHY: Instead of sleeping and waiting for an interrupt when a socket
# has no data, the thread spins for 50 µs checking for data. This
# reduces latency by avoiding the sleep→interrupt→wake cycle, at the
# cost of burning CPU during the spin.

# ─── Prevent Idle Performance Loss ───
net.ipv4.tcp_slow_start_after_idle = 0 # Don't reset cwnd after idle

# WHY: Default TCP reduces the congestion window after idle periods,
# causing slow starts on bursty traffic patterns.
```

### Apply and verify
```bash
sudo sysctl -p /etc/sysctl.d/99-network-hpc.conf

# Verify a specific setting
sysctl net.ipv4.tcp_congestion_control
# Expected: bbr
```

---

## 1.4 Unix Domain Socket (1-3 µs — same-host, no TCP overhead)

### What it is
A Unix Domain Socket (UDS) is a communication channel between processes
on the SAME host that bypasses the entire TCP/IP stack:

```
TCP over localhost:
  App → TCP → IP → loopback interface → IP → TCP → App
  = still goes through the full network stack

Unix Domain Socket:
  App → kernel socket buffer → App
  = direct buffer-to-buffer, no TCP/IP processing
```

### When to use it
When two services are on the SAME node and you want lower latency
than localhost TCP. Most relevant for:
- your C++ scoring service → co-located LLM server (vLLM/TRT-LLM)
- your Rust gateway → co-located feature store
- any same-pod or same-node service communication

### Latency comparison
```
TCP localhost:        5-15 ms (full TCP/IP stack + loopback)
Unix Domain Socket:   1-3 ms  (kernel buffer only)
Shared memory:        < 0.1 ms (zero-copy, no kernel involvement for data)
```

### How to use it
```python
# vLLM with Unix Domain Socket (merged August 2025)
# Server:
vllm serve model --uds-path /tmp/vllm.sock

# Client:
import requests_unixsocket
session = requests_unixsocket.Session()
response = session.post(
    "http+unix://%2Ftmp%2Fvllm.sock/v1/chat/completions",
    json={"model": "llama-7b", "messages": [...]})
```

---

## 1.5 io_uring (shared rings — reduced syscall overhead)

### What it is
io_uring is a Linux I/O interface that uses shared memory rings between
user space and kernel space. Instead of one syscall per I/O operation,
you batch multiple operations into a submission queue (SQ) and harvest
completions from a completion queue (CQ).

```
Traditional I/O:
  write() → syscall → kernel → return → write() → syscall → ...
  N operations = N syscalls = N user↔kernel transitions

io_uring:
  [fill SQ with N operations] → submit_and_wait(1) → [harvest CQ]
  N operations = 1 syscall (or even 0 with SQPOLL mode)
```

### When to use it
- high-rate network I/O (many small requests)
- storage I/O (many concurrent disk operations)
- as a stepping stone before going to AF_XDP/DPDK
- when you want better than standard sockets but don't need full kernel bypass

### Key io_uring features
- **Submission Queue (SQ):** user writes I/O requests
- **Completion Queue (CQ):** kernel writes completions
- **Both are mmap'd shared memory** — no copy between user and kernel
- **SQPOLL mode:** kernel polls the SQ continuously — zero syscalls!
- **Fixed buffers:** pre-register buffers to avoid per-I/O registration
- **Multishot:** one request generates multiple completions (useful for accept)

---

## 1.6 SR-IOV (5-10 µs — direct NIC access per pod)

### What it is
SR-IOV (Single Root I/O Virtualization) creates multiple lightweight
"Virtual Functions" (VFs) on a physical NIC. Each VF can be assigned
directly to a pod, giving it hardware-level NIC access that bypasses
the kernel network stack.

```
Without SR-IOV (shared NIC):
  Pod A → veth → bridge → kernel → Physical NIC
  Pod B → veth → bridge → kernel → Physical NIC
  All traffic goes through kernel stack

With SR-IOV (direct NIC access):
  Pod A → VF0 → Physical NIC (direct hardware path)
  Pod B → VF1 → Physical NIC (direct hardware path)
  Each pod has its own hardware NIC queue
```

### Why it matters for HPC
- **5-10 µs latency** (vs 50-200 µs through standard pod networking)
- **Line-rate throughput** (25-100 Gbps per VF)
- **Pod-level isolation** (each pod has its own VF — unlike hostNetwork)
- **Required for RDMA** in containers (RDMA needs direct NIC access)

### Host-level setup
```bash
# 1. Create Virtual Functions on the physical NIC
echo 8 > /sys/class/net/ens5f0/device/sriov_numvfs

# 2. Verify VFs created
lspci | grep "Virtual Function"

# 3. Configure VF (VLAN, MAC, rate limit)
ip link set ens5f0 vf 0 mac aa:bb:cc:dd:ee:00 vlan 100
ip link set ens5f0 vf 0 rate 25000  # 25 Gbps limit
```

### Kubernetes setup (SR-IOV device plugin + Multus)
```yaml
# SR-IOV Network Device Plugin advertises VFs to K8s
apiVersion: sriovnetwork.openshift.io/v1
kind: SriovNetworkNodePolicy
metadata:
  name: gpu-sriov-policy
spec:
  nodeSelector:
    node-role/gpu-inference: "true"
  numVfs: 8
  nicSelector:
    vendor: "15b3"     # Mellanox
    deviceID: "101b"   # ConnectX-6
  deviceType: netdevice
  resourceName: mellanox_sriov_vf

---
# Multus NetworkAttachmentDefinition
apiVersion: k8s.cni.cncf.io/v1
kind: NetworkAttachmentDefinition
metadata:
  name: sriov-net
spec:
  config: |
    {
      "type": "sriov",
      "ipam": {"type": "host-local", "subnet": "10.56.0.0/16"}
    }

---
# Pod requesting SR-IOV VF
apiVersion: v1
kind: Pod
metadata:
  name: inference-pod
  annotations:
    k8s.v1.cni.cncf.io/networks: sriov-net
spec:
  containers:
  - name: inference
    resources:
      requests:
        mellanox_sriov_vf: "1"
      limits:
        mellanox_sriov_vf: "1"
```

### When to use SR-IOV
- GPU inference pods needing low-latency network access
- RDMA-enabled pods (GPUDirect RDMA, KV cache transfer)
- when you need pod isolation (unlike hostNetwork) + low latency

---

## 1.7 AF_XDP (2-5 µs — kernel-native fast path)

### What it is
AF_XDP is a Linux-native high-performance packet I/O mechanism that uses
XDP (eXpress Data Path) to redirect packets from the NIC driver directly
into user-space memory, bypassing most of the kernel network stack.

```
Standard path:
  NIC → driver → kernel network stack → socket buffer → user space
  (full TCP/IP processing, iptables, conntrack, etc.)

AF_XDP path:
  NIC → driver → XDP program (eBPF) → UMEM (shared memory) → user space
  (bypasses TCP/IP, iptables, conntrack entirely)
```

### Key components
- **UMEM:** shared memory region between kernel and user space (like io_uring)
- **RX/TX rings:** SPSC descriptor rings for packet delivery
- **FILL/COMPLETION rings:** buffer management between user and kernel
- **XDP program:** eBPF program at the NIC driver that decides which
  packets go to AF_XDP vs normal stack

### How it differs from DPDK
```
AF_XDP:
  ✅ Linux-native (no special drivers)
  ✅ Works with existing kernel NIC drivers
  ✅ Can selectively redirect specific traffic (XDP filter)
  ✅ Rest of traffic still goes through normal stack
  ❌ Slightly higher latency than DPDK (~2-5 µs vs ~1-3 µs)

DPDK:
  ✅ Lowest possible latency (~1-3 µs)
  ✅ Full control over NIC queues
  ❌ Requires special PMD drivers (takes NIC away from kernel)
  ❌ All traffic must go through DPDK (can't mix with kernel stack)
  ❌ More complex to operate (hugepages, VFIO, dedicated cores)
```

### When to use AF_XDP
- when standard TCP is too slow but DPDK is too invasive
- when you want selective fast-path (some traffic fast, rest normal)
- for custom packet processing (telemetry, DPI, filtering)
- as a more operational-friendly alternative to DPDK

---

## 1.8 DPDK (1-3 µs — user-space PMD polling)

### What it is
DPDK (Data Plane Development Kit) is a user-space framework where the
application polls the NIC directly using Poll Mode Drivers (PMDs),
completely bypassing the Linux kernel network stack.

```
Kernel path:
  NIC → interrupt → kernel driver → kernel stack → socket → user space
  (interrupts, context switches, kernel processing)

DPDK path:
  NIC → PMD (user-space driver) → application ring buffer → application
  (polling, no interrupts, no kernel, no context switches)
```

### Key properties
- **Polling, not interrupts:** dedicated CPU cores poll NIC queues continuously
- **User-space drivers:** NIC is detached from kernel, owned by DPDK
- **Per-core architecture:** each core owns specific NIC RX/TX queues
- **Hugepage-backed buffers:** all packet buffers on hugepages
- **Ring-based:** internal communication uses lock-free rings

### Two execution models
```
Run-to-completion:
  Core receives packet → processes it → sends response → polls for next
  (simplest, one core does everything for a packet)

Pipeline:
  Core 1: poll NIC → enqueue to ring
  Core 2: dequeue from ring → process → enqueue to next ring
  Core 3: dequeue → send response
  (more complex, better for multi-stage processing)
```

### When to use DPDK
- dedicated low-latency packet processing appliances
- telemetry/security inspection at line rate
- when you need absolute minimum latency and control
- when you can dedicate cores and hugepages to networking

### When NOT to use DPDK
- if AF_XDP or Cilium is fast enough for your SLA
- if operational complexity is a concern
- if you need the kernel stack for some traffic

---

## 1.9 RDMA (1-2 µs — direct remote memory access)

### What it is
RDMA (Remote Direct Memory Access) allows one machine to read from or
write to another machine's memory DIRECTLY over the network, without
involving the remote CPU or kernel in the data path.

```
Traditional TCP:
  App A → syscall → kernel → TCP → IP → NIC → wire →
  NIC → IP → TCP → kernel → syscall → App B
  (both CPUs involved, both kernels involved, multiple copies)

RDMA:
  App A → verb → RNIC → wire → RNIC → App B's memory
  (RNIC handles transport, remote CPU/kernel not involved)
```

### Key RDMA operations
- **RDMA Write:** write data directly into remote machine's memory
- **RDMA Read:** read data directly from remote machine's memory
- **Send/Receive:** traditional message passing, but still kernel-bypass

### Two RDMA fabrics
```
InfiniBand:
  ✅ Purpose-built for RDMA (lowest latency, highest bandwidth)
  ✅ Lossless by design (credit-based flow control)
  ✅ Standard for HPC clusters and GPU training
  ❌ Requires special switches and NICs
  ❌ Expensive

RoCE v2 (RDMA over Converged Ethernet):
  ✅ Works on standard Ethernet switches
  ✅ Much cheaper infrastructure
  ✅ Increasingly popular in data centers
  ❌ Requires PFC (Priority Flow Control) and ECN for lossless behavior
  ❌ Slightly higher latency than InfiniBand
```

### Why RDMA matters for AI/HPC
- **Distributed training:** NCCL uses RDMA for All-Reduce across GPUs
- **GPUDirect RDMA:** NIC writes directly to GPU memory (skips CPU entirely)
- **KV cache transfer:** NVIDIA Dynamo uses RDMA for cross-node KV movement
- **Checkpoint storage:** NVMe-oF over RDMA for fast checkpoint writes

### RDMA in Kubernetes
```yaml
# RDMA shared device plugin DaemonSet
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: rdma-shared-dp
spec:
  template:
    spec:
      containers:
      - name: rdma-dp
        image: mellanox/k8s-rdma-shared-dev-plugin
        volumeMounts:
        - name: devs
          mountPath: /dev/infiniband
      volumes:
      - name: devs
        hostPath:
          path: /dev/infiniband

---
# Pod requesting RDMA device
apiVersion: v1
kind: Pod
metadata:
  name: kv-cache-transfer
spec:
  containers:
  - name: transfer
    resources:
      requests:
        rdma/hca_shared_devices_a: "1"
    securityContext:
      capabilities:
        add: ["IPC_LOCK"]  # Required for RDMA memory registration
```

---

# SECTION 2: DISTRIBUTED AI COMMUNICATION

---

## 2.1 NCCL Collectives

### What NCCL is
NCCL (NVIDIA Collective Communication Library) is the standard library
for GPU-to-GPU communication in distributed training and multi-GPU inference.

### The three key collectives

**All-Reduce (most important for training)**
```
Before: Each GPU has a gradient tensor
  GPU 0: [1, 2, 3]
  GPU 1: [4, 5, 6]
  GPU 2: [7, 8, 9]

After All-Reduce (sum):
  GPU 0: [12, 15, 18]  (same on all GPUs)
  GPU 1: [12, 15, 18]
  GPU 2: [12, 15, 18]

Use: gradient synchronization in DDP training
```

**All-Gather (important for tensor parallelism)**
```
Before: Each GPU has a shard
  GPU 0: [a]
  GPU 1: [b]
  GPU 2: [c]

After All-Gather:
  GPU 0: [a, b, c]  (same on all GPUs)
  GPU 1: [a, b, c]
  GPU 2: [a, b, c]

Use: gathering sharded activations in tensor-parallel inference
```

**Reduce-Scatter (important for FSDP/ZeRO)**
```
Before: Each GPU has a full tensor
  GPU 0: [1, 2, 3]
  GPU 1: [4, 5, 6]
  GPU 2: [7, 8, 9]

After Reduce-Scatter (sum):
  GPU 0: [12]      (sum of position 0 across all GPUs)
  GPU 1: [15]      (sum of position 1)
  GPU 2: [18]      (sum of position 2)

Use: gradient reduction + sharding in FSDP/ZeRO
```

### Ring vs Tree topology
```
Ring All-Reduce:
  GPU 0 → GPU 1 → GPU 2 → GPU 3 → GPU 0
  Bandwidth-optimal: uses full bisection bandwidth
  Latency: O(p) where p = number of GPUs
  Best for: large messages

Tree All-Reduce:
  GPU 0 ←→ GPU 1
       ↕
  GPU 2 ←→ GPU 3
  Latency-optimal: O(log p)
  Bandwidth: not optimal
  Best for: small messages, latency-sensitive
```

### Practical importance
All-Reduce bandwidth directly determines training speed for DDP.
If your network is slow, All-Reduce becomes the bottleneck:

```
GPU compute time per step:     100 ms
All-Reduce time (good network): 20 ms  → total: 120 ms
All-Reduce time (bad network):  80 ms  → total: 180 ms (33% slower training!)
```

---

## 2.2 Incast

### What it is
Incast occurs when many senders simultaneously send data to ONE receiver,
causing buffer overflow at the receiver's switch port or NIC.

```
Normal:
  GPU 1 → switch → GPU 0  (one sender, no congestion)

Incast:
  GPU 1 ─┐
  GPU 2 ─┤
  GPU 3 ─┼→ switch port → GPU 0  (all at once!)
  GPU 4 ─┤
  GPU 5 ─┘
  
  Switch buffer overflows → packets dropped → TCP retransmits →
  congestion window collapses → throughput drops dramatically
```

### When it happens in AI/HPC
- **All-Reduce gather phase:** all GPUs send to one GPU simultaneously
- **Checkpoint writing:** all ranks write to storage simultaneously
- **Parameter server:** all workers pull parameters from one server
- **Feature store access:** many inference workers hit one Redis/cache

### How to detect
```bash
# Check TCP retransmissions
sudo tcpretrans

# Check NIC drop counters
ethtool -S eth0 | grep -i drop

# Check switch buffer utilization (vendor-specific)
```

### How to mitigate
- **Larger switch buffers** (deep buffer switches)
- **ECN (Explicit Congestion Notification)** — mark packets before dropping
- **PFC (Priority Flow Control)** — pause senders before buffer overflow
- **Staggered communication** — jitter start times to avoid simultaneous sends
- **Tree topology** — reduce fan-in at any single point

---

## 2.3 GPU-to-GPU Communication

### The interconnect hierarchy (from fastest to slowest)

```
Same GPU:        HBM bandwidth    ~3.35 TB/s (H100)
Same node:       NVLink/NVSwitch  ~900 GB/s (H100 NVLink)
Same node:       PCIe Gen5        ~128 GB/s (bidirectional)
Cross-node:      InfiniBand HDR   ~200 Gbps (25 GB/s) per port
Cross-node:      RoCE v2          ~100-400 Gbps per port
Cross-node:      TCP/IP           ~10-100 Gbps
```

### GPUDirect technologies

**GPUDirect Peer-to-Peer (P2P):**
GPU-to-GPU memory copy within the same node, bypassing CPU.
```
Without P2P: GPU 0 → CPU memory → GPU 1 (two copies)
With P2P:    GPU 0 → NVLink/PCIe → GPU 1 (one copy, no CPU)
```

**GPUDirect RDMA:**
NIC reads/writes directly to GPU memory, bypassing CPU entirely.
```
Without:  NIC → CPU memory → GPU memory (two copies, CPU involved)
With:     NIC → GPU memory directly (one copy, no CPU)
```

**GPUDirect Storage:**
Storage device reads/writes directly to GPU memory.
```
Without:  NVMe → CPU memory → GPU memory
With:     NVMe → GPU memory directly
```

---

# SECTION 3: HOST-LEVEL NIC TUNING

---

## 3.1 NIC Ring Buffers

```bash
# Check current ring buffer sizes
ethtool -g eth0
# Pre-set maximums: RX 8192, TX 8192
# Current: RX 256, TX 256  ← likely too small!

# Increase ring buffers
ethtool -G eth0 rx 8192 tx 8192

# WHY: Larger ring buffers absorb traffic bursts without drops.
# If the CPU is briefly busy (context switch, GC pause), packets
# queue in the ring buffer instead of being dropped.
```

## 3.2 Interrupt Coalescing

```bash
# Low-latency mode: interrupt per packet
ethtool -C eth0 rx-usecs 0 rx-frames 1 tx-usecs 0 tx-frames 1
# = NIC interrupts CPU for EVERY packet
# + lowest latency
# - highest CPU usage from interrupts

# Throughput mode: batch interrupts
ethtool -C eth0 rx-usecs 100 rx-frames 64 tx-usecs 100 tx-frames 64
# = NIC waits up to 100 µs or 64 packets before interrupting
# + much lower CPU usage
# - adds up to 100 µs latency

# Adaptive mode: driver auto-adjusts
ethtool -C eth0 adaptive-rx on adaptive-tx on
# + automatically balances latency and CPU
# - less deterministic
```

## 3.3 IRQ Affinity and Multi-Queue NIC

```bash
# Check NIC queue count
ethtool -l eth0
# Combined: 32

# Set queue count to match performance CPUs
ethtool -L eth0 combined 16

# Pin each queue's IRQ to a specific CPU
# This ensures NIC processing doesn't land on your isolated CPUs
/opt/mellanox/bin/set_irq_affinity_cpulist.sh 0-3 eth0

# Distribute flows across queues (RSS)
ethtool -X eth0 equal 16

# Custom flow steering: specific port → specific queue → specific CPU
ethtool -N eth0 flow-type tcp4 dst-port 8080 action 4
```

## 3.4 Jumbo Frames

```bash
# Standard MTU: 1500 bytes
# Jumbo MTU: 9000 bytes

ip link set eth0 mtu 9000

# WHY: Fewer packets for the same data volume.
# 1 GB transfer:
#   MTU 1500: ~700,000 packets (700K interrupts, 700K headers)
#   MTU 9000: ~120,000 packets (120K interrupts, 120K headers)
#   = 83% fewer packets = less CPU overhead

# IMPORTANT: ALL switches and hosts on the path must support jumbo frames.
# One misconfigured switch will silently drop jumbo packets.
```

---

# SECTION 4: DECISION FRAMEWORK — WHICH TRANSPORT FOR WHICH USE CASE

```
Is your latency target > 1 ms?
  YES → Cilium pod networking is fine
         (add TCP tuning for throughput)
  NO →
    Are services on the SAME host?
      YES →
        Need structured request/response? → Unix Domain Socket (1-3 µs)
        Need raw shared data? → Shared memory (< 0.1 µs)
      NO →
        Need pod isolation?
          YES → SR-IOV (5-10 µs, isolated VFs)
          NO →
            Need RDMA? → SR-IOV + RDMA device plugin
            Need raw packet processing? → AF_XDP or DPDK
            Otherwise → hostNetwork (0 overhead, no isolation)
```

---

# SECTION 5: HOW LAYER 2 CONNECTS TO YOUR STORIES

## Fraud scoring hot path
```
Layer 2 choices:
  Hot path scoring:    shared memory (same-host, zero-copy)
  Feature store:       TCP tuned (Redis, same-DC)
  Hot → Warm handoff:  shared memory (same node)
  Warm → LLM server:   Unix Domain Socket (same node, 1-3 µs)
  Cross-node:          Cilium eBPF (if needed)
```

## Siri pipeline
```
Layer 2 choices:
  Edge → Cloud:        gRPC over TLS (encrypted, session-affine)
  Between stages:      shared memory (same host, zero-copy)
  Search → OpenSearch: TCP tuned (same-DC, BBR, large buffers)
  GPU search:          in-process (no network, FAISS GPU)
```

## Distributed training
```
Layer 2 choices:
  Gradient sync:       NCCL over NVLink (intra-node) + IB/RoCE (inter-node)
  Checkpoint:          RDMA to storage (GPUDirect Storage if available)
  Parameter loading:   NFS or parallel filesystem
```

---

# SECTION 6: INTERVIEW CHEAT SHEET

## "How do you reduce networking overhead in K8s?"
→ Cilium eBPF (replace iptables), TCP tuning, SR-IOV for GPU pods

## "When would you use RDMA?"
→ GPU-to-GPU training sync, KV cache transfer, storage at scale

## "What is the difference between DPDK and AF_XDP?"
→ DPDK: user-space PMD, takes NIC from kernel, lowest latency
→ AF_XDP: kernel-native XDP redirect, works with existing drivers

## "How does NCCL All-Reduce work?"
→ Ring algorithm: each GPU sends/receives to/from neighbor in a ring
→ Bandwidth-optimal for large tensors
→ Alternative: tree for latency-sensitive small messages

## "What causes incast?"
→ Many-to-one traffic pattern → switch buffer overflow → drops → retransmits
→ Fix: ECN/PFC, staggered sends, tree topology, larger buffers

---
---

# SECTION 7: DATA CENTER NETWORK TOPOLOGY (What interviewers LOVE to ask)

---

## 7.1 Fat-Tree / Clos Topology

### What it is
The standard topology for modern HPC/AI data centers. A multi-stage
switching fabric that provides full bisection bandwidth (any half of
the hosts can communicate with the other half at full line rate).

```
                    ┌──────────┐  ┌──────────┐
                    │  Spine 1 │  │  Spine 2 │   ... (Spine layer)
                    └──┬───┬───┘  └───┬───┬──┘
                       │   │          │   │
              ┌────────┘   └──────┐   │   └──────────┐
              │                   │   │               │
         ┌────┴────┐        ┌────┴───┴┐        ┌────┴────┐
         │  Leaf 1 │        │  Leaf 2 │        │  Leaf 3 │  (Leaf layer)
         └─┬──┬──┬─┘        └─┬──┬──┬─┘        └─┬──┬──┬─┘
           │  │  │             │  │  │             │  │  │
          H1 H2 H3           H4 H5 H6           H7 H8 H9  (Hosts/GPUs)
```

### Key properties
- **Full bisection bandwidth:** total bandwidth from leaves to spines
  equals total bandwidth from hosts to leaves
- **Non-blocking:** any permutation traffic pattern achieves full throughput
- **ECMP (Equal-Cost Multi-Path):** multiple spine paths provide redundancy
  and load balancing
- **Scalability:** add more spines for more bisection bandwidth,
  add more leaves for more hosts

### 3-tier Clos (for very large clusters)
```
Super-Spine layer (connects pods)
    ↕
Spine layer (within each pod)
    ↕
Leaf/ToR layer (connects to hosts)
    ↕
Hosts (GPU nodes)
```

### Oversubscription
```
Full bisection bandwidth (1:1):
  Every host can talk to any other host at full line rate simultaneously.
  Expensive — needs many spine switches.

2:1 oversubscription:
  Only half the hosts can talk at full rate simultaneously.
  Common in non-HPC data centers. NEVER acceptable for training.

For AI training clusters:
  → ALWAYS 1:1 (full bisection bandwidth) within a pod/rail
  → All-Reduce needs uniform bandwidth in every direction
```

### Rail-Optimized Topology (NVIDIA's approach for GPU clusters)

```
Rail 0:  GPU0-Node1 ─── GPU0-Node2 ─── GPU0-Node3 ─── ... (same leaf switch)
Rail 1:  GPU1-Node1 ─── GPU1-Node2 ─── GPU1-Node3 ─── ... (same leaf switch)
Rail 2:  GPU2-Node1 ─── GPU2-Node2 ─── GPU2-Node3 ─── ...
...
Rail 7:  GPU7-Node1 ─── GPU7-Node2 ─── GPU7-Node3 ─── ...

Each "rail" connects the same-numbered GPU across all nodes to
the same leaf switch. This means:
- Intra-node: NVLink (900 GB/s on H100)
- Inter-node same-rail: single hop through one leaf (lowest latency)
- Inter-node cross-rail: spine traversal needed (higher latency)

NCCL exploits this by doing:
1. Intra-node reduce via NVLink
2. Inter-node all-reduce along each rail (single-hop each)
```

---

## 7.2 Dragonfly Topology

### What it is
Used in very large HPC systems (Cray Slingshot, HPE). Groups of switches
are fully connected within a group, and groups are connected via global links.

```
Group A                    Group B
┌─────────────────┐        ┌─────────────────┐
│ SW1─SW2─SW3─SW4 │──────→ │ SW1─SW2─SW3─SW4 │
│ (full mesh)     │←────── │ (full mesh)     │
└─────────────────┘        └─────────────────┘
       ↑                          ↑
       └──── Global links ────────┘

Within group: full mesh (any-to-any, single hop)
Between groups: 1-2 global link hops
```

### Why it matters
- **Fewer cables** than equivalent fat-tree for same scale
- **Adaptive routing:** can route around congestion (important!)
- **Used by:** Frontier, Perlmutter, Aurora supercomputers

---

## 7.3 ECMP and Its Problems

### What ECMP does
Equal-Cost Multi-Path: when multiple paths exist to a destination,
hash the packet header to choose one path deterministically.

```
Hash(src_ip, dst_ip, src_port, dst_port, protocol) % num_paths → path_index
```

### The polarization problem
If ALL switches use the same hash function with the same fields,
traffic that was spread across paths at one tier concentrates on
one path at the next tier.

```
Tier 1 hash: flow → path 2
Tier 2 hash: same flow → SAME path 2 (because same 5-tuple!)
Result: one spine link overloaded, others idle
```

### Fix: hash seed randomization
Each switch uses a different hash seed, decorrelating path choices
across tiers. Modern switches (Tomahawk 4, Spectrum-4) do this.

### The elephant flow problem
One large flow (e.g., NCCL ring traffic) can saturate one ECMP path
while leaving others idle.

```
Solution 1: Flowlet switching
  Split long flows into bursts; re-hash after idle gap
  If flow is idle for > X µs, next burst may take different path

Solution 2: Packet spraying
  Send individual packets round-robin across all paths
  + perfect load balance
  - causes packet reordering (TCP hates this, RDMA handles it)

Solution 3: Adaptive routing (Slingshot, InfiniBand)
  Switch monitors queue depths; routes to least-loaded path
```

---

# SECTION 8: LOSSLESS ETHERNET FOR RoCE (Critical for RDMA over Ethernet)

---

## 8.1 Why Lossless Matters

RDMA protocols (RoCE v2) were designed for InfiniBand, which is
inherently lossless (credit-based flow control). When you run RDMA
over Ethernet, you MUST configure lossless behavior, otherwise:

```
Packet drop → RDMA transport retransmit → goes to software path →
latency jumps from 2 µs to 200+ µs → collective stalls →
ALL GPUs wait → training throughput collapses
```

A single packet drop in an 8192-GPU training run can stall ALL GPUs.

---

## 8.2 PFC (Priority Flow Control) — IEEE 802.1Qbb

### What it does
When a switch buffer for a specific priority class fills above a threshold,
it sends a PAUSE frame to the upstream sender, telling it to stop
sending that priority class.

```
Normal state:
  Sender ──[packets]──→ Switch buffer (50% full) ──→ Receiver

Buffer filling:
  Sender ──[packets]──→ Switch buffer (XOFF threshold!) ──→ Receiver
                         ↓
                    PAUSE frame (priority 3)
                         ↓
  Sender ←──[PAUSE]─── Switch

After PAUSE:
  Sender (stopped for priority 3) ... Switch drains buffer ... 

Buffer drained:
  Switch buffer (XON threshold)
  Sender ←──[resume/timeout]─── 
  Sender ──[packets]──→ (resumes)
```

### PFC configuration (Mellanox/NVIDIA switches)
```bash
# Enable PFC on priority 3 (RDMA traffic class)
# On ConnectX NIC:
mlnx_qos -i eth0 --pfc 0,0,0,1,0,0,0,0
#                        ^ priority 3 enabled

# Set XOFF/XON thresholds
# XOFF: stop when buffer reaches this level
# XON:  resume when buffer drains to this level
mlnx_qos -i eth0 --buffer_size 262144  # per-priority buffer: 256 KB
```

### PFC Deadlock — the WORST failure mode

```
PFC deadlock (circular dependency):
  Switch A pauses → Switch B (buffer full)
  Switch B pauses → Switch C (buffer full)
  Switch C pauses → Switch A (buffer full)
  
  ALL three switches are paused, waiting for each other.
  NOTHING moves. Entire fabric is DEAD.
  
  Called: "PFC storm" or "PFC deadlock"
```

### How to prevent PFC deadlock
1. **Limit PFC to specific traffic classes** (only RDMA, not everything)
2. **PFC watchdog:** detect sustained PAUSE and drop after timeout
3. **Proper buffer allocation:** ensure headroom for in-flight packets
4. **Spanning-tree free topology:** use L3 routing (no L2 loops)
5. **DCBX negotiation:** ensure consistent PFC config across all hops

---

## 8.3 ECN (Explicit Congestion Notification) — the BETTER approach

### Why ECN is preferred over PFC alone
PFC is a blunt instrument (stop ALL traffic on a priority). ECN is
surgical (mark specific packets to signal congestion, let end hosts react).

```
ECN flow:
1. Switch buffer crosses marking threshold
2. Switch sets ECN CE (Congestion Experienced) bit in IP header
3. Receiver gets marked packet
4. Receiver sends CNP (Congestion Notification Packet) to sender
5. Sender reduces sending rate
6. Buffer pressure relieved WITHOUT pause/drop

Result: smoother flow, no PFC pause, no deadlock risk
```

### DCQCN (Data Center QCN — the RoCE congestion control algorithm)

```
DCQCN combines:
  - ECN marking at switches (signal congestion)
  - Rate-based reaction at sender (reduce rate on CNP)
  - Timer-based recovery (gradually increase rate after reduction)

Key parameters:
  - Marking threshold: when to start marking (e.g., 100 KB queue depth)
  - Rate reduction factor: how much to slow down (e.g., factor of 2)
  - Recovery stages: how quickly to ramp back up (AI, HAI timers)
```

### Switch ECN configuration
```bash
# On Mellanox Spectrum switches (cumulus/NVOS):
# Mark ECN when queue exceeds 100KB, absolute threshold
nv set qos ecn default threshold min 150000 max 1500000

# On host NIC (verify ECN capability):
ethtool --show-offload eth0 | grep ecn
# tx-ecn-capable: on

# Enable ECN in kernel:
sysctl -w net.ipv4.tcp_ecn=1
```

### The recommended approach: ECN + PFC together
```
ECN handles:    normal congestion (surgical, smooth)
PFC handles:    burst overflow that ECN couldn't prevent (safety net)

Think of it as:
  ECN = brakes (gradual slowdown)
  PFC = emergency stop (last resort before crash)
```

---

## 8.4 DSCP / Traffic Class Mapping

### The full QoS pipeline for RoCE
```
Application sets DSCP → NIC maps to priority → Switch maps to queue
                                                → ECN/PFC per queue

Example mapping:
  DSCP 26 (AF31) → Priority 3 → Queue 3 → PFC enabled, ECN enabled
  DSCP 0  (BE)   → Priority 0 → Queue 0 → Best effort, no PFC
```

### Why this matters
You want RDMA traffic (latency-critical) on a lossless priority class,
while bulk TCP traffic (checkpointing, logging) stays on best-effort.
Mixing them causes PFC to fire for non-critical traffic, pausing RDMA.

```bash
# Set DSCP on RDMA traffic (ConnectX NIC):
mlnx_qos -i eth0 --trust dscp
cma_roce_tos -d mlx5_0 -t 104  # DSCP 26 (= TOS 104)
```

---

# SECTION 9: RDMA PROGRAMMING MODEL (Deep Dive)

---

## 9.1 RDMA Verbs Architecture

### Core abstractions
```
┌─────────────────────────────────────────────────┐
│ Application                                      │
├─────────────────────────────────────────────────┤
│ libibverbs API (user-space)                      │
├─────────────────────────────────────────────────┤
│ Kernel driver (mlx5_ib)                          │
├─────────────────────────────────────────────────┤
│ RNIC (RDMA-capable NIC hardware)                 │
└─────────────────────────────────────────────────┘
```

### The key objects

**Protection Domain (PD):**
Security boundary. Objects in different PDs cannot interact.
Like a process address space — isolates memory regions.

**Memory Region (MR):**
A registered chunk of user memory that the NIC can DMA to/from.
Registration pins pages and gives the NIC a translation table.
```c
struct ibv_mr *mr = ibv_reg_mr(pd, buffer, size,
    IBV_ACCESS_LOCAL_WRITE |
    IBV_ACCESS_REMOTE_WRITE |
    IBV_ACCESS_REMOTE_READ);
// mr->lkey: used by local operations
// mr->rkey: given to remote peer for RDMA write/read
```

**Queue Pair (QP):**
The RDMA equivalent of a socket. One QP per connection.
Contains a Send Queue (SQ) and Receive Queue (RQ).
```
QP states: RESET → INIT → RTR (Ready to Receive) → RTS (Ready to Send)
Must transition through states in order (unlike TCP connect which is one call)
```

**Completion Queue (CQ):**
Where the NIC posts completion notifications after operations finish.
```c
struct ibv_cq *cq = ibv_create_cq(ctx, cq_size, NULL, NULL, 0);
// Poll for completions:
struct ibv_wc wc;
int n = ibv_poll_cq(cq, 1, &wc);
if (n > 0 && wc.status == IBV_WC_SUCCESS) { /* done */ }
```

### The three operation types

**1. Send/Receive (two-sided):**
```
Sender posts Send WR to SQ    →    Receiver must have posted Receive WR to RQ
NIC sends data                 →    NIC places data in receiver's buffer
Both sides involved — like TCP

Use: connection setup, small control messages
```

**2. RDMA Write (one-sided):**
```
Sender knows remote address + rkey
Sender posts RDMA Write WR to SQ
NIC writes data directly to remote memory
REMOTE CPU IS NOT INVOLVED — no receive posted, no interrupt

Use: bulk data transfer, KV cache shipping, parameter updates
```

**3. RDMA Read (one-sided):**
```
Requester knows remote address + rkey
Requester posts RDMA Read WR to SQ
NIC reads from remote memory and delivers locally
REMOTE CPU IS NOT INVOLVED

Use: pulling data on-demand (e.g., remote feature lookup)
```

### Why memory registration is expensive
```
ibv_reg_mr() does:
1. Pin pages (prevent swap-out) — can take milliseconds for large regions
2. Create NIC translation table (virtual → physical address mapping)
3. Allocate hardware resources on the NIC

Best practice:
- Register memory ONCE at startup (not per-request!)
- Use memory pools with pre-registered buffers
- For GPU memory: use nvidia_peermem for GPU MR registration
```

---

## 9.2 RDMA Performance Considerations

### Inline data
For small messages (< 64-256 bytes), embed data directly in the WQE
(Work Queue Entry) instead of using a separate buffer. Saves one DMA read.

### Doorbell batching
Instead of ringing the doorbell (MMIO write to NIC) for every WQE,
batch multiple WQEs and ring once. Reduces PCIe transactions.

### Unsignaled completions
Don't request a CQE for every operation. Signal every Nth completion.
Reduces CQ pressure and PCIe traffic.
```c
wr.send_flags = (count % 32 == 0) ? IBV_SEND_SIGNALED : 0;
// Only generate completion every 32nd operation
```

### SRQ (Shared Receive Queue)
Instead of posting receive buffers to EVERY QP individually, use one
SRQ shared across all QPs. Reduces memory usage from O(connections × buffers)
to O(buffers).

---

# SECTION 10: NCCL DEEP TUNING (What makes distributed training fast or slow)

---

## 10.1 NCCL Environment Variables

```bash
# ─── Transport Selection ───
NCCL_IB_HCA=mlx5_0,mlx5_1        # Which IB devices to use
NCCL_SOCKET_IFNAME=eth0            # Which interface for TCP bootstrap
NCCL_NET_GDR_LEVEL=5               # GPUDirect RDMA level (SYS=5, highest)
NCCL_IB_GID_INDEX=3                # RoCE GID index (for RoCE v2)

# ─── Algorithm Selection ───
NCCL_ALGO=Ring                     # Ring, Tree, CollnetDirect, CollnetChain
NCCL_PROTO=Simple                  # Simple, LL (Low Latency), LL128

# Algorithm selection logic:
#   Large messages (>256KB): Ring (bandwidth-optimal)
#   Small messages (<256KB): Tree (latency-optimal)
#   SHARP available: CollnetDirect (in-network reduction)

# ─── Performance Tuning ───
NCCL_BUFFSIZE=16777216             # 16MB internal buffer (default 4MB)
NCCL_NTHREADS=512                  # Threads per NCCL kernel
NCCL_MAX_NCHANNELS=32              # Max parallel channels
NCCL_MIN_NCHANNELS=16              # Min parallel channels

# WHY channels matter:
# Each channel is an independent ring/tree that runs in parallel.
# More channels = more bandwidth utilization (up to link saturation)
# Too many channels = GPU SM contention with compute kernels

# ─── Debugging ───
NCCL_DEBUG=INFO                    # WARN, INFO, TRACE
NCCL_DEBUG_SUBSYS=INIT,NET        # Filter to specific subsystems

# ─── InfiniBand Specific ───
NCCL_IB_QPS_PER_CONNECTION=4      # QPs per GPU pair (more = more bandwidth)
NCCL_IB_TC=106                    # Traffic class (for QoS mapping)
NCCL_IB_TIMEOUT=22                # IB timeout (2^22 × 4.096 µs ≈ 17 sec)
NCCL_IB_RETRY_CNT=7               # Retry count before failure
```

## 10.2 NCCL Topology Detection

NCCL auto-detects the hardware topology and selects optimal paths:

```bash
# NCCL prints topology at INIT:
# NCCL INFO Trees [0] 1/0/-1->2->3 [1] 1/0/-1->2->3
# NCCL INFO Channel 00/16 : 0 1 2 3 4 5 6 7
# NCCL INFO Channel 01/16 : 0 3 2 1 4 7 6 5

# Force topology from file:
NCCL_TOPO_FILE=/path/to/topo.xml

# Dump detected topology:
NCCL_TOPO_DUMP_FILE=/tmp/nccl_topo.xml
```

### How NCCL chooses intra-node vs inter-node
```
Intra-node (GPUs connected via NVLink/NVSwitch):
  → Use NVLink for all-reduce (900 GB/s on H100 8-GPU)
  → Ring across NVLink paths

Inter-node (GPUs connected via IB/RoCE):
  → Use IB/RoCE RDMA
  → Hierarchical: reduce within node first, then across nodes
  → This is the "hierarchical all-reduce" pattern
```

## 10.3 SHARP (Scalable Hierarchical Aggregation and Reduction Protocol)

### What it is
SHARP offloads collective operations (reduce, all-reduce) to the
InfiniBand SWITCH HARDWARE. Instead of data going through GPUs for
reduction, the switch does the math.

```
Without SHARP (standard ring all-reduce):
  GPU 0 → GPU 1 → GPU 2 → GPU 3 → ... → GPU 0
  Data traverses the ring, each GPU adds its contribution
  Latency: O(p), Bandwidth: optimal but slow for small messages

With SHARP (in-network reduction):
  All GPUs → Switch (reduces IN HARDWARE) → All GPUs
  Switch has ALUs that perform sum/min/max
  Latency: O(1) effectively — just one round-trip to switch!
  
  Especially impactful for small messages where latency dominates
```

### SHARP setup
```bash
# Enable SHARP in NCCL:
NCCL_ALGO=CollnetDirect  # or CollnetChain
NCCL_COLLNET_ENABLE=1

# Requires:
# - Mellanox Quantum/Quantum-2 switches with SHARP support
# - SHARP daemon (sharpd) running on fabric manager
# - SHARP aggregation nodes configured
```

---

# SECTION 11: NETWORK OBSERVABILITY & DEBUGGING

---

## 11.1 Measuring Network Latency Correctly

### Why ping is NOT enough
```bash
# ping measures ICMP RTT — but ICMP is handled differently than TCP/UDP
# It doesn't reflect:
# - iptables/conntrack overhead (ICMP may bypass rules)
# - TCP handshake time
# - TLS negotiation time
# - application serialization time

# Better tools:
```

### sockperf (accurate TCP/UDP latency)
```bash
# Server:
sockperf sr --tcp -p 12345

# Client (latency test):
sockperf ping-pong --tcp -i 10.0.0.1 -p 12345 -t 30
# Reports: avg, p50, p99, p99.9 latency

# Client (throughput test):
sockperf tp --tcp -i 10.0.0.1 -p 12345 -t 30 --msg-size 1472
```

### ib_write_lat / ib_read_lat (RDMA latency)
```bash
# Server:
ib_write_lat -d mlx5_0

# Client:
ib_write_lat -d mlx5_0 10.0.0.1
# Reports: min, max, avg, p99 for RDMA write latency
# Expected: 1-2 µs for same-rack, 3-5 µs cross-rack

# Bandwidth test:
ib_write_bw -d mlx5_0 --report_gbits 10.0.0.1
# Expected: ~190 Gbps for HDR (200G), ~380 Gbps for NDR (400G)
```

### qperf (quick network perf — latency AND bandwidth)
```bash
# Server:
qperf

# Client:
qperf 10.0.0.1 tcp_lat tcp_bw udp_lat rc_rdma_write_lat rc_rdma_write_bw
```

## 11.2 InfiniBand Diagnostics

```bash
# Check IB port state
ibstat
# Expected: State: Active, Physical state: LinkUp, Rate: 200 (HDR)

# Check for errors on IB port
perfquery -x  # Extended counters
# Look for: PortRcvErrors, PortXmitDiscards, ExcessiveBufferOverrunErrors
# ANY non-zero error counter is a problem in a healthy fabric

# Fabric-wide health check
ibdiagnet  # Scans entire fabric
# Reports: topology errors, bad links, routing issues

# Check RDMA connectivity
rdma_bw_test -d mlx5_0 -p 18515 server_ip

# Trace route through IB fabric
ibtracert <src_lid> <dst_lid>

# Monitor real-time counters
watch -n 1 perfquery -x
```

## 11.3 Detecting Network Problems

### Silent packet corruption
```bash
# NIC hardware CRC errors:
ethtool -S eth0 | grep -i "crc\|error\|discard"
# rx_crc_errors: 0  ← any non-zero = bad cable/optic

# For IB:
perfquery | grep -i "symbol\|error"
# SymbolErrorCounter: 0  ← any non-zero = bad link
```

### Detecting micro-bursts
```bash
# Check NIC pause frame counters:
ethtool -S eth0 | grep pause
# rx_pause: shows how often this NIC was told to pause (congestion upstream)
# tx_pause: shows how often this NIC told upstream to pause (local congestion)

# High rx_pause = your traffic is being throttled
# High tx_pause = you are the congestion point
```

### TCP retransmission analysis
```bash
# Real-time retransmit monitoring
sudo tcpretrans  # BCC tool — shows each retransmit with connection info

# Aggregate stats
ss -ti | grep -i retrans
# retrans:5/10 = 5 retransmits in current window, 10 total

# Netstat summary
netstat -s | grep -i retrans
# segments retransmitted: should be < 0.1% of total segments
```

### Detecting bufferbloat
```bash
# Check queue depth at switch (vendor-specific)
# On host, check socket queue backlog:
ss -tnp | awk '{print $3, $4}'  # Recv-Q Send-Q
# Non-zero Send-Q = data waiting to be ACK'd (possible congestion)
# Large Recv-Q = application not reading fast enough
```

---

# SECTION 12: gRPC TUNING FOR HPC SERVICES

---

## 12.1 Why gRPC Matters

gRPC is the default RPC framework for:
- Kubernetes control plane (kubelet, API server)
- Model serving (TensorFlow Serving, Triton, vLLM)
- Service-to-service communication in inference pipelines
- NCCL bootstrap (initial topology discovery)

Default gRPC settings are tuned for general use, NOT for HPC latency.

## 12.2 Critical gRPC Settings

```python
# Python gRPC channel options for low-latency inference:
channel = grpc.insecure_channel('target:50051', options=[
    # ─── Keepalive ───
    ('grpc.keepalive_time_ms', 10000),          # Ping every 10s
    ('grpc.keepalive_timeout_ms', 5000),         # 5s to get pong
    ('grpc.keepalive_permit_without_calls', 1),  # Keep alive even when idle
    ('grpc.http2.max_pings_without_data', 0),    # Unlimited pings
    
    # ─── Flow Control ───
    ('grpc.http2.initial_window_size', 1048576),      # 1 MB initial window
    ('grpc.http2.initial_connection_window_size', 2097152),  # 2 MB conn window
    # WHY: Default 64KB window is tiny. Large tensors need big windows
    # to avoid stalls waiting for window updates.
    
    # ─── Connection Management ───
    ('grpc.max_receive_message_length', 104857600),  # 100 MB max message
    ('grpc.max_send_message_length', 104857600),
    ('grpc.enable_retries', 1),
    ('grpc.service_config', json.dumps({
        "methodConfig": [{
            "name": [{}],
            "retryPolicy": {
                "maxAttempts": 3,
                "initialBackoff": "0.01s",   # 10ms initial
                "maxBackoff": "0.1s",        # 100ms max
                "backoffMultiplier": 2,
                "retryableStatusCodes": ["UNAVAILABLE"]
            }
        }]
    })),
    
    # ─── TCP Tuning ───
    ('grpc.so_reuseport', 1),           # Multiple threads accept on same port
    ('grpc.tcp_nodelay', 1),            # Disable Nagle's algorithm
])
```

### C++ gRPC (for Triton/inference servers)
```cpp
grpc::ChannelArguments args;
args.SetInt(GRPC_ARG_HTTP2_BDP_PROBE, 1);           // BDP estimation
args.SetInt(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);
args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);
args.SetInt(GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE, 1048576);  // 1MB write buffer
args.SetInt(GRPC_ARG_HTTP2_MAX_FRAME_SIZE, 16384);       // 16KB frames
```

## 12.3 Connection Pooling and Load Balancing

```
Problem: gRPC uses HTTP/2 which multiplexes ALL requests over ONE TCP connection.
L4 load balancers (K8s Service, AWS NLB) balance per-connection, NOT per-request.
Result: one backend gets ALL requests from one client.

Solutions:
1. Client-side load balancing (gRPC built-in):
   - round_robin: distribute across all resolved endpoints
   - pick_first: use one endpoint (default — BAD for load distribution)

2. L7 load balancer (Envoy, Istio):
   - Inspects HTTP/2 frames, balances per-REQUEST
   - Adds ~0.5-2 ms latency (may be unacceptable for hot path)

3. Multiple channels:
   - Create N gRPC channels (= N TCP connections)
   - Round-robin across channels at application level
   - Each connection gets L4-balanced to different backend
```

---

# SECTION 13: SERVICE MESH IMPACT AND ALTERNATIVES

---

## 13.1 The Istio/Envoy Overhead Problem

```
Standard Envoy sidecar proxy path:
  App → localhost:port → Envoy sidecar (iptables redirect) → 
  TCP stack → wire → Envoy sidecar (on dest) → 
  iptables redirect → localhost:port → App

Per-hop overhead: 1-5 ms (two extra proxy hops!)
For a 3-service chain: +6-15 ms added latency

For HPC inference with 10-30 ms budget: UNACCEPTABLE
```

## 13.2 Alternatives to full service mesh for HPC

```
Option 1: Ambient mesh (Istio ambient mode)
  - No sidecars! Uses per-node ztunnel (shared proxy)
  - L4 processing only (no L7 inspection per-request)
  - Overhead: ~0.2-0.5 ms (much better)

Option 2: Cilium service mesh (eBPF-based)
  - No sidecar, no proxy for L4
  - eBPF handles mTLS, load balancing, observability
  - Per-hop overhead: ~50-100 µs
  - L7 policies still need Envoy (optional, per-service)

Option 3: No mesh for hot path
  - Direct service-to-service communication
  - Use Cilium network policies for security
  - Manual mTLS with cert-manager
  - Acceptable for internal HPC services

Recommendation for AI inference:
  Hot path: NO mesh (direct communication, Cilium policies)
  Control plane: Ambient mesh or Cilium mesh (security + observability)
```

---

# SECTION 14: NETWORK BANDWIDTH MATH (Must be able to calculate on the spot)

---

## 14.1 Bandwidth-Delay Product (BDP)

```
BDP = Bandwidth × RTT

This tells you the MINIMUM buffer size needed to keep a link fully utilized.

Example: 100 Gbps link, 0.5 ms RTT (cross-datacenter)
  BDP = 100 Gbps × 0.5 ms = 100 × 10^9 × 0.5 × 10^-3 = 6.25 MB

  → TCP buffers must be >= 6.25 MB to fully utilize the link
  → Default TCP buffer (128 KB) would utilize only 2% of the link!
  
  This is why net.ipv4.tcp_rmem max = 128 MB in our tuning.
```

## 14.2 All-Reduce Bandwidth Requirement

```
For Ring All-Reduce with N GPUs, message size M:
  Each GPU sends: 2 × M × (N-1)/N bytes total
  Time = 2 × (N-1) × α + 2 × M × (N-1)/(N × BW)
  
  Where:
    α = per-message latency (startup cost)
    BW = per-link bandwidth
    
  For large M (bandwidth-dominated):
    Time ≈ 2M/BW (independent of GPU count!)
    This is why ring all-reduce is "bandwidth-optimal"

Example: 
  Model: 7B parameters × 4 bytes (FP32 gradients) = 28 GB
  Network: 400 Gbps (50 GB/s) per GPU
  Ring all-reduce time ≈ 2 × 28 GB / 50 GB/s = 1.12 seconds
  
  With FP16 gradients (14 GB):
  Time ≈ 2 × 14 / 50 = 0.56 seconds
  
  With 8 parallel channels:
  Time ≈ 0.56 / 8 = 70 ms (if links not saturated)
```

## 14.3 Network Bottleneck Analysis

```
Question: "Is my training network-bound or compute-bound?"

Compute time per step (forward + backward): T_compute
All-reduce time per step: T_allreduce

If T_allreduce > T_compute: NETWORK BOUND
  → Need faster network or gradient compression

If T_compute > T_allreduce: COMPUTE BOUND  
  → Overlap allreduce with backward pass (default in PyTorch DDP)

The overlap trick:
  Backward pass computes gradients layer-by-layer (last layer first).
  As soon as a layer's gradient is ready, start all-reduce for that layer
  while computing gradients for earlier layers.
  
  Effective all-reduce cost = max(0, T_allreduce - T_compute_overlap)
```

## 14.4 Cross-Node Bandwidth Planning

```
For a 256-GPU cluster (32 nodes × 8 GPUs):
  Intra-node bandwidth (NVLink): 900 GB/s per GPU (not a bottleneck)
  Inter-node bandwidth: 8 × 400 Gbps = 3200 Gbps per node = 400 GB/s

  Model gradient size: 70B × 2 bytes (BF16) = 140 GB
  
  Hierarchical all-reduce:
    Step 1: Intra-node reduce (NVLink): 140 GB / 900 GB/s = 0.16 s
    Step 2: Inter-node all-reduce (32 nodes, ring):
            2 × 140 GB / 400 GB/s = 0.7 s
    Step 3: Intra-node broadcast (NVLink): 140 GB / 900 GB/s = 0.16 s
    
  Total: ~1 second (if no overlap with compute)
  
  Can we do better?
    - Gradient compression (1-bit Adam): 140 GB → ~4.4 GB → 22 ms
    - FP8 communication: 140 GB → 70 GB → 0.35 s
    - SHARP in-network reduction: cuts inter-node by ~50%
```

---

# SECTION 15: NETWORK SECURITY IN HPC

---

## 15.1 Encryption Overhead

```
                    Throughput      Latency Add     CPU Cost
No encryption:      100 Gbps       0                0
IPsec (AES-GCM):   40-60 Gbps     10-50 µs        High (without offload)
IPsec (NIC offload): 100 Gbps     2-5 µs          Zero (hardware)
MACsec:             100 Gbps       1-2 µs          Zero (NIC hardware)
WireGuard:          20-40 Gbps     5-20 µs         Moderate
kTLS (kernel TLS):  80-100 Gbps    2-5 µs          Low (NIC offload)
```

### When to encrypt in HPC
```
MUST encrypt:
  - Multi-tenant GPU clusters (different customers sharing fabric)
  - Cross-datacenter traffic (over public/shared links)
  - Compliance requirements (PCI-DSS, HIPAA)

CAN skip encryption:
  - Single-tenant dedicated GPU cluster (air-gapped)
  - Intra-node NVLink traffic (physically isolated)
  - Same-rack traffic on dedicated VLAN (physical security)

Best practice:
  - Use NIC-offloaded IPsec or MACsec (zero CPU overhead)
  - ConnectX-6 Dx and later support IPsec inline at 100+ Gbps
  - Never use software encryption on the training data path
```

### ConnectX IPsec offload
```bash
# Enable IPsec full offload on ConnectX-6 Dx:
ip xfrm state add ... offload dev eth0 dir out
# NIC handles encryption/decryption in hardware
# Zero CPU overhead, line-rate throughput, ~2 µs added latency
```

---

# SECTION 16: REAL-WORLD FAILURE SCENARIOS AND FIXES

---

## 16.1 "Training suddenly 3x slower, no errors in logs"

```
Diagnosis:
  1. Check NCCL debug output: NCCL_DEBUG=INFO
     → "NET/IB: Using device mlx5_1 port 1" (only ONE device!)
     → Should be using mlx5_0 AND mlx5_1 (two ports)
  
  2. Check IB port state: ibstat
     → mlx5_0: State: Down  ← ONE PORT FAILED
     
  3. NCCL silently fell back to one port = half bandwidth = ~2x slower
     Plus increased contention = additional slowdown

Fix:
  - Check cable/optic on failed port
  - Replace if hardware fault
  - Set NCCL_IB_HCA explicitly to fail-fast rather than silent fallback
```

## 16.2 "Intermittent 100ms latency spikes in inference"

```
Diagnosis:
  1. Check PFC pause counters: ethtool -S eth0 | grep pause
     → rx_pause: climbing!
     → Someone upstream is pausing us
  
  2. Check switch buffer utilization (switch CLI)
     → One queue at 95% utilization
     → Cause: a training job on same fabric causing incast
  
  3. The training all-reduce bursts are consuming switch buffers,
     triggering PFC pauses on the shared fabric, stalling inference

Fix:
  - Separate traffic classes: training on TC3, inference on TC1
  - Different PFC/ECN settings per class
  - OR: physically separate fabrics for training vs inference
  - OR: time-division (schedule training all-reduce to avoid inference peak)
```

## 16.3 "NCCL timeout after 5 minutes, then job crashes"

```
Diagnosis:
  1. NCCL error: "Watchdog caught collective operation timeout"
  2. Check: one GPU is stuck (CUDA error, or NIC error)
  3. ibstat shows one node's port flapping (Up/Down/Up/Down)
  
  Root cause: bad fiber optic causing intermittent link drops
  Each drop → PFC pause → buffer drain → resume → drop again
  Eventually NCCL times out waiting for the slow node

Fix:
  - Replace optic/cable on flapping port
  - Set NCCL_IB_TIMEOUT appropriately (not too short, not too long)
  - Enable NCCL health checks: NCCL_HEALTH_CHECK_INTERVAL=60
  - Use job scheduler fabric health pre-checks before launching training
```

## 16.4 "gRPC deadline exceeded" on inference hot path

```
Diagnosis:
  1. p99 latency: 45 ms (SLA: 30 ms)
  2. Not the model (model inference: 8 ms p99)
  3. Network: check ss -ti for retransmits
     → retrans: 3/47  ← retransmissions happening!
  4. ethtool -S: rx_pause frames increasing
  5. Cause: Nagle's algorithm + delayed ACK interaction
  
  Nagle: "Don't send small packets, wait for more data"
  Delayed ACK: "Don't ACK immediately, wait for more data"
  Together: DEADLOCK for 200ms until timer fires!

Fix:
  - TCP_NODELAY on all sockets (disable Nagle)
  - TCP_QUICKACK on receiver
  - Already in our gRPC config: ('grpc.tcp_nodelay', 1)
  - Verify it's actually set on the server side too!
```

---

# SECTION 17: ADVANCED INTERVIEW QUESTIONS (Grind-Proof)

## "Walk me through what happens when GPU 0 on Node 1 needs to send a gradient to GPU 3 on Node 5"

→ Full path:
1. GPU 0 computes gradient in HBM
2. NCCL kernel copies gradient to NIC-registered buffer (if not using GPUDirect)
   OR with GPUDirect RDMA: NIC reads directly from GPU HBM
3. RNIC posts RDMA Write work request to QP's send queue
4. RNIC DMAs data from GPU memory → NIC buffer
5. NIC segments into MTU-sized packets, adds IB/RoCE headers
6. Packets traverse: Node 1 NIC → Leaf switch → Spine switch → Leaf switch → Node 5 NIC
7. Node 5 RNIC reassembles, writes directly to GPU 3's registered memory region
8. Completion posted to CQ (if signaled)
9. No CPU involvement on Node 5 for the data transfer itself

## "How would you debug a 50% drop in all-reduce bandwidth?"

→ Systematic approach:
1. `ibstat` — check all ports are up and at expected rate
2. `perfquery -x` — check for error counters (SymbolErrors, RcvErrors)
3. `NCCL_DEBUG=INFO` — check which devices/paths NCCL is using
4. `ib_write_bw` point-to-point — isolate if it's one link or fabric-wide
5. Check PFC counters — is flow control throttling us?
6. Check ECMP — is traffic polarized to one spine?
7. Check for topology change — did a link fail causing rerouting?
8. Check thermal throttling on NIC (mlx5_core temperature)

## "Explain the tradeoff between PFC and ECN for lossless Ethernet"

→ PFC: stops sender entirely, zero drops guaranteed, BUT:
  - Risk of deadlock (circular pause dependency)
  - Head-of-line blocking (pauses ALL traffic on that priority)
  - Propagates congestion backwards (one slow receiver pauses entire chain)

→ ECN: marks packets, sender reduces rate gradually, BUT:
  - Marking threshold too low = premature throttling = underutilization
  - Marking threshold too high = drops before senders react
  - Requires end-host support (DCQCN/DCTCP)
  - Cannot guarantee zero drops (burst can exceed buffer before feedback)

→ Best practice: use BOTH. ECN for steady-state, PFC as safety net.

## "How do you size switch buffers for an AI training cluster?"

→ Buffer must absorb incast bursts during all-reduce gather phase:
  Buffer needed = Number_of_senders × MTU × (PFC_reaction_time / packet_time)
  
  Example: 32 senders, 9000 byte MTU, 400 Gbps ports
    Packet time = 9000 × 8 / 400 Gbps = 0.18 µs
    PFC reaction time (round-trip + processing) ≈ 3 µs
    Burst per sender = 3 µs / 0.18 µs × 9000 bytes ≈ 150 KB
    Total buffer for 32 senders = 32 × 150 KB = 4.8 MB per port
    
  Deep buffer switches (Memory ≥ 100 MB): sufficient
  Shallow buffer switches (Memory ≤ 16 MB): will drop under incast!

## "What is head-of-line blocking and how does it affect GPU training?"

→ In a switch, if one output port is congested (paused by PFC),
  packets queued behind the stalled packets for OTHER destinations
  also get delayed — even though their destination is free.

  Impact: One slow GPU stalls traffic to ALL GPUs through that switch.
  
  Fix: Virtual Output Queues (VOQ) — separate queue per output port.
  Modern switches (Tomahawk 4, Spectrum-4) use VOQ architecture.
  Older switches suffer HOL blocking — avoid for training fabrics.

## "Compare InfiniBand vs RoCE v2 for a 1000-GPU training cluster"

→ InfiniBand:
  + Guaranteed lossless (credit-based, no PFC needed)
  + Adaptive routing built-in (handles congestion at switch level)
  + SHARP in-network reduction (50% less data on fabric)
  + Proven at scale (most TOP500 systems)
  + Deterministic latency (~1.3 µs)
  - Expensive (specialized switches, cables, requires subnet manager)
  - Vendor lock-in (NVIDIA/Mellanox)
  - Separate fabric from Ethernet (two networks to manage)

→ RoCE v2:
  + Runs on standard Ethernet (shared infrastructure, cheaper)
  + Familiar tools and operational model
  + Converged network (one fabric for everything)
  + Multiple vendors (Broadcom, Intel, Mellanox)
  - Requires careful PFC/ECN tuning (complexity moves to config)
  - PFC deadlock risk at scale
  - No native adaptive routing (relies on ECMP)
  - No SHARP equivalent on Ethernet
  - Slightly higher latency (~2-3 µs)

→ Recommendation:
  <100 GPUs: RoCE v2 (good enough, cheaper, simpler)
  100-1000 GPUs: Either (depends on team expertise)
  >1000 GPUs: InfiniBand (proven scale, SHARP, adaptive routing)

## "How does NCCL overlap communication with computation?"

→ NCCL + PyTorch DDP uses gradient bucketing:
  1. Backward pass computes gradients layer-by-layer (last → first)
  2. Gradients are grouped into "buckets" (~25 MB each)
  3. As soon as a bucket is full, all-reduce starts for that bucket
  4. Meanwhile, backward pass continues computing earlier layers
  5. By the time backward pass finishes, most all-reduces are already done

  Overlap efficiency depends on:
  - Bucket size: too small = too many collectives, too large = late start
  - Network bandwidth vs compute speed ratio
  - Model architecture (uniform layers overlap better)

  Environment variables:
    TORCH_NCCL_ASYNC_ERROR_HANDLING=1
    Bucket size controlled by: bucket_cap_mb in DDP constructor
```

---

# SECTION 18: RCCL — AMD's COLLECTIVE COMMUNICATION FOR INFERENCE

---

## 18.1 RCCL vs NCCL: What Changes for AMD

```
RCCL (ROCm Communication Collectives Library):
  AMD's equivalent of NCCL for multi-GPU communication
  API-compatible with NCCL (same collective ops, same semantics)
  Targets AMD Instinct GPUs (MI250X, MI300X, MI350X)

Key differences from NCCL:
  | Aspect | NCCL (NVIDIA) | RCCL (AMD) |
  |--------|---------------|------------|
  | Transport | NVLink + PCIe | Infinity Fabric + PCIe + XGMI |
  | GPU interconnect | NVLink 4.0 (900 GB/s) | XGMI 3.0 (896 GB/s MI300X) |
  | Intra-node topology | NVSwitch full mesh | Infinity Fabric mesh |
  | RDMA support | GPUDirect RDMA | ROCm RDMA (kernel module) |
  | Profiling | NCCL_DEBUG + Nsight | RCCL_DEBUG + rocprof |
  | Environment prefix | NCCL_ | RCCL_ (mostly same names) |

For inference with Tensor Parallelism:
  Multi-GPU inference (70B model on 4×MI300X):
  - TP splits model across 4 GPUs
  - Each decode step: all-reduce across 4 GPUs via XGMI
  - Latency per all-reduce: ~5-8 µs (intra-node XGMI)
  - Same continuous batching logic as NVIDIA (vLLM supports ROCm)
```

## 18.2 RCCL Environment Variables for Inference Tuning

```
# Core RCCL tuning (mirror of NCCL variables):
RCCL_SOCKET_IFNAME=eth0          # Network interface for inter-node
RCCL_IB_HCA=mlx5_0              # InfiniBand HCA selection
RCCL_NET_GDR_LEVEL=5            # GPU Direct RDMA level
RCCL_P2P_LEVEL=SYS              # Peer-to-peer topology level
RCCL_DEBUG=INFO                  # Debug logging

# AMD-specific tuning:
HSA_FORCE_FINE_GRAIN_PCIE=1     # Fine-grained PCIe for small messages
GPU_MAX_HW_QUEUES=16            # Hardware queue depth per GPU
HIP_VISIBLE_DEVICES=0,1,2,3    # GPU visibility (like CUDA_VISIBLE_DEVICES)

# For multi-GPU inference (TP=4):
RCCL_ALGO=Ring                   # Ring vs Tree (Ring better for small TP groups)
RCCL_PROTO=Simple                # Simple vs LL vs LL128

# Performance comparison (intra-node, 4 GPUs):
  AllReduce 128 MB:
    NCCL on 4×H100 (NVSwitch): ~0.15 ms
    RCCL on 4×MI300X (XGMI):   ~0.16 ms (nearly identical)
  
  AllReduce 1 MB (TP per-layer sync):
    NCCL: ~8 µs
    RCCL: ~9 µs
    → For inference TP, communication is NOT the bottleneck (compute-bound)
```

## 18.3 Multi-GPU Inference Communication Patterns

```
Tensor Parallelism communication (per decode step):

  For each transformer layer:
    1. Split input across TP GPUs (scatter or already split)
    2. Each GPU computes partial GEMM (attention/FFN)
    3. All-Reduce to combine results (small message: hidden_dim × batch × dtype)
    4. Repeat for next layer

  Message size per all-reduce:
    hidden_dim=8192, batch=32, dtype=FP16 (2 bytes)
    Size = 8192 × 32 × 2 = 512 KB per all-reduce
    
    Layers=80 (70B model): 80 × 2 (attention + FFN) = 160 all-reduces per step
    Total bandwidth: 160 × 512 KB = 80 MB per decode step
    At 50 ms per step: 80 MB / 0.05 s = 1.6 GB/s needed
    
    XGMI bandwidth: 896 GB/s → using 0.18% of bandwidth
    → Communication is NOT bottleneck for inference TP (it's for training)

Inter-node inference (Pipeline Parallelism for very large models):
  Message = activations between pipeline stages
  Size = batch × seq_len × hidden_dim × dtype
  One message per micro-batch per step
  Uses RDMA (RoCE or InfiniBand) between nodes
  Latency: 5-20 µs (acceptable for LLM decode at 30-50 ms per token)
```

## 18.4 AMD GPU Direct RDMA for Inference

```
ROCm RDMA (amdgpu kernel module + OFED):
  GPU memory → NIC → network → NIC → remote GPU memory
  Bypasses CPU and system memory entirely
  
  Required for: multi-node inference (PP across nodes)
  Setup:
    1. Install ROCm + OFED drivers
    2. Load amdkfd module with GPU Direct support
    3. Register GPU memory with RDMA: hipIpcGetMemHandle()
    4. NIC reads/writes GPU HBM directly via BAR aperture

  Performance:
    Without GPU Direct: GPU → CPU mem → NIC → network → NIC → CPU mem → GPU
    Latency: ~50 µs, BW limited by PCIe (64 GB/s)
    
    With GPU Direct: GPU → NIC → network → NIC → GPU
    Latency: ~5 µs, BW = NIC line rate (400 Gbps = 50 GB/s)

  For inference specifically:
    Only needed when model > single-node GPU memory
    70B FP8 = 70 GB → fits in 1 node (4×MI300X with 192 GB each)
    405B FP8 = 405 GB → needs 2+ nodes → GPU Direct RDMA critical
```
