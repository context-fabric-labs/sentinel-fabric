
# Layer 4: GPU & Accelerator — Complete Deep Dive
## "How does the GPU execute our workloads?"

> **Why this layer matters:** GPUs are the compute engine behind every AI/HPC
> workload — transformer inference, vector search, batched ranking, distributed
> training. But a GPU is NOT a fast CPU. It has a fundamentally different
> architecture optimized for massive parallelism, and understanding that
> architecture is what separates "I use GPUs" from "I optimize GPU workloads."
>
> **The core insight:** Most GPU performance problems are NOT about the GPU
> being slow. They are about the GPU being IDLE — waiting for data, waiting
> for kernel launches, waiting for synchronization. A strong systems engineer
> keeps the GPU FED and BUSY.

---

# SECTION 1: GPU ARCHITECTURE FUNDAMENTALS

---

## 1.1 SM (Streaming Multiprocessor) — The Building Block

### What an SM is
An SM is the fundamental compute unit of a GPU. A modern GPU (like H100)
has ~132 SMs. Each SM can run thousands of threads simultaneously.

```
CPU Core vs GPU SM:

CPU Core:                          GPU SM:
  1-2 threads at full speed          Up to 2,048 resident threads
  Deep pipeline (20+ stages)         Shallow pipeline
  Large caches (32 KB L1, 1 MB L2)   Small caches (shared with threads)
  Branch prediction                  No branch prediction
  Out-of-order execution             In-order execution
  Optimized for LATENCY              Optimized for THROUGHPUT

CPU: "Do one thing FAST"
GPU: "Do many things AT ONCE"
```

### What an SM contains (H100 example)
```
One SM:
  ├── 128 CUDA cores (FP32)
  ├── 64 FP64 cores
  ├── 4 Tensor Cores (4th gen)
  ├── 4 warp schedulers
  ├── 256 KB register file
  ├── 228 KB shared memory / L1 cache (configurable split)
  ├── 16 load/store units
  └── 4 special function units (sin, cos, exp, etc.)

Full H100 GPU: 132 SMs × above = massive parallelism
```

---

## 1.2 Warps and SIMT Execution

### What a warp is
A warp is a group of **32 threads** that execute the SAME instruction
at the SAME time (SIMT = Single Instruction, Multiple Threads).

```
Warp of 32 threads:
  Thread 0:  execute ADD instruction on data[0]
  Thread 1:  execute ADD instruction on data[1]
  Thread 2:  execute ADD instruction on data[2]
  ...
  Thread 31: execute ADD instruction on data[31]

All 32 threads execute ADD simultaneously.
One instruction, 32 data elements processed.
```

### Warp divergence — why if/else kills GPU throughput
When threads in a warp take DIFFERENT branches, the warp must execute
BOTH paths sequentially (with threads masked out for the path they
don't take):

```
if (threadIdx.x < 16) {
    // Path A: only threads 0-15 execute
    result = expensive_function_A(data);
} else {
    // Path B: only threads 16-31 execute
    result = expensive_function_B(data);
}

Execution:
  Step 1: threads 0-15 execute Path A, threads 16-31 IDLE (masked)
  Step 2: threads 16-31 execute Path B, threads 0-15 IDLE (masked)
  Total: 2× the time of a non-divergent warp

Worst case: 32 different paths → 32× serialization within the warp
```

### Why this matters for inference
Tree-based models (XGBoost) on GPU suffer from divergence because
different data points take different tree branches. This is why
XGBoost on CPU is often faster than GPU for single-request inference.

---

## 1.3 Memory Hierarchy

```
FASTEST                                              SLOWEST
  ←────────────────────────────────────────────────────→

Registers    Shared Mem/L1    L2 Cache       HBM (Global Memory)
~0 cycles    ~20-30 cycles    ~200 cycles    ~400-600 cycles
256 KB/SM    228 KB/SM        50 MB          80 GB (H100)
per-thread   per-block        per-GPU        per-GPU
             user-managed     hardware-managed
```

### Why HBM bandwidth matters for LLM inference
LLM decode is **memory-bandwidth-bound**, not compute-bound:

```
Decode step for 7B model:
  Each token generation reads ALL model weights from HBM
  7B params × 2 bytes (FP16) = 14 GB per token
  H100 HBM bandwidth: 3.35 TB/s
  Max tokens/sec: 3,350 GB/s ÷ 14 GB = ~239 tokens/sec (theoretical)

  The GPU has plenty of COMPUTE for this — Tensor Cores are mostly idle
  The bottleneck is reading 14 GB from HBM for every single token
```

This is why:
- KV caching exists (don't re-read past token weights)
- quantization helps (FP8 = 7 GB instead of 14 GB per token = 2× speed)
- batching helps (amortize weight reads across multiple sequences)

---

## 1.4 Coalesced Memory Access

### What coalescing is
When threads in a warp access CONTIGUOUS memory addresses, the GPU
combines them into a few large memory transactions. When they access
SCATTERED addresses, each thread needs a separate transaction.

```
COALESCED (good):
  Thread 0 reads address 0x1000
  Thread 1 reads address 0x1004
  Thread 2 reads address 0x1008
  ...
  Thread 31 reads address 0x107C
  → ONE 128-byte memory transaction (all 32 reads served at once)

SCATTERED (bad):
  Thread 0 reads address 0x1000
  Thread 1 reads address 0x5000
  Thread 2 reads address 0x9000
  ...
  Thread 31 reads address 0x7C000
  → UP TO 32 separate memory transactions
  → 10-30x slower than coalesced!
```

### Practical impact
Array-of-Structures (AoS) vs Structure-of-Arrays (SoA):
```
AoS (bad for GPU): struct { float x, y, z; } particles[N];
  Thread i reads particles[i].x → scattered across structs
  
SoA (good for GPU): float x[N], y[N], z[N];
  Thread i reads x[i] → contiguous, coalesced!
```

---

## 1.5 Tensor Cores

### What they do
Tensor Cores perform **matrix multiply-accumulate** operations on small
matrices (e.g., 16×16) in a single clock cycle:

```
D = A × B + C
where A, B, C, D are small matrices (e.g., 16×16)

One Tensor Core operation:
  16 × 16 × 16 = 4,096 multiply-accumulate operations
  in ONE clock cycle

vs CUDA cores:
  4,096 operations would take many cycles
```

### Precision modes
```
H100 Tensor Core throughput:
  FP64:    67 TFLOPS
  TF32:    989 TFLOPS
  FP16:    1,979 TFLOPS
  BF16:    1,979 TFLOPS
  FP8:     3,958 TFLOPS
  INT8:    3,958 TFLOPS

FP8 is 59× faster than FP64 on Tensor Cores!
```

### Why mixed precision matters for inference
```
Model weights in FP16 + compute in FP16 + accumulate in FP32:
  → Tensor Cores do the heavy lifting
  → 2× less HBM bandwidth (FP16 vs FP32)
  → Near-identical model quality
  → This is the DEFAULT for modern inference
```

---

## 1.6 Occupancy

### What it is
Occupancy = (active warps per SM) / (maximum warps per SM)

The GPU hides memory latency by switching between warps. When one warp
is waiting for data from HBM, another warp executes. Higher occupancy
= more warps to switch between = better latency hiding.

### What limits occupancy
```
Each SM has fixed resources shared among all resident warps:
  - Registers: 256 KB per SM
  - Shared memory: 228 KB per SM
  - Max threads: 2,048 per SM (= 64 warps)

If your kernel uses too many registers per thread:
  Fewer threads fit → fewer warps → lower occupancy

If your kernel uses too much shared memory per block:
  Fewer blocks fit → fewer warps → lower occupancy
```

### Important nuance
Max occupancy ≠ max performance. Sometimes using MORE registers per
thread (lower occupancy) gives BETTER performance because the kernel
does more work per thread without spilling to slow memory.

---

# SECTION 2: CUDA PROGRAMMING PATTERNS

---

## 2.1 CUDA Streams (Concurrent Execution)

### What a stream is
A CUDA stream is an ordered queue of GPU operations. Operations in the
SAME stream execute in order. Operations in DIFFERENT streams can
execute concurrently.

```
ONE stream (sequential):
  [H2D copy] → [kernel A] → [kernel B] → [D2H copy]
  Everything waits for the previous step.

TWO streams (concurrent):
  Stream 1: [H2D copy 1] → [kernel A] → [D2H copy 1]
  Stream 2:    [H2D copy 2] → [kernel B] → [D2H copy 2]
  Overlapping! GPU copy engine + compute engine work simultaneously.
```

### Your pipeline uses streams for parallel NLU models
```
Stream 1: [intent_classifier: 8 ms]
Stream 2: [query_embedding: 6 ms][gpu_search: 0.7 ms][ranking: 0.8 ms]
CPU:      [BM25 search: 8 ms]

Wall clock: max(8, 7.5, 8) = ~8 ms (not 22 ms sequential!)
```

---

## 2.2 CUDA Graphs (Eliminate Launch Overhead)

### The problem
Each kernel launch has CPU-side overhead (~3-5 µs):
```
30 kernels × 4 µs = 120 µs of pure dispatch overhead
For a 5 ms inference, that's 2.4% wasted on dispatch
For micro-batched decode steps, it can be 10-20% waste
```

### The solution
Capture the entire execution sequence once, replay it without
re-encoding:
```
Capture (once at warmup):
  [encode kernel 1] [encode kernel 2] ... [encode kernel 30] → save graph

Replay (every inference):
  [launch graph] → all 30 kernels execute as ONE submission
  CPU dispatch: 5 µs total (not 120 µs)
```

### When CUDA Graphs work best
- Fixed input shapes (or pad to a few standard sizes)
- Repeated execution pattern (same kernels every time)
- Many small kernels (where launch overhead dominates)

### When they DON'T work
- Dynamic shapes that change every request
- Conditional execution (different kernels depending on input)
- Operations that allocate memory dynamically

---

## 2.3 Kernel Fusion

### The problem
Unfused operations read/write HBM between each step:
```
Unfused (3 kernels, 6 HBM accesses):
  kernel 1: read input from HBM → compute residual_add → write to HBM
  kernel 2: read from HBM → compute RMSNorm → write to HBM
  kernel 3: read from HBM → compute quantize → write to HBM

Fused (1 kernel, 2 HBM accesses):
  kernel: read input from HBM → residual_add → RMSNorm → quantize → write to HBM
  
  Savings: 4 HBM accesses eliminated = potentially 2-3x faster
```

### Common fusions in LLM/transformer inference
```
- RMSNorm + residual add
- RMSNorm + quantize
- SiLU + multiply + quantize
- QKV projection (3 GEMMs → 1 fused GEMM)
- Attention + output projection
- RoPE + KV cache write
```

---

## 2.4 Pinned Memory

### Why pinned memory matters
```
Pageable memory (default):
  cudaMemcpy: driver copies from pageable → internal pinned buffer → DMA to GPU
  = TWO copies on the host side

Pinned memory (cudaMallocHost):
  cudaMemcpy: DMA directly from pinned buffer → GPU
  = ONE copy (the DMA itself)
  
  Also required for: cudaMemcpyAsync (true async, non-blocking)
  Also required for: CUDA Graph capture with H2D/D2H
```

---

# SECTION 3: PROFILING WITH NSIGHT

---

## 3.1 Nsight Systems (Timeline Analysis)

### What it shows
A timeline of ALL CPU and GPU activity: kernel launches, memory transfers,
API calls, stream activity, GPU utilization, CPU thread activity.

### What to look for
```
1. GPU idle gaps between kernels → launch overhead → CUDA Graphs
2. Sequential kernels on one stream → can they be parallelized on multiple streams?
3. H2D/D2H not overlapping with compute → use async + multiple streams
4. Long CPU sections between GPU work → CPU is the bottleneck
5. Small kernels with large gaps → kernel fusion opportunity
```

### Commands
```bash
# Profile a running service
nsys profile --trace=cuda,osrt,nvtx --gpu-metrics-device=0 \
    -o my_profile ./my_service

# View in Nsight Systems GUI
nsys-ui my_profile.nsys-rep
```

---

## 3.2 Nsight Compute (Per-Kernel Analysis)

### What it shows
Detailed metrics for a SINGLE kernel: occupancy, memory throughput,
compute throughput, warp stall reasons, register usage, shared memory usage.

### What to look for
```
1. Low occupancy → too many registers or too much shared memory
2. Memory-bound → high HBM utilization, low compute utilization
3. Compute-bound → high compute utilization, low memory utilization
4. Warp stalls → what are warps waiting for? (memory, execution, sync)
5. Non-coalesced access → memory efficiency metric is low
```

### Commands
```bash
# Profile specific kernel
ncu --target-processes all --kernel-name "my_kernel" ./my_service

# Full analysis
ncu --set full ./my_service
```

---

# SECTION 4: GPU WORKLOADS IN YOUR PIPELINE

## How GPU is used across your stories

| Workload | GPU operation | Key optimization |
|---|---|---|
| Intent classifier | BERT forward pass (GEMMs + attention) | CUDA Graphs, micro-batching |
| Entity extraction | Same BERT forward pass (multi-task) | Shared backbone (1 pass, 3 heads) |
| Query embedding | Same BERT forward pass (mean pooling) | Output stays on GPU for FAISS |
| Vector search | FAISS GPU dot products | Zero D2H (embedding → search on GPU) |
| Batched ranking | MLP forward pass (ONNX Runtime CUDA) | Batch all candidates in one pass |
| LLM inference | Transformer decode (TRT-LLM/vLLM) | Prefix caching, KV quantization |

---

# SECTION 5: INTERVIEW CHEAT SHEET

## "Why is LLM decode memory-bound?"
→ Each token reads ALL model weights from HBM. Compute is fast (Tensor Cores),
   but reading 14 GB per token at 3.35 TB/s limits throughput.

## "What is warp divergence?"
→ When threads in a 32-thread warp take different branches, the warp
   serializes both paths. 50% divergence = 2x slower.

## "How do CUDA Graphs help?"
→ Capture the kernel launch sequence once, replay as single GPU submission.
   Eliminates per-kernel CPU dispatch overhead (~3-5 µs × N kernels).

## "What is coalesced memory access?"
→ Adjacent threads accessing adjacent memory addresses. GPU combines
   into fewer large transactions. Scattered access = 10-30x slower.

## "How do you profile GPU workloads?"
→ Nsight Systems for timeline (find gaps, parallelism, overlap).
   Nsight Compute for per-kernel analysis (occupancy, memory, compute).
