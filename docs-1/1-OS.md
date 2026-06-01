# Layer 1: Linux Systems & Performance — Complete Deep Dive

## "How does the host OS behave under our workload?"

> **Why this layer matters:** Every AI/HPC workload runs on Linux.
> No matter how well you optimize your GPU kernels (Layer 4), your
> model serving (Layer 6), or your data flow (Layer 5), if the HOST
> OS is fighting you — scheduling your hot thread off its CPU, placing
> memory on the wrong NUMA node, or thrashing the TLB — your p99
> will be unpredictable and your throughput will suffer.
>
> **The core insight:** Layer 3 problems manifest as SYMPTOMS in higher
> layers. A "GPU inference latency spike" might actually be a CFS
> scheduling cliff. A "model loading is slow" might actually be NUMA
> remote memory access. A "feature store lookup is slow" might actually
> be TLB miss storms on a large hash table.
>
> A strong systems engineer investigates DOWN to Layer 3 before blaming
> higher layers.

---

# SECTION 1: CPU SCHEDULING

---

## 1.1 CFS (Completely Fair Scheduler)

### What it is

CFS is the default Linux CPU scheduler for normal (non-realtime) processes.
Its goal is to give every runnable thread a "fair share" of CPU time
proportional to its weight (nice value).

### How it works (simplified)

CFS maintains a virtual runtime for each runnable thread. The thread with
the LOWEST virtual runtime gets to run next. As a thread runs, its virtual
runtime increases. When it exceeds others, it gets preempted and another
thread runs.

```
Thread A: vruntime = 100 ms  (has run a lot → low priority)
Thread B: vruntime = 50 ms   (hasn't run much → high priority)
Thread C: vruntime = 75 ms   (in between)

CFS picks: Thread B (lowest vruntime)
Thread B runs for its time slice...
Thread B: vruntime = 50 + slice → 58 ms
Now Thread C has lowest → CFS picks C
... and so on
```

### Why it matters for HPC

CFS is designed for FAIRNESS, not for LATENCY. It doesn't know that your
fraud-scoring thread is more important than a log rotation process. If
both are runnable, CFS gives them "fair" time — which means your hot
thread can get preempted by a random background task.

### Key parameters

```
/proc/sys/kernel/sched_min_granularity_ns  — minimum time slice (default: 3 ms)
/proc/sys/kernel/sched_latency_ns          — scheduling period (default: 24 ms)
/proc/sys/kernel/sched_wakeup_granularity_ns — preemption granularity
```

---

## 1.2 CFS Cliff

### What it is

A CFS cliff occurs when the number of RUNNABLE threads significantly exceeds
the number of available CPU cores. The scheduler must rapidly switch between
threads, causing:

- massive increase in context switches
- cache working set disruption (each thread has different cache needs)
- TLB invalidation on each switch
- CPU pipeline flushes
- unpredictable and much higher p99 latency

### The signature

```
HEALTHY:
  8 cores, 8 runnable threads
  Context switches: ~200/sec
  p99 latency: 3 ms

CFS CLIFF:
  8 cores, 64 runnable threads (8x oversubscribed)
  Context switches: 45,000/sec (225x increase!)
  p99 latency: 28 ms (9x worse!)
```

### Why it happens

When threads > cores:

- CFS must time-slice between many threads on each core
- each context switch flushes the thread's cache state
- the next thread loads ITS cache state (cold cache)
- when the original thread comes back, its data is evicted
- → THRASHING: every thread runs on a cold cache every time

### How to detect it

```bash
# Quick check: count runnable threads vs cores
nproc                              # number of cores
ps -eo state | grep -c R          # number of runnable threads

# If runnable >> cores, you have potential CFS cliff

# Measure context switches
perf stat -e context-switches,cpu-migrations -p $PID -- sleep 10

# Watch live
vmstat 1
# Look at 'r' column (runnable) and 'cs' column (context switches)

# Per-process detail
pidstat -w 1

# Per-core view
mpstat -P ALL 1

# eBPF: run queue latency histogram
sudo runqlat 10 1
# Shows how long threads WAIT before getting a CPU

# eBPF: detect high run queue latency events
sudo runqslower 1000
# Alerts when any thread waits > 1000 µs for a CPU
```

### How to fix it

**Fix 1: Reduce thread count**
The simplest and most effective fix. Match worker threads to available cores.

```cpp
int num_workers = std::thread::hardware_concurrency();
// or even fewer if you need cores for other work
int num_workers = num_isolated_cpus;  // e.g., 8 if you isolated 8 cores
```

**Fix 2: Pin threads to cores**
Prevent the scheduler from migrating threads between cores.

```cpp
void pin_to_cpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}
```

**Fix 3: Isolate CPUs**
Remove your hot CPUs from the general scheduler pool.

```bash
# Boot parameter
GRUB_CMDLINE_LINUX="isolcpus=4-15 nohz_full=4-15 rcu_nocbs=4-15"
```

**Fix 4: Separate IO from compute**
Don't let blocking I/O threads share cores with compute threads.

```
CPU 0-3:  OS + I/O threads + logging
CPU 4-11: compute-only workers (isolated, pinned)
CPU 12-15: GPU feeding threads (isolated, pinned)
```

### Interview example

> "We had a fraud scoring service with 64 goroutines on 8 cores. perf stat
> showed 45,000 context switches/sec. I reduced workers to 8, pinned each
> to an isolated core, and context switches dropped to 200/sec. p99 went
> from 28 ms to 3 ms. The model didn't change — the OS was the bottleneck."

---

## 1.3 Context Switches — Why They Kill p99

### What happens during a context switch

```
Thread A is running on Core 5:
  - A's data is warm in L1/L2 cache
  - A's page translations are in TLB
  - A's branch predictions are warm in branch predictor
  - A is executing efficiently

CONTEXT SWITCH occurs (CFS preempts A, schedules B):
  1. Save A's registers (stack pointer, instruction pointer, etc.)
  2. Switch page tables (if different process)
  3. Load B's registers
  4. B starts executing with:
     - COLD L1/L2 cache (A's data, not B's)
     - COLD TLB (A's translations, not B's)
     - COLD branch predictor (A's patterns, not B's)
  5. B suffers cache misses, TLB misses, branch mispredictions
     for the first few hundred microseconds
```

### The cost

- Each context switch: ~1-10 µs for the switch itself
- But the CACHE WARMING penalty: ~50-500 µs of degraded performance
- At 45,000 switches/sec, you are ALWAYS running on cold cache

### How to measure

```bash
# Count context switches
perf stat -e context-switches -p $PID -- sleep 10

# Voluntary vs involuntary
perf stat -e context-switches:u,context-switches:k -p $PID -- sleep 10

# See which threads are switching most
pidstat -w -t -p $PID 1
```

### Voluntary vs involuntary context switches

- **Voluntary:** thread CHOSE to give up CPU (blocked on I/O, mutex, sleep)
  → usually fine, means thread has nothing to do
- **Involuntary:** scheduler FORCED thread off CPU (time slice expired, higher priority)
  → BAD for latency — means your hot thread was preempted

```bash
# Check per-thread
cat /proc/$PID/status | grep ctxt
# voluntary_ctxt_switches: 1234
# nonvoluntary_ctxt_switches: 5678  ← if high, problem!
```

---

## 1.4 CPU Isolation

### The four boot parameters

**isolcpus=4-15**
Removes CPUs 4-15 from the general scheduler domain. The scheduler will NOT
place arbitrary tasks on these CPUs. Only tasks explicitly pinned to them
(via sched_setaffinity or cpuset) will run there.

**nohz_full=4-15**
Disables the periodic scheduler tick on CPUs 4-15 when they are running a
single task. The tick normally fires every 1-4 ms to check if the thread
should be preempted. With nohz_full, no tick → no 1 ms jitter.

Important: only works when ONE runnable task is on the CPU. If two or more
threads are runnable, the tick comes back (to time-slice between them).

**rcu_nocbs=4-15**
Offloads RCU (Read-Copy-Update) callback processing from CPUs 4-15. RCU is
a kernel synchronization mechanism that defers work via callbacks. Without
this, RCU callbacks can run on your isolated CPUs and cause jitter.

**irqaffinity=0-3**
Directs hardware interrupts to CPUs 0-3 only. Without this, NIC interrupts,
disk interrupts, and timer interrupts can land on your isolated CPUs.

### The complete isolation stack

```bash
# /etc/default/grub
GRUB_CMDLINE_LINUX="isolcpus=4-15 nohz_full=4-15 rcu_nocbs=4-15 irqaffinity=0-3"

# After reboot, verify:
cat /sys/devices/system/cpu/isolated
# Expected: 4-15

cat /proc/cmdline
# Should show all four parameters
```

### How it works with Kubernetes

```yaml
# Kubelet configuration
apiVersion: kubelet.config.k8s.io/v1beta1
kind: KubeletConfiguration
cpuManagerPolicy: static          # exclusive CPU pinning for Guaranteed pods
topologyManagerPolicy: single-numa-node  # NUMA alignment
reservedSystemCPUs: "0-3"         # keep 0-3 for kubelet/OS
```

K8s CPU Manager assigns isolated CPUs to Guaranteed pods.
Boot parameters remove kernel noise from those CPUs.
Together: your pod gets quiet, dedicated, isolated CPUs.

---

## 1.5 Thread Pinning

### Why pinning matters

Without pinning, even on isolated CPUs, the scheduler can MIGRATE your
thread between allowed CPUs. Each migration means:

- cache cold on the new CPU
- TLB cold on the new CPU
- potential NUMA node change

### Application-level pinning (C++)

```cpp
#include <pthread.h>
#include <sched.h>

// Pin current thread to specific CPU
void pin_to_cpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        throw std::runtime_error("Failed to pin to CPU " + std::to_string(cpu));
    }
}

// Verify current CPU
int get_current_cpu() {
    return sched_getcpu();
}

// Pin at thread creation time
void create_pinned_thread(int cpu, void* (*func)(void*), void* arg) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
  
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
  
    pthread_t thread;
    pthread_create(&thread, &attr, func, arg);
    pthread_attr_destroy(&attr);
}
```

### Worker-to-core mapping pattern

```cpp
// Map each worker to a specific isolated CPU
struct WorkerConfig {
    int worker_id;
    int cpu;
    int numa_node;
};

std::vector<WorkerConfig> workers = {
    {0, 4, 0},   // worker 0 → CPU 4, NUMA node 0
    {1, 5, 0},   // worker 1 → CPU 5, NUMA node 0
    {2, 6, 0},   // worker 2 → CPU 6, NUMA node 0
    {3, 7, 0},   // worker 3 → CPU 7, NUMA node 0
};

for (auto& w : workers) {
    std::thread t([&w]() {
        pin_to_cpu(w.cpu);
        // allocate memory on w.numa_node
        // run worker loop
    });
}
```

---

# SECTION 2: MEMORY SYSTEM

---

## 2.1 NUMA (Non-Uniform Memory Access)

### What it is

On multi-socket servers, each CPU socket has its own LOCAL memory. Accessing
local memory is fast; accessing memory on ANOTHER socket (remote) is slower
because it must cross the interconnect (QPI/UPI on Intel, Infinity Fabric
on AMD).

```
Socket 0                          Socket 1
┌──────────────────┐              ┌──────────────────┐
│ CPUs 0-15        │              │ CPUs 16-31       │
│                  │              │                  │
│ LOCAL memory     │←── fast ───→│ LOCAL memory     │
│ (NUMA node 0)    │   (local)   │ (NUMA node 1)    │
│ 64 GB            │              │ 64 GB            │
└──────────────────┘              └──────────────────┘
        ↕ slow (remote, 2-3x latency)  ↕
```

### The numbers

```
Local memory access:    ~80-100 ns
Remote memory access:   ~150-300 ns (1.5-3x slower)
Local bandwidth:        ~50-80 GB/s per socket
Remote bandwidth:       ~20-40 GB/s (cross-socket)
```

### Why it matters for HPC

If your fraud-scoring thread runs on CPU 4 (NUMA node 0) but its feature
data and model weights are on NUMA node 1, EVERY memory access pays the
remote penalty. For a workload that does millions of memory accesses per
second, this can mean 30-50% performance degradation.

### First-touch policy

Linux allocates physical memory pages on the NUMA node where the thread
that FIRST TOUCHES (writes to) the page is running. This means:

- if thread on CPU 4 (node 0) initializes the array → pages on node 0
- if thread on CPU 20 (node 1) later uses the array → REMOTE access!

The fix: initialize memory from the SAME thread/CPU that will use it.

### How to discover your NUMA topology

```bash
# See NUMA nodes and CPUs
numactl --hardware
# node 0 cpus: 0 1 2 3 4 5 6 7
# node 0 size: 65536 MB
# node 1 cpus: 8 9 10 11 12 13 14 15
# node 1 size: 65536 MB

# IMPORTANT: CPU numbering is NOT always sequential per node!
# Always check with lscpu or numactl --hardware

lscpu | grep -i numa
# NUMA node0 CPU(s): 0-13,28-41  ← split ranges with hyperthreading!
# NUMA node1 CPU(s): 14-27,42-55

# Detailed topology
lstopo  # from hwloc package — shows full CPU/cache/NUMA/GPU/NIC hierarchy
```

### How to detect NUMA problems

```bash
# Check per-process NUMA allocation
numastat -p $PID
# Expected: most allocations on LOCAL node
# Bad sign: high numa_miss or other_node counts

# Controlled experiment
# Good case: CPU and memory on same node
numactl --cpunodebind=0 --membind=0 ./my_service

# Bad case: CPU on node 0, memory FORCED to node 1
numactl --cpunodebind=0 --membind=1 ./my_service

# Compare latency/throughput between the two
```

### How to fix NUMA problems

**Fix 1: Bind CPU and memory to same node**

```bash
numactl --cpunodebind=0 --membind=0 ./my_service
```

**Fix 2: In-code NUMA-aware allocation**

```cpp
#include <numa.h>

// Allocate on specific NUMA node
void* alloc_on_node(size_t bytes, int node) {
    void* ptr = numa_alloc_onnode(bytes, node);
    memset(ptr, 0, bytes);  // first-touch on THIS thread/node
    return ptr;
}
```

**Fix 3: Initialize from the right thread**

```cpp
// Pin thread first, THEN allocate and initialize
pin_to_cpu(4);  // CPU 4 is on NUMA node 0
float* features = new float[1000000];
memset(features, 0, sizeof(float) * 1000000);  // pages allocated on node 0
```

**Fix 4: In Kubernetes**

```yaml
# Topology Manager ensures NUMA alignment
topologyManagerPolicy: single-numa-node
```

---

## 2.2 NUMA Cliff

### The signature

```
HEALTHY (local memory):
  Throughput: 50 GB/s
  Latency: 80 ns
  numastat: 99% numa_hit

NUMA CLIFF (remote memory):
  Throughput: 25 GB/s (50% drop!)
  Latency: 200 ns (2.5x worse!)
  numastat: 40% numa_miss
```

### When NUMA cliffs happen in practice

- Thread migrated to different socket by scheduler → accessing remote memory
- Large allocation done by main thread on node 0, worker threads on node 1
- Model weights loaded on wrong node
- Shared memory arena allocated without NUMA awareness
- Kubernetes pod scheduled without topology manager

---

## 2.3 TLB (Translation Lookaside Buffer)

### What it is

Every memory access uses a VIRTUAL address that must be translated to a
PHYSICAL address. The TLB is a small hardware cache of recent translations.

```
CPU wants to access virtual address 0x7f4a8b120000:
  TLB lookup: is this address cached?
    YES (TLB hit) → physical address found instantly (~1-2 cycles)
    NO (TLB miss) → page table walk needed (~10-100 cycles)
      → walk 4 levels of page tables in memory
      → PML4 → PDPT → PD → PT → physical address
      → each level may itself cause a cache miss
```

### Why TLB is scarce

A typical CPU has only ~1,000-4,000 TLB entries.

With 4 KB pages:
  1,024 entries × 4 KB = 4 MB coverage

If your working set (model weights, feature tables, hash maps) is
larger than 4 MB, you WILL get TLB misses on many accesses.

### The TLB cliff

```
Working set: 512 MB feature table

With 4 KB pages:
  512 MB / 4 KB = 131,072 pages
  TLB has ~1,024 entries
  TLB can cover: 4 MB of 512 MB (0.8%)
  Almost EVERY access misses TLB
  → constant page table walks → 30-50% performance loss

With 2 MB hugepages:
  512 MB / 2 MB = 256 pages
  TLB has ~1,024 entries
  TLB can cover: 2 GB (more than enough!)
  Almost EVERY access hits TLB
  → near-zero page table walks → full performance
```

### How to detect TLB problems

```bash
# Count TLB misses
perf stat -e dTLB-load-misses,dTLB-store-misses,cycles,instructions \
    -p $PID -- sleep 10

# Compare with contiguous vs random access patterns
# If dTLB-load-misses is high, you have a TLB problem
```

---

## 2.4 Hugepages

### The math (why hugepages work)

| Page size | TLB entries | Coverage | vs 4 KB  |
| --------- | ----------- | -------- | -------- |
| 4 KB      | 1,024       | 4 MB     | 1x       |
| 2 MB      | 1,024       | 2 GB     | 512x     |
| 1 GB      | 1,024       | 1 TB     | 262,144x |

### Two flavors

**HugeTLB (explicit, recommended for HPC)**

- Pre-allocated at boot or runtime
- Deterministic — no compaction surprises
- Must be requested explicitly (mmap with MAP_HUGETLB)
- Not swappable

```bash
# Reserve at boot
GRUB_CMDLINE_LINUX="hugepagesz=2M hugepages=1024"

# Or at runtime
echo 1024 > /proc/sys/vm/nr_hugepages

# Verify
cat /proc/meminfo | grep Huge
```

```cpp
// Use in C++
void* ptr = mmap(nullptr, size,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                -1, 0);
```

**THP (Transparent Huge Pages)**

- Automatic — kernel promotes 4 KB pages to 2 MB when possible
- No code changes needed
- BUT: compaction can cause latency spikes
- Many databases DISABLE THP for this reason

```bash
# Check THP status
cat /sys/kernel/mm/transparent_hugepage/enabled
# [always] madvise never

# Disable THP (common for latency-sensitive workloads)
echo never > /sys/kernel/mm/transparent_hugepage/enabled
```

### When to use which

```
HPC hot path with pre-allocated arenas → HugeTLB (deterministic)
General application heap                → THP or neither
Database (Redis, Postgres, MongoDB)     → DISABLE THP (latency spikes)
DPDK / RDMA buffers                     → HugeTLB (required)
Kubernetes pod with hugepage request    → HugeTLB (pre-allocated on node)
```

### Why NOT always use hugepages

- Reserved memory is unavailable to other workloads
- Internal fragmentation (5 KB allocation uses full 2 MB page)
- Not swappable (stuck in RAM)
- Copy-on-write becomes expensive (2 MB per COW fault vs 4 KB)
- Allocation can fail if no contiguous physical memory available

---

## 2.5 Memory-Mapped Files and Shared Memory

### mmap

Maps a file or memory object into the process's virtual address space.

```cpp
// File-backed mmap
int fd = open("data.bin", O_RDONLY);
void* ptr = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
// Now 'ptr' reads directly from the file via page cache — no read() calls

// Anonymous mmap (just memory, no file)
void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
```

### POSIX shared memory

For cross-process zero-copy IPC:

```cpp
// Process A: create shared region
int fd = shm_open("/my_arena", O_CREAT | O_RDWR, 0600);
ftruncate(fd, size);
void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Process B: attach to same region
int fd = shm_open("/my_arena", O_RDWR, 0600);
void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Both processes now read/write the SAME physical memory
// Zero-copy IPC!
```

### Key rules for shared memory

- Use OFFSETS, not raw pointers (different virtual addresses per process)
- Use hugepage backing for large arenas (MAP_HUGETLB)
- Align allocations to cache lines (64 bytes) to prevent false sharing
- Use atomics or SPSC rings for signaling, not the data itself

---

# SECTION 3: PROFILING TOOLS

---

## 3.1 Quick Reference: Which Tool for Which Problem

| Problem                       | First tool                              | Deep-dive tool      | What to look for                |
| ----------------------------- | --------------------------------------- | ------------------- | ------------------------------- |
| **CFS cliff**           | `perf stat` (context-switches)        | BCC `runqlat`     | cs > 10K/sec, run queue > cores |
| **CPU migration**       | `perf stat` (cpu-migrations)          | `mpstat -P ALL`   | migrations > 100/sec            |
| **TLB cliff**           | `perf stat` (dTLB-load-misses)        | VTune Memory Access | dTLB misses high, IPC drops     |
| **NUMA cliff**          | `numastat -p`                         | VTune Memory Access | numa_miss > 10%, remote allocs  |
| **Cache misses**        | `perf stat` (cache-misses)            | VTune/uProf         | L3 miss rate > 5%               |
| **Where CPU time goes** | `perf record` + flamegraph            | VTune Hotspots      | unexpected hot functions        |
| **Thread contention**   | `perf stat` (cs) + BCC `offcputime` | VTune Threading     | threads blocked on locks        |
| **Scheduler delays**    | BCC `runqlat`, `runqslower`         | Perfetto timeline   | run queue latency > 100 µs     |
| **Block I/O stalls**    | BCC `biolatency`                      | `blktrace`        | high I/O latency histogram      |
| **Network issues**      | BCC `tcpretrans`                      | Wireshark           | retransmissions, RTT spikes     |

## 3.2 The Standard Investigation Workflow

```
Step 1: BASELINE (application-level)
  Measure p50/p99 latency and throughput
  Identify WHICH stage is slow (per-stage timing)

Step 2: SYSTEM OVERVIEW (vmstat, mpstat, pidstat)
  vmstat 1    → runnable count, context switches, memory
  mpstat -P ALL 1  → per-core utilization
  pidstat 1   → per-process CPU, I/O

Step 3: HARDWARE COUNTERS (perf stat)
  perf stat -e cycles,instructions,context-switches,
    cpu-migrations,cache-misses,dTLB-load-misses
    -p $PID -- sleep 10

Step 4: IDENTIFY THE DOMINANT COUNTER
  High context-switches? → CFS cliff → reduce threads, pin
  High dTLB-misses?      → TLB cliff → hugepages, improve locality
  High cache-misses?     → data locality issue → restructure data
  High cpu-migrations?   → thread pinning needed

Step 5: DEEP-DIVE with specialized tool
  CFS:  runqlat, runqslower, offcputime
  TLB:  VTune Memory Access
  NUMA: numastat, VTune Memory Access
  Scheduler: Perfetto timeline

Step 6: APPLY FIX

Step 7: RE-MEASURE (same perf stat command)
  Verify the dominant counter improved
  Verify p50/p99 improved
  Document before/after
```

## 3.3 The Minimum Profiling Commands Everyone Should Know

```bash
# 1. "Is the CPU oversubscribed?"
vmstat 1
# Look at: r (runnable) > number of cores = oversubscribed

# 2. "How many context switches?"
perf stat -e context-switches -p $PID -- sleep 10
# > 10,000/sec on a single service = likely CFS cliff

# 3. "Is there a TLB problem?"
perf stat -e dTLB-load-misses -p $PID -- sleep 10
# Compare contiguous vs random access workloads

# 4. "Is NUMA causing problems?"
numastat -p $PID
# High numa_miss or other_node = remote memory access

# 5. "Where is CPU time going?"
perf record -g -p $PID -- sleep 10
perf report
# Or generate a flamegraph

# 6. "Where are threads blocked?"
sudo offcputime -p $PID 10
# Shows stack traces of WHERE threads are sleeping/waiting

# 7. "Is the scheduler delaying my threads?"
sudo runqlat 10 1
# Histogram of how long threads wait for a CPU

# 8. "Are there network retransmits?"
sudo tcpretrans
# Shows TCP retransmissions in real time

# 9. "Is disk I/O slow?"
sudo biolatency 1
# Block I/O latency histogram

# 10. "What is the IPC (instructions per cycle)?"
perf stat -e cycles,instructions -p $PID -- sleep 10
# IPC < 0.5 = likely memory-bound or stalling
# IPC > 1.0 = reasonably compute-efficient
```

---

# SECTION 4: HOW IT ALL CONNECTS TO YOUR STORIES

---

## In your fraud scoring hot path

```
Layer 3 contribution:
  ├── CPU isolation: isolcpus + nohz_full + rcu_nocbs for scoring workers
  ├── Thread pinning: each scoring worker pinned to specific isolated CPU
  ├── NUMA alignment: workers on same NUMA node as GPU
  ├── Hugepage arena: 512 MB shared memory with 2 MB hugepages
  ├── First-touch: memory initialized by the worker thread that uses it
  └── Profiling: perf stat before/after to prove improvement
```

## In your Siri pipeline

```
Layer 3 contribution:
  ├── Per-stage workers pinned to isolated CPUs
  ├── Shared memory arena hugepage-backed for TLB efficiency
  ├── NUMA-aware buffer allocation (arena on same node as GPU)
  ├── Context switch monitoring to verify isolation effectiveness
  └── eBPF runqlat to detect scheduler interference
```

## In your Kubernetes GPU serving

```
Layer 3 contribution:
  ├── Boot params: isolcpus + nohz_full + rcu_nocbs + irqaffinity
  ├── Kubelet: CPU Manager static + Topology Manager single-numa-node
  ├── Pod spec: Guaranteed QoS + integer CPUs + hugepage volumes
  ├── Verification: numastat + perf stat inside the container
  └── eBPF DaemonSet: runqlat + offcputime for scheduler monitoring
```

---

# SECTION 5: THE INTERVIEW CHEAT SHEET

## CFS cliff

**Detect:** `perf stat -e context-switches` → > 10K/sec
**Root cause:** runnable threads > cores
**Fix:** reduce threads + pin + isolate
**Verify:** context switches drop, p99 drops

## TLB cliff

**Detect:** `perf stat -e dTLB-load-misses` → high misses, IPC drops
**Root cause:** working set exceeds TLB coverage with 4 KB pages
**Fix:** hugepages (2 MB/1 GB) for large arenas/buffers
**Verify:** dTLB misses drop, IPC improves

## NUMA cliff

**Detect:** `numastat -p $PID` → high numa_miss / other_node
**Root cause:** memory on wrong NUMA node
**Fix:** bind CPU + memory to same node, first-touch discipline
**Verify:** numa_miss drops, throughput improves

## The golden rule

> **Measure with counters. Fix the dominant counter.
> Re-measure to prove improvement. Document before/after.**

---
---

# SECTION 6: CACHE LINE MECHANICS & FALSE SHARING

---

## 6.1 Cache Line Fundamentals

```
CPU cache operates in LINES of 64 bytes (on x86).
You can't load 1 byte — you always load the entire 64-byte line.

Impact on data structures:
  struct CounterBad {
      std::atomic<uint64_t> counter_a;  // bytes 0-7
      std::atomic<uint64_t> counter_b;  // bytes 8-15
  };
  // BOTH counters share the same cache line!
  // Thread A writes counter_a → invalidates counter_b in Thread B's cache
  // Thread B writes counter_b → invalidates counter_a in Thread A's cache
  // = FALSE SHARING: both threads thrash each other's cache line

  struct CounterGood {
      alignas(64) std::atomic<uint64_t> counter_a;  // own cache line
      alignas(64) std::atomic<uint64_t> counter_b;  // own cache line
  };
  // Each counter on its own cache line — no interference!
```

## 6.2 False Sharing — Detection and Fix

```
Symptoms:
  - Two threads with independent data run slower together than apart
  - perf c2c (cache-to-cache) shows HITM (Hit-in-Modified) events
  - IPC drops when multiple threads write "independent" variables

Detection:
  perf c2c record -p $PID -- sleep 10
  perf c2c report
  # Shows: which cache lines have the most contention (HITM)
  # "Shared Data Cache Line Table" shows the hot lines
  # Cross-reference with your struct layout

  # Alternative: perf stat
  perf stat -e l2_rqsts.all_demand_data_rd,l2_rqsts.demand_data_rd_hit \
    -p $PID -- sleep 10

Fix patterns:
  1. Padding: alignas(64) or __attribute__((aligned(64)))
  2. Per-thread copies: each thread has its own counter, merge at end
  3. Rearrange struct: group read-mostly vs write-mostly fields
  4. __cacheline_aligned (Linux kernel macro)
```

## 6.3 Cache Hierarchy Performance Numbers

```
                    Latency        Bandwidth
L1 cache (32 KB):   ~4 cycles      ~1 TB/s (per core)
L2 cache (256 KB):  ~12 cycles     ~500 GB/s
L3 cache (32 MB):   ~40 cycles     ~200 GB/s (shared across cores)
Main memory (DDR5): ~100-200 cycles ~50-80 GB/s (per socket)

For HPC:
  If your critical data fits in L1: 4 cycle access → ~1.5 ns
  If it falls to main memory: 200 cycle access → ~70 ns
  That's a 47× difference!

Strategies to stay in cache:
  - Keep hot data small (fit in L1/L2)
  - Sequential access patterns (hardware prefetcher works)
  - Avoid pointer chasing (linked lists → bad cache behavior)
  - Use structure-of-arrays (SoA) not array-of-structures (AoS)
  - Align critical structures to cache lines
```

## 6.4 Prefetching

```
Hardware prefetcher:
  CPU detects sequential/strided access patterns and prefetches next lines.
  Works great for: array traversal, matrix operations, streaming reads.
  Fails for: random access, pointer chasing, hash table lookups.

Software prefetch (manual hint):
  __builtin_prefetch(addr, 0, 3);  // prefetch for read, high temporal locality
  
  // Prefetch next iteration's data while processing current:
  for (int i = 0; i < N; i++) {
      __builtin_prefetch(&data[i + PREFETCH_DISTANCE], 0, 1);
      process(data[i]);
  }
  
  PREFETCH_DISTANCE depends on:
    memory latency (cycles) / loop body size (cycles)
    Typical: 8-16 iterations ahead

When to use:
  - Known access pattern with predictable but non-sequential addresses
  - Hash table probing (prefetch next bucket while processing current)
  - Tree traversal (prefetch both children while processing parent)
```

---

# SECTION 7: MEMORY ALLOCATOR INTERNALS

---

## 7.1 Why malloc() Matters for Latency

```
Standard glibc malloc:
  - Single global arena with lock → thread contention
  - Each malloc/free may acquire a lock → blocks other threads
  - free() may trigger madvise(MADV_DONTNEED) → page fault later
  - mmap() for large allocations → expensive syscall

For HPC hot paths:
  A SINGLE lock-contention event in malloc can add 5-50 µs
  If this happens at p99, your 10 ms budget just lost 0.5-5%
```

## 7.2 tcmalloc (Google's Thread-Caching Malloc)

```
Architecture:
  ┌─────────────────────────────────────────────┐
  │ Thread-Local Cache (per-thread, lock-free)   │
  │  Small objects: freed/allocated without lock  │
  ├─────────────────────────────────────────────┤
  │ Central Free Lists (per-size-class, locked)  │
  │  Batch transfers to/from thread caches       │
  ├─────────────────────────────────────────────┤
  │ Page Heap (central, locked)                  │
  │  Large allocations, page-level management    │
  └─────────────────────────────────────────────┘

Key properties:
  - Small allocations (< 256 KB): thread-local, NO LOCK
  - Only when thread cache is empty: batch refill from central
  - Size classes: 88 different sizes (8B to 256KB), pre-rounded
  - Reduces fragmentation (fixed size classes)
  - perf: ~20 ns per malloc/free (vs ~100-500 ns for glibc under contention)

Usage:
  LD_PRELOAD=/usr/lib/libtcmalloc.so ./my_service
  OR link at compile time: -ltcmalloc
```

## 7.3 jemalloc (FreeBSD/Meta's Allocator)

```
Architecture:
  ┌─────────────────────────────────────────────┐
  │ Thread-Local Cache (tcache)                  │
  │  Per-thread, lock-free for common sizes      │
  ├─────────────────────────────────────────────┤
  │ Arenas (multiple, round-robin per thread)    │
  │  Each arena has its own lock → reduced contention │
  ├─────────────────────────────────────────────┤
  │ Extents (memory regions)                     │
  │  Backed by mmap or sbrk                     │
  └─────────────────────────────────────────────┘

Key properties:
  - Multiple arenas (default: 4× ncpus) → lock contention divided
  - Thread caches for small objects (similar to tcmalloc)
  - Better memory return to OS (purging, decay)
  - Extensive profiling built-in: heap profiling, leak detection

  Usage:
  LD_PRELOAD=/usr/lib/libjemalloc.so ./my_service
  
  Tuning for HPC:
  export MALLOC_CONF="background_thread:true,narenas:4,dirty_decay_ms:10000"
  # background_thread: async page purging (no allocation-path stalls)
  # narenas:4: reduce arenas for HPC (less wasted memory)
  # dirty_decay_ms: delay returning pages to OS (avoid page faults)
```

## 7.4 Arena / Pool Allocator (Custom — Best for Hot Path)

```
For the absolute lowest latency, bypass malloc entirely:

struct Arena {
    char* base;      // mmap'd at startup (hugepage-backed)
    size_t offset;   // current allocation point (bump pointer)
    size_t capacity;

    void* alloc(size_t size) {
        size = (size + 63) & ~63;  // align to cache line
        void* ptr = base + offset;
        offset += size;
        return ptr;
    }

    void reset() { offset = 0; }  // "free" everything at once
};

Why this is ideal for request processing:
  1. Allocate at startup: mmap + MAP_HUGETLB (one-time cost)
  2. Per-request: bump the pointer (1-2 ns per allocation!)
  3. After request: reset() (free everything in O(1))
  4. No locks (per-worker arena)
  5. No fragmentation (sequential allocation)
  6. No TLB misses (hugepage-backed)
  7. No syscalls (never calls mmap/munmap during hot path)

Your fraud scoring hot path uses this pattern:
  Per-worker arena (512 MB, hugepage-backed)
  All features, intermediate results, model inputs → bump-allocated
  After response sent → arena.reset()
```

## 7.5 When to Use Which Allocator

```
Use case                              Allocator
────────────────────────────────────────────────
Hot path (sub-ms, per-request)       Custom arena (bump+reset)
General service (mixed workload)      jemalloc or tcmalloc
Lots of small objects (< 64 B)        tcmalloc (better small-obj perf)
Need heap profiling                   jemalloc (built-in profiling)
DPDK/RDMA buffers                     Custom pool (hugepage-backed)
Standard Linux (default)              glibc malloc (acceptable for most)
```

---

# SECTION 8: LOCK-FREE DATA STRUCTURES

---

## 8.1 Why Locks Kill Tail Latency

```
Mutex-based queue:
  Thread A: lock() → enqueue → unlock()     [holds lock for 100 ns]
  Thread B: lock() → BLOCKED (A has lock) → ... → got lock → enqueue
  
  If Thread A gets preempted WHILE holding lock:
    Thread B blocks for Thread A's entire preemption period (1-10 ms!)
    This is "priority inversion" and explains many p99.9 spikes

Lock-free queue:
  Thread A: CAS loop → enqueue                [no lock held]
  Thread B: CAS loop → enqueue                [no lock held]
  
  If Thread A gets preempted:
    Thread B is NOT affected (no lock to wait for)
    Worst case: B's CAS retries once (few ns)
```

## 8.2 SPSC Ring Buffer (Single-Producer Single-Consumer)

```cpp
// The fundamental HPC IPC primitive: SPSC ring buffer
// Lock-free, wait-free, cache-friendly

template<typename T, size_t N>
class SPSCRing {
    alignas(64) std::atomic<size_t> head_{0};  // producer writes
    alignas(64) std::atomic<size_t> tail_{0};  // consumer reads
    alignas(64) T buffer_[N];                   // power-of-2 size

public:
    bool push(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (N - 1);  // mask instead of modulo
        if (next == tail_.load(std::memory_order_acquire))
            return false;  // full
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;  // empty
        item = buffer_[tail];
        tail_.store((tail + 1) & (N - 1), std::memory_order_release);
        return true;
    }
};

// Properties:
// - Wait-free: both push and pop are O(1), no retry loops
// - Cache-friendly: head and tail on separate cache lines (no false sharing)
// - Memory ordering: acquire/release ensures visibility without full barriers
// - Zero allocation: fixed-size buffer, allocated once
// - Used in: your pipeline stage communication, kernel event buffers
```

## 8.3 MPSC Queue (Multiple-Producer Single-Consumer)

```cpp
// Common in: multiple worker threads → one aggregator/flusher
// Dmitry Vyukov's intrusive MPSC queue (Linux kernel pattern)

struct Node {
    std::atomic<Node*> next{nullptr};
    // ... payload ...
};

class MPSCQueue {
    alignas(64) std::atomic<Node*> head_;  // producers push here (CAS)
    alignas(64) Node* tail_;                // consumer pops here (no contention)
    Node stub_;                             // sentinel

public:
    MPSCQueue() : head_(&stub_), tail_(&stub_) {}

    void push(Node* node) {  // called by multiple producers
        node->next.store(nullptr, std::memory_order_relaxed);
        Node* prev = head_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }

    Node* pop() {  // called by single consumer
        Node* tail = tail_;
        Node* next = tail->next.load(std::memory_order_acquire);
        if (tail == &stub_) {
            if (next == nullptr) return nullptr;
            tail_ = next;
            tail = next;
            next = next->next.load(std::memory_order_acquire);
        }
        if (next) {
            tail_ = next;
            return tail;
        }
        return nullptr;  // empty or pending push
    }
};
```

## 8.4 Memory Ordering (What the Atomic Modes Mean)

```
std::memory_order_relaxed:
  No ordering constraint. Compiler/CPU can reorder freely.
  Use for: counters, statistics (where exact ordering doesn't matter)

std::memory_order_acquire (on loads):
  No reads/writes AFTER this load can be reordered BEFORE it.
  "Acquire the lock" — see all writes that happened before the release.

std::memory_order_release (on stores):
  No reads/writes BEFORE this store can be reordered AFTER it.
  "Release the lock" — make all prior writes visible to the acquirer.

std::memory_order_seq_cst:
  Full sequential consistency. Most expensive. Usually unnecessary.
  Use only when you need a total order visible to ALL threads.

The acquire/release pattern (90% of lock-free code):
  Producer: write data → store(release) to flag/pointer
  Consumer: load(acquire) from flag/pointer → read data
  Guarantees: consumer sees all data written before the release
```

---

# SECTION 9: SYSTEM CALL OVERHEAD

---

## 9.1 What a Syscall Costs

```
User-mode function call: ~1-5 ns (no privilege change)
System call (x86_64):    ~100-300 ns (full kernel entry/exit)

What happens:
  1. SYSCALL instruction → CPU switches to ring 0
  2. Save user registers (on kernel stack)
  3. KPTI: switch page tables (kernel isolation for Spectre/Meltdown)
  4. Execute kernel function
  5. KPTI: switch page tables back
  6. Restore user registers
  7. SYSRET instruction → back to ring 3

KPTI cost: ~50-100 ns extra per syscall (Spectre mitigation)
Without KPTI (pre-2018 or trusted environments): ~50-100 ns total
```

## 9.2 Reducing Syscall Frequency

```
Pattern 1: Batching I/O
  BAD:  10 separate write() calls = 10 × 200 ns = 2000 ns overhead
  GOOD: writev() with 10 iovecs = 1 × 200 ns = 200 ns overhead
  
  Or: buffer in user space, flush once per request

Pattern 2: io_uring (zero or minimal syscalls)
  Submit N operations → 1 syscall (io_uring_enter)
  Or with SQPOLL: submit N operations → 0 syscalls!
  
  See Layer 2 for io_uring details

Pattern 3: vDSO (Virtual Dynamic Shared Object)
  clock_gettime() and gettimeofday() do NOT enter kernel!
  They read from a shared page mapped into user space.
  Cost: ~20 ns (same as a function call)
  
  Verify: strace -c shows NO syscalls for clock_gettime

Pattern 4: Memory-mapped files
  After initial mmap(): no syscalls for read/write
  Data accessed via pointer dereference (page fault only on first access)
  
  vs read() loop: each read() is a syscall

Pattern 5: Preallocate file descriptors
  BAD:  open() + read() + close() per request = 3 syscalls
  GOOD: open() once at startup, reuse fd = 0 syscalls per request
```

## 9.3 strace for Syscall Analysis

```bash
# Count syscalls by type (no per-call overhead display)
strace -c -p $PID -e trace=all -o /dev/stderr -- sleep 10
# Shows: % time, seconds, usecs/call, calls, errors, syscall

# Common findings for HPC services:
#   futex: 40%       ← mutex/condvar contention (fix: lock-free)
#   epoll_wait: 20%  ← I/O waiting (normal if I/O-bound)
#   write: 15%       ← logging too much (fix: batch, async)
#   clock_gettime: 10% ← actually cheap (vDSO), ignore
#   mmap/munmap: 5%  ← allocation (fix: custom allocator)

# Trace specific syscalls with timing:
strace -T -e trace=futex,write,read -p $PID 2>&1 | head -100
# -T shows time spent IN each syscall
```

---

# SECTION 10: I/O MODELS AND epoll

---

## 10.1 The I/O Model Spectrum

```
BLOCKING (simplest, worst for concurrency):
  thread per connection → read() blocks until data → process → write()
  10,000 connections = 10,000 threads → CFS cliff!
  
NON-BLOCKING + epoll (standard for servers):
  epoll_wait() blocks on N file descriptors
  When ANY fd is ready → process it
  1 thread can handle 10,000+ connections
  
  But: still 1 syscall per batch of events

io_uring (newest, lowest overhead):
  Completion-based: submit work → kernel notifies when done
  Can eliminate ALL syscalls with SQPOLL
  See Layer 2 for details
```

## 10.2 epoll Internals

```
epoll_create1() → create epoll instance (red-black tree of monitored fds)
epoll_ctl(ADD, fd, events) → add fd to monitor set
epoll_wait(epfd, events, maxevents, timeout) → block until events ready

Internal data structure:
  - Red-black tree: all registered fds (O(log n) add/remove)
  - Ready list: fds with pending events (O(1) retrieval)
  - When a socket gets data: callback adds fd to ready list
  - epoll_wait returns immediately if ready list non-empty

Why epoll scales (vs select/poll):
  select/poll: kernel scans ALL fds every call = O(n)
  epoll: kernel only returns READY fds = O(active)
  
  10,000 connections, 10 active:
    select: scan 10,000 fds → expensive
    epoll: return 10 ready fds → cheap
```

## 10.3 Edge-Triggered vs Level-Triggered

```
Level-triggered (default):
  epoll_wait returns fd as readable AS LONG AS there's data in buffer
  If you don't read all data, next epoll_wait returns the fd AGAIN
  Forgiving: you can read partial data and come back
  But: can cause thundering herd (multiple threads wake for same fd)

Edge-triggered (EPOLLET):
  epoll_wait returns fd ONLY when NEW data arrives
  You MUST read until EAGAIN (all data) or you'll miss the event
  More efficient: no repeated notifications
  But: more complex code (must drain completely)
  
  Required for: high-performance servers (Nginx, etc.)
  
  Pattern:
  while (true) {
      n = read(fd, buf, sizeof(buf));
      if (n == -1 && errno == EAGAIN) break;  // drained
      process(buf, n);
  }
```

---

# SECTION 11: SIGNALS AND PROCESS MANAGEMENT

---

## 11.1 Signals That Matter for HPC Services

```
SIGTERM (15): graceful shutdown
  → K8s sends this first (preStop hook / terminationGracePeriodSeconds)
  → Your service should: stop accepting new work, drain in-flight,
    flush buffers, close connections, exit cleanly
  
SIGKILL (9): immediate death (cannot be caught)
  → K8s sends this after grace period expires
  → No cleanup possible

SIGSEGV (11): segmentation fault
  → Common in C++ services: null pointer, use-after-free, buffer overflow
  → Should trigger core dump for analysis
  → Core dump path: /proc/sys/kernel/core_pattern

SIGBUS (7): bus error
  → mmap'd file truncated while reading (common with shared memory!)
  → Fix: handle SIGBUS, check file size before access

SIGUSR1/SIGUSR2: application-defined
  → Common use: trigger heap dump, toggle debug logging, reload config
  
SIGSTOP/SIGCONT: pause/resume (used by debuggers, job control)
  → Container runtime can pause containers (freeze cgroup)
```

## 11.2 Graceful Shutdown Pattern

```cpp
#include <signal.h>
#include <atomic>

std::atomic<bool> shutdown_requested{false};

void signal_handler(int signum) {
    shutdown_requested.store(true, std::memory_order_release);
    // DO NOT: call malloc, printf, or anything async-signal-unsafe
    // ONLY: set atomic flags, write to pipe (self-pipe trick)
}

int main() {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // Main loop
    while (!shutdown_requested.load(std::memory_order_acquire)) {
        auto request = accept_request();  // non-blocking or timed
        if (request) process(request);
    }

    // Graceful drain
    drain_in_flight_requests(timeout_ms);
    flush_buffers();
    close_connections();
    return 0;
}
```

---

# SECTION 12: KERNEL BYPASS AND USER-SPACE I/O

---

## 12.1 Why Bypass the Kernel

```
Kernel involvement per packet/operation:
  1. Context switch to kernel mode (~100 ns)
  2. Copy data from user buffer to kernel buffer (or vice versa)
  3. Lock acquisition in kernel data structures
  4. IRQ handling and scheduling
  5. Context switch back to user mode (~100 ns)
  
  Total per-operation: 200-500 ns
  
  For 10 million operations/sec: 2-5 SECONDS of overhead per second!
  That's your entire CPU budget gone on just syscall overhead.

Kernel bypass approaches:
  - DPDK: user-space NIC driver (poll mode, no interrupts)
  - AF_XDP: kernel-assisted redirect to user-space
  - io_uring: batched async I/O (reduced syscalls)
  - SPDK: user-space NVMe driver
  - RDMA: NIC bypasses kernel for data transfer
```

## 12.2 io_uring for File I/O (Most Practical Bypass)

```cpp
// io_uring setup for high-throughput file reads
struct io_uring ring;
io_uring_queue_init(256, &ring, 0);  // 256 entries

// Submit multiple reads at once
for (int i = 0; i < batch_size; i++) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fd[i], buf[i], size[i], offset[i]);
    io_uring_sqe_set_data(sqe, &context[i]);
}

// Submit ALL at once (1 syscall for N operations)
io_uring_submit(&ring);

// Harvest completions
struct io_uring_cqe *cqe;
for (int i = 0; i < batch_size; i++) {
    io_uring_wait_cqe(&ring, &cqe);
    auto* ctx = (Context*)io_uring_cqe_get_data(cqe);
    process_completion(ctx, cqe->res);
    io_uring_cqe_seen(&ring, cqe);
}
```

---

# SECTION 13: REAL-WORLD FAILURE SCENARIOS

---

## 13.1 "Service p99 spikes to 50ms every 30 seconds"

```
Diagnosis:
  1. perf stat: context switches normal, no CFS cliff
  2. vmstat: no memory pressure, no swap
  3. Timing analysis: spikes exactly 30 seconds apart
  4. strace: madvise(MADV_DONTNEED) calls at spike time
  5. /proc/PID/smaps: THPs being split!

Root cause: khugepaged (THP compaction daemon) running every 30s
  - Scans process memory for 4KB pages to merge into 2MB pages
  - Causes page table locks and TLB shootdowns
  - Even just SCANNING takes time with THP enabled

Fix:
  echo never > /sys/kernel/mm/transparent_hugepage/enabled
  Use explicit hugepages instead (no compaction daemon)
  
  After fix: p99 spikes GONE
```

## 13.2 "Sudden 10× throughput drop, no code changes"

```
Diagnosis:
  1. mpstat: CPUs 4-11 show 100% sys (kernel mode)
  2. But our service runs on CPUs 4-11!
  3. perf top: kcompactd, reclaim_clean_pages_from_list
  4. Check /proc/buddyinfo: severe memory fragmentation
  5. Kernel trying to create contiguous pages for THP or DMA

Root cause: after a week of running, memory became fragmented
  - kcompactd aggressively compacting in background
  - Runs on ANY cpu (including our isolated ones!)
  - isolcpus doesn't prevent kernel threads from kcompactd

Fix:
  - Disable THP (reduces compaction pressure)
  - Use /proc/sys/vm/compaction_proactiveness=0
  - Long-term: pre-allocate hugepages at boot (no runtime compaction)
  - If needed: restart node weekly (prevents fragmentation buildup)
```

## 13.3 "Service uses 3× expected memory, eventually OOMs"

```
Diagnosis:
  1. RSS growing steadily: 2 GB → 4 GB → 6 GB over 24 hours
  2. No obvious leak in application (valgrind clean)
  3. /proc/PID/smaps shows many small anonymous mappings
  4. strace: frequent mmap/munmap from glibc malloc
  5. glibc malloc holding onto free'd memory (not returning to OS)

Root cause: glibc malloc fragmentation
  - Many small allocations across 64 arenas
  - When freed, memory stays in malloc's free lists
  - Adjacent free chunks not coalesced across different arenas
  - madvise(MADV_DONTNEED) threshold (128 KB) never reached

Fix:
  - Switch to jemalloc (better memory return via background thread):
    export MALLOC_CONF="background_thread:true,dirty_decay_ms:5000"
  - OR: switch to custom arena allocator (reset per request)
  - OR: malloc_trim(0) periodically (force glibc to return pages)
```

## 13.4 "Latency bimodal: 2 ms OR 15 ms, nothing in between"

```
Diagnosis:
  1. Histogram shows two distinct peaks: 2 ms and 15 ms
  2. Not correlated with request size or type
  3. perf stat: some requests have 10× more dTLB-load-misses
  4. numastat: some requests hit remote NUMA (numa_miss)
  
  Pattern: first request after idle → 15 ms, subsequent → 2 ms

Root cause: CPU C-state + cold cache
  - After 1 ms idle: CPU enters C3 sleep (200 µs wake penalty)
  - After wake: L1/L2 caches are cold (previous data evicted)
  - After wake: TLB is cold (no translations cached)
  - First request pays ALL these penalties
  - Subsequent requests: everything warm → 2 ms

Fix:
  - Limit C-state: intel_idle.max_cstate=1 (boot param)
  - CPU governor: performance (no frequency ramp-up delay)
  - Busy-wait polling: keep thread spinning even when idle
    (burns power but eliminates wake latency)
  - Warm-up requests: send synthetic requests during idle periods
```

## 13.5 "perf shows high IPC but service is still slow"

```
Diagnosis:
  1. perf stat: IPC = 2.5 (high! CPU is executing efficiently)
  2. But throughput is 50% of expected
  3. mpstat: only 4 of 8 CPUs are active
  4. pidstat -t: worker threads blocked on futex (mutex)
  5. offcputime shows: threads waiting for a shared hash map lock

Root cause: lock contention on shared data structure
  - CPU executes efficiently WHEN it has the lock
  - But 50% of time is spent WAITING for the lock
  - IPC is high during execution (misleading!)

Fix:
  - Replace shared hash map with concurrent hash map (lock striping)
  - OR: per-thread hash maps with merge at batch boundaries
  - OR: RCU pattern (readers never block, writers copy-and-swap)
  - Verify: offcputime after fix shows no futex waits
```

---

# SECTION 14: ADVANCED LINUX INTERNALS

---

## 14.1 Page Faults

```
Minor page fault:
  Page exists in memory but not yet mapped in page table
  Kernel just updates page table entry (no I/O)
  Cost: ~1-5 µs
  
  Common when: first access to mmap'd region (demand paging)
  Fix: MAP_POPULATE (pre-fault all pages at mmap time)
       mlock() (pin pages, prevent future faults)

Major page fault:
  Page NOT in memory — must be read from disk
  Kernel: allocate page → read from disk → map in page table
  Cost: 1-10 ms (disk I/O!)
  
  Common when: swapping (should NEVER happen for HPC)
  Fix: swapoff -a, adequate memory, mlock for critical data

Pre-faulting pattern:
  void* ptr = mmap(nullptr, size, PROT_READ|PROT_WRITE, 
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
  // MAP_POPULATE: kernel allocates and maps ALL pages immediately
  // No page faults during runtime access
  
  // Alternative: manual pre-fault
  for (size_t i = 0; i < size; i += 4096) {
      ((volatile char*)ptr)[i] = 0;  // touch each page
  }
```

## 14.2 Copy-on-Write (COW) and fork()

```
When a process forks:
  Child gets a COPY of parent's page table (not physical pages)
  All pages marked read-only (shared between parent and child)
  
  When either writes → page fault → kernel copies the page → both continue
  This is COW: defer copying until actually needed
  
Why this matters for HPC:
  If your service forks (e.g., for crash resilience, or a library does):
  - Parent has 10 GB of data
  - Fork creates child (fast — just copies page table)
  - Any write by parent: COW fault → copy 4 KB page → write
  - If parent writes to ALL pages: 10 GB of copies!
  - RSS suddenly doubles (the "fork bomb" effect on memory)

  Fix: don't fork in data-heavy services
  OR: use hugepages (COW copies 2 MB per fault — faster per-fault but
      more memory pressure if many writes)
  OR: use vfork() + exec() if you just need to launch a subprocess
```

## 14.3 Memory-Mapped I/O for Models

```
Loading a 14 GB model file:

Option 1: read() into allocated buffer
  malloc(14 GB)  → may fragment, THP issues
  read(fd, buf, 14 GB)  → copies data kernel→user (14 GB copy!)
  Total: 14 GB kernel copy + 14 GB user allocation = 28 GB transient memory
  Time: ~2-4 seconds (limited by disk + memcpy speed)

Option 2: mmap() the file
  void* model = mmap(nullptr, 14 GB, PROT_READ, MAP_PRIVATE, fd, 0);
  // NO data copied! Just page table entries created.
  // Pages loaded on-demand (page fault → disk read → map)
  
  With MAP_POPULATE:
  void* model = mmap(nullptr, 14 GB, PROT_READ, MAP_PRIVATE|MAP_POPULATE, fd, 0);
  // Pre-faults all pages (still only 14 GB, no extra copy)
  
  With mlock:
  mlock(model, 14 GB);  // pin in RAM, never page out
  
  Benefits:
  - No double-memory (file + buffer): kernel page cache IS the buffer
  - Multiple processes can share the same pages (read-only models)
  - OS manages page cache efficiently
  
  Drawbacks:
  - Page faults on first access (unless MAP_POPULATE)
  - No control over page eviction (other processes may evict your model)
  - mlock solves eviction but requires CAP_IPC_LOCK
```

## 14.4 RCU (Read-Copy-Update)

```
RCU: kernel synchronization mechanism for read-mostly data structures.
Concept: readers NEVER block. Writers make a copy, modify it, then
atomically swap the pointer. Old version freed after all readers finish.

User-space RCU (liburcu):

// Reader (ZERO cost, no lock, no atomic):
rcu_read_lock();
struct Config* cfg = rcu_dereference(global_config);
use(cfg);
rcu_read_unlock();

// Writer (expensive, but rare):
struct Config* new_cfg = copy_and_modify(old_cfg);
rcu_assign_pointer(global_config, new_cfg);
synchronize_rcu();  // wait for all readers of old_cfg to finish
free(old_cfg);

When to use:
  - Read-heavy, write-rare data (config, routing tables, model metadata)
  - Alternative to rwlock (rwlock still has atomic increment for readers!)
  - Used in: Linux kernel (routing tables, security modules, filesystems)
  
Your use case:
  - Feature store cache: reads per request, updates hourly
  - Model routing table: reads per request, updates on deploy
  - Rules engine config: reads per request, updates daily
```

---

# SECTION 15: ADVANCED INTERVIEW QUESTIONS (Grind-Proof)

## "Walk me through what happens when your fraud scoring thread accesses a feature value"

→ Full path:
1. Thread executes `float val = features[idx]` (virtual address)
2. CPU checks L1 data cache: miss (first access to this feature)
3. CPU checks L2 cache: miss
4. CPU checks L3 cache: hit (feature was prefetched by adjacent access)
   → If L3 miss: go to DRAM controller → NUMA node check → 80-200 ns
5. Cache line (64 bytes) loaded into L1/L2
6. TLB lookup: virtual → physical translation
   → TLB hit: 1 cycle
   → TLB miss: 4-level page walk (PML4→PDPT→PD→PT) → 10-100 cycles
     → With hugepage: 3-level walk (PML4→PDPT→PD) + covers 2 MB
7. Physical address resolved, data delivered to register
8. Total: best case ~4 cycles (L1 hit), worst case ~200 cycles (DRAM, no hugepage)

## "How would you debug a 2× throughput regression with no code changes?"

→ Systematic:
1. `perf stat -e cycles,instructions` → has IPC changed? (same code, so should be same)
2. If IPC dropped: `perf stat -e cache-misses,dTLB-load-misses` → data locality issue
3. `numastat -p PID` → did NUMA affinity change? (scheduler migration?)
4. `mpstat -P ALL` → are all expected CPUs being used? (thread died? stuck on lock?)
5. `pidstat -w` → context switch spike? (new cron job? log rotation?)
6. `cat /proc/cpuinfo | grep MHz` → CPU frequency dropped? (throttling?)
7. `perf record -g` → new hot function? (library update? different code path?)
8. `dmesg | grep -i error` → hardware event? (ECC, NIC link flap?)

## "Explain the difference between mmap and read() for loading a model file"

→ Read:
  - Copies data from kernel page cache → user buffer (CPU memcpy)
  - Requires separate allocation for user buffer (2× memory transient)
  - Sequential, predictable, simple
  - Can use O_DIRECT to bypass page cache (for very large files)

→ mmap:
  - Maps kernel page cache directly into user address space (zero-copy)
  - Page faults on first access (unless MAP_POPULATE)
  - Multiple processes share same physical pages (read-only model files)
  - Kernel manages eviction (risk: other processes evict your pages)
  - Use mlock() to pin (requires capability)

→ For HPC model loading: mmap + MAP_POPULATE + mlock = best
  (pre-fault, zero-copy, pinned, shareable across replicas)

## "What is priority inversion and how does it affect your hot path?"

→ Scenario:
  1. Low-priority logging thread holds malloc lock (performing allocation)
  2. High-priority scoring thread calls malloc → blocks on lock
  3. Medium-priority unrelated thread preempts low-priority thread (CFS)
  4. Result: high-priority thread waits for low-priority thread,
     which is itself preempted by medium-priority thread
  → High-priority thread effectively runs at medium-priority's mercy

→ Solutions:
  1. Priority inheritance: lock holder inherits waiter's priority (PI mutex)
  2. Lock-free allocator: scoring thread never calls malloc (arena allocator)
  3. Different CPU: logging on CPU 0-3, scoring on isolated 4-11 (can't preempt)
  4. Our approach: per-worker arena allocator = NO SHARED LOCKS on hot path

## "How does the kernel decide which CPU to wake a thread on?"

→ The kernel's `select_task_rq_fair()` function:
  1. Check last CPU: is it idle? → use it (cache warm)
  2. Check same LLC (L3 shared): idle CPU in same L3 group? → good cache sharing
  3. Check same NUMA node: idle CPU in same node? → good memory locality
  4. Check allowed CPUs (cpuset): only consider allowed set
  5. Load balance: pick least-loaded CPU among candidates

→ For isolated CPUs (isolcpus): CPU not in scheduler domain, so step 1-4
  skip it. Only tasks EXPLICITLY pinned can be placed there.

→ Why this matters: without pinning, kernel may wake your thread on a
  different LLC (cold L3) or different NUMA node (remote memory) = latency spike.
  With pinning: always same CPU = warm cache, local memory, deterministic.

## "What are TLB shootdowns and when do they happen?"

→ When one CPU modifies a page table entry that may be cached in
  other CPUs' TLBs, it must send an IPI (Inter-Processor Interrupt)
  to those CPUs telling them to invalidate that TLB entry.

→ When it happens:
  - munmap(): unmapping pages → TLB shootdown to all sharing CPUs
  - mprotect(): changing permissions → TLB shootdown
  - COW fault resolution → TLB shootdown to parent/child
  - THP compaction → TLB shootdown to all users of those pages
  - Process exit → TLB shootdown

→ Cost: 5-20 µs per shootdown (IPI latency + TLB invalidation)
  At scale (many CPUs sharing address space): can be significant

→ Mitigation:
  - Hugepages: fewer page table entries → fewer shootdowns
  - Process isolation: dedicated address space → no sharing
  - Avoid dynamic mmap/munmap in hot path (use arena, no OS calls)
  - PCID (Process Context ID): reduces shootdowns on context switch
