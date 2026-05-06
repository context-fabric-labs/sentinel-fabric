
# HPC Fraud Detection Pipeline — CUDA Profiling & Optimization Stories
## Interview-Ready: "What profiling did you do, what did you find, how did you optimize?"

> **Context:** Ultra-low-latency, high-throughput payment fraud detection pipeline
> with real-time scoring (Go/No-Go), reasoning for positive classifications,
> and curation to reduce false positives.

> **Architecture:**
> - **Tier 0:** Real-time fraud scoring lane (sub-5ms p99 SLA)
> - **Tier 1:** Reasoning lane for flagged transactions (2-5 second SLA)
> - **Tier 2:** Triage and curation lane (5-10 second SLA)

---

# Story 1: Profiling and Eliminating Kernel Launch Overhead with CUDA Graphs
## (CapitalOne — Real-Time Fraud Scoring Lane)

### Situation

We operated a real-time fraud scoring pipeline for payment authorization that
ran a neural scoring model (distilled transformer + XGBoost ensemble) on GPU
as part of the Go/No-Go decision path. The end-to-end latency SLA for the
GPU-accelerated scoring was **sub-5 ms at p99** — anything slower risked
degrading the payment authorization experience or forcing fallback to a
less-accurate CPU-only path.

During a capacity review, we noticed that even though individual kernel
execution times were fast, the **overall GPU inference time was higher
than expected**. The scoring model itself was small and well-optimized,
so the suspicion was that overhead outside of actual compute was the
bottleneck.

### Profiling Approach

#### Step 1 — Baseline measurement with application-level timing
We instrumented the scoring service with per-request CUDA event timing
around the full inference path (H2D transfer → model forward → D2H transfer):

```cpp
cudaEventRecord(start, stream);
// ... H2D + forward + D2H ...
cudaEventRecord(stop, stream);
cudaEventSynchronize(stop);
cudaEventElapsedTime(&ms, start, stop);
```

**Finding:** The end-to-end GPU time averaged ~1.2 ms, but with periodic
spikes to ~2.5 ms at p99. The model forward pass itself should have been
well under 1 ms for our batch sizes.

#### Step 2 — Nsight Systems timeline analysis
We captured a trace with Nsight Systems:

```bash
nsys profile --trace=cuda,osrt,nvtx -o fraud-scoring-baseline ./fraud-scoring-service
```

**Key findings from the timeline:**

1. **~45 small kernel launches per inference step** — the neural model
   forward pass decomposed into many tiny operations: RMSNorm, residual
   add, activation (SiLU), quantize, dequantize, attention projection
   GEMMs, output projection, plus the XGBoost tree traversal kernel.

2. **Visible gaps between kernels** — the timeline showed clear
   idle gaps (5-15 µs each) between successive kernel launches where
   the GPU was waiting for the next CPU-dispatched kernel.

3. **CPU-side dispatch dominated** — the host thread spent most of
   its time in CUDA runtime calls (kernel launch, stream synchronization),
   not in useful application logic.

4. **Total launch overhead: ~150 µs per inference** — across 45
   kernels at ~3-5 µs launch overhead each, the cumulative CPU-side
   dispatch cost was ~150 µs, representing roughly 12% of our
   latency budget.

#### Step 3 — Nsight Compute spot-check on individual kernels
We profiled a few of the small kernels with Nsight Compute to confirm
they were genuinely small (not secretly compute-heavy):

```bash
ncu --target-processes all --kernel-name "rms_norm" ./fraud-scoring-service
```

**Finding:** Individual kernels like RMSNorm and activation were completing
in 2-8 µs of GPU time, confirming the bottleneck was **launch overhead,
not kernel execution**.

#### Step 4 — Quantify the opportunity
We measured:
- Total kernel execution time (sum of all kernel durations): ~800 µs
- Total wall-clock GPU time (including gaps): ~1200 µs
- Gap time (launch overhead): ~400 µs at average, ~150 µs attributable
  to pure CPU dispatch, rest to stream synchronization and scheduling

### Analysis and Root Cause

The root cause was clear from the Nsight Systems timeline:

**PyTorch eager execution dispatches each operation as a separate CUDA
kernel launch.** Even though the scoring service used optimized custom
CUDA kernels for attention, normalization, and activation, the overall
forward pass was still orchestrated by Python/PyTorch eagerly — meaning
each of the ~45 ops was submitted to the GPU individually with CPU-side
overhead per launch.

The ~150 µs of pure launch overhead was:
- **Not from the model being slow** (kernels were fast)
- **Not from data transfer** (H2D/D2H was already using pinned memory)
- **Not from GPU saturation** (utilization was actually low during gaps)
- **Entirely from the CPU→GPU dispatch pattern**

This is a well-known problem in low-batch-size GPU inference: when
individual kernels are very small, the per-kernel launch cost (~3-5 µs
on the CPU side) becomes a significant fraction of the total time.

### Optimization: CUDA Graph Capture and Replay

#### Design
We implemented CUDA Graph capture for the entire scoring path:

```
[H2D copy] → [model forward (all 45 kernels)] → [D2H copy]
```

captured as a single graph during warmup, then replayed per micro-batch.

#### Implementation

**Step 1 — Preallocate all buffers**

```cpp
// Per-worker preallocated buffers (pinned host + device)
struct ScoringBuffers {
    // Pinned host buffers (allocated once at startup)
    float* h_features;        // pinned, for H2D
    float* h_scores;          // pinned, for D2H
    
    // Device buffers (allocated once at startup)
    float* d_features;        // GPU input
    float* d_scores;          // GPU output
    float* d_intermediates;   // scratch space for forward pass
    
    cudaStream_t stream;
    cudaGraph_t graph;
    cudaGraphExec_t graph_exec;
};

void init_buffers(ScoringBuffers& buf, int max_batch, int num_features) {
    // Pinned host memory — avoids hidden pageable→pinned staging copy
    cudaMallocHost(&buf.h_features, max_batch * num_features * sizeof(float));
    cudaMallocHost(&buf.h_scores, max_batch * sizeof(float));
    
    // Device memory — preallocated, no per-request malloc
    cudaMalloc(&buf.d_features, max_batch * num_features * sizeof(float));
    cudaMalloc(&buf.d_scores, max_batch * sizeof(float));
    cudaMalloc(&buf.d_intermediates, SCRATCH_SIZE);
    
    cudaStreamCreate(&buf.stream);
}
```

**Step 2 — Capture the graph during warmup**

```cpp
void capture_scoring_graph(ScoringBuffers& buf, Model& model, int batch_size) {
    // Warmup run (required before capture)
    run_scoring_forward(buf, model, batch_size);
    cudaStreamSynchronize(buf.stream);
    
    // Begin capture
    cudaStreamBeginCapture(buf.stream, cudaStreamCaptureModeGlobal);
    
    // H2D
    cudaMemcpyAsync(buf.d_features, buf.h_features,
                    batch_size * NUM_FEATURES * sizeof(float),
                    cudaMemcpyHostToDevice, buf.stream);
    
    // Model forward (all ~45 kernels captured into graph)
    model.forward(buf.d_features, buf.d_scores,
                  buf.d_intermediates, batch_size, buf.stream);
    
    // D2H
    cudaMemcpyAsync(buf.h_scores, buf.d_scores,
                    batch_size * sizeof(float),
                    cudaMemcpyDeviceToHost, buf.stream);
    
    // End capture
    cudaStreamEndCapture(buf.stream, &buf.graph);
    cudaGraphInstantiate(&buf.graph_exec, buf.graph, nullptr, nullptr, 0);
}
```

**Step 3 — Replay per micro-batch**

```cpp
float* score_batch(ScoringBuffers& buf, const float* features, int batch_size) {
    // Write features directly into pinned buffer (zero extra copy)
    memcpy(buf.h_features, features, batch_size * NUM_FEATURES * sizeof(float));
    
    // Replay the entire captured graph as ONE GPU submission
    cudaGraphLaunch(buf.graph_exec, buf.stream);
    cudaStreamSynchronize(buf.stream);
    
    return buf.h_scores;
}
```

#### Why this works
- **Before:** 45 separate CPU→GPU kernel submissions, each with ~3-5 µs overhead
- **After:** 1 graph launch that replays all 45 kernels as a single GPU submission
- CPU-side dispatch: O(layers × kernels) → O(1)

### Results (Before vs After)

| Metric | Before (eager) | After (CUDA Graph) | Improvement |
|---|---|---|---|
| Kernel launch overhead | ~150 µs | ~5 µs | **30x reduction** |
| p50 GPU inference time | 1.2 ms | 0.9 ms | 25% faster |
| p99 GPU inference time | 2.5 ms | 1.1 ms | 56% faster |
| End-to-end p99 (including network + feature fetch) | 5.8 ms | 3.8 ms | **Within SLA** |
| GPU idle gaps | Visible in timeline | Nearly eliminated | Continuous execution |

### Verification
After optimization, we re-profiled with Nsight Systems:

```bash
nsys profile --trace=cuda,osrt,nvtx -o fraud-scoring-optimized ./fraud-scoring-service
```

**Confirmed:**
- Timeline showed continuous kernel execution with no visible gaps
- Single graph launch replaced 45 individual launches
- CPU thread was free to do feature preparation for the next batch
  while the current graph was executing

### Learnings

1. **Profiling first, not guessing:** Without Nsight Systems, we would have
   assumed the model itself needed optimization. The timeline made it
   immediately obvious that launch overhead, not kernel compute, was the
   bottleneck.

2. **CUDA Graphs don't reduce HBM traffic:** If we had also seen unfused
   operation chains (e.g., separate RMSNorm → quantize → activate kernels
   each reading/writing HBM), we would have needed kernel fusion too.
   CUDA Graphs only eliminate launch overhead.

3. **Fixed shapes required:** CUDA Graphs capture a specific execution
   pattern. We handled variable batch sizes by capturing graphs for a
   few common batch sizes (1, 4, 8, 16) and padding smaller batches
   to the nearest captured size.

4. **Pinned memory is prerequisite:** CUDA Graphs with H2D/D2H require
   pinned host memory. Pageable memory would have added an implicit
   staging copy that defeats the purpose.

---

# Story 2: Profiling and Optimizing Throughput with Micro-Batching and Pinned Buffers
## (CapitalOne — Real-Time Fraud Scoring Lane, Phase 2)

### Situation

After Story 1's CUDA Graph optimization brought p99 latency within the
sub-5 ms SLA, the next challenge was **throughput**. The payment platform
was scaling to handle peak holiday traffic, and the fraud scoring GPU
tier needed to support **10,000+ requests/sec per GPU** while maintaining
the latency SLA.

At the time, the system was processing requests **one at a time** through
the CUDA Graph pipeline, achieving ~1,000 req/sec/GPU. GPU utilization
(SM occupancy) was only ~15-20% because each single-request inference
used a tiny fraction of the GPU's compute capacity.

### Profiling Approach

#### Step 1 — Nsight Systems timeline at production load
We profiled under realistic traffic:

```bash
nsys profile --trace=cuda,osrt,nvtx --duration=30     -o fraud-scoring-throughput ./fraud-scoring-service
```

**Key findings:**

1. **GPU was mostly idle between inferences** — each graph replay
   completed in ~0.9 ms, but the next request didn't arrive and get
   dispatched for another ~0.5-1.0 ms, leaving the GPU idle ~50% of
   the time.

2. **SM utilization was low during execution** — even during the
   forward pass, only 15-20% of SMs were active because the batch
   size was 1. The GPU's massive parallelism was underutilized.

3. **H2D transfers were tiny** — single-request feature vectors
   were only a few KB, far below the threshold where PCIe bandwidth
   becomes relevant. The transfer was latency-dominated, not
   bandwidth-dominated.

4. **DCGM metrics confirmed low utilization:**
   ```
   GPU Utilization: 18%
   Memory Bandwidth Utilization: 5%
   Tensor Core Utilization: 12%
   ```

#### Step 2 — Roofline analysis
Using Nsight Compute on the forward pass kernels:

```bash
ncu --set roofline --target-processes all ./fraud-scoring-service
```

**Finding:** The GEMM kernels were well below the roofline — they had
enough arithmetic intensity to benefit from larger batch sizes, but at
batch=1 they were memory-latency-bound rather than compute-bound.

### Analysis and Root Cause

The root cause was straightforward:

**Processing one request at a time wastes GPU parallelism.** The GPU has
thousands of CUDA cores and hundreds of Tensor Cores, but a single
fraud-scoring inference for one transaction uses only a tiny fraction
of that capacity. The GPU is designed for throughput — it needs enough
work to fill its execution units.

However, we could not simply batch 100 requests and wait — that would
add **queueing latency** and violate the sub-5 ms SLA.

The challenge was: **increase batch size for throughput WITHOUT
increasing latency beyond the SLA.**

### Optimization: Bounded Micro-Batching with Deadline-Aware Dispatch

#### Design

We implemented a **micro-batching layer** between the ingress dispatcher
and the GPU scoring path:

```
[Ingress threads]
    ↓ (SPSC descriptor rings, per-worker)
[Micro-batch accumulator]
    ↓ (bounded by time window AND batch size)
[CUDA Graph replay]
    ↓
[Score distribution]
```

**Key design decisions:**

1. **Bounded time window:** Maximum 500 µs accumulation window.
   If 500 µs passes, dispatch whatever is accumulated (even batch=1).

2. **Bounded batch size:** Maximum 16 requests per micro-batch.
   If 16 requests arrive before the time window expires, dispatch
   immediately.

3. **Deadline-aware:** Each request carries a deadline timestamp.
   If the oldest request in the accumulator is within 1 ms of its
   deadline, dispatch immediately regardless of batch fill.

#### Implementation

**Per-worker buffer pool:**

```cpp
struct WorkerBuffers {
    // Pre-captured CUDA graphs for common batch sizes
    cudaGraphExec_t graphs[5];  // batch 1, 4, 8, 12, 16
    
    // Pinned host staging — large enough for max batch
    float* h_features;   // [MAX_BATCH × NUM_FEATURES], pinned
    float* h_scores;     // [MAX_BATCH], pinned
    
    // Device buffers
    float* d_features;
    float* d_scores;
    float* d_scratch;
    
    cudaStream_t stream;
    
    // Accumulator state
    int current_count;
    uint64_t oldest_deadline_ns;
    uint64_t window_start_ns;
};
```

**Micro-batch accumulator logic:**

```cpp
void accumulator_loop(WorkerBuffers& buf, SPSCQueue& rx) {
    buf.current_count = 0;
    buf.window_start_ns = now_ns();
    
    while (true) {
        TxnDescriptor desc;
        bool got = rx.try_pop(desc);
        
        if (got) {
            // Write features directly into pinned buffer slot
            // (zero-copy from descriptor → pinned staging)
            copy_features_to_slot(buf.h_features, buf.current_count, desc);
            buf.current_count++;
            
            if (buf.current_count == 1) {
                buf.oldest_deadline_ns = desc.deadline_ns;
                buf.window_start_ns = now_ns();
            }
        }
        
        bool should_dispatch =
            buf.current_count >= MAX_BATCH_SIZE ||
            (buf.current_count > 0 && (now_ns() - buf.window_start_ns) > MAX_WINDOW_NS) ||
            (buf.current_count > 0 && (buf.oldest_deadline_ns - now_ns()) < DEADLINE_MARGIN_NS);
        
        if (should_dispatch && buf.current_count > 0) {
            dispatch_batch(buf);
            buf.current_count = 0;
        }
        
        if (!got && buf.current_count == 0) {
            _mm_pause();  // brief spin, not full yield
        }
    }
}

void dispatch_batch(WorkerBuffers& buf) {
    // Select the right pre-captured graph for this batch size
    int graph_idx = select_graph_for_size(buf.current_count);
    
    // Launch the graph (H2D + forward + D2H in one submission)
    cudaGraphLaunch(buf.graphs[graph_idx], buf.stream);
    cudaStreamSynchronize(buf.stream);
    
    // Distribute scores back to waiting request contexts
    distribute_scores(buf.h_scores, buf.current_count);
}
```

**Stream and pinned memory design:**

```cpp
void init_worker(WorkerBuffers& buf) {
    // Pin this worker thread to an isolated CPU
    pin_to_cpu(worker_cpu);
    
    // Allocate pinned host memory on the NUMA node closest to the GPU
    cudaMallocHost(&buf.h_features, MAX_BATCH * NUM_FEATURES * sizeof(float));
    cudaMallocHost(&buf.h_scores, MAX_BATCH * sizeof(float));
    
    // Device allocations
    cudaMalloc(&buf.d_features, MAX_BATCH * NUM_FEATURES * sizeof(float));
    cudaMalloc(&buf.d_scores, MAX_BATCH * sizeof(float));
    
    // Dedicated stream per worker
    cudaStreamCreateWithPriority(&buf.stream, cudaStreamNonBlocking, 0);
    
    // Capture graphs for common batch sizes
    for (int bs : {1, 4, 8, 12, 16}) {
        capture_graph_for_batch_size(buf, bs);
    }
}
```

### Results (Before vs After)

| Metric | Before (batch=1) | After (micro-batch) | Improvement |
|---|---|---|---|
| Throughput | 1,000 req/sec/GPU | 10,000 req/sec/GPU | **10x** |
| p50 latency | 0.9 ms | 1.2 ms | +0.3 ms (acceptable) |
| p99 latency | 1.1 ms | 3.2 ms | +2.1 ms (within 5 ms SLA) |
| GPU SM utilization | 18% | 72% | **4x better utilization** |
| GPU memory bandwidth util | 5% | 35% | **7x better** |
| Tensor Core utilization | 12% | 58% | **4.8x better** |

### Verification with Nsight Systems

Post-optimization Nsight Systems trace showed:
- **Continuous GPU execution** — no idle gaps between batches
- **Larger, more efficient kernels** — GEMMs with batch=16 used
  Tensor Cores much more effectively than batch=1
- **H2D transfers amortized** — one 16-request transfer instead of
  16 single-request transfers
- **CPU thread utilization improved** — accumulator logic ran
  concurrently with GPU execution

### Learnings

1. **Micro-batching is the single biggest throughput lever** for
   GPU inference in low-latency systems. The GPU is a throughput
   engine — feeding it one request at a time is like using a
   highway with one car.

2. **Bounded time windows preserve latency discipline.** The key
   insight is that you don't need large batches — even batch=4 or
   batch=8 dramatically improves GPU utilization while adding only
   hundreds of microseconds of queueing delay.

3. **Pre-captured CUDA Graphs for multiple batch sizes** avoid the
   overhead of dynamic graph capture while supporting variable load.

4. **Pinned memory + dedicated streams per worker** ensure that
   the micro-batching layer itself doesn't introduce new bottlenecks.

---

# Story 3: Profiling and Optimizing H2D Transfer with Pinned Memory and Async DMA
## (Fiserv — Reasoning Lane for Flagged Transactions)

### Situation

For transactions flagged as potential fraud (the "No-Go" path from the
real-time lane), we operated a **reasoning service** that ran a larger
transformer model to generate explanations and confidence assessments.
This lane had a **2-5 second SLA** — much more relaxed than the real-time
lane, but the model was also much larger (a fine-tuned 7B parameter model
running on vLLM-style infrastructure).

During initial deployment, we observed that **GPU utilization was
surprisingly low (~35%)** even under moderate load, and **TTFT (Time To
First Token) was higher than expected** for the model size.

### Profiling Approach

#### Step 1 — Nsight Systems end-to-end trace

```bash
nsys profile --trace=cuda,osrt,nvtx,cudnn,cublas     --gpu-metrics-device=all -o reasoning-baseline ./reasoning-service
```

**Key findings from the timeline:**

1. **Large gaps between H2D transfers and kernel execution** — the
   timeline showed the GPU waiting for data transfers to complete
   before starting compute. Transfers and compute were **serialized,
   not overlapped**.

2. **H2D transfers were slow** — feature context payloads (transaction
   history, merchant embeddings, tokenized text) were 2-8 MB per
   request, and transfer times were 800 µs - 2.5 ms per request.

3. **Implicit staging copies detected** — Nsight Systems showed
   `cudaMemcpy` (synchronous) calls and internal staging buffer
   allocations, indicating the application was using **pageable
   host memory**, causing the CUDA runtime to internally copy to
   a pinned staging buffer before DMA.

4. **GPU SM utilization timeline** confirmed the GPU was idle during
   transfers and only active during forward pass execution.

#### Step 2 — Nsight Compute on transfer operations

We used Nsight Compute's memory throughput analysis on the prefill
kernels:

```bash
ncu --set full --kernel-name "flash_attn" ./reasoning-service
```

**Finding:** The attention kernels themselves were well-optimized
(achieving ~70% of peak HBM bandwidth). The bottleneck was not
kernel efficiency — it was the **pipeline stall** from serialized
transfers.

#### Step 3 — perf stat on the host side

```bash
perf stat -e context-switches,cpu-migrations,dTLB-load-misses     -p $(pgrep reasoning) -- sleep 30
```

**Finding:** Elevated context switches on the transfer thread,
suggesting the synchronous `cudaMemcpy` calls were blocking the
thread and allowing scheduler interference.

### Analysis and Root Cause

Three problems compounded:

1. **Pageable host memory → implicit staging copy:**
   When `cudaMemcpy` (or `cudaMemcpyAsync` on pageable memory) is
   used, the CUDA runtime must first copy data from the pageable
   user buffer into an internal pinned staging buffer, then DMA
   from that staging buffer to the GPU. This doubles the host-side
   memory bandwidth consumed and adds latency.

2. **Synchronous transfers → no overlap:**
   The code used `cudaMemcpy` (blocking) instead of `cudaMemcpyAsync`
   with streams. This meant the CPU thread blocked until the transfer
   completed, and the GPU could not start compute until the transfer
   finished.

3. **Per-request buffer allocation:**
   Each request allocated a new host buffer with `malloc`, used it
   for one transfer, then freed it. This added allocator overhead
   and prevented the CUDA runtime from optimizing DMA paths.

### Optimization: Pinned Buffer Pool + Async DMA + Stream Pipelining

#### Implementation

**Step 1 — Pinned buffer pool**

```cpp
class PinnedBufferPool {
    struct Slot {
        void* host_ptr;      // pinned
        void* device_ptr;    // pre-allocated device buffer
        cudaStream_t stream;
        cudaEvent_t ready;
        bool in_use;
    };
    
    std::vector<Slot> slots;
    
public:
    PinnedBufferPool(int num_slots, size_t slot_size) {
        slots.resize(num_slots);
        for (auto& s : slots) {
            cudaMallocHost(&s.host_ptr, slot_size);  // pinned
            cudaMalloc(&s.device_ptr, slot_size);
            cudaStreamCreate(&s.stream);
            cudaEventCreate(&s.ready);
            s.in_use = false;
        }
    }
    
    Slot* acquire() {
        for (auto& s : slots) {
            if (!s.in_use) {
                s.in_use = true;
                return &s;
            }
        }
        return nullptr;  // all slots busy
    }
    
    void release(Slot* s) {
        s->in_use = false;
    }
};
```

**Step 2 — Async transfer + compute overlap**

```cpp
void process_request(PinnedBufferPool& pool, Model& model,
                     const RequestContext& ctx) {
    auto* slot = pool.acquire();
    
    // 1. Write directly into pinned buffer (zero staging copy)
    prepare_model_input(slot->host_ptr, ctx);
    
    // 2. Async H2D on dedicated stream
    cudaMemcpyAsync(slot->device_ptr, slot->host_ptr,
                    ctx.input_size,
                    cudaMemcpyHostToDevice, slot->stream);
    
    // 3. Launch model forward on same stream (auto-waits for H2D)
    model.forward_async(slot->device_ptr, slot->device_ptr,
                        ctx.seq_len, slot->stream);
    
    // 4. Async D2H for output
    cudaMemcpyAsync(slot->host_ptr, slot->device_ptr,
                    ctx.output_size,
                    cudaMemcpyDeviceToHost, slot->stream);
    
    // 5. Record completion event
    cudaEventRecord(slot->ready, slot->stream);
    
    // 6. CPU is FREE to prepare next request while GPU works
    // ... (prepare next request, fetch features, etc.)
    
    // 7. Wait for completion when result is needed
    cudaEventSynchronize(slot->ready);
    
    // 8. Read results from pinned buffer
    extract_scores(slot->host_ptr, ctx);
    
    pool.release(slot);
}
```

**Step 3 — Multi-slot pipelining**

```
Time →
Slot 0: [H2D_0] [Forward_0] [D2H_0]
Slot 1:    [H2D_1] [Forward_1] [D2H_1]
Slot 2:       [H2D_2] [Forward_2] [D2H_2]

Different streams → GPU can overlap transfers and compute
```

### Results (Before vs After)

| Metric | Before | After | Improvement |
|---|---|---|---|
| H2D transfer time (2 MB) | 850 µs | 265 µs | **3.2x faster** |
| GPU utilization | 35% | 85% | **2.4x better** |
| TTFT (p50) | 180 ms | 95 ms | **47% faster** |
| TTFT (p99) | 450 ms | 160 ms | **64% faster** |
| Throughput (req/sec) | 45 | 120 | **2.7x higher** |
| Host-side context switches | 1,200/sec | 180/sec | **85% reduction** |

### Verification

Post-optimization Nsight Systems trace confirmed:
- **No implicit staging copies** — all transfers went directly from
  pinned host memory to device via DMA
- **Overlapped execution** — while one slot's forward pass ran on
  the GPU, another slot's H2D transfer was in progress on a different
  stream
- **Continuous GPU activity** — SM utilization timeline showed
  near-continuous execution with minimal idle gaps
- **Reduced CPU blocking** — the host thread spent less time waiting
  in synchronous CUDA calls

### Learnings

1. **Pinned memory is not optional for serious GPU serving.** The
   implicit staging copy from pageable memory is a hidden tax that
   roughly doubles H2D latency for medium-to-large payloads.

2. **Async transfers + multiple streams enable pipelining.** The GPU
   has separate copy engines and compute engines — they can run
   concurrently, but only if you use async APIs and separate streams.

3. **Buffer pooling eliminates allocator jitter.** Pre-allocating
   pinned buffers at startup and reusing them removes both the
   `cudaMallocHost` latency and the allocator contention that
   comes with per-request allocation.

4. **NUMA placement of pinned buffers matters.** We verified with
   `numastat` that pinned buffers were allocated on the NUMA node
   closest to the GPU's PCIe root complex. Remote NUMA allocation
   would have added ~30% to transfer times.

---

# Story 4: Profiling and Optimizing KV-Cache Routing for Multi-Turn Reasoning
## (Fiserv — Reasoning + Triage Lanes)

### Situation

The reasoning lane (Tier 1, 2-5 sec SLA) and triage lane (Tier 2,
5-10 sec SLA) both used a fine-tuned 7B model for generating fraud
explanations, confidence assessments, and recommended actions. Many
fraud investigations involved **multi-turn interactions**: an initial
assessment, follow-up queries with additional context, and iterative
refinement.

We ran the model on a fleet of **8 GPU workers** behind a load balancer.
The problem: **every follow-up request in a multi-turn session was
treated as a brand-new request**, meaning the model had to re-process
the entire conversation history (system prompt + prior turns) from
scratch every time.

### Profiling Approach

#### Step 1 — Application-level metrics
We instrumented TTFT (Time To First Token) broken down by request type:

| Request type | Avg context length | TTFT p50 | TTFT p99 |
|---|---|---|---|
| First turn (new session) | 2K tokens | 95 ms | 160 ms |
| Follow-up turn (3rd msg) | 8K tokens | 320 ms | 580 ms |
| Deep investigation (8th msg) | 25K tokens | 980 ms | 1,800 ms |

**Finding:** Follow-up turns were dramatically slower because the
entire conversation prefix was being re-prefilled every time.

#### Step 2 — Nsight Systems on a follow-up request

```bash
nsys profile --trace=cuda,nvtx -o reasoning-followup ./reasoning-service
```

**Key finding:** The prefill phase for an 8K-token follow-up request
showed the GPU processing all 8K tokens through all model layers,
even though **6K of those tokens were identical to the previous turn's
prefix**. That was ~75% redundant compute.

#### Step 3 — GPU memory analysis with DCGM/NVML

```bash
dcgmi dmon -e 252,253 -d 1000  # GPU memory used/free
```

**Finding:** Each worker's KV cache was filling up independently.
Worker A might have session X's KV cache, but the load balancer
could route session X's next request to Worker B, which had no
cached state for that session. Worker B would then recompute
everything from scratch while Worker A's cached KV state sat
unused and eventually got evicted.

#### Step 4 — Cache hit rate measurement
We added KV cache hit/miss counters to the serving engine:

```
KV cache hit rate: 12%
KV cache eviction rate: 45 evictions/sec
Redundant prefill compute: ~75% of total prefill FLOPS
```

### Analysis and Root Cause

Three compounding problems:

1. **Round-robin load balancing** ignored session affinity. Follow-up
   requests for the same fraud investigation session could land on
   any of the 8 workers, usually one that had no cached KV state
   for that session.

2. **No prefix sharing across requests.** Even when different sessions
   shared the same system prompt (which was ~1K tokens), each session's
   first request recomputed the system prompt KV from scratch.

3. **KV cache pressure from scattered sessions.** Because sessions
   were scattered across workers, each worker held partial KV state
   for many sessions rather than complete KV state for fewer sessions.
   This increased eviction pressure and reduced effective cache size.

### Optimization: Session-Aware KV-Cache Routing

#### Design

We implemented a **KV-aware routing layer** between the load balancer
and the GPU workers:

```
[Incoming request]
    ↓
[KV-Aware Router]
    ├── Check: does any worker hold KV cache for this session?
    │   YES → route to that worker (if it has capacity)
    │   NO  → route to worker with best GPU headroom
    ├── Check: shared prefix (system prompt) cached anywhere?
    │   YES → prefer that worker
    │   NO  → any worker with capacity
    └── Fallback: least-loaded worker
```

#### Implementation

**Router state (lightweight, in-memory):**

```cpp
struct WorkerState {
    int worker_id;
    float gpu_memory_used_pct;
    float gpu_memory_free_gb;
    int active_sequences;
    std::unordered_map<std::string, KVCacheEntry> cached_sessions;
};

struct KVCacheEntry {
    std::string session_id;
    int cached_token_count;
    uint64_t last_access_ns;
    float estimated_kv_size_mb;
};

struct RoutingDecision {
    int target_worker;
    float kv_overlap_ratio;
    float estimated_prefill_savings_ms;
};
```

**Routing logic:**

```cpp
RoutingDecision route_request(const Request& req,
                              const std::vector<WorkerState>& workers) {
    RoutingDecision best;
    best.target_worker = -1;
    float best_score = -1.0f;
    
    for (const auto& w : workers) {
        float score = 0.0f;
        float overlap = 0.0f;
        
        // Factor 1: KV cache overlap (highest weight)
        auto it = w.cached_sessions.find(req.session_id);
        if (it != w.cached_sessions.end()) {
            overlap = (float)it->second.cached_token_count / req.total_context_tokens;
            score += overlap * 10.0f;  // strong preference for cache hit
        }
        
        // Factor 2: GPU memory headroom
        float headroom = w.gpu_memory_free_gb / TOTAL_GPU_MEMORY_GB;
        if (headroom < MIN_HEADROOM_RATIO) {
            continue;  // skip overloaded workers
        }
        score += headroom * 3.0f;
        
        // Factor 3: Active sequence count (prefer less loaded)
        float load = 1.0f - ((float)w.active_sequences / MAX_SEQUENCES);
        score += load * 2.0f;
        
        if (score > best_score) {
            best_score = score;
            best.target_worker = w.worker_id;
            best.kv_overlap_ratio = overlap;
            best.estimated_prefill_savings_ms =
                overlap * req.total_context_tokens * PREFILL_MS_PER_TOKEN;
        }
    }
    
    return best;
}
```

**Worker-side KV cache management:**

```cpp
// Workers report their KV cache state periodically
void report_kv_state(WorkerState& state) {
    // Query vLLM/engine for active KV blocks
    auto kv_info = engine.get_kv_cache_info();
    
    state.gpu_memory_used_pct = get_gpu_memory_used_pct();
    state.gpu_memory_free_gb = get_gpu_memory_free_gb();
    state.active_sequences = kv_info.active_sequences;
    
    state.cached_sessions.clear();
    for (const auto& seq : kv_info.sequences) {
        state.cached_sessions[seq.session_id] = {
            .session_id = seq.session_id,
            .cached_token_count = seq.cached_tokens,
            .last_access_ns = seq.last_access_ns,
            .estimated_kv_size_mb = seq.kv_size_mb
        };
    }
}
```

### Results (Before vs After)

| Metric | Before (round-robin) | After (KV-aware) | Improvement |
|---|---|---|---|
| KV cache hit rate | 12% | 70% | **5.8x better** |
| Follow-up TTFT (p50) | 320 ms | 85 ms | **73% faster** |
| Follow-up TTFT (p99) | 580 ms | 180 ms | **69% faster** |
| Deep investigation TTFT (p50) | 980 ms | 210 ms | **78% faster** |
| GPU memory pressure (evictions/sec) | 45 | 12 | **73% reduction** |
| Redundant prefill FLOPS | ~75% | ~20% | **73% reduction** |
| GPU utilization (effective) | 35% | 65% | **86% better** |

### Verification

#### Application-level
```
KV cache hit rate: 70% (up from 12%)
Session affinity rate: 92% (follow-ups hit same worker)
System prompt cache hit: 99% (shared across all sessions on each worker)
```

#### GPU-level (DCGM)
```
GPU memory utilization: more stable, fewer spikes
Prefill compute per request: ~3.5x lower on cache-hit requests
```

#### Nsight Systems (follow-up request trace)
The timeline for a follow-up request showed:
- **Prefill phase only processed new tokens** (the delta from the
  previous turn), not the entire history
- **KV cache read** replaced **KV cache compute** for the cached prefix
- Total prefill time dropped from ~320 ms to ~85 ms

### Learnings

1. **KV cache routing is a system-design optimization, not a kernel
   optimization.** The GPU kernels themselves were already fast — the
   problem was that we were running them on data that had already
   been computed elsewhere. The fix was in the routing layer, not
   the CUDA code.

2. **Session affinity is the highest-leverage routing signal.** Even
   a simple "sticky session" policy would have captured most of the
   benefit. The full scoring function (overlap + headroom + load)
   added refinement but the core win was just keeping sessions on
   the same worker.

3. **Shared prefix caching compounds with session routing.** Once
   sessions were sticky, the system prompt KV cache (shared across
   all sessions on a worker) achieved near-100% hit rate, further
   reducing per-session overhead.

4. **GPU memory pressure tracking prevents cascading failures.**
   Without headroom-aware routing, a worker could accept too many
   sessions, run out of KV cache space, start evicting actively-used
   sessions, and enter a "thrashing" state where every request
   triggered a full re-prefill. The headroom factor in the routing
   score prevented this.

---

# How These Stories Connect

These four stories form a **complete optimization narrative** across the
three tiers of the fraud detection pipeline:

```
Story 1 (CUDA Graphs)     → Tier 0 real-time lane: eliminate launch overhead
Story 2 (Micro-batching)  → Tier 0 real-time lane: maximize GPU throughput
Story 3 (Pinned + Async)  → Tier 1 reasoning lane: optimize data movement
Story 4 (KV-Cache Router) → Tier 1+2 reasoning/triage: eliminate redundant compute
```

Together, they demonstrate:
- **Profiling-first methodology** (Nsight Systems → Nsight Compute → perf stat → DCGM)
- **Different bottleneck types** (launch overhead, utilization, transfer, redundant compute)
- **Different optimization strategies** (CUDA Graphs, batching, pinned memory, routing)
- **System-level thinking** (not just kernel optimization, but scheduling and routing)

Each story follows the same structure:
1. **Situation:** what was the problem and why it mattered
2. **Profiling:** what tools were used and what they revealed
3. **Analysis:** root cause identification with evidence
4. **Optimization:** what was changed and why
5. **Results:** before/after measurements
6. **Learnings:** what surprised us or what we'd do differently
