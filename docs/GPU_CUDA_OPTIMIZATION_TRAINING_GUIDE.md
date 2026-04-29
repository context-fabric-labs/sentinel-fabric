# GPU/CUDA Optimization Mastery Guide
## Comprehensive Training for Staff/Principal Engineer Interview Readiness

**A One-Stop Guide to NVIDIA GPU Architecture, CUDA Performance Engineering, and Production Inference Optimization**

**This guide expands the GPU/CUDA section from `docs/INTERVIEW_STUDY_GUIDE.md` into a dedicated standalone deep-dive.**

---

## Table of Contents

1. [Training Overview](#training-overview)
2. [Module 1: GPU Architecture Fundamentals](#module-1-gpu-architecture-fundamentals)
3. [Module 2: CUDA Programming Model & Execution](#module-2-cuda-programming-model--execution)
4. [Module 3: Memory Hierarchy & Data Movement](#module-3-memory-hierarchy--data-movement)
5. [Module 4: Occupancy, Performance Modeling & Profiling](#module-4-occupancy-performance-modeling--profiling)
6. [Module 5: Streams, Concurrency & Multi-GPU Coordination](#module-5-streams-concurrency--multi-gpu-coordination)
7. [Module 6: Advanced CUDA Optimization](#module-6-advanced-cuda-optimization)
8. [Module 7: Tensor Cores, Mixed Precision & GEMM](#module-7-tensor-cores-mixed-precision--gemm)
9. [Module 8: Production GPU Patterns for Inference Systems](#module-8-production-gpu-patterns-for-inference-systems)
10. [Module 9: Hands-On Labs](#module-9-hands-on-labs)
11. [Interview Question Bank](#interview-question-bank)
12. [Story Bank: Project-Backed Answers](#story-bank-project-backed-answers)
13. [Resource Library](#resource-library)
14. [Progress Tracker](#progress-tracker)

---

## Training Overview

### **Target Audience:**
- Engineers with basic CUDA, PyTorch, or GPU inference exposure
- Preparing for Staff/Principal AI Infrastructure, GPU Platform, or HPC roles
- Want to move from "I used GPUs" to "I can explain and optimize them from first principles"

### **Learning Objectives:**
By the end of this training, you will:
- Understand how modern NVIDIA GPUs execute work at the SM, warp, block, and grid levels
- Explain the GPU memory hierarchy and choose the right memory space for each access pattern
- Tune kernels for coalescing, shared memory reuse, register pressure, and occupancy
- Use Nsight Systems and Nsight Compute to find real bottlenecks instead of guessing
- Overlap transfers with compute using streams, pinned memory, events, and buffering
- Apply CUDA Graphs, warp primitives, tensor cores, and mixed precision correctly
- Connect interview answers to real project outcomes from Broadcom, CapitalOne, and Fiserv

### **Training Duration:**
- **Total Hours:** 50-60 hours
- **Duration:** 6-8 weeks (part-time)
- **Format:** 35% theory, 65% hands-on labs and profiling

### **Prerequisites:**
- Comfortable with C++ or Python
- Basic linear algebra and matrix multiplication intuition
- Familiarity with Linux command line
- Some exposure to ML inference or performance tuning

### **Priority Legend:**

| Priority | Meaning | Expectation |
|----------|---------|-------------|
| **P0** | Must know cold | Explain from memory and apply in design/debugging |
| **P1** | Important | Implement and discuss trade-offs |
| **P2** | Nice to have | Recognize and discuss when relevant |

### **How to Use This Guide:**
1. Read the module overview first.
2. Study the core concept tables and mental models.
3. Walk through the code snippets until you can explain every line.
4. Do the lab for that module.
5. End each week by answering interview questions out loud without notes.

### **Project Coverage:**

| Project | Hardware | GPU/CUDA Themes |
|--------|----------|-----------------|
| **Broadcom Cloud SWG** | NVIDIA T4, V100 | Dynamic batching, multi-tenant GPU scheduling, throughput optimization |
| **CapitalOne Fraud Platform** | NVIDIA A100, H100 | CUDA Graphs, micro-batching, pinned buffers, low-latency scoring |
| **Fiserv Loan Director** | NVIDIA H100 | Pinned memory, async DMA, CUDA streams, KV-cache pressure management |
| **Apple Siri** | Apple GPU / unified memory | Useful contrast for memory architecture, but not CUDA-specific |

---

## Module 1: GPU Architecture Fundamentals

### **1.1 Why GPUs Are Fast**

#### **CPU vs GPU Mental Model**

**CPU:**
- Optimized for low-latency execution of a few threads
- Large caches, aggressive branch prediction, strong single-thread performance
- Great for control-heavy logic and irregular memory access

**GPU:**
- Optimized for throughput across many threads
- Simpler cores, massive parallelism, very high memory bandwidth
- Great for regular math-heavy work such as GEMM, convolutions, attention, reductions

**Key Interview Line:** GPUs do not make a single thread dramatically faster. They win by keeping thousands of threads in flight so the hardware can hide latency and saturate arithmetic units.

---

### **1.2 Execution Hierarchy: Grid -> Block -> Warp -> Thread**

```
Kernel Launch
  -> Grid
     -> Blocks
        -> Warps (32 threads each)
           -> Threads
```

#### **Core Terms**

- **Thread:** Smallest execution unit. Has its own registers, program counter, and local state.
- **Warp:** Group of 32 threads scheduled together on NVIDIA GPUs. This is the basic unit of execution.
- **Block:** Group of threads that can cooperate via shared memory and `__syncthreads()`.
- **Grid:** All blocks launched for a kernel.
- **SM (Streaming Multiprocessor):** Hardware unit that executes blocks and warps.

#### **Rules That Matter**

1. A block runs entirely on one SM.
2. Threads in different blocks cannot synchronize directly inside a normal kernel.
3. Warps execute one instruction stream at a time; divergence hurts efficiency.
4. Multiple blocks can reside on one SM if resources allow.

#### **Project Connection**
- **All GPU projects:** Every optimization came back to this hierarchy. CapitalOne tuned micro-batch kernels to keep enough warps in flight for sub-5 ms p99 scoring. Fiserv tuned transfer and compute stages so the GPU stayed busy instead of waiting on host copies.

---

### **1.3 SM Anatomy**

An SM is not "one core." It is a throughput engine with many execution resources:

- Warp schedulers and dispatch units
- Register file
- Shared memory / L1 cache
- Load/store units
- Tensor cores
- Special function units

#### **What the SM Scheduler Does**

When one warp stalls on memory, the scheduler can issue instructions from another ready warp. This is the basis of **latency hiding**.

#### **Latency Hiding Example**

If a global memory load takes hundreds of cycles:
- A CPU thread often just waits.
- A GPU SM switches to another ready warp.

The result: high memory latency can be tolerated if enough independent warps are resident and ready.

#### **Independent Thread Scheduling**

Since Volta, NVIDIA GPUs support more flexible scheduling within a warp. This reduces some old warp-synchronous assumptions, but it does **not** eliminate the cost of divergence or the need for correct synchronization.

---

### **1.4 Warp Execution and Divergence**

#### **What Is Warp Divergence?**

When threads in the same warp take different branches:

```cpp
if (threadIdx.x % 2 == 0) {
    // Path A
} else {
    // Path B
}
```

The warp serializes the paths. Threads not on the active path are masked off. You still pay for both paths.

#### **Why It Hurts**

- Lower effective throughput
- Poor utilization of execution units
- Harder-to-predict performance

#### **How to Reduce Divergence**

- Keep branch conditions uniform within a warp when possible
- Group similar work together before launch
- Replace tiny branches with arithmetic or predication when readable
- Split kernels if two code paths are fundamentally different

#### **Interview Nuance**

Not all divergence is worth eliminating. If the branch is rare or the alternative makes memory access worse, you may keep the branch. Optimize for end-to-end time, not abstract purity.

---

### **1.5 GPU Memory Hierarchy**

#### **Memory Space Cheat Sheet**

| Memory | Scope | Typical Latency | Capacity | Best Use |
|--------|-------|-----------------|----------|----------|
| **Registers** | Per thread | ~1 cycle | Smallest | Loop variables, accumulators, hot scalars |
| **Shared Memory** | Per block | ~20-30 cycles if conflict-free | Tens of KB per SM | Tiling, staging, reductions, reuse within block |
| **L1 / Texture Cache** | Per SM | ~20-50 cycles | Small | Cached reads, spatial locality |
| **L2 Cache** | Device-wide | ~100-200 cycles | MBs | Shared cache across SMs |
| **Global Memory (HBM/GDDR)** | Device-wide | ~400-800+ cycles | Largest | Main device storage |
| **Local Memory** | Per thread address space, often in global memory | Similar to global when spilled | Backed by device memory | Register spills, large thread-local arrays |
| **Constant Memory** | Device-wide, cached | Very fast for uniform access | Small | Broadcast constants read by many threads |
| **Texture / Read-only Path** | Device-wide cached reads | Low if cache-friendly | Cached view | Irregular read-only access with locality |

**Important:** These are ballpark values for reasoning, not hard contracts. Exact latency depends on architecture, caching, access pattern, and contention.

#### **How to Think About Each Space**

- **Registers:** Fastest, but too many registers per thread can reduce occupancy.
- **Shared Memory:** Software-managed scratchpad. Fast when you control layout well.
- **Global Memory:** Large and bandwidth-rich, but high latency. Coalescing matters.
- **Local Memory:** "Local" means addressability, not on-chip placement. Spilled locals are expensive.
- **Constant Memory:** Great when all threads read the same address at the same time.

---

### **1.6 Occupancy vs Utilization**

#### **Occupancy**

```
Occupancy = active warps per SM / maximum warps per SM
```

Higher occupancy can help hide latency, but maximum occupancy is not the goal by itself.

#### **Utilization**

This is the real question: are the arithmetic units, memory pipelines, and tensor cores doing useful work?

#### **Why 100% Occupancy Is Not Always Best**

- Extra threads may increase register pressure
- More blocks may reduce cache locality
- Shared memory tiling may lower occupancy but still speed up the kernel
- Some kernels are already bandwidth-limited and cannot go faster

**Interview Line:** Occupancy is a means to latency hiding, not the end goal. The right target is the best throughput or latency for the actual workload.

---

### **1.7 Architecture Generations Cheat Sheet**

| GPU | Architecture | Compute Capability | Why It Matters |
|-----|--------------|-------------------|----------------|
| **V100** | Volta | `sm_70` | First major tensor core generation, independent thread scheduling |
| **T4** | Turing | `sm_75` | Strong inference efficiency, good INT8 story, common cloud inference card |
| **A100** | Ampere | `sm_80` | TF32, BF16, larger L2, MIG, `cp.async`, major inference/training workhorse |
| **H100** | Hopper | `sm_90` | FP8, Transformer Engine, TMA, thread block clusters, top-end inference/training |

#### **Interview Notes by Generation**

- **T4:** Great example for cost-efficient inference; watch PCIe and memory limits.
- **V100:** Useful interview anchor for the first wave of tensor core-based acceleration.
- **A100:** Common production answer for mixed precision, MIG isolation, and mature large-model inference.
- **H100:** Mention FP8, Transformer Engine, and Hopper-specific data movement features if the role is cutting-edge GPU infra.

#### **Project Connection**
- **Broadcom:** T4/V100 for dynamic batching and tenant-aware scheduling.
- **CapitalOne:** A100/H100 for micro-batched fraud scoring and CUDA Graphs.
- **Fiserv:** H100 for large-context inference and GPU memory pressure management.

---

### **1.8 Quick Check: Architecture**

**Practice Questions:**
1. What is the relationship between threads, warps, blocks, grids, and SMs?
2. Why does warp divergence hurt throughput?
3. What is the difference between occupancy and utilization?
4. Why can a lower-occupancy kernel beat a higher-occupancy kernel?
5. When is constant memory a great fit, and when is it not?

---

## Module 2: CUDA Programming Model & Execution

### **2.1 Kernel Launch Configuration**

#### **Basic Launch Syntax**

```cpp
dim3 block(256);
dim3 grid((n + block.x - 1) / block.x);
my_kernel<<<grid, block>>>(...);
```

#### **Canonical Indexing**

```cpp
__global__ void saxpy(const float* x, const float* y, float* out, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        out[i] = 2.0f * x[i] + y[i];
    }
}
```

#### **Launch Heuristics**

| Workload | Common Starting Point | Why |
|----------|-----------------------|-----|
| 1D vector ops | 128-256 threads/block | Good balance for memory-bound work |
| 2D image or matrix tiles | `16x16`, `32x8` | Natural spatial mapping |
| Reductions | 128-256 threads/block | Enough warps for latency hiding without too much sync |
| GEMM | Use cuBLAS/CUTLASS first | Hand-written kernels are rarely the first answer |

#### **Interview Rule of Thumb**

Start with a multiple of 32, profile, then tune based on:
- registers per thread
- shared memory per block
- achieved occupancy
- memory throughput
- stall reasons

---

### **2.2 Thread Cooperation with Shared Memory**

#### **Declaring Shared Memory**

```cpp
__global__ void kernel(...) {
    __shared__ float tile[32][33];
    ...
}
```

Or dynamic shared memory:

```cpp
__global__ void kernel(...) {
    extern __shared__ float scratch[];
    ...
}
```

#### **When to Use Shared Memory**

- Reuse data across multiple threads in a block
- Tile global memory into on-chip storage
- Perform block-level reductions, scans, histograms
- Reorder access patterns to become coalesced

#### **When Not to Use It**

- Data is used once per thread
- Reuse is too low to pay for extra instructions and synchronization
- Shared memory usage would severely reduce occupancy

---

### **2.3 Synchronization Primitives**

| Primitive | Scope | What It Does | Common Use |
|-----------|-------|--------------|------------|
| `__syncthreads()` | Block | Barrier + shared memory visibility within block | Tiled kernels, reductions |
| `__syncwarp(mask)` | Warp | Warp-level barrier | Warp-specialized code |
| `__threadfence()` | Device | Orders writes to device memory | Producer-consumer across blocks with atomics |
| `cudaDeviceSynchronize()` | Device / host | Wait for all prior device work | Debugging, timing boundaries |

#### **Critical Correctness Rule**

Every thread in the block must reach `__syncthreads()` or the kernel can deadlock.

#### **Interview Nuance**

`__threadfence()` is **not** a barrier. It enforces ordering, not collective arrival.

---

### **2.4 Atomics**

#### **What Atomics Solve**

They let multiple threads safely update a shared value:

```cpp
atomicAdd(&sum, value);
```

#### **What Makes Atomics Slow**

- High contention on the same address
- Global-memory atomics are more expensive than register operations
- Serialization grows with hot spots

#### **Optimization Pattern**

1. Reduce locally in registers
2. Reduce within a warp
3. Reduce within shared memory
4. Use one atomic per block or one atomic per warp

**Interview Line:** Atomics are often the right correctness primitive, but performance usually improves when you reduce the number of atomic operations rather than trying to avoid atomics entirely.

---

### **2.5 Warp Primitives**

#### **Why Warp Primitives Matter**

Warp-level collectives let threads exchange data without shared memory for intra-warp communication.

Common intrinsics:
- `__shfl_sync`
- `__shfl_down_sync`
- `__ballot_sync`
- `__activemask`
- `__match_any_sync`

#### **Warp Reduction Example**

```cpp
__device__ float warp_sum(float v) {
    for (int offset = 16; offset > 0; offset >>= 1) {
        v += __shfl_down_sync(0xffffffff, v, offset);
    }
    return v;
}
```

#### **Why This Helps**

- Fewer shared-memory accesses
- Fewer barriers
- Lower latency for reductions, scans, voting, compaction

#### **Interview Nuance**

Warp intrinsics work best when you reason explicitly about the active mask. They are powerful, but correctness gets subtle with partial warps and divergence.

---

### **2.6 Cooperative Groups (P1)**

CUDA cooperative groups give structured APIs for thread groups beyond raw intrinsics.

Use them when:
- you want cleaner synchronization abstractions
- you are building reusable collectives
- you want code that is easier to read than hand-written warp logic

They are useful, but you should still understand the underlying warp and block execution model first.

---

### **2.7 Common Correctness Pitfalls**

1. Off-by-one indexing at grid boundaries
2. Missing error checks after kernel launch
3. Assuming different blocks can synchronize mid-kernel
4. Forgetting shared-memory bank layout during transpose or tiling
5. Using `cudaMemcpyAsync` from pageable host memory and assuming full overlap
6. Calling `__syncthreads()` under a divergent branch
7. Ignoring race conditions because "it seems to work on my GPU"

#### **Minimal Error-Check Pattern**

```cpp
my_kernel<<<grid, block>>>(...);
cudaError_t err = cudaGetLastError();
if (err != cudaSuccess) {
    fprintf(stderr, "launch failed: %s\n", cudaGetErrorString(err));
}
```

---

### **2.8 Quick Check: Programming Model**

**Practice Questions:**
1. How do you choose grid and block dimensions for a new kernel?
2. What is the difference between `__syncthreads()` and `__threadfence()`?
3. When should you use warp shuffles instead of shared memory?
4. Why do atomics become expensive under contention?
5. Why can a kernel be functionally correct but still perform poorly?

---

## Module 3: Memory Hierarchy & Data Movement

### **3.1 Coalesced Global Memory Access**

#### **What Coalescing Means**

Threads in a warp should access contiguous, aligned memory so the hardware can combine requests into as few memory transactions as possible.

#### **Good Access Pattern**

```cpp
int i = blockIdx.x * blockDim.x + threadIdx.x;
out[i] = in[i];
```

#### **Bad Access Pattern**

```cpp
int i = blockIdx.x * blockDim.x + threadIdx.x;
out[i * stride] = in[i * stride];
```

If `stride` is large, the warp touches many cache lines and memory transactions explode.

#### **Why Coalescing Matters**

- Fewer global memory transactions
- Higher effective bandwidth
- Lower DRAM pressure
- Better cache behavior

#### **Data Layout Rule**

Prefer **structure of arrays (SoA)** over **array of structures (AoS)** when threads read one field across many items.

---

### **3.2 Alignment and Vectorized Loads**

Aligned vectorized accesses can reduce instruction count and improve bandwidth when data layout allows.

Examples:
- `float4`
- `int4`
- `half2`

#### **When Vectorization Helps**

- Data is naturally aligned
- Threads read contiguous vectors
- Memory bandwidth is a bottleneck

#### **When It Backfires**

- Misaligned accesses force extra work
- Tail handling gets messy
- Register pressure increases too much

**Interview Line:** Vectorized loads are an optimization, not a law. First fix coalescing and layout; then consider vectorization.

---

### **3.3 Shared Memory Tiling**

#### **Why Tiling Works**

Load a tile from global memory once, reuse it many times from shared memory.

This is the backbone of:
- matrix multiplication
- stencil codes
- convolution
- transpose
- attention kernels

#### **Tiled GEMM Skeleton**

```cpp
template <int TILE>
__global__ void matmul_tiled(const float* A, const float* B, float* C, int N) {
    __shared__ float As[TILE][TILE];
    __shared__ float Bs[TILE][TILE];

    int row = blockIdx.y * TILE + threadIdx.y;
    int col = blockIdx.x * TILE + threadIdx.x;

    float acc = 0.0f;

    for (int t = 0; t < N; t += TILE) {
        As[threadIdx.y][threadIdx.x] = A[row * N + (t + threadIdx.x)];
        Bs[threadIdx.y][threadIdx.x] = B[(t + threadIdx.y) * N + col];
        __syncthreads();

        #pragma unroll
        for (int k = 0; k < TILE; ++k) {
            acc += As[threadIdx.y][k] * Bs[k][threadIdx.x];
        }
        __syncthreads();
    }

    C[row * N + col] = acc;
}
```

#### **What This Teaches in Interviews**

- Why shared memory exists
- Why barriers are necessary
- How data reuse changes arithmetic intensity
- Why block size, tile shape, and register usage all interact

---

### **3.4 Shared Memory Bank Conflicts**

Shared memory is banked. If multiple threads in a warp hit the same bank, accesses can serialize.

#### **Classic Fix**

Add padding:

```cpp
__shared__ float tile[32][33];
```

The extra column breaks the stride pattern that causes bank conflicts in transpose-style kernels.

#### **Interview Line**

Shared memory is fast only when you lay it out well. A badly banked shared-memory kernel can lose to a well-coalesced global-memory kernel.

---

### **3.5 Registers and Spills**

Registers are the fastest storage, but heavy register use reduces the number of resident warps.

If the compiler runs out of registers, it spills values into local memory, which is usually backed by global memory.

#### **How to Spot Register Pressure**

- `nvcc -Xptxas -v ...`
- Nsight Compute register and local memory metrics
- Achieved occupancy lower than expected
- Stall reasons or bandwidth consumed by spills

#### **Typical Causes**

- Large per-thread arrays
- Aggressive unrolling
- Fusing too much work into one kernel
- Excess temporaries from complicated indexing

#### **Optimization Levers**

- Reduce live variable count
- Reconsider unrolling
- Split the kernel if needed
- Use `__restrict__` when correct
- Tune launch bounds carefully if you know the trade-off

---

### **3.6 Host-Device Transfer Optimization**

#### **Pageable vs Pinned Memory**

| Host Memory Type | Async-Friendly | Best Use | Trade-off |
|------------------|----------------|----------|-----------|
| **Pageable** | Limited | Simplicity | Extra staging, worse overlap |
| **Pinned (page-locked)** | Yes | High-throughput H2D/D2H | Scarce OS resource, use carefully |
| **Mapped zero-copy** | CPU memory directly visible to GPU | Tiny control data, integrated-like use cases | Much slower than real device memory for repeated access |
| **Unified Memory** | Productivity first | Rapid development, complex ownership | Can page-fault and migrate unpredictably if unmanaged |

#### **Pinned Memory**

Pinned memory is required for reliable overlap with `cudaMemcpyAsync`.

```cpp
cudaHostAlloc(&h_buf, bytes, cudaHostAllocDefault);
cudaMemcpyAsync(d_buf, h_buf, bytes, cudaMemcpyHostToDevice, stream);
```

#### **Unified Memory**

Use when productivity matters or ownership is awkward, but performance-sensitive code usually needs:
- `cudaMemPrefetchAsync`
- `cudaMemAdvise`
- profiling for migration faults

#### **Interview Nuance**

Zero-copy host access sounds attractive, but on discrete GPUs it is usually a niche optimization. For hot data reused many times, copy it to device memory.

---

### **3.7 Effective Bandwidth**

One of the fastest ways to sanity-check a memory-bound kernel:

```
Effective Bandwidth = bytes read + bytes written / elapsed time
```

If your effective bandwidth is far below what the hardware should sustain, look at:
- coalescing
- unnecessary transfers
- shared-memory bank conflicts
- synchronization overhead
- kernel launch configuration

---

### **3.8 Project Connection: Fiserv Zero-Copy Transfer Engine**

**Problem:** Large-context inference workflows were paying too much host-to-device transfer overhead.

**What Was Done:**
- Preallocated pinned host buffers
- Used async DMA with CUDA streams
- Added event-based completion tracking
- Reused buffers and overlapped copies with downstream compute

**Result:**
- **3.2x faster H2D transfers**
- **85% GPU utilization**
- Better end-to-end pipeline efficiency under load

**Interview Framing:** This is a strong example for explaining when pinned memory and stream overlap matter more than kernel math itself.

---

### **3.9 Quick Check: Memory Optimization**

**Practice Questions:**
1. What is coalesced memory access and why does it matter?
2. When is shared memory worth using?
3. What is a bank conflict and how do you fix it?
4. Why can register spills be devastating?
5. When would you choose pinned memory over unified memory?

---

## Module 4: Occupancy, Performance Modeling & Profiling

### **4.1 Occupancy Fundamentals**

Occupancy depends on four main resource constraints:

1. Threads per block
2. Registers per thread
3. Shared memory per block
4. Architectural limits on blocks and warps per SM

#### **Common Misunderstanding**

High occupancy does not guarantee good performance.

Examples:
- A memory-bound kernel may already saturate DRAM at 50% occupancy
- More occupancy can hurt if it forces register spills
- A tiled kernel can be faster even if shared memory lowers occupancy

---

### **4.2 Latency Hiding and Eligible Warps**

The scheduler can only issue instructions from **eligible warps**. If most warps are stalled on memory or barriers, the SM goes underutilized.

#### **Typical Stall Categories**

- Waiting on global memory
- Waiting at barriers
- Dependency on previous instructions
- Not enough active/eligible warps

#### **What to Ask When Profiling**

- Are we memory-bound or compute-bound?
- Are there enough ready warps?
- Are we saturating memory bandwidth?
- Is synchronization excessive?
- Is launch overhead dominating?

---

### **4.3 Arithmetic Intensity and Roofline Thinking**

#### **Arithmetic Intensity**

```
Arithmetic Intensity = FLOPs / bytes moved
```

- **Low arithmetic intensity:** Often memory-bound
- **High arithmetic intensity:** More likely compute-bound

#### **Examples**

| Operation | Likely Character |
|-----------|------------------|
| Vector copy | Strongly memory-bound |
| SAXPY | Memory-bound |
| Reduction | Often memory-bound with synchronization cost |
| GEMM | Compute-heavy when tiled well |
| Attention | Mixed; often constrained by both memory movement and math |

#### **Interview Line**

Before optimizing a kernel, decide whether you are chasing bandwidth, occupancy, launch overhead, or math throughput. The wrong optimization target wastes time.

---

### **4.4 Timing Correctly**

#### **Use CUDA Events for Device Timing**

```cpp
cudaEvent_t start, stop;
cudaEventCreate(&start);
cudaEventCreate(&stop);

cudaEventRecord(start, stream);
my_kernel<<<grid, block, 0, stream>>>(...);
cudaEventRecord(stop, stream);
cudaEventSynchronize(stop);

float ms = 0.0f;
cudaEventElapsedTime(&ms, start, stop);
```

#### **Why Host Timers Mislead**

- Kernel launches are asynchronous
- Transfers can overlap
- A host-side timer may include unrelated work or synchronization

---

### **4.5 Nsight Systems vs Nsight Compute**

#### **Nsight Systems**

Use it first to answer:
- Are there gaps between kernels?
- Are H2D and D2H overlapping with compute?
- Is CPU-side launch overhead significant?
- Are streams serialized by accident?

Think: **timeline and pipeline behavior**

#### **Nsight Compute**

Use it to answer:
- Is the kernel memory-bound or compute-bound?
- How efficient are memory accesses?
- Are there bank conflicts?
- What is achieved occupancy?
- Which stall reasons dominate?

Think: **kernel internals and hardware counters**

#### **Local Repo Resources**
- `cuda-lab/docs/NSIGHT_SYSTEMS_GUIDE.md`
- `cuda-lab/docs/NSIGHT_COMPUTE_GUIDE.md`

---

### **4.6 Metrics That Matter**

| Metric | What It Tells You |
|--------|-------------------|
| **Kernel duration** | Is the kernel itself actually expensive? |
| **Achieved occupancy** | Are enough warps resident to hide latency? |
| **Eligible warps per cycle** | Can the scheduler issue work consistently? |
| **DRAM throughput % peak** | Are you bandwidth-limited? |
| **Global memory efficiency** | Are loads/stores coalesced? |
| **L2 hit rate** | Is cache helping or are accesses too scattered? |
| **Registers per thread** | Is register pressure constraining occupancy? |
| **Local memory traffic** | Are spills happening? |
| **Shared memory bank conflicts** | Is your tile layout causing serialization? |
| **Memcpy overlap** | Are transfers hiding behind compute? |

#### **Profiling Order**

1. Verify correctness
2. Measure end-to-end latency/throughput
3. Run Nsight Systems
4. Run Nsight Compute on the slowest kernels
5. Change one thing at a time
6. Re-measure

---

### **4.7 Symptom-to-Bottleneck Cheat Sheet**

| Symptom | Likely Cause | First Thing to Check |
|---------|--------------|----------------------|
| Low bandwidth on simple copy kernel | Poor coalescing or small launch | Global load/store efficiency, block size |
| High occupancy but poor speed | Memory bottleneck or barrier overhead | DRAM throughput, stall reasons |
| Good kernel time but poor end-to-end latency | Launch overhead or transfer gaps | Nsight Systems timeline |
| Streams do not overlap | Pageable memory or hidden sync | Host allocation type, default stream use |
| Shared-memory kernel slower than baseline | Bank conflicts or low reuse | Shared-memory layout, conflict metrics |
| Tiled kernel regressed badly | Register spills | Registers/thread, local memory traffic |
| Graph capture gives no win | Kernel runtime dominates or shapes too dynamic | Timeline, graph reuse rate |
| Tensor-core GPU shows weak speedup | Wrong dtype/layout or memory-bound kernel | Library config, arithmetic intensity |
| p99 spikes under burst load | Queue wait, memory pressure, or batch policy | Queue time, HBM headroom, batch fill |
| Sudden OOMs at same traffic level | Fragmentation or cache growth | Allocator behavior, KV-cache telemetry |

---

### **4.8 Benchmarking Checklist**

- Warm up before measurement
- Reuse allocations; do not benchmark `cudaMalloc` unless that is the point
- Benchmark representative problem sizes
- Compare against vendor libraries when relevant
- Report throughput and latency, not only kernel time
- Include correctness checks
- Measure percentiles if the production workload is latency-sensitive

#### **Interview Line**

Real optimization work is not "I changed a line and got a speedup." It is a profiling loop with a baseline, hypothesis, controlled change, and validation.

---

### **4.9 Project Connection: CapitalOne Launch Overhead**

CapitalOne's fraud-scoring path had a very tight latency budget:

- 20-50 small kernels in a scoring path
- launch overhead mattered as much as the math
- CUDA Graphs reduced launch overhead from about **150 us** to about **5 us**
- savings per inference: about **145 us**

**Why this matters in interviews:** It shows you know when the bottleneck is not FLOPs or bandwidth, but orchestration overhead itself.

---

### **4.10 Quick Check: Profiling & Performance**

**Practice Questions:**
1. How do you know if a kernel is memory-bound or compute-bound?
2. Why can 100% occupancy be a bad goal?
3. When do you use Nsight Systems vs Nsight Compute?
4. Why are CUDA events better than naive host timing?
5. How do you design a trustworthy benchmark?

---

## Module 5: Streams, Concurrency & Multi-GPU Coordination

### **5.1 CUDA Streams**

A stream is an ordered queue of work on the GPU.

Within a single stream:
- operations are serialized in issue order

Across different streams:
- operations may overlap if dependencies and hardware resources allow

#### **Core Building Blocks**
- streams
- events
- async memcpy
- host callbacks or polling

#### **Default Stream Nuance**

Be careful with the default stream:

- In legacy behavior, the default stream can implicitly synchronize with other streams.
- Per-thread default stream behavior is less surprising, but you still need to know what your environment uses.

**Interview Line:** A lot of "my async pipeline does not overlap" bugs come down to accidental synchronization on the default stream.

---

### **5.2 Overlapping H2D -> Compute -> D2H**

#### **What You Need for True Overlap**

1. Non-default streams
2. Pinned host memory
3. Hardware copy engine support
4. No hidden dependencies on the same buffers

#### **Double-Buffered Pipeline**

```
Stream 0: H2D(batch0) -> Kernel(batch0) -> D2H(batch0)
Stream 1:              H2D(batch1) -> Kernel(batch1) -> D2H(batch1)
```

This lets transfer for batch 1 overlap with compute for batch 0.

#### **Event-Based Dependency Example**

```cpp
cudaMemcpyAsync(d_in0, h_in0, bytes, cudaMemcpyHostToDevice, stream0);
kernel<<<grid, block, 0, stream0>>>(d_in0, d_out0);
cudaEventRecord(done0, stream0);

cudaStreamWaitEvent(stream1, done0, 0);
cudaMemcpyAsync(h_out0, d_out0, out_bytes, cudaMemcpyDeviceToHost, stream1);
```

#### **Common Reasons Overlap Does Not Happen**

- pageable memory instead of pinned memory
- one giant kernel monopolizes the GPU
- implicit sync on default stream
- same stream used for all stages
- PCIe already saturated

---

### **5.3 Multi-Stream Concurrency**

Small kernels can often overlap across streams. Huge kernels that use most SMs typically cannot.

#### **Good Candidates for Multi-Stream Execution**

- batched inference for many small requests
- transfer plus compute pipelines
- independent preprocessing and postprocessing stages

#### **Bad Candidates**

- one massive GEMM that already saturates the device
- workloads with unavoidable stream synchronization after every step

#### **Interview Nuance**

More streams do not automatically mean more throughput. They help when there is idle hardware to exploit or when copy/compute engines can operate independently.

---

### **5.4 Stream Priorities**

Useful for latency-sensitive workloads sharing a GPU with batch work:

- high-priority stream for interactive scoring
- lower-priority streams for bulk jobs

But priorities are not magic preemption. If a kernel is already running and hogging the device, the high-priority stream may still wait.

---

### **5.5 Multi-GPU Basics**

#### **Scale-Up Options**

- **Data parallel:** Same model on many GPUs, split requests or batches
- **Model parallel:** Split model across GPUs
- **Pipeline parallel:** Stages on different GPUs
- **Tensor parallel:** Split matrix operations across devices

#### **Core Concepts**

- `cudaDeviceEnablePeerAccess`
- NVLink vs PCIe
- NCCL for collectives
- keeping transfers off the CPU when possible

#### **Interview Rule**

Before distributing a workload across many GPUs, make the single-GPU path efficient. Otherwise you scale inefficiency.

---

### **5.6 Multi-Tenant Isolation**

Production systems often need both performance and isolation.

#### **Isolation Tools**

- Dedicated GPU pools
- MIG on A100/H100
- Time-slicing
- Memory quotas
- Admission control
- Health-aware routing

#### **Project Connection**

**CapitalOne**
- Dedicated Tier 1 GPU pool for real-time scoring
- Separate pools for slower reasoning tiers
- Protected sub-5 ms p99 traffic from longer-running work

**Broadcom**
- Multi-tenant GPU scheduling with throughput-oriented batching

**Fiserv**
- GPU-headroom-aware routing across 20+ inference pods

---

### **5.7 Quick Check: Streams & Concurrency**

**Practice Questions:**
1. What conditions must hold for H2D and compute to overlap?
2. Why might multiple streams fail to improve throughput?
3. When do stream priorities help?
4. What is the difference between data parallel and model parallel?
5. When would you choose MIG over time-slicing?

---

## Module 6: Advanced CUDA Optimization

### **6.1 CUDA Graphs**

#### **What Problem CUDA Graphs Solve**

Repeated launch of many small kernels and memcpys can burn significant CPU overhead.

CUDA Graphs let you:
- capture a fixed sequence once
- instantiate it
- replay it with very low overhead

#### **Best Fit**

- steady execution path
- stable tensor shapes or a small number of shape buckets
- low-latency inference pipelines

#### **Poor Fit**

- highly dynamic control flow
- constantly changing graph topology
- workloads where one large kernel dominates time anyway

#### **Simple Capture Pattern**

```cpp
cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal);

cudaMemcpyAsync(d_in, h_in, in_bytes, cudaMemcpyHostToDevice, stream);
kernel1<<<grid1, block1, 0, stream>>>(...);
kernel2<<<grid2, block2, 0, stream>>>(...);
cudaMemcpyAsync(h_out, d_out, out_bytes, cudaMemcpyDeviceToHost, stream);

cudaStreamEndCapture(stream, &graph);
cudaGraphInstantiate(&graph_exec, graph, nullptr, nullptr, 0);

// Fast replay
cudaGraphLaunch(graph_exec, stream);
```

#### **CapitalOne Result**

- ~30 kernels per inference path
- launch overhead reduced from about **150 us** to about **5 us**
- about **145 us saved per inference**

This mattered because the entire p99 budget was only about **5 ms**.

---

### **6.2 Kernel Fusion**

#### **Why Fuse**

- fewer kernel launches
- less global memory traffic
- better producer-consumer locality

#### **Examples**

- bias + activation
- normalization + activation
- small postprocessing stages

#### **Trade-offs**

- higher register pressure
- larger code surface
- less reusable kernels
- harder profiling and debugging

**Interview Line:** Fuse when it removes a real bottleneck, not just because fewer kernels feels cleaner.

---

### **6.3 Warp-Optimized Reductions**

#### **Common Reduction Strategy**

1. Each thread accumulates multiple elements in registers
2. Reduce within warp using `__shfl_down_sync`
3. One value per warp written to shared memory
4. Final warp reduces the warp results

#### **Why It Works**

- fewer atomics
- fewer barriers
- less shared-memory traffic

---

### **6.4 Asynchronous Copy and Pipelining**

#### **Ampere: `cp.async`**

Ampere introduced async copy from global memory to shared memory, enabling software pipelines that overlap data movement and compute inside a kernel.

#### **Hopper: TMA**

Hopper extends this with Tensor Memory Accelerator (TMA), which reduces per-thread overhead for moving multidimensional tiles.

#### **When to Mention This in Interviews**

- kernel-intensive roles
- Hopper/Ampere optimization work
- custom attention, GEMM, or stencil kernels

#### **What to Say**

These features improve the load-compute pipeline inside the kernel, but only if tile reuse is high enough and the implementation cost is justified.

---

### **6.5 Persistent Kernels**

Persistent kernels keep a fixed set of blocks resident and pull work from a queue instead of launching many small kernels.

#### **When Useful**

- ultra-fine-grained work
- repeated tiny tasks
- minimizing launch overhead
- queue-driven runtimes

#### **Risks**

- fairness problems
- harder debugging
- trickier scheduling
- watchdog limits in some environments

This is more advanced than many interviews require, but it is a strong differentiator for GPU systems roles.

---

### **6.6 Optimization Anti-Patterns**

1. Chasing occupancy without measuring throughput
2. Adding shared memory where there is no reuse
3. Over-fusing until register spills dominate
4. Using unified memory in a hot path without profiling page migration
5. Assuming graph capture helps a kernel that already runs for milliseconds
6. Hand-writing GEMM before trying cuBLAS, cuBLASLt, or CUTLASS

---

### **6.7 Quick Check: Advanced Optimization**

**Practice Questions:**
1. When do CUDA Graphs help the most?
2. Why can kernel fusion speed things up?
3. What is the benefit of warp-level reductions?
4. What do `cp.async` and TMA improve?
5. When would a persistent kernel make sense?

---

## Module 7: Tensor Cores, Mixed Precision & GEMM

### **7.1 What Tensor Cores Are**

Tensor cores accelerate matrix multiply-accumulate on small tiles using reduced precision inputs with efficient accumulation modes.

They are different from regular CUDA cores:

- **CUDA cores:** General-purpose arithmetic
- **Tensor cores:** Specialized high-throughput matrix math units

#### **Where They Matter**

- GEMM
- convolutions
- attention projections
- MLP layers
- embedding-heavy inference paths with dense math

---

### **7.2 Precision Modes Cheat Sheet**

| Precision | Best Use | Strength | Risk |
|-----------|----------|----------|------|
| **FP32** | Baseline, numerically sensitive code | Safest | Slowest / largest |
| **TF32** | Ampere+ matrix math with FP32-like workflow | Easy speedup for many matrix ops | Reduced mantissa precision |
| **FP16** | Common inference/training mixed precision | Fast, compact | Limited dynamic range |
| **BF16** | Mixed precision with better range | Safer than FP16 for many models | Lower mantissa precision than FP32 |
| **INT8** | Quantized inference | Excellent throughput | Requires calibration/QAT validation |
| **FP8** | Hopper-era aggressive optimization | Very high throughput | Accuracy validation is critical |

#### **Rule of Thumb**

- Prefer **BF16** over FP16 when range issues show up
- Prefer **FP32 accumulation** when numerical stability matters
- Use **INT8/FP8** only after validating model quality and calibration

---

### **7.3 WMMA vs CUTLASS vs cuBLAS**

#### **Use This Order**

1. **cuBLAS / cuBLASLt** for standard GEMM
2. **CUTLASS** for custom layouts, fusion, or learning how high-performance kernels are built
3. **WMMA API** for educational or very specialized custom kernels

#### **Interview Line**

The right production answer is usually not "I hand-wrote GEMM." It is "I used the best available library first, then went custom only where fusion or data layout made it worth it."

---

### **7.4 Requirements to Hit Tensor Cores**

To use tensor cores effectively, you typically need:
- supported data type
- compatible matrix dimensions and alignment
- layout/library configuration that maps to tensor core instructions

If you do not meet those conditions, the code may silently fall back to slower execution paths.

---

### **7.5 Mixed Precision in Interviews**

#### **Good Answer Pattern**

1. Start with FP32 baseline
2. Move to FP16 or BF16
3. Validate accuracy and numerics
4. Inspect throughput and memory savings
5. Consider INT8 or FP8 only when the model and business tolerance allow it

#### **Project Connection**

- **CapitalOne:** FP16-friendly fraud scoring path paired with CUDA Graphs and micro-batching
- **Fiserv:** H100-era discussion point for FP8 and transformer-oriented acceleration after quality validation

---

### **7.6 Tensor Core Interview Traps**

1. "Tensor cores always make things faster"
   - False. Memory bottlenecks can dominate.
2. "FP16 is always safe"
   - False. Range issues can break numerics.
3. "Using a tensor-core GPU means my kernel hits tensor cores"
   - False. Layout and implementation must map correctly.
4. "INT8 is free speed"
   - False. Calibration and accuracy risk are real.

---

### **7.7 Quick Check: Tensor Cores**

**Practice Questions:**
1. What is the difference between CUDA cores and tensor cores?
2. When would you choose BF16 over FP16?
3. Why might a tensor-core-capable GPU still not use tensor cores?
4. When does INT8 make sense for inference?
5. Why should cuBLAS/cuBLASLt usually come before custom GEMM?

---

## Module 8: Production GPU Patterns for Inference Systems

### **8.1 Low-Latency Inference Design Pattern**

For low-latency inference, the fast path usually looks like this:

1. Preallocate pinned host buffers
2. Preallocate device buffers
3. Keep weights resident on the GPU
4. Reuse streams instead of creating them per request
5. Capture steady pipelines with CUDA Graphs
6. Use bounded micro-batching
7. Avoid host-side serialization and unnecessary copies

#### **CapitalOne Pattern**

- per-worker pinned buffers
- dedicated CUDA stream
- pre-instantiated graph
- micro-batch scoring
- about **10x throughput improvement** from **1K -> 10K req/sec/GPU**

---

### **8.2 Throughput vs Latency Trade-off**

#### **Micro-Batching**

Good for:
- small models
- many tiny requests
- tight p99 targets

Typical strategy:
- batch up to a small cap
- wait only a few milliseconds
- keep buffers preallocated

#### **Dynamic Batching**

Good for:
- throughput-heavy systems
- variable request arrival
- offline or near-real-time scoring

#### **Interview Line**

The right batching strategy depends on SLA class. Do not use a throughput-maximizing batch policy for a 5 ms interactive lane.

---

### **8.3 KV-Cache and GPU Memory Pressure**

LLM systems add a new GPU memory problem: large, long-lived KV-cache allocations.

#### **Common Problems**

- fragmentation from variable sequence lengths
- memory pressure spikes during bursts
- poor cache locality when sessions move between pods
- low cache hit rate if routing is unaware of session affinity

#### **Good Patterns**

- paged or block-based allocation
- memory pooling
- session stickiness
- headroom-aware scheduling
- eviction policy and telemetry

#### **Project Connection: Fiserv**

- session-aware routing for KV-cache affinity
- **70% KV-cache hit rate**
- **50% reduction in GPU memory pressure**

#### **Project Connection: CapitalOne**

- optimized KV layout and memory management for neural scoring flows
- improved cache reuse and lower fragmentation under bursty load

---

### **8.4 GPU Memory Pools and Fragmentation Control**

Per-request allocation is one of the easiest ways to create instability in GPU serving systems.

#### **Good Patterns**

- Preallocate long-lived buffers
- Use block allocators or slab-style pools for predictable shapes
- Reuse staging buffers for H2D/D2H
- Consider `cudaMallocAsync` and CUDA memory pools where they fit the runtime model
- Separate short-lived scratch buffers from long-lived caches

#### **Why This Matters**

- lower allocator overhead
- fewer synchronization surprises
- reduced fragmentation
- more stable tail latency

#### **Interview Line**

For production inference, memory management is often a bigger win than another 5% kernel speedup. A fast kernel does not help if the allocator path jitters or fragments under load.

---

### **8.5 Routing with GPU Headroom Awareness**

Production routing should consider more than queue length:

- free HBM / memory headroom
- inflight requests
- estimated batch fit
- stream availability
- latency budget class
- recent error rate / health
- cache affinity / session locality

This is where systems engineering meets GPU optimization.

---

### **8.6 Isolation, Governance, and Reliability**

GPU optimization in production is not only about speed:

- protect premium or interactive traffic
- isolate noisy neighbors
- avoid OOM storms
- expose GPU health via DCGM
- trigger circuit breakers on unhealthy replicas
- use canaries for kernel/library upgrades

#### **Project Connection**

**CapitalOne**
- premium-lane isolation and per-tier pools
- DCGM-based health monitoring

**Fiserv**
- health-aware failover
- bounded queues
- GPU-headroom scheduling

---

### **8.7 Production Metrics That Matter**

| Metric | Why It Matters |
|--------|----------------|
| **GPU utilization** | Tells you whether the device is busy enough, but only with context |
| **SM occupancy / eligible warps** | Shows whether the scheduler has useful work ready |
| **HBM used / free headroom** | Predicts OOM risk and batch fit |
| **Memcpy time and overlap ratio** | Reveals data movement bottlenecks |
| **Kernel launch count** | Helps identify graph-capture candidates |
| **Batch fill ratio** | Shows whether batching policy is working |
| **Queue wait time** | Critical for p99 behavior |
| **KV-cache hit rate / eviction rate** | Essential for LLM serving efficiency |
| **Graph cache hit rate** | Tells you whether graph amortization is real |
| **Error rate / Xid / ECC health** | Distinguishes software tuning issues from hardware health issues |

---

### **8.8 Production Debugging Checklist**

When p99 worsens, ask:

1. Is GPU utilization low because the device is idle, or because the workload is serialized?
2. Did a library or kernel change increase register pressure?
3. Are H2D copies no longer overlapping?
4. Did a new request shape break graph reuse?
5. Is KV-cache fragmentation reducing usable memory?
6. Did batching wait times drift upward under light load?
7. Did a tenant or queue class start starving another?

---

### **8.9 Quick Check: Production Patterns**

**Practice Questions:**
1. How would you build a sub-5 ms GPU scoring path?
2. How do you choose between micro-batching and dynamic batching?
3. What metrics would you use for GPU-aware routing?
4. Why is session stickiness valuable for KV-cache?
5. How do you protect interactive traffic from batch jobs on shared GPU infrastructure?

---

## Module 9: Hands-On Labs

### **Lab 1: Coalesced vs Non-Coalesced Memory Access**

**Objective:** See how memory access pattern changes throughput.

**Suggested Starting Point:**
- Use `cuda-lab` page copy kernels and benchmark harness

**Steps:**
1. Run the naive kernel
2. Run the coalesced kernel
3. Compare bandwidth and runtime
4. Profile both with Nsight Systems and Nsight Compute
5. Explain the speedup in terms of memory transactions

**Expected Result:**
- Large speedup for coalesced/vectorized variants
- Lower L2 and DRAM transaction overhead

---

### **Lab 2: Tiled Matrix Multiplication**

**Objective:** Compare naive GEMM with shared-memory tiled GEMM.

**Steps:**
1. Implement naive matrix multiplication
2. Implement tiled version using shared memory
3. Tune tile size (`16x16`, `32x8`, `32x32` as appropriate)
4. Measure throughput and occupancy
5. Compare with cuBLAS baseline

**Expected Learning:**
- Why arithmetic intensity changes performance
- Why shared memory and register use interact with occupancy

---

### **Lab 3: Parallel Reduction with Warp Shuffles**

**Objective:** Build a reduction kernel and remove unnecessary atomics/shared-memory traffic.

**Steps:**
1. Start with atomic-based baseline
2. Add block-local shared-memory reduction
3. Replace intra-warp shared-memory reduction with `__shfl_down_sync`
4. Measure runtime and atomics count
5. Explain the final structure

**Expected Learning:**
- Hierarchical reduction pattern
- Why warp primitives often beat shared memory for the last mile

---

### **Lab 4: H2D -> Compute -> D2H Overlap**

**Objective:** Build a stream pipeline with pinned memory and events.

**Steps:**
1. Allocate pageable host memory and benchmark
2. Switch to pinned memory
3. Use `cudaMemcpyAsync`
4. Add two or more streams
5. Add event-based synchronization and double buffering
6. Compare throughput and GPU utilization

**Expected Learning:**
- Why pinned memory is required for real overlap
- How copy engines and compute can run concurrently

---

### **Lab 5: CUDA Graph Capture**

**Objective:** Reduce launch overhead in a multi-kernel pipeline.

**Steps:**
1. Build a pipeline with H2D, several small kernels, and D2H
2. Benchmark without graphs
3. Capture with CUDA Graphs
4. Instantiate and replay
5. Measure CPU overhead and end-to-end latency

**Expected Learning:**
- Where graphs help
- Why fixed-shape micro-batch pipelines are ideal

---

### **Lab 6: Tensor Core GEMM**

**Objective:** Compare regular FP32 GEMM against tensor-core-accelerated GEMM.

**Steps:**
1. Build FP32 baseline
2. Try cuBLAS or cuBLASLt mixed-precision path
3. If you want deeper learning, implement a WMMA-based version
4. Measure throughput, memory footprint, and numerical error
5. Explain the trade-off between speed and accuracy

**Expected Learning:**
- How tensor cores change throughput
- Why accumulation type and validation matter

---

### **Lab 7: Full Profiling Workflow**

**Objective:** Practice the real profiling loop used in production.

**Steps:**
1. Start with end-to-end benchmark
2. Use Nsight Systems for timeline analysis
3. Use Nsight Compute for slow kernels
4. Make one optimization change
5. Re-benchmark and document the win or regression

**Suggested Local References:**
- `cuda-lab/docs/NSIGHT_SYSTEMS_GUIDE.md`
- `cuda-lab/docs/NSIGHT_COMPUTE_GUIDE.md`

---

## Interview Question Bank

### **Architecture Fundamentals**

1. Explain how an SM schedules work.
2. What is a warp, and why does it matter?
3. What causes warp divergence?
4. Explain the GPU memory hierarchy in order of speed and size.
5. What is occupancy, and why is it not the same as utilization?
6. Why can a lower-occupancy kernel be faster?

### **CUDA Programming Model**

7. How do you choose block and grid dimensions?
8. What is the difference between a block-wide barrier and a memory fence?
9. Why can threads in different blocks not directly synchronize?
10. When do you use warp shuffles?
11. What are common kernel correctness bugs?

### **Memory Optimization**

12. What is coalesced memory access?
13. Why is SoA often better than AoS on GPUs?
14. When is shared memory worth using?
15. What is a bank conflict?
16. What causes register spills?
17. When should you use pinned memory?
18. When would you avoid zero-copy mapped host memory?
19. What are the trade-offs of unified memory?

### **Profiling and Performance**

20. How do you determine whether a kernel is memory-bound or compute-bound?
21. When do you use Nsight Systems vs Nsight Compute?
22. How do you measure kernel latency correctly?
23. What metrics do you inspect first for a slow memory-bound kernel?
24. How do you benchmark fairly?

### **Streams and Concurrency**

25. What conditions are required for copy/compute overlap?
26. Why might multiple streams not improve throughput?
27. What are stream priorities good for?
28. How do you reason about PCIe vs NVLink bottlenecks?
29. How would you design a multi-GPU inference pipeline?

### **Advanced CUDA**

30. What are CUDA Graphs? When are they useful?
31. What are the trade-offs of kernel fusion?
32. How do warp-level reductions work?
33. What is a persistent kernel?
34. What do `cp.async` and Hopper TMA buy you?

### **Tensor Cores and Mixed Precision**

35. How do tensor cores differ from CUDA cores?
36. When would you use FP16 vs BF16?
37. When does INT8 make sense?
38. What are the risks of FP8?
39. Why should you start with cuBLAS or cuBLASLt before custom WMMA?

### **Production Inference Systems**

40. How would you design a sub-5 ms GPU scoring path?
41. How do you choose between micro-batching and dynamic batching?
42. How do you keep GPU utilization high without violating p99 latency?
43. How do you manage KV-cache pressure on H100-class inference pods?
44. What metrics should drive GPU-aware request routing?
45. How do you isolate premium traffic from batch workloads?
46. How do you debug sudden p99 regression after a CUDA upgrade?

---

## Story Bank: Project-Backed Answers

### **Story 1: CapitalOne - CUDA Graphs for Sub-5 ms Fraud Scoring**

**Problem:**
- Many small GPU kernels in the neural scoring path
- Launch overhead consumed too much of the latency budget

**Approach:**
- Preallocated pinned host buffers and device buffers per worker
- Dedicated CUDA stream
- Captured H2D -> model forward -> D2H as a CUDA Graph
- Replayed the graph per micro-batch

**Result:**
- Launch overhead reduced from about **150 us** to about **5 us**
- Saved about **145 us** per inference
- Supported **3.8 ms p99** in the real-time lane

**Best Interview Use:**
- CUDA Graphs
- low-latency inference
- why orchestration overhead matters

---

### **Story 2: CapitalOne - Micro-Batching with Pinned Buffers**

**Problem:**
- Need to improve throughput without breaking the real-time SLA

**Approach:**
- Preallocated host/device buffers
- Dedicated streams per worker
- Small bounded micro-batches
- Reused graph-executed pipeline

**Result:**
- **10x throughput improvement** from **1K -> 10K req/sec/GPU**
- High utilization without losing interactive latency discipline

**Best Interview Use:**
- throughput vs latency trade-off
- pinned memory
- streams
- batching policy design

---

### **Story 3: Fiserv - Pinned Memory and Async DMA**

**Problem:**
- Large-context inference incurred heavy H2D overhead
- GPU utilization was too low because transfers and compute were not well pipelined

**Approach:**
- Implemented a C++/CUDA transfer engine
- Used pinned host memory, `cudaMemcpyAsync`, streams, and event-based completion
- Reused buffers instead of allocating per request

**Result:**
- **3.2x faster H2D transfers**
- **85% GPU utilization**

**Best Interview Use:**
- memory optimization
- async overlap
- why pinned memory matters

---

### **Story 4: Fiserv - Session-Aware KV-Cache Routing**

**Problem:**
- Recomputing KV-cache across pods increased latency and memory pressure

**Approach:**
- Sticky routing based on session affinity
- KV-aware placement and GPU-headroom scoring
- Cache reuse and memory pressure tracking

**Result:**
- **70% KV-cache hit rate**
- **50% reduction in GPU memory pressure**

**Best Interview Use:**
- inference-specific GPU memory management
- routing and scheduling
- system design beyond the kernel

---

### **Story 5: Broadcom - GPU Throughput and Tenant Isolation**

**Problem:**
- Needed high throughput across mixed tenants without one workload starving another

**Approach:**
- Dynamic batching for GPU-friendly work
- Tenant-aware scheduling and queueing
- Hardware-aware routing across CPU/GPU options

**Result:**
- Strong GPU throughput gains on T4/V100-class hardware
- Better fairness and operational stability in shared infrastructure

**Best Interview Use:**
- multi-tenant GPU scheduling
- batching
- throughput-oriented platform design

---

### **Story 6: Profiling Methodology**

**Problem:**
- Performance regressions were easy to misdiagnose by intuition alone

**Approach:**
- Start with end-to-end benchmark
- Use Nsight Systems to find gaps and serialization
- Use Nsight Compute for kernel-level bottlenecks
- Change one variable at a time

**Result:**
- Faster root cause analysis
- Better confidence in optimization decisions
- Strong, credible interview narratives instead of vague claims

**Best Interview Use:**
- any question that asks "How do you optimize?"
- showing engineering maturity

---

## Resource Library

### **Books**

1. **Programming Massively Parallel Processors** - Hwu, Kirk, El Hajj
2. **CUDA by Example** - Sanders, Kandrot
3. **Computer Architecture: A Quantitative Approach** - Hennessy, Patterson
4. **Systems Performance** - Brendan Gregg

### **NVIDIA Documentation**

1. CUDA C Programming Guide
2. CUDA Best Practices Guide
3. CUDA Graphs documentation
4. Nsight Systems documentation
5. Nsight Compute documentation
6. cuBLAS / cuBLASLt documentation
7. CUTLASS documentation
8. TensorRT and TensorRT-LLM documentation

### **Courses and Talks**

1. NVIDIA DLI: CUDA C/C++ Programming
2. NVIDIA GTC talks on CUDA optimization
3. GTC talks on tensor cores and Hopper architecture
4. Talks on CUTLASS and high-performance GEMM

### **Papers and Technical References**

1. CUTLASS papers and design docs
2. FlashAttention papers for IO-aware attention design
3. vLLM paper for paged KV-cache management
4. Hopper and Ampere architecture whitepapers

### **Local Repo References**

1. `docs/CAPITALONE_TRANSACTION_PROCESSING_STORY.md`
2. `docs/FISERV_LOAN_DIRECTOR_STORY.md`
3. `docs/INTERVIEW_STUDY_GUIDE.md`
4. `cuda-lab/docs/NSIGHT_SYSTEMS_GUIDE.md`
5. `cuda-lab/docs/NSIGHT_COMPUTE_GUIDE.md`
6. `cuda-lab/docs/PYTORCH_EXTENSION_GUIDE.md`

### **Tools**

1. `nvidia-smi`
2. NVIDIA DCGM
3. Nsight Systems
4. Nsight Compute
5. `nvcc`
6. `cuobjdump`
7. `nvdisasm`
8. `perf` for CPU-side launch path analysis

---

## Progress Tracker

### **Weekly Plan**

| Week | Focus | Hours | Completion | Notes |
|------|-------|-------|------------|-------|
| 1 | Module 1-2: Architecture + Programming Model | 8 | [ ] | |
| 2 | Module 3: Memory Hierarchy + Data Movement | 8 | [ ] | |
| 3 | Module 4: Profiling + Occupancy | 8 | [ ] | |
| 4 | Module 5-6: Streams + Advanced CUDA | 10 | [ ] | |
| 5 | Module 7: Tensor Cores + Mixed Precision | 8 | [ ] | |
| 6 | Module 8-9: Production Patterns + Labs | 10 | [ ] | |

### **Lab Tracker**

| Lab | Status | Date Completed | Notes |
|-----|--------|----------------|-------|
| Lab 1: Coalescing | [ ] | | |
| Lab 2: Tiled GEMM | [ ] | | |
| Lab 3: Reduction | [ ] | | |
| Lab 4: Stream Overlap | [ ] | | |
| Lab 5: CUDA Graphs | [ ] | | |
| Lab 6: Tensor Core GEMM | [ ] | | |
| Lab 7: Full Profiling Workflow | [ ] | | |

### **Confidence Self-Assessment**

| Topic | Before (1-10) | After (1-10) | Improvement |
|-------|---------------|--------------|-------------|
| GPU Architecture | | | |
| CUDA Programming Model | | | |
| Memory Optimization | | | |
| Occupancy & Profiling | | | |
| Streams & Concurrency | | | |
| CUDA Graphs & Advanced Optimization | | | |
| Tensor Cores & Mixed Precision | | | |
| Production GPU Inference Design | | | |

### **Mock Interview Checklist**

- [ ] Explain the GPU memory hierarchy from memory
- [ ] Walk through coalesced vs non-coalesced access on a whiteboard
- [ ] Explain occupancy without oversimplifying it
- [ ] Describe when pinned memory matters
- [ ] Explain CUDA Graphs and when they help
- [ ] Compare FP16, BF16, INT8, and FP8
- [ ] Tell one CapitalOne GPU story end-to-end
- [ ] Tell one Fiserv GPU story end-to-end
- [ ] Explain a profiling workflow using Nsight Systems and Nsight Compute
- [ ] Design a GPU-aware inference router with batching and isolation

---

**Final Reminder:** The goal is not memorizing every microarchitectural detail. The goal is to be able to reason about GPU performance, debug bottlenecks methodically, and connect the concepts to your real production stories.

**Interview North Star:** If you can explain where the time goes, where the bytes move, what the scheduler sees, and what trade-off you chose, you are already answering at a strong Staff/Principal level.
