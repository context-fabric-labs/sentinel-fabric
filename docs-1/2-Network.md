
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
