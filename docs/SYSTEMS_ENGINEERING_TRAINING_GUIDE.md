# Full-Stack Systems Engineering Mastery Guide
## Comprehensive Training for Staff/Principal Engineer Interview Readiness

**A One-Stop Guide to Production Systems Engineering, Zero-Copy Architectures, High-Performance Computing, and Platform Infrastructure for Low-Latency Services**

---

## Table of Contents

### Part I: Application-Level Systems Engineering
1. [Training Overview](#training-overview)
2. [Module 1: Memory & CPU Architecture](#module-1-memory--cpu-architecture)
3. [Module 2: Concurrency & Parallelism](#module-2-concurrency--parallelism)
4. [Module 3: Zero-Copy Architectures](#module-3-zero-copy-architectures)
5. [Module 4: Distributed Systems Patterns](#module-4-distributed-systems-patterns)
6. [Module 5: System Design & Architecture](#module-5-system-design--architecture)
7. [Module 6: Performance Optimization](#module-6-performance-optimization)
8. [Module 7: Hands-On Labs](#module-7-hands-on-labs)

### Part II: Platform & Infrastructure Engineering
9. [Module 8: Host & OS Tuning for HPC](#module-8-host--os-tuning-for-hpc)
10. [Module 9: Kubernetes for Low-Latency Workloads](#module-9-kubernetes-for-low-latency-workloads)
11. [Module 10: Networking Infrastructure](#module-10-networking-infrastructure)
12. [Module 11: GPU Infrastructure & Device Management](#module-11-gpu-infrastructure--device-management)
13. [Module 12: Storage & Memory Infrastructure](#module-12-storage--memory-infrastructure)
14. [Module 13: Observability & SRE for HPC](#module-13-observability--sre-for-hpc)
15. [Module 14: Platform Hands-On Labs](#module-14-platform-hands-on-labs)

### Part III: Performance Troubleshooting & Optimization
16. [Module 15: Performance Analysis Methodology](#module-15-performance-analysis-methodology)
17. [Module 16: Linux Performance Tools Mastery](#module-16-linux-performance-tools-mastery)
18. [Module 17: Advanced Profiling & Tracing](#module-17-advanced-profiling--tracing)
19. [Module 18: Memory & Leak Analysis](#module-18-memory--leak-analysis)
20. [Module 19: Network Troubleshooting](#module-19-network-troubleshooting)
21. [Module 20: Performance Tuning Recipes](#module-20-performance-tuning-recipes)
22. [Module 21: Code Coverage & Quality](#module-21-code-coverage--quality)
23. [Module 22: Troubleshooting Hands-On Labs](#module-22-troubleshooting-hands-on-labs)

### Reference
24. [Interview Question Bank](#interview-question-bank)
25. [Resource Library](#resource-library)
26. [Progress Tracker](#progress-tracker)

---

## Training Overview

### **Target Audience:**
- **Full-Stack Systems Engineers** who write C++/Rust applications AND deploy them
- Engineers with basic systems/platform engineering experience
- Preparing for Staff/Principal AI Infrastructure / Platform Engineering roles
- Want to go from "deployed services" to "optimized systems at scale on optimized infrastructure"

### **Why Full-Stack Systems Engineering?**

A NUMA-aware, lock-free C++ service is only as fast as the infrastructure it runs on:

```
┌─────────────────────────────────────────────────────────────┐
│  Application Layer (Part I)                                  │
│  • Lock-free queues, arena allocators, SIMD, zero-copy      │
│  • These ONLY work if the platform exposes the right        │
│    hardware topology, memory config, and CPU isolation       │
├─────────────────────────────────────────────────────────────┤
│  Platform Layer (Part II)                                    │
│  • NUMA topology exposure, CPU pinning, huge pages           │
│  • Kernel tuning, interrupt affinity, network config         │
│  • Container orchestration that doesn't destroy locality     │
├─────────────────────────────────────────────────────────────┤
│  Hardware Layer                                              │
│  • CPU, GPU, NIC, NVMe, Interconnect                         │
└─────────────────────────────────────────────────────────────┘
```

**Key Insight:** If the platform engineer misconfigures CPU isolation or lets the kernel scheduler move threads across NUMA nodes, all the application-level NUMA optimizations (Module 1) are **completely wasted**.

### **Learning Objectives:**
By the end of this training, you will:

**Part I (Application):**
- ✅ Understand **memory hierarchy** and CPU architecture at depth
- ✅ Implement **lock-free data structures** (SPSC, MPMC queues)
- ✅ Design **zero-copy architectures** (shared memory, Apache Arrow)
- ✅ Master **distributed systems patterns** (consensus, consistency, sharding)
- ✅ Optimize **system performance** (cache locality, NUMA, SIMD)

**Part II (Platform):**
- ✅ Configure **host OS** for HPC workloads (CPU isolation, huge pages, kernel tuning)
- ✅ Deploy on **Kubernetes** with topology-aware scheduling (CPU Manager, Topology Manager)
- ✅ Architect **low-latency networking** (SR-IOV, DPDK, RDMA in containers)
- ✅ Manage **GPU infrastructure** (device plugins, MIG, time-slicing, DCGM)
- ✅ Design **storage infrastructure** for zero-copy (tmpfs, huge pages, NVMe)
- ✅ Build **observability** for HPC (eBPF, DCGM, custom metrics)
- ✅ Answer **interview questions** with real-world examples from your 4 major projects

### **Training Duration:**
- **Total Hours:** 70–80 hours
- **Duration:** 8–10 weeks (part-time)
- **Format:** 30% theory, 70% hands-on labs

### **Prerequisites:**
- Basic understanding of operating systems
- Some experience with C++ or Rust
- Familiarity with Linux/command-line
- Basic knowledge of distributed systems
- Basic Kubernetes familiarity (pods, deployments, services)

---

## Module 1: Memory & CPU Architecture

### **1.1 Memory Hierarchy Deep Dive**

#### **Why Memory Hierarchy Matters:**

```
CPU Register:     0.5 ns   (1 cycle)
L1 Cache:         1 ns     (3-4 cycles)
L2 Cache:         3 ns     (10-15 cycles)
L3 Cache:         10 ns    (30-50 cycles)
Main Memory:      100 ns   (300-500 cycles)
SSD:              100 µs   (300,000 cycles)
Network (DC):     500 µs   (1,500,000 cycles)
```

**Key Insight:** A cache miss costs **300+ CPU cycles**. Optimizing for cache locality can give you **10–100× speedup**.

#### **Cache Lines and Coherence:**

**Cache Line Structure (64 bytes on x86):**
```
┌─────────────────────────────────────────────────────────┐
│  Cache Line (64 bytes)                                  │
│  ┌──────────────┬──────────────┬──────────────┐        │
│  │ Tag (bits)   │ Index (bits) │ Offset (6 bits) │     │
│  └──────────────┴──────────────┴──────────────┘        │
└─────────────────────────────────────────────────────────┘
```

**False Sharing Problem:**
```cpp
// BAD: False sharing - two threads write to different variables
// on the same cache line, causing cache line bouncing
struct Counter {
    int64_t count1;  // Thread 1 writes here
    int64_t count2;  // Thread 2 writes here
    // Both on same 64-byte cache line!
};

// GOOD: Pad to separate cache lines
struct Counter {
    int64_t count1;
    char padding[64 - sizeof(int64_t)];  // Force to next cache line
    int64_t count2;
};

// BETTER: Use alignas
struct Counter {
    alignas(64) int64_t count1;
    alignas(64) int64_t count2;
};
```

**Project Connection:** CapitalOne (per-core arenas with cacheline alignment), Broadcom (feature blocks aligned to 64 bytes)

---

### **1.2 NUMA Architecture**

#### **What is NUMA?**

**UMA (Uniform Memory Access):**
```
┌─────────────┐
│    CPU 0    │
│    CPU 1    │
│    CPU 2    │
│    CPU 3    │
└──────┬──────┘
       │
┌──────┴──────┐
│   Memory    │
│   Controller│
└─────────────┘
→ All CPUs have same latency to memory
```

**NUMA (Non-Uniform Memory Access):**
```
┌──────────────┐     ┌──────────────┐
│  CPU 0,1     │     │  CPU 2,3     │
│  Memory Ctrl │     │  Memory Ctrl │
│  Node 0      │     │  Node 1      │
└──────┬───────┘     └───────┬──────┘
       │                     │
       └──────────┬──────────┘
                  │
         ┌────────┴────────┐
         │  Interconnect   │
         │  (QPI/UPI)      │
         └─────────────────┘
→ CPU 0,1 have faster access to Node 0 memory
→ CPU 2,3 have faster access to Node 1 memory
→ Cross-node access: 2–3× slower
```

**NUMA Optimization:**
```cpp
// Linux: Pin thread to specific NUMA node
#include <numa.h>

// Allocate memory on specific NUMA node
void* ptr = numa_alloc_onnode(size, node_id);

// Pin thread to CPU core on that node
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(core_id, &cpuset);
pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);

// Check NUMA topology
numa_available();
numa_num_configured_nodes();
numa_node_of_cpu(cpu_id);
```

**Project Connection:** Broadcom (NUMA-aware feature extraction), CapitalOne (per-core memory allocation)

---

### **1.3 Memory Allocation Patterns**

#### **Arena Allocator Implementation:**

```cpp
class ArenaAllocator {
private:
    std::vector<uint8_t*> blocks;
    size_t block_size;
    size_t current_offset;
    
public:
    ArenaAllocator(size_t block_size = 1024 * 1024) 
        : block_size(block_size), current_offset(0) {
        // Pre-allocate first block
        blocks.push_back(new uint8_t[block_size]);
    }
    
    ~ArenaAllocator() {
        for (auto* block : blocks) {
            delete[] block;
        }
    }
    
    void* allocate(size_t size, size_t alignment = 8) {
        // Align offset
        size_t aligned_offset = (current_offset + alignment - 1) 
                               & ~(alignment - 1);
        
        // Check if current block has space
        if (aligned_offset + size > block_size) {
            // Allocate new block
            blocks.push_back(new uint8_t[block_size]);
            current_offset = 0;
            aligned_offset = 0;
        }
        
        // Allocate from current block
        void* ptr = blocks.back() + aligned_offset;
        current_offset = aligned_offset + size;
        return ptr;
    }
    
    void reset() {
        // Keep first block, reset offset
        current_offset = 0;
        // Free all other blocks
        for (size_t i = 1; i < blocks.size(); i++) {
            delete[] blocks[i];
        }
        blocks.resize(1);
    }
};

// Usage:
ArenaAllocator arena(1024 * 1024);  // 1MB blocks

// Fast allocation (no malloc overhead)
for (int i = 0; i < 1000000; i++) {
    auto* obj = arena.allocate(sizeof(MyObject), alignof(MyObject));
    new (obj) MyObject();  // Placement new
}

// Bulk deallocation (O(1))
arena.reset();  // All objects freed at once
```

**Performance Comparison:**
| Allocator | Allocation Time | Deallocation | Fragmentation |
|-----------|----------------|--------------|---------------|
| malloc/free | 50 ns | 50 ns | Low |
| Arena (per-request) | 5 ns | N/A | None |
| Arena (bulk reset) | 5 ns | O(1) | None |

**Project Connection:** CapitalOne (per-core arena with bulk reset), Apple (shared memory arena)

---

### **1.4 SIMD Optimization**

#### **AVX-512 Example:**

```cpp
#include <immintrin.h>

// Scalar version (processes 1 element at a time)
void add_scalar(float* a, float* b, float* c, int n) {
    for (int i = 0; i < n; i++) {
        c[i] = a[i] + b[i];
    }
}

// AVX-512 version (processes 16 elements at once)
void add_avx512(float* a, float* b, float* c, int n) {
    int i = 0;
    for (; i + 16 <= n; i += 16) {
        __m512 va = _mm512_loadu_ps(a + i);
        __m512 vb = _mm512_loadu_ps(b + i);
        __m512 vc = _mm512_add_ps(va, vb);
        _mm512_storeu_ps(c + i, vc);
    }
    // Handle remainder
    for (; i < n; i++) {
        c[i] = a[i] + b[i];
    }
}

// Performance:
// Scalar: 100M elements/sec
// AVX-512: 1.6B elements/sec (16× speedup)
```

**Project Connection:** Apple (NEON SIMD 4× speedup for audio features), Broadcom (AVX-512 for feature extraction)

---

### **1.5 Practice Questions: Memory & CPU**

1. **Explain the cost of a cache miss vs. L1 cache hit. How does this affect your data structure design?**
   
   **Answer:** L1 cache hit: ~1 ns (3-4 cycles). L3 cache miss (goes to RAM): ~100 ns (300-500 cycles). That's a **100× difference**. For data structures:
   - Use **arrays instead of linked lists** (contiguous memory)
   - **Structure of Arrays (SoA)** instead of Array of Structures (AoS) for SIMD
   - **Cache-aware algorithms** (block matrix multiplication)
   - **Align to cache lines** (64 bytes) to avoid false sharing

2. **What is false sharing? How do you detect and prevent it?**
   
   **Answer:** False sharing occurs when two threads write to different variables on the same cache line. The cache line bounces between cores, causing **10–100× slowdown**.
   
   **Detection:**
   - `perf c2c` (cache-to-cache monitoring)
   - Intel VTune (memory access analysis)
   - High cache coherency traffic in profiling
   
   **Prevention:**
   - `alignas(64)` for thread-local variables
   - Padding between frequently-written variables
   - Per-core data structures (no sharing)

3. **When would you use an arena allocator vs. standard malloc/free?**
   
   **Answer:** Use arena when:
   - **Many small allocations** of similar-sized objects
   - **Bulk deallocation** (all objects freed at once)
   - **Request-scoped memory** (reset per request)
   - **Low fragmentation** requirement
   
   **Avoid when:**
   - Objects have different lifetimes
   - Need individual deallocation
   - Memory-constrained (arena may over-allocate)

4. **How does NUMA affect multi-threaded performance? How do you optimize for it?**
   
   **Answer:** Cross-NUMA memory access is **2–3× slower** than local access.
   
   **Optimization:**
   - **Pin threads** to specific NUMA nodes
   - **Allocate memory** on same node as thread
   - **First-touch policy:** Touch memory on the node that will use it
   - **NUMA-aware data structures:** Partition data by node

5. **What are huge pages? When should you use them?**
   
   **Answer:** Huge pages (2MB, 1GB) reduce TLB misses for large memory regions.
   
   **Use when:**
   - Large contiguous allocations (> 100MB)
   - Database buffer pools
   - ML model weights
   - Shared memory regions
   
   **Avoid when:**
   - Small allocations (< 1MB)
   - Memory fragmentation concern
   - Security (huge pages harder to zero)

---

## Module 2: Concurrency & Parallelism

### **2.1 Lock-Free Programming Fundamentals**

#### **Memory Ordering:**

```cpp
#include <atomic>

std::atomic<int> data{0};
std::atomic<bool> ready{false};

// Thread 1 (Writer)
void writer() {
    data.store(42, std::memory_order_relaxed);  // No ordering guarantee
    ready.store(true, std::memory_order_release);  // All writes before this are visible
    
// Thread 2 (Reader)
void reader() {
    while (!ready.load(std::memory_order_acquire));  // All writes after this see writer's releases
    assert(data.load(std::memory_order_relaxed) == 42);  // Guaranteed to see 42
}
```

**Memory Ordering Hierarchy:**
```
Strongest → Weakest
seq_cst  (Sequential consistency - default, safest)
acq_rel  (Acquire-Release - bidirectional)
acquire  (Acquire - for loads)
release  (Release - for stores)
relaxed  (No ordering - fastest, use carefully)
```

**Project Connection:** CapitalOne (SPSC ring buffers with atomic indices), Broadcom (lock-free telemetry pipelines)

---

### **2.2 Lock-Free SPSC Queue Implementation**

```cpp
template<typename T>
class SPSCQueue {
private:
    struct alignas(64) AlignedAtomic {
        std::atomic<size_t> value{0};
    };
    
    std::vector<T> buffer;
    size_t capacity;
    AlignedAtomic head;  // Consumer reads from head
    AlignedAtomic tail;  // Producer writes to tail
    
public:
    SPSCQueue(size_t cap) : buffer(cap), capacity(cap) {}
    
    bool try_push(const T& item) {
        const size_t current_tail = tail.value.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) % capacity;
        
        // Check if queue is full
        if (next_tail == head.value.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }
        
        // Write data
        buffer[current_tail] = item;
        
        // Update tail (release ensures data write is visible)
        tail.value.store(next_tail, std::memory_order_release);
        return true;
    }
    
    bool try_pop(T& item) {
        const size_t current_head = head.value.load(std::memory_order_relaxed);
        
        // Check if queue is empty
        if (current_head == tail.value.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }
        
        // Read data
        item = buffer[current_head];
        
        // Update head
        head.value.store((current_head + 1) % capacity, std::memory_order_release);
        return true;
    }
    
    bool empty() const {
        return head.value.load(std::memory_order_acquire) == 
               tail.value.load(std::memory_order_acquire);
    }
    
    size_t size() const {
        const size_t head_val = head.value.load(std::memory_order_acquire);
        const size_t tail_val = tail.value.load(std::memory_order_acquire);
        return (tail_val - head_val + capacity) % capacity;
    }
};

// Performance:
// Mutex-based queue: 100M ops/sec
// Lock-free SPSC: 500M ops/sec (5× speedup)
```

---

### **2.3 Thread Pool with Work Stealing**

```cpp
class WorkStealingThreadPool {
private:
    struct Worker {
        std::deque<std::function<void()>> queue;
        std::mutex mutex;
        std::condition_variable cv;
        std::thread thread;
        std::atomic<bool> stop{false};
    };
    
    std::vector<std::unique_ptr<Worker>> workers;
    std::atomic<size_t> next_worker{0};
    
public:
    WorkStealingThreadPool(size_t num_threads) {
        for (size_t i = 0; i < num_threads; i++) {
            auto worker = std::make_unique<Worker>();
            
            worker->thread = std::thread([this, w = worker.get()]() {
                while (true) {
                    std::function<void()> task;
                    
                    // Try to get task from own queue
                    {
                        std::unique_lock<std::mutex> lock(w->mutex);
                        w->cv.wait(lock, [w]() {
                            return w->stop || !w->queue.empty();
                        });
                        
                        if (w->stop && w->queue.empty()) {
                            return;
                        }
                        
                        task = std::move(w->queue.front());
                        w->queue.pop_front();
                    }
                    
                    // Execute task
                    task();
                }
            });
            
            workers.push_back(std::move(worker));
        }
    }
    
    void submit(std::function<void()> task) {
        // Round-robin distribution
        size_t idx = next_worker.fetch_add(1) % workers.size();
        auto& worker = workers[idx];
        
        {
            std::lock_guard<std::mutex> lock(worker->mutex);
            worker->queue.push_back(std::move(task));
        }
        worker->cv.notify_one();
    }
    
    ~WorkStealingThreadPool() {
        for (auto& worker : workers) {
            worker->stop = true;
            worker->cv.notify_all();
        }
        for (auto& worker : workers) {
            worker->thread.join();
        }
    }
};

// Usage:
WorkStealingThreadPool pool(std::thread::hardware_concurrency());

for (int i = 0; i < 1000000; i++) {
    pool.submit([i]() {
        // Do work
        process(i);
    });
}
```

---

### **2.4 Practice Questions: Concurrency**

1. **What are the different memory orderings in C++ atomics? When would you use `memory_order_relaxed` vs. `memory_order_seq_cst`?**
   
   **Answer:**
   - **`seq_cst`:** Sequential consistency (default). All threads see same order. Safest but slowest.
   - **`acquire/release`:** Pair for producer-consumer. Release ensures prior writes visible, acquire sees them.
   - **`relaxed`:** No ordering guarantees. Only atomicity. Fastest, use for counters.
   
   **Use `relaxed` when:**
   - Incrementing counters (no dependencies)
   - Statistics collection
   - Sequence numbers
   
   **Use `seq_cst` when:**
   - Complex synchronization
   - Multiple variables with dependencies
   - Not sure which to use (safe default)

2. **Implement a lock-free SPSC queue. What are the key challenges?**
   
   **Answer:** See implementation above. Key challenges:
   - **Memory ordering:** Must use acquire/release correctly
   - **False sharing:** Head and tail must be on separate cache lines
   - **ABA problem:** Not an issue for SPSC (single producer/consumer)
   - **Capacity management:** Detect full/empty correctly

3. **What is the ABA problem? How do you solve it?**
   
   **Answer:** Thread 1 reads value A, gets preempted. Thread 2 changes A→B→A. Thread 1 resumes, sees A (unchanged), but state is different.
   
   **Solutions:**
   - **Tagged pointers:** Add version counter to pointer
   - **Hazard pointers:** Track in-use pointers
   - **RCU (Read-Copy-Update):** Defer deallocation
   - **Garbage collection:** Automatic memory management

4. **When would you use a spinlock vs. a mutex?**
   
   **Answer:**
   
   **Spinlock:**
   - **Short critical sections** (< 100 µs)
   - **Low contention** (rare locking)
   - **Kernel/low-level code** (can't sleep)
   - **Busy-wait acceptable** (CPU not needed for other work)
   
   **Mutex:**
   - **Long critical sections** (> 100 µs)
   - **High contention** (many threads waiting)
   - **User-space applications**
   - **CPU better used elsewhere** (thread sleeps)

5. **How does work stealing improve thread pool performance?**
   
   **Answer:** Instead of idle threads waiting for work, they "steal" tasks from busy threads' queues.
   
   **Benefits:**
   - **Better load balancing** (no idle threads)
   - **Cache locality** (work on same core)
   - **Scalability** (no central queue bottleneck)
   
   **Implementation:**
   - Each thread has **double-ended queue (deque)**
   - Owner pushes/pops from **front** (LIFO, cache-friendly)
   - Thieves steal from **back** (FIFO, less contention)

---

## Module 3: Zero-Copy Architectures

### **3.1 Shared Memory IPC**

#### **POSIX Shared Memory:**

```cpp
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

// Create shared memory region
const char* name = "/my_shared_memory";
const size_t size = 1024 * 1024;  // 1MB

int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
ftruncate(fd, size);

// Map into process address space
void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, 
                 MAP_SHARED, fd, 0);

// Use shared memory
struct SharedData {
    std::atomic<int> counter;
    char data[1024];
};

auto* shared = reinterpret_cast<SharedData*>(ptr);
shared->counter.fetch_add(1);

// Unmap when done
munmap(ptr, size);
close(fd);

// Unlink (delete) when all processes done
shm_unlink(name);
```

**Performance:**
- **Shared memory:** 100 ns latency
- **TCP sockets:** 10 µs latency (100× slower)
- **Unix domain sockets:** 1 µs latency (10× slower)

---

### **3.2 Apache Arrow for Zero-Copy Data Exchange**

#### **Arrow Columnar Format:**

```python
import pyarrow as pa

# Create Arrow table (zero-copy from NumPy)
import numpy as np

data = {
    'id': np.array([1, 2, 3, 4, 5], dtype=np.int64),
    'value': np.array([1.1, 2.2, 3.3, 4.4, 5.5], dtype=np.float64),
    'name': ['Alice', 'Bob', 'Charlie', 'David', 'Eve']
}

table = pa.table(data)

# Zero-copy to Pandas
df = table.to_pandas()  # No data copy!

# Zero-copy to C++ (via C Data Interface)
# C++ can access Arrow arrays without serialization
```

**Arrow C Data Interface:**
```cpp
// C++ side: Access Python Arrow data without copying
#include <arrow/c/abi.h>
#include <arrow/c/bridge.h>

// Import Arrow array from Python
struct ArrowArray array;
struct ArrowSchema schema;

// Python exports C Data Interface pointers
# Python code:
# array._export_to_c(<int>array.address)
# schema._export_to_c(<int>schema.address)

// C++ imports and wraps
auto imported_array = arrow::ImportArray(&array, &schema);

// Zero-copy access to data
auto double_array = std::static_pointer_cast<arrow::DoubleArray>(imported_array);
const double* values = double_array->raw_values();  // No copy!
```

**Project Connection:** All four projects (zero-copy as recurring theme), Broadcom (eliminated 200ms serialization overhead)

---

### **3.3 Serialization Comparison**

| Format | Speed | Size | Zero-Copy | Schema | Language Support |
|--------|-------|------|-----------|--------|------------------|
| **JSON** | Slow | Large | No | No | Universal |
| **Protobuf** | Fast | Small | No | Yes | C++, Java, Python |
| **FlatBuffers** | Very Fast | Small | **Yes** | Yes | C++, Java, Python |
| **Cap'n Proto** | Very Fast | Small | **Yes** | Yes | C++, Python |
| **Arrow** | Very Fast | Medium | **Yes** | Yes | C++, Python, Java |

#### **FlatBuffers Example:**

```cpp
// Schema (mydata.fbs)
table Person {
  id: long;
  name: string;
  email: string;
}

// Generate C++ code
flatc --cpp mydata.fbs

// Use (zero-copy deserialization)
#include "mydata_generated.h"

// Read from file (no parsing!)
std::ifstream file("data.bin", std::ios::binary);
file.seekg(0, std::ios::end);
size_t size = file.tellg();
file.seekg(0, std::ios::beg);

std::vector<char> buffer(size);
file.read(buffer.data(), size);

// Zero-copy access
auto* person = GetPerson(buffer.data());
std::cout << person->name()->c_str();  // No deserialization!
std::cout << person->id();             // Direct memory access
```

**Performance:**
- **Protobuf:** 100 MB/s deserialize
- **FlatBuffers:** 1 GB/s (10× faster, zero-copy)

---

### **3.4 RDMA and Kernel Bypass**

#### **What is RDMA?**

**Traditional Network:**
```
Application → Kernel TCP/IP → NIC Driver → NIC → Network
→ Context switches, CPU copies, interrupts
→ Latency: 50–100 µs
```

**RDMA (Remote Direct Memory Access):**
```
Application → NIC → Network
→ Zero-copy, no CPU involvement
→ Latency: 1–5 µs (20× faster)
```

**RDMA Verbs Example:**
```cpp
// Register memory region (pin pages)
struct ibv_mr* mr = ibv_reg_mr(
    pd,           // Protection domain
    buffer,       // Buffer to register
    size,         // Size
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ
);

// Get remote key for access
uint32_t rkey = mr->rkey;
uint64_t remote_addr = (uint64_t)remote_buffer;

// Post RDMA read (zero-copy from remote)
struct ibv_send_wr wr = {};
wr.wr_id = 1;
wr.opcode = IBV_WR_RDMA_READ;
wr.sg_list = &sge;
wr.num_sge = 1;
wr.wr.rdma.remote_addr = remote_addr;
wr.wr.rdma.rkey = rkey;

ibv_post_send(qp, &wr, &bad_wr);

// Wait for completion
ibv_poll_cq(cq, 1, &wc);
```

**Project Connection:** Broadcom (10 Gbps per node telemetry), high-frequency trading systems

---

### **3.5 Practice Questions: Zero-Copy**

1. **What is zero-copy? Give examples from your projects.**
   
   **Answer:** Zero-copy eliminates unnecessary data copies between user/kernel space or between processes.
   
   **Examples:**
   - **Broadcom:** Shared memory request context (eliminated 200ms serialization)
   - **Apple:** Shared memory arena for stage handoff (50–80ms → 5–10ms)
   - **CapitalOne:** Per-core arenas (no cross-core copies)
   - **Fiserv:** Apache Arrow for cross-tier data (2KB → 47 bytes copied)

2. **How does Apache Arrow enable zero-copy data exchange?**
   
   **Answer:** Arrow uses:
   - **Columnar format:** Contiguous memory per column
   - **C Data Interface:** Standard C struct for array/schema
   - **Immutable buffers:** No modification after creation
   - **Reference counting:** Safe sharing across languages
   
   **Result:** Python → C++ → Java without serialization/deserialization.

3. **Compare Protocol Buffers, FlatBuffers, and Cap'n Proto.**
   
   **Answer:**
   
   | Feature | Protobuf | FlatBuffers | Cap'n Proto |
   |---------|----------|-------------|-------------|
   | Parsing | Yes | **No** (zero-copy) | **No** (zero-copy) |
   | Speed | Fast | Very Fast | Very Fast |
   | Size | Small | Small | Medium |
   | Language Support | Excellent | Good | Good |
   | Schema Evolution | Excellent | Good | Good |
   
   **Use Protobuf:** Maximum language support, schema evolution
   **Use FlatBuffers:** Zero-copy, read-heavy workloads
   **Use Cap'n Proto:** RPC, capability-based security

4. **What is RDMA? When would you use it?**
   
   **Answer:** RDMA (Remote Direct Memory Access) allows direct memory access between machines without CPU involvement.
   
   **Use when:**
   - **Low latency required** (< 10 µs)
   - **High throughput** (> 10 Gbps)
   - **CPU efficiency critical** (offload network to NIC)
   - **Data center networking** (RoCE, InfiniBand)
   
   **Avoid when:**
   - **Commodity hardware** (requires special NICs)
   - **WAN/internet** (RDMA over WAN is complex)
   - **Small messages** (< 1KB, overhead dominates)

5. **How does DPDK achieve kernel bypass?**
   
   **Answer:** DPDK (Data Plane Development Kit):
   - **Userspace drivers:** NIC driver in userspace (no kernel context switches)
   - **Huge pages:** Pin memory, avoid TLB misses
   - **Poll mode:** No interrupts (busy-wait, lower latency)
   - **Batch processing:** Process packets in batches (amortize overhead)
   
   **Result:** 10M+ packets/sec (vs. 1M/sec with kernel networking)

---

## Module 4: Distributed Systems Patterns

### **4.1 Consistency Models**

#### **CAP Theorem:**

```
You can only have 2 of 3:
┌─────────────────────────────────────┐
│  Consistency  │                    │
│               │                    │
│               │                    │
│  ─────────────┼────────────        │
│               │                    │
│               │                    │
│  Availability │  Partition Tolerance│
└─────────────────────────────────────┘

CP Systems: Consul, etcd, ZooKeeper
AP Systems: Cassandra, DynamoDB, Riak
CA Systems: Traditional RDBMS (no partition tolerance)
```

**PACELC Extension:**
- **If Partition (P):** Choose Availability (A) or Consistency (C)
- **Else (E):** Choose Latency (L) or Consistency (C)

**Example:** Cassandra = PA/EL (AP during partition, EL during normal operation)

---

### **4.2 Consistent Hashing**

#### **Implementation:**

```cpp
class ConsistentHash {
private:
    std::map<uint64_t, std::string> ring;
    std::vector<std::string> nodes;
    size_t virtual_nodes = 150;
    
    uint64_t hash(const std::string& key) {
        return std::hash<std::string>{}(key);
    }
    
public:
    void add_node(const std::string& node) {
        nodes.push_back(node);
        for (size_t i = 0; i < virtual_nodes; i++) {
            uint64_t h = hash(node + "#" + std::to_string(i));
            ring[h] = node;
        }
    }
    
    std::string get_node(const std::string& key) {
        uint64_t h = hash(key);
        auto it = ring.lower_bound(h);
        
        if (it == ring.end()) {
            it = ring.begin();  // Wrap around
        }
        
        return it->second;
    }
    
    void remove_node(const std::string& node) {
        nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
        
        for (size_t i = 0; i < virtual_nodes; i++) {
            uint64_t h = hash(node + "#" + std::to_string(i));
            ring.erase(h);
        }
    }
};

// Usage:
ConsistentHash hasher;
hasher.add_node("node1");
hasher.add_node("node2");
hasher.add_node("node3");

std::string node = hasher.get_node("session_12345");
// Same session always routes to same node (85% cache hit rate at Apple)
```

**Project Connection:** Apple (consistent hashing for 85% cache hit rate), Fiserv (health-aware routing)

---

### **4.3 Circuit Breaker Pattern**

```cpp
class CircuitBreaker {
private:
    enum State { CLOSED, OPEN, HALF_OPEN };
    State state = CLOSED;
    
    std::chrono::milliseconds timeout{30000};
    int failure_threshold = 5;
    int success_threshold = 2;
    
    std::atomic<int> failures{0};
    std::atomic<int> successes{0};
    std::chrono::steady_clock::time_point last_failure_time;
    
public:
    template<typename Func>
    auto execute(Func&& func) -> decltype(func()) {
        if (state == OPEN) {
            if (std::chrono::steady_clock::now() - last_failure_time > timeout) {
                state = HALF_OPEN;
                successes = 0;
            } else {
                throw std::runtime_error("Circuit breaker is OPEN");
            }
        }
        
        try {
            auto result = func();
            
            if (state == HALF_OPEN) {
                successes++;
                if (successes >= success_threshold) {
                    state = CLOSED;
                    failures = 0;
                }
            }
            
            return result;
        } catch (...) {
            failures++;
            last_failure_time = std::chrono::steady_clock::now();
            
            if (state == HALF_OPEN || failures >= failure_threshold) {
                state = OPEN;
            }
            
            throw;
        }
    }
    
    State get_state() const { return state; }
};

// Usage:
CircuitBreaker cb;

try {
    auto result = cb.execute([]() {
        return call_external_service();
    });
} catch (const std::runtime_error& e) {
    // Circuit breaker open, use fallback
    result = fallback_result();
}
```

**Project Connection:** Broadcom (99.99% availability), CapitalOne (99.999% availability)

---

### **4.4 Practice Questions: Distributed Systems**

1. **Explain the CAP theorem. Can you have all three?**
   
   **Answer:** No, you can only have 2 of 3:
   - **Consistency:** All nodes see same data at same time
   - **Availability:** Every request gets a response (success/failure)
   - **Partition Tolerance:** System works despite network partitions
   
   **During partition (P):** Must choose A or C
   - **CP:** Reject requests (maintain consistency)
   - **AP:** Return stale data (maintain availability)
   
   **Real-world:** All distributed systems must handle partitions, so choice is CA vs. CP vs. AP.

2. **What is consistent hashing? How does it help with load balancing?**
   
   **Answer:** Consistent hashing maps keys and nodes to a circle (hash ring). Each key is assigned to the next node clockwise.
   
   **Benefits:**
   - **Minimal remapping:** Adding/removing nodes affects only 1/N keys
   - **No central coordinator:** Each client can compute independently
   - **Virtual nodes:** Balance load across heterogeneous nodes
   
   **Use cases:**
   - Session stickiness (Apple: 85% cache hit rate)
   - Distributed caching (Redis Cluster)
   - Sharded databases

3. **How do circuit breakers work? When would you use one?**
   
   **Answer:** Circuit breaker has 3 states:
   - **CLOSED:** Normal operation, requests pass through
   - **OPEN:** Failures exceeded threshold, reject all requests
   - **HALF_OPEN:** After timeout, allow test requests
   
   **Use when:**
   - External service calls (database, API)
   - Cascading failure prevention
   - Graceful degradation
   
   **Example:** CapitalOne (circuit breakers with DCGM for GPU health monitoring)

4. **What is the difference between at-least-once and exactly-once semantics?**
   
   **Answer:**
   
   **At-least-once:**
   - Message delivered ≥ 1 times
   - May have duplicates
   - **Idempotency required** on consumer
   - **Faster, simpler**
   
   **Exactly-once:**
   - Message delivered exactly 1 time
   - No duplicates, no losses
   - **Two-phase commit, transactions**
   - **Slower, complex**
   
   **Use at-least-once + idempotency** for most cases (Kafka default).

5. **How do you handle distributed transactions?**
   
   **Answer:**
   
   **Two-Phase Commit (2PC):**
   - Phase 1: Prepare (all nodes vote)
   - Phase 2: Commit/Rollback (based on votes)
   - **Blocking:** Coordinator failure = blocked
   - **Synchronous:** High latency
   
   **Saga Pattern:**
   - Break transaction into local transactions
   - Each has compensating transaction (rollback)
   - **Non-blocking:** No coordinator
   - **Eventual consistency**
   
   **Use Saga for:** Microservices, long-running transactions

---

## Module 5: System Design & Architecture

### **5.1 System Design Framework**

#### **Step-by-Step Approach:**

```
1. Requirements (5 min)
   - Functional: What does system do?
   - Non-functional: Latency, throughput, availability, consistency
   
2. Capacity Estimation (5 min)
   - QPS, storage, bandwidth
   - Back-of-envelope calculations
   
3. High-Level Design (10 min)
   - Major components
   - Data flow
   - API design
   
4. Deep Dive (15 min)
   - 1-2 critical components
   - Data models
   - Scaling strategies
   
5. Bottleneck Analysis (5 min)
   - Single points of failure
   - Performance bottlenecks
   - Mitigation strategies
```

---

### **5.2 Capacity Estimation Cheat Sheet**

```
Seconds per day: 86,400
Seconds per month: 2.5M

QPS Calculations:
- 1M requests/day → 1M / 86,400 ≈ 12 QPS
- 100M requests/day → 100M / 86,400 ≈ 1,200 QPS
- 1B requests/day → 1B / 86,400 ≈ 12,000 QPS

Storage Calculations:
- 1 KB per post × 100M posts/day × 365 days × 10 years
  = 365 TB × 3 (replication) = 1 PB

Bandwidth Calculations:
- 1 MB per request × 1,000 QPS = 1 GB/s
- 1 GB/s × 8 = 8 Gbps

Latency Numbers (for reference):
- L1 cache: 0.5 ns
- L3 cache: 10 ns
- Main memory: 100 ns
- SSD: 100 µs
- Network (DC): 500 µs
```

---

### **5.3 AI-Specific System Design: LLM Serving Platform**

#### **Requirements:**
- **Functional:** Serve LLM inference for chat, completion, embedding
- **Non-functional:**
  - Latency: TTFT < 500ms, TPOT < 50ms/token
  - Throughput: 10,000+ tokens/sec
  - Availability: 99.9%
  - Multi-tenancy: Isolate customers

#### **High-Level Design:**

```
┌─────────────────────────────────────────────────────────┐
│                    Load Balancer                         │
│  • Round-robin / consistent hashing                     │
│  • Rate limiting per tenant                             │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                  Request Router                          │
│  • Session stickiness (KV-cache affinity)               │
│  • Priority queues (P0, P1, P2)                         │
│  • Admission control                                    │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│               Inference Engine (vLLM)                    │
│  • PagedAttention (KV-cache management)                 │
│  • Continuous batching                                  │
│  • Prefix caching                                       │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                  GPU Pool                                │
│  • NVIDIA A100/H100                                     │
│  • MIG for multi-tenancy                                │
│  • DCGM monitoring                                      │
└─────────────────────────────────────────────────────────┘
```

#### **Key Design Decisions:**

1. **KV-Cache Management:**
   - Use **PagedAttention** (vLLM) for memory efficiency
   - **Session stickiness** for cache hits (70% target)
   - **LRU eviction** for memory pressure

2. **Batching Strategy:**
   - **Continuous batching** (in-flight batching)
   - **Max batch size:** 256 sequences
   - **Max wait time:** 10ms

3. **Multi-Tenancy:**
   - **GPU isolation:** MIG or time-slicing
   - **Rate limiting:** Per-token budgets
   - **Priority queues:** P0 (interactive), P1 (batch)

**Project Connection:** Fiserv (multi-model LLM orchestration), CapitalOne (multi-tenant GPU isolation)

---

## Module 6: Performance Optimization

### **6.1 Profiling Tools**

#### **Linux Profiling Stack:**

```
Application
    │
    ▼
perf (CPU profiling)
    │
    ▼
VTune (Intel, detailed)
    │
    ▼
Nsight Systems (NVIDIA GPU)
    │
    ▼
eBPF (kernel tracing)
```

#### **perf Examples:**

```bash
# CPU profiling
perf record -g ./my_program
perf report

# Cache misses
perf stat -e cache-misses,cache-references ./my_program

# Memory profiling
perf stat -e major-faults,minor-faults ./my_program

# Flame graph
perf record -g ./my_program
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

---

### **6.2 Optimization Checklist**

#### **CPU Optimization:**
- [ ] Profile before optimizing (find bottlenecks)
- [ ] Reduce cache misses (data locality)
- [ ] Use SIMD (AVX2, AVX-512, NEON)
- [ ] Minimize branch mispredictions
- [ ] Use appropriate data structures (arrays vs. linked lists)
- [ ] Avoid false sharing (align to cache lines)
- [ ] Use arena allocators (reduce malloc overhead)

#### **Memory Optimization:**
- [ ] Profile memory usage (heap, stack)
- [ ] Reduce allocations (reuse buffers)
- [ ] Use huge pages (reduce TLB misses)
- [ ] Pin memory for DMA (pinned memory)
- [ ] Avoid NUMA cross-node access
- [ ] Use memory-mapped files (large files)

#### **GPU Optimization:**
- [ ] Maximize occupancy (threads per SM)
- [ ] Use shared memory (reduce global memory access)
- [ ] Coalesce memory access (aligned, contiguous)
- [ ] Use Tensor Cores (mixed precision)
- [ ] Overlap H2D with compute (CUDA streams)
- [ ] Use CUDA Graphs (reduce launch overhead)

#### **Network Optimization:**
- [ ] Use zero-copy (sendfile, splice)
- [ ] Batch small messages
- [ ] Use compression (zstd, lz4)
- [ ] Tune TCP parameters (buffer sizes)
- [ ] Consider RDMA (low latency)
- [ ] Use kernel bypass (DPDK) for extreme performance

---

## Module 7: Hands-On Labs

### **Lab 1: Build Lock-Free SPSC Queue**

**Objective:** Implement and benchmark lock-free queue.

**Steps:**
1. Implement SPSCQueue class (see Section 2.2)
2. Benchmark vs. mutex-based queue
3. Measure throughput (ops/sec)
4. Test with different workloads (producer/consumer on same/different cores)

**Expected Results:**
- Lock-free: 500M ops/sec
- Mutex-based: 100M ops/sec
- **5× speedup**

---

### **Lab 2: Implement Arena Allocator**

**Objective:** Build arena allocator and compare with malloc.

**Steps:**
1. Implement ArenaAllocator class (see Section 1.3)
2. Benchmark allocation/deallocation
3. Measure fragmentation
4. Test bulk reset performance

**Expected Results:**
- Arena allocation: 5 ns
- malloc: 50 ns
- **10× speedup**
- Bulk reset: O(1) vs. O(n) for individual frees

---

### **Lab 3: Zero-Copy Shared Memory**

**Objective:** Build shared memory IPC between two processes.

**Steps:**
1. Create shared memory region (POSIX shm)
2. Map into producer process
3. Map into consumer process
4. Implement SPSC queue in shared memory
5. Benchmark latency vs. sockets

**Expected Results:**
- Shared memory: 100 ns latency
- Unix domain sockets: 1 µs
- TCP sockets: 10 µs
- **100× speedup** vs. TCP

---

---

# Part II: Platform & Infrastructure Engineering

---

## Module 8: Host & OS Tuning for HPC

### **8.1 Why Platform Configuration Matters**

#### **The Problem:**

Your C++ service uses NUMA-aware allocations, CPU pinning, and huge pages. But the platform:
- Doesn't expose NUMA topology to containers
- Lets the kernel scheduler migrate threads freely
- Doesn't pre-allocate huge pages
- Has noisy neighbors stealing CPU cycles

**Result:** All your application-level optimizations are **nullified**.

#### **Platform Engineer's Responsibility:**

```
Application Developer Says:          Platform Engineer Must Configure:
─────────────────────────────────    ──────────────────────────────────────
"I use NUMA-aware allocation"     →  Expose NUMA topology, pin to node
"I use huge pages for KV-cache"   →  Pre-allocate huge pages on host
"I use CPU pinning for threads"   →  Isolate CPUs from kernel scheduler
"I use lock-free SPSC queues"     →  Ensure producer/consumer on same socket
"I use SIMD (AVX-512)"           →  Verify CPU flags, disable power saving
"I use RDMA for networking"       →  Configure SR-IOV, expose VFs to pods
"I use shared memory IPC"         →  Configure IPC namespace, tmpfs sizing
```

---

### **8.2 CPU Isolation & Pinning**

#### **Why CPU Isolation?**

Without isolation, the Linux kernel scheduler can:
- Move your latency-sensitive threads between CPUs
- Schedule housekeeping tasks on your performance-critical cores
- Cause cache thrashing when threads migrate across NUMA nodes

#### **isolcpus and CPU Sets:**

```bash
# Kernel boot parameter: isolate CPUs 4-31 from general scheduling
# /etc/default/grub
GRUB_CMDLINE_LINUX="isolcpus=4-31 nohz_full=4-31 rcu_nocbs=4-31"

# isolcpus: Remove CPUs from kernel scheduler
# nohz_full: Disable timer interrupts on isolated CPUs (tickless)
# rcu_nocbs: Offload RCU callbacks from isolated CPUs
```

**What each parameter does:**

| Parameter | Effect | Why It Matters |
|-----------|--------|----------------|
| `isolcpus=4-31` | CPUs 4-31 not used by scheduler | Your threads get dedicated CPUs |
| `nohz_full=4-31` | No timer ticks on those CPUs | No 1ms jitter from timer interrupts |
| `rcu_nocbs=4-31` | RCU callbacks run elsewhere | No kernel housekeeping overhead |
| `irqaffinity=0-3` | IRQs only on CPUs 0-3 | No interrupt storm on perf CPUs |

#### **CPU Shielding with cgroups v2:**

```bash
# Create a performance cpuset
mkdir -p /sys/fs/cgroup/performance

# Assign CPUs 4-15 (NUMA node 0) and 16-31 (NUMA node 1)
echo "4-31" > /sys/fs/cgroup/performance/cpuset.cpus
echo "0-1" > /sys/fs/cgroup/performance/cpuset.mems

# Pin specific workload
echo $PID > /sys/fs/cgroup/performance/cgroup.procs

# Verify isolation
cat /proc/$PID/status | grep Cpus_allowed_list
# Output: Cpus_allowed_list: 4-31
```

#### **systemd-based CPU Isolation:**

```bash
# /etc/systemd/system/my-hpc-service.service
[Service]
Type=simple
ExecStart=/opt/my-service/bin/server
CPUAffinity=4-15
AllowedCPUs=4-15
AllowedMemoryNodes=0
Nice=-20
IOSchedulingClass=realtime
IOSchedulingPriority=0

# Prevent other services from using these CPUs
[Service]
# For system services
CPUAffinity=0-3
```

#### **Verifying CPU Isolation:**

```bash
# Check which CPUs are isolated
cat /sys/devices/system/cpu/isolated
# Output: 4-31

# Check CPU affinity of a process
taskset -p $PID
# Output: pid 1234's current affinity mask: fffffffc (CPUs 2-31)

# Monitor context switches (should be near zero for isolated CPUs)
perf stat -e context-switches -p $PID -- sleep 10

# Check NUMA placement
numactl --hardware
numastat -p $PID
```

**Project Connection:** CapitalOne (dedicated cores for fraud scoring pipeline), Broadcom (isolated CPUs for packet processing)

---

### **8.3 Huge Pages Configuration**

#### **Why Huge Pages?**

Standard 4KB pages → frequent TLB misses for large memory regions (ML model weights, KV-cache).

**TLB Miss Impact:**
- 4KB pages, 1GB memory = 262,144 page table entries
- TLB size: ~1,500 entries (L1+L2 TLB)
- **Miss rate: Very high** → Each miss = 10-100 ns penalty

**2MB huge pages, 1GB memory = 512 entries → fits in TLB!**

#### **Pre-Allocating Huge Pages:**

```bash
# Method 1: Boot-time allocation (recommended, avoids fragmentation)
# /etc/default/grub
GRUB_CMDLINE_LINUX="default_hugepagesz=2M hugepagesz=2M hugepages=4096 \
                    hugepagesz=1G hugepages=16"
# Allocates: 4096 × 2MB = 8GB + 16 × 1GB = 16GB = 24GB reserved

# Method 2: Runtime allocation (may fail due to fragmentation)
echo 4096 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
echo 16 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages

# Method 3: Per-NUMA node allocation (critical for NUMA-aware services)
echo 2048 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 2048 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

# Verify allocation
cat /proc/meminfo | grep -i huge
# HugePages_Total:    4096
# HugePages_Free:     4096
# HugePages_Rsvd:        0
# Hugepagesize:       2048 kB
```

#### **Mounting hugetlbfs:**

```bash
# Mount huge page filesystem
mkdir -p /mnt/hugepages-2M
mount -t hugetlbfs -o pagesize=2M none /mnt/hugepages-2M

mkdir -p /mnt/hugepages-1G
mount -t hugetlbfs -o pagesize=1G none /mnt/hugepages-1G

# /etc/fstab for persistence
none /mnt/hugepages-2M hugetlbfs pagesize=2M,size=8G 0 0
none /mnt/hugepages-1G hugetlbfs pagesize=1G,size=16G 0 0
```

#### **Transparent Huge Pages (THP) - Caution:**

```bash
# THP can cause latency spikes (compaction pauses)
# For latency-sensitive workloads, DISABLE THP:
echo never > /sys/kernel/mm/transparent_hugepage/enabled
echo never > /sys/kernel/mm/transparent_hugepage/defrag

# For throughput workloads (ML training), THP can help:
echo always > /sys/kernel/mm/transparent_hugepage/enabled
echo defer > /sys/kernel/mm/transparent_hugepage/defrag
```

| Workload Type | THP Setting | Explicit Huge Pages | Reason |
|---------------|-------------|---------------------|--------|
| Low-latency inference | `never` | Yes (2MB) | Avoid compaction pauses |
| ML training | `madvise` | Yes (1GB) | Large model weights |
| KV-cache store | `never` | Yes (2MB) | Predictable latency |
| General services | `madvise` | No | Application decides |

**Project Connection:** Fiserv (huge pages for KV-cache, 3.2× faster H2D), CapitalOne (1GB pages for model weights)

---

### **8.4 Kernel Tuning for Low-Latency**

#### **sysctl Parameters:**

```bash
# /etc/sysctl.d/99-hpc-tuning.conf

# --- Memory ---
vm.swappiness = 0                    # Never swap (OOM kill instead)
vm.overcommit_memory = 1             # Allow overcommit (mmap won't fail)
vm.dirty_ratio = 5                   # Flush dirty pages early
vm.dirty_background_ratio = 1        # Background flush threshold
vm.nr_hugepages = 4096               # Pre-allocate huge pages
vm.max_map_count = 1048576           # Allow many mmap regions

# --- Network (see Module 10 for details) ---
net.core.somaxconn = 65535           # Max socket backlog
net.core.netdev_max_backlog = 65535  # NIC backlog queue
net.core.rmem_max = 134217728        # 128MB receive buffer
net.core.wmem_max = 134217728        # 128MB send buffer
net.ipv4.tcp_rmem = 4096 131072 134217728
net.ipv4.tcp_wmem = 4096 131072 134217728
net.ipv4.tcp_fastopen = 3            # Enable TCP Fast Open
net.ipv4.tcp_low_latency = 1         # Favor latency over throughput

# --- Scheduling ---
kernel.sched_min_granularity_ns = 100000   # 100µs min timeslice
kernel.sched_migration_cost_ns = 5000000   # 5ms migration cost (reduces migration)
kernel.sched_rt_runtime_us = 990000        # 99% RT budget

# --- IPC ---
kernel.shmmax = 68719476736          # 64GB max shared memory segment
kernel.shmall = 4294967296           # Max shared memory pages
kernel.msgmax = 65536                # Max message size
kernel.sem = 250 256000 32 1024      # Semaphore limits
```

#### **IRQ Affinity (Interrupt Steering):**

```bash
# Problem: NIC interrupts can hit performance-critical CPUs
# Solution: Pin interrupts to housekeeping CPUs (0-3)

# Find NIC IRQs
cat /proc/interrupts | grep eth0
# Or for modern NICs with multiple queues:
ls /sys/class/net/eth0/device/msi_irqs/

# Pin all NIC IRQs to CPUs 0-3
for irq in $(cat /proc/interrupts | grep eth0 | awk '{print $1}' | sed 's/://'); do
    echo "f" > /proc/irq/$irq/smp_affinity  # 0xf = CPUs 0-3
done

# Use irqbalance with policy file
# /etc/irqbalance.d/hpc-policy
IRQBALANCE_BANNED_CPUS=FFFFFFFC  # Ban CPUs 2-31 from IRQs

# Or use set_irq_affinity script (common with Mellanox/NVIDIA NICs)
/opt/mellanox/bin/set_irq_affinity_cpulist.sh 0-3 eth0
```

#### **Real-Time Kernel Patches (PREEMPT_RT):**

```bash
# For extreme low-latency (sub-microsecond jitter):
# Install PREEMPT_RT kernel
apt install linux-image-rt-amd64

# Verify
uname -a
# Output: ... PREEMPT RT ...

# Benefits:
# - Fully preemptible kernel
# - Priority inheritance for mutexes
# - Reduced worst-case latency: 100µs → 10µs

# When to use PREEMPT_RT:
# ✅ Trading systems (< 10µs jitter requirement)
# ✅ Audio/video processing
# ✅ Industrial control
# ❌ General ML inference (standard kernel is fine)
# ❌ Throughput-focused workloads
```

#### **Power Management (Critical for consistent latency):**

```bash
# Disable CPU frequency scaling (C-states cause latency spikes)
# /etc/default/grub
GRUB_CMDLINE_LINUX="intel_idle.max_cstate=0 processor.max_cstate=0 \
                    intel_pstate=disable idle=poll"

# Set CPU governor to performance
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "performance" > $cpu
done

# Disable turbo boost (inconsistent timing)
echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
# Or for consistency, leave turbo ON but set:
echo 0 > /sys/devices/system/cpu/cpufreq/boost

# Verify
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
# Output: performance

cpupower frequency-info
```

| Setting | Effect | Latency Impact |
|---------|--------|----------------|
| C-state disabled | CPU always at full power | Eliminates 10-100µs wake-up latency |
| Governor: performance | Fixed max frequency | No frequency ramping delay |
| Turbo disabled | Consistent clock speed | Predictable timing |
| idle=poll | CPU never enters idle | Minimum possible latency (high power) |

---

### **8.5 NUMA Topology for Platform Engineers**

#### **Discovering NUMA Topology:**

```bash
# View NUMA topology
numactl --hardware
# Output:
# available: 2 nodes (0-1)
# node 0 cpus: 0 1 2 3 4 5 6 7 16 17 18 19 20 21 22 23
# node 0 size: 32768 MB
# node 1 cpus: 8 9 10 11 12 13 14 15 24 25 26 27 28 29 30 31
# node 1 size: 32768 MB
# node distances:
# node   0   1
#   0:  10  21
#   1:  21  10

# lstopo (graphical view)
lstopo --of txt
# Shows: CPUs, caches, memory, PCIe devices, NICs, GPUs per NUMA node

# Check GPU NUMA affinity
nvidia-smi topo -m
# GPU0 ↔ GPU1: NVLink
# GPU0 ↔ mlx5_0: PIX (same PCIe switch)
# GPU0 ↔ CPU: NODE0
```

#### **NUMA-Aware Service Placement:**

```bash
# Rule: Place service on SAME NUMA node as its GPU and NIC

# Example: ML inference service using GPU0 (NUMA node 0)
# Step 1: Find GPU NUMA node
cat /sys/bus/pci/devices/0000:3b:00.0/numa_node  # GPU0
# Output: 0

# Step 2: Find NIC NUMA node
cat /sys/bus/pci/devices/0000:86:00.0/numa_node  # NIC
# Output: 0

# Step 3: Pin service to NUMA node 0 CPUs
numactl --cpunodebind=0 --membind=0 ./my-inference-service

# Step 4: Verify
numastat -p $(pgrep my-inference)
# Should show most memory on node 0
```

#### **NUMA Misalignment Cost:**

```
Aligned (GPU + NIC + CPU on same NUMA node):
PCIe → GPU → CPU (local memory) → NIC
Latency: 2-5 µs

Misaligned (GPU on node 0, CPU on node 1):
PCIe → GPU → QPI/UPI interconnect → CPU (remote memory) → NIC
Latency: 8-15 µs (3× slower!)
```

**Platform Engineering Decision:** Always verify GPU, NIC, and CPU NUMA affinity before deploying HPC workloads.

---

### **8.6 Practice Questions: Host & OS Tuning**

1. **Your ML inference service has p99 latency spikes of 50ms every ~1 second. The average latency is 5ms. What would you investigate?**

   **Answer:** Timer tick interrupts and THP compaction.
   - Check if `nohz_full` is set for the CPUs running the service
   - Check THP: `cat /sys/kernel/mm/transparent_hugepage/enabled` — if "always", compaction causes pauses
   - Check IRQ affinity: `cat /proc/interrupts` — are IRQs hitting perf CPUs?
   - Check CPU governor: frequency scaling causes inconsistent latency
   - **Fix:** `nohz_full=<cpus>`, `THP=never`, pin IRQs to housekeeping CPUs, governor=performance

2. **You've deployed a NUMA-aware C++ service in a container, but it's not getting NUMA benefits. What's wrong?**

   **Answer:** Container/K8s is not exposing NUMA topology.
   - Kubernetes CPU Manager not in "static" policy
   - Topology Manager not enabled or policy is "none"
   - Container's cpuset doesn't align with NUMA boundaries
   - **Fix:** Enable CPU Manager (static), Topology Manager (single-numa-node), use Guaranteed QoS

3. **A service requests 4GB of huge pages but the allocation fails at runtime. Boot-time allocation succeeded. What happened?**

   **Answer:** Huge pages were allocated but not on the correct NUMA node, or another pod consumed them.
   - Check per-NUMA allocation: `cat /sys/devices/system/node/node0/hugepages/...`
   - Check if other pods are consuming huge pages (K8s hugepages resource)
   - **Fix:** Allocate per-NUMA node, set resource limits in pod spec, use node affinity

---

## Module 9: Kubernetes for Low-Latency Workloads

### **9.1 The Kubernetes Performance Challenge**

#### **Why Kubernetes Hurts HPC by Default:**

```
Default Kubernetes:
┌─────────────────────────────────────────────┐
│  Pod A (CPU: 2, no pinning)                 │
│  → Threads can run on ANY 2 CPUs            │
│  → May span NUMA nodes                      │
│  → Shared with other pods on same CPUs      │
│  → No huge page access                      │
│  → Standard CNI networking (iptables)       │
└─────────────────────────────────────────────┘

Optimized Kubernetes for HPC:
┌─────────────────────────────────────────────┐
│  Pod A (CPU: 2, exclusive, NUMA-aligned)    │
│  → Threads pinned to CPUs 4,5 (exclusive)   │
│  → Both CPUs on NUMA node 0                 │
│  → No other pods share these CPUs           │
│  → 2GB huge pages mounted                   │
│  → SR-IOV VF for direct NIC access          │
│  → GPU on same NUMA node                    │
└─────────────────────────────────────────────┘
```

#### **Key K8s Features for HPC:**

| Feature | Purpose | When to Use |
|---------|---------|-------------|
| CPU Manager (static) | Exclusive CPU pinning | Low-latency inference |
| Topology Manager | NUMA-aware scheduling | Multi-resource alignment |
| Memory Manager | NUMA-aware memory | Large memory workloads |
| Device Plugins | GPU/NIC/FPGA access | Hardware accelerators |
| Huge Pages | TLB efficiency | Large memory regions |
| Host Networking | Bypass kube-proxy | Ultra-low-latency |

---

### **9.2 CPU Manager Policy: Static**

#### **How CPU Manager Works:**

```yaml
# kubelet configuration (/var/lib/kubelet/config.yaml)
apiVersion: kubelet.config.k8s.io/v1beta1
kind: KubeletConfiguration
cpuManagerPolicy: static           # Enable exclusive CPU allocation
cpuManagerPolicyOptions:
  full-pcpus-only: "true"         # Allocate full physical cores (avoid HT sharing)
topologyManagerPolicy: single-numa-node  # All resources from one NUMA node
topologyManagerScope: pod          # Apply per-pod (not per-container)
reservedSystemCPUs: "0-3"          # Reserve CPUs 0-3 for system/kubelet
memoryManagerPolicy: Static        # NUMA-aware memory allocation
```

#### **QoS Classes and CPU Pinning:**

```
Guaranteed QoS (REQUIRED for CPU pinning):
- requests == limits for ALL resources (CPU + memory)
- Gets exclusive CPUs from CPU Manager

Burstable QoS:
- requests < limits
- Shares CPU time with other Burstable/BestEffort pods
- NO CPU pinning

BestEffort QoS:
- No requests or limits set
- Uses leftover resources
- Worst performance
```

#### **Pod Spec for Low-Latency Inference:**

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: fraud-scoring-engine
  annotations:
    # Topology hint: prefer single NUMA node
    topologymanager.kubernetes.io/policy: single-numa-node
spec:
  # Prevent eviction
  priorityClassName: system-critical
  
  # Node selection
  nodeSelector:
    node.kubernetes.io/instance-type: "gpu-hpc"
    feature.node.kubernetes.io/cpu-cpuid.AVX512F: "true"
  
  containers:
  - name: inference-engine
    image: myregistry/fraud-engine:v2.1
    
    resources:
      # Guaranteed QoS: requests == limits
      requests:
        cpu: "8"                    # 8 exclusive CPUs
        memory: "32Gi"             # 32GB memory
        hugepages-2Mi: "4Gi"       # 4GB huge pages
        nvidia.com/gpu: "1"        # 1 GPU
      limits:
        cpu: "8"
        memory: "32Gi"
        hugepages-2Mi: "4Gi"
        nvidia.com/gpu: "1"
    
    volumeMounts:
    - name: hugepage-2mi
      mountPath: /mnt/hugepages
    - name: dev-shm
      mountPath: /dev/shm
    
    # Security context for performance
    securityContext:
      privileged: false
      capabilities:
        add:
        - IPC_LOCK              # Allow mlock (pin memory)
        - SYS_NICE              # Allow real-time priority
        - NET_RAW               # Allow raw sockets (DPDK)
  
  volumes:
  - name: hugepage-2mi
    emptyDir:
      medium: HugePages-2Mi
      sizeLimit: "4Gi"
  - name: dev-shm
    emptyDir:
      medium: Memory
      sizeLimit: "8Gi"           # Large /dev/shm for shared memory IPC
  
  # Prevent scheduling on same node as noisy neighbors
  affinity:
    podAntiAffinity:
      requiredDuringSchedulingIgnoredDuringExecution:
      - labelSelector:
          matchExpressions:
          - key: workload-type
            operator: In
            values: ["batch-processing"]
        topologyKey: kubernetes.io/hostname
```

#### **What Happens Under the Hood:**

```
1. Pod requests 8 CPUs (Guaranteed QoS)
2. CPU Manager allocates 8 exclusive CPUs (e.g., CPUs 4-11)
3. Topology Manager ensures CPUs are on same NUMA node as GPU
4. Memory Manager allocates memory from same NUMA node
5. Pod's cpuset cgroup is set to "4-11"
6. No other pod can use CPUs 4-11
7. Application's numa_alloc/CPU pinning WORKS as intended
```

---

### **9.3 Topology Manager Deep Dive**

#### **Topology Manager Policies:**

| Policy | Behavior | Use Case |
|--------|----------|----------|
| `none` | No topology awareness | Default, general workloads |
| `best-effort` | Try to align, don't reject | Prefer alignment, tolerate mismatch |
| `restricted` | Reject if no aligned resources | NUMA-sensitive workloads |
| `single-numa-node` | ALL resources from one NUMA node | Strict HPC (GPU + CPU + Memory) |

#### **How Topology Manager Coordinates:**

```
Pod requests: 4 CPUs + 1 GPU + 4Gi hugepages

Topology Manager asks each hint provider:
┌─────────────────────────────────────────────────────────┐
│  CPU Manager Hint: "CPUs 4-7 on NUMA 0, CPUs 12-15 on NUMA 1"  │
│  Device Manager Hint: "GPU0 on NUMA 0, GPU1 on NUMA 1"         │
│  Memory Manager Hint: "4Gi hugepages on NUMA 0, 4Gi on NUMA 1" │
└─────────────────────────────────────────────────────────┘

Topology Manager merges hints:
→ Policy: single-numa-node
→ Best fit: NUMA 0 (has CPUs 4-7 + GPU0 + hugepages)
→ Allocate ALL from NUMA 0

Result: CPU 4-7 + GPU0 + 4Gi HP on NUMA 0
→ Zero cross-NUMA traffic for this pod!
```

#### **Node Feature Discovery (NFD):**

```yaml
# Deploy NFD to auto-detect hardware features
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: node-feature-discovery
spec:
  template:
    spec:
      containers:
      - name: nfd-worker
        image: registry.k8s.io/nfd/node-feature-discovery:v0.14.0
        command: ["nfd-worker"]
        args:
        - "-sources=cpu,memory,pci,system,local"
        volumeMounts:
        - name: host-sys
          mountPath: /host-sys
          readOnly: true
```

**Labels NFD adds to nodes:**
```
feature.node.kubernetes.io/cpu-cpuid.AVX512F=true
feature.node.kubernetes.io/cpu-hardware_multithreading=true
feature.node.kubernetes.io/memory-numa=true
feature.node.kubernetes.io/pci-10de.present=true (NVIDIA GPU)
feature.node.kubernetes.io/system-os_release.ID=ubuntu
```

---

### **9.4 Shared Memory & IPC in Kubernetes**

#### **The Challenge:**

Your application uses POSIX shared memory (`shm_open`) for zero-copy IPC between containers in the same pod. By default, Kubernetes gives each pod only **64MB** of `/dev/shm`.

#### **Configuring Shared Memory:**

```yaml
# Method 1: emptyDir with Memory medium (recommended)
volumes:
- name: dshm
  emptyDir:
    medium: Memory
    sizeLimit: "16Gi"  # 16GB shared memory

# Mount in all containers that need IPC
containers:
- name: producer
  volumeMounts:
  - name: dshm
    mountPath: /dev/shm

- name: consumer
  volumeMounts:
  - name: dshm
    mountPath: /dev/shm
# Both containers now share the same /dev/shm (same IPC namespace)
```

#### **IPC Namespace Considerations:**

```yaml
# Default: Each pod has its own IPC namespace
# Containers within a pod SHARE IPC namespace (can use shm, semaphores, message queues)

# To share IPC between pods (rare, security concern):
spec:
  hostIPC: true  # Use host IPC namespace
  # WARNING: All pods with hostIPC can see each other's shared memory
  # Only use for tightly-coupled services that MUST share memory across pods
```

#### **Shared Memory Performance in K8s:**

| Configuration | Latency | Notes |
|---------------|---------|-------|
| Same pod, emptyDir Memory | 100 ns | Same as bare metal |
| Same pod, hostIPC | 100 ns | Shares with host |
| Different pods, hostIPC | 100 ns | Security risk |
| Different pods, network | 10-50 µs | Use gRPC/TCP |

**Project Connection:** Apple (shared memory between pipeline stages), Broadcom (zero-copy between parser and scorer)

---

### **9.5 Pod Disruption & Preemption Control**

#### **Preventing Eviction of HPC Workloads:**

```yaml
# PriorityClass for HPC workloads
apiVersion: scheduling.k8s.io/v1
kind: PriorityClass
metadata:
  name: hpc-critical
value: 1000000
globalDefault: false
preemptionPolicy: Never  # Don't preempt other critical pods
description: "Priority for HPC/inference workloads"

---
# Pod Disruption Budget
apiVersion: policy/v1
kind: PodDisruptionBudget
metadata:
  name: inference-pdb
spec:
  maxUnavailable: 0  # NEVER evict during voluntary disruptions
  selector:
    matchLabels:
      app: fraud-inference
```

#### **Graceful Shutdown for Stateful HPC:**

```yaml
spec:
  terminationGracePeriodSeconds: 300  # 5 min to drain
  containers:
  - name: inference
    lifecycle:
      preStop:
        exec:
          command:
          - /bin/sh
          - -c
          - |
            # Signal service to stop accepting new requests
            kill -SIGUSR1 1
            # Wait for in-flight requests to complete
            while [ $(curl -s localhost:8080/metrics | grep inflight_requests | awk '{print $2}') -gt 0 ]; do
              sleep 1
            done
            # Flush KV-cache to disk for fast restart
            curl -X POST localhost:8080/admin/flush-cache
```

---

### **9.6 Non-Kubernetes Deployment: Bare Metal & VMs**

#### **When NOT to Use Kubernetes:**

| Scenario | Recommendation | Reason |
|----------|---------------|--------|
| Ultra-low-latency (< 10µs) | Bare metal | K8s overhead too high |
| Single-tenant GPU cluster | Bare metal + Slurm | Simpler, less overhead |
| RDMA networking | Bare metal or SR-IOV | Container networking adds latency |
| Multi-tenant inference | Kubernetes + optimization | Best isolation + scheduling |
| Mixed workloads | Kubernetes | Best resource utilization |

#### **Bare Metal Deployment (systemd):**

```bash
# /etc/systemd/system/inference-engine.service
[Unit]
Description=Low-Latency Inference Engine
After=network.target nvidia-persistenced.service
Requires=nvidia-persistenced.service

[Service]
Type=notify
ExecStartPre=/usr/bin/nvidia-smi -pm 1
ExecStartPre=/usr/bin/nvidia-smi -ac 1215,1410
ExecStart=/opt/inference/bin/server \
    --numa-node=0 \
    --cpus=4-15 \
    --gpu=0 \
    --hugepages=/mnt/hugepages-2M

# Performance settings
CPUAffinity=4-15
AllowedCPUs=4-15
AllowedMemoryNodes=0
Nice=-20
LimitMEMLOCK=infinity
LimitNOFILE=1048576
LimitNPROC=65535

# Restart policy
Restart=always
RestartSec=5
WatchdogSec=30

[Install]
WantedBy=multi-user.target
```

#### **Slurm for GPU Clusters:**

```bash
# slurm.conf (GPU-aware scheduling)
GresTypes=gpu
NodeName=gpu-node[01-16] Gres=gpu:a100:8 CPUs=128 RealMemory=1024000

# Job submission with GPU + NUMA awareness
srun --gres=gpu:1 \
     --cpu-bind=map_cpu:4,5,6,7,8,9,10,11 \
     --mem-bind=local \
     ./inference-engine --batch-size=32
```

---

### **9.7 Practice Questions: Kubernetes for HPC**

1. **Your inference pod has 8 CPUs requested but you see latency variance. `perf` shows context switches. What's the K8s configuration issue?**

   **Answer:**
   - Pod is Burstable QoS (requests ≠ limits) → no exclusive CPUs
   - CPU Manager policy is "none" (default) → shared CFS bandwidth
   - **Fix:** Set requests == limits (Guaranteed QoS), enable `cpuManagerPolicy: static`
   - Also check: `full-pcpus-only` to avoid HyperThread sharing

2. **You deployed a GPU inference service but GPU-to-CPU data transfer is 3× slower than bare metal. What's wrong?**

   **Answer:** NUMA misalignment.
   - GPU is on NUMA node 0 but pod's CPUs are on NUMA node 1
   - DMA transfers cross the QPI/UPI interconnect
   - **Fix:** Enable `topologyManagerPolicy: single-numa-node`
   - Verify with `nvidia-smi topo -m` and pod's cpuset

3. **Two containers in the same pod need to share 8GB of data via shared memory. How do you configure this in K8s?**

   **Answer:**
   - Add `emptyDir` volume with `medium: Memory` and `sizeLimit: 8Gi`
   - Mount at `/dev/shm` in both containers
   - Both containers share IPC namespace within the pod
   - No additional security context needed (same pod = same IPC namespace)
   - Add `IPC_LOCK` capability if using `mlock()`

---

## Module 10: Networking Infrastructure

### **10.1 Container Networking Overhead**

#### **Default K8s Networking Path:**

```
Pod A → veth → bridge → iptables/nftables → routing → bridge → veth → Pod B
                                ↑
                    kube-proxy rules (DNAT/SNAT)
                    conntrack table lookup
                    netfilter hooks
                    
Latency: 50-200 µs (vs. 1-5 µs bare metal)
```

#### **Sources of Networking Overhead:**

| Layer | Overhead | Impact |
|-------|----------|--------|
| veth pair | 5-10 µs | Virtual interface crossing |
| Linux bridge | 5-10 µs | L2 forwarding |
| iptables/nftables | 10-50 µs | Rule evaluation (scales with rules) |
| conntrack | 5-20 µs | Connection tracking table |
| SNAT/DNAT | 5-10 µs | Address translation |
| Overlay (VXLAN) | 20-50 µs | Encapsulation/decapsulation |
| **Total** | **50-150 µs** | **vs. 1-5 µs native** |

---

### **10.2 High-Performance CNI Plugins**

#### **CNI Comparison for HPC:**

| CNI | Latency | Throughput | Features | Use Case |
|-----|---------|------------|----------|----------|
| Flannel (VXLAN) | 100-200 µs | 5 Gbps | Simple overlay | General workloads |
| Calico (BGP) | 50-100 µs | 10 Gbps | Network policy | Multi-tenant |
| Cilium (eBPF) | 20-50 µs | 25 Gbps | No iptables | Low-latency |
| SR-IOV | 5-10 µs | 100 Gbps | Direct NIC access | Ultra-low-latency |
| Host networking | 1-5 µs | Line rate | No isolation | Single-tenant HPC |

#### **Cilium (eBPF-based, recommended for HPC):**

```yaml
# Cilium configuration for low-latency
apiVersion: cilium.io/v2alpha1
kind: CiliumConfig
metadata:
  name: cilium-config
spec:
  # Replace kube-proxy entirely
  kubeProxyReplacement: strict
  
  # Use eBPF instead of iptables
  bpf:
    masquerade: true
    hostRouting: true
    # Direct Server Return (skip reverse path)
    loadBalancer:
      mode: dsr
  
  # Disable overlay for local traffic
  tunnel: disabled
  autoDirectNodeRoutes: true
  
  # Enable bandwidth manager
  bandwidthManager:
    enabled: true
    bbr: true  # BBR congestion control
```

**Why Cilium is better for HPC:**
- **No iptables:** eBPF processes packets in kernel, no userspace
- **No conntrack overhead:** Direct routing for known connections
- **XDP (eXpress Data Path):** Process at NIC driver level (before kernel stack)
- **Socket-level LB:** Skip network stack entirely for local services

---

### **10.3 SR-IOV for Direct NIC Access**

#### **What is SR-IOV?**

```
Traditional (shared NIC):
Pod A → veth → bridge → kernel → Physical NIC
Pod B → veth → bridge → kernel → Physical NIC
→ All traffic through kernel, shared bandwidth

SR-IOV (direct NIC access):
Pod A → VF0 (Virtual Function) → Physical NIC
Pod B → VF1 (Virtual Function) → Physical NIC
→ Each pod has direct hardware access, no kernel overhead
```

#### **SR-IOV Setup:**

```bash
# 1. Enable SR-IOV on NIC
echo 8 > /sys/class/net/ens5f0/device/sriov_numvfs

# 2. Verify VFs created
lspci | grep "Virtual Function"
# 0000:86:02.0 Ethernet controller: Mellanox ConnectX-6 VF
# 0000:86:02.1 Ethernet controller: Mellanox ConnectX-6 VF
# ... (8 VFs)

# 3. Configure VF (VLAN, MAC, rate limiting)
ip link set ens5f0 vf 0 mac aa:bb:cc:dd:ee:00 vlan 100
ip link set ens5f0 vf 0 rate 25000  # 25 Gbps limit

# 4. Bind VF to vfio-pci (for DPDK) or leave in kernel
modprobe vfio-pci
echo "0000:86:02.0" > /sys/bus/pci/drivers/vfio-pci/bind
```

#### **SR-IOV in Kubernetes:**

```yaml
# SR-IOV Network Device Plugin (DaemonSet)
apiVersion: sriovnetwork.openshift.io/v1
kind: SriovNetworkNodePolicy
metadata:
  name: gpu-sriov-policy
spec:
  nodeSelector:
    feature.node.kubernetes.io/pci-10de.present: "true"
  numVfs: 8
  nicSelector:
    vendor: "15b3"  # Mellanox
    deviceID: "101b"  # ConnectX-6
  deviceType: netdevice  # or vfio-pci for DPDK
  resourceName: mellanox_sriov_vf

---
# NetworkAttachmentDefinition (Multus)
apiVersion: k8s.cni.cncf.io/v1
kind: NetworkAttachmentDefinition
metadata:
  name: sriov-net
  annotations:
    k8s.v1.cni.cncf.io/resourceName: intel.com/mellanox_sriov_vf
spec:
  config: |
    {
      "type": "sriov",
      "ipam": { "type": "host-local", "subnet": "10.56.0.0/16" }
    }

---
# Pod with SR-IOV interface
apiVersion: v1
kind: Pod
metadata:
  name: rdma-inference
  annotations:
    k8s.v1.cni.cncf.io/networks: sriov-net
spec:
  containers:
  - name: inference
    resources:
      requests:
        intel.com/mellanox_sriov_vf: "1"
      limits:
        intel.com/mellanox_sriov_vf: "1"
```

---

### **10.4 RDMA in Containers**

#### **RDMA Modes in Kubernetes:**

```
Mode 1: Shared RDMA (rdma-shared-device-plugin)
→ Multiple pods share same RDMA device
→ Kernel manages QPs (queue pairs)
→ Good for most inference workloads

Mode 2: Exclusive RDMA (SR-IOV + RDMA)
→ Each pod gets dedicated VF with RDMA capability
→ Direct hardware access
→ Required for high-frequency trading, extreme low-latency
```

#### **RDMA Device Plugin:**

```yaml
# Deploy RDMA shared device plugin
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
        - name: class
          mountPath: /sys/class/infiniband_verbs
      volumes:
      - name: devs
        hostPath:
          path: /dev/infiniband
      - name: class
        hostPath:
          path: /sys/class/infiniband_verbs

---
# Pod using RDMA
apiVersion: v1
kind: Pod
metadata:
  name: kv-cache-transfer
spec:
  containers:
  - name: transfer-engine
    resources:
      requests:
        rdma/hca_shared_devices_a: "1"
      limits:
        rdma/hca_shared_devices_a: "1"
    securityContext:
      capabilities:
        add: ["IPC_LOCK"]  # Required for RDMA memory registration
```

**Project Connection:** Data-plane NIXL (RDMA-based KV-cache transfer between GPUs across nodes)

---

### **10.5 Network Tuning Parameters**

#### **Kernel Network Stack Tuning:**

```bash
# /etc/sysctl.d/99-network-hpc.conf

# --- Buffer Sizes ---
net.core.rmem_max = 134217728           # 128MB max receive buffer
net.core.wmem_max = 134217728           # 128MB max send buffer
net.core.rmem_default = 16777216        # 16MB default receive
net.core.wmem_default = 16777216        # 16MB default send
net.ipv4.tcp_rmem = 4096 1048576 134217728  # min, default, max
net.ipv4.tcp_wmem = 4096 1048576 134217728

# --- Connection Handling ---
net.core.somaxconn = 65535              # Max listen backlog
net.core.netdev_max_backlog = 250000    # NIC ring buffer backlog
net.ipv4.tcp_max_syn_backlog = 65535    # SYN queue size
net.ipv4.tcp_max_tw_buckets = 2000000  # TIME_WAIT bucket limit

# --- Low-Latency TCP ---
net.ipv4.tcp_low_latency = 1           # Prefer latency over throughput
net.ipv4.tcp_fastopen = 3              # TCP Fast Open (client + server)
net.ipv4.tcp_nodelay = 1               # Disable Nagle's algorithm
net.ipv4.tcp_timestamps = 0            # Disable timestamps (saves 12 bytes/packet)
net.ipv4.tcp_sack = 1                  # Selective ACK (keeps)

# --- Congestion Control ---
net.ipv4.tcp_congestion_control = bbr  # Google BBR (better for data center)
net.core.default_qdisc = fq            # Fair Queue (required for BBR)

# --- Busy Polling (userspace poll, reduces latency) ---
net.core.busy_poll = 50                # µs to busy-poll before blocking
net.core.busy_read = 50                # µs to busy-read

# --- Disable features that add latency ---
net.ipv4.tcp_slow_start_after_idle = 0 # Don't reset cwnd after idle
net.ipv4.tcp_mtu_probing = 1           # Enable MTU probing
```

#### **NIC Ring Buffer & Coalescing:**

```bash
# Increase NIC ring buffer (fewer drops under burst)
ethtool -G eth0 rx 8192 tx 8192

# Tune interrupt coalescing (trade throughput for latency)
# Low-latency mode:
ethtool -C eth0 rx-usecs 0 rx-frames 1 tx-usecs 0 tx-frames 1
# This means: interrupt immediately on each packet (lowest latency, highest CPU)

# Throughput mode:
ethtool -C eth0 rx-usecs 100 rx-frames 64 tx-usecs 100 tx-frames 64
# This means: batch 64 packets or wait 100µs (higher throughput, higher latency)

# Enable hardware offloads
ethtool -K eth0 rx-checksum on tx-checksum-ip-generic on \
    tso on gro on gso on lro off  # Disable LRO for low-latency

# Jumbo frames (9000 MTU for intra-DC traffic)
ip link set eth0 mtu 9000
```

#### **Multi-Queue NIC Configuration:**

```bash
# Modern NICs have multiple RX/TX queues (one per CPU core)
ethtool -l eth0
# Combined: 32 (max)

# Set queue count to match number of performance CPUs
ethtool -L eth0 combined 16

# Pin each queue's IRQ to specific CPU
# Queue 0 → CPU 0, Queue 1 → CPU 1, etc.
/opt/mellanox/bin/set_irq_affinity_cpulist.sh 0-15 eth0

# Flow steering (RSS: Receive Side Scaling)
ethtool -X eth0 equal 16  # Distribute evenly across 16 queues

# Or use custom flow rules to pin specific traffic to specific CPUs
ethtool -N eth0 flow-type tcp4 dst-port 8080 action 4  # Port 8080 → queue 4
```

---

### **10.6 Host Networking vs. Pod Networking**

#### **When to Use hostNetwork:**

```yaml
# Host networking: Pod uses host's network namespace directly
spec:
  hostNetwork: true
  dnsPolicy: ClusterFirstWithHostNet
  containers:
  - name: inference
    ports:
    - containerPort: 8080
      hostPort: 8080
```

| Aspect | Pod Network | Host Network |
|--------|-------------|--------------|
| Latency | 50-200 µs overhead | Native (0 overhead) |
| Isolation | Full (separate namespace) | None (shared with host) |
| Port conflicts | No | Yes (must manage ports) |
| Service discovery | Full K8s services | Manual or hostPort |
| Security | Strong (NetworkPolicy) | Weak (host-level only) |
| Use case | General workloads | Ultra-low-latency, DPDK |

**Decision Framework:**
- **Use pod networking** for: multi-tenant, general services, need NetworkPolicy
- **Use SR-IOV** for: low-latency + isolation (best of both worlds)
- **Use host networking** for: single-tenant, extreme latency, DPDK/RDMA

---

### **10.7 Practice Questions: Networking**

1. **Your inference service has 200µs network latency between pods on the same node. How do you reduce it to < 10µs?**

   **Answer:**
   - Current: Standard CNI (veth + bridge + iptables) = 50-200µs
   - Option 1: Cilium with eBPF + socket-level LB → 20-50µs
   - Option 2: SR-IOV (dedicated VF per pod) → 5-10µs
   - Option 3: Shared memory IPC (same pod, different containers) → 100ns
   - **Best for same-node inference:** Put cooperating services in same pod with shared memory

2. **You need RDMA between GPU nodes for KV-cache transfer. How do you configure this in K8s?**

   **Answer:**
   - Deploy RDMA device plugin or SR-IOV network operator
   - Create NetworkAttachmentDefinition for RDMA network
   - Pod requests `rdma/hca_shared_devices` resource
   - Add `IPC_LOCK` capability for memory registration
   - Ensure pods on same RDMA fabric (subnet/partition)
   - Use Topology Manager to align RDMA NIC with GPU (same PCIe switch)

3. **Explain the tradeoff between interrupt coalescing settings.**

   **Answer:**
   - `rx-usecs=0, rx-frames=1`: Interrupt on EVERY packet. Lowest latency (~1µs), highest CPU usage
   - `rx-usecs=100, rx-frames=64`: Batch packets. Higher latency (~100µs), lower CPU
   - **For HPC inference:** Use low coalescing on dedicated IRQ CPUs
   - **For throughput:** Use higher coalescing, process in batches

---

## Module 11: GPU Infrastructure & Device Management

### **11.1 NVIDIA GPU Operator**

#### **GPU Operator Components:**

```
┌─────────────────────────────────────────────────────────────┐
│  NVIDIA GPU Operator (manages all GPU components)            │
├──────────────────────────────────────────────────────────────┤
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐   │
│  │ GPU Driver    │  │ Device Plugin │  │ DCGM Exporter │   │
│  │ (DaemonSet)   │  │ (DaemonSet)   │  │ (DaemonSet)   │   │
│  └───────────────┘  └───────────────┘  └───────────────┘   │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐   │
│  │ Container     │  │ MIG Manager   │  │ GPU Feature   │   │
│  │ Toolkit       │  │               │  │ Discovery     │   │
│  └───────────────┘  └───────────────┘  └───────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

#### **Installation:**

```bash
# Install GPU Operator via Helm
helm repo add nvidia https://helm.ngc.nvidia.com/nvidia
helm repo update

helm install gpu-operator nvidia/gpu-operator \
  --namespace gpu-operator \
  --create-namespace \
  --set driver.enabled=true \
  --set toolkit.enabled=true \
  --set devicePlugin.enabled=true \
  --set dcgmExporter.enabled=true \
  --set migManager.enabled=true \
  --set gfd.enabled=true
```

---

### **11.2 Multi-Instance GPU (MIG)**

#### **What is MIG?**

MIG (Multi-Instance GPU) partitions a single GPU into multiple isolated instances, each with dedicated:
- **Compute:** SM (Streaming Multiprocessors)
- **Memory:** Dedicated VRAM partition
- **Cache:** Separate L2 cache

```
A100 80GB without MIG:
┌─────────────────────────────────────────┐
│  Full GPU: 108 SMs, 80GB VRAM           │
│  Pod A uses 100% (even if needs 20%)    │
└─────────────────────────────────────────┘

A100 80GB with MIG (7 instances):
┌─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│ 1g  │ 1g  │ 1g  │ 1g  │ 1g  │ 1g  │ 1g  │
│ 10gb│ 10gb│ 10gb│ 10gb│ 10gb│ 10gb│ 10gb│
└─────┴─────┴─────┴─────┴─────┴─────┴─────┘
7 isolated instances, each with 14 SMs + 10GB
```

#### **MIG Configuration:**

```bash
# Enable MIG mode (requires GPU reset)
nvidia-smi -i 0 -mig 1

# View available MIG profiles
nvidia-smi mig -lgip
# +---+----------------------+--------+--------+
# | # | Profile              | Memory | SM     |
# +---+----------------------+--------+--------+
# | 0 | MIG 1g.10gb         | 10 GB  | 14 SMs |
# | 1 | MIG 2g.20gb         | 20 GB  | 28 SMs |
# | 2 | MIG 3g.40gb         | 40 GB  | 42 SMs |
# | 3 | MIG 4g.40gb         | 40 GB  | 56 SMs |
# | 4 | MIG 7g.80gb         | 80 GB  | 98 SMs |
# +---+----------------------+--------+--------+

# Create MIG instances
# Strategy 1: 7 small instances (multi-tenant, small models)
nvidia-smi mig -cgi 0,0,0,0,0,0,0 -C

# Strategy 2: Mixed (2 large + 3 small)
nvidia-smi mig -cgi 1,1,0,0,0 -C

# Strategy 3: 1 large instance (single large model)
nvidia-smi mig -cgi 4 -C

# Verify
nvidia-smi mig -lgi
```

#### **MIG in Kubernetes:**

```yaml
# MIG strategy in GPU device plugin config
apiVersion: v1
kind: ConfigMap
metadata:
  name: nvidia-device-plugin-config
data:
  config.yaml: |
    version: v1
    sharing:
      mig:
        strategy: mixed  # or "single" or "none"
        resources:
        - name: nvidia.com/mig-1g.10gb
          rename: nvidia.com/gpu-small
        - name: nvidia.com/mig-3g.40gb
          rename: nvidia.com/gpu-medium
        - name: nvidia.com/mig-7g.80gb
          rename: nvidia.com/gpu-large

---
# Pod requesting specific MIG slice
apiVersion: v1
kind: Pod
metadata:
  name: small-model-inference
spec:
  containers:
  - name: inference
    image: myregistry/small-model:v1
    resources:
      requests:
        nvidia.com/mig-1g.10gb: "1"  # Small MIG slice
      limits:
        nvidia.com/mig-1g.10gb: "1"
```

#### **MIG vs. Time-Slicing vs. MPS:**

| Feature | MIG | Time-Slicing | MPS |
|---------|-----|--------------|-----|
| **Isolation** | Full (memory, compute, cache) | None (shared everything) | Partial (shared memory) |
| **Overhead** | None | Context switch overhead | Minimal |
| **Granularity** | Fixed profiles | Any fraction | Any fraction |
| **Error Isolation** | Yes (GPU fault isolated) | No (one fault kills all) | No |
| **Best For** | Multi-tenant production | Dev/test, burstable | Cooperative multi-process |
| **GPU Support** | A100, H100 only | All NVIDIA GPUs | Volta+ |
| **Max Instances** | 7 (A100) | Unlimited (time-shared) | 48 processes |

---

### **11.3 GPU Time-Slicing**

#### **When MIG Isn't Available (T4, V100):**

```yaml
# GPU time-slicing configuration
apiVersion: v1
kind: ConfigMap
metadata:
  name: nvidia-device-plugin-config
data:
  config.yaml: |
    version: v1
    sharing:
      timeSlicing:
        renameByDefault: false
        resources:
        - name: nvidia.com/gpu
          replicas: 4  # Each physical GPU appears as 4 virtual GPUs
          # WARNING: No memory isolation! OOM can crash all workloads
```

**Time-Slicing Behavior:**
```
Physical GPU: 1× A100 80GB
Time-sliced: 4× "virtual GPUs"

Pod A gets nvidia.com/gpu: 1 → Actually gets ~25% time, shared 80GB memory
Pod B gets nvidia.com/gpu: 1 → Actually gets ~25% time, shared 80GB memory
Pod C gets nvidia.com/gpu: 1 → Actually gets ~25% time, shared 80GB memory
Pod D gets nvidia.com/gpu: 1 → Actually gets ~25% time, shared 80GB memory

⚠️  If Pod A allocates 70GB, Pods B/C/D will OOM!
```

---

### **11.4 GPU Health Monitoring (DCGM)**

#### **DCGM Exporter for Prometheus:**

```yaml
# DCGM Exporter DaemonSet (via GPU Operator)
# Exposes metrics at :9400/metrics

# Key metrics to monitor:
# DCGM_FI_DEV_GPU_UTIL          - GPU utilization %
# DCGM_FI_DEV_MEM_COPY_UTIL     - Memory bandwidth utilization %
# DCGM_FI_DEV_FB_FREE           - Free framebuffer memory (MB)
# DCGM_FI_DEV_FB_USED           - Used framebuffer memory (MB)
# DCGM_FI_DEV_GPU_TEMP          - GPU temperature (°C)
# DCGM_FI_DEV_POWER_USAGE       - Power usage (W)
# DCGM_FI_DEV_SM_CLOCK          - SM clock frequency (MHz)
# DCGM_FI_DEV_PCIE_TX_THROUGHPUT - PCIe TX throughput (KB/s)
# DCGM_FI_DEV_PCIE_RX_THROUGHPUT - PCIe RX throughput (KB/s)
# DCGM_FI_DEV_XID_ERRORS        - XID error count (GPU faults)
# DCGM_FI_DEV_RETIRED_PAGES     - Retired memory pages (hardware errors)
```

#### **GPU Health-Based Pod Scheduling:**

```yaml
# Custom scheduler that checks GPU health before placement
apiVersion: v1
kind: ConfigMap
metadata:
  name: gpu-health-policy
data:
  policy.yaml: |
    rules:
    - name: temperature-check
      metric: DCGM_FI_DEV_GPU_TEMP
      threshold: 85  # Don't schedule if > 85°C
      action: avoid
    
    - name: memory-check
      metric: DCGM_FI_DEV_FB_FREE
      threshold: 10240  # Need at least 10GB free
      action: require
    
    - name: xid-error-check
      metric: DCGM_FI_DEV_XID_ERRORS
      threshold: 0  # Any XID errors = avoid GPU
      action: avoid
    
    - name: ecc-error-check
      metric: DCGM_FI_DEV_RETIRED_PAGES
      threshold: 5  # More than 5 retired pages = quarantine
      action: quarantine
```

#### **Automated GPU Quarantine:**

```bash
#!/bin/bash
# gpu-health-monitor.sh - Runs on each GPU node

while true; do
    # Check for XID errors (GPU hardware faults)
    xid_errors=$(nvidia-smi --query-gpu=xid_errors --format=csv,noheader -i 0)
    
    if [[ "$xid_errors" != "N/A" && "$xid_errors" -gt 0 ]]; then
        echo "GPU 0 has XID errors: $xid_errors"
        
        # Cordon node to prevent new scheduling
        kubectl cordon $(hostname)
        
        # Label GPU as unhealthy
        kubectl label node $(hostname) gpu-health=degraded --overwrite
        
        # Alert
        curl -X POST "http://alertmanager:9093/api/v1/alerts" \
            -H "Content-Type: application/json" \
            -d "[{\"labels\":{\"alertname\":\"GPUXIDError\",\"node\":\"$(hostname)\",\"severity\":\"critical\"}}]"
    fi
    
    sleep 30
done
```

**Project Connection:** CapitalOne (DCGM-based GPU health monitoring for 99.999% availability)

---

### **11.5 GPU Persistence & Clock Settings**

#### **NVIDIA Persistence Daemon:**

```bash
# Without persistence: First CUDA call takes 1-3 seconds (driver initialization)
# With persistence: First CUDA call takes < 10ms

# Enable persistence mode
nvidia-smi -pm 1  # Or: systemctl enable nvidia-persistenced

# Set application clocks (fixed, no boost/throttle)
# A100: Max memory clock 1215 MHz, Max SM clock 1410 MHz
nvidia-smi -ac 1215,1410

# Or lock to specific clocks (prevents any frequency changes)
nvidia-smi --lock-gpu-clocks=1410,1410
nvidia-smi --lock-memory-clocks=1215,1215

# Set power limit (if thermal throttling is a concern)
nvidia-smi -pl 300  # 300W power limit (A100 TDP = 400W)

# Configure compute mode
nvidia-smi -c EXCLUSIVE_PROCESS  # Only one process per GPU
# Options: DEFAULT, EXCLUSIVE_THREAD, EXCLUSIVE_PROCESS, PROHIBITED
```

#### **systemd Service for GPU Init:**

```bash
# /etc/systemd/system/nvidia-gpu-init.service
[Unit]
Description=NVIDIA GPU Initialization
After=nvidia-persistenced.service
Requires=nvidia-persistenced.service

[Service]
Type=oneshot
RemainAfterExit=true
ExecStart=/bin/bash -c '\
    nvidia-smi -pm 1 && \
    nvidia-smi -ac 1215,1410 && \
    nvidia-smi --lock-gpu-clocks=1410,1410 && \
    nvidia-smi -c EXCLUSIVE_PROCESS'

[Install]
WantedBy=multi-user.target
```

---

### **11.6 Practice Questions: GPU Infrastructure**

1. **You have 4 A100 GPUs and need to serve 20 different small models (each needs < 10GB VRAM). How do you configure GPU sharing?**

   **Answer:**
   - Use MIG: Each A100 → 7× `1g.10gb` instances = 28 MIG slices for 4 GPUs
   - Each model gets 1 MIG slice (10GB VRAM, 14 SMs)
   - Full isolation between models (memory faults don't propagate)
   - Configure via GPU Operator's MIG Manager
   - **Why not time-slicing:** No memory isolation, OOM risk
   - **Why not MPS:** No fault isolation

2. **A GPU shows XID error 79 (GPU fallen off the bus). What's your response procedure?**

   **Answer:**
   1. Immediately cordon the node (`kubectl cordon`)
   2. Drain non-critical workloads (`kubectl drain --ignore-daemonsets`)
   3. Check if it's transient: reset GPU (`nvidia-smi -r -i <gpu_id>`)
   4. If persists: Check PCIe link (`nvidia-smi -q -d PCIE`), check `dmesg` for AER errors
   5. Mark GPU as quarantined in labels
   6. File hardware replacement ticket
   7. Monitor other GPUs on same node (PCIe switch failure can cascade)

3. **Why should you lock GPU clocks for inference workloads?**

   **Answer:**
   - Default: GPU boosts/throttles based on thermal + power
   - This causes **latency variance**: p50=5ms but p99=15ms (during throttle)
   - Locking clocks gives consistent performance (p50≈p99)
   - Tradeoff: Slightly lower peak throughput, but predictable latency
   - **For inference:** Lock clocks (latency SLA matters)
   - **For training:** Allow boost (throughput matters, latency doesn't)

---

## Module 12: Storage & Memory Infrastructure

### **12.1 Storage for HPC Workloads**

#### **Storage Hierarchy for ML/HPC:**

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 1: GPU VRAM (fastest, smallest)                       │
│  • Model weights, KV-cache, activations                      │
│  • Capacity: 40-80 GB per GPU                                │
│  • Bandwidth: 2-3 TB/s (HBM3)                               │
├─────────────────────────────────────────────────────────────┤
│  Layer 2: Host Memory + Huge Pages (fast, medium)            │
│  • Model loading buffer, preprocessing                       │
│  • Capacity: 256 GB - 2 TB per node                          │
│  • Bandwidth: 200-400 GB/s (DDR5)                            │
├─────────────────────────────────────────────────────────────┤
│  Layer 3: Local NVMe SSD (medium, large)                     │
│  • Model checkpoints, KV-cache overflow                      │
│  • Capacity: 1-8 TB per node                                 │
│  • Bandwidth: 7 GB/s (PCIe 4.0 x4)                          │
├─────────────────────────────────────────────────────────────┤
│  Layer 4: Network Storage (slow, largest)                    │
│  • Model repository, training data                           │
│  • Capacity: PB scale                                        │
│  • Bandwidth: 10-100 Gbps (network-limited)                  │
└─────────────────────────────────────────────────────────────┘
```

#### **Local NVMe for KV-Cache Overflow:**

```yaml
# Local NVMe StorageClass (no network round-trip)
apiVersion: storage.k8s.io/v1
kind: StorageClass
metadata:
  name: local-nvme
provisioner: kubernetes.io/no-provisioner
volumeBindingMode: WaitForFirstConsumer  # Bind after pod scheduled

---
# PersistentVolume for local NVMe
apiVersion: v1
kind: PersistentVolume
metadata:
  name: nvme-node01-pv
spec:
  capacity:
    storage: 1Ti
  volumeMode: Filesystem
  accessModes:
  - ReadWriteOnce
  persistentVolumeReclaimPolicy: Delete
  storageClassName: local-nvme
  local:
    path: /mnt/nvme0  # Direct NVMe mount
  nodeAffinity:
    required:
      nodeSelectorTerms:
      - matchExpressions:
        - key: kubernetes.io/hostname
          operator: In
          values: ["gpu-node-01"]
```

#### **tmpfs for Shared Memory (RAM-backed):**

```yaml
# tmpfs volume for inter-container communication
volumes:
- name: shared-memory
  emptyDir:
    medium: Memory     # RAM-backed (not disk)
    sizeLimit: "16Gi"  # Counts against pod memory limit

- name: model-cache
  emptyDir:
    medium: HugePages-2Mi  # Huge-page backed tmpfs
    sizeLimit: "8Gi"
```

---

### **12.2 GPUDirect Storage (GDS)**

#### **What is GPUDirect Storage?**

```
Traditional Storage Path:
NVMe → CPU Memory (bounce buffer) → GPU Memory
→ 2 copies, CPU involved, limited by PCIe bandwidth once

GPUDirect Storage:
NVMe → GPU Memory (direct DMA)
→ 1 copy, no CPU involvement, full PCIe bandwidth
→ 2-3× faster for model loading
```

#### **Enabling GDS:**

```bash
# Install GDS package
apt install nvidia-gds

# Verify GDS is working
/usr/local/cuda/gds/tools/gds_stats

# Mount filesystem with GDS support (ext4 or XFS)
mount -o dax /dev/nvme0n1p1 /mnt/gds-storage

# Kubernetes: Use GPUDirect Storage CSI driver
helm install nvidia-gds-driver nvidia/nvidia-gds-driver
```

#### **Performance Comparison:**

| Path | Model Load (7B, 14GB) | Bandwidth |
|------|----------------------|-----------|
| Network → CPU → GPU | 5-10 seconds | 1-2 GB/s |
| Local NVMe → CPU → GPU | 2-3 seconds | 5-6 GB/s |
| Local NVMe → GPU (GDS) | 1-1.5 seconds | 10-12 GB/s |
| NVMe-oF → GPU (GDS) | 2-3 seconds | 8-10 GB/s |

---

### **12.3 Memory Oversubscription & OOM Management**

#### **GPU Memory Oversubscription:**

```yaml
# Kubernetes doesn't natively understand GPU memory
# Use NVIDIA's GPU memory management features:

# Option 1: MIG (hard memory partition)
# Pod gets exactly 10GB, cannot exceed

# Option 2: CUDA_MEM_LIMIT (soft limit, process level)
env:
- name: CUDA_MEM_LIMIT
  value: "10737418240"  # 10GB limit

# Option 3: Unified Memory (GPU + CPU memory pool)
# Allows GPU to "page" to CPU memory when VRAM full
# Good for large models that don't fit in VRAM
env:
- name: CUDA_MANAGED_FORCE_DEVICE_ALLOC
  value: "0"  # Allow managed memory to overflow to CPU
```

#### **Host OOM Configuration:**

```bash
# Prevent OOM killer from killing inference processes
# /proc/<pid>/oom_score_adj = -1000 (never kill)
echo -1000 > /proc/$(pgrep inference-engine)/oom_score_adj

# In Kubernetes: Use Guaranteed QoS (oom_score_adj = -997)
# Burstable: oom_score_adj = 2-999 (higher = more likely to be killed)
# BestEffort: oom_score_adj = 1000 (first to be killed)

# System-level OOM tuning
vm.overcommit_memory = 0  # Don't overcommit (fail malloc if no memory)
vm.panic_on_oom = 0       # Don't panic, kill process instead
vm.oom_kill_allocating_task = 1  # Kill the allocating task, not random
```

---

### **12.4 Practice Questions: Storage & Memory**

1. **Your model loading takes 10 seconds (14GB model from network storage to GPU). How do you reduce to < 2 seconds?**

   **Answer:**
   - Use local NVMe storage for model weights (5-6 GB/s) → ~2.5s
   - Enable GPUDirect Storage for direct NVMe→GPU DMA (10-12 GB/s) → ~1.2s
   - Pre-load models on pod startup (warm cache)
   - Use model sharding (load portions in parallel across multiple NVMe)
   - Consider NVIDIA MPS for model pre-loading

2. **A pod is getting OOM-killed but the node has plenty of memory. What's happening?**

   **Answer:**
   - Pod's memory *limit* is set too low (cgroup limit, not node limit)
   - Huge pages are allocated but not counted in cgroup memory
   - `/dev/shm` (tmpfs) counts against memory limit
   - **Fix:** Increase memory limit, or check if tmpfs usage is excessive
   - For GPU: Check if CUDA memory is leaking (use `nvidia-smi` to track)

---

## Module 13: Observability & SRE for HPC

### **13.1 Metrics Stack for HPC**

#### **The Four Pillars of HPC Observability:**

```
┌─────────────────────────────────────────────────────────────┐
│  1. Infrastructure Metrics                                    │
│     • CPU utilization per core, IPC (instructions per cycle)  │
│     • Memory bandwidth, NUMA locality                         │
│     • Network throughput, packet drops, latency               │
│     • GPU utilization, memory, temperature, power             │
├─────────────────────────────────────────────────────────────┤
│  2. Application Metrics                                       │
│     • Request latency (p50, p95, p99, p999)                  │
│     • Throughput (requests/sec, tokens/sec)                   │
│     • Queue depth, batch size, cache hit rate                 │
│     • Model-specific: TTFT, TPOT, KV-cache utilization       │
├─────────────────────────────────────────────────────────────┤
│  3. Kernel/OS Metrics (eBPF)                                  │
│     • Context switches, CPU migrations                        │
│     • Page faults (major/minor), TLB misses                  │
│     • Scheduler latency, run queue length                     │
│     • Lock contention, futex wait time                        │
├─────────────────────────────────────────────────────────────┤
│  4. Hardware Counters (PMU/DCGM)                              │
│     • Cache hit/miss ratio (L1, L2, L3)                      │
│     • Branch misprediction rate                               │
│     • Memory bandwidth utilization                            │
│     • PCIe throughput, NVLink bandwidth                       │
└─────────────────────────────────────────────────────────────┘
```

#### **Prometheus Metrics Collection:**

```yaml
# Custom metrics from HPC service
# Expose at :9090/metrics

# Latency histogram (microsecond resolution)
inference_request_duration_microseconds_bucket{model="fraud-v2",le="1000"} 450
inference_request_duration_microseconds_bucket{model="fraud-v2",le="5000"} 980
inference_request_duration_microseconds_bucket{model="fraud-v2",le="10000"} 999
inference_request_duration_microseconds_bucket{model="fraud-v2",le="+Inf"} 1000

# GPU metrics (from DCGM exporter)
DCGM_FI_DEV_GPU_UTIL{gpu="0",UUID="GPU-xxx"} 85
DCGM_FI_DEV_FB_USED{gpu="0"} 65536
DCGM_FI_DEV_POWER_USAGE{gpu="0"} 280

# System metrics (from node exporter + custom)
node_cpu_seconds_total{cpu="4",mode="idle"} 1000
node_memory_MemFree_bytes 34359738368
node_numa_memory_total{node="0"} 68719476736
```

---

### **13.2 eBPF for Deep System Observability**

#### **Why eBPF for HPC?**

Traditional profiling (perf, strace) adds overhead. eBPF runs in kernel with **near-zero overhead**.

```python
# bpftrace one-liners for HPC debugging

# Track context switches per CPU (should be ~0 on isolated CPUs)
bpftrace -e 'tracepoint:sched:sched_switch { @[cpu] = count(); }'

# Measure scheduler latency (time between wakeup and run)
bpftrace -e 'tracepoint:sched:sched_wakeup { @start[tid] = nsecs; }
             tracepoint:sched:sched_switch /args->prev_state == 0/ {
               if (@start[tid]) { @usecs = hist((nsecs - @start[tid])/1000); delete(@start[tid]); }
             }'

# Track page faults (should be 0 after warm-up with huge pages)
bpftrace -e 'tracepoint:exceptions:page_fault_user { @[comm] = count(); }'

# Monitor NUMA remote memory accesses
bpftrace -e 'tracepoint:migrate:mm_migrate_pages { @[comm] = count(); }'

# Track futex contention (lock waits)
bpftrace -e 'tracepoint:syscalls:sys_enter_futex /args->op == 0/ { @[comm] = count(); }'
```

#### **Custom eBPF Programs for GPU Workloads:**

```python
# Track GPU memory allocation patterns
# Uses CUDA interposition library + eBPF

# Monitor cudaMalloc latency
bpftrace -e 'uprobe:/usr/local/cuda/lib64/libcudart.so:cudaMalloc { @start[tid] = nsecs; }
             uretprobe:/usr/local/cuda/lib64/libcudart.so:cudaMalloc {
               @alloc_us = hist((nsecs - @start[tid])/1000);
               delete(@start[tid]);
             }'

# Track PCIe DMA transfer sizes
bpftrace -e 'uprobe:/usr/local/cuda/lib64/libcudart.so:cudaMemcpy {
               @sizes = hist(arg2);  # arg2 = size
             }'
```

---

### **13.3 SLO/SLA Definition for HPC Services**

#### **SLO Framework:**

```yaml
# SLO definitions for ML inference service
slos:
  - name: inference-latency
    description: "End-to-end inference latency"
    indicator:
      type: latency
      metric: inference_request_duration_microseconds
    objectives:
      - target: 0.99    # 99% of requests
        threshold: 5000  # < 5ms
      - target: 0.999   # 99.9% of requests
        threshold: 10000 # < 10ms
      - target: 0.9999  # 99.99% of requests
        threshold: 50000 # < 50ms
    
  - name: gpu-availability
    description: "GPU available for scheduling"
    indicator:
      type: availability
      metric: DCGM_FI_DEV_GPU_UTIL > 0
    objectives:
      - target: 0.9999  # 99.99% uptime (< 53 min/year downtime)
    
  - name: throughput
    description: "Inference throughput"
    indicator:
      type: throughput
      metric: inference_requests_total
    objectives:
      - target: 10000   # Minimum 10K requests/sec sustained
```

#### **Error Budget Calculation:**

```
Monthly error budget for 99.99% availability:
- Total minutes/month: 43,200
- Allowed downtime: 43,200 × 0.01% = 4.32 minutes/month
- Current burn rate: If consuming 2 min/week → 8 min/month → OVER BUDGET

Response:
- Burn rate > 1×: Alert team
- Burn rate > 2×: Page on-call
- Burn rate > 5×: Incident, freeze deployments
```

---

### **13.4 Alerting Rules for HPC:**

```yaml
# Prometheus alerting rules
groups:
- name: hpc-alerts
  rules:
  # GPU thermal throttling
  - alert: GPUThermalThrottle
    expr: DCGM_FI_DEV_GPU_TEMP > 83
    for: 2m
    labels:
      severity: warning
    annotations:
      summary: "GPU {{ $labels.gpu }} temperature {{ $value }}°C"
      
  # GPU memory exhaustion
  - alert: GPUMemoryLow
    expr: (DCGM_FI_DEV_FB_FREE / DCGM_FI_DEV_FB_TOTAL) < 0.1
    for: 5m
    labels:
      severity: critical
    annotations:
      summary: "GPU {{ $labels.gpu }} memory < 10% free"
  
  # Latency SLO breach
  - alert: InferenceLatencyHigh
    expr: |
      histogram_quantile(0.99, 
        rate(inference_request_duration_microseconds_bucket[5m])
      ) > 10000
    for: 5m
    labels:
      severity: critical
    annotations:
      summary: "p99 latency {{ $value }}µs exceeds 10ms SLO"
  
  # NUMA misalignment detected
  - alert: NUMARemoteAccess
    expr: rate(node_numa_interleave_hit_total[5m]) / 
          (rate(node_numa_hit_total[5m]) + rate(node_numa_miss_total[5m])) < 0.9
    for: 10m
    labels:
      severity: warning
    annotations:
      summary: "NUMA locality < 90% on {{ $labels.instance }}"
  
  # Context switches on isolated CPUs
  - alert: IsolatedCPUContextSwitches
    expr: rate(node_context_switches_total{cpu=~"[4-9]|[12-31]"}[1m]) > 100
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "Isolated CPU {{ $labels.cpu }} has context switches"
```

---

### **13.5 Practice Questions: Observability**

1. **Your inference p99 latency jumped from 5ms to 50ms. Walk through your debugging process.**

   **Answer:**
   1. Check GPU metrics (DCGM): Is GPU throttling? Memory full? XID errors?
   2. Check system metrics: CPU migration? Context switches on perf CPUs? Page faults?
   3. Check network: Increased latency? Packet drops? Retransmissions?
   4. Check application: Batch size spike? KV-cache eviction? Model reload?
   5. Use eBPF: `runqlat` for scheduler delay, `biolatency` for I/O
   6. Common causes: THP compaction, IRQ storm on perf CPUs, noisy neighbor, GPU thermal throttle

2. **How do you detect NUMA misalignment in production without adding latency?**

   **Answer:**
   - Use hardware performance counters (PMU): `perf stat -e numa-stores-remote`
   - eBPF tracepoint: `migrate:mm_migrate_pages` (pages moving between nodes)
   - Prometheus: `node_numa_miss_total` vs `node_numa_hit_total`
   - DCGM: PCIe throughput (cross-NUMA GPU access shows lower bandwidth)
   - **Zero overhead** because hardware counters and eBPF run in kernel

---

## Module 14: Platform Hands-On Labs

### **Lab 4: Configure Kubernetes Node for HPC**

**Objective:** Set up a K8s worker node optimized for low-latency inference.

**Steps:**
1. Configure kernel boot parameters (isolcpus, hugepages, nohz_full)
2. Set up CPU Manager with static policy
3. Enable Topology Manager (single-numa-node)
4. Configure huge pages (2MB and 1GB)
5. Deploy a test pod and verify:
   - Exclusive CPU pinning (check cgroup cpuset)
   - NUMA alignment (numastat)
   - Huge page access (check /proc/meminfo in pod)
6. Benchmark: Compare latency with/without optimizations

**Expected Results:**
- Without tuning: p99 = 15ms, variance = 10ms
- With tuning: p99 = 5ms, variance = 0.5ms
- **3× better p99, 20× less variance**

---

### **Lab 5: SR-IOV Network Setup**

**Objective:** Deploy SR-IOV for direct NIC access in pods.

**Steps:**
1. Enable SR-IOV on NIC (create 8 VFs)
2. Install SR-IOV Network Device Plugin
3. Create NetworkAttachmentDefinition
4. Deploy pod with SR-IOV interface
5. Benchmark: iperf3 throughput and latency vs. standard CNI

**Expected Results:**
- Standard CNI: 10 Gbps, 50µs latency
- SR-IOV: 100 Gbps, 5µs latency
- **10× throughput, 10× lower latency**

---

### **Lab 6: GPU MIG Configuration**

**Objective:** Partition A100 into MIG instances and serve multiple models.

**Steps:**
1. Enable MIG mode on GPU
2. Create MIG instances (mix of 1g.10gb and 3g.40gb)
3. Configure Device Plugin for MIG
4. Deploy pods requesting specific MIG profiles
5. Verify isolation (memory fault in one instance doesn't affect others)

**Expected Results:**
- 7 isolated instances per GPU
- Each instance serves independent model
- Zero interference between instances
- GPU utilization: 80%+ (vs. 15% with one model per GPU)

---

### **Lab 7: Full Platform Observability Stack**

**Objective:** Deploy complete monitoring for HPC cluster.

**Steps:**
1. Deploy Prometheus + Grafana
2. Deploy DCGM Exporter (GPU metrics)
3. Deploy Node Exporter with HPC-specific collectors
4. Create eBPF-based custom exporter (scheduler latency, NUMA stats)
5. Configure alerting rules (SLO-based)
6. Build Grafana dashboard showing:
   - Per-core CPU usage on isolated cores
   - GPU utilization, memory, temperature
   - NUMA locality percentage
   - Inference latency distribution (µs resolution)

**Expected Results:**
- Single pane of glass for entire HPC cluster health
- Sub-second alerting on performance degradation
- Historical data for capacity planning

---

## Interview Question Bank

### **Memory & CPU:**

1. Explain the cost of a cache miss vs. L1 cache hit.
2. What is false sharing? How do you detect and prevent it?
3. When would you use an arena allocator vs. malloc?
4. How does NUMA affect performance? How do you optimize?
5. What are huge pages? When should you use them?

### **Concurrency:**

6. Compare memory orderings in C++ atomics.
7. Implement a lock-free SPSC queue.
8. What is the ABA problem? How do you solve it?
9. When would you use spinlock vs. mutex?
10. How does work stealing improve thread pool performance?

### **Zero-Copy:**

11. What is zero-copy? Give examples from your projects.
12. How does Apache Arrow enable zero-copy?
13. Compare Protobuf, FlatBuffers, Cap'n Proto.
14. What is RDMA? When would you use it?
15. How does DPDK achieve kernel bypass?

### **Distributed Systems:**

16. Explain the CAP theorem.
17. What is consistent hashing? How does it help?
18. How do circuit breakers work?
19. Compare at-least-once vs. exactly-once semantics.
20. How do you handle distributed transactions?

### **System Design:**

21. Design an LLM serving platform.
22. Design a real-time fraud detection system.
23. Design a distributed cache.
24. Design a message queue.
25. Design a rate limiter.

### **Platform & Infrastructure Engineering (NEW):**

26. Your C++ service uses NUMA-aware memory allocation, but when deployed in Kubernetes, NUMA locality drops to 50%. What's wrong and how do you fix it?

    **Answer:** Kubernetes CPU Manager is in "none" policy (default), so the pod's cpuset spans both NUMA nodes. Fix: Enable `cpuManagerPolicy: static`, use Guaranteed QoS (requests==limits), enable `topologyManagerPolicy: single-numa-node`. Verify with `numastat -p <pid>`.

27. Compare MIG, time-slicing, and MPS for GPU sharing in a multi-tenant cluster. When would you use each?

    **Answer:**
    - **MIG:** Full isolation (memory + compute + cache), A100/H100 only, max 7 instances. Use for production multi-tenant with SLA guarantees.
    - **Time-slicing:** No isolation, any GPU, unlimited virtual GPUs. Use for dev/test, non-critical workloads.
    - **MPS:** Partial isolation (shared memory), Volta+, max 48 processes. Use for cooperative multi-process (same team, trusted code).
    - **Decision:** Production inference = MIG. Dev clusters = time-slicing. Parallel CUDA kernels = MPS.

28. A low-latency inference pod has p99 latency spikes every ~1 second. The application code is clean. What infrastructure issues would you investigate?

    **Answer:** Timer tick interrupts on the CPU.
    1. Check `nohz_full` — if not set, kernel timer fires every 1ms (jitter source)
    2. Check THP compaction — `cat /sys/kernel/mm/transparent_hugepage/enabled` 
    3. Check IRQ affinity — NIC interrupts hitting performance CPUs
    4. Check CPU governor — frequency scaling causes timing variance
    5. Check noisy neighbors — other pods stealing CPU time
    
    **Fix:** `nohz_full=<cpus>`, `THP=never`, pin IRQs to housekeeping CPUs, `governor=performance`, use Guaranteed QoS with CPU Manager.

29. How do you configure Kubernetes to provide exclusive CPU cores to a pod? What are the prerequisites?

    **Answer:**
    1. `cpuManagerPolicy: static` in kubelet config
    2. Pod must be **Guaranteed QoS** (requests == limits for ALL containers)
    3. CPU request must be an **integer** (not fractional like 0.5)
    4. Reserve system CPUs: `reservedSystemCPUs: "0-3"`
    5. Optional: `full-pcpus-only: "true"` to avoid HyperThread sharing
    6. After enabling, delete `cpu_manager_state` file and restart kubelet

30. Your inference cluster uses RDMA for KV-cache transfer. How do you expose RDMA to Kubernetes pods?

    **Answer:**
    - Option 1: RDMA shared device plugin (shared HCA, multiple pods)
    - Option 2: SR-IOV + RDMA (exclusive VF per pod, best isolation)
    - Both require: Multus CNI (secondary network), `IPC_LOCK` capability, RDMA kernel modules
    - Topology Manager should align RDMA NIC and GPU on same NUMA node
    - Use NetworkAttachmentDefinition for secondary RDMA network

31. Explain the networking overhead in Kubernetes and list 3 strategies to reduce it for HPC workloads.

    **Answer:** Default K8s networking adds 50-200µs (veth + bridge + iptables + conntrack + overlay).
    
    **Strategies:**
    1. **Cilium with eBPF:** Replace iptables, skip conntrack for known connections (20-50µs)
    2. **SR-IOV:** Direct NIC VF to pod, bypass entire kernel network stack (5-10µs)
    3. **Host networking:** `hostNetwork: true`, zero overhead but no isolation (1-5µs)
    
    **Bonus:** Same-pod shared memory for co-located services (100ns, zero network)

32. How do you size huge pages for a node running multiple inference pods? What happens if you over/under-allocate?

    **Answer:**
    - **Sizing:** Sum all pods' `hugepages-2Mi` requests + 10% buffer
    - **Per-NUMA:** Allocate proportionally to each NUMA node
    - **Under-allocate:** Pods get stuck in Pending (resource unavailable, not schedulable)
    - **Over-allocate:** Wastes memory (huge pages are reserved, can't be used for regular pages)
    - **Best practice:** Boot-time allocation (avoids fragmentation), monitor with `cat /proc/meminfo | grep Huge`

33. Design a GPU health monitoring and auto-quarantine system for a 100-node inference cluster.

    **Answer:**
    - **Collection:** DCGM Exporter on each node → Prometheus
    - **Key metrics:** Temperature, XID errors, ECC errors, retired pages, PCIe errors
    - **Thresholds:** Temp > 85°C (warning), XID errors > 0 (critical), retired pages > 5 (quarantine)
    - **Automation:** Prometheus alerting → webhook → script that cordons node + labels GPU unhealthy
    - **Recovery:** After manual inspection/reset, uncordon and remove label
    - **Prevention:** Lock GPU clocks, adequate cooling, persistence mode

34. When would you choose bare metal + Slurm over Kubernetes for HPC workloads?

    **Answer:**
    - **Bare metal + Slurm when:**
      - Single-tenant (dedicated team/workload)
      - Ultra-low-latency requirement (< 10µs)
      - RDMA networking is primary communication
      - Training workloads (long-running, static allocation)
      - Simpler operational model (no K8s overhead)
    
    - **Kubernetes when:**
      - Multi-tenant (shared cluster, different teams)
      - Mixed workloads (inference + batch + development)
      - Dynamic scaling (autoscaling inference replicas)
      - Need service mesh, observability, CI/CD integration
      - Teams already have K8s expertise

35. How do you prevent the OOM killer from terminating your inference process?

    **Answer:**
    - **Host level:** `echo -1000 > /proc/<pid>/oom_score_adj` (never OOM-kill)
    - **K8s level:** Use Guaranteed QoS (oom_score_adj = -997, last to be killed)
    - **Preventive:** Set accurate memory limits, monitor memory usage
    - **Huge pages:** Don't count against cgroup memory limit (but `/dev/shm` does!)
    - **GPU memory:** Not managed by Linux OOM (separate CUDA OOM)
    - **Best practice:** `vm.overcommit_memory=0` (fail allocation instead of OOM later)

### **Full-Stack Systems Design (NEW):**

36. Design an inference platform that serves 50K requests/sec with p99 < 10ms. Cover both application architecture AND infrastructure.

    **Answer:**
    - **Application:** vLLM with continuous batching, PagedAttention, prefix caching. Multiple model replicas.
    - **Infrastructure:** 
      - 8× A100 nodes, MIG 3g.40gb (2 instances/GPU = 16 instances)
      - CPU Manager static, Topology Manager single-numa-node
      - Cilium CNI (eBPF, no iptables)
      - Huge pages for KV-cache (pre-allocated at boot)
      - GPU clocks locked, persistence mode, C-states disabled
      - DCGM monitoring, eBPF scheduler latency tracking

37. You're migrating a bare-metal HPC cluster to Kubernetes. What performance regressions do you expect and how do you mitigate?

    **Answer:**
    | Regression | Cause | Mitigation |
    |-----------|-------|------------|
    | +50-200µs network | CNI overhead | SR-IOV or Cilium |
    | CPU jitter | No isolation | CPU Manager static + nohz_full |
    | NUMA misalignment | Random scheduling | Topology Manager |
    | Huge page unavailable | Not configured | Pre-allocate, resource requests |
    | GPU init latency | No persistence | nvidia-persistenced, locked clocks |
    | IPC overhead | Namespace isolation | Same-pod + shared /dev/shm |

38. Design a multi-region GPU inference cluster with automatic failover. How do you handle KV-cache state?

    **Answer:**
    - **Architecture:** Active-active across 2-3 regions, global load balancer (Cloudflare/AWS Global Accelerator)
    - **KV-cache:** Not replicated (too expensive). Session affinity to region. On failover: cold start (accept higher TTFT for first request)
    - **Alternative:** Prefix cache shared across region (store common system prompts). Per-user KV-cache is disposable.
    - **Failover:** Health check → drain → redirect. Target: < 30s failover time
    - **Cost optimization:** Auto-scale between regions based on time-of-day load

39. A CUDA kernel is 10× faster on dev machine but only 2× faster in production. What infrastructure differences could explain this?

    **Answer:**
    - **GPU generation:** Dev has H100 (FP8 Tensor Cores), prod has T4 (no FP8)
    - **GPU clocks:** Dev at boost clock, prod thermally throttled
    - **PCIe bandwidth:** Dev has PCIe 5.0, prod has PCIe 3.0 (memory-bound kernels affected)
    - **NUMA misalignment:** Prod GPU on different NUMA node than CPU (DMA latency)
    - **Power management:** Prod GPU in power-saving mode (clocks not locked)
    - **Contention:** Prod GPU shared via time-slicing (context switch overhead)
    - **Fix:** Lock clocks, verify NUMA affinity, use MIG not time-slicing, match PCIe gen

40. Design the platform for a service needing shared memory between 3 processes, GPU access, RDMA networking, and sub-ms latency.

    **Answer:**
    ```yaml
    # Single pod with 3 containers (shared IPC namespace)
    spec:
      hostNetwork: false  # Use SR-IOV for RDMA
      containers:
      - name: process-a  # Producer
      - name: process-b  # GPU compute  
      - name: process-c  # Network I/O
      
      # Shared /dev/shm for zero-copy IPC
      volumes:
      - name: dshm
        emptyDir: { medium: Memory, sizeLimit: "16Gi" }
      
      # Resources: Guaranteed QoS
      resources:
        requests/limits:
          cpu: "16"              # Exclusive CPUs
          memory: "64Gi"
          hugepages-2Mi: "8Gi"
          nvidia.com/gpu: "1"
          rdma/hca: "1"
      
      # Node: CPU Manager static, Topology Manager single-numa-node
      # All resources (CPU + GPU + RDMA NIC) on same NUMA node
    ```

### **Troubleshooting & Performance (Part III):**

41. Walk me through your approach when a production service has degraded latency. You have 15 minutes to diagnose.

    **Answer:**
    1. **60-second check:** `uptime` (load), `dmesg | tail` (errors), `vmstat 1 3` (CPU/mem), `mpstat -P ALL 1 1` (per-CPU), `pidstat 1 3` (per-process)
    2. **USE method:** For each resource (CPU, memory, disk, network, GPU), check utilization, saturation, errors
    3. **Identify bottleneck resource** (the saturated one)
    4. **Profile:** If CPU → `perf record -F 99 -g + flame graph`. If memory → `free -m` + `numastat`. If network → `ss -tnip` + `tcpdump`
    5. **Root cause:** Read flame graph (wide plateau = hot path), check off-CPU (blocked = lock/IO)
    6. **Quick fix:** Apply tuning recipe (CPU pin, lock fix, buffer increase)
    - **Key insight:** Systematic methodology (USE) beats guessing. Never jump to conclusions before checking ALL resources.

42. Your flame graph shows 30% time in `madvise()` system call. What's happening and how do you fix it?

    **Answer:** Transparent Huge Pages (THP) compaction. The kernel is trying to compact memory into huge pages, causing `madvise(MADV_HUGEPAGE)` stalls.
    - **Confirm:** `perf stat -e page-faults,compaction-stalls`
    - **Fix:** `echo never > /sys/kernel/mm/transparent_hugepage/enabled`
    - **Alternative:** Use explicit huge pages (pre-allocated at boot, no compaction)
    - **Impact:** THP compaction can add 10-50ms stalls (devastating for p99 latency)
    - **K8s:** Set via tuned operator or init container in DaemonSet

43. Describe how you would use eBPF/bpftrace to debug a scheduling latency issue where threads are experiencing 5ms+ wakeup delays on "isolated" CPUs.

    **Answer:**
    ```bash
    # 1. Measure scheduler latency on specific CPUs
    bpftrace -e 'tracepoint:sched:sched_wakeup /args->target_cpu >= 4 && args->target_cpu <= 15/ {
        @start[args->pid] = nsecs;
    }
    tracepoint:sched:sched_switch /args->next_pid != 0 && @start[args->next_pid]/ {
        @delay_us = hist((nsecs - @start[args->next_pid]) / 1000);
        delete(@start[args->next_pid]);
    }'
    
    # 2. Find what's running on "isolated" CPUs
    bpftrace -e 'tracepoint:sched:sched_switch /cpu >= 4 && cpu <= 15/ {
        printf("CPU %d: %s (pid %d) -> %s (pid %d)\n", cpu,
               args->prev_comm, args->prev_pid, args->next_comm, args->next_pid);
    }'
    ```
    - **Common causes:** RCU callbacks, workqueue threads, IRQs not affinity-set, kernel timers
    - **Fix:** `nohz_full=4-15`, `rcu_nocbs=4-15`, IRQ affinity to CPUs 0-3, `isolcpus=nohz,domain,managed_irq,4-15`

44. You're using tcmalloc and notice periodic 20ms latency spikes every 60 seconds. HeapTrack shows stable memory (no leak). What's causing it?

    **Answer:** tcmalloc's background thread releasing memory back to the OS (`MallocExtension::ReleaseToSystem()`). Every 60s, tcmalloc scans its page heap and calls `madvise(MADV_DONTNEED)` on unused spans.
    - **Confirm:** `bpftrace -e 'tracepoint:syscalls:sys_enter_madvise /args->behavior == 4/ { @[ustack] = count(); }'`
    - **Fix options:**
      - `TCMALLOC_RELEASE_RATE=0` (never release, accept higher RSS)
      - Tune release rate: `MallocExtension::instance()->SetMemoryReleaseRate(0.0)`
      - Switch to jemalloc with `dirty_decay_ms:-1`
      - Pre-allocate memory pools for hot path (bypass allocator entirely)

45. Explain the difference between on-CPU and off-CPU flame graphs. When would off-CPU analysis reveal problems that on-CPU cannot?

    **Answer:**
    - **On-CPU:** Shows where threads spend time RUNNING (consuming CPU cycles). Good for: CPU-bound issues, hot algorithms, compute optimization
    - **Off-CPU:** Shows where threads spend time BLOCKED (not running). Good for: lock contention, I/O wait, scheduler delay, dependency wait
    - **Off-CPU reveals problems on-CPU cannot when:**
      - CPU utilization is low but latency is high (threads waiting, not running)
      - Lock contention: threads spinning briefly then blocking (shows as futex_wait in off-CPU)
      - I/O stalls: synchronous disk/network calls (thread deschedules during I/O)
      - Scheduling issues: high-priority process preempting your thread
    - **Example:** Service latency = 20ms, but on-CPU flame graph shows only 3ms of compute → 17ms is off-CPU (find with `offcputime` BCC tool)

---

---

# Part III: Performance Troubleshooting & Optimization

---

## Module 15: Performance Analysis Methodology

### **15.1 The USE Method (Utilization, Saturation, Errors)**

#### **What is the USE Method?**

Created by Brendan Gregg. For every resource, check:
- **U**tilization: How busy is the resource? (% time busy)
- **S**aturation: How much extra work is queued? (queue depth)
- **E**rrors: Are there error events? (failed operations)

```
For each resource (CPU, Memory, Disk, Network, GPU):
┌─────────────────────────────────────────────────────────┐
│  1. Utilization: % of time resource was busy            │
│     → High utilization = potential bottleneck            │
│                                                         │
│  2. Saturation: Degree to which extra work is queued    │
│     → Any saturation = definite bottleneck              │
│                                                         │
│  3. Errors: Number of error events                      │
│     → Any errors = investigate immediately              │
└─────────────────────────────────────────────────────────┘
```

#### **USE Method Applied to Each Resource:**

| Resource | Utilization | Saturation | Errors |
|----------|-------------|------------|--------|
| **CPU** | `mpstat` (%usr+%sys) | run queue length (`vmstat` r column) | `perf stat` (machine check exceptions) |
| **Memory** | `free -m` (used/total) | swap usage, OOM kills | `dmesg` (ECC errors) |
| **Disk** | `iostat` (%util) | `iostat` (avgqu-sz) | `smartctl`, `dmesg` |
| **Network** | `sar -n DEV` (bandwidth) | `ss -s` (drops), `netstat -s` (retransmits) | `ethtool -S` (errors) |
| **GPU** | `nvidia-smi` (GPU %, MEM %) | pending kernel queue | XID errors, ECC errors |
| **PCIe** | `nvidia-smi` (tx/rx throughput) | replay count | correctable/uncorrectable errors |
| **Interconnect** | `numastat` (remote accesses) | QPI/UPI backpressure | link errors |

#### **USE Method Checklist Script:**

```bash
#!/bin/bash
# use-method-check.sh - Quick system health assessment

echo "=== CPU ==="
echo "Utilization:"
mpstat 1 1 | tail -1 | awk '{print "  Total: " 100-$NF "%"}'
echo "Saturation:"
vmstat 1 2 | tail -1 | awk '{print "  Run queue: " $1 " (>CPU count = saturated)"}'
echo "Errors:"
dmesg | grep -c "MCE\|Machine check" | xargs -I{} echo "  MCE errors: {}"

echo ""
echo "=== Memory ==="
echo "Utilization:"
free -m | awk '/Mem:/{printf "  Used: %.1f%% (%dMB/%dMB)\n", $3/$2*100, $3, $2}'
echo "Saturation:"
vmstat 1 2 | tail -1 | awk '{print "  Swap in/out: " $7 "/" $8 " pages/s"}'
echo "  OOM kills: $(dmesg | grep -c 'Out of memory')"
echo "Errors:"
echo "  ECC errors: $(edac-util -s 2>/dev/null || echo 'N/A')"

echo ""
echo "=== Disk ==="
echo "Utilization:"
iostat -x 1 2 | grep -E "^(nvme|sd)" | awk '{print "  " $1 ": " $NF "% util"}'
echo "Saturation:"
iostat -x 1 2 | grep -E "^(nvme|sd)" | awk '{print "  " $1 ": avgqu-sz=" $9}'
echo "Errors:"
echo "  SMART errors: $(smartctl -H /dev/sda 2>/dev/null | grep -c FAILED)"

echo ""
echo "=== Network ==="
echo "Utilization:"
sar -n DEV 1 1 | grep -v "lo\|IFACE\|Average\|^$" | awk '{print "  " $2 ": rx=" $5 " tx=" $6 " kB/s"}'
echo "Saturation:"
ss -s | grep -E "drops|retrans"
netstat -s | grep -i "retransmit\|drop" | head -5
echo "Errors:"
for iface in $(ls /sys/class/net/ | grep -v lo); do
    errors=$(ethtool -S $iface 2>/dev/null | grep -i "error\|drop" | grep -v ": 0" | head -3)
    [[ -n "$errors" ]] && echo "  $iface: $errors"
done

echo ""
echo "=== GPU ==="
if command -v nvidia-smi &>/dev/null; then
    echo "Utilization:"
    nvidia-smi --query-gpu=index,utilization.gpu,utilization.memory --format=csv,noheader | \
        awk -F', ' '{print "  GPU" $1 ": compute=" $2 " memory=" $3}'
    echo "Saturation:"
    nvidia-smi --query-gpu=index,pcie.link.gen.current,pcie.link.width.current --format=csv,noheader | \
        awk -F', ' '{print "  GPU" $1 ": PCIe Gen" $2 " x" $3}'
    echo "Errors:"
    nvidia-smi --query-gpu=index,ecc.errors.corrected.aggregate.total --format=csv,noheader 2>/dev/null | \
        awk -F', ' '{print "  GPU" $1 ": ECC corrected=" $2}'
fi
```

---

### **15.2 The RED Method (Rate, Errors, Duration)**

#### **For Services (Complement to USE for Resources):**

| Metric | Description | Tool |
|--------|-------------|------|
| **R**ate | Requests per second | Prometheus `rate()` |
| **E**rrors | Failed requests per second | HTTP 5xx, gRPC errors |
| **D**uration | Latency distribution (p50/p99) | Histogram buckets |

```
USE Method → Resources (CPU, Memory, Disk, Network, GPU)
RED Method → Services (API endpoints, inference pipelines)

Together they cover the FULL troubleshooting space:
┌─────────────────────────────────────────────────────────┐
│  "My service is slow" → RED (which service? what latency?) │
│                    ↓                                       │
│  "Which resource is bottleneck?" → USE (CPU? Memory? GPU?) │
│                    ↓                                       │
│  "Why is it saturated?" → Profiling (flame graphs, traces)  │
└─────────────────────────────────────────────────────────┘
```

---

### **15.3 Performance Analysis Workflow**

#### **The 60-Second Analysis:**

```bash
# Brendan Gregg's 60-second checklist (run on ANY performance issue)

# 1. Load averages (are we busy?)
uptime
# Output: load average: 12.50, 11.20, 10.80
# If load > CPU count → saturated

# 2. Kernel errors (dmesg)
dmesg | tail -20
# Look for: OOM, MCE, NIC errors, GPU XID, disk errors

# 3. Overall CPU stats
vmstat 1 5
# Key columns: r (run queue), si/so (swap), us/sy (CPU), wa (I/O wait)

# 4. Per-CPU breakdown
mpstat -P ALL 1 5
# Look for: single CPU at 100% (single-threaded bottleneck)
# Look for: high %sys (kernel overhead)

# 5. Process list (who's using resources?)
pidstat 1 5
# Or: ps aux --sort=-%cpu | head -20

# 6. Disk I/O
iostat -xz 1 5
# Key: %util > 80% = bottleneck, await > 10ms = slow

# 7. Memory
free -m
# Key: available (not just free), swap used

# 8. Network
sar -n DEV 1 5
# Key: TX/RX throughput, errors

# 9. TCP stats
sar -n TCP,ETCP 1 5
# Key: retransmits, passive/active connections

# 10. System overview
top -b -n 1 | head -30
# Or: htop (interactive)
```

#### **Performance Investigation Decision Tree:**

```
Symptom: High latency
├── Is CPU saturated? (vmstat r > CPU count, mpstat %usr+%sys > 90%)
│   ├── Yes → Profile with perf/flamegraph
│   │   ├── Application code hot → Optimize algorithm
│   │   ├── Kernel hot (%sys) → System call overhead, lock contention
│   │   └── Single CPU hot → Single-threaded bottleneck, pin check
│   └── No → Not CPU-bound
├── Is Memory saturated? (swap activity, page faults)
│   ├── Yes → Check allocations (heaptrack), check RSS growth
│   │   ├── Leak → Valgrind/ASan
│   │   └── Working set too large → Reduce, add RAM, use huge pages
│   └── No → Not memory-bound
├── Is Disk saturated? (iostat %util > 80%, avgqu-sz > 4)
│   ├── Yes → Check I/O pattern
│   │   ├── Random I/O → Move to NVMe, use mmap
│   │   └── Throughput → Check filesystem, use direct I/O
│   └── No → Not disk-bound
├── Is Network saturated? (bandwidth near line rate, retransmits)
│   ├── Yes → Check MTU, TCP tuning, consider RDMA
│   └── No → Not network-bound
├── Is GPU saturated? (nvidia-smi util > 95%)
│   ├── Yes → Optimize kernel, increase batch size, quantize
│   └── No → GPU idle waiting for data (CPU/memory bound)
└── None saturated? → Lock contention, scheduling issue, dependency wait
    → Use: perf lock, futex tracing, off-CPU flame graphs
```

---

### **15.4 Latency Analysis Framework**

#### **Breaking Down End-to-End Latency:**

```
Request lifecycle (inference service):
┌─────────────────────────────────────────────────────────────────┐
│ Network → Queue → Preprocessing → GPU Compute → Postprocess → Response │
│  5µs      2ms       1ms              5ms           0.5ms        5µs     │
└─────────────────────────────────────────────────────────────────┘

Where to measure:
1. Wire latency (tcpdump timestamps)
2. Kernel → userspace (eBPF socket tracing)
3. Request queue time (application metrics)
4. Preprocessing (CPU profiling)
5. GPU kernel time (Nsight Systems)
6. Postprocessing (CPU profiling)
7. Response serialization + send (application + tcpdump)
```

#### **On-CPU vs. Off-CPU Analysis:**

```
Thread lifecycle:
┌─────────────┐   ┌─────────────┐   ┌─────────────┐
│  On-CPU     │   │  Off-CPU    │   │  On-CPU     │
│  (running)  │   │  (blocked)  │   │  (running)  │
│  5ms        │   │  15ms       │   │  3ms        │
└─────────────┘   └─────────────┘   └─────────────┘

Total latency: 23ms
On-CPU: 8ms (profile with perf flame graphs)
Off-CPU: 15ms (profile with off-CPU flame graphs)
  → Lock wait? I/O wait? Scheduling delay? Network wait?
```

```bash
# On-CPU flame graph (where does the CPU spend time?)
perf record -F 99 -g -p $PID -- sleep 30
perf script | stackcollapse-perf.pl | flamegraph.pl > oncpu.svg

# Off-CPU flame graph (where is the thread BLOCKED?)
# Using bpftrace:
bpftrace -e '
  tracepoint:sched:sched_switch /args->prev_pid == '$PID'/ {
    @start[tid] = nsecs;
  }
  tracepoint:sched:sched_switch /args->next_pid == '$PID'/ {
    if (@start[tid]) {
      @offcpu[kstack] = sum(nsecs - @start[tid]);
      delete(@start[tid]);
    }
  }
' > offcpu.bt

# Or using perf (sched tracing):
perf sched record -p $PID -- sleep 30
perf sched latency --sort max
```

---

### **15.5 Practice Questions: Methodology**

1. **A service has p99 latency of 50ms but p50 is only 5ms. What does this tell you? How would you investigate?**

   **Answer:** Bimodal or tail latency issue. The median case is fine but outliers are 10× worse. Investigate:
   - **Queue buildup:** Periodic saturation causing queue delay (check batch boundaries)
   - **GC/compaction:** If Java/Go, GC pauses. If C++, THP compaction.
   - **Lock contention:** Occasional contention causes long waits
   - **Scheduling:** Thread being preempted (check with `perf sched latency`)
   - **Approach:** Correlate p99 spikes with system events (IRQs, GC, CPU throttle)

2. **You run the USE method and find CPU utilization at 40%, no saturation, no errors — but the service is still slow. What's happening?**

   **Answer:** Thread is spending time OFF-CPU (blocked, not running):
   - Lock contention (waiting for mutex)
   - I/O wait (synchronous disk/network call)
   - Scheduling delay (low priority, preempted)
   - Dependency wait (waiting for downstream service)
   - **Investigate with:** Off-CPU flame graphs, `perf sched latency`, lock profiling

---

## Module 16: Linux Performance Tools Mastery

### **16.1 CPU Analysis Tools**

#### **top / htop — Process Overview:**

```bash
# top: Quick overview
top -b -n 1 -o %CPU | head -20

# Key fields:
# %CPU - CPU usage (can exceed 100% for multi-threaded)
# %MEM - Memory usage
# VIRT - Virtual memory (includes mmap'd files)
# RES  - Resident memory (actually in RAM)
# SHR  - Shared memory
# S    - State (R=running, S=sleeping, D=disk wait, Z=zombie)

# htop: Better interactive view
# F5 = tree mode (see parent-child)
# F6 = sort by column
# F2 = setup (add NUMA node, CPU frequency columns)

# Tip: Add these columns in htop setup:
# PROCESSOR (which CPU), STARTTIME, IO_RATE, NLWP (thread count)
```

#### **uptime — Load Average:**

```bash
uptime
# Output: 14:30:01 up 45 days, load average: 8.50, 7.20, 6.80
#                                              1min  5min  15min

# Interpretation:
# Load = number of runnable + uninterruptible processes
# If load > CPU count → saturation
# Trend: increasing (1min > 15min) = getting worse
# Trend: decreasing (1min < 15min) = recovering

# On a 16-core machine:
# load 16 = 100% utilization, no saturation
# load 32 = 100% utilization + 16 queued = saturated
# load 4  = 25% utilization (plenty of headroom)
```

#### **mpstat — Per-CPU Statistics:**

```bash
# All CPUs, 1-second interval, 5 samples
mpstat -P ALL 1 5

# Key columns:
# %usr   - User-space CPU (your application)
# %sys   - Kernel CPU (system calls, interrupts)
# %iowait - Waiting for I/O (disk, network)
# %irq   - Hardware interrupts
# %soft  - Software interrupts (network packet processing)
# %idle  - Idle (unused)

# What to look for:
# 1. Single CPU at 100% usr → single-threaded bottleneck
# 2. High %sys → excessive system calls, lock contention in kernel
# 3. High %iowait → disk/network I/O bottleneck
# 4. High %irq on specific CPUs → interrupt affinity issue
# 5. %soft > 20% → network packet storm (softirq processing)

# Example: Detect CPU isolation violation
mpstat -P 4-15 1 5  # Check isolated CPUs
# If %usr > 0 on "isolated" CPUs → something is scheduled there
```

#### **pidstat — Per-Process Stats:**

```bash
# CPU usage per process, 1-second interval
pidstat 1 5

# Per-thread breakdown
pidstat -t -p $PID 1 5

# Context switches per process
pidstat -w -p $PID 1 5
# cswch/s = voluntary (I/O wait, lock)
# nvcswch/s = involuntary (preempted by scheduler)
# HIGH nvcswch = CPU contention, needs more CPU or isolation

# I/O per process
pidstat -d -p $PID 1 5
# kB_rd/s, kB_wr/s = read/write throughput

# Memory faults per process
pidstat -r -p $PID 1 5
# minflt/s = minor faults (page in from cache)
# majflt/s = major faults (page from disk - BAD for latency)
```

#### **ps — Process Snapshot:**

```bash
# Top CPU consumers
ps aux --sort=-%cpu | head -20

# Top memory consumers
ps aux --sort=-%mem | head -20

# Check thread count and CPU affinity
ps -eLf -p $PID | wc -l  # Thread count
taskset -p $PID           # CPU affinity mask

# Check NUMA node
ps -o pid,comm,psr -p $PID  # PSR = processor (CPU number)

# Process tree (see parent-child relationships)
ps axjf | grep inference

# Long format with scheduling info
ps -eo pid,ppid,cls,pri,ni,rtprio,psr,comm --sort=-pcpu | head -20
# cls = scheduling class (TS=normal, FF=FIFO, RR=round-robin)
# pri = priority, ni = nice, rtprio = real-time priority
```

---

### **16.2 Memory Analysis Tools**

#### **free — Memory Overview:**

```bash
free -m
#               total    used    free    shared  buff/cache  available
# Mem:         128000   64000    8000     2000      56000      62000
# Swap:          8000       0    8000

# Key insight: "available" NOT "free" is what matters
# available = free + reclaimable cache
# If available < 10% of total → memory pressure
# If swap used > 0 → investigate (should be 0 for HPC)

# Watch memory over time
free -m -s 5  # Every 5 seconds
```

#### **vmstat — Virtual Memory Stats:**

```bash
vmstat 1 10
# procs   memory           swap    io     system     cpu
# r  b    swpd  free  buff cache   si so  bi  bo   in  cs  us sy id wa
# 3  0    0   8000000 2000 56000   0  0   10  50   5000 8000 40 5  55 0

# Key columns:
# r  = Run queue (runnable threads). If r > CPU count → saturated
# b  = Blocked threads (I/O wait). If b > 0 → I/O bottleneck
# si/so = Swap in/out (pages/s). Should be 0 for HPC
# in = Interrupts/sec. High = IRQ storm
# cs = Context switches/sec. High = lock contention or too many threads
# us = User CPU %, sy = System CPU %, wa = I/O wait %

# What to look for:
# r > 16 (on 16-core): CPU saturated
# si/so > 0: Swapping (CRITICAL for latency)
# cs > 100000: Excessive context switches
# wa > 20%: I/O bottleneck
```

#### **numastat — NUMA Memory Statistics:**

```bash
# Per-node memory stats
numastat
# Output:
#                   node0      node1
# numa_hit        45678901   43210987
# numa_miss            123        456  # ← These should be ZERO
# numa_foreign         456        123
# interleave_hit       100        100
# local_node      45678901   43210987
# other_node           123        456  # ← Cross-node access (BAD)

# Per-process NUMA stats
numastat -p $PID
# Output:
#                   node0      node1
# Total            8192.00      0.00   # ← All memory on node 0 (GOOD)

# If memory is split across nodes → NUMA misalignment
# Fix: numactl --membind=0, or Topology Manager in K8s
```

#### **numactl — NUMA Control:**

```bash
# Show NUMA hardware topology
numactl --hardware

# Run process on specific NUMA node
numactl --cpunodebind=0 --membind=0 ./my-service

# Interleave memory across nodes (good for shared data)
numactl --interleave=all ./shared-data-service

# Check current NUMA policy for a process
cat /proc/$PID/numa_maps | head -20
# Look for: N0=1234 N1=5 (most pages on node 0, good)
# Bad: N0=600 N1=600 (split across nodes)
```

#### **slabtop — Kernel Slab Cache:**

```bash
# View kernel memory allocations
slabtop -s c  # Sort by cache size

# Key caches to monitor:
# dentry          - Directory entry cache (high = many files accessed)
# inode_cache     - Inode cache
# kmalloc-*       - General kernel allocations
# task_struct     - Process/thread descriptors (high = many threads)
# buffer_head     - Block I/O buffers

# If kernel memory growing → check for kernel memory leaks
# If dentry/inode very large → file descriptor leak or too many open files
```

#### **pcstat — Page Cache Statistics:**

```bash
# Check if a file is in page cache
pcstat /opt/models/llama-7b.bin
# Output:
# +----------------------------+--------+--------+---------+--------+
# | Name                       | Size   | Pages  | Cached  | Percent|
# +----------------------------+--------+--------+---------+--------+
# | /opt/models/llama-7b.bin   | 14.0G  | 3670016| 3670016 | 100.0% |
# +----------------------------+--------+--------+---------+--------+

# Useful for:
# - Verifying model files are in page cache (warm startup)
# - Checking if frequently accessed files are cached
# - Identifying cache thrashing (file too large for available cache)
```

---

### **16.3 Disk I/O Analysis Tools**

#### **iostat — Disk Statistics:**

```bash
# Extended stats, 1-second interval
iostat -xz 1 5

# Key columns:
# r/s, w/s       - Reads/writes per second (IOPS)
# rkB/s, wkB/s   - Read/write throughput (KB/s)
# await           - Average I/O wait time (ms). > 10ms = slow
# r_await,w_await - Separate read/write await
# avgqu-sz        - Average queue depth. > 1 = queuing
# %util           - % time device was busy. > 80% = saturated

# NVMe should show:
# await < 0.1ms (100µs)
# %util < 50% (NVMe handles parallelism well)

# Example output for healthy NVMe:
# nvme0n1  0.00  200.00  0.00  819200.00  0.00  0.05  0.01  5.00
#                                                      ^^^^
#                                                      Very low await = healthy
```

#### **iotop — Per-Process I/O:**

```bash
# Real-time I/O per process
iotop -o  # Only show processes doing I/O

# Key: Identify which process is causing disk pressure
# If inference service shows high I/O → model reloading? Logging? Swap?

# Batch mode for scripting
iotop -b -n 5 -d 1 -p $PID
```

---

### **16.4 Network Analysis Tools**

#### **ss — Socket Statistics:**

```bash
# All TCP connections with stats
ss -tnp

# Connection states summary
ss -s
# TCP: 1234 (estab 800, closed 50, orphaned 5, timewait 200)

# Detailed socket info (buffer sizes, congestion window)
ss -tnip dst :8080
# Output includes:
# cubic wscale:7,7 rto:204 rtt:0.5/0.2 ato:40 mss:1448
# cwnd:10 bytes_sent:1048576 bytes_received:2097152
# send 231.5Mbps retrans:0/0

# Find connections with high retransmits
ss -tnip | grep -i retrans

# Socket buffer usage
ss -tm
# skmem:(r<recv_buf>,rb<recv_buf_max>,t<send_buf>,tb<send_buf_max>)
# If r is close to rb → receive buffer full → increase rmem_max
```

#### **netstat — Network Statistics (Legacy but useful):**

```bash
# TCP retransmission stats
netstat -s | grep -i retransmit
# segments retransmitted: 1234
# If high → network congestion, packet loss, or timeout issues

# Connection states
netstat -ant | awk '{print $6}' | sort | uniq -c | sort -rn
# If many TIME_WAIT → connection churn (use connection pooling)
# If many CLOSE_WAIT → application not closing connections properly

# Interface errors
netstat -i
# RX-ERR, TX-ERR, RX-DRP, TX-DRP should all be 0
```

#### **tcpdump — Packet Capture:**

```bash
# Capture inference traffic
tcpdump -i eth0 -nn port 8080 -w capture.pcap

# Quick latency check (request → response timing)
tcpdump -i eth0 -nn port 8080 -ttt
# -ttt = delta between packets (shows request-response gap)

# Capture only packet headers (minimal overhead)
tcpdump -i eth0 -nn -s 96 port 8080

# Filter retransmissions (troubleshoot packet loss)
tcpdump -i eth0 -nn 'tcp[tcpflags] & (tcp-syn|tcp-fin) != 0'

# BPF filter for specific flow
tcpdump -i eth0 'src host 10.0.1.5 and dst port 8080 and tcp'

# Ring buffer capture (for intermittent issues)
tcpdump -i eth0 -nn -W 10 -C 100 -w /tmp/capture port 8080
# -W 10: Keep last 10 files, -C 100: 100MB per file
# Total: 1GB rolling window capture
```

---

### **16.5 Practice Questions: Linux Tools**

1. **`vmstat` shows r=32 on a 16-core system, but CPU utilization is only 60%. Explain.**

   **Answer:** 32 runnable threads but only 16 CPUs → threads are queuing. But utilization per-CPU averages 60% because some CPUs are idle while others are at 100%. Likely cause: uneven load distribution (some CPUs saturated, others idle). Fix: check CPU affinity, rebalance workload, or use work-stealing thread pool.

2. **`numastat -p $PID` shows memory split 50/50 across nodes. The service uses `numactl --membind=0`. What happened?**

   **Answer:** Possible causes:
   - Process forked after `numactl` and child inherited but used `malloc` with first-touch on different node
   - Memory-mapped files with default policy (interleave)
   - Shared memory region created with wrong NUMA policy
   - Kubernetes Topology Manager not properly configured
   - **Debug:** Check `/proc/$PID/numa_maps` for per-mapping NUMA allocation

3. **`ss -tnip` shows `retrans:45/1200` for your inference client connection. What does this mean and how do you fix it?**

   **Answer:** 45 retransmissions out of 1200 segments = 3.75% packet loss. Very bad for latency (each retransmit adds RTT + timeout).
   - Check `ethtool -S` for interface errors (hardware issue?)
   - Check switch buffer drops (`show interface counters`)
   - Check TCP buffer sizing (`ss -tm` — buffer full = drops)
   - Fix: Increase NIC ring buffers, check for MTU mismatch, consider jumbo frames, check for network congestion

---

## Module 17: Advanced Profiling & Tracing

### **17.1 perf — Linux Performance Counters**

#### **CPU Profiling:**

```bash
# Sample CPU stacks at 99 Hz for 30 seconds
perf record -F 99 -g -p $PID -- sleep 30

# Report (interactive TUI)
perf report --stdio --sort=dso,symbol

# Key options:
# -F 99     : 99 Hz sampling (avoids aliasing with timer at 100 Hz)
# -g        : Capture call graphs (stack traces)
# -p $PID   : Profile specific process
# --call-graph dwarf : Better stacks for optimized code (vs. fp)
# -a        : System-wide (all CPUs)

# Annotate hot function (see assembly)
perf annotate -s my_hot_function
```

#### **Hardware Performance Counters:**

```bash
# Cache performance
perf stat -e cache-references,cache-misses,L1-dcache-load-misses,\
LLC-load-misses,LLC-store-misses -p $PID -- sleep 10
# Target: cache-miss-rate < 5%
# If L1 misses high → data not cache-friendly (AoS → SoA conversion)
# If LLC misses high → working set > L3 cache (reduce or use prefetch)

# Branch prediction
perf stat -e branches,branch-misses -p $PID -- sleep 10
# Target: branch-miss-rate < 2%
# If high → unpredictable branches (sort data, use branchless code)

# Instructions per cycle (IPC)
perf stat -e instructions,cycles -p $PID -- sleep 10
# IPC > 2: CPU-bound, well-optimized
# IPC < 1: Memory-bound or stalling (cache misses, branch misses)
# IPC = 0.3-0.5: Severely memory-bound (fix data layout)

# TLB performance (huge pages effectiveness)
perf stat -e dTLB-load-misses,dTLB-store-misses,\
iTLB-load-misses -p $PID -- sleep 10
# If high → enable huge pages

# NUMA analysis
perf stat -e node-loads,node-load-misses,\
node-stores,node-store-misses -p $PID -- sleep 10
# node-load-misses = remote NUMA access → fix NUMA binding

# Full memory hierarchy analysis
perf stat -e L1-dcache-loads,L1-dcache-load-misses,\
l2_rqsts.demand_data_rd_miss,LLC-loads,LLC-load-misses \
-p $PID -- sleep 10
```

#### **perf Tracepoints (Kernel Events):**

```bash
# Context switches (why is my thread being preempted?)
perf record -e sched:sched_switch -p $PID -- sleep 10
perf script | head -50
# Shows: who preempted whom, on which CPU

# Page faults
perf record -e page-faults -g -p $PID -- sleep 10
perf report
# Shows: which code paths cause page faults (mmap? heap growth?)

# System calls (overhead)
perf trace -p $PID -s -- sleep 10
# Shows: syscall count and time spent in each
# If futex() dominates → lock contention
# If write() dominates → excessive logging
# If epoll_wait() dominates → I/O bound (normal for servers)

# Lock contention
perf lock record -p $PID -- sleep 10
perf lock report
# Shows: which locks are contended, wait times, hold times
```

---

### **17.2 Flame Graphs**

#### **On-CPU Flame Graphs:**

```bash
# Step 1: Record
perf record -F 99 -g -p $PID -- sleep 30

# Step 2: Generate flame graph
perf script | \
    stackcollapse-perf.pl | \
    flamegraph.pl --title "On-CPU: inference-engine" \
                  --subtitle "30s profile" \
                  --width 1200 > oncpu_flame.svg

# Reading a flame graph:
# - X-axis: Stack population (NOT time sequence)
# - Y-axis: Stack depth (bottom = entry point, top = leaf)
# - Width: Proportion of CPU time
# - Color: Random (or use --color=java for language-specific)
#
# Look for:
# - Wide plateaus at top = hot functions (optimize these)
# - Wide base = common entry point (expected)
# - Unexpected width in kernel stacks = system overhead
```

#### **Off-CPU Flame Graphs (Where threads are BLOCKED):**

```bash
# Method 1: Using bcc/BPF tools
/usr/share/bcc/tools/offcputime -p $PID -f 30 > offcpu.stacks
flamegraph.pl --color=io --title "Off-CPU" < offcpu.stacks > offcpu_flame.svg

# Method 2: Using bpftrace
bpftrace -e '
tracepoint:sched:sched_switch /args->prev_pid == '$PID' && args->prev_state != 0/ {
    @start[tid] = nsecs;
}
tracepoint:sched:sched_switch /args->next_pid == '$PID' && @start[tid]/ {
    @offcpu[kstack, ustack] = sum(nsecs - @start[tid]);
    delete(@start[tid]);
}
END { print(@offcpu); }
' -c "sleep 30" > offcpu.bt

# Reading off-CPU flame graph:
# - Width = time spent BLOCKED (not CPU time)
# - Look for: futex_wait (lock), io_schedule (disk), sk_wait_data (network)
# - Wide stack in futex_wait → lock contention → reduce critical section
# - Wide stack in io_schedule → synchronous I/O → make async
```

#### **Differential Flame Graphs (Compare before/after):**

```bash
# Profile before optimization
perf record -F 99 -g -p $PID -- sleep 30
perf script > before.perf

# Profile after optimization
perf record -F 99 -g -p $PID -- sleep 30
perf script > after.perf

# Generate differential flame graph
difffolded.pl <(stackcollapse-perf.pl before.perf) \
              <(stackcollapse-perf.pl after.perf) | \
    flamegraph.pl --negate --title "Differential: Before vs After" > diff.svg

# Colors:
# Red = hotter in "after" (regression)
# Blue = cooler in "after" (improvement)
```

#### **GPU Flame Graphs (Nsight Systems):**

```bash
# Profile GPU application
nsys profile --stats=true -o gpu_profile ./inference-engine

# Export for flame graph
nsys export --type=sqlite gpu_profile.nsys-rep

# Or use Nsight Systems GUI:
# Timeline shows: CPU, CUDA API, GPU kernels, memory transfers
# Identifies: gaps between kernels, memory transfer bottlenecks
```

---

### **17.3 eBPF — Programmable Kernel Tracing**

#### **bpftrace One-Liners for HPC:**

```bash
# --- CPU Analysis ---

# Scheduler latency histogram (µs) — how long threads wait to run
bpftrace -e 'tracepoint:sched:sched_wakeup { @start[args->pid] = nsecs; }
tracepoint:sched:sched_switch /args->prev_state == 0 && @start[args->next_pid]/ {
    @usecs = hist((nsecs - @start[args->next_pid]) / 1000);
    delete(@start[args->next_pid]);
}'
# Target: < 10µs for isolated CPUs

# CPU migration tracking (threads moving between CPUs)
bpftrace -e 'tracepoint:sched:sched_migrate_task {
    printf("pid=%d comm=%s from_cpu=%d to_cpu=%d\n",
           args->pid, args->comm, args->orig_cpu, args->dest_cpu);
}'
# Should see ZERO migrations for pinned threads

# Run queue length per CPU
bpftrace -e 'profile:hz:99 { @runqlen[cpu] = lhist(runqlen, 0, 32, 1); }'

# --- Memory Analysis ---

# Page fault latency
bpftrace -e 'tracepoint:exceptions:page_fault_user {
    @start[tid] = nsecs;
}
tracepoint:exceptions:page_fault_user /retval == 0 && @start[tid]/ {
    @fault_us = hist((nsecs - @start[tid]) / 1000);
    delete(@start[tid]);
}'

# Huge page allocations
bpftrace -e 'tracepoint:huge_memory:mm_khugepaged_scan_pmd {
    @[comm] = count();
}'

# malloc size distribution
bpftrace -e 'uprobe:/lib/x86_64-linux-gnu/libc.so.6:malloc {
    @size = hist(arg0);
}'

# --- Lock Analysis ---

# Futex contention (mutex wait time)
bpftrace -e 'tracepoint:syscalls:sys_enter_futex /args->op == 0/ {
    @start[tid] = nsecs;
}
tracepoint:syscalls:sys_exit_futex /args->op == 0 && @start[tid]/ {
    @futex_us = hist((nsecs - @start[tid]) / 1000);
    delete(@start[tid]);
}'

# pthread_mutex_lock latency
bpftrace -e 'uprobe:/lib/x86_64-linux-gnu/libpthread.so.0:pthread_mutex_lock {
    @start[tid] = nsecs;
}
uretprobe:/lib/x86_64-linux-gnu/libpthread.so.0:pthread_mutex_lock /@start[tid]/ {
    @lock_ns = hist(nsecs - @start[tid]);
    delete(@start[tid]);
}'

# --- I/O Analysis ---

# Block I/O latency per device
bpftrace -e 'tracepoint:block:block_rq_issue { @start[args->dev, args->sector] = nsecs; }
tracepoint:block:block_rq_complete /@start[args->dev, args->sector]/ {
    @us[args->dev] = hist((nsecs - @start[args->dev, args->sector]) / 1000);
    delete(@start[args->dev, args->sector]);
}'

# --- Network Analysis ---

# TCP retransmit tracing
bpftrace -e 'tracepoint:tcp:tcp_retransmit_skb {
    printf("retransmit: %s:%d -> %s:%d state=%d\n",
           ntop(args->saddr), args->sport, ntop(args->daddr), args->dport, args->state);
}'

# Socket read/write latency
bpftrace -e 'tracepoint:syscalls:sys_enter_read /args->fd > 2/ {
    @start[tid] = nsecs;
}
tracepoint:syscalls:sys_exit_read /@start[tid]/ {
    @read_us = hist((nsecs - @start[tid]) / 1000);
    delete(@start[tid]);
}'
```

#### **BCC Tools Collection:**

```bash
# Pre-built BPF tools (install: apt install bcc-tools)

# CPU
/usr/share/bcc/tools/runqlat          # Scheduler run queue latency
/usr/share/bcc/tools/runqlen          # Run queue length
/usr/share/bcc/tools/cpudist          # On-CPU time distribution
/usr/share/bcc/tools/cpuunclaimed     # Idle CPUs with runnable threads
/usr/share/bcc/tools/llcstat          # LLC hit/miss rate
/usr/share/bcc/tools/profile          # CPU profiling (flame graph source)

# Memory
/usr/share/bcc/tools/memleak          # Memory leak detection
/usr/share/bcc/tools/cachestat        # Page cache hit/miss
/usr/share/bcc/tools/cachetop         # Per-process page cache
/usr/share/bcc/tools/oomkill          # OOM kill events

# Disk
/usr/share/bcc/tools/biolatency       # Block I/O latency histogram
/usr/share/bcc/tools/biosnoop         # Per-event block I/O tracing
/usr/share/bcc/tools/ext4slower       # Slow ext4 operations

# Network
/usr/share/bcc/tools/tcplife          # TCP connection lifespan
/usr/share/bcc/tools/tcpretrans       # TCP retransmissions
/usr/share/bcc/tools/tcpconnlat       # TCP connection latency
/usr/share/bcc/tools/tcpstates        # TCP state transitions

# General
/usr/share/bcc/tools/funclatency      # Function latency (kernel or user)
/usr/share/bcc/tools/funccount        # Function call count
/usr/share/bcc/tools/trace            # General event tracing
/usr/share/bcc/tools/argdist          # Function argument distribution
```

---

### **17.4 Intel VTune Profiler**

#### **When to Use VTune vs. perf:**

| Aspect | perf | VTune |
|--------|------|-------|
| **Cost** | Free | Free (community) |
| **Platforms** | Linux only | Linux, Windows, macOS |
| **Ease** | CLI, learning curve | GUI, guided analysis |
| **Depth** | Raw counters | Interpreted results, suggestions |
| **GPU** | No | Limited (OpenCL) |
| **Threading** | Basic | Excellent (threading analysis) |
| **Memory** | Basic counters | Full memory access analysis |
| **Best for** | Quick profiling, scripting | Deep analysis, reporting |

#### **VTune Analysis Types:**

```bash
# 1. Hotspots Analysis (most common)
vtune -collect hotspots -result-dir r001-hot -- ./inference-engine
# Identifies: CPU-heavy functions, call stacks, source-line attribution
# When to use: "What's consuming CPU time?"

# 2. Memory Access Analysis
vtune -collect memory-access -result-dir r002-mem -- ./inference-engine
# Identifies: Cache misses by level, NUMA remote access, bandwidth utilization
# When to use: "Why is my IPC low? Memory layout issues?"

# 3. Threading Analysis
vtune -collect threading -result-dir r003-thread -- ./inference-engine
# Identifies: Lock contention, thread wait time, load imbalance
# When to use: "Why doesn't my multi-threaded code scale?"

# 4. Microarchitecture Exploration
vtune -collect uarch-exploration -result-dir r004-uarch -- ./inference-engine
# Identifies: Pipeline slots (frontend bound, backend bound, retiring, bad speculation)
# When to use: "What's the CPU microarchitecture bottleneck?"

# 5. Platform Profiler (system-wide)
vtune -collect platform-profiler -result-dir r005-plat -- ./inference-engine
# Identifies: PCIe bandwidth, memory bandwidth, cross-socket traffic
# When to use: "Is the system interconnect a bottleneck?"

# Open results in GUI
vtune-gui r001-hot/
```

#### **VTune Pipeline Slot Analysis (Top-Down Method):**

```
Total Pipeline Slots
├── Retiring (useful work)          Target: > 50%
│   ├── Regular                     Good: compute-heavy code
│   └── Microcode assists           Bad: denormals, assists
├── Frontend Bound                  Target: < 10%
│   ├── Fetch Latency              I-cache miss, ITLB miss
│   └── Fetch Bandwidth            Decode bottleneck
├── Backend Bound                   Common bottleneck
│   ├── Memory Bound               Cache miss, bandwidth
│   │   ├── L1 Bound              Data too large for L1
│   │   ├── L2 Bound              Working set > L1 but < L2
│   │   ├── L3 Bound              Working set > L2 but < L3
│   │   ├── DRAM Bound            Working set > L3
│   │   └── Store Bound           Store buffer full
│   └── Core Bound                 Compute port saturation
│       ├── Divider                Division operations
│       ├── Port Utilization       ALU/vector ports saturated
│       └── Serialization          Memory ordering fences
└── Bad Speculation                 Target: < 5%
    ├── Branch Mispredict          Sort data, branchless
    └── Machine Clears             Memory ordering violations

Example interpretation:
- 60% Backend Memory Bound, 30% L3 Bound
  → Working set doesn't fit in L2, spilling to L3
  → Fix: Reduce data size, improve locality, use prefetch
```

---

### **17.5 Intel Advisor**

#### **Vectorization Analysis:**

```bash
# Analyze vectorization opportunities
advixe-cl -collect survey -project-dir r001-advisor -- ./inference-engine

# Check which loops are vectorized
advixe-cl -collect tripcounts -flop -project-dir r001-advisor -- ./inference-engine

# Report
advixe-cl -report survey -project-dir r001-advisor

# Key insights:
# - Which loops are NOT vectorized (and why)
# - Vector efficiency (are all SIMD lanes used?)
# - Roofline analysis (compute vs. memory bound per loop)
```

#### **Roofline Model:**

```
Performance (GFLOP/s)
│
│    ╱────────────── Compute Ceiling (peak FLOP/s)
│   ╱
│  ╱    ● Your kernel (below ceiling = room to optimize)
│ ╱
│╱___________________________ Memory Ceiling (bandwidth × arithmetic intensity)
│
└──────────────────────────── Arithmetic Intensity (FLOP/byte)

Interpretation:
- Kernel on memory ceiling → memory-bound (optimize data access)
- Kernel below memory ceiling → neither bound (likely latency-bound)
- Kernel on compute ceiling → compute-bound (use better algorithms)
- Gap between kernel and ceiling = optimization headroom
```

---

### **17.6 Intel Inspector**

#### **Thread Error Detection:**

```bash
# Detect data races and deadlocks
inspxe-cl -collect ti3 -result-dir r001-inspect -- ./inference-engine

# Analysis types:
# ti1: Quick threading analysis (detect races)
# ti2: Medium (races + deadlocks)
# ti3: Deep (races + deadlocks + API errors)

# Report
inspxe-cl -report problems -result-dir r001-inspect

# Key findings:
# - Data race: Two threads access same memory without synchronization
# - Deadlock: Two threads each hold a lock the other needs
# - Lock hierarchy violation: Locks acquired in inconsistent order
```

---

### **17.7 Tracy Profiler**

#### **What is Tracy?**

A real-time frame profiler for C++ applications. Extremely low overhead (< 1% CPU), designed for game engines and real-time systems.

```cpp
// Integration in C++ code
#include <tracy/Tracy.hpp>

void process_batch(const Batch& batch) {
    ZoneScoped;  // Automatic zone profiling
    
    {
        ZoneScopedN("preprocessing");
        preprocess(batch);
    }
    
    {
        ZoneScopedN("gpu_inference");
        TracyGpuZone("inference_kernel");
        run_inference(batch);
    }
    
    {
        ZoneScopedN("postprocessing");
        postprocess(batch);
    }
    
    // Track memory allocations
    TracyAlloc(ptr, size);
    TracyFree(ptr);
    
    // Custom plots (metrics over time)
    TracyPlot("batch_size", batch.size());
    TracyPlot("queue_depth", queue.size());
    
    // Frame marks (for per-iteration analysis)
    FrameMark;
}
```

#### **Tracy Features:**

| Feature | Description | Use Case |
|---------|-------------|----------|
| Zone profiling | Function/block timing | Find hot code paths |
| Lock tracking | Mutex contention visualization | Debug lock waits |
| Memory profiling | Allocation tracking per frame | Find allocation spikes |
| GPU profiling | CUDA/Vulkan timeline | GPU kernel timing |
| Frame analysis | Per-iteration breakdown | Latency consistency |
| Network streaming | Real-time remote profiling | Production profiling |
| Call stacks | Stack sampling | Combined with zones |

**When to use Tracy vs. perf:**
- **Tracy:** Instrumented, real-time, per-frame analysis, GUI visualization
- **perf:** Sampling, system-wide, no code changes, statistical

---

### **17.8 Nsight Systems & Nsight Compute (GPU Profiling)**

#### **Nsight Systems (Timeline Profiler):**

```bash
# Full system trace (CPU + GPU)
nsys profile --trace=cuda,nvtx,osrt,cudnn \
    --output=timeline \
    --force-overwrite true \
    ./inference-engine --batch-size=32

# Key analysis points:
# 1. GPU idle gaps (between kernels) → kernel launch overhead
# 2. H2D/D2H transfers → overlap with compute?
# 3. CPU-GPU synchronization → unnecessary syncs?
# 4. Memory allocation → cudaMalloc during inference?

# CLI stats summary
nsys stats timeline.nsys-rep

# Filter specific time range
nsys profile --capture-range=cudaProfilerApi \
    ./inference-engine  # Use cudaProfilerStart/Stop in code
```

#### **Nsight Compute (Kernel Profiler):**

```bash
# Profile specific CUDA kernel
ncu --set full \
    --target-processes all \
    --kernel-name "attention_kernel" \
    ./inference-engine

# Key metrics:
# - SM Occupancy (target: > 50%)
# - Memory Throughput (% of peak)
# - Compute Throughput (% of peak)
# - Warp Stall Reasons (what's blocking execution?)
# - Shared Memory Bank Conflicts
# - L1/L2 Cache Hit Rate

# Compare two kernel implementations
ncu --set full -o baseline ./inference-engine --kernel=v1
ncu --set full -o optimized ./inference-engine --kernel=v2
# Open both in GUI, use "Compare" feature
```

---

### **17.9 Practice Questions: Profiling**

1. **You have a flame graph where 40% of CPU time is in `__pthread_mutex_lock`. What does this tell you? How do you fix it?**

   **Answer:** 40% of CPU time wasted spinning on locks. The application has severe lock contention.
   - **Identify the lock:** Check caller stacks in flame graph (which mutex?)
   - **Options:**
     - Reduce critical section size (hold lock for less time)
     - Use lock-free data structures (SPSC queue, atomics)
     - Partition data (per-thread data structures, reduce sharing)
     - Use reader-writer lock if reads >> writes
     - Use per-CPU/per-core data (eliminate sharing entirely)
   - **Verify:** After fix, re-profile. futex_wait should decrease in off-CPU flame graph.

2. **VTune shows 70% Backend Memory Bound, specifically L3 Bound. What's happening and how do you optimize?**

   **Answer:** Working set fits in L2 per-access but total accesses spill to L3 frequently.
   - **Root cause:** Data access pattern has poor spatial locality or working set slightly exceeds L2
   - **Fixes:**
     - Cache blocking (tile loops to fit working set in L2)
     - Structure of Arrays (SoA) instead of AoS (better prefetch)
     - Data prefetch hints (`__builtin_prefetch`)
     - Reduce data size (compression, smaller types, quantization)
     - Pack hot fields together (cache-line-aware struct layout)
   - **Measure:** Use `perf stat -e LLC-load-misses` before/after

3. **Your off-CPU flame graph shows 60% of blocked time in `ep_poll` (epoll_wait). Is this a problem?**

   **Answer:** **Usually NOT a problem.** `epoll_wait` is the event loop waiting for I/O events (normal for async servers). It means the thread is efficiently waiting for work.
   - **Problem if:** latency is high AND CPU is idle. Then investigate why responses take long:
     - Downstream service slow?
     - Batch not filling? (timeout-based batching)
   - **Problem if:** thread wakes and immediately blocks again (no work to do)
   - **NOT a problem if:** CPU utilization is appropriate and latency meets SLA

---

## Module 18: Memory & Leak Analysis

### **18.1 Valgrind — Comprehensive Memory Analysis**

#### **Memcheck (Memory Error Detector):**

```bash
# Detect memory errors (use uninitialized, buffer overflow, use-after-free)
valgrind --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    ./inference-engine --test-mode

# Key error types:
# 1. Invalid read/write (buffer overflow, use-after-free)
#    → Crash risk, security vulnerability
# 2. Use of uninitialized value
#    → Undefined behavior, non-deterministic results
# 3. Memory leaks (definitely lost, indirectly lost, possibly lost)
#    → OOM over time, performance degradation
# 4. Mismatched new/delete (new[] with delete, not delete[])
#    → Heap corruption

# Suppress known issues (third-party library leaks)
valgrind --gen-suppressions=all ./my-app  # Generate suppression file
valgrind --suppressions=my-app.supp ./my-app  # Apply suppressions

# IMPORTANT: Valgrind is 10-50× slower
# Use for: testing/CI, NOT production
# Alternative for production: AddressSanitizer (ASan) — 2× overhead
```

#### **Callgrind (CPU Profile):**

```bash
# Instruction-level profiling
valgrind --tool=callgrind \
    --callgrind-out-file=callgrind.out \
    ./inference-engine --test-mode

# Visualize with kcachegrind/qcachegrind
kcachegrind callgrind.out

# Key metrics:
# - Ir (Instruction reads) — instruction count per function
# - D1mr (L1 data read miss) — L1 cache misses
# - DLmr (LL data read miss) — Last-level cache misses
# - Cycle estimation — estimated CPU cycles
```

#### **Massif (Heap Profiler):**

```bash
# Track heap memory usage over time
valgrind --tool=massif \
    --pages-as-heap=yes \
    --max-snapshots=200 \
    ./inference-engine --test-mode

# Visualize
ms_print massif.out.$PID

# Shows:
# - Memory usage timeline (peak, growth pattern)
# - Allocation sites (which code allocated the most)
# - Useful for: finding memory growth patterns, right-sizing buffers
```

---

### **18.2 AddressSanitizer (ASan) — Production-Ready Memory Checking**

#### **Compile with ASan:**

```bash
# GCC/Clang flag
g++ -fsanitize=address -fno-omit-frame-pointer -g -O1 \
    -o inference-engine inference_engine.cpp

# CMake integration
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")

# Runtime: 2× overhead (vs. Valgrind 10-50×)
# Detects: buffer overflow, use-after-free, use-after-return, double-free, memory leaks
```

#### **ASan + LSan (Leak Sanitizer) Output:**

```
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000014
READ of size 4 at 0x602000000014 thread T0
    #0 0x4c5b8f in process_tokens inference.cpp:142
    #1 0x4c8a12 in run_inference inference.cpp:256
    #2 0x4ca123 in main main.cpp:45

0x602000000014 is located 0 bytes to the right of 20-byte region
allocated by thread T0 here:
    #0 0x7f1234 in malloc
    #1 0x4c5a00 in allocate_buffer inference.cpp:138
```

#### **ThreadSanitizer (TSan) — Data Race Detection:**

```bash
# Compile with TSan (CANNOT combine with ASan)
g++ -fsanitize=thread -fno-omit-frame-pointer -g -O1 \
    -o inference-engine inference_engine.cpp

# Runtime: 5-10× overhead
# Detects: data races, deadlocks, use-after-signal

# Output example:
# WARNING: ThreadSanitizer: data race (pid=12345)
#   Write of size 8 at 0x7f... by thread T2:
#     #0 update_counter stats.cpp:45
#   Previous read of size 8 at 0x7f... by thread T1:
#     #0 read_counter stats.cpp:32
```

---

### **18.3 HeapTrack — Heap Memory Profiler**

#### **What is HeapTrack?**

A lightweight heap profiler for tracking allocations over time. Lower overhead than Valgrind Massif, with a nice GUI.

```bash
# Record allocations
heaptrack ./inference-engine --duration=60

# Analyze
heaptrack_gui heaptrack.inference-engine.$PID.gz

# Or CLI analysis
heaptrack_print heaptrack.inference-engine.$PID.gz

# Key features:
# - Allocation count over time (leak detection)
# - Peak memory usage by call stack
# - Temporary allocations (alloc/free churn)
# - Memory consumption flame graph

# Output summary:
# total runtime: 60.00s
# bytes allocated in total: 4.50GB
# calls to allocation functions: 12,345,678
# peak heap memory consumption: 2.1GB
# peak RSS (including mmap): 8.5GB
# allocations per second: 205,761
#
# Top allocators:
# 1. 1.2GB: KVCache::allocate_block() kv_cache.cpp:89
# 2. 800MB: Tokenizer::encode() tokenizer.cpp:45
# 3. 500MB: std::vector<float>::push_back() [internal]
```

#### **HeapTrack vs. Other Tools:**

| Tool | Overhead | Leak Detection | Temporal View | GUI | Production |
|------|----------|----------------|---------------|-----|------------|
| Valgrind Massif | 10-50× | Yes | Yes | No (ms_print) | No |
| HeapTrack | 2-5× | Yes | Yes | Yes (excellent) | Testing only |
| ASan + LSan | 2× | Yes (at exit) | No | No | CI/staging |
| tcmalloc profiler | 1-2% | No | Yes | Web UI | Yes |
| jemalloc profiler | 1-2% | No | Yes | jeprof | Yes |

---

### **18.4 Production Memory Profiling (tcmalloc/jemalloc)**

#### **tcmalloc Heap Profiling:**

```bash
# Link with tcmalloc
g++ -o inference-engine inference.cpp -ltcmalloc

# Enable heap profiling
HEAPPROFILE=/tmp/myprof ./inference-engine

# Analyze profiles
pprof --text ./inference-engine /tmp/myprof.0001.heap
pprof --svg ./inference-engine /tmp/myprof.0001.heap > heap.svg
pprof --web ./inference-engine /tmp/myprof.0001.heap

# Diff two profiles (find growth)
pprof --text --base=/tmp/myprof.0001.heap \
    ./inference-engine /tmp/myprof.0010.heap
# Shows: only NEW allocations between snapshots
```

#### **jemalloc Profiling:**

```bash
# Link with jemalloc
g++ -o inference-engine inference.cpp -ljemalloc

# Enable profiling
MALLOC_CONF="prof:true,prof_prefix:jeprof" ./inference-engine

# Analyze
jeprof --svg ./inference-engine jeprof.*.heap > heap.svg

# Live profiling (production-safe)
MALLOC_CONF="prof:true,prof_active:false" ./inference-engine
# Activate profiling on-demand:
# mallctl("prof.active", NULL, NULL, &active, sizeof(active));
```

---

### **18.5 Practice Questions: Memory Analysis**

1. **HeapTrack shows 200K allocations/sec in your inference hot path. The allocations are all 64 bytes. How do you fix this?**

   **Answer:** Excessive small allocations cause heap fragmentation and malloc overhead (50ns each = 10ms/sec wasted).
   - **Fix 1:** Arena allocator (pre-allocate, bulk reset per request) — 5ns per alloc
   - **Fix 2:** Object pool (pre-allocate fixed-size objects, recycle)
   - **Fix 3:** Stack allocation (if lifetime is function-scoped)
   - **Fix 4:** Batch allocation (allocate 1000 objects at once, dispense)
   - **Verify:** HeapTrack allocation rate should drop 100× after fix

2. **ASan reports use-after-free in your lock-free queue. The queue passes tests. What's happening?**

   **Answer:** ABA problem or premature deallocation in concurrent access:
   - Consumer reads pointer, gets preempted
   - Producer frees and reallocates the slot
   - Consumer uses stale pointer → use-after-free
   - **Fix:** Use hazard pointers, epoch-based reclamation, or RCU
   - **Alternative:** Don't free (arena allocator with periodic bulk reset)

---

## Module 19: Network Troubleshooting

### **19.1 Wireshark/tshark — Deep Packet Analysis**

#### **Common Capture Filters:**

```bash
# Capture with tshark (CLI Wireshark)
tshark -i eth0 -f "port 8080" -w /tmp/inference.pcap

# Capture only TCP handshakes and resets
tshark -i eth0 -f "tcp[tcpflags] & (tcp-syn|tcp-rst|tcp-fin) != 0" -w /tmp/tcp_events.pcap

# Display filters (post-capture analysis)
tshark -r /tmp/inference.pcap -Y "tcp.analysis.retransmission"      # Retransmits
tshark -r /tmp/inference.pcap -Y "tcp.analysis.duplicate_ack"       # Duplicate ACKs
tshark -r /tmp/inference.pcap -Y "tcp.analysis.zero_window"         # Zero window (buffer full)
tshark -r /tmp/inference.pcap -Y "tcp.analysis.fast_retransmission" # Fast retransmits

# Calculate request-response latency
tshark -r /tmp/inference.pcap -Y "http" \
    -T fields -e frame.time_delta_displayed -e http.request.uri -e http.response.code

# TCP stream reassembly
tshark -r /tmp/inference.pcap -z "io,stat,0.1,tcp" # Throughput per 100ms interval

# Expert analysis (auto-detect problems)
tshark -r /tmp/inference.pcap -z expert
# Warns about: retransmissions, out-of-order, window problems, etc.
```

#### **Network Performance Patterns to Look For:**

| Pattern | Symptom in Wireshark | Root Cause | Fix |
|---------|---------------------|------------|-----|
| Retransmissions | `[TCP Retransmission]` | Packet loss | Check switch buffers, NIC errors |
| Small window | `[TCP Window Update]` low values | Receiver can't keep up | Increase rmem_max |
| Nagle's delay | 40ms gaps between small packets | Nagle + delayed ACK | TCP_NODELAY |
| MTU issues | Fragmentation, ICMP "too big" | MTU mismatch | Set consistent MTU |
| Connection reset | `[RST]` after data | App crash, timeout | Check application logs |
| Slow start | Low throughput for first RTT | TCP slow start | TCP Fast Open, larger initial window |

---

### **19.2 Network Latency Decomposition**

#### **Breaking Down Network Latency:**

```bash
# 1. DNS resolution
dig inference-service.ns.svc.cluster.local +stats
# Query time: 1 msec (should be < 5ms for in-cluster DNS)

# 2. TCP handshake (SYN → SYN-ACK → ACK)
# Use tcpdump to measure SYN → SYN-ACK time
tcpdump -i eth0 -nn "tcp[tcpflags] & tcp-syn != 0" -ttt

# 3. TLS handshake (if HTTPS)
openssl s_time -connect inference:8443 -new -num 100
# Time per connection = TLS overhead

# 4. Application processing (request → response)
# Measure in application metrics or with tcpdump timestamps

# 5. Kernel networking overhead
# Use eBPF to trace time in kernel network stack:
bpftrace -e 'kprobe:tcp_sendmsg { @start[tid] = nsecs; }
kretprobe:tcp_sendmsg /@start[tid]/ {
    @send_us = hist((nsecs - @start[tid]) / 1000);
    delete(@start[tid]);
}'
```

#### **Measuring Inter-Pod Latency:**

```bash
# Ping-based (ICMP, not always representative)
ping -c 100 -i 0.01 $POD_IP
# Shows: avg/min/max/mdev

# TCP-based latency (more representative)
# Use hping3 for TCP SYN latency
hping3 -S -p 8080 -c 100 $POD_IP
# rtt min/avg/max = 0.1/0.3/2.5 ms

# Application-level latency (gRPC health check)
grpcurl -plaintext $POD_IP:8080 grpc.health.v1.Health/Check
# Time this externally for real app-level RTT

# sockperf for precise latency measurement
sockperf ping-pong -i $POD_IP -p 8080 --time 10
# Output: avg latency = 25.3 µs, p99 = 89.2 µs
```

---

### **19.3 Practice Questions: Network Troubleshooting**

1. **Wireshark shows [TCP ZeroWindow] from your inference server. What's happening?**

   **Answer:** Server's receive buffer is full — it's advertising zero window (telling client to stop sending). Causes:
   - Application not reading from socket fast enough (processing backlog)
   - Receive buffer too small (`net.core.rmem_max`)
   - **Fix:** Increase buffer: `sysctl -w net.core.rmem_max=134217728`
   - **Also check:** Why is the application slow to read? (CPU bottleneck? lock contention?)

2. **You see 200ms latency between pods on the same node. `ping` shows 0.1ms. Why the difference?**

   **Answer:** Ping (ICMP) bypasses most of the application stack:
   - Connection pooling/establishment overhead
   - Serialization/deserialization time
   - Request queuing in application
   - iptables/conntrack processing (for TCP, not ICMP)
   - TLS handshake (if encrypted)
   - **Debug:** Use `tcpdump -ttt` on both sides to see where time is spent. Check if it's kernel (iptables) or application (queue).

---

## Module 20: Performance Tuning Recipes

### **20.1 CPU Tuning Recipes**

#### **Recipe: Single-Threaded Bottleneck**

```bash
# Symptom: One CPU at 100%, others idle
# Diagnosis:
mpstat -P ALL 1 5  # One CPU at 100% usr
perf top -p $PID   # Find hot function

# Fix options:
# 1. Parallelize the hot path (split work across threads)
# 2. Optimize the algorithm (reduce complexity)
# 3. Use SIMD (vectorize the computation)
# 4. Move to faster CPU (higher single-thread frequency)
```

#### **Recipe: Lock Contention**

```bash
# Symptom: Multiple CPUs busy but throughput doesn't scale
# Diagnosis:
perf record -e sched:sched_switch -p $PID -- sleep 10
perf report  # See futex_wait in stacks

# Or with bpftrace:
bpftrace -e 'tracepoint:syscalls:sys_enter_futex /pid == '$PID' && args->op == 0/ {
    @[ustack] = count();
}'

# Fix:
# 1. Reduce critical section (hold lock for less time)
# 2. Lock-free alternative (atomics, SPSC queue)
# 3. Per-thread data (eliminate sharing)
# 4. RWLock (if reads >> writes)
# 5. Sharded locks (partition data, separate lock per shard)
```

#### **Recipe: Context Switch Storm**

```bash
# Symptom: High cs in vmstat, high nvcswch in pidstat
# Diagnosis:
pidstat -w -p $PID 1 5
# nvcswch/s > 10000 = too many involuntary switches

# Root cause:
# - Too many threads (> CPU count) competing
# - Short time slices (cfs.min_granularity too small)
# - Threads not pinned (scheduler moves them)

# Fix:
# 1. Reduce thread count (match CPU count)
# 2. Pin threads to CPUs (taskset, CPU affinity)
# 3. Use isolcpus for critical threads
# 4. Increase sched_min_granularity_ns
```

---

### **20.2 Memory Tuning Recipes**

#### **Recipe: TLB Miss Overhead**

```bash
# Symptom: High dTLB-load-misses in perf stat
# Diagnosis:
perf stat -e dTLB-load-misses,dTLB-loads -p $PID -- sleep 10
# If miss rate > 1% → TLB pressure

# Fix:
# 1. Enable huge pages (2MB or 1GB)
echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
# 2. Use MAP_HUGETLB in mmap
# 3. Disable THP (use explicit huge pages for control)
# 4. Reduce working set (fewer pages to translate)
```

#### **Recipe: NUMA Remote Access**

```bash
# Symptom: High latency despite CPU < 50%
# Diagnosis:
perf stat -e node-load-misses,node-loads -p $PID -- sleep 10
# Or: numastat -p $PID (check "other_node" column)

# Fix:
# 1. Bind process: numactl --cpunodebind=0 --membind=0
# 2. First-touch policy: touch memory from correct thread
# 3. In K8s: topologyManagerPolicy: single-numa-node
# 4. Verify: nvidia-smi topo -m (GPU on same NUMA node)
```

#### **Recipe: Excessive Allocation/Free**

```bash
# Symptom: high CPU in malloc/free (visible in flame graph)
# Diagnosis:
perf record -g -p $PID -- sleep 30
perf report  # Look for malloc/free/mmap in hot stacks

# Or: heaptrack for allocation count
heaptrack ./my-app
# "allocations per second: 500,000" = too many

# Fix:
# 1. Arena allocator (bulk alloc, bulk free)
# 2. Object pool (pre-allocate, recycle)
# 3. Stack allocation (alloca, VLAs for small arrays)
# 4. jemalloc/tcmalloc (better scalability than glibc malloc)
# 5. Avoid std::vector growth (reserve() capacity)
```

---

### **20.3 GPU Tuning Recipes**

#### **Recipe: Low GPU Utilization**

```bash
# Symptom: nvidia-smi shows < 50% GPU utilization
# Diagnosis:
nsys profile ./inference-engine
# Look at timeline: gaps between GPU kernels = CPU bottleneck

# Common causes:
# 1. Small batch size (not enough work to saturate GPU)
# 2. CPU preprocessing too slow (GPU waiting for data)
# 3. Synchronous memory copies (H2D blocks compute)
# 4. Kernel launch overhead (many small kernels)

# Fix:
# 1. Increase batch size (fill GPU)
# 2. Use CUDA streams (overlap H2D + compute + D2H)
# 3. Use CUDA Graphs (eliminate launch overhead)
# 4. Async preprocessing (pipeline CPU + GPU work)
```

#### **Recipe: GPU Memory Pressure**

```bash
# Symptom: CUDA OOM, or memory utilization near 100%
# Diagnosis:
nvidia-smi -l 1  # Watch memory usage
# Or: torch.cuda.memory_stats() for per-tensor tracking

# Fix:
# 1. Reduce batch size (less activation memory)
# 2. Quantize model (FP16/INT8 = 50-75% less memory)
# 3. Enable KV-cache paging (PagedAttention)
# 4. Gradient checkpointing (training only)
# 5. Model sharding (tensor parallelism across GPUs)
# 6. Offload to CPU (unified memory as overflow)
```

---

### **20.4 Network Tuning Recipes**

#### **Recipe: High TCP Latency**

```bash
# Symptom: 10ms+ latency for same-datacenter requests
# Diagnosis:
ss -tnip dst $SERVER_IP | grep -i "rtt\|retrans"
# rtt: 0.2/0.1 → network fine, latency is elsewhere
# retrans: 5/100 → packet loss causing retransmissions

# Fix:
# 1. TCP_NODELAY (disable Nagle's algorithm)
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
# 2. Increase initial congestion window
ip route change default via $GW initcwnd 32 initrwnd 32
# 3. TCP Fast Open
echo 3 > /proc/sys/net/ipv4/tcp_fastopen
# 4. BBR congestion control
echo bbr > /proc/sys/net/ipv4/tcp_congestion_control
# 5. Busy polling (avoid interrupt latency)
echo 50 > /proc/sys/net/core/busy_poll
```

#### **Recipe: Connection Exhaustion**

```bash
# Symptom: "cannot assign requested address", TIME_WAIT explosion
# Diagnosis:
ss -s  # Check TIME_WAIT count
netstat -ant | awk '{print $6}' | sort | uniq -c | sort -rn

# Fix:
# 1. Connection pooling (reuse connections)
# 2. tcp_tw_reuse=1 (reuse TIME_WAIT for outgoing)
echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse
# 3. Reduce TIME_WAIT timeout
echo 30 > /proc/sys/net/ipv4/tcp_fin_timeout
# 4. Increase local port range
echo "1024 65535" > /proc/sys/net/ipv4/ip_local_port_range
# 5. Use Unix domain sockets for local communication
```

---

### **20.5 End-to-End Tuning Checklist**

#### **For a New HPC/Inference Service Deployment:**

```
Phase 1: Baseline (Before Any Tuning)
☐ Measure baseline latency (p50, p95, p99, p999)
☐ Measure baseline throughput (requests/sec)
☐ Profile CPU (perf record + flame graph)
☐ Profile memory (heaptrack or tcmalloc)
☐ Profile GPU (nsys if applicable)
☐ Check system: vmstat, iostat, mpstat, free

Phase 2: Host/OS Tuning
☐ CPU isolation (isolcpus, nohz_full)
☐ Huge pages (pre-allocate at boot)
☐ Disable THP (for latency workloads)
☐ CPU governor = performance
☐ Disable C-states / idle
☐ Kernel tuning (sysctl parameters)
☐ IRQ affinity (pin to housekeeping CPUs)
☐ NUMA binding (cpu + memory on same node)

Phase 3: Application Tuning
☐ Optimize hot path (flame graph → fix widest stacks)
☐ Reduce allocations (arena, pool, stack)
☐ Fix lock contention (lock-free, partition)
☐ Optimize memory layout (cache-friendly, SoA)
☐ Vectorize (SIMD where applicable)
☐ GPU: streams, graphs, overlap

Phase 4: Platform Tuning
☐ Kubernetes: CPU Manager, Topology Manager
☐ Networking: SR-IOV or Cilium
☐ GPU: persistence, locked clocks, MIG
☐ Shared memory: /dev/shm sizing
☐ Pod anti-affinity (no noisy neighbors)

Phase 5: Validation
☐ Re-measure latency (compare to baseline)
☐ Stress test (sustained load)
☐ Chaos test (failure scenarios)
☐ Monitor for regressions (alerting)
```

---

### **20.6 Practice Questions: Tuning**

1. **After deploying your inference service, p99 is 50ms but p50 is 3ms. Your tuning target is p99 < 10ms. Walk through your approach.**

   **Answer:**
   1. **Profile the tail:** Capture timestamps of slow requests, correlate with system events
   2. **Check off-CPU:** Off-CPU flame graph for slow requests (what are they blocked on?)
   3. **Check system:** Is it periodic? (Timer ticks, GC, THP compaction)
   4. **Fix likely causes:**
      - `nohz_full` for CPU isolation (eliminates timer jitter)
      - `THP=never` (eliminates compaction pauses)
      - Lock GPU clocks (eliminates frequency variance)
      - Increase batch timeout (avoid partial batch overhead)
   5. **Re-measure:** Expect p99 to drop to 5-10ms range
   6. **If still high:** Investigate application-level (lock contention, memory allocation in hot path)

2. **Your service does 10K req/sec on bare metal but only 6K req/sec in Kubernetes. Where's the 40% regression?**

   **Answer:** Systematic investigation:
   - **Network:** 10-50µs overhead per request × 10K = 100-500ms/sec of overhead. Fix: Cilium/SR-IOV
   - **CPU isolation:** Without CPU Manager static, context switches add latency. Fix: Guaranteed QoS + static policy
   - **NUMA:** Random scheduling across nodes. Fix: Topology Manager
   - **Huge pages:** Not available in container. Fix: hugepages resource + volume mount
   - **Expected recovery:** Each fix recovers 5-15%. Combined should recover most of the 40%.

---

## Module 21: Code Coverage & Quality

### **21.1 Code Coverage for C++ (gcov/lcov)**

#### **Why Coverage Matters for HPC:**

Coverage ensures your hot paths are tested. For performance-critical code:
- 100% branch coverage on hot paths (the code that matters for latency)
- Integration tests cover real deployment scenarios
- Performance regression tests with coverage tracking

#### **Setup:**

```bash
# Compile with coverage flags
g++ -fprofile-arcs -ftest-coverage -O0 -g \
    -o inference-engine-cov inference_engine.cpp

# Run tests
./inference-engine-cov --run-tests

# Generate coverage data
gcov inference_engine.cpp

# Generate HTML report with lcov
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

# Open report
open coverage_html/index.html
```

#### **Coverage Targets:**

| Code Category | Target Coverage | Rationale |
|---------------|----------------|-----------|
| Hot path (inference loop) | 100% line + branch | Performance-critical, must be tested |
| Error handling | 90% line | Failure paths must work |
| Configuration/startup | 80% line | Less critical but should work |
| Debug/admin endpoints | 50% line | Nice to have |
| Overall project | 80%+ line | General quality bar |

#### **Integration with CI:**

```yaml
# GitHub Actions example
- name: Build with coverage
  run: |
    cmake -DCMAKE_BUILD_TYPE=Debug \
          -DCOVERAGE=ON \
          -DCMAKE_CXX_FLAGS="--coverage" ..
    make -j$(nproc)

- name: Run tests
  run: ctest --output-on-failure

- name: Generate coverage report
  run: |
    lcov --capture --directory . --output-file coverage.info
    lcov --remove coverage.info '/usr/*' '*/test/*' --output-file coverage.info
    
- name: Check coverage threshold
  run: |
    COVERAGE=$(lcov --summary coverage.info | grep "lines" | awk '{print $2}' | sed 's/%//')
    if (( $(echo "$COVERAGE < 80" | bc -l) )); then
      echo "Coverage $COVERAGE% below threshold 80%"
      exit 1
    fi
```

---

### **21.2 Sanitizers as Quality Gates**

#### **Sanitizer Matrix:**

| Sanitizer | Flag | Overhead | Detects | When to Use |
|-----------|------|----------|---------|-------------|
| ASan | `-fsanitize=address` | 2× | Buffer overflow, use-after-free, leaks | Every CI build |
| TSan | `-fsanitize=thread` | 5-10× | Data races, deadlocks | Nightly/weekly |
| MSan | `-fsanitize=memory` | 3× | Uninitialized reads | Weekly |
| UBSan | `-fsanitize=undefined` | 1.2× | Undefined behavior | Every CI build |

```bash
# CI pipeline: Run tests with each sanitizer
# Build 1: ASan + UBSan (catches most bugs, reasonable overhead)
g++ -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 \
    -o test-asan tests.cpp

# Build 2: TSan (separate, incompatible with ASan)
g++ -fsanitize=thread -fno-omit-frame-pointer -g -O1 \
    -o test-tsan tests.cpp

# Run both in CI
./test-asan && echo "ASan PASS" || echo "ASan FAIL"
./test-tsan && echo "TSan PASS" || echo "TSan FAIL"
```

---

### **21.3 Performance Regression Testing**

#### **Automated Performance CI:**

```bash
#!/bin/bash
# perf-regression-test.sh

# Run benchmark
./benchmark --iterations=1000 --warmup=100 > current_results.json

# Compare with baseline
python3 compare_perf.py baseline_results.json current_results.json

# compare_perf.py:
# - If p99 increased by > 10%: FAIL
# - If throughput decreased by > 5%: FAIL
# - If memory usage increased by > 10%: WARN
```

```python
# compare_perf.py
import json
import sys

with open(sys.argv[1]) as f:
    baseline = json.load(f)
with open(sys.argv[2]) as f:
    current = json.load(f)

checks = [
    ("p99_latency_ms", 1.10, "FAIL"),   # Max 10% regression
    ("throughput_rps", 0.95, "FAIL"),     # Max 5% drop
    ("peak_memory_mb", 1.10, "WARN"),    # Max 10% growth
    ("p50_latency_ms", 1.05, "WARN"),    # Max 5% regression
]

exit_code = 0
for metric, threshold, severity in checks:
    ratio = current[metric] / baseline[metric]
    # For latency: higher is worse. For throughput: lower is worse.
    if metric in ["throughput_rps"]:
        if ratio < threshold:
            print(f"{severity}: {metric} regressed {ratio:.2f}x (threshold: {threshold})")
            if severity == "FAIL":
                exit_code = 1
    else:
        if ratio > threshold:
            print(f"{severity}: {metric} regressed {ratio:.2f}x (threshold: {threshold})")
            if severity == "FAIL":
                exit_code = 1

sys.exit(exit_code)
```

#### **Continuous Profiling in Production:**

```bash
# Always-on profiling with minimal overhead

# Option 1: perf with low frequency (production-safe)
perf record -F 11 -g -p $PID -o /var/log/perf/$(date +%H%M).perf -- sleep 60
# 11 Hz = negligible overhead, enough for flame graphs over time

# Option 2: Continuous profiling services
# - Pyroscope (open-source)
# - Parca (open-source, eBPF-based)
# - Google Cloud Profiler
# - Datadog Continuous Profiler

# Option 3: eBPF always-on (custom)
# Profile at 1 Hz, aggregate in-kernel, export to Prometheus
bpftrace -e 'profile:hz:1 /pid == '$PID'/ { @[ustack(5)] = count(); }'
```

---

### **21.4 Practice Questions: Code Coverage & Quality**

1. **Your CI has 95% code coverage but you're still seeing production crashes. Why? What's missing?**

   **Answer:** Line coverage doesn't guarantee correctness:
   - **Branch coverage missing:** Lines executed but not all branches (if/else paths)
   - **Concurrency not tested:** Single-threaded tests pass but race conditions in production
   - **Edge cases:** Coverage doesn't mean all input combinations tested
   - **Fix:** Add branch coverage (target 80%+), add TSan to CI, add fuzz testing, add load tests that exercise concurrent paths

2. **How would you set up continuous profiling for a production inference service without impacting latency?**

   **Answer:**
   - Use eBPF-based profiling (Parca/Pyroscope) — kernel-level, < 1% overhead
   - Sample at 19 Hz (prime number avoids aliasing with periodic events)
   - Aggregate stacks in-kernel BPF map (no per-sample userspace overhead)
   - Export aggregated data every 10 seconds to storage
   - **Do NOT use:** Valgrind, ASan, full perf recording (too much overhead)
   - **Result:** Always-on flame graphs, can investigate any time window retroactively

---

## Module 22: Troubleshooting Hands-On Labs

### **Lab 8: Full USE Method Analysis**

**Objective:** Apply USE method to diagnose performance issue on a loaded system.

**Steps:**
1. Run the USE method checklist script (Section 15.1)
2. Identify which resource is the bottleneck (CPU? Memory? Disk? Network? GPU?)
3. Drill down with appropriate tool (perf, iostat, ss, nvidia-smi)
4. Identify root cause
5. Apply fix and re-measure

**Expected Outcome:**
- Systematic identification of bottleneck in < 5 minutes
- Root cause in < 15 minutes
- Fix validated with before/after metrics

---

### **Lab 9: Flame Graph Profiling Pipeline**

**Objective:** Build a complete profiling pipeline (on-CPU + off-CPU + differential).

**Steps:**
1. Profile baseline with `perf record` → generate on-CPU flame graph
2. Profile after load increase → generate second flame graph
3. Generate differential flame graph (compare)
4. Profile off-CPU (bcc/offcputime) → identify blocking
5. Fix the top issue identified
6. Verify with differential flame graph (blue = improvement)

**Expected Outcome:**
- Identify hot function (> 10% CPU)
- Identify blocking point (off-CPU)
- Show 20%+ improvement in differential flame graph

---

### **Lab 10: Memory Leak Detection**

**Objective:** Find and fix a memory leak using multiple tools.

**Steps:**
1. Run with HeapTrack → identify growing allocation
2. Run with ASan (LeakSanitizer) → get stack trace of leak
3. Fix the leak (missing free/delete, reference cycle, etc.)
4. Verify with HeapTrack (stable memory, no growth)
5. Run Valgrind memcheck for any remaining issues

**Expected Outcome:**
- Leak identified with exact source location
- Memory stable after fix (no growth over 10-minute run)
- Zero Valgrind errors after fix

---

### **Lab 11: Network Latency Investigation**

**Objective:** Diagnose and fix high network latency between services.

**Steps:**
1. Measure baseline with `sockperf` (µs-precision RTT)
2. Capture with `tcpdump` during high-latency period
3. Analyze in Wireshark/tshark (retransmissions? window? Nagle?)
4. Apply network tuning (TCP_NODELAY, buffer sizes, BBR)
5. Re-measure and compare
6. Document before/after

**Expected Outcome:**
- Identify specific network issue (e.g., Nagle's delay = 40ms spikes)
- Apply fix (TCP_NODELAY)
- Latency reduction: 40ms → 0.5ms

---

### **Lab 12: End-to-End Performance Optimization**

**Objective:** Take a service from "slow" to "fast" using the complete troubleshooting toolkit.

**Steps:**
1. **60-second analysis** (uptime, vmstat, mpstat, pidstat, iostat, free, sar, ss, top)
2. **USE method** → identify bottleneck resource
3. **Profile** (perf/flame graph for CPU, heaptrack for memory, nsys for GPU)
4. **Diagnose** (root cause from profiles)
5. **Fix** (code change, configuration, or platform tuning)
6. **Validate** (re-run full analysis, confirm fix)
7. **Document** (before/after metrics, what was changed, why)

**Expected Outcome:**
- 2-5× latency improvement
- Complete troubleshooting documented with evidence
- Reproducible methodology for future issues

---

## Resource Library

### **Books:**

**Part I (Application):**
1. "Computer Systems: A Programmer's Perspective" - Bryant & O'Hallaron
2. "Effective Modern C++" - Scott Meyers
3. "C++ Concurrency in Action" - Anthony Williams
4. "Designing Data-Intensive Applications" - Martin Kleppmann
5. "Systems Performance" - Brendan Gregg

**Part II (Platform):**
6. "Kubernetes in Action" - Marko Lukša
7. "BPF Performance Tools" - Brendan Gregg
8. "Linux Kernel Development" - Robert Love
9. "High Performance Browser Networking" - Ilya Grigorik
10. "Site Reliability Engineering" - Google SRE Team

**Part III (Troubleshooting & Profiling):**
11. "Systems Performance: Enterprise and the Cloud" (2nd Edition) - Brendan Gregg
12. "The Art of Debugging with GDB, DDD, and Eclipse" - Matloff & Salzman
13. "High Performance Linux Clusters" - Joseph Sloan
14. "Linux Observability with BPF" - David Calavera
15. "Optimizing Software in C++" - Agner Fog (free online)

### **Online Resources:**

**Systems Programming:**
1. **Ulrich Drepper:** "What Every Programmer Should Know About Memory"
2. **1024cores.net:** Lock-free programming tutorials
3. **Brendan Gregg's Blog:** Performance analysis methodologies
4. **Cloudflare Blog:** High-performance networking
5. **NVIDIA Developer Blog:** GPU optimization

**Platform Engineering:**
6. **Kubernetes Documentation:** CPU Manager, Topology Manager, Device Plugins
7. **NVIDIA GPU Operator Docs:** MIG, DCGM, device plugin configuration
8. **Cilium Documentation:** eBPF networking, performance tuning
9. **Linux kernel docs:** sysctl tuning, cgroups v2, CPU isolation
10. **SR-IOV Network Operator:** Kubernetes RDMA and SR-IOV setup
11. **NVIDIA GTC Talks:** "Kubernetes for HPC", "GPU Scheduling Best Practices"
12. **Red Hat Performance Tuning Guide:** NUMA, huge pages, real-time kernel

**Troubleshooting & Profiling:**
13. **Brendan Gregg's USE Method:** usemethod.com
14. **Flame Graph Repository:** github.com/brendangregg/FlameGraph
15. **BCC/BPF Tools:** github.com/iovisor/bcc
16. **Intel VTune Cookbook:** Performance analysis recipes
17. **Tracy Profiler Docs:** Real-time C++ profiling
18. **Agner Fog's Optimization Manuals:** Instruction tables, microarchitecture
19. **perf Wiki:** perf.wiki.kernel.org
20. **Valgrind Quick Start:** valgrind.org/docs/manual/quick-start.html

### **Tools:**

**Profiling & Debugging:**
1. **perf:** CPU profiling, hardware counters, tracepoints
2. **Intel VTune:** Hotspots, memory access, threading, microarchitecture
3. **Intel Advisor:** Vectorization analysis, roofline model
4. **Intel Inspector:** Thread errors, memory errors
5. **Nsight Systems:** NVIDIA GPU + CPU timeline profiling
6. **Nsight Compute:** NVIDIA GPU kernel profiling (occupancy, memory)
7. **Wireshark/tshark:** Deep packet inspection, protocol analysis
8. **eBPF/bpftrace:** Programmable kernel tracing, near-zero overhead
9. **Tracy:** Real-time frame profiler for C++ (zones, locks, memory)
10. **Hotspot (KDAB):** perf data visualization (flame graphs, timeline)

**Memory & Leak Detection:**
11. **Valgrind (memcheck):** Memory errors, leaks (10-50× overhead)
12. **Valgrind (callgrind):** Instruction-level profiling
13. **Valgrind (massif):** Heap profiling over time
14. **HeapTrack:** Lightweight heap profiler with GUI
15. **AddressSanitizer (ASan):** Buffer overflow, use-after-free (2× overhead)
16. **ThreadSanitizer (TSan):** Data race detection (5-10× overhead)
17. **MemorySanitizer (MSan):** Uninitialized memory reads
18. **UndefinedBehaviorSanitizer (UBSan):** UB detection (1.2× overhead)

**System Monitoring:**
19. **top/htop:** Process overview
20. **vmstat:** Virtual memory statistics
21. **mpstat:** Per-CPU statistics
22. **iostat:** Disk I/O statistics
23. **pidstat:** Per-process stats (CPU, memory, I/O)
24. **free:** Memory overview
25. **iotop:** Per-process I/O
26. **slabtop:** Kernel slab cache
27. **numastat/numactl:** NUMA statistics and control
28. **pcstat:** Page cache statistics
29. **ss/netstat:** Socket statistics
30. **tcpdump:** Network packet capture
31. **ethtool:** NIC configuration and statistics
32. **sar:** System Activity Reporter (historical)

**Platform & Infrastructure:**
33. **nvidia-smi/DCGM:** GPU monitoring and management
34. **cpupower:** CPU frequency and governor management
35. **kubectl/crictl:** Container runtime inspection
36. **Prometheus/Grafana:** Metrics collection and visualization
37. **Cilium CLI:** eBPF networking diagnostics
38. **Flame Graph tools:** stackcollapse-perf.pl, flamegraph.pl, difffolded.pl

**Code Quality:**
39. **gcov/lcov:** Code coverage (C/C++)
40. **genhtml:** Coverage HTML reports
41. **cppcheck:** Static analysis
42. **clang-tidy:** Linting and modernization

---

## Progress Tracker

### **Weekly Checkpoints:**

| Week | Module | Hours Planned | Hours Actual | Completion % | Notes |
|------|--------|---------------|--------------|--------------|-------|
| 1 | Memory & CPU | 10 | | | |
| 2 | Concurrency | 10 | | | |
| 3 | Zero-Copy | 10 | | | |
| 4 | Distributed Systems | 10 | | | |
| 5 | System Design | 10 | | | |
| 6 | Performance + App Labs | 10 | | | |
| 7 | Host & OS Tuning | 8 | | | |
| 8 | Kubernetes for HPC | 10 | | | |
| 9 | Networking + GPU Infra | 10 | | | |
| 10 | Storage + Observability + Platform Labs | 10 | | | |
| 11 | Performance Methodology + Linux Tools | 10 | | | |
| 12 | Profiling (perf, VTune, eBPF, Tracy) | 10 | | | |
| 13 | Memory Analysis + Network Troubleshooting | 10 | | | |
| 14 | Tuning Recipes + Coverage + Troubleshooting Labs | 10 | | | |

### **Hands-On Labs:**

| Lab | Status | Date Completed | Notes |
|-----|--------|----------------|-------|
| Lab 1: Lock-Free Queue | ☐ | | |
| Lab 2: Arena Allocator | ☐ | | |
| Lab 3: Shared Memory IPC | ☐ | | |
| Lab 4: K8s Node for HPC | ☐ | | |
| Lab 5: SR-IOV Network | ☐ | | |
| Lab 6: GPU MIG Config | ☐ | | |
| Lab 7: Observability Stack | ☐ | | |
| Lab 8: USE Method Analysis | ☐ | | |
| Lab 9: Flame Graph Pipeline | ☐ | | |
| Lab 10: Memory Leak Detection | ☐ | | |
| Lab 11: Network Latency Investigation | ☐ | | |
| Lab 12: End-to-End Optimization | ☐ | | |

### **Confidence Self-Assessment:**

| Topic | Before (1-10) | After (1-10) | Improvement |
|-------|---------------|--------------|-------------|
| Memory Hierarchy | | | |
| Concurrency | | | |
| Zero-Copy | | | |
| Distributed Systems | | | |
| System Design | | | |
| Performance Optimization | | | |
| Host & OS Tuning | | | |
| Kubernetes for HPC | | | |
| Networking Infrastructure | | | |
| GPU Infrastructure | | | |
| Observability & SRE | | | |
| Performance Methodology (USE/RED) | | | |
| Linux Performance Tools | | | |
| Profiling (perf/VTune/eBPF) | | | |
| Memory & Leak Analysis | | | |
| Network Troubleshooting | | | |
| Tuning Recipes | | | |
| Code Coverage & Quality | | | |

---

**Good luck with your training! 🚀**

**Remember:** This guide covers the **full stack** — from writing NUMA-aware C++ to deploying it on a properly-tuned Kubernetes cluster to troubleshooting production performance issues with flame graphs and eBPF. The three parts form a complete loop:

```
Part I: BUILD optimized applications (C++/Rust, lock-free, zero-copy)
         ↓
Part II: DEPLOY on optimized infrastructure (K8s, NUMA, GPU, networking)
         ↓
Part III: TROUBLESHOOT & OPTIMIZE end-to-end (USE method, profiling, tuning)
         ↓
         (feed findings back into Parts I & II)
```

**You've got this!** 💪
