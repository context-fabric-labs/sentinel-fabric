
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

---
---

# SECTION 6: GPU MEMORY MANAGEMENT (Critical for Production)

---

## 6.1 GPU Memory Layout

```
GPU Memory (80 GB on H100):

┌─────────────────────────────────────────────────────────────────┐
│                         HBM3 (80 GB)                            │
├─────────────────────────────────────────────────────────────────┤
│ Model Weights    │ KV Cache     │ Activations  │ Workspace/Misc │
│ (fixed)          │ (grows)      │ (per-batch)  │ (temp)         │
│                  │              │              │                │
│ 7B FP16 = 14 GB │ depends on   │ depends on   │ CUDA alloc     │
│ 70B FP16= 140 GB│ seq_len ×    │ batch_size × │ scratch space  │
│                  │ batch_size × │ hidden_dim × │                │
│                  │ num_layers × │ num_layers   │                │
│                  │ 2 × head_dim │              │                │
└─────────────────────────────────────────────────────────────────┘
```

## 6.2 KV Cache Memory Math

```
KV cache per token per layer:
  = 2 (K and V) × num_heads × head_dim × dtype_size

For Llama-70B (80 layers, 64 heads, head_dim=128, FP16):
  Per token per layer: 2 × 64 × 128 × 2 bytes = 32 KB
  Per token ALL layers: 32 KB × 80 = 2.56 MB
  
  For a sequence of 4096 tokens:
  KV cache per sequence: 2.56 MB × 4096 = 10.5 GB!
  
  With batch size 8:
  Total KV cache: 10.5 GB × 8 = 84 GB  ← EXCEEDS H100 80 GB!

This is why KV cache optimization is CRITICAL:
  - KV quantization (FP8): 50% reduction → 42 GB (fits!)
  - Grouped Query Attention (GQA): 8 KV heads instead of 64 → 8× reduction
  - Paged Attention: eliminates fragmentation waste
```

## 6.3 Paged Attention (vLLM's Key Innovation)

```
Problem without paging:
  Each request pre-allocates MAX sequence length of KV cache
  Request may only use 50% of allocated space → 50% WASTED
  Memory fragmentation prevents new requests from scheduling

  Example: 4 requests, each allocates 4096 tokens of KV cache
  But actual usage: [1024, 2048, 512, 3072] tokens
  Waste: (4096-1024) + (4096-2048) + (4096-512) + (4096-3072) = 9,536 tokens wasted
  = 57% memory wasted!

Paged Attention solution:
  Allocate KV cache in BLOCKS (like OS virtual memory pages)
  Block size: 16 tokens (configurable)
  
  Physical blocks allocated on-demand as tokens are generated:
  ┌──────┐ ┌──────┐ ┌──────┐
  │Blk 0 │→│Blk 1 │→│Blk 2 │  Request A (48 tokens used)
  └──────┘ └──────┘ └──────┘
  
  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐
  │Blk 3 │→│Blk 4 │→│Blk 5 │→│Blk 6 │  Request B (64 tokens)
  └──────┘ └──────┘ └──────┘ └──────┘
  
  Blocks can be anywhere in physical GPU memory (like pages in RAM)
  Block table maps logical → physical (like page table)
  
  Result: near-zero waste, 2-4× more concurrent requests!
```

## 6.4 CUDA Memory Allocator Behavior

```
PyTorch caching allocator:
  - Does NOT return memory to CUDA after free()
  - Keeps freed blocks in a cache for reuse
  - Reduces costly cudaMalloc/cudaFree calls
  - But: can appear to "leak" memory (nvidia-smi shows full, but it's cached)

Key environment variables:
  PYTORCH_CUDA_ALLOC_CONF=max_split_size_mb:512
    → prevents splitting large blocks (reduces fragmentation)
  
  PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True
    → allows allocator to grow segments (reduces OOM from fragmentation)
  
  PYTORCH_CUDA_ALLOC_CONF=garbage_collection_threshold:0.8
    → trigger GC when 80% of reserved memory is cached (not in use)

Diagnosing GPU memory issues:
  torch.cuda.memory_stats()  # detailed allocator statistics
  torch.cuda.memory_snapshot()  # dump full allocation map
  torch.cuda.reset_peak_memory_stats()  # reset peak tracking
```

## 6.5 Unified Memory (CUDA Managed Memory)

```
cudaMallocManaged: memory accessible from both CPU and GPU
  - Driver automatically migrates pages between CPU and GPU on access
  - Simplifies programming (no explicit H2D/D2H copies)
  - BUT: page faults on GPU are EXPENSIVE (~20-50 µs per fault)
  - NOT suitable for latency-sensitive inference
  
When to use:
  - Prototyping (ease of development)
  - When data is too large for GPU memory (oversubscription)
  - Exploratory data analysis (random access patterns)

When NOT to use:
  - Production inference (page faults kill latency)
  - Training (explicit copies with pinned memory are faster)
  - Anything latency-sensitive
```

---

# SECTION 7: QUANTIZATION DEEP DIVE

---

## 7.1 Numeric Precision Formats

```
Format   Bits  Range              Mantissa  Use Case
─────────────────────────────────────────────────────
FP32     32    ±3.4×10^38         23 bits   Training (master weights)
TF32     19    ±3.4×10^38         10 bits   Training compute (A100+)
BF16     16    ±3.4×10^38         7 bits    Training (same range as FP32)
FP16     16    ±65,504            10 bits   Inference (more precision)
FP8 E4M3 8     ±448               3 bits    Inference (weights)
FP8 E5M2 8     ±57,344            2 bits    Inference (activations/gradients)
INT8     8     -128 to 127        —         Inference (quantized)
INT4     4     -8 to 7            —         Inference (aggressive quant)

Memory savings per parameter:
  FP32: 4 bytes → FP16: 2 bytes (2× less memory, 2× more throughput)
  FP16: 2 bytes → INT8: 1 byte (4× less than FP32)
  FP16: 2 bytes → INT4: 0.5 bytes (8× less than FP32)
```

## 7.2 Quantization Methods

### Post-Training Quantization (PTQ)
```
Quantize a pre-trained model WITHOUT retraining:

1. Collect calibration data (small representative dataset)
2. Run forward passes to observe activation distributions
3. Compute scale/zero-point for each tensor:
   
   Symmetric: scale = max(|tensor|) / max_int_value
   Asymmetric: scale = (max - min) / (max_int - min_int)
               zero_point = round(-min / scale)
   
   quantized = round(float_value / scale) + zero_point
   dequantized = (quantized - zero_point) × scale

Pros: fast (minutes, not days), no training data needed
Cons: accuracy loss for aggressive quantization (INT4)
```

### Weight-Only Quantization (GPTQ, AWQ)
```
Only quantize WEIGHTS (keep activations in FP16):
  - Weights are static → can be quantized offline with care
  - Activations change per input → harder to quantize
  
GPTQ (GPT Quantization):
  - Layer-by-layer quantization with error compensation
  - Uses Hessian information to minimize quantization error
  - Quantizes to INT4 with grouping (group_size=128)
  - 3-4 bit with minimal accuracy loss
  
AWQ (Activation-Aware Weight Quantization):
  - Observes which weight channels matter most (via activations)
  - Protects important channels from aggressive quantization
  - Slightly better accuracy than GPTQ for same bit width

Result: 70B model at INT4 = ~35 GB (fits on single H100!)
  vs FP16 = 140 GB (needs 2× H100 with tensor parallelism)
```

### FP8 Quantization (H100 Native)
```
H100 Tensor Cores support FP8 natively:
  - E4M3 for weights (better precision for static values)
  - E5M2 for activations/gradients (larger range for dynamic values)
  
Benefits:
  - 2× throughput vs FP16 on same hardware
  - 2× less memory bandwidth consumed
  - Near-FP16 accuracy for most models
  - No special calibration needed (dynamic per-tensor scaling)

TensorRT-LLM FP8:
  - Automatic per-tensor scaling
  - Fused dequant → GEMM → quant operations
  - No accuracy calibration pass needed for most models
```

## 7.3 Quantization Impact on Inference Math

```
7B model decode throughput:

FP16 (14 GB weights):
  H100 bandwidth: 3,350 GB/s
  Max tokens/sec: 3,350 / 14 = 239 tok/s

INT8 (7 GB weights):
  Max tokens/sec: 3,350 / 7 = 478 tok/s (2× improvement!)

INT4 (3.5 GB weights):
  Max tokens/sec: 3,350 / 3.5 = 957 tok/s (4× improvement!)
  But: compute becomes the bottleneck before reaching this
  (INT4 dequantization + FP16 matmul overhead)

Practical INT4 (GPTQ with group_size=128):
  Actual throughput: ~600-700 tok/s (limited by dequant overhead)
  Still 2.5-3× better than FP16
```

---

# SECTION 8: LLM INFERENCE SERVING (vLLM / TensorRT-LLM)

---

## 8.1 Continuous Batching

```
Static batching (old approach):
  Wait for batch of N requests → process all → return all
  Problem: short requests wait for long requests to finish
  
  Request A: 10 tokens to generate (done in 50 ms)
  Request B: 200 tokens to generate (done in 1000 ms)
  
  With static batch: Request A waits 1000 ms (idle for 950 ms!)

Continuous batching (vLLM/TRT-LLM):
  Process iteration by iteration (one token per step per request)
  When a request finishes, immediately ADD a new request from queue
  
  Step 1: [A, B, C, D] → generate 1 token each
  Step 2: [A, B, C, D] → A finishes! → [E, B, C, D]
  Step 3: [E, B, C, D] → B finishes! → [E, F, C, D]
  
  Result: 
  - Request A returned immediately when done (not waiting for B)
  - GPU never idle (always has full batch)
  - 2-5× better throughput than static batching
```

## 8.2 Prefix Caching

```
Many requests share common prefixes (system prompts):

Request 1: "You are a helpful assistant. [user query 1]"
Request 2: "You are a helpful assistant. [user query 2]"

Without prefix caching:
  Request 1: compute KV cache for entire prefix + query
  Request 2: compute KV cache for entire prefix + query (DUPLICATE!)

With prefix caching:
  First request: compute KV cache for prefix → CACHE IT
  Subsequent requests: reuse cached prefix KV cache → only compute query part

  If prefix = 2048 tokens, query = 100 tokens:
  Speedup for prefill: (2048+100) / 100 = 21× faster!
  
  vLLM: automatic prefix caching with hash-based block lookup
  TRT-LLM: explicit prefix caching via session API
```

## 8.3 Speculative Decoding

```
Problem: autoregressive decoding generates 1 token at a time
  Each step reads ALL model weights from HBM (memory-bound)
  Can't generate faster than HBM bandwidth allows

Speculative decoding:
  1. Small "draft" model generates K candidate tokens quickly
     (e.g., 7B draft model generates 5 tokens in ~5 ms)
  2. Large "target" model VERIFIES all K tokens in ONE forward pass
     (70B model processes 5 tokens simultaneously)
  3. Accept all tokens up to first rejection, discard the rest
  4. If all K accepted: got K tokens for cost of 1 large forward pass!

Why it works:
  - Verification is PARALLELIZABLE (process K tokens at once)
  - Draft model is fast because it's small
  - Acceptance rate is high for "easy" tokens (common next words)
  - Mathematically guaranteed: same output distribution as target model
  
Performance:
  - Acceptance rate ~70-90% for well-matched draft/target
  - Effective speedup: 1.5-2.5× for decode-heavy workloads
  - No accuracy compromise (provably identical distribution)
```

## 8.4 Tensor Parallelism vs Pipeline Parallelism

```
When a model doesn't fit on one GPU:

Tensor Parallelism (TP):
  Split each layer's weights ACROSS GPUs
  Every GPU computes PART of each layer, then ALL-GATHER results
  
  GPU 0: first half of attention heads
  GPU 1: second half of attention heads
  → All-Gather after each layer
  
  Pros: lowest latency (all GPUs work simultaneously on each token)
  Cons: requires high-bandwidth interconnect (NVLink) due to frequent sync
  Best for: inference (latency-sensitive)

Pipeline Parallelism (PP):
  Assign different LAYERS to different GPUs
  
  GPU 0: layers 0-19
  GPU 1: layers 20-39
  GPU 2: layers 40-59
  GPU 3: layers 60-79
  
  Pros: minimal communication (only between stages)
  Cons: pipeline bubbles (GPUs idle waiting for earlier stages)
  Best for: training (can overlap with micro-batches)

Common configurations for 70B model:
  Inference:  TP=4 (4 GPUs, NVLink) → fits in 4×80 GB with KV cache room
  Training:   TP=8 (intra-node) × PP=4 (inter-node) × DP=8 = 256 GPUs
```

## 8.5 vLLM Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    vLLM Engine                           │
├─────────────────────────────────────────────────────────┤
│ Scheduler:                                              │
│   - Manages request queue (waiting, running, swapped)    │
│   - Decides which requests get GPU KV cache blocks      │
│   - Preemption policy: recompute or swap to CPU         │
├─────────────────────────────────────────────────────────┤
│ Block Manager:                                          │
│   - Allocates/frees KV cache blocks (paged attention)   │
│   - Block table: logical block → physical GPU memory    │
│   - Copy-on-write for prefix sharing                    │
├─────────────────────────────────────────────────────────┤
│ Worker (one per GPU):                                   │
│   - Executes model forward pass                         │
│   - Paged attention kernels (custom CUDA)               │
│   - KV cache management                                 │
├─────────────────────────────────────────────────────────┤
│ Model Runner:                                           │
│   - CUDA Graphs for decode (fixed batch shapes)         │
│   - Flash Attention for prefill                         │
│   - Prefix caching lookup                               │
└─────────────────────────────────────────────────────────┘

Key performance features:
  - Continuous batching (iteration-level scheduling)
  - PagedAttention (near-zero KV cache waste)
  - CUDA Graphs (reduced kernel launch overhead for decode)
  - Prefix caching (shared system prompt KV)
  - Chunked prefill (split long prefills to avoid blocking decode)
  - Speculative decoding (optional, for throughput)
```

## 8.6 TensorRT-LLM Architecture

```
TensorRT-LLM = TensorRT optimization + LLM-specific features

Build phase (offline, once per model):
  1. Import model (HuggingFace → TRT-LLM model definition)
  2. Apply optimizations:
     - Layer fusion (RMSNorm+residual, QKV projection fusion)
     - Precision calibration (FP8/INT8 with calibration data)
     - Memory planning (optimal tensor placement)
     - Kernel selection (auto-tune kernels for specific GPU)
  3. Build TensorRT engine (serialized, GPU-specific binary)

Runtime (Triton + TRT-LLM backend):
  - In-flight batching (continuous batching implementation)
  - Paged KV cache (similar to vLLM)
  - Multi-GPU: tensor parallel execution
  - Inflight request cancellation
  - KV cache quantization (FP8 KV)
  
Performance edge vs vLLM:
  - Custom CUDA kernels for each GPU architecture
  - Aggressive fusion (fewer kernel launches)
  - Static graph optimization (TensorRT compiler)
  - Typically 10-30% faster than vLLM for same model
  
Flexibility edge of vLLM:
  - Supports more models out-of-the-box
  - Easier to modify/extend (Python)
  - Faster iteration on new features
  - Better for research/experimentation
```

---

# SECTION 9: FLASH ATTENTION

---

## 9.1 The Memory Problem with Standard Attention

```
Standard attention (naive):
  Q, K, V ∈ R^(batch × heads × seq_len × head_dim)
  
  Step 1: S = Q × K^T              → S ∈ R^(seq_len × seq_len)  HUGE!
  Step 2: P = softmax(S)           → P ∈ R^(seq_len × seq_len)
  Step 3: O = P × V               → O ∈ R^(seq_len × head_dim)
  
  Problem: S and P are seq_len × seq_len matrices
  For seq_len = 8192: 8192² × 4 bytes = 256 MB per head per batch!
  Must store in HBM → multiple HBM reads/writes → SLOW

Memory complexity: O(N²) where N = sequence length
This is why long-context models were expensive before Flash Attention.
```

## 9.2 Flash Attention Algorithm

```
Key insight: compute attention in TILES that fit in SRAM (shared memory),
never materializing the full N×N attention matrix in HBM.

Algorithm (tiled, online softmax):
  For each block of Q (tile_q):
    For each block of K, V (tile_k, tile_v):
      1. Load tile_q, tile_k, tile_v into shared memory (fast!)
      2. Compute S_tile = tile_q × tile_k^T (in shared memory)
      3. Update running softmax statistics (online softmax trick)
      4. Compute partial O_tile = softmax(S_tile) × tile_v
      5. Accumulate into output O
    End for
  End for
  
  Never writes the N×N matrix to HBM!
  Only reads Q, K, V once and writes O once.
  
Memory complexity: O(N) instead of O(N²)!
Speed: 2-4× faster than standard attention (fewer HBM accesses)

IO complexity:
  Standard: O(N² × d) HBM accesses
  Flash:    O(N² × d² / M) where M = SRAM size
  
  For typical M (shared memory) >> d²: Flash is much better
```

## 9.3 Flash Attention Variants

```
Flash Attention 1 (2022):
  - Tiled computation, online softmax
  - 2-4× faster, exact (no approximation)
  - Fused into single CUDA kernel (no intermediate writes)

Flash Attention 2 (2023):
  - Better parallelism (parallelize over sequence length, not just batch)
  - Better warp-level scheduling (reduce non-matmul instructions)
  - 2× faster than Flash Attention 1

Flash Attention 3 (2024, H100):
  - Exploits H100 asynchronous execution (TMA + wgmma)
  - Overlaps shared memory loads with Tensor Core computation
  - FP8 support (native H100 precision)
  - Near peak FP8 throughput on H100

Flash Decoding:
  - Optimized for decode phase (batch=1 per sequence, long KV cache)
  - Parallelizes over KV cache length (split-K)
  - Important for serving (decode is the bottleneck, not prefill)
```

---

# SECTION 10: GPU MULTI-TENANCY & SHARING

---

## 10.1 MIG Deep Dive

```
H100 MIG (Multi-Instance GPU) profiles:

Full GPU: 132 SMs, 80 GB HBM, 8 memory partitions

Available profiles:
  1g.10gb:  ~16 SMs, 10 GB  (7 instances max) — small models
  2g.20gb:  ~32 SMs, 20 GB  (3 instances max) — medium models
  3g.40gb:  ~48 SMs, 40 GB  (2 instances max) — larger models
  4g.40gb:  ~64 SMs, 40 GB  (1 instance + 1g) — asymmetric
  7g.80gb:  132 SMs, 80 GB  (1 instance) — full GPU (default)

Key properties:
  - HARDWARE isolation: each instance has dedicated SMs + memory
  - Fault isolation: ECC error in one instance doesn't affect others
  - QoS guarantees: guaranteed memory bandwidth per instance
  - Separate CUDA contexts: each instance appears as a separate GPU

Configuring MIG:
  nvidia-smi mig -cgi 9,9,9,9,9,9,9  # Create 7 × 1g.10gb instances
  nvidia-smi mig -cci                  # Create compute instances

K8s integration:
  - NVIDIA device plugin advertises MIG instances as separate resources
  - Pod requests: nvidia.com/mig-1g.10gb: "1"
```

## 10.2 MPS (Multi-Process Service)

```
MPS allows multiple processes to share GPU compute simultaneously:

Without MPS:
  Process A: [kernel].........[kernel].........[kernel]
  Process B: .........[kernel].........[kernel]..........
  = Time-sliced, context switch overhead (~25 µs per switch)

With MPS:
  Process A: [kernel][kernel][kernel]
  Process B: [kernel][kernel][kernel]
  = Running SIMULTANEOUSLY on different SMs
  = No context switch overhead

How it works:
  - MPS daemon creates a shared CUDA context
  - All client processes submit work through the shared context
  - GPU schedules kernels from all processes concurrently
  - Software-level resource partitioning (thread percentage limits)

Limitations:
  - NO memory isolation (one process can corrupt another's memory)
  - NO error isolation (one fault affects all processes)
  - Limited to same-user (security boundary)
  
When to use:
  - Multiple small models that individually underutilize GPU
  - Ensemble inference (5 small models sharing one GPU)
  - When MIG granularity is too coarse

Configuration:
  nvidia-cuda-mps-control -d  # start MPS daemon
  echo "set_default_active_thread_percentage 50" | \
    nvidia-cuda-mps-control    # limit each client to 50% of SMs
```

---

# SECTION 11: GPU ERRORS, HEALTH, AND RELIABILITY

---

## 11.1 Xid Errors (GPU Fatal/Non-Fatal Events)

```
Xid errors are GPU hardware/software error events logged by the driver.
Check: dmesg | grep -i "xid"

Critical Xid codes:
  Xid 31: GPU memory page fault (application bug OR hardware failure)
  Xid 43: GPU stopped processing (hardware hang → needs reset)
  Xid 45: Preemptive cleanup due to previous errors
  Xid 48: Double-bit ECC error (UNCORRECTABLE → data corruption!)
  Xid 63: ECC page retirement (bad memory cell removed from use)
  Xid 64: ECC page retired, limit reached (GPU end-of-life!)
  Xid 69: GPU access to other GPU's memory failed (NVLink error)
  Xid 74: NVLink error (link degradation or failure)
  Xid 79: GPU has fallen off the bus (PCIe failure)
  Xid 94: Contained ECC error (corrected, but watch frequency)

Action matrix:
  Xid 31: check application code → if persistent, replace GPU
  Xid 43: reset GPU (nvidia-smi -r) → if persistent, RMA
  Xid 48: IMMEDIATELY stop using GPU, RMA
  Xid 63: monitor retired page count → plan replacement
  Xid 64: GPU must be replaced (too many bad cells)
  Xid 79: check PCIe riser/cable, reseat GPU, check motherboard
```

## 11.2 ECC Memory and Row Remapping

```
GPU HBM has ECC (Error Correcting Code):
  - SECDED: Single-Error Correct, Double-Error Detect
  - Single-bit errors: silently corrected (common, normal)
  - Double-bit errors: detected but NOT correctable (Xid 48 → FATAL)

nvidia-smi:
  Correctable:    1234  (single-bit, corrected — watch if climbing fast)
  Uncorrectable:  0     (double-bit, if non-zero → PROBLEM)

Row remapping:
  When a memory cell keeps having errors, the GPU "remaps" that row
  to a spare row (like disk bad-sector remapping).
  
  nvidia-smi -q | grep "Row Remapper"
  Max remappable rows per bank: 128
  If exhausted: GPU is end-of-life (Xid 64)

Monitoring:
  DCGM metrics:
    DCGM_FI_DEV_ECC_SBE_VOL_TOTAL  (single-bit errors)
    DCGM_FI_DEV_ECC_DBE_VOL_TOTAL  (double-bit errors)
    DCGM_FI_DEV_ROW_REMAP_PENDING  (pending remaps)
    DCGM_FI_DEV_ROW_REMAP_FAILURE  (remap failures)
```

## 11.3 Thermal Throttling

```
GPU temperature management:
  Target temperature: ~80°C (H100)
  Throttle temperature: ~83°C (clock reduction begins)
  Shutdown temperature: ~90°C (emergency shutdown)

nvidia-smi -q -d TEMPERATURE,POWER:
  GPU Current Temp: 78 C
  GPU Shutdown Temp: 90 C
  GPU Slowdown Temp: 83 C
  Power Draw: 650 W / 700 W (TDP)

Throttle reasons:
  nvidia-smi -q -d PERFORMANCE
  Clocks Throttle Reasons:
    GPU Idle: Not Active
    SW Power Cap: Not Active
    HW Slowdown: Not Active        ← if Active: thermal or power!
    HW Thermal Slowdown: Active    ← GPU too hot!
    HW Power Brake Slowdown: Not Active

Impact:
  Throttling reduces clocks by 10-30%
  → Training: steps take longer (but correct)
  → Inference: latency increases by 10-30% (SLA breach!)

Prevention:
  - Adequate cooling (liquid cooling for H100 at 700W TDP)
  - Limit max power: nvidia-smi -pl 600 (reduce TDP cap)
  - Airflow management in rack (hot/cold aisle)
  - Monitor temperature PROACTIVELY with alerts at 75°C
```

## 11.4 GPU Health Checks in Production

```yaml
# Pre-job GPU health validation (run before training/inference):
apiVersion: batch/v1
kind: Job
metadata:
  name: gpu-health-check
spec:
  template:
    spec:
      containers:
      - name: health
        image: nvcr.io/nvidia/cuda:12.4-runtime
        command: ["bash", "-c"]
        args:
        - |
          # Check GPU is visible
          nvidia-smi || exit 1
          
          # Check ECC errors
          DBE=$(nvidia-smi --query-gpu=ecc.errors.uncorrected.volatile.total --format=csv,noheader)
          [[ "$DBE" != "0" ]] && echo "FATAL: uncorrectable ECC errors" && exit 1
          
          # Check memory health (allocate and test)
          /usr/local/cuda/extras/demo_suite/deviceQuery || exit 1
          
          # Run DCGM diagnostic (level 3 = thorough)
          dcgmi diag -r 3 || exit 1
          
          # Check NVLink health
          nvidia-smi nvlink -s || exit 1
          
          echo "GPU health: PASS"
        resources:
          limits:
            nvidia.com/gpu: "1"
```

---

# SECTION 12: PCIe AND INTERCONNECT DETAILS

---

## 12.1 PCIe Bandwidth Reality

```
PCIe Gen5 x16 (H100):
  Theoretical: 128 GB/s bidirectional (64 GB/s each direction)
  Encoding overhead (128b/130b): ~1.5%
  Actual usable: ~63 GB/s per direction
  
  In practice (DMA transfers):
  Small transfers (< 64 KB): dominated by latency (~2-5 µs)
  Large transfers (> 1 MB): approach line rate (~60 GB/s)

PCIe vs NVLink:
  PCIe Gen5 x16: 64 GB/s per direction
  NVLink (H100):  450 GB/s per direction (7× faster!)
  
  This is why tensor parallelism should ONLY use NVLink-connected GPUs.
  PCIe is for: CPU↔GPU, GPU↔NIC, GPU↔NVMe
  NVLink is for: GPU↔GPU (model parallelism)
```

## 12.2 BAR1 Memory

```
BAR1: a PCIe memory window that maps GPU memory to CPU address space.

Why it matters:
  - RDMA (GPUDirect): NIC needs to DMA to GPU memory via BAR1
  - Large BAR (resizable BAR): BAR1 maps ALL of GPU memory
  - Without large BAR: limited to 256 MB window → extra copies needed

Check BAR1 size:
  nvidia-smi -q | grep "BAR1"
  BAR1 Memory Usage:
    Total: 131072 MiB  ← 128 GB (large BAR enabled, good!)
    
  If total is 256 MiB: large BAR not enabled → GPUDirect will be slow!

Enable large BAR:
  - BIOS setting: "Above 4G Decoding" = Enabled
  - BIOS setting: "Resizable BAR" = Enabled
  - Requires 64-bit OS and PCIe Gen3+ motherboard
```

## 12.3 NVLink and NVSwitch Topology

```
H100 DGX (8 GPUs per node):

Without NVSwitch (PCIe only):
  GPU 0 ↔ PCIe switch ↔ GPU 1  (64 GB/s, shared bus)
  Any-to-any: must traverse PCIe topology, contention possible

With NVSwitch (DGX H100):
  ┌─────────────────────────────────────────┐
  │            NVSwitch Fabric               │
  │  (4 NVSwitches, full crossbar)          │
  ├────┬────┬────┬────┬────┬────┬────┬────┤
  │GPU0│GPU1│GPU2│GPU3│GPU4│GPU5│GPU6│GPU7│
  └────┴────┴────┴────┴────┴────┴────┴────┘
  
  Every GPU can talk to every other GPU at 900 GB/s simultaneously
  Full bisection bandwidth: 900 GB/s × 8 = 7.2 TB/s aggregate!
  
  This is why intra-node all-reduce is ~free compared to inter-node.

NVLink topology query:
  nvidia-smi topo -m
  Shows: NV18 (NVLink 18 hops), PIX (PCIe switch), SYS (cross-socket)
  
  Example output:
        GPU0  GPU1  GPU2  GPU3
  GPU0  X     NV18  NV18  NV18
  GPU1  NV18  X     NV18  NV18
  GPU2  NV18  NV18  X     NV18
  GPU3  NV18  NV18  NV18  X
  
  NV18 = connected via NVSwitch (18 links), optimal
  SYS = crosses PCIe/QPI, avoid for tensor parallelism
```

---

# SECTION 13: DISTRIBUTED TRAINING PATTERNS

---

## 13.1 Data Parallelism (DDP)

```
Each GPU has a FULL copy of the model.
Different GPUs process different mini-batches.
Gradients synchronized via All-Reduce.

GPU 0: full model, mini-batch 0 → forward → backward → all-reduce gradients
GPU 1: full model, mini-batch 1 → forward → backward → all-reduce gradients
GPU 2: full model, mini-batch 2 → forward → backward → all-reduce gradients
GPU 3: full model, mini-batch 3 → forward → backward → all-reduce gradients

After all-reduce: all GPUs have same averaged gradients → same weight update.

Scaling:
  - Linear speedup up to the point where all-reduce dominates
  - Works for models that fit on one GPU
  - Global batch size = per-GPU batch × num_GPUs
  - May need learning rate warmup for large global batch sizes

PyTorch DDP:
  model = DistributedDataParallel(model, device_ids=[local_rank])
  # Automatically overlaps all-reduce with backward pass
```

## 13.2 FSDP (Fully Sharded Data Parallelism)

```
Problem with DDP: model must fit on ONE GPU (weights + gradients + optimizer)
  7B model needs: 14 GB (weights) + 14 GB (gradients) + 28 GB (Adam states) = 56 GB!
  Barely fits on H100 80 GB (no room for activations/batch)

FSDP solution: shard EVERYTHING across GPUs
  Each GPU holds only 1/N of: weights, gradients, optimizer states
  
  Forward pass:
    All-Gather weights for each layer (temporary, from other GPUs)
    Compute forward with full weights
    Discard non-local weight shards (free memory)
  
  Backward pass:
    All-Gather weights again (needed for gradient computation)
    Compute gradients
    Reduce-Scatter gradients (each GPU keeps 1/N of gradients)
    Free non-local gradients
  
  Memory per GPU: (Weights + Grads + Optim) / N + Activations
  
  7B on 4 GPUs: 56 GB / 4 = 14 GB per GPU (+ activations)
  Fits easily with room for large batch sizes!

Tradeoff:
  - More communication (all-gather before each layer)
  - But: overlaps with compute (pipeline the next gather while computing)
  - Enables training models 4-8× larger than DDP on same hardware
```

## 13.3 The 3D Parallelism Strategy

```
For very large models (70B+) on large clusters:

Combine: Data Parallelism (DP) × Tensor Parallelism (TP) × Pipeline Parallelism (PP)

Example: 70B model on 256 GPUs (32 nodes × 8 GPUs):
  TP = 8 (within one node, NVLink) — split layers across 8 GPUs
  PP = 4 (across 4 nodes) — split 80 layers into 4 stages of 20 layers
  DP = 8 (across remaining 8 groups) — replicate for throughput

  Total: 8 × 4 × 8 = 256 GPUs

Why this split:
  - TP within node: requires NVLink bandwidth (all-gather every layer)
  - PP across nodes: minimal communication (only between stages)
  - DP across groups: all-reduce once per step (overlapped with backward)

Communication cost:
  TP: continuous, high-bandwidth (NVLink handles it)
  PP: point-to-point, only at stage boundaries (moderate)
  DP: one all-reduce per step (can overlap with compute)
```

---

# SECTION 14: GPU UTILIZATION METRICS (What to Monitor)

---

## 14.1 Metric Types

```
nvidia-smi dmon (real-time monitoring):
  SM%:   Percentage of time at least one warp is active on SMs
  MEM%:  HBM memory bandwidth utilization
  ENC%:  Video encoder utilization (irrelevant for AI)
  DEC%:  Video decoder utilization (irrelevant for AI)

DCGM (Data Center GPU Manager) — production monitoring:
  DCGM_FI_DEV_GPU_UTIL:          SM utilization (0-100%)
  DCGM_FI_DEV_MEM_COPY_UTIL:     Memory bandwidth utilization (0-100%)
  DCGM_FI_DEV_GPU_TEMP:          Temperature
  DCGM_FI_DEV_POWER_USAGE:       Power draw (watts)
  DCGM_FI_DEV_TENSOR_ACTIVE:     Tensor Core activity (0-100%)
  DCGM_FI_DEV_FB_USED:           GPU memory used (bytes)
  DCGM_FI_DEV_PCIE_TX_THROUGHPUT: PCIe TX bandwidth
  DCGM_FI_DEV_NVLINK_BANDWIDTH_TOTAL: NVLink bandwidth
  DCGM_FI_DEV_XID_ERRORS:        Xid error count
```

## 14.2 Interpreting Metrics for Different Workloads

```
LLM Decode (memory-bound):
  SM%:  30-60%   (low! waiting for memory)
  MEM%: 80-95%   (high! bottleneck is HBM bandwidth)
  Tensor Core: 20-40%  (Tensor Cores mostly idle)
  → This is NORMAL for decode. Don't optimize SM util, optimize bandwidth.

LLM Prefill (compute-bound):
  SM%:  85-95%   (high! computing attention + FFN)
  MEM%: 50-70%   (moderate, reading KV + weights)
  Tensor Core: 70-90%  (doing actual GEMM work)
  → This is where quantization and fusion matter most.

Training GEMM (compute-bound):
  SM%:  90-99%   (saturated)
  MEM%: 40-60%   (weights read once, reused many times)
  Tensor Core: 80-95%  (peak compute)
  → Good! If lower, check batch size (too small) or sync overhead.

Vector Search (bandwidth-bound):
  SM%:  40-70%   (computing dot products)
  MEM%: 70-90%   (reading embeddings from HBM)
  Tensor Core: 0-10%  (dot products use CUDA cores, not Tensor Cores)
  → Optimize by: batching more queries, using FP16/INT8 embeddings
```

## 14.3 The "GPU is 0% utilized" Problem

```
nvidia-smi shows GPU Util: 0% but your service is "running"

Common causes:
1. Model not loaded into GPU memory yet (still on CPU/disk)
   Check: GPU Memory Used should show model size

2. All requests being handled by CPU preprocessing
   Check: is there actual inference traffic reaching the GPU?

3. CUDA Graphs warmup phase (capturing graphs, not yet serving)
   Check: vLLM/TRT-LLM startup logs for "warmup complete"

4. Wrong GPU device (pod sees GPU 0 but model loaded to GPU 1)
   Check: CUDA_VISIBLE_DEVICES, nvidia-smi pmon -i 0

5. Batching waiting for batch to fill (no requests in queue)
   Check: adjust max_batch_wait_time (vLLM: waiting_served_ratio)

Debug command:
  nvidia-smi pmon -i 0 -s u   # Per-process GPU utilization
  Shows: which PID is using GPU and how much
```

---

# SECTION 15: REAL-WORLD GPU FAILURE SCENARIOS

---

## 15.1 "Model loads but inference returns garbage"

```
Diagnosis:
  1. Output tokens are random/corrupted
  2. nvidia-smi shows normal temperature and clocks
  3. Check: DCGM_FI_DEV_ECC_SBE_VOL_TOTAL → climbing fast!
  4. High single-bit ECC error rate → data corruption DESPITE correction

Root cause: GPU memory cells degrading (early failure mode)
  ECC corrects single-bit errors, but if the rate is high enough,
  some multi-bit errors slip through → corrupted activations/weights

Fix:
  - Monitor SBE rate: > 100/hour is abnormal
  - Run DCGM diagnostic: dcgmi diag -r 3
  - If failing: replace GPU (RMA)
  - Prevention: alert on SBE rate > threshold in Grafana
```

## 15.2 "Training NaN after 1000 steps"

```
Diagnosis:
  1. Loss goes to NaN suddenly at step ~1000
  2. No code changes, same config worked before
  3. Check: nvidia-smi -q | grep "Retired Pages"
     → Pending retirement: 2 pages!
  4. GPU has memory pages queued for retirement but not yet retired
     Until next GPU reset, those pages may return corrupted data

Root cause: ECC row remapping hasn't been applied (needs GPU reset)
Fix:
  - nvidia-smi -r 0  (reset GPU to apply pending retirements)
  - If persistent after reset: check DBE count (uncorrectable = RMA)
  - Add pre-training health check: reject GPUs with pending retirements
```

## 15.3 "8-GPU training is only 4× faster, not 8×"

```
Diagnosis:
  1. Expected ~8× speedup with 8 GPUs, getting only 4×
  2. Check NCCL debug: all GPUs using NVLink (good)
  3. Profile with nsight: large gaps between compute and all-reduce
  4. Check: nvidia-smi topo -m
     → GPU 0-3: NV18 (NVSwitch connected)
     → GPU 4-7: NV18 (NVSwitch connected)
     → GPU 0 ↔ GPU 4: SYS (crosses CPU socket!)

Root cause: not a DGX — NVLink only within each socket group
  All-reduce between socket groups goes through PCIe/QPI (much slower)
  
Fix:
  - Use hierarchical all-reduce (reduce within socket, then across)
  - NCCL_ALGO=Tree (may help for this topology)
  - Or: TP=4 (within NVLink group) + DP=2 (across sockets)
  - Long-term: use proper DGX/NVSwitch nodes for 8-GPU training
```

## 15.4 "Inference latency gradually increases over hours"

```
Diagnosis:
  1. p50 stable at 8 ms, p99 stable at 12 ms initially
  2. After 6 hours: p50 = 15 ms, p99 = 45 ms (both degrading!)
  3. GPU temperature: started 72°C, now 85°C → THERMAL THROTTLING
  4. nvidia-smi: Current Clocks: 1200 MHz (should be 1830 MHz)

Root cause: cooling system degradation or airflow obstruction
  As GPU heats up: clocks reduce → inference slower → more queuing → more heat
  (positive feedback loop until thermal equilibrium at reduced clocks)

Fix:
  - Immediate: reduce batch size or power limit (nvidia-smi -pl 500)
  - Root cause: check fans, airflow, thermal paste, ambient temperature
  - Prevention: alert at 78°C, hard limit at 80°C
  - Use nvidia-smi -q -d PERFORMANCE to confirm throttle reason
```

## 15.5 "GPU memory leak — OOM after 24 hours of serving"

```
Diagnosis:
  1. nvidia-smi: memory usage slowly climbing (1 MB/hour)
  2. No new models being loaded
  3. After 24h: OOM → service crash

Root cause: CUDA context leak from failed requests
  Each failed request creates an error in a CUDA stream
  Error state holds references to allocated memory
  Without proper cleanup → memory never freed

Fix:
  - Ensure all CUDA errors are checked and streams synchronized
  - Use torch.cuda.empty_cache() periodically
  - Set PYTORCH_CUDA_ALLOC_CONF=garbage_collection_threshold:0.6
  - vLLM: ensure max_model_len is correctly set (prevents over-allocation)
  - Restart pod on schedule (every 12-24h) as safety net (ugly but effective)
```

---

# SECTION 16: ADVANCED GPU INTERVIEW QUESTIONS (Grind-Proof)

## "Walk me through what happens on the GPU when a single token is generated"

→ Full path for one decode step (LLM):
1. Input token embedding looked up from embedding table (HBM read)
2. For each transformer layer (×80 for 70B):
   a. RMSNorm: read residual from HBM, normalize (memory-bound, fused kernel)
   b. QKV projection: GEMM with weight matrix (Tensor Core, compute-bound)
   c. RoPE: apply rotary position encoding (element-wise, fused with Q)
   d. KV cache update: write new K, V to paged KV cache slots
   e. Attention: Flash Decoding over entire KV cache sequence (memory-bound!)
   f. Output projection: GEMM (Tensor Core)
   g. Residual add + RMSNorm (fused kernel)
   h. FFN gate+up projection: fused GEMM
   i. SiLU activation + element-wise multiply (fused)
   j. FFN down projection: GEMM
   k. Residual add
3. Final RMSNorm
4. LM head: GEMM projecting to vocabulary (50K+ tokens)
5. Sampling: top-k/top-p filtering, temperature scaling, random sample
6. Output token ID

Total HBM read for 70B FP16: ~140 GB (every weight matrix read once)
At 3.35 TB/s: minimum 42 ms per token (single GPU)
With TP=4: 140/4 = 35 GB per GPU, ~10 ms per token

## "How do you decide between FP8, INT8, and INT4 quantization?"

→ Decision framework:
```
Accuracy sensitivity:
  High (medical, legal): FP16 or FP8 only
  Medium (general chat): FP8 or INT8
  Low (classification, short answers): INT4 with GPTQ/AWQ

Hardware:
  H100: use FP8 (native Tensor Core support, no special calibration)
  A100: use INT8 (best supported, FP8 not native)
  Older GPUs: INT8 only (limited quantization support)

Model size vs GPU memory:
  Model fits in FP16: use FP16 (simplest, no accuracy risk)
  Model needs 2 GPUs in FP16: use FP8 → fits in 1 GPU (save TP overhead)
  Model needs 4+ GPUs in FP16: use INT4 → reduce TP degree

Throughput need:
  FP16 → FP8: ~2× throughput gain (half the bandwidth)
  FP16 → INT4: ~3× throughput gain (limited by dequant overhead)
```

## "Explain the roofline model and how you use it for GPU kernels"

→ The roofline model shows whether a kernel is compute-bound or memory-bound:
```
Performance (FLOPS/s)
    ▲
    │              ╱─────────── Peak Compute (e.g., 1979 TFLOPS FP16)
    │            ╱
    │          ╱
    │        ╱  ← Memory-bound region
    │      ╱      (performance limited by HBM bandwidth)
    │    ╱
    │  ╱
    │╱
    └──────────────────────► Arithmetic Intensity (FLOPS/byte)
         ↑
    Ridge point = Peak FLOPS / Peak BW
    H100: 1979 TFLOPS / 3.35 TB/s = 590 FLOPS/byte

    If your kernel's arithmetic intensity < 590: MEMORY-BOUND
    If your kernel's arithmetic intensity > 590: COMPUTE-BOUND

LLM decode attention: ~10 FLOPS/byte → deeply memory-bound
GEMM (large batch):   ~1000 FLOPS/byte → compute-bound
```

## "How does vLLM handle request preemption when GPU memory is full?"

→ Two strategies:
1. **Recompute:** evict KV cache of lowest-priority request, free blocks.
   When request resumes: recompute its KV cache from scratch (re-prefill).
   Pros: simple, no CPU memory needed
   Cons: wasted compute for recomputation

2. **Swap:** copy evicted KV cache to CPU memory (pinned, async).
   When request resumes: copy back from CPU to GPU.
   Pros: no recomputation
   Cons: requires CPU memory headroom, PCIe bandwidth for swap

   vLLM default: swap (if CPU memory available), else recompute.
   Triggered when: new request arrives but no free blocks for its KV cache.

## "What is the difference between SM utilization and Tensor Core utilization?"

→ They measure different things:
```
SM Utilization (GPU Util in nvidia-smi):
  "What fraction of time is at least one warp active on SMs?"
  Can be 100% even if doing simple integer ops (no Tensor Cores used)
  Misleading for memory-bound kernels (warps active but stalled on memory)

Tensor Core Utilization:
  "What fraction of time are Tensor Cores executing matrix operations?"
  Only active during GEMMs/convolutions that use TC
  Better indicator of whether you're using the GPU's BEST hardware

Memory Bandwidth Utilization:
  "What fraction of peak HBM bandwidth is being used?"
  For memory-bound kernels (LLM decode): this is the TRUE bottleneck metric
  
Example interpretation:
  SM=95%, TC=10%, MEM=90%: memory-bound kernel using full bandwidth
  SM=95%, TC=85%, MEM=50%: compute-bound GEMM (Tensor Cores saturated)
  SM=30%, TC=5%, MEM=10%: GPU mostly IDLE (check batching, scheduling)
```

## "How do you overlap prefill and decode in a serving system?"

→ Chunked prefill (vLLM/TRT-LLM):
```
Problem: long prefill (4096 tokens) blocks all decode iterations
  → decode latency spikes for existing requests (waiting for prefill to finish)

Solution: split prefill into chunks, interleave with decode:
  Step 1: [prefill chunk 1: 512 tokens] + [decode batch: 32 requests]
  Step 2: [prefill chunk 2: 512 tokens] + [decode batch: 32 requests]
  ...
  Step 8: [prefill chunk 8: 512 tokens] + [decode batch: 32 requests]

  Prefill takes 8 steps instead of 1 (slightly slower for that request)
  But decode requests continue generating tokens without interruption!
  
  Tradeoff: slightly higher TTFT (time-to-first-token) for new requests
            but stable TPOT (time-per-output-token) for existing requests
  
  vLLM: max_num_batched_tokens controls chunk size
  TRT-LLM: max_tokens_in_paged_kv_cache + scheduling_policy
```

---

# SECTION 17: AMD INSTINCT GPUs & ROCm FOR INFERENCE

---

## 17.1 AMD Instinct GPU Architecture Comparison

```
| Spec | MI250X (CDNA2) | MI300X (CDNA3) | H100 (Hopper) |
|------|----------------|----------------|----------------|
| Memory | 128 GB HBM2e | 192 GB HBM3 | 80 GB HBM3 |
| Memory BW | 3.2 TB/s | 5.3 TB/s | 3.35 TB/s |
| FP16 TFLOPS | 383 | 1,307 | 1,979 |
| FP8 TFLOPS | N/A | 2,615 | 3,958 |
| BF16 TFLOPS | 383 | 1,307 | 1,979 |
| Interconnect | Infinity Fabric | XGMI 3.0 (896 GB/s) | NVLink 4.0 (900 GB/s) |
| TDP | 560W | 750W | 700W |
| Die architecture | 2 GCDs (chiplets) | 1 XCD × 8 (chiplets) | Monolithic |
| Compute Units/SMs | 220 CU | 304 CU | 132 SM |

Key insight for inference:
  MI300X has 192 GB HBM3 vs H100's 80 GB HBM3
  → MI300X can serve 70B model in FP16 on SINGLE GPU (140 GB needed)!
  → H100 needs TP=2 minimum for same model
  → Fewer GPUs for inference = simpler deployment, lower inter-GPU comm

Memory bandwidth advantage:
  MI300X: 5.3 TB/s → decode throughput limited by BW
  H100:   3.35 TB/s
  For memory-bound decode: MI300X is ~58% faster per-GPU theoretically
  
  Tokens/sec (decode, 70B FP16):
    1 × MI300X: ~90 tokens/sec (model fits entirely!)
    2 × H100 TP=2: ~80 tokens/sec (comm overhead eats into advantage)
```

## 17.2 ROCm Software Stack (vs CUDA)

```
ROCm equivalents of NVIDIA tools:

| NVIDIA (CUDA) | AMD (ROCm) | Notes |
|---------------|------------|-------|
| CUDA Runtime | HIP Runtime | API-compatible (hipify converts CUDA→HIP) |
| cuBLAS | rocBLAS | Same API semantics |
| cuDNN | MIOpen | Different API, same function |
| NCCL | RCCL | API-compatible |
| Nsight Systems | rocprof / Omniperf | |
| Nsight Compute | Omniperf / rocprof --stats | |
| TensorRT | MIGraphX | AMD inference optimizer |
| nvidia-smi | rocm-smi | |
| CUDA Graphs | HIP Graphs | Same concept, HIP API |
| nvcc | hipcc | Compiler |

ROCm version compatibility (critical for production):
  ROCm 6.x: required for MI300X
  ROCm 5.x: MI250X
  PyTorch: check torch.version.hip (ROCm version it was compiled with)
  vLLM: ROCm support from v0.4+ (continuous improvement)
  
Container images:
  rocm/pytorch:latest         # PyTorch with ROCm
  vllm/vllm-openai:latest-rocm  # vLLM for AMD
```

## 17.3 Running vLLM on AMD MI300X

```bash
# vLLM on ROCm (production deployment):
docker run --device=/dev/kfd --device=/dev/dri \
  --security-opt seccomp=unconfined \
  -v /models:/models \
  vllm/vllm-openai:latest-rocm \
  --model /models/llama-70b \
  --tensor-parallel-size 1 \          # 70B FP16 fits in 192 GB!
  --dtype float16 \
  --max-model-len 4096 \
  --gpu-memory-utilization 0.90

# Key ROCm-specific flags:
HSA_OVERRIDE_GFX_VERSION=9.4.2  # For MI300X compatibility
PYTORCH_TUNABLEOP_ENABLED=1      # AMD auto-tuning for GEMMs
HIP_FORCE_DEV_KERNARG=1          # Performance optimization
VLLM_USE_TRITON_FLASH_ATTN=1    # Triton-based FlashAttention for AMD

# Performance tuning for MI300X inference:
#   1. Enable TunableOp: auto-selects fastest GEMM kernel per shape
#   2. Use FP8 (MI300X has native FP8): --dtype fp8 --quantization fp8
#   3. Larger KV cache (192 GB allows it): more concurrent sequences
#   4. Use HIP Graphs for decode: reduces kernel launch overhead
```

## 17.4 MI300X Architecture Deep Dive (Interview-Ready)

```
MI300X internal architecture:
  8 XCDs (Accelerator Complex Dies) — chiplet design
  Each XCD: 38 Compute Units, 64 KB L1/CU, 4 MB L2
  Total: 304 CUs, 256 MB L2 cache (massive!)
  
  Infinity Cache: 256 MB (acts as L3, reduces HBM pressure)
  HBM3 stacks: 8 × 24 GB = 192 GB
  
  Memory hierarchy for inference:
    Registers: fastest, per-wavefront (AMD's "warp")
    LDS (Local Data Share): equivalent to CUDA shared memory, 64 KB
    L1 cache: 64 KB per CU
    L2 cache: 4 MB per XCD (32 MB total across XCDs)
    Infinity Cache: 256 MB (unique to AMD, between L2 and HBM)
    HBM3: 192 GB at 5.3 TB/s

  Why Infinity Cache helps inference:
    KV cache hot entries often accessed repeatedly
    256 MB Infinity Cache can hold significant KV cache portion
    Effective bandwidth when hitting cache >> HBM bandwidth
    Reduces memory pressure during decode (auto-managed, no code changes)

Wavefront (AMD's Warp):
  64 threads wide (vs NVIDIA's 32-thread warp)
  Implication: occupancy calculations differ
  Min threads per CU for full occupancy = different formula
  But: framework handles this (vLLM/PyTorch abstract it)
```

## 17.5 MI300X vs H100: Inference Decision Matrix

```
Choose MI300X when:
  ✓ Large models (>80 GB) — MI300X's 192 GB avoids multi-GPU TP
  ✓ Memory-bandwidth-bound (decode phase, small batches)
  ✓ Cost-sensitive (AMD typically 30-40% cheaper per GPU-hour)
  ✓ vLLM is your serving stack (well-supported on ROCm)
  ✓ You don't need TensorRT-specific optimizations

Choose H100 when:
  ✓ Compute-bound workloads (large batch prefill)
  ✓ Need TensorRT-LLM (NVIDIA-only, best raw performance)
  ✓ FP8 with NVIDIA's calibration tools (more mature)
  ✓ Established CUDA ecosystem / existing CUDA code
  ✓ Need NVLink multi-node training (more proven at scale)

Hybrid strategy (what AMD customers actually do):
  Training: H100/A100 clusters (larger ecosystem, proven at scale)
  Inference: MI300X (cost advantage, memory advantage for large models)
  Use same vLLM config, just change --device flag
```

---

# SECTION 18: INFERENCE BENCHMARKING METHODOLOGY

---

## 18.1 Standard Inference Benchmarks

```
Key metrics to benchmark:
  1. TTFT (Time to First Token): prefill latency
  2. TPOT (Time Per Output Token): decode latency per token
  3. Throughput (tokens/sec): total output at various batch sizes
  4. Concurrent users: max users at target latency SLA

Benchmark tools:
  vLLM benchmark:
    python benchmark_serving.py \
      --backend vllm \
      --model meta-llama/Llama-2-70b \
      --num-prompts 1000 \
      --request-rate 10 \
      --dataset ShareGPT_V3_unfiltered_cleaned_split.json
  
  GenAI-Perf (NVIDIA/MLPerf):
    genai-perf -m llama-70b \
      --concurrency 32 \
      --measurement-interval 60000 \
      --percentile 99
  
  Custom benchmark (for AMD playbooks):
    Vary: batch_size, input_len, output_len, concurrency
    Measure: TTFT, TPOT, throughput, GPU util, memory usage
    Report: at each concurrency level + find saturation point
```

## 18.2 Benchmarking Methodology (Reproducible Results)

```
Phase 1: Single-request latency baseline
  - Send 1 request at a time, measure TTFT + full generation time
  - Vary input length: 128, 512, 1024, 2048, 4096 tokens
  - Vary output length: 64, 128, 256, 512, 1024 tokens
  - This establishes the MINIMUM possible latency

Phase 2: Throughput saturation
  - Fixed input/output length (e.g., 512 in / 256 out)
  - Ramp concurrency: 1, 2, 4, 8, 16, 32, 64, 128
  - Find: concurrency where throughput plateaus
  - Record: GPU utilization, memory usage at each point

Phase 3: SLA-bounded capacity
  - Set SLA: TTFT < 500 ms, TPOT < 50 ms
  - Find: max concurrency that stays within SLA
  - This is your "capacity per replica" number

Phase 4: Multi-GPU scaling
  - TP=1 vs TP=2 vs TP=4 vs TP=8
  - For each: repeat Phase 3
  - Determine: optimal TP for your model size and SLA

Phase 5: Optimization comparison
  - Baseline: FP16, no optimizations
  - +FP8 quantization: measure quality loss + speedup
  - +Prefix caching (for repeated system prompts)
  - +Speculative decoding (measure acceptance rate)
  - +CUDA/HIP Graphs
  - Report: cumulative benefit of each optimization

Report template (customer-ready playbook):
  | Config | TTFT p99 | TPOT p99 | Throughput | Max Concurrency | GPU Util |
  |--------|----------|----------|------------|-----------------|----------|
  | 1×MI300X FP16 | 180 ms | 32 ms | 2,800 tok/s | 45 users | 78% |
  | 1×MI300X FP8 | 120 ms | 22 ms | 4,200 tok/s | 68 users | 82% |
  | 4×MI300X TP4 FP8 | 80 ms | 15 ms | 15,000 tok/s | 250 users | 75% |
```

## 18.3 Communication Benchmarking for Multi-GPU Inference

```
RCCL/NCCL test suite for inference topology validation:

# Install RCCL tests:
git clone https://github.com/ROCmSoftwarePlatform/rccl-tests
cd rccl-tests && make HIP_HOME=/opt/rocm

# All-Reduce benchmark (TP communication pattern):
./build/all_reduce_perf -b 256K -e 4M -f 2 -g 4
# Tests all-reduce from 256 KB to 4 MB on 4 GPUs
# Expected: < 10 µs for 512 KB on intra-node XGMI

# Bandwidth test (sustained throughput):
./build/all_reduce_perf -b 128M -e 128M -n 1000 -g 8
# Should approach XGMI bandwidth (896 GB/s bus BW)
# Actual algorithm BW: ~800 GB/s on MI300X (ring all-reduce)

# What to look for:
  1. Latency for inference-sized messages (256KB-2MB): should be < 15 µs
  2. No unexpected drops (indicate XGMI link issues)
  3. Consistent across GPU pairs (no slow link)
  4. Compare ring vs tree for your TP group size:
     TP=2: ring always wins
     TP=4: ring usually wins
     TP=8: tree may win (fewer hops)

# CPU/GPU affinity validation:
rocm-smi --showtopo
# Verify GPUs are on expected NUMA nodes
# Ensure inference process pinned to correct NUMA node
```

## 18.4 Scaling Efficiency Metrics for Inference

```
Scaling efficiency = (TP=N throughput) / (N × TP=1 throughput) × 100%

Example (70B model on MI300X):
  TP=1: N/A (doesn't fit in FP16 on H100, but fits on MI300X!)
  TP=2 (on H100): 4,500 tok/s
  Expected linear: 2 × 2,500 = 5,000 tok/s
  Efficiency: 4,500 / 5,000 = 90%  (10% lost to communication)

  TP=4: 8,000 tok/s
  Expected: 4 × 2,500 = 10,000 tok/s
  Efficiency: 80%  (more sync points, more idle time)
  
  TP=8: 12,000 tok/s
  Expected: 8 × 2,500 = 20,000 tok/s
  Efficiency: 60%  (diminishing returns — communication dominates)

Rule of thumb:
  TP=2: 85-95% efficient (always acceptable)
  TP=4: 75-85% efficient (good for large models)
  TP=8: 55-70% efficient (only if model requires it)
  
  If scaling efficiency < 70%: model may be too small for that TP degree
  Better: use fewer GPUs + quantization (FP8) to fit model
```
