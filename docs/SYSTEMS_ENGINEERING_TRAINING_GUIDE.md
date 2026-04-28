# Advanced Systems Engineering Mastery Guide
## Comprehensive Training for Staff/Principal Engineer Interview Readiness

**A One-Stop Guide to Production Systems Engineering, Zero-Copy Architectures, and High-Performance Computing**

---

## Table of Contents

1. [Training Overview](#training-overview)
2. [Module 1: Memory & CPU Architecture](#module-1-memory--cpu-architecture)
3. [Module 2: Concurrency & Parallelism](#module-2-concurrency--parallelism)
4. [Module 3: Zero-Copy Architectures](#module-3-zero-copy-architectures)
5. [Module 4: Distributed Systems Patterns](#module-4-distributed-systems-patterns)
6. [Module 5: System Design & Architecture](#module-5-system-design--architecture)
7. [Module 6: Performance Optimization](#module-6-performance-optimization)
8. [Module 7: Hands-On Labs](#module-7-hands-on-labs)
9. [Interview Question Bank](#interview-question-bank)
10. [Resource Library](#resource-library)
11. [Progress Tracker](#progress-tracker)

---

## Training Overview

### **Target Audience:**
- Engineers with basic systems/platform engineering experience
- Preparing for Staff/Principal AI Infrastructure roles
- Want to go from "deployed services" to "optimized systems at scale"

### **Learning Objectives:**
By the end of this training, you will:
- ✅ Understand **memory hierarchy** and CPU architecture at depth
- ✅ Implement **lock-free data structures** (SPSC, MPMC queues)
- ✅ Design **zero-copy architectures** (shared memory, Apache Arrow)
- ✅ Master **distributed systems patterns** (consensus, consistency, sharding)
- ✅ Optimize **system performance** (cache locality, NUMA, SIMD)
- ✅ Answer **interview questions** with real-world examples from your 4 major projects

### **Training Duration:**
- **Total Hours:** 50–60 hours
- **Duration:** 6–8 weeks (part-time)
- **Format:** 30% theory, 70% hands-on labs

### **Prerequisites:**
- Basic understanding of operating systems
- Some experience with C++ or Rust
- Familiarity with Linux/command-line
- Basic knowledge of distributed systems

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

---

## Resource Library

### **Books:**
1. "Computer Systems: A Programmer's Perspective" - Bryant & O'Hallaron
2. "Effective Modern C++" - Scott Meyers
3. "C++ Concurrency in Action" - Anthony Williams
4. "Designing Data-Intensive Applications" - Martin Kleppmann
5. "Systems Performance" - Brendan Gregg

### **Online Resources:**
1. **Ulrich Drepper:** "What Every Programmer Should Know About Memory"
2. **1024cores.net:** Lock-free programming tutorials
3. **Brendan Gregg's Blog:** Performance analysis
4. **Cloudflare Blog:** High-performance networking
5. **NVIDIA Developer Blog:** GPU optimization

### **Tools:**
1. **perf:** Linux profiling
2. **VTune:** Intel profiling
3. **Nsight Systems:** NVIDIA profiling
4. **eBPF:** Kernel tracing
5. **Wireshark:** Network analysis

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
| 6 | Performance + Labs | 10 | | | |

### **Hands-On Labs:**

| Lab | Status | Date Completed | Notes |
|-----|--------|----------------|-------|
| Lab 1: Lock-Free Queue | ☐ | | |
| Lab 2: Arena Allocator | ☐ | | |
| Lab 3: Shared Memory IPC | ☐ | | |

### **Confidence Self-Assessment:**

| Topic | Before (1-10) | After (1-10) | Improvement |
|-------|---------------|--------------|-------------|
| Memory Hierarchy | | | |
| Concurrency | | | |
| Zero-Copy | | | |
| Distributed Systems | | | |
| System Design | | | |
| Performance Optimization | | | |

---

**Good luck with your training! 🚀**

**Remember:** The goal is **30% depth** across all topics. For deeper expertise, refer to individual books and resources linked in the Resource Library.

**You've got this!** 💪
