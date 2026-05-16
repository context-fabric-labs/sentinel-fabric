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
