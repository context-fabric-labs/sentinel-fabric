
# Layer 6: AI Model Serving & Inference — Complete Deep Dive
## "How do we run models fast and reliably in production?"

> **Why this layer matters:** This is where AI/HPC rubber meets the road.
> You can have perfect GPU kernels (Layer 4) and zero-copy data flow
> (Layer 5), but if you serve the model wrong — wrong batching, wrong
> runtime, wrong memory management — latency and throughput suffer.
>
> **The core insight:** The serving pattern depends on the model type.
> Small stateless models → embed in-process. Large stateful models
> (LLMs with KV cache) → serve separately. The wrong choice wastes
> either latency (unnecessary network hops) or resources (duplicate
> GPU memory).

---

# SECTION 1: MODEL SERVING PATTERNS

---

## 1.1 In-Process Embedding (for hot-path models)

### When to use
- Model fits comfortably in GPU memory alongside other models
- Model is STATELESS (no KV cache, no growing memory)
- Latency budget is tight (10-30 ms, can't afford network hop)
- Model is called on every request (100% of traffic)

### Models that fit this pattern
```
XGBoost         → C API          → ~0.3-1 ms    → in-process
ONNX Runtime    → C++ API        → ~2-10 ms     → in-process
FAISS           → C++ library    → ~0.5-1 ms    → in-process
Rules engine    → compiled C++   → ~0.01-0.1 ms → in-process
Small BERT      → ONNX Runtime   → ~8-15 ms     → in-process
```

### The architecture
```cpp
class ScoringEngine {
    BoosterHandle xgb;                    // XGBoost C API
    std::unique_ptr<Ort::Session> bert;   // ONNX Runtime C++ API
    std::unique_ptr<faiss::Index> index;  // FAISS C++ library
    RulesEngine rules;                    // compiled C++

    struct WorkerBuffers { /* pre-allocated per worker */ };
    std::vector<WorkerBuffers> workers;

    float score(int worker_id, const Features& features) {
        auto& buf = workers[worker_id];
        // ALL models read from SAME feature block — zero copy
        float xgb_score = run_xgboost(buf);
        float bert_score = run_bert(buf);        // GPU
        float faiss_score = run_faiss_search(buf); // GPU (zero D2H!)
        float rules_score = rules.evaluate(buf);
        return weighted_combine(xgb_score, bert_score, faiss_score, rules_score);
    }
};
```

### Why this works
- **Zero network overhead** — function calls, not gRPC
- **Zero serialization** — shared feature buffer
- **CPU ∥ GPU parallel** — XGBoost+rules on CPU while BERT+FAISS on GPU
- **Pre-allocated buffers** — no per-request allocation

---

## 1.2 Separate Service (for LLMs)

### When to use
- Model is LARGE (7B+ parameters, 10+ GB GPU memory)
- Model is STATEFUL (KV cache grows/shrinks per request)
- Model benefits from cross-request batching (continuous batching)
- Latency budget allows network hop (> 1 sec SLA)

### Models that fit this pattern
```
7B LLM (vLLM)          → HTTP/gRPC    → ~100-500 ms TTFT
7B LLM (TensorRT-LLM)  → HTTP/gRPC    → ~50-200 ms TTFT
13B+ LLM               → HTTP/gRPC    → ~200-1000 ms TTFT
```

### Why separate service for LLMs
1. **KV cache management** — paged allocation, eviction, offload
2. **Cross-request batching** — inflight/continuous batching
3. **Prefix caching** — shared system prompt KV reuse
4. **GPU memory isolation** — LLM doesn't compete with hot-path models
5. **Independent scaling** — add GPU capacity for LLM without touching hot path

---

## 1.3 Multi-Task Model (shared backbone, multiple heads)

### The optimization
Instead of 3 separate BERT models (intent + NER + embedding):
- ONE shared backbone → ONE forward pass
- Three lightweight heads on top
- 3× less GPU memory, 3× less compute, same accuracy

```
3 separate models:           1 multi-task model:
  BERT → intent  (8 ms)       BERT backbone (8 ms)
  BERT → NER     (8 ms)         → intent head  (+0.01 ms)
  BERT → embed   (6 ms)         → NER head     (+0.01 ms)
  Total: 22 ms                   → embed head   (+0.01 ms)
  GPU mem: ~1.5 GB              Total: ~8 ms
                                GPU mem: ~300 MB
```

---

# SECTION 2: LLM INFERENCE RUNTIMES

---

## 2.1 The Runtime Landscape

| Runtime | Language | Best for | Key feature |
|---|---|---|---|
| **vLLM** | Python | Easy deployment, broad model support | PagedAttention, APC |
| **TensorRT-LLM** | C++ | Max NVIDIA GPU performance | Compiled engine, FP8, C++ API |
| **SGLang** | Python | Prefix-heavy workloads | RadixAttention tree cache |
| **llama.cpp** | C/C++ | Embed in C++ process, edge | GGUF quantization, no deps |
| **NVIDIA Dynamo** | Multi | Fleet-scale serving | KV-aware routing, disaggregation |

## 2.2 Key LLM Serving Optimizations

### Continuous / Inflight Batching
```
WITHOUT (static batching):
  Batch = [A(100 tok), B(500 tok), C(50 tok)]
  C finishes at 50 → GPU slot WASTED until B finishes at 500
  
WITH (inflight batching):
  C finishes at 50 → slot freed → D enters immediately
  GPU always full, no head-of-line blocking
```

### Prefix Caching
```
System prompt (500 tokens) → computed ONCE, cached
Request 1: [cached 500 tokens] + [new 200 tokens] → prefill only 200
Request 2: [cached 500 tokens] + [new 150 tokens] → prefill only 150
Savings: 500 tokens × N requests worth of prefill compute
```

### KV Cache Quantization
```
FP16 KV: 7B model, 2048 context → ~6 GB per sequence
FP8 KV:  same → ~3 GB per sequence (2× more concurrent users)
INT8 KV: same → ~3 GB per sequence
```

### KV-Aware Routing (Dynamo)
```
Request with session_id=X arrives
Router checks: which worker has session X's KV cached?
  Worker 3 has it → route to Worker 3 → skip re-prefill
  
Without routing: every follow-up re-prefills entire history
With routing:    follow-ups reuse cached KV → 50-80% TTFT reduction
```

---

# SECTION 3: SEARCH & RETRIEVAL

---

## 3.1 Two-Stage Pipeline (Retrieve → Rank)

```
RETRIEVAL (recall-focused, fast, approximate):
  5M documents → top 50-100 candidates
  Methods: BM25 (keyword) + FAISS GPU (semantic)
  Latency: ~8 ms (parallel BM25 ∥ FAISS)

RANKING (precision-focused, slower, richer features):
  50-100 candidates → scored with ranking model → top 5-10
  Method: MLP or small transformer on GPU (batched)
  Latency: ~0.8 ms (GPU batched forward pass)
```

## 3.2 Hybrid Retrieval (BM25 + Semantic)

```
BM25 (keyword):  matches exact words → great for specific terms
FAISS (semantic): matches meaning → great for paraphrases/concepts
RRF fusion:       documents ranked high in BOTH get highest score

"cheap eats near me":
  BM25 finds: documents with "cheap" and "eats"
  FAISS finds: documents about "affordable restaurants"
  Fusion: documents about affordable restaurants with matching keywords → TOP
```

## 3.3 Embedding Training

```
Dual-encoder architecture:
  Query encoder:    "cheap eats near me" → query_vector (768D)
  Document encoder: "Affordable dining guide" → doc_vector (768D)

Training:
  Positive pairs: (query, clicked_document) → push vectors CLOSE
  Hard negatives: (query, keyword_match_but_irrelevant) → push APART
  Loss: contrastive (InfoNCE)

Evaluation:
  Recall@k, NDCG@k, online A/B tests
```

---

# SECTION 4: THE COMPLETE SERVING ARCHITECTURE

```
HOT PATH (10-30 ms):
┌──────────────────────────────────────────────┐
│ ALL IN-PROCESS (C++ / Rust)                   │
│                                              │
│ XGBoost (CPU)  ∥  Multi-task BERT (GPU)      │
│ Rules (CPU)    ∥  FAISS GPU search           │
│                ∥  GPU-batched ranking         │
│                                              │
│ Shared feature block → all models read same  │
│ Pre-allocated buffers → zero allocation      │
│ CUDA Graphs → minimal launch overhead        │
│ Parallel streams → CPU ∥ GPU concurrent      │
└──────────────────────────────────────────────┘
        │ FLAGGED (Arrow IPC, shared memory)
        ↓
WARM PATH (2-5 sec):
┌──────────────────────────────────────────────┐
│ LLM AS SEPARATE SERVICE                       │
│                                              │
│ Context builder reads Arrow (zero-copy)       │
│ → UDS to co-located TensorRT-LLM / vLLM     │
│ → Prefix caching (skip system prompt prefill) │
│ → KV-aware routing (reuse session KV)        │
│ → FP8 quantization (2× concurrent capacity)  │
│ → Streaming → TTS (hide generation latency)  │
└──────────────────────────────────────────────┘
```

---

# SECTION 5: INTERVIEW CHEAT SHEET

## "How do you serve multiple models without network overhead?"
→ Embed all hot-path models in one C++ process: XGBoost C API, ONNX Runtime
   C++ API, FAISS C++ library. All read from same feature block. Zero
   serialization, zero network hops.

## "Why not embed the LLM in-process too?"
→ LLM is stateful (KV cache management), benefits from cross-request
   batching, and uses 10+ GB GPU memory. Separate service gives better
   utilization, isolation, and scaling.

## "How do you optimize LLM serving?"
→ Prefix caching (skip shared prompt prefill), inflight batching (no
   head-of-line blocking), KV quantization (2× capacity), KV-aware
   routing (reuse cached state), CUDA Graphs (decode launch overhead).

## "What is the difference between retrieval and ranking?"
→ Retrieval = recall (cast wide net, find candidates from millions).
   Ranking = precision (score candidates with richer features, pick top-k).
   Two stages because retrieval must be fast (sub-ms per candidate) while
   ranking can be richer (one batched GPU forward pass for all candidates).

## "How do you train embeddings for search?"
→ Dual-encoder with contrastive loss on query-document pairs. Hard
   negatives from BM25 (keyword match but irrelevant). Evaluate with
   Recall@k, NDCG@k, and online A/B tests.

---
---

# SECTION 6: REQUEST BATCHING STRATEGIES (Deep Dive)

---

## 6.1 Why Batching Is the Most Important Optimization

```
Single request inference:
  GPU utilization: 5-15% (GPU mostly idle waiting for memory)
  Tokens/sec: 50 (one sequence, memory-bandwidth-limited)
  Cost per token: $$$

Batched inference (batch=32):
  GPU utilization: 70-90% (SMs fully occupied)
  Tokens/sec: 1,200 (amortize weight reads across batch)
  Cost per token: $/24 (24× cheaper!)

Why batching helps:
  Model weights: read from HBM ONCE, applied to ALL batch items
  Without batch: read 14 GB weights, compute 1 result (waste bandwidth)
  With batch=32: read 14 GB weights, compute 32 results (share bandwidth)
  
  Arithmetic intensity goes UP with batch size:
  Single: 2 FLOPS per byte read (memory-bound)
  Batch=32: 64 FLOPS per byte read (approaching compute-bound!)
```

## 6.2 Batching Taxonomy

```
STATIC BATCHING (simplest, worst):
  Wait for N requests (or timeout) → process all → return all
  
  Problems:
  - Head-of-line blocking: short request waits for long request
  - Timeout waste: if traffic is low, you wait for nothing
  - Fixed batch size: can't adapt to traffic patterns
  
  Only acceptable for: synchronous offline batch processing

DYNAMIC BATCHING (better — Triton/ONNX Runtime):
  Requests arrive → queue → batch formed when:
    batch_size reached OR max_wait_time elapsed
  Process batch → return individual results
  
  Triton config:
    dynamic_batching {
      preferred_batch_size: [8, 16, 32]
      max_queue_delay_microseconds: 500  # wait max 0.5 ms
    }
  
  Better than static: adapts to traffic, bounds wait time
  Still blocks: all items in batch finish together

CONTINUOUS BATCHING (best for LLMs — vLLM/TRT-LLM):
  Token-level scheduling: each iteration processes one token per request
  When request finishes → slot freed → new request enters immediately
  No head-of-line blocking, GPU always full
  
  See Section 2 for details.

MICRO-BATCHING (for in-process hot path):
  Accumulate requests for 50-100 µs → batch GPU operations
  Much shorter wait than dynamic batching (sub-ms)
  Used when: models embedded in-process, can't afford ms-level wait
  
  Implementation:
  while (true) {
      collect_requests(buffer, max_wait_us=100, max_batch=16);
      if (buffer.empty()) continue;
      gpu_batch_inference(buffer);  // CUDA Graph, one launch for all
      scatter_results(buffer);
  }
```

## 6.3 Batch Size Selection

```
The batch size tradeoff:
  Larger batch → higher throughput, higher latency
  Smaller batch → lower latency, lower throughput

Finding optimal batch size:
  1. Measure: latency vs batch size curve
  2. Find: where latency exceeds your SLA
  3. Use: largest batch that stays under SLA

Typical curves:
  Batch=1:   latency=5 ms,  throughput=200 req/s
  Batch=4:   latency=6 ms,  throughput=660 req/s
  Batch=8:   latency=7 ms,  throughput=1,100 req/s
  Batch=16:  latency=9 ms,  throughput=1,700 req/s
  Batch=32:  latency=14 ms, throughput=2,200 req/s  ← SLA breach at 10ms!
  Batch=64:  latency=25 ms, throughput=2,500 req/s

  If SLA = 10 ms: optimal batch = 16 (best throughput under SLA)

For LLMs (decode phase):
  Batch doesn't increase latency much (memory-bound, same weights read)
  Batch=1: 50 tok/s (per-GPU)
  Batch=32: 1,500 tok/s (only marginally slower per-token)
  → Batch as large as KV cache memory allows!
```

---

# SECTION 7: MODEL OPTIMIZATION PIPELINE

---

## 7.1 The Optimization Stack

```
Training framework (PyTorch/JAX)
    ↓ export
ONNX / SafeTensors / HuggingFace checkpoint
    ↓ optimize
TensorRT / ONNX Runtime optimized / torch.compile
    ↓ quantize
FP8 / INT8 / INT4 calibrated model
    ↓ deploy
Serving runtime (vLLM / Triton / in-process)
```

## 7.2 ONNX Runtime Optimization Levels

```
Level 0 (basic): no optimization (just runs the graph)
Level 1 (basic): constant folding, redundant node elimination
Level 2 (extended): operator fusion (Conv+BN, MatMul+Add, etc.)
Level 3 (layout): data layout optimization (NCHW → NCHWc for CPU)
Level 99 (all): all optimizations + graph-level transforms

For inference:
  sess_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
  
  Key fusions ORT performs automatically:
  - MatMul + Add → FusedGemm
  - LayerNorm (multiple ops) → single LayerNormalization kernel
  - Attention (Q, K, V, softmax, matmul) → MultiHeadAttention kernel
  - GELU approximation → single kernel

Performance with ORT:
  Unoptimized BERT: 15 ms
  ORT Level 99:     8 ms (1.9× faster, zero code changes)
  ORT + FP16:       4 ms (3.75× faster)
  ORT + CUDA Graph: 3.5 ms (4.3× faster)
```

---

## 7.2.1 How ORT Graph Optimization Actually Works (Mental Model)

```
ORT optimization is a PASS-BASED graph rewriter, not a black box.
A model loads as a graph of nodes (ops) + edges (tensors). ORT runs
ordered passes; each pass matches a sub-graph PATTERN and rewrites it.

  ONNX graph ──▶ [L1 passes] ──▶ [L2 fusions] ──▶ [L3 layout]
              ──▶ [EP partitioning] ──▶ [EP-specific compile] ──▶ ready

Two distinct optimization phases (this trips people up):

  1. ORT GRAPH OPTIMIZER (hardware-agnostic, runs on CPU at load):
     constant folding, dead-node elimination, op fusion.

  2. EXECUTION PROVIDER COMPILE (hardware-specific, runs per EP):
     CUDA EP / TensorRT EP take the partitioned sub-graphs and
     compile them to their own kernels. TensorRT EP may even build
     a full TRT engine for the partition it claims.

The optimization LEVEL only controls phase 1. The EP controls phase 2.
That's why "Level 99 + CPU EP" and "Level 99 + CUDA EP" produce very
different runtime behavior from the SAME optimized graph.
```

### Why the levels are ordered the way they are
```
L1 (constant folding) must run FIRST — it shrinks the graph so later
    fusions have fewer nodes to pattern-match.
L2 (fusion) needs the cleaned graph from L1 to recognize patterns like
    MatMul→Add→GELU. If L1 didn't fold constants, the bias might be a
    computed node instead of an initializer and the fusion won't fire.
L3 (layout) runs LAST because it changes tensor memory format, which
    would break the pattern matchers in L2 if it ran earlier.

Practical consequence: ALWAYS use ORT_ENABLE_ALL unless a specific
fusion is buggy for your model — partial levels rarely help.
```

### The fusions that matter most for transformer hot-path models
```
| Fused op                | Replaces                          | Why it wins                |
|-------------------------|-----------------------------------|----------------------------|
| Attention / MHA         | 7-12 ops (QKV, scale, softmax,mm) | 1 kernel, no intermediates |
| SkipLayerNormalization  | Add + LayerNorm                   | fuses residual + norm      |
| EmbedLayerNormalization | Gather + Add + LayerNorm          | fuses input embedding path |
| FastGelu / BiasGelu     | Add(bias) + Gelu                  | 1 kernel, fewer HBM trips  |
| FusedMatMul             | MatMul + Add + (transpose)        | maps to cuBLAS GEMM+bias   |
| MatMulIntegerToFloat    | quantized MatMul + dequant        | INT8 GEMM path             |

To SEE which fusions fired, dump the optimized graph (Section 7.2.4).
If "Attention" doesn't appear in the optimized graph, you lost the
single biggest win — usually caused by an unsupported attention
variant or an opset too old for the fusion to match.
```

---

## 7.2.2 The Full Optimization Lever Set (beyond levels)

ORT performance is NOT just `graph_optimization_level`. These are the
levers that actually move latency, roughly in order of impact:

```
1. EXECUTION PROVIDER (biggest lever)
   CPU EP → CUDA EP → TensorRT EP. Picking the right EP for the
   hardware dwarfs every graph-level tweak. (Section 7.2.5)

2. PRECISION
   FP32 → FP16 (2× on GPU, near-free accuracy loss for inference)
   FP16 → INT8 (another ~2× but needs calibration, Section 7.2.6)

3. CUDA GRAPH CAPTURE
   Eliminates per-kernel launch overhead. Huge for small-batch,
   many-small-kernel models. Requires FIXED shapes. (Section 7.2.7)

4. IO BINDING (zero-copy in/out)
   Keep inputs/outputs on GPU; skip host<->device copies every call.
   This is THE key integration point for your C++ data-plane. (7.2.5)

5. SHAPE STRATEGY
   Fixed/static shapes >> dynamic shapes. Dynamic axes block CUDA
   Graphs and force re-planning. Free dimensions to fixed if you can.

6. THREADING
   intra_op_num_threads / inter_op_num_threads (CPU EP & CPU fallback
   ops). For GPU serving, set intra-op low and pin worker threads.

7. MEMORY ARENA
   Enable arena allocator + memory pattern planning so ORT pre-plans
   one big allocation instead of per-run malloc/free.

8. PROVIDER OPTIONS
   cudnn_conv_algo_search, do_copy_in_default_stream, arena config,
   TRT cache paths. The defaults are conservative, not fastest.
```

---

## 7.2.3 PRACTICAL STEP-BY-STEP: Optimizing an ONNX Model

This is the repeatable pipeline. Do it in THIS order — each step
assumes the previous one is done and measured.

### Step 0 — Establish a baseline (never skip)
```bash
# Get a clean, correct baseline number BEFORE changing anything.
# Use onnxruntime_perf_test (ships with ORT) for apples-to-apples runs.
onnxruntime_perf_test -e cpu   -r 200 -m times model.onnx   # CPU EP
onnxruntime_perf_test -e cuda  -r 200 -m times model.onnx   # CUDA EP

# Record: p50/p90/p99 latency, throughput, and a reference OUTPUT
# vector. Every later step must reproduce this output within tolerance.
```

### Step 1 — Clean and check the model
```bash
pip install onnx onnxsim onnxruntime

# (a) Validate the graph is well-formed
python -c "import onnx; onnx.checker.check_model('model.onnx')"

# (b) Simplify: fold constants, remove no-ops, collapse shape math.
#     This alone often removes dozens of nodes and unblocks fusions.
onnxsim model.onnx model_simplified.onnx

# (c) Upgrade opset if old — many fusions (Attention) need opset >= 11/13
python -c "import onnx; from onnx import version_converter as v; \
  onnx.save(v.convert_version(onnx.load('model_simplified.onnx'), 17), \
            'model_op17.onnx')"
```

### Step 2 — Fix the shapes (static beats dynamic)
```bash
# Dynamic batch/seq axes block CUDA Graphs and slow planning.
# If you serve a fixed batch (e.g. micro-batch=16, seq=128), pin them.
python -m onnxruntime.tools.make_dynamic_shape_fixed \
  --dim_param batch --dim_value 16 \
  --dim_param seq   --dim_value 128 \
  model_op17.onnx model_fixed.onnx

# If you truly need variable shapes, define a small set of buckets
# (e.g. seq in {64,128,256}) and keep one session per bucket.
```

### Step 3 — Apply offline graph optimization (bake it in)
```python
import onnxruntime as ort

so = ort.SessionOptions()
so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
# Persist the optimized graph so production loads fast and deterministic:
so.optimized_model_filepath = "model_optimized.onnx"
# Run with the SAME EP you'll deploy on — fusions can be EP-aware:
ort.InferenceSession("model_fixed.onnx", so,
                     providers=["CUDAExecutionProvider"])
# model_optimized.onnx now contains fused Attention/LayerNorm/Gelu etc.
```

### Step 3b — Transformer-specific optimizer (do this for BERT-family)
```bash
# The generic optimizer is conservative. The transformer tool applies
# aggressive, transformer-aware fusions + optional FP16 conversion.
python -m onnxruntime.transformers.optimizer \
  --input  model_optimized.onnx \
  --output model_bert_fp16.onnx \
  --model_type bert \
  --num_heads 12 --hidden_size 768 \
  --float16                       # convert to FP16 in one shot

# Verify the big fusions actually fired:
python -m onnxruntime.transformers.optimizer --input model_bert_fp16.onnx \
  --output /tmp/x.onnx --model_type bert --use_gpu --verbose
# Look for: Attention, SkipLayerNormalization, EmbedLayerNormalization,
#           FastGelu  → these should appear with non-zero counts.
```

### Step 4 — Precision: FP16, then INT8 if needed
```python
# FP16 (almost always worth it on GPU): done above via --float16,
# or convert explicitly:
from onnxconverter_common import float16
import onnx
onnx.save(float16.convert_float_to_float16(onnx.load("model_optimized.onnx")),
          "model_fp16.onnx")

# INT8 (only if FP16 isn't fast/small enough — needs calibration data):
from onnxruntime.quantization import quantize_static, CalibrationDataReader
# Provide ~100-500 representative samples via a CalibrationDataReader.
# Prefer static quantization for latency; dynamic for quick CPU wins.
quantize_static("model_fp16.onnx", "model_int8.onnx", calib_reader)
```

### Step 5 — Re-validate correctness (gate before perf)
```python
# Compare optimized output to the Step-0 reference. Fail closed.
import numpy as np
np.testing.assert_allclose(opt_out, ref_out, rtol=1e-2, atol=1e-2)
# FP16/INT8 will drift — set tolerances per business metric, not 1e-7.
# For ranking/fraud: check that top-k order / score deciles are stable,
# not just raw float equality.
```

### Step 6 — Re-benchmark and pick the config
```bash
onnxruntime_perf_test -e cuda -r 500 -m times \
  -i "cudnn_conv_algo_search|EXHAUSTIVE" model_bert_fp16.onnx
# Sweep: FP32 vs FP16 vs INT8 × CUDA EP vs TRT EP × CUDA Graph on/off.
# Choose the fastest config that passes Step-5 accuracy gates AND meets
# your p99 SLA at the batch size you actually serve.
```

### Step 7 — Lock it for production
```
- Ship model_optimized/_fp16 (pre-optimized) so prod load is fast and
  byte-identical across replicas.
- Set so.graph_optimization_level = ORT_DISABLE_ALL at load time IF the
  model is already optimized offline (avoids re-optimizing every boot).
- Pin EP, provider options, thread counts, and shapes in config.
- Save the perf + accuracy numbers as the regression baseline.
```

```
ONE-LINE MENTAL CHECKLIST:
  simplify → fix shapes → ORT_ENABLE_ALL (offline) → transformer
  fusions → FP16/INT8 → validate → CUDA EP + IO binding + CUDA Graph
  → re-bench → freeze.
```

---

## 7.2.4 Inspecting What Optimization Did (don't fly blind)

```python
# 1. Dump the optimized graph and open it in Netron to SEE the fusions.
so = ort.SessionOptions()
so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
so.optimized_model_filepath = "optimized.onnx"   # <-- inspect this
ort.InferenceSession("model.onnx", so, providers=["CUDAExecutionProvider"])

# 2. Turn on the built-in profiler to get a per-op timeline (Chrome trace)
so.enable_profiling = True
# After runs, ORT writes onnxruntime_profile_*.json → open in
# chrome://tracing. Shows per-kernel time + which EP ran each node.

# 3. Confirm node placement: which ops went to CUDA vs fell back to CPU.
#    CPU fallback in the middle of a GPU graph = hidden D2H/H2D copies =
#    latency killer. Hunt these down and eliminate them.
```

```
RED FLAGS in the optimized graph / profile:
  - No "Attention" node      → biggest fusion didn't fire (opset/variant)
  - "MemcpyToHost"/"MemcpyFromHost" nodes inside the graph → CPU fallback
  - Many tiny kernels        → candidate for CUDA Graph capture
  - Dynamic reshape per run   → shapes not fixed, planning overhead
```

---

## 7.2.5 ONNX Runtime + CUDA Execution Provider — C++ GPU Serving

This is the integration that matters for the in-process hot path in the
C++ data-plane. The goal: **model runs on GPU, inputs/outputs stay on
GPU, zero host copies per request.**

### How the CUDA EP fits
```
ORT session is created with an ORDERED provider list:
  providers = [CUDAExecutionProvider, CPUExecutionProvider]

  ORT partitions the graph: every node the CUDA EP supports runs on GPU;
  anything it doesn't support FALLS BACK to the CPU EP. Fallback inserts
  Memcpy nodes (D2H/H2D) — these are the silent latency tax. Goal: keep
  the ENTIRE hot-path graph on CUDA EP so zero copies are inserted.
```

### Provider options that actually matter
```cpp
OrtCUDAProviderOptionsV2* cuda_opts = nullptr;
Ort::ThrowOnError(api.CreateCUDAProviderOptions(&cuda_opts));

std::vector<const char*> keys = {
    "device_id",                       // which GPU
    "arena_extend_strategy",           // kNextPowerOfTwo: fewer reallocs
    "cudnn_conv_algo_search",          // EXHAUSTIVE: best conv kernels
    "do_copy_in_default_stream",       // 1: correctness w/ shared stream
    "gpu_mem_limit",                   // cap arena so it won't OOM peers
};
std::vector<const char*> vals = {
    "0", "kNextPowerOfTwo", "EXHAUSTIVE", "1", "8589934592" /*8GB*/
};
Ort::ThrowOnError(api.UpdateCUDAProviderOptions(
    cuda_opts, keys.data(), vals.data(), keys.size()));
```

### Full in-process C++ session setup (data-plane hot path)
```cpp
#include <onnxruntime_cxx_api.h>
#include <cuda_runtime.h>

class OnnxGpuModel {
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "dataplane"};
    Ort::Session session_{nullptr};
    Ort::MemoryInfo gpu_mem_{nullptr};   // CUDA pinned memory descriptor

public:
    explicit OnnxGpuModel(const std::string& model_path, int device_id) {
        Ort::SessionOptions so;

        // Model is ALREADY optimized offline (Section 7.2.3 Step 7),
        // so disable re-optimization for fast, deterministic load.
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);

        // Few intra-op threads: GPU does the work, don't oversubscribe CPU.
        so.SetIntraOpNumThreads(1);
        so.SetExecutionMode(ORT_SEQUENTIAL);

        // Attach CUDA EP (then CPU EP as last-resort fallback).
        const auto& api = Ort::GetApi();
        OrtCUDAProviderOptionsV2* cuda_opts = nullptr;
        api.CreateCUDAProviderOptions(&cuda_opts);
        const std::string dev = std::to_string(device_id);
        const char* keys[] = {"device_id", "cudnn_conv_algo_search",
                              "do_copy_in_default_stream"};
        const char* vals[] = {dev.c_str(), "EXHAUSTIVE", "1"};
        api.UpdateCUDAProviderOptions(cuda_opts, keys, vals, 3);
        so.AppendExecutionProvider_CUDA_V2(*cuda_opts);
        api.ReleaseCUDAProviderOptions(cuda_opts);

        session_ = Ort::Session(env_, model_path.c_str(), so);

        // MemoryInfo describing GPU-resident tensors for IO binding.
        gpu_mem_ = Ort::MemoryInfo("Cuda", OrtArenaAllocator,
                                   device_id, OrtMemTypeDefault);
    }

    // d_input / d_output are RAW DEVICE POINTERS already on the GPU,
    // produced by the data-plane's CUDA transport (zero-copy).
    void infer(float* d_input,  const int64_t* in_shape,  size_t in_dims,
               float* d_output, const int64_t* out_shape, size_t out_dims) {
        // Wrap existing device memory as ORT tensors — NO host copy.
        Ort::Value in = Ort::Value::CreateTensor<float>(
            gpu_mem_, d_input, numel(in_shape, in_dims),
            in_shape, in_dims);
        Ort::Value out = Ort::Value::CreateTensor<float>(
            gpu_mem_, d_output, numel(out_shape, out_dims),
            out_shape, out_dims);

        // IO BINDING: bind GPU tensors directly; output written in-place
        // on the GPU. This is what makes it truly zero-copy.
        Ort::IoBinding binding(session_);
        binding.BindInput (input_name_,  in);
        binding.BindOutput(output_name_, out);

        Ort::RunOptions ro;
        session_.Run(ro, binding);   // runs on the CUDA EP / GPU stream
    }

private:
    const char* input_name_  = "input";
    const char* output_name_ = "output";
    static int64_t numel(const int64_t* s, size_t n) {
        int64_t p = 1; for (size_t i = 0; i < n; ++i) p *= s[i]; return p;
    }
};
```

### Why IO Binding is the whole point
```
WITHOUT IO binding (naive Session::Run with CPU tensors):
  request → H2D copy inputs → GPU compute → D2H copy outputs → response
  Two PCIe copies per call. For a 0.7 ms FAISS-like model, the copies
  can cost MORE than the compute.

WITH IO binding (BindInput/BindOutput on GPU tensors):
  data-plane already has features in GPU memory (Layer 5 zero-copy) →
  bind device pointers → GPU compute → output stays on GPU for the next
  stage. ZERO PCIe copies in the hot path.

This is exactly why the model lives IN-PROCESS in the C++ data-plane:
shared GPU buffers flow model→model without ever touching host memory.
```

### Stream alignment with the data-plane
```
- Run ORT on the SAME CUDA stream the transport uses (or a stream you
  synchronize against) so compute overlaps transfers correctly.
- do_copy_in_default_stream=1 keeps any internal copies ordered on the
  default stream — set it unless you fully manage streams yourself.
- For multi-worker serving: ONE session can be called from multiple
  threads (ORT sessions are thread-safe for Run), but give each worker
  its own IoBinding + pre-allocated GPU output buffers to avoid sharing.
```

---

## 7.2.6 INT8 Quantization for ONNX (when FP16 isn't enough)

```
Two modes:
  DYNAMIC quantization  → weights INT8, activations quantized at runtime.
     No calibration data. Easy. Best for CPU / lightweight models.
  STATIC quantization   → weights AND activations INT8 via calibration.
     Needs representative data. Best for GPU latency + max throughput.

Static flow:
  1. Collect 100-500 representative production samples.
  2. Implement CalibrationDataReader to feed them.
  3. quantize_static(fp16_model, int8_model, reader,
                     quant_format=QDQ, per_channel=True)
  4. Validate: accuracy drop must pass business metric (not raw MSE).

GPU note: on CUDA/TensorRT EP, INT8 needs QDQ (QuantizeLinear/
DequantizeLinear) format so the EP can fuse Q/DQ into INT8 GEMMs.
Per-channel weight quant preserves accuracy far better than per-tensor.
```

---

## 7.2.7 CUDA Graphs with ORT (squeeze out launch overhead)

```
For small-batch transformer/ranking models, kernel LAUNCH overhead can
rival compute. CUDA Graph capture records the whole kernel sequence once
and replays it as a single launch.

Enable on the CUDA EP:
  provider option "enable_cuda_graph" = "1"

HARD REQUIREMENTS (or it silently won't help / will error):
  - FIXED input shapes (this is why Section 7.2.3 Step 2 matters).
  - Same GPU memory addresses each run → use IO binding with STABLE,
    pre-allocated input/output buffers (don't realloc per request).
  - First run "captures" the graph; subsequent runs "replay" it.

Typical win: 3.5 ms → ~3.0-3.2 ms for a small BERT (launch-bound). The
smaller and more kernel-heavy the model, the bigger the relative gain.
```

---

## 7.2.8 CUDA EP vs TensorRT EP (which GPU EP to pick)

```
| Aspect            | CUDA EP                    | TensorRT EP                  |
|-------------------|----------------------------|------------------------------|
| Setup             | drop-in, no build step     | builds TRT engine (minutes)  |
| Peak speed        | fast                       | usually fastest (compiled)   |
| Dynamic shapes    | handles well               | needs opt profiles, harder   |
| INT8/FP8          | INT8 via QDQ               | best calibration + FP8       |
| Portability       | any CUDA GPU               | engine is GPU-arch specific  |
| Cold start        | fast                       | slow (engine build/cache)    |
| Fallback          | per-op to CPU EP           | unsupported sub-graph→CUDA EP |

Rule of thumb:
  - Start with CUDA EP. It's the integration above and needs no build.
  - Move hot, shape-stable models to TensorRT EP for the last 10-30%.
  - Cache TRT engines on local NVMe (trt_engine_cache_enable=1) so you
    don't rebuild on every cold start (ties to Section 9 warm-up).
  - You can stack them: providers=[TensorRT EP, CUDA EP, CPU EP] — TRT
    claims what it can, CUDA EP catches the rest, CPU is last resort.
```

---

## 7.2.9 DevOps: Building the ONNX Optimization Pipeline (raw → optimized)

The manual steps in 7.2.3 are correct, but a human running them by hand
is not reproducible, not auditable, and not safe for production. From a
DevOps view, model optimization is a **build pipeline with the same
discipline as a code build**: versioned input, deterministic stages,
automated gates, signed output artifact.

### The mental shift
```
Code pipeline:   source → compile → test → package → sign → registry
Model pipeline:  raw.onnx → optimize → validate → benchmark → package
                 → sign → model registry

A model is just another build artifact. Treat the raw ONNX like source
code and the optimized FP16/INT8 model like a compiled, signed binary.
```

### Pipeline topology (what runs where)
```
┌──────────────┐   push raw.onnx     ┌────────────────────────────────┐
│ Model author │ ──── + metadata ──▶ │ Artifact store (S3/GCS/OCI)    │
│ (DS / train) │                     │ raw/<model>/<version>/model.onnx│
└──────────────┘                     └───────────────┬────────────────┘
                                                     │ event/trigger
                                                     ▼
┌──────────────────────────────────────────────────────────────────────┐
│ CI RUNNER #1: CPU stage (no GPU needed — cheap, parallelizable)       │
│   simplify → opset upgrade → fix shapes → ORT_ENABLE_ALL (offline)    │
│   → transformer fusions → FP16 convert → numeric self-check (CPU)     │
│   Output: candidate_fp16.onnx + fusion report                         │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ artifact handoff
                                ▼
┌──────────────────────────────────────────────────────────────────────┐
│ CI RUNNER #2: GPU stage (scheduled on a GPU node — the SAME arch as   │
│   production: build for H100 on H100)                                 │
│   CUDA EP correctness gate → (optional) INT8 calibrate → TRT engine   │
│   build + cache → benchmark sweep (perf_test) → SLA gate              │
│   Output: optimized.onnx (+ optional trt_engine.cache) + perf report  │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ if all gates pass
                                ▼
┌──────────────────────────────────────────────────────────────────────┐
│ PACKAGE + SIGN + PUBLISH                                              │
│   bundle(model + manifest + perf.json + accuracy.json) → cosign sign  │
│   → push to MODEL REGISTRY as optimized/<model>/<version>            │
│   → mark "staging"; promotion to "prod" is a separate gated step      │
└──────────────────────────────────────────────────────────────────────┘
```

### Why the CPU/GPU split matters (DevOps cost + correctness)
```
- CPU stage is hardware-agnostic and cheap → run it on normal CI runners,
  fan out across many models in parallel.
- GPU stage MUST run on the same GPU architecture you deploy to, because
  CUDA Graph capture, cuDNN algo choice, and especially TensorRT engines
  are architecture-specific. Build for H100 on an H100 runner.
- Splitting keeps scarce GPU CI minutes spent only on what truly needs a
  GPU (correctness + benchmark + TRT build).
```

### The pipeline as a single declarative spec
```yaml
# optimize.pipeline.yaml — input-driven, one model per run
input:
  model_uri: s3://models/raw/ranker/v7/model.onnx
  model_type: bert            # drives transformer optimizer
  num_heads: 12
  hidden_size: 768
  fixed_shapes: { batch: 16, seq: 128 }
  target_gpu: h100            # selects GPU runner + TRT build target

precision_candidates: [fp16, int8]   # pipeline tries each, picks best-passing

gates:
  accuracy:
    dataset_uri: s3://datasets/ranker/validation_5k.parquet
    metric: ndcg@10
    max_relative_drop: 0.005          # <=0.5% NDCG loss allowed
  performance:
    batch_size: 16
    p99_latency_ms_max: 10
    min_throughput_qps: 1500
  safety:
    no_cpu_fallback_nodes: true        # fail if Memcpy nodes appear
    require_fused_attention: true      # fail if Attention fusion missing

output:
  registry: oci://registry.internal/models/optimized
  sign: true                           # cosign / sigstore
  promote_to: staging                  # never auto-promote to prod
```

### What the pipeline runner actually executes (stage script)
```python
# optimize_pipeline.py — invoked by CI, fully non-interactive.
# Each stage is idempotent and writes an artifact + a JSON report so the
# whole run is auditable. Exit non-zero on ANY gate failure (fail-closed).

def run(cfg):
    raw   = fetch(cfg.input.model_uri)              # pull versioned input
    ref   = compute_reference_outputs(raw, cfg)      # Step-0 baseline

    # ---- CPU stage (hardware-agnostic) ----
    m = simplify(raw)
    m = upgrade_opset(m, 17)
    m = fix_shapes(m, cfg.input.fixed_shapes)
    m = graph_optimize_all(m)                        # ORT_ENABLE_ALL offline
    m, report = transformer_optimize(m, cfg)         # fusion report
    assert_gate(report.has_fused_attention,          # safety gate
                cfg.gates.safety.require_fused_attention)

    best = None
    for prec in cfg.precision_candidates:            # try fp16, int8...
        cand = to_precision(m, prec, calib=cfg.gates.accuracy.dataset_uri)

        # ---- GPU stage (same arch as prod) ----
        place = check_node_placement(cand)           # detect CPU fallback
        assert_gate(not place.has_cpu_fallback,
                    cfg.gates.safety.no_cpu_fallback_nodes)

        acc  = evaluate_accuracy(cand, ref, cfg.gates.accuracy)
        perf = benchmark(cand, cfg.gates.performance) # perf_test sweep

        if acc.passed and perf.passed:
            best = pick_better(best, cand, perf)     # keep fastest passing

    if best is None:
        fail("no precision candidate passed accuracy + SLA gates")

    artifact = package(best, manifest=cfg, accuracy=acc, perf=perf)
    sign(artifact)                                   # cosign
    publish(artifact, cfg.output.registry, tag="staging")
```

### The manifest = provenance (audit + rollback)
```json
// Published ALONGSIDE the optimized model. This is what makes the
// pipeline auditable and reversible.
{
  "model": "ranker", "version": "v7",
  "source_raw_sha256": "ab12...",        // exact input that produced this
  "pipeline_git_sha": "9f3c...",         // which pipeline code ran
  "ort_version": "1.18.0",
  "precision": "fp16",
  "target_gpu": "h100",
  "fixed_shapes": {"batch":16,"seq":128},
  "fusions": {"Attention":12,"SkipLayerNorm":24,"FastGelu":12},
  "accuracy": {"metric":"ndcg@10","baseline":0.812,"optimized":0.810},
  "performance": {"p99_ms":8.4,"throughput_qps":1720,"cuda_graph":true},
  "built_at": "2026-06-08T10:00:00Z",
  "signature": "cosign:..."
}
```

### Gating philosophy (the DevOps non-negotiables)
```
1. FAIL-CLOSED: any gate failure aborts the run. Never publish a model
   that missed an accuracy or SLA gate "to unblock" someone.
2. DETERMINISTIC: pin ORT version, CUDA/cuDNN version, opset, and shapes.
   Same raw input + same pipeline SHA must produce byte-stable output.
3. PROVENANCE: every optimized artifact traces to one raw SHA + one
   pipeline SHA. No mystery models in prod.
4. SEPARATE BUILD FROM DEPLOY: pipeline produces a SIGNED artifact tagged
   "staging". Promotion to "prod" is a different, separately-approved
   step (ties to canary in Section 12).
5. REGRESSION BASELINE: store this run's perf+accuracy. Next version must
   not regress without explicit sign-off.
```

### CI integration (where it plugs in)
```yaml
# .github/workflows/optimize-model.yml (sketch)
on:
  workflow_dispatch: { inputs: { model_uri: { required: true } } }
  repository_dispatch: { types: [new-raw-model] }   # train job triggers it
jobs:
  cpu-stage:
    runs-on: [self-hosted, cpu]
    steps:
      - run: python optimize_pipeline.py --stage cpu --config optimize.pipeline.yaml
  gpu-stage:
    needs: cpu-stage
    runs-on: [self-hosted, gpu, h100]                # MUST match prod arch
    steps:
      - run: python optimize_pipeline.py --stage gpu --config optimize.pipeline.yaml
      - run: cosign sign --key $COSIGN_KEY $ARTIFACT  # sign on success only
```

### Operating the pipeline (Day-2 concerns)
```
- CACHE TRT engines keyed by (model_sha, ort_ver, gpu_arch, shapes) so
  re-runs skip multi-minute engine builds.
- REPRODUCIBLE RUNNERS: pin the toolchain in a container image
  (onnxruntime-gpu + tensorrt + cuda) — the pipeline runs INSIDE it so
  every run uses identical tool versions.
- QUARANTINE failures: a model that fails gates is pushed to a "rejected"
  path with its report attached, not silently dropped.
- OBSERVABILITY: emit per-stage duration, gate pass/fail, and final
  perf/accuracy as metrics → dashboard model-build health over time.
- SECURITY: validate the raw ONNX (onnx.checker) before loading; an ONNX
  file is a deserialized graph and a malformed/hostile one is an input-
  validation risk. Run the pipeline in an isolated, least-privilege job.
```

### Interview-ready one-liner
```
"We treat optimization as a build pipeline: versioned raw ONNX is the
source, a containerized two-stage job (cheap CPU graph optimization, then
GPU correctness + benchmark on the SAME arch as prod) produces a SIGNED,
provenance-tagged optimized artifact. It's fail-closed on accuracy and
p99 SLA gates, deterministic via pinned tool versions, and publishes to
a model registry as 'staging' — promotion to prod is a separate canary."
```

---

## 7.3 TensorRT Build Process

```
TensorRT optimization (offline, per-GPU architecture):

1. Parse model (ONNX or TensorFlow)
2. Layer fusion:
   - Conv + BN + ReLU → single kernel
   - GEMM + Bias + Activation → single kernel
   - Multi-head attention → FlashAttention kernel
3. Precision calibration:
   - Run calibration dataset through model
   - Collect activation ranges per layer
   - Determine scale factors for INT8/FP8
4. Kernel auto-tuning:
   - For each layer: benchmark multiple CUDA kernel implementations
   - Select fastest for THIS specific GPU
   - Cache results (build is GPU-architecture-specific)
5. Memory optimization:
   - Determine optimal tensor placement (reuse buffers)
   - Minimize peak memory by scheduling operations carefully
6. Serialize engine (binary blob, GPU-specific)

Build time: 5-60 minutes (depending on model size)
Runtime latency improvement: typically 2-5× vs PyTorch eager

Important: TensorRT engine is NOT portable across GPU architectures!
  Built for H100 → won't run on A100 (different SM count, capabilities)
  Must rebuild per deployment target
```

## 7.4 torch.compile (PyTorch 2.0+)

```
torch.compile(): JIT compilation that fuses operations at graph level

model = torch.compile(model, mode="max-autotune")

Modes:
  "default":      balance compile time and speedup
  "reduce-overhead": minimize framework overhead (CUDA Graphs)
  "max-autotune": try many kernel configurations (slow compile, fastest run)

What it does:
  1. Traces Python model into FX graph (IR)
  2. Applies graph-level optimizations (fusion, dead code elimination)
  3. Generates optimized CUDA kernels via Triton or inductor backend
  4. Compiles and caches the result

Speedup: typically 1.3-2× over eager mode for transformer inference
Limitation: dynamic shapes require recompilation (guard + recompile)
  - Use torch._dynamo.config.cache_size_limit for shape variants
```

---

# SECTION 8: LATENCY ANALYSIS AND TAIL LATENCY

---

## 8.1 Percentile Math

```
Why p99 matters more than p50:
  p50 = median (half of requests faster, half slower)
  p99 = 99th percentile (1 in 100 requests is this slow or worse)
  p99.9 = 999th percentile (1 in 1000 requests)

At scale:
  1,000 req/sec × p99 = 10 users/sec experience this latency
  10,000 req/sec × p99.9 = 10 users/sec experience this latency
  
Fan-out amplification:
  If your service calls 10 backends in parallel:
  p99 of overall = p99.9 of each backend (roughly)!
  
  Because: ANY of the 10 backends being slow makes the whole request slow.
  Probability ALL 10 are fast = 0.99^10 = 0.90 = only p90!
  
  To get p99 overall with 10 fan-out: each backend needs ~p99.9
```

## 8.2 Sources of Tail Latency

```
| Source | Impact | Detection | Fix |
|--------|--------|-----------|-----|
| GC pause (Java/Go) | 5-50 ms | GC logs, pauses metric | Tune GC, reduce alloc, off-heap |
| CFS throttling | 5-100 ms | cpu.stat nr_throttled | Static CPU Manager, remove limits |
| THP compaction | 2-20 ms | khugepaged in perf | Disable THP, use explicit HP |
| NIC interrupt on hot CPU | 0.1-5 ms | /proc/interrupts | irqaffinity to system CPUs |
| Context switch | 0.05-1 ms | perf stat cs | Reduce threads, pin CPUs |
| Page fault | 0.001-10 ms | perf stat page-faults | MAP_POPULATE, mlock |
| TCP retransmit | 5-200 ms | tcpretrans | TCP tuning, check network |
| DNS lookup | 1-100 ms | strace | Local DNS cache, resolve at startup |
| Disk I/O (log flush) | 1-50 ms | biolatency | Async logging, buffered writes |
| Mutex contention | 0.01-10 ms | offcputime | Lock-free, per-thread, reduce CS |
| NUMA remote access | per-access +100ns | numastat | Topology manager, first-touch |
| CPU frequency scaling | 5-50 ms | cpufreq | Performance governor |
```

## 8.3 Hedged Requests

```
For fan-out services (your request hits multiple backends):

Strategy: send request to TWO backends, take first response, cancel second.

Without hedging:
  p99 = max(backend_1_p99, backend_2_p99, ...) → amplified tail

With hedging (after p50 deadline):
  1. Send to backend A
  2. If no response in p50 time (5 ms): also send to backend B
  3. Take whichever responds first
  4. Cancel the other

  Result: p99 drops to approximately p50 of the backend
  Cost: ~1% extra traffic (only hedge when p50 exceeded)

Implementation:
  - Timeout-based: hedge after fixed delay
  - Tied requests: send to 2 backends immediately, first one to START cancels other
  
When to use:
  - Read-only requests (safe to duplicate)
  - High fan-out (amplified tail latency)
  - Backend is occasionally slow (not consistently)
```

## 8.4 Load Shedding

```
When at capacity: better to REJECT some requests fast
than to SERVE all requests slowly.

Without load shedding:
  Queue grows → latency grows for ALL requests → SLA breach for everyone
  "Brownout" — everything slow, nothing fails cleanly

With load shedding:
  Queue > threshold → reject NEW requests with 503/429
  In-flight requests continue at normal speed
  Healthy users get fast responses, excess users get immediate reject

Strategies:
  1. Queue depth: reject when queue > N
  2. Latency-based: reject when p50 > target (adaptive)
  3. CPU-based: reject when CPU > 80% (resource-aware)
  4. CoDel (Controlled Delay): reject requests that waited too long in queue
     (if request sat in queue > target ms, it's already late — reject it)

CoDel for inference:
  if (time_in_queue > 10ms) {
      drop_request_with_503();
      increment_metric("load_shedding.drops");
  }
  // Better: the client retries to a different replica
  // Worse: client gets a stale response that arrives after deadline
```

---

# SECTION 9: MODEL WARM-UP AND COLD START

---

## 9.1 The Cold Start Problem

```
GPU inference service startup:
  1. Pull container image:          30-120 sec (depends on image size)
  2. Download model weights:        10-60 sec (depends on storage)
  3. Load model to GPU:             5-30 sec (depends on model size)
  4. Allocate KV cache:             1-5 sec
  5. Compile/optimize (if needed):  10-60 sec (torch.compile, TRT build)
  6. Warm-up inference:             5-30 sec (CUDA lazy init, JIT)
  TOTAL:                            60-300 sec before first real request!

During this time: zero capacity from this replica
If scaling up under load: 60-300 sec before new capacity helps
```

## 9.2 Warm-Up Strategies

```
CUDA lazy initialization:
  First CUDA call initializes the CUDA context (~200-500 ms)
  First kernel launch triggers JIT compilation
  First cuDNN call runs algorithm selection
  
  Fix: warm-up requests at startup BEFORE accepting real traffic

Warm-up request pattern:
  // During startup (before readiness probe passes):
  for (int i = 0; i < WARMUP_ITERATIONS; i++) {
      dummy_input = create_representative_input(batch_size=MAX_BATCH);
      model.forward(dummy_input);
      // This triggers:
      // - CUDA context init
      // - cuDNN algorithm selection (cached for future calls)
      // - CUDA Graph capture (if using graphs)
      // - JIT compilation (torch.compile)
      // - Memory allocator warm-up (pre-split blocks)
  }
  // NOW mark as ready (pass readiness probe)

K8s readiness probe:
  readinessProbe:
    httpGet:
      path: /health/ready    # returns 200 only AFTER warm-up complete
      port: 8080
    initialDelaySeconds: 60   # give time for model loading
    periodSeconds: 5
    failureThreshold: 20      # allow 100 sec for warmup before considering failed

vLLM warm-up:
  vLLM automatically warms up by:
  1. Loading model and allocating KV cache blocks
  2. Running CUDA Graph capture for all batch sizes
  3. Processing a dummy request through full pipeline
  4. Only then passing readiness probe
```

## 9.3 Reducing Cold Start Time

```
Strategy 1: Pre-cached model on local NVMe
  Model already on node → skip download (10-60 sec saved)
  DaemonSet keeps models synced from object store

Strategy 2: Smaller container images
  Multi-stage build: runtime image has ONLY model + runtime deps
  Typical reduction: 15 GB → 3 GB image = 4× faster pull

Strategy 3: Model sharding across replicas (speculative)
  Load different model parts in parallel from multiple sources
  Assemble on GPU (overlap download + loading)

Strategy 4: Checkpoint-based restart (warm restart)
  Save GPU state (model + KV cache) to fast storage on shutdown
  On restart: load state instead of re-initializing from scratch
  Reduces restart: 300 sec → 30 sec

Strategy 5: Keep warm spare replicas
  Maintain 1-2 extra replicas that are loaded but idle
  Traffic spike → instantly available (no cold start)
  Cost: paying for idle GPU (acceptable for critical services)
```

---

# SECTION 10: MULTI-MODEL SERVING ON SINGLE GPU

---

## 10.1 The Challenge

```
Multiple small models need GPU but individually don't saturate it:
  Intent classifier: uses 200 MB, 10% GPU utilization
  Entity extractor: uses 200 MB, 10% GPU utilization  
  Embedding model: uses 200 MB, 8% GPU utilization
  Ranking model: uses 100 MB, 5% GPU utilization
  
  Total: 700 MB (of 80 GB available)
  Utilization: 33% at best (if running sequentially)
  
  Dedicating one H100 (80 GB, $30K+) for 700 MB of models = waste!
```

## 10.2 Strategies

```
Strategy 1: Multi-model in single process (YOUR approach)
  Load ALL small models into ONE process on ONE GPU
  Use CUDA streams for concurrent execution
  
  Stream 0: intent classifier
  Stream 1: entity extractor
  Stream 2: embedding model
  Stream 3: ranking model
  
  All 4 run CONCURRENTLY on same GPU!
  GPU utilization: 60-80%
  
  Best for: models that are always co-invoked (your pipeline)

Strategy 2: Triton multi-model
  Triton Inference Server hosts multiple models
  Each model gets its own backend (ONNX, TRT, Python)
  Triton handles scheduling and batching per-model
  
  config.pbtxt per model:
    instance_group [{
      count: 1
      kind: KIND_GPU
      gpus: [0]
    }]
  
  Multiple models share GPU 0 via Triton scheduler

Strategy 3: MIG partitioning
  Split H100 into multiple instances
  Assign different models to different instances
  Hardware isolation between models
  
  Best for: multi-tenant (different teams/models on same GPU)

Strategy 4: Model multiplexing (load/unload)
  Keep hot models on GPU, cold models on CPU/disk
  Load model to GPU on-demand when requests arrive
  Unload after idle timeout
  
  Problem: load time (seconds) makes first request slow
  Mitigation: predict which models will be needed (pre-load)
```

## 10.3 In-Process Multi-Model GPU Memory Management

```cpp
// Your architecture: all models share one CUDA device
class MultiModelGPU {
    Ort::Session intent_model;      // 200 MB
    Ort::Session entity_model;      // 200 MB
    Ort::Session embed_model;       // 200 MB
    Ort::Session rank_model;        // 100 MB
    faiss::GpuIndex* search_index;  // 2 GB (IVF index)
    
    // Pre-allocated GPU buffers per worker (avoid runtime allocation)
    struct WorkerGPUBuffers {
        float* input_tensor;    // cudaMalloc'd at startup
        float* intent_output;
        float* entity_output;
        float* embed_output;
        float* rank_output;
    };
    std::vector<WorkerGPUBuffers> workers;
    
    // CUDA streams for concurrent model execution
    cudaStream_t streams[4];
    
    void infer_all(int worker_id, const float* features) {
        auto& buf = workers[worker_id];
        // Copy features to GPU (async, stream 0)
        cudaMemcpyAsync(buf.input_tensor, features, size,
                        cudaMemcpyHostToDevice, streams[0]);
        
        // Launch all models concurrently on different streams
        intent_model.Run(buf.input_tensor, buf.intent_output, streams[0]);
        entity_model.Run(buf.input_tensor, buf.entity_output, streams[1]);
        embed_model.Run(buf.input_tensor, buf.embed_output, streams[2]);
        // ranking waits for embedding (dependency)
        cudaStreamWaitEvent(streams[3], embed_done_event);
        rank_model.Run(buf.embed_output, buf.rank_output, streams[3]);
        
        // Sync only when ALL results needed
        cudaStreamSynchronize(streams[3]);
    }
};
```

---

# SECTION 11: INFERENCE COST OPTIMIZATION

---

## 11.1 Cost Per Token/Request Math

```
H100 cost: ~$3.50/hour (cloud spot) to ~$8/hour (on-demand)

vLLM serving 7B FP16:
  Throughput: ~2,000 tokens/sec (batch=32)
  Cost per 1M tokens: $8/hour / 7,200 tokens/sec × 1,000,000 = $0.31/M tokens
  
  vs API pricing: OpenAI GPT-4o = $5.00/M tokens (input)
  Self-hosted: 16× cheaper for same quality model!

vLLM serving 7B INT4 (GPTQ):
  Throughput: ~4,500 tokens/sec
  Cost per 1M tokens: $0.14/M tokens
  37× cheaper than API!

Optimization impact on cost:
  FP16 → FP8: 2× throughput → 50% cost reduction
  Static → continuous batching: 3× throughput → 67% cost reduction
  No prefix cache → prefix cache: 1.5× effective throughput → 33% cost reduction
  Combined: FP8 + continuous + prefix = ~4-5× cost reduction
```

## 11.2 GPU Utilization Targets

```
Inference (latency-sensitive):
  Target: 50-70% GPU utilization
  Why not 100%? → queuing theory: at 100% util, queue grows infinitely
  At 70% util: reasonable queue depth, stable latency
  
  Monitor: if util > 80% → scale up BEFORE latency degrades

Training (throughput-sensitive):
  Target: 90-99% GPU utilization
  Acceptable because: no latency SLA, just maximize throughput
  
  If < 90%: data loading bottleneck, communication bottleneck, or
            bubbles in pipeline parallelism

Metric: MFU (Model FLOPS Utilization)
  MFU = actual_FLOPS / peak_theoretical_FLOPS
  
  Good training: MFU = 40-60% (including communication overhead)
  Good inference: MFU = 20-40% (memory-bound, Tensor Cores partially idle)
  
  H100 peak FP16: 1,979 TFLOPS
  Training at 50% MFU: 990 actual TFLOPS
```

---

# SECTION 12: A/B TESTING AND CANARY FOR MODELS

---

## 12.1 Model Deployment Strategies

```
Blue-Green deployment:
  v1 (100% traffic) → deploy v2 alongside → switch traffic → v1 standby
  
  Pros: instant rollback (switch back to v1)
  Cons: 2× GPU resources during transition, no gradual validation

Canary deployment:
  v1 (100%) → v2 gets 1% → monitor → 5% → 10% → 50% → 100%
  
  Pros: catch regressions early with minimal blast radius
  Cons: slow (days to full rollout), needs traffic splitting

Shadow deployment:
  v1 handles real traffic, v2 processes same requests in background
  Compare v2 outputs to v1 (no impact on users)
  
  Pros: zero risk to users, full comparison data
  Cons: 2× GPU cost, no real user feedback (can't measure engagement)
```

## 12.2 Model A/B Testing Framework

```
For ML models, standard A/B testing needs modifications:

1. Feature/prediction logging:
   Log EVERY prediction with: request_id, model_version, features, score, decision
   
2. Delayed outcomes:
   Fraud: true label arrives days/weeks later (chargeback)
   Search: engagement measured over session (not instant)
   → Need to join predictions with delayed outcomes

3. Metrics:
   Online: latency, error rate, fallback rate (immediate)
   Business: precision, recall, false positive rate (delayed)
   
4. Statistical significance:
   Need enough samples per variant for confidence
   Fraud (rare event): may need weeks for significance on FPR
   Search (common): hours may suffice for NDCG

5. Guardrail metrics:
   Set AUTOMATIC rollback triggers:
   - latency p99 > 2× baseline
   - error rate > 1%
   - fraud miss rate increases > 5% (critical!)
   
   If guardrail breached: auto-rollback within minutes
```

## 12.3 Feature Store for Inference

```
Feature store architecture for real-time inference:

OFFLINE:
  Feature pipelines (Spark/Flink) → compute features → write to store
  Examples: 30-day aggregates, embedding pre-computation, graph features

ONLINE (real-time serving):
  Request arrives → feature store lookup → return pre-computed features
  Latency requirement: < 5 ms (within your 10-30 ms budget)
  
  Storage options:
    Redis/ElastiCache:  < 1 ms, limited by memory, $$$
    DynamoDB/Bigtable:  3-10 ms, unlimited scale, $$
    In-process cache:   < 0.1 ms, limited by pod memory, staleness risk

Feature freshness trade-offs:
  Real-time (< 1 sec): streaming computation (Flink), expensive
  Near-real-time (minutes): micro-batch, moderate cost
  Batch (hours): Spark jobs, cheap but stale
  
  For fraud: velocity features must be real-time (< 1 sec)
  For search: embedding features can be batch (updated hourly)

Cache invalidation (the hard problem):
  TTL-based: simple but risks serving stale data
  Event-driven: feature pipeline pushes invalidation on update
  Versioned: every lookup includes version, mismatch triggers async refresh
```

---

# SECTION 13: REAL-WORLD INFERENCE FAILURE SCENARIOS

---

## 13.1 "Model accuracy drops 5% after deployment, no code change"

```
Diagnosis:
  1. Model version unchanged, same weights
  2. Feature distribution shift detected in monitoring
  3. Root cause: upstream feature pipeline changed encoding
     String categorical → one-hot mapping changed (new category added)
     Model expects old encoding, gets shifted features
     
Root cause: feature/model version mismatch
  Feature store updated schema, model not retrained

Fix:
  - Pin feature schema version to model version
  - Feature contracts: schema checked at serving time
  - Integration tests: model + feature pipeline tested together before deploy
  - Monitoring: feature distribution drift alerts (KL divergence, PSI)
```

## 13.2 "Latency spikes at exactly top-of-hour"

```
Diagnosis:
  1. p99 spikes every 60 minutes (periodic)
  2. Not traffic-related (volume constant)
  3. Correlates with: feature store cache TTL expiration
  4. At top-of-hour: ALL cached features expire simultaneously
     → thundering herd: all requests hit backing store
     → backing store overwhelmed → latency spikes

Root cause: synchronized cache expiration

Fix:
  - Jittered TTL: TTL = 60 min + random(0, 10 min) per key
  - Staggered refresh: refresh 1/60 of cache per minute (not all at once)
  - Double-buffer: keep stale cache, refresh in background, swap atomically
  - Pre-warm: detect upcoming expiration, refresh proactively
```

## 13.3 "GPU OOM during peak traffic"

```
Diagnosis:
  1. Service crashes with CUDA OOM at 2 PM (peak)
  2. Model size: 14 GB. GPU memory: 80 GB. Should be fine!
  3. Check: KV cache grows with batch size
     At peak: batch=64, seq_len=2048
     KV cache per request: 2.56 MB × 2048 = 5.2 GB × 64 = 335 GB > 80 GB!
     
Root cause: KV cache not bounded, grows with traffic

Fix:
  - Set max KV cache memory: vLLM --gpu-memory-utilization 0.9
  - Limit concurrent requests: max_num_seqs=32
  - Preemption: evict lowest-priority requests when memory full
  - Reduce per-request memory: KV quantization (FP8), shorter max_seq_len
  - Scale out: add replicas before hitting memory wall
```

## 13.4 "Embedding search returns irrelevant results for new content"

```
Diagnosis:
  1. Newly published documents not appearing in semantic search
  2. BM25 finds them fine (keyword match works)
  3. FAISS index not updated with new document embeddings!
  
Root cause: index staleness
  Embedding index rebuilt every 4 hours (batch job)
  New documents: visible to BM25 (real-time inverted index) but not FAISS

Fix:
  - Real-time index updates: compute embedding on publish, add to GPU index
  - IVF index supports add() without full rebuild (add to existing clusters)
  - Or: dual-index strategy:
    Base index (rebuilt daily): bulk of documents
    Delta index (real-time): new documents since last rebuild
    Search BOTH, merge results
```

## 13.5 "Model serving 200 ms slower after Kubernetes upgrade"

```
Diagnosis:
  1. Same model, same code, same hardware
  2. K8s upgrade: 1.28 → 1.29
  3. Check cgroup settings: cgroups v2 migration happened!
  4. CPU accounting overhead different in cgroups v2
  5. Also: new kubelet default enabled swap (K8s 1.28+ supports swap)
     → model pages occasionally swapped out → major page faults!

Root cause: K8s upgrade changed cgroup version + enabled swap

Fix:
  - Disable swap for GPU nodes: --fail-swap-on=true (kubelet flag)
  - Verify cgroups v2 CPU accounting: no unexpected throttling
  - Pin memory: mlock in container (CAP_IPC_LOCK)
  - Test K8s upgrades against inference latency BEFORE rolling out
```

---

# SECTION 14: INFERENCE OBSERVABILITY

---

## 14.1 The Four Golden Signals for Inference

```
1. LATENCY (per request):
   - TTFT (Time to First Token): for LLM streaming
   - TPOT (Time Per Output Token): for LLM decode quality
   - E2E latency: from request received to response sent
   - Breakdown: queue_time + prefill_time + decode_time
   
   Histograms, not averages! p50, p90, p99, p99.9

2. THROUGHPUT:
   - Requests/sec (overall)
   - Tokens/sec (for LLMs)
   - GPU utilization % (are we near capacity?)
   - Batch fill rate (are batches full?)

3. ERRORS:
   - HTTP 5xx rate
   - CUDA OOM events
   - Model timeout rate (request exceeded deadline)
   - Fallback activation rate (primary model unavailable)

4. SATURATION:
   - GPU memory utilization %
   - Request queue depth
   - KV cache utilization % (for LLMs)
   - In-flight requests / max concurrent
```

## 14.2 Distributed Tracing for Inference

```
A fraud scoring request spans multiple components:

trace_id: abc123
├── [gateway]        2 ms  (routing, auth)
├── [feature_store]  5 ms  (Redis lookup)
├── [hot_scoring]    8 ms  (in-process models)
│   ├── [xgboost]     0.5 ms
│   ├── [bert_gpu]    5 ms
│   ├── [faiss_gpu]   0.7 ms
│   └── [rules]       0.1 ms
├── [decision]       0.5 ms (combine scores)
└── [publish_event]  0.3 ms (to warm path)

Each span captures:
  - Duration
  - Model version used
  - Batch size
  - GPU device ID
  - Cache hit/miss
  - Queue wait time

Tools: OpenTelemetry + eBPF auto-instrumentation (zero code changes)
       Custom spans for GPU operations (NVTX ranges → OTel spans)
```

## 14.3 DCGM Metrics for Production Monitoring

```yaml
# Prometheus scrape config for DCGM exporter:
# Key metrics to dashboard:

# Utilization
DCGM_FI_DEV_GPU_UTIL:           "GPU SM utilization %"
DCGM_FI_DEV_MEM_COPY_UTIL:     "Memory bandwidth utilization %"
DCGM_FI_DEV_TENSOR_ACTIVE:     "Tensor Core utilization %"

# Memory
DCGM_FI_DEV_FB_USED:           "GPU memory used (bytes)"
DCGM_FI_DEV_FB_FREE:           "GPU memory free (bytes)"

# Health
DCGM_FI_DEV_GPU_TEMP:          "GPU temperature"
DCGM_FI_DEV_POWER_USAGE:       "Power draw (watts)"
DCGM_FI_DEV_ECC_SBE_VOL_TOTAL: "Single-bit ECC errors (correctable)"
DCGM_FI_DEV_ECC_DBE_VOL_TOTAL: "Double-bit ECC errors (FATAL)"
DCGM_FI_DEV_XID_ERRORS:        "Xid error count"

# Throttling
DCGM_FI_DEV_CLOCK_THROTTLE_REASONS: "Why clocks are throttled"

# Alerts:
  GPU_TEMP > 80°C:                    WARNING
  ECC_DBE > 0:                        CRITICAL (replace GPU)
  GPU_UTIL < 10% for 5 min:           WARNING (something broken)
  FB_USED > 90%:                      WARNING (approaching OOM)
```

---

# SECTION 15: ADVANCED INFERENCE INTERVIEW QUESTIONS (Grind-Proof)

## "Design a serving system for 10,000 req/s with 15 ms p99 SLA"

→ Capacity planning:
```
Step 1: Single-replica capacity
  Model: BERT-base on H100 with TensorRT
  Per-replica throughput: ~1,500 req/s at p99 < 10 ms (batch=16)
  
Step 2: Replicas needed
  10,000 / 1,500 = 7 replicas minimum
  Add 30% headroom: 7 × 1.3 = 10 replicas
  Add 1 for rolling update: 11 replicas
  
Step 3: Latency budget
  Network: 1 ms (Cilium, same DC)
  Queue wait: 2 ms (at 70% utilization)
  Inference: 8 ms (TensorRT, batch=16)
  Response: 1 ms
  Total: 12 ms p99 (under 15 ms SLA with 3 ms headroom)

Step 4: Scaling policy
  HPA metric: request queue depth > 10 → scale up
  Scale-down: 5 min stabilization (avoid thrashing)
  Max: 20 replicas (cost cap)
```

## "How do you handle a model that needs 2 GPUs but you want low latency?"

→ Tensor Parallelism with NVLink:
  - Split model across 2 GPUs connected via NVLink (900 GB/s)
  - Each GPU holds half the layers' weights
  - All-Gather after each layer (NVLink latency: ~5 µs)
  - Effective latency: single-GPU latency + (num_layers × 5 µs) ≈ +0.4 ms
  - Much better than Pipeline Parallelism (which adds full-layer latency)
  
  Key: MUST be NVLink-connected (PCIe would add 10× more latency per sync)

## "Compare vLLM vs TensorRT-LLM for production deployment"

→ Decision matrix:
```
| Factor | vLLM | TensorRT-LLM |
|--------|------|--------------|
| Ease of deployment | pip install, run | build engine (minutes), more config |
| Model support | 100+ models | fewer, but growing |
| Raw performance | good | 10-30% faster (compiled kernels) |
| FP8 support | yes | yes (better calibration tools) |
| Prefix caching | automatic | manual session API |
| Multi-GPU | TP via Ray | TP native, PP support |
| Customization | Python (easy) | C++ plugin (harder) |
| Debugging | Python traceback | less visibility |
| Community | large, active | NVIDIA-maintained |

Choose vLLM when: fast iteration, many model types, team is Python-heavy
Choose TRT-LLM when: max perf on NVIDIA hardware, fewer model types, C++ team
Choose both: vLLM for dev/staging, TRT-LLM for high-traffic production
```

## "What happens when your inference service receives 3× normal traffic suddenly?"

→ Defense layers:
1. **Load balancer**: distribute across replicas (already at higher per-replica load)
2. **Request queue**: absorbs burst (queue grows, latency increases proportionally)
3. **Adaptive batching**: batch size increases (more efficient GPU use)
4. **Load shedding**: if queue > threshold, reject with 429 (protect remaining)
5. **HPA triggers**: scale-up signal sent (4-5 min until new capacity)
6. **Fallback**: if latency exceeds SLA, switch to lighter model (INT4 vs FP16)
7. **Circuit breaker**: if everything fails, return cached/default response

Timeline:
  T+0: burst starts, queue grows
  T+1 sec: adaptive batching kicks in (helps ~30% more throughput)
  T+5 sec: queue > threshold, load shedding starts (503 for excess)
  T+30 sec: HPA decides to scale up
  T+90 sec: new pod starting (pulling image, loading model)
  T+180 sec: new pod ready, serves traffic, queue drains

## "How do you decide between in-process inference vs separate service?"

→ Decision tree:
```
Is the model stateful (KV cache, growing memory)?
  YES → Separate service (needs dedicated memory management)
  NO →
    Does the model need cross-request batching to be efficient?
      YES → Separate service (Triton/vLLM handles batching)
      NO →
        Is latency budget < 5 ms?
          YES → In-process (can't afford network hop)
          NO →
            Is model > 2 GB GPU memory?
              YES → Separate (avoid memory pressure on hot path)
              NO → In-process (embed it, zero overhead)

Your pipeline:
  XGBoost (CPU, stateless, 0.5 ms): in-process ✓
  BERT classifier (GPU, stateless, 5 ms): in-process ✓
  FAISS search (GPU, stateless, 0.7 ms): in-process ✓
  7B LLM (GPU, STATEFUL, 200 ms): separate service ✓
```

---

# SECTION 16: SGLang — STRUCTURED GENERATION LANGUAGE

---

## 16.1 What SGLang Is (and Why AMD Cares)

```
SGLang = Structured Generation Language (UC Berkeley / LMSYS)
A serving framework AND programming interface for LLMs:
  - Like vLLM: continuous batching, paged attention, TP
  - PLUS: first-class support for structured generation, constrained decoding
  - PLUS: RadixAttention (advanced prefix caching with tree structure)
  - PLUS: native Python frontend for complex LLM programs

Why AMD pushes SGLang alongside vLLM:
  1. SGLang has excellent ROCm support (co-developed with AMD)
  2. RadixAttention gives significant throughput gains for multi-turn
  3. Constrained decoding (JSON/regex) without speed penalty
  4. Simpler to customize than vLLM for AMD-specific optimizations
```

## 16.2 SGLang vs vLLM vs TRT-LLM Comparison

```
| Feature | vLLM | SGLang | TensorRT-LLM |
|---------|------|--------|--------------|
| Continuous batching | ✓ | ✓ | ✓ |
| Paged attention | ✓ (PagedAttn) | ✓ (PagedAttn) | ✓ (in-flight batching) |
| Prefix caching | ✓ (hash-based) | ✓ (RadixAttention — tree) | ✓ (session API) |
| Constrained decode | basic (outlines) | native (fast) | limited |
| Multi-turn optimization | prefix cache | RadixAttention (best) | session pinning |
| AMD/ROCm support | good | excellent | none (NVIDIA only) |
| NVIDIA perf | excellent | very good | best |
| TP/PP | TP via Ray | TP native | TP + PP native |
| Speculative decode | ✓ | ✓ | ✓ |
| Python frontend | OpenAI API | SGLang programs + API | Triton + API |
| Ease of deployment | pip install | pip install | build engine (complex) |
| Community | largest | growing fast | NVIDIA-maintained |

Decision for AMD deployment:
  Default: vLLM (most models, largest community, good ROCm)
  When multi-turn/structured heavy: SGLang (RadixAttention wins)
  When max NVIDIA perf needed: TRT-LLM (not for AMD)
```

## 16.3 RadixAttention (SGLang's Key Innovation)

```
Problem: standard prefix caching is flat (exact prefix match required)
  Request 1: [system prompt] + [user: "What is X?"]
  Request 2: [system prompt] + [user: "What is Y?"]
  Both share system prompt → cache hit ✓
  
  But:
  Request 3: [system prompt] + [user: "What is X?"] + [assistant: "..."] + [user: "Follow-up"]
  This PARTIALLY matches Request 1 → flat cache can't handle efficiently!

RadixAttention: tree-based KV cache management
  
  Cache organized as RADIX TREE (trie):
  Root: [system prompt KV]
    ├── [user: "What is X?" KV]
    │     └── [assistant: "..." KV]  
    │           └── [user: "Follow-up" KV]  ← reuse entire branch!
    ├── [user: "What is Y?" KV]
    └── [user: "What is Z?" KV]

  Benefits:
  - ANY shared prefix (not just system prompt) → cache hit
  - Multi-turn conversations: reuse all previous turns' KV
  - Fork-based programs: share computation across branches
  - Automatic LRU eviction at leaf level (keep popular paths hot)

  Performance impact:
    Multi-turn chatbot (10-turn average):
      vLLM prefix cache: reuse system prompt only → save 500 tokens
      SGLang RadixAttn: reuse all prior turns → save 5,000+ tokens
      Result: 3-5× TTFT improvement for multi-turn conversations
```

## 16.4 SGLang on AMD MI300X (Production Setup)

```bash
# Install SGLang with ROCm:
pip install "sglang[all]" --find-links https://docs.amd.com/

# Launch SGLang server on MI300X:
python -m sglang.launch_server \
  --model-path meta-llama/Llama-3-70B \
  --tp 1 \                              # Single MI300X (192 GB fits 70B FP16!)
  --dtype float16 \
  --port 30000 \
  --mem-fraction-static 0.88 \
  --enable-flashinfer \                  # FlashInfer attention backend
  --chunked-prefill-size 4096 \
  --schedule-policy lpm                  # Longest Prefix Match (RadixAttn)

# ROCm-specific environment:
export HIP_VISIBLE_DEVICES=0
export HSA_OVERRIDE_GFX_VERSION=9.4.2
export PYTORCH_TUNABLEOP_ENABLED=1

# SGLang programming interface (unique feature):
import sglang as sgl

@sgl.function
def fraud_analysis(s, transaction):
    s += sgl.system("You are a fraud analyst.")
    s += sgl.user(f"Analyze this transaction: {transaction}")
    s += sgl.assistant(sgl.gen("analysis", max_tokens=200))
    s += sgl.user("Rate fraud probability 0-100 as JSON:")
    s += sgl.assistant(sgl.gen("score", 
                                max_tokens=50,
                                regex=r'\{"fraud_score": \d{1,3}\}'))  # constrained!

# The regex constraint ensures valid JSON output WITHOUT retry loops
# SGLang compiles constraint into finite automaton → zero-overhead decoding
```

## 16.5 Constrained Decoding Performance

```
Traditional approach (retry-based):
  1. Generate freely
  2. Parse output (JSON, regex, schema)
  3. If invalid: retry (waste GPU compute!)
  Retry rate: 10-30% for complex schemas
  Effective throughput: reduced by retry overhead

SGLang constrained decoding:
  1. Compile constraint to finite state machine (FSM) at request start
  2. At each decode step: mask logits to only valid next tokens
  3. Generation is ALWAYS valid — zero retries
  
  Performance:
    Unconstrained: 100 tokens/sec
    With regex constraint: 98 tokens/sec (2% overhead from masking)
    With JSON schema: 95 tokens/sec (5% overhead)
    
    vs retry approach: 70 tokens/sec effective (30% wasted on retries)
    
    SGLang constrained: 35% faster than retry-based for structured output

Use cases for inference:
  - API responses (must be valid JSON)
  - Classification (constrain to valid labels)
  - Entity extraction (constrain to schema)
  - Code generation (constrain to valid syntax)
```

---

# SECTION 17: INFERENCE PERFORMANCE PLAYBOOK (Customer-Ready)

---

## 17.1 Systematic Optimization Sequence

```
Always optimize in this order (each step builds on previous):

Step 1: Quantization (biggest single win)
  FP32 → FP16: 2× memory reduction, same quality
  FP16 → FP8: 2× memory reduction, < 1% quality loss
  FP16 → INT4 (GPTQ/AWQ): 4× memory reduction, 1-3% quality loss
  
  Impact: determines how many GPUs you need!
  70B FP16: 140 GB → needs 2×H100 or 1×MI300X
  70B FP8:  70 GB → fits on 1×H100!
  70B INT4: 35 GB → fits on 1×A100 40GB

Step 2: Attention optimization
  Standard attention → FlashAttention 2/3
  Impact: 2-3× prefill speedup, no quality loss
  Enables longer context lengths (memory efficient)

Step 3: KV cache optimization
  FP16 KV → FP8 KV: 2× more concurrent sequences
  Paged attention: eliminates memory fragmentation
  Impact: higher batch size possible → better throughput

Step 4: Continuous batching
  Static → continuous: 3-5× throughput improvement
  Chunked prefill: prevents decode stalls during long prefills

Step 5: Prefix caching
  System prompts reused across requests
  Impact: 2-10× TTFT improvement (depends on prefix length)
  RadixAttention (SGLang): extends to multi-turn conversations

Step 6: Speculative decoding
  Draft model generates candidates, main model verifies in batch
  Impact: 1.5-2.5× decode throughput (acceptance rate dependent)
  Best for: small draft model + large main model (7B drafts for 70B)

Step 7: HIP/CUDA Graphs
  Capture kernel launch sequence, replay as single launch
  Impact: 10-30% decode latency reduction (reduces launch overhead)
  Critical for: small batches where launch overhead is significant
```

## 17.2 Inference Performance Debugging Flowchart

```
SYMPTOM: Low throughput (tokens/sec below expectations)
  │
  ├─ GPU utilization < 50%?
  │   ├─ YES → batch too small or pipeline stall
  │   │   ├─ Check: request rate (is there traffic?)
  │   │   ├─ Check: max_num_seqs setting (allow more concurrent)
  │   │   └─ Check: prefill blocking decode (enable chunked prefill)
  │   └─ NO (GPU util high) → GPU is working, but not efficiently
  │       ├─ Check: memory bandwidth utilization (rocprof/ncu)
  │       ├─ If BW-bound: model too large for batch → quantize
  │       └─ If compute-bound: smaller model or use Tensor Cores better
  │
SYMPTOM: High TTFT (time to first token)
  │
  ├─ Is prefill compute-bound?
  │   ├─ YES → long input sequence, normal
  │   │   ├─ Fix: chunked prefill (overlap with decode)
  │   │   ├─ Fix: prefix caching (avoid recomputing system prompt)
  │   │   └─ Fix: quantize model → faster per-layer computation
  │   └─ NO → something blocking prefill from starting
  │       ├─ Check: queue depth (too many waiting?)
  │       ├─ Check: memory pressure (OOM causing preemption)
  │       └─ Check: scheduling delay (continuous batch scheduler bug)
  │
SYMPTOM: High TPOT (slow decode)
  │
  ├─ Always slow, or intermittent?
  │   ├─ ALWAYS → memory-bandwidth limited
  │   │   ├─ Model on too few GPUs → add TP
  │   │   ├─ KV cache thrashing L2 → reduce batch or quantize KV
  │   │   └─ Not using CUDA/HIP Graphs → enable them
  │   └─ INTERMITTENT → tail latency issue
  │       ├─ Check: GPU thermal throttling (rocm-smi --showtemp)
  │       ├─ Check: CFS throttling on host CPU (cpu.stat)
  │       ├─ Check: NUMA misalignment (numastat)
  │       └─ Check: memory compaction (THP disabled?)
```

## 17.3 AMD-Specific Inference Optimizations

```
MI300X tuning knobs not available on NVIDIA:

1. TunableOp (automatic GEMM tuning):
   PYTORCH_TUNABLEOP_ENABLED=1
   PYTORCH_TUNABLEOP_TUNING=1  # first run: benchmark all GEMM configs
   PYTORCH_TUNABLEOP_FILENAME=gemm_cache.csv  # persist results
   
   Impact: 10-20% throughput improvement (auto-selects best kernel per shape)
   First inference: slow (tuning), subsequent: cached + fast

2. Infinity Cache awareness:
   MI300X has 256 MB L3 (Infinity Cache)
   KV cache hot set that fits in 256 MB → served from cache, not HBM
   Result: effective bandwidth > 5.3 TB/s for hot data
   
   Tuning: keep per-sequence KV within cache-friendly access patterns
   (Framework handles this — but explains why MI300X decode is fast)

3. Composable Kernel (CK) library:
   AMD's equivalent of CUTLASS
   Hand-tuned kernels for: GEMM, attention, layernorm, etc.
   SGLang and vLLM use CK kernels on ROCm automatically

4. hipGraph (HIP Graphs):
   Same concept as CUDA Graphs, HIP API
   Capture decode step → replay with minimal overhead
   Critical for small-batch decode (where kernel launch > compute)

5. Multi-GCD considerations (MI250X/MI300A):
   MI250X = 2 Graphics Compute Dies (GCDs) — appears as 2 "GPUs"
   Each GCD: 64 GB, separate memory controller
   For inference: treat as TP=2 within single physical GPU
   RCCL handles inter-GCD communication (Infinity Fabric, fast)
```

---

# SECTION 18: INFERENCE INTERVIEW QUESTIONS (AMD JD-Targeted)

## "How would you benchmark vLLM on MI300X and produce a customer playbook?"

→ Methodology:
```
1. Environment setup:
   - Single MI300X node, ROCm 6.x, vLLM latest with ROCm
   - Set HSA_OVERRIDE_GFX_VERSION, enable TunableOp
   - Run warm-up: 100 requests to trigger GEMM tuning + JIT

2. Baseline measurement:
   - Model: Llama-3-70B (FP16, fits in 192 GB single GPU!)
   - ShareGPT dataset (realistic conversation distribution)
   - Sweep: request rate 1→100 req/s, measure TTFT/TPOT/throughput
   - Record saturation point (QPS where p99 exceeds SLA)

3. Optimization sweep:
   - FP16 → FP8: expect 1.5-2× throughput
   - Enable prefix caching: measure cache hit rate + TTFT reduction
   - Enable chunked prefill: measure TPOT stability
   - HIP Graphs: measure decode latency reduction

4. Comparison table:
   Each config: TTFT p50/p99, TPOT p50/p99, max throughput, GPU util
   Show cumulative improvement curve

5. Customer-ready deliverables:
   - Deployment YAML (K8s manifests)
   - Performance data sheet (the table above)
   - Tuning guide (which knobs for their traffic pattern)
   - Capacity calculator (given their QPS, how many GPUs)
```

## "What's the advantage of MI300X over H100 for inference specifically?"

→ Three key advantages:
1. **Memory capacity (192 GB vs 80 GB)**:
   - 70B FP16 fits on 1×MI300X, needs 2×H100
   - Fewer GPUs = no TP communication overhead = simpler deployment
   - 405B FP8 on 3×MI300X vs 6×H100

2. **Memory bandwidth (5.3 TB/s vs 3.35 TB/s)**:
   - LLM decode is memory-bandwidth bound
   - 58% more bandwidth → 40-50% faster decode at same batch size
   - Particularly impactful for small batches (latency-sensitive use cases)

3. **Infinity Cache (256 MB)**:
   - KV cache hot set served from L3 at higher effective bandwidth
   - Multi-turn conversations benefit (recent turns' KV stays in cache)
   - No equivalent on H100 (only 50 MB L2)

## "How do you handle the differences between CUDA and ROCm in production?"

→ Abstraction strategy:
```
Layer 1: Framework abstraction (PyTorch, vLLM, SGLang)
  Same Python API, backend-agnostic
  torch.device("cuda") works on both (HIP translates)
  vLLM: same config, different base image

Layer 2: Container abstraction
  NVIDIA: nvidia/cuda:12.x-runtime
  AMD: rocm/dev-ubuntu-22.04:6.x
  Same application code, different base layer

Layer 3: K8s abstraction
  NVIDIA: nvidia.com/gpu resource
  AMD: amd.com/gpu resource
  Same pod spec, different resource name + nodeSelector

Layer 4: Custom kernels (if any)
  CUDA → HIP via hipify-perl (automatic translation)
  Most CUDA API calls have 1:1 HIP equivalent
  Exceptions: warp-level primitives (64-wide on AMD vs 32 on NVIDIA)

Layer 5: Profiling
  NVIDIA: ncu, nsys
  AMD: rocprof, omniperf
  Different tools, same methodology (roofline, occupancy, memory BW)
```

## "Design multi-tenant inference platform with SLA isolation"

→ Architecture:
```
Tier 1 (Production, guaranteed SLA):
  - Dedicated GPU nodes (no sharing)
  - PriorityClass: 1000000 (never preempted)
  - ResourceQuota: guaranteed GPU count
  - MIG for small models (hardware isolation)
  - Kueue admission: always instant (within quota)

Tier 2 (Standard, best-effort SLA):
  - Shared GPU pool with resource quotas
  - PriorityClass: 500000 (preempt Tier 3 only)
  - Burst above quota allowed if cluster has capacity
  - May queue during peak (Kueue manages)

Tier 3 (Experimental, no SLA):
  - Opportunistic scheduling (use idle capacity)
  - PriorityClass: 100 (preempted by everything)
  - Spot-like: can be evicted with 30-sec warning
  - Run batch embedding jobs, model evaluation here

Cross-tier observability:
  - Per-tier SLO dashboard (latency, availability)
  - Noisy neighbor detection (Tier 2 impacting Tier 1)
  - Preemption tracking (how often Tier 3 evicted)
  - Cost attribution per team/namespace
```

---
---

# SECTION 19: PER-TENANT KV CACHE ISOLATION (End-to-End Implementation)

---

## 19.1 The Vulnerability: Shared Prefix Caching in Multi-Tenant Inference

```
DEFAULT vLLM BEHAVIOR (enable_prefix_caching=True):

Request from Tenant A:  [system_prompt: 500 tokens] + [user query: 200 tokens]
Request from Tenant B:  [system_prompt: 500 tokens] + [user query: 150 tokens]

vLLM prefix cache hash:  hash(token_block_content)
  Both tenants have SAME system prompt → SAME hash → SHARED KV BLOCKS

WHY THIS IS A SECURITY VULNERABILITY:
  KV values are NOT just the prefix tokens.
  Due to causal attention, KV values at position i are computed with
  ALL prior context visible. If Tenant A's KV was computed with
  tenant-specific context in a prior turn, that context is ENCODED
  in the KV values — and now Tenant B reads those values.

  Think of it as: the KV cache is a "compressed memory" of everything
  the model has seen. Sharing it = sharing that memory across tenants.

ATTACK VECTOR (proven):
  1. Tenant A asks: "What are our auto loan underwriting thresholds?"
  2. Model generates answer, KV cache stored (prefix + answer tokens)
  3. Tenant B sends request with matching prefix
  4. vLLM serves cached KV from Tenant A's computation
  5. Tenant B's generation is "primed" with Tenant A's context
  6. Result: Tenant B sees auto loan data they shouldn't have access to
```

## 19.2 Architecture: Tenant-Scoped Cache Keys

```
┌─────────────────────────────────────────────────────────────────┐
│                     API GATEWAY (Kong / Envoy)                   │
│  JWT validation → extract tenant_id from claims                 │
│  Set header: X-Tenant-ID: {tenant_id}                           │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    vLLM API SERVER                                │
│  Read X-Tenant-ID header (or reject 400 if missing)             │
│  Attach tenant_id to SamplingParams / SequenceGroup              │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    SCHEDULER                                      │
│  SequenceGroup carries tenant_id                                 │
│  Passes to BlockManager on allocate()                            │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│              BLOCK MANAGER (PrefixCachingBlockAllocator)          │
│                                                                  │
│  OLD hash: hash(parent_hash, *token_ids)                         │
│  NEW hash: hash(tenant_id, parent_hash, *token_ids)              │
│                                                                  │
│  Same tokens + different tenant → DIFFERENT hash → NO sharing    │
│  Same tokens + same tenant → SAME hash → sharing OK ✓           │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│              GPU BLOCK TABLE                                      │
│  Physical KV blocks on GPU memory                                │
│  Each block tagged with: content_hash + tenant_id                │
│  Evictor: LRU per-tenant (prevents one tenant evicting another)  │
└─────────────────────────────────────────────────────────────────┘
```

## 19.3 Implementation: Step-by-Step Code Changes

### Step 1: API Layer — Extract and Validate tenant_id

```python
# vllm/entrypoints/openai/api_server.py

from fastapi import HTTPException, Request

async def create_chat_completion(
    request: ChatCompletionRequest, 
    raw_request: Request
):
    # FAIL-CLOSED: reject if no tenant identity
    tenant_id = raw_request.headers.get("X-Tenant-ID")
    if not tenant_id:
        # Also check extra_body for direct API usage
        tenant_id = getattr(request, "tenant_id", None)
    
    if not tenant_id:
        raise HTTPException(
            status_code=400,
            detail="X-Tenant-ID header required for multi-tenant serving"
        )
    
    # Sanitize: tenant_id must be alphanumeric (prevent injection)
    if not tenant_id.isalnum() or len(tenant_id) > 64:
        raise HTTPException(
            status_code=400,
            detail="Invalid tenant_id format"
        )
    
    # Pass to engine
    results = await engine.generate(
        prompt=prompt,
        sampling_params=sampling_params,
        request_id=request_id,
        tenant_id=tenant_id,  # NEW
    )
```

### Step 2: Sequence Group — Carry tenant_id Through Scheduling

```python
# vllm/sequence.py

class SequenceGroup:
    def __init__(
        self,
        request_id: str,
        seqs: List[Sequence],
        sampling_params: SamplingParams,
        arrival_time: float,
        tenant_id: str = "default",  # NEW FIELD
        ...
    ):
        self.request_id = request_id
        self.seqs = seqs
        self.sampling_params = sampling_params
        self.arrival_time = arrival_time
        self.tenant_id = tenant_id  # Propagated to block allocator
```

### Step 3: AsyncLLMEngine — Thread tenant_id to Scheduler

```python
# vllm/engine/async_llm_engine.py

async def generate(
    self,
    prompt: PromptType,
    sampling_params: SamplingParams,
    request_id: str,
    tenant_id: str = "default",  # NEW
    ...
) -> AsyncGenerator[RequestOutput, None]:
    
    # Create SequenceGroup with tenant context
    seq_group = SequenceGroup(
        request_id=request_id,
        seqs=[seq],
        sampling_params=sampling_params,
        arrival_time=time.time(),
        tenant_id=tenant_id,  # Propagated
    )
    
    # Add to scheduler
    self.scheduler.add_seq_group(seq_group)
```

### Step 4: Block Allocator — Tenant-Scoped Hash (THE CRITICAL CHANGE)

```python
# vllm/core/block/prefix_caching_block.py

class PrefixCachingBlock(Block):
    """Block with content-based hashing for prefix caching."""
    
    def __init__(
        self,
        prev_block: Optional[Block],
        token_ids: List[int],
        block_size: int,
        allocator: BlockAllocator,
        tenant_id: str = "default",  # NEW
        block_id: Optional[int] = None,
    ):
        self._prev_block = prev_block
        self._token_ids = token_ids
        self._block_size = block_size
        self._allocator = allocator
        self._tenant_id = tenant_id  # Stored for hash computation
        self._block_id = block_id
        self._content_hash: Optional[int] = None
    
    @property
    def content_hash(self) -> Optional[int]:
        """Compute tenant-scoped content hash.
        
        SECURITY INVARIANT: Two blocks with identical token content
        but different tenant_ids MUST produce different hashes.
        This makes cross-tenant KV sharing architecturally impossible.
        """
        if self._content_hash is None:
            if not self.is_full:
                return None
            
            parent_hash = (
                self._prev_block.content_hash 
                if self._prev_block is not None 
                else None
            )
            
            # TENANT-SCOPED HASH — the security boundary
            self._content_hash = hash((
                self._tenant_id,   # ← THIS IS THE FIX
                parent_hash,
                *self._token_ids
            ))
        
        return self._content_hash
```

### Step 5: Block Manager — Pass tenant_id on Allocation

```python
# vllm/core/block_manager.py

class BlockSpaceManagerV2:
    
    def allocate(self, seq_group: SequenceGroup) -> None:
        """Allocate block table for a new sequence group."""
        for seq in seq_group.get_seqs(status=SequenceStatus.WAITING):
            block_table = self._allocate_sequence(
                seq,
                tenant_id=seq_group.tenant_id  # PASS THROUGH
            )
            self._block_tables[seq.seq_id] = block_table
    
    def _allocate_sequence(
        self, 
        seq: Sequence, 
        tenant_id: str = "default"
    ) -> BlockTable:
        """Create blocks with tenant-scoped hashing."""
        token_ids = seq.get_token_ids()
        blocks: List[Block] = []
        
        for i in range(0, len(token_ids), self.block_size):
            block_tokens = token_ids[i:i + self.block_size]
            
            block = PrefixCachingBlock(
                prev_block=blocks[-1] if blocks else None,
                token_ids=block_tokens,
                block_size=self.block_size,
                allocator=self._allocator,
                tenant_id=tenant_id,  # SCOPED CACHE KEY
            )
            blocks.append(block)
        
        return BlockTable(blocks=blocks)
```

### Step 6: Evictor — Per-Tenant LRU Fairness

```python
# vllm/core/evictor.py

class TenantAwareLRUEvictor:
    """LRU evictor that prevents one tenant from monopolizing cache.
    
    Policy: Each tenant gets a fair share of cache blocks.
    When evicting, prefer blocks from the tenant that is OVER its quota.
    This prevents a high-traffic tenant from evicting a low-traffic
    tenant's hot cache entries.
    """
    
    def __init__(self, max_blocks: int, max_tenants: int = 100):
        self._blocks: Dict[int, EvictableBlock] = {}  # hash → block
        self._tenant_blocks: Dict[str, OrderedDict] = {}  # tenant → LRU
        self._max_blocks = max_blocks
        self._fair_share = max_blocks // max_tenants
    
    def add(self, block: EvictableBlock, tenant_id: str) -> None:
        self._blocks[block.content_hash] = block
        if tenant_id not in self._tenant_blocks:
            self._tenant_blocks[tenant_id] = OrderedDict()
        self._tenant_blocks[tenant_id][block.content_hash] = block
    
    def evict(self) -> EvictableBlock:
        """Evict from the most over-quota tenant first."""
        # Find tenant with most blocks over fair share
        over_quota = [
            (tid, len(blocks) - self._fair_share)
            for tid, blocks in self._tenant_blocks.items()
            if len(blocks) > self._fair_share
        ]
        
        if over_quota:
            # Evict LRU block from most over-quota tenant
            over_quota.sort(key=lambda x: -x[1])
            target_tenant = over_quota[0][0]
        else:
            # All under quota — global LRU
            target_tenant = min(
                self._tenant_blocks,
                key=lambda t: next(iter(self._tenant_blocks[t].values())).last_access
            )
        
        # Pop oldest block from target tenant
        _, block = self._tenant_blocks[target_tenant].popitem(last=False)
        del self._blocks[block.content_hash]
        return block
    
    def access(self, block_hash: int, tenant_id: str) -> None:
        """Mark block as recently accessed (move to end of LRU)."""
        if block_hash in self._tenant_blocks.get(tenant_id, {}):
            self._tenant_blocks[tenant_id].move_to_end(block_hash)
```

## 19.4 Gateway Integration: JWT → tenant_id Flow

```
END-TO-END REQUEST FLOW:

1. Client sends request with Bearer JWT token
   POST /v1/chat/completions
   Authorization: Bearer eyJhbGciOiJSUzI1NiI...
   
2. API Gateway (Kong/Envoy) validates JWT:
   - Verifies signature (RS256, public key from JWKS endpoint)
   - Checks exp, iss, aud claims
   - Extracts: claims.tenant_id = "credit_cards"
   - Sets header: X-Tenant-ID: credit_cards
   
3. vLLM API server reads X-Tenant-ID (trusts gateway — internal network)
   - Does NOT re-validate JWT (gateway already did)
   - Rejects if header missing (fail-closed)
   
4. tenant_id flows: API → Engine → Scheduler → BlockManager → Block Hash

GATEWAY CONFIG (Kong example):
```

```yaml
# Kong JWT plugin + header injection
plugins:
  - name: jwt
    config:
      claims_to_verify: [exp]
      key_claim_name: iss
  - name: request-transformer
    config:
      add:
        headers:
          - "X-Tenant-ID:$(jwt.claims.tenant_id)"
      # Remove any client-set X-Tenant-ID (prevent spoofing)
      remove:
        headers:
          - "X-Tenant-ID"
```

```yaml
# Envoy equivalent (ext_authz + Lua filter):
http_filters:
  - name: envoy.filters.http.jwt_authn
    typed_config:
      providers:
        auth0:
          issuer: "https://auth.company.com/"
          audiences: ["inference-api"]
          remote_jwks:
            http_uri:
              uri: "https://auth.company.com/.well-known/jwks.json"
      rules:
        - match: { prefix: "/v1/" }
          requires: { provider_name: "auth0" }
  
  - name: envoy.filters.http.lua
    typed_config:
      inline_code: |
        function envoy_on_request(handle)
          local jwt = handle:headers():get("x-jwt-payload")
          local claims = json.decode(base64_decode(jwt))
          handle:headers():add("X-Tenant-ID", claims.tenant_id)
        end
```

## 19.5 Deployment Topology Options

```
OPTION A: Shared Engine, Scoped Cache (RECOMMENDED — cost optimal)
┌──────────────────────────────────────────────────────┐
│ Single vLLM Engine (per GPU)                          │
│ ┌──────────────────────────────────────────────────┐ │
│ │ KV Cache (GPU Memory)                             │ │
│ │  ┌─────────────┐  ┌─────────────┐  ┌──────────┐ │ │
│ │  │ Tenant A    │  │ Tenant B    │  │ Tenant C │ │ │
│ │  │ blocks      │  │ blocks      │  │ blocks   │ │ │
│ │  │ (hash=A+tok)│  │ (hash=B+tok)│  │(hash=C+t)│ │ │
│ │  └─────────────┘  └─────────────┘  └──────────┘ │ │
│ │  No cross-tenant block sharing possible           │ │
│ └──────────────────────────────────────────────────┘ │
│ Scheduler: shared (all tenants in same batch)         │
│ Model weights: shared (read-only, safe)               │
│ Continuous batching: cross-tenant batching OK          │
│ (batching ≠ cache sharing — weights are stateless)    │
└──────────────────────────────────────────────────────┘

  Pros: Maximum GPU utilization, minimal cost
  Cons: No compute isolation (noisy neighbor at compute level)
  Use when: tenants don't need latency SLA guarantees per-tenant

OPTION B: Per-Tenant Engine (maximum isolation)
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ vLLM Engine A    │  │ vLLM Engine B    │  │ vLLM Engine C    │
│ GPU: MIG 3g.40gb │  │ GPU: MIG 3g.40gb │  │ GPU: Full H100   │
│ Tenant: credit   │  │ Tenant: auto     │  │ Tenant: banking  │
│ Dedicated KV     │  │ Dedicated KV     │  │ Dedicated KV     │
└──────────────────┘  └──────────────────┘  └──────────────────┘

  Pros: Full isolation (memory, compute, cache)
  Cons: Wasteful (each engine loads model weights separately)
  Use when: regulatory requirement, strict SLA per tenant

OPTION C: Hybrid (recommended for Capital One-like deployment)
┌──────────────────────────────────────────────────────────────┐
│ HIGH-VALUE TENANTS: Dedicated engines (Option B)              │
│   credit_cards (high volume, strict SLA) → own H100          │
│   banking (regulated, PCI compliance) → own H100             │
│                                                              │
│ STANDARD TENANTS: Shared engine (Option A)                    │
│   auto_loans, disputes, collections → shared pool            │
│   Tenant-scoped cache keys prevent leakage                   │
│   Cost-effective, slightly weaker compute isolation           │
└──────────────────────────────────────────────────────────────┘
```

## 19.6 Timing Side-Channel Mitigation (Advanced)

```
PROBLEM: Even with scoped cache keys, TIMING reveals information.
  Cache hit: 45 ms (skip prefill)
  Cache miss: 120 ms (full prefill)
  
  In shared households (Apple Siri use case):
  Device A queries "medical symptoms" → KV cached for that user
  Device B (attacker) sends same prefix → measures latency
  If 45 ms → Device A recently asked about medical topics (privacy violation!)

THIS ATTACK WORKS EVEN WITH TENANT-SCOPED KEYS IF:
  - Users within same tenant share a cache (family members = same "household tenant")
  - The attacker can measure response timing accurately

MITIGATION: Constant-Time Response Layer

Architecture:
┌────────────────────────────────────────────────────────────┐
│ CONSTANT-TIME RESPONSE PROXY (sits between client & vLLM)  │
│                                                            │
│ 1. On request arrival: start timer                         │
│ 2. Forward to vLLM backend                                 │
│ 3. Receive response (fast = cache hit, slow = cache miss)  │
│ 4. If response arrived BEFORE target_time:                 │
│       buffer response, wait until target_time, then send   │
│ 5. If response arrived AFTER target_time:                  │
│       send immediately (already past deadline)             │
│                                                            │
│ target_time = p90 of cache-miss latency                    │
│ Result: ALL responses appear to take ~same time            │
│         Timing side-channel eliminated                     │
└────────────────────────────────────────────────────────────┘
```

```python
# Constant-time response proxy (simplified)
import asyncio
import time

class ConstantTimeProxy:
    """Normalize response timing to eliminate cache-hit/miss signal."""
    
    def __init__(self, target_latency_ms: float = 130.0):
        # Set slightly above p90 of cache-miss latency
        self.target_latency = target_latency_ms / 1000.0
    
    async def handle_request(self, request):
        start = time.monotonic()
        
        # Forward to vLLM (may return fast on cache hit)
        response = await self.vllm_client.generate(request)
        
        elapsed = time.monotonic() - start
        remaining = self.target_latency - elapsed
        
        if remaining > 0:
            # Cache hit was fast — add delay to normalize timing
            await asyncio.sleep(remaining)
        
        # Response timing is now constant (within jitter)
        return response
```

```
ALTERNATIVE: Preemptive Cache Warming
  On session start: warm KV cache for ALL common domain prefixes
  (not just the one the user will query)
  
  Result: ALL prefixes have cache hits → no timing differential
  Cost: more GPU memory (cache entries for unused prefixes)
  Tradeoff: memory vs privacy (acceptable for consumer products)

  Implementation:
    warm_prefixes = [
        "You are a music assistant...",
        "You are a health assistant...",
        "You are a shopping assistant...",
        "You are a navigation assistant...",
        "You are a smart home assistant...",
    ]
    # At session start, prefill ALL → all cached
    for prefix in warm_prefixes:
        await engine.prefill_only(prefix, user_id=user_id)
    # Now any user query hits cache → no timing signal
```

## 19.7 Testing & Validation Framework

```python
# tests/test_tenant_kv_isolation.py

import pytest
import hashlib

class TestTenantIsolation:
    """Comprehensive tests for KV cache tenant isolation."""
    
    # --- UNIT TESTS (block-level) ---
    
    def test_different_tenants_different_hashes(self):
        """CRITICAL: identical tokens from different tenants must not collide."""
        tokens = list(range(16))  # arbitrary token block
        
        block_a = PrefixCachingBlock(
            prev_block=None, token_ids=tokens,
            block_size=16, allocator=mock_alloc,
            tenant_id="credit_cards"
        )
        block_b = PrefixCachingBlock(
            prev_block=None, token_ids=tokens,
            block_size=16, allocator=mock_alloc,
            tenant_id="auto_loans"
        )
        
        assert block_a.content_hash != block_b.content_hash
    
    def test_same_tenant_same_hash(self):
        """Performance: same tenant + same tokens SHOULD share."""
        tokens = list(range(16))
        
        block_a = PrefixCachingBlock(
            prev_block=None, token_ids=tokens,
            block_size=16, allocator=mock_alloc,
            tenant_id="credit_cards"
        )
        block_b = PrefixCachingBlock(
            prev_block=None, token_ids=tokens,
            block_size=16, allocator=mock_alloc,
            tenant_id="credit_cards"
        )
        
        assert block_a.content_hash == block_b.content_hash
    
    def test_hash_chain_propagates_tenant(self):
        """Multi-block sequences: tenant propagates through chain."""
        tokens_1 = list(range(16))
        tokens_2 = list(range(16, 32))
        
        # Tenant A chain
        block_a1 = PrefixCachingBlock(None, tokens_1, 16, mock_alloc, "tenant_a")
        block_a2 = PrefixCachingBlock(block_a1, tokens_2, 16, mock_alloc, "tenant_a")
        
        # Tenant B chain (same tokens!)
        block_b1 = PrefixCachingBlock(None, tokens_1, 16, mock_alloc, "tenant_b")
        block_b2 = PrefixCachingBlock(block_b1, tokens_2, 16, mock_alloc, "tenant_b")
        
        # Block 1: different hash (tenant scoped)
        assert block_a1.content_hash != block_b1.content_hash
        # Block 2: also different (parent hash differs → cascades)
        assert block_a2.content_hash != block_b2.content_hash
    
    def test_missing_tenant_rejected(self):
        """Fail-closed: requests without tenant_id are rejected."""
        with pytest.raises(HTTPException) as exc_info:
            await create_chat_completion(
                request=mock_request,
                raw_request=Request(headers={})  # no X-Tenant-ID
            )
        assert exc_info.value.status_code == 400
    
    # --- INTEGRATION TESTS (engine-level) ---
    
    @pytest.mark.asyncio
    async def test_cross_tenant_no_cache_hit(self):
        """End-to-end: different tenants with same prompt don't share cache."""
        engine = create_test_engine(enable_prefix_caching=True)
        
        prompt = "You are a financial assistant. Analyze this:"
        
        # Tenant A request
        await engine.generate(
            prompt=prompt + " credit card fraud pattern",
            sampling_params=SamplingParams(max_tokens=50),
            request_id="r1",
            tenant_id="credit_cards"
        )
        
        # Tenant B request (same prefix!)
        metrics_before = engine.get_cache_metrics()
        await engine.generate(
            prompt=prompt + " auto loan application",
            sampling_params=SamplingParams(max_tokens=50),
            request_id="r2",
            tenant_id="auto_loans"
        )
        metrics_after = engine.get_cache_metrics()
        
        # Tenant B must NOT get a prefix cache hit from Tenant A
        cross_tenant_hits = (
            metrics_after["prefix_cache_hits"] - metrics_before["prefix_cache_hits"]
        )
        assert cross_tenant_hits == 0, "Cross-tenant cache hit detected!"
    
    @pytest.mark.asyncio
    async def test_same_tenant_gets_cache_hit(self):
        """Performance preserved: same tenant reuses cache."""
        engine = create_test_engine(enable_prefix_caching=True)
        
        prompt = "You are a financial assistant. Analyze this:"
        
        # First request
        await engine.generate(
            prompt=prompt + " pattern A",
            sampling_params=SamplingParams(max_tokens=50),
            request_id="r1",
            tenant_id="credit_cards"
        )
        
        # Second request (same tenant, same prefix)
        metrics_before = engine.get_cache_metrics()
        await engine.generate(
            prompt=prompt + " pattern B",
            sampling_params=SamplingParams(max_tokens=50),
            request_id="r2",
            tenant_id="credit_cards"  # SAME tenant
        )
        metrics_after = engine.get_cache_metrics()
        
        # Same tenant SHOULD get prefix cache hit
        same_tenant_hits = (
            metrics_after["prefix_cache_hits"] - metrics_before["prefix_cache_hits"]
        )
        assert same_tenant_hits > 0, "Same-tenant cache hit expected!"
    
    # --- CHAOS / SECURITY TESTS ---
    
    def test_tenant_id_injection_blocked(self):
        """Malicious tenant_id values are rejected."""
        malicious_ids = [
            "tenant_a\x00tenant_b",  # null byte injection
            "a" * 1000,              # overflow attempt
            "../../../etc/passwd",   # path traversal
            "tenant'; DROP TABLE--", # SQL injection (shouldn't matter but defense in depth)
        ]
        for bad_id in malicious_ids:
            with pytest.raises(HTTPException):
                validate_tenant_id(bad_id)
    
    @pytest.mark.asyncio
    async def test_timing_side_channel_mitigated(self):
        """Verify constant-time proxy normalizes response timing."""
        proxy = ConstantTimeProxy(target_latency_ms=130.0)
        
        # Simulate cache hit (fast) and cache miss (slow)
        timings = []
        for _ in range(100):
            start = time.monotonic()
            await proxy.handle_request(mock_request)
            timings.append(time.monotonic() - start)
        
        # Standard deviation of timings should be low (normalized)
        import numpy as np
        std_dev = np.std(timings)
        assert std_dev < 0.005, f"Timing variance too high: {std_dev}"
```

## 19.8 Monitoring & Alerting

```yaml
# Prometheus metrics for tenant KV cache isolation

# CRITICAL ALERT: Should NEVER fire after fix
- alert: KVCacheCrossTenantHit
  expr: vllm_kv_cache_cross_tenant_hits_total > 0
  for: 0m
  severity: critical
  annotations:
    summary: "Cross-tenant KV cache sharing detected — data isolation breach"
    runbook: "Immediately disable prefix caching. Engage security team."

# Per-tenant cache metrics
- record: vllm:kv_cache_hit_rate_by_tenant
  expr: |
    rate(vllm_kv_cache_hits_total{hit_type="prefix"}[5m]) 
    / rate(vllm_kv_cache_lookups_total[5m])
  labels:
    tenant_id: "{{ $labels.tenant_id }}"

# Fairness: detect one tenant monopolizing cache
- alert: KVCacheTenantMonopoly
  expr: |
    vllm_kv_cache_blocks_by_tenant / vllm_kv_cache_blocks_total > 0.7
  for: 5m
  severity: warning
  annotations:
    summary: "Tenant {{ $labels.tenant_id }} using >70% of KV cache"
    
# Timing anomaly (side-channel detection)
- alert: ResponseTimingBimodal
  expr: |
    histogram_quantile(0.95, vllm_response_latency_seconds_bucket)
    - histogram_quantile(0.05, vllm_response_latency_seconds_bucket) > 0.08
  for: 10m
  severity: warning
  annotations:
    summary: "Bimodal response timing detected — possible side-channel"
```

```python
# Custom Prometheus metrics to add to vLLM
from prometheus_client import Counter, Gauge, Histogram

# Per-tenant cache operations
kv_cache_hits = Counter(
    "vllm_kv_cache_hits_total",
    "KV cache hits",
    ["tenant_id", "hit_type"]  # hit_type: prefix_hit, prefix_miss
)

kv_cache_blocks_by_tenant = Gauge(
    "vllm_kv_cache_blocks_by_tenant",
    "Number of cached blocks per tenant",
    ["tenant_id"]
)

# Cross-tenant detection (should be 0 — canary metric)
kv_cache_cross_tenant_hits = Counter(
    "vllm_kv_cache_cross_tenant_hits_total",
    "Cache lookups where hash collided across tenants (BUG if > 0)",
    ["source_tenant", "cached_tenant"]
)

# Eviction tracking
kv_cache_evictions = Counter(
    "vllm_kv_cache_evictions_total",
    "Cache evictions by tenant and reason",
    ["tenant_id", "reason"]  # reason: lru, over_quota, memory_pressure
)
```

## 19.9 Rollout Strategy

```
PHASE 1: IMMEDIATE CONTAINMENT (Day 0, < 30 minutes)
  Action: Disable prefix caching entirely
  Command: --enable-prefix-caching=false
  Impact: +150% latency (cache miss on every request)
  Risk: Zero — only performance regression, no data risk
  Verification: confirm kv_cache_hits_total stops increasing

PHASE 2: FORENSICS (Day 0-1)
  Action: Audit last 72 hours of requests
  Query: Kafka topic for response tokens containing cross-tenant terminology
  Tool: grep response logs for product-line-specific terms appearing in wrong tenant
  Output: Count of affected requests, severity assessment
  Report to: Security team, compliance, legal (if PII leaked)

PHASE 3: PATCH DEVELOPMENT (Day 1-3)
  Action: Implement tenant-scoped hash in vLLM fork
  Changes: 5 files (api_server.py, sequence.py, async_llm_engine.py,
           prefix_caching_block.py, block_manager.py)
  Tests: Unit + integration (see 19.7 above)
  Review: Security team code review required

PHASE 4: STAGING VALIDATION (Day 3-5)
  Action: Deploy patched vLLM to staging environment
  Traffic: Synthetic multi-tenant load (3 simulated tenants)
  Validation:
    - cross_tenant_hits metric = 0 ✓
    - same_tenant hit rate matches pre-incident baseline ✓
    - latency within 5% of pre-incident (prefix caching working) ✓
    - run integration test suite (19.7) against staging ✓

PHASE 5: CANARY PRODUCTION (Day 5-7)
  Action: Route 5% production traffic to patched engine
  Monitor: 24 hours
  Success criteria:
    - Zero cross-tenant hits
    - Cache hit rate per-tenant ≥ 80% of original total hit rate
    - p99 latency within 10% of pre-incident
  
PHASE 6: FULL ROLLOUT (Day 7-9)
  Action: Roll patched engine to 100% production
  Retain: Old engine on standby for 48 hours (instant rollback)
  
PHASE 7: POST-MORTEM & HARDENING (Day 9-14)
  - Publish internal security advisory
  - Add prefix caching isolation to security review checklist
  - Upstream contribution to vLLM (if approved by legal)
  - Add CI gate: test_cross_tenant_isolation runs on every vLLM upgrade
  - Implement per-tenant cache eviction fairness (19.6)
  - Evaluate timing side-channel risk (if applicable to deployment)
```

## 19.10 Performance Impact Analysis

```
SCENARIO: 3 tenants (credit_cards, auto_loans, banking), shared vLLM engine

BEFORE FIX (APC disabled — immediate containment):
  prefix_cache_hit_rate: 0%
  Average TTFT: 180 ms (full prefill every request)
  Throughput: ~800 req/s per GPU

AFTER FIX (tenant-scoped APC):
  prefix_cache_hit_rate: ~65% per-tenant (was 72% globally)
  Average TTFT: 62 ms (cache hit on same-tenant repeated prefixes)
  Throughput: ~1,850 req/s per GPU

WHY HIT RATE DROPS SLIGHTLY:
  Old (insecure): ALL tenants contribute to ONE cache pool
    → More diverse traffic = more cache entries = higher hit rate
    → But those hits leaked data!
  
  New (secure): Each tenant has isolated cache partition
    → Each partition smaller = fewer entries = slightly lower hit rate
    → But ALL hits are safe

  Typical impact: 5-10% lower hit rate than insecure baseline
  This is the CORRECT tradeoff (security > 5% throughput)

MEMORY OVERHEAD:
  Per block: +8 bytes (tenant_id string reference)
  Per 100K blocks: +800 KB (negligible vs 80 GB GPU memory)
  Hash computation: +2ns per block (one extra value in tuple hash)
  
  TOTAL OVERHEAD: < 0.001% — effectively free

BATCH EFFICIENCY (important nuance):
  Continuous batching STILL batches across tenants!
  Batching shares MODEL WEIGHTS (read-only, safe)
  Only CACHE is tenant-scoped (stateful, unsafe to share)
  
  → Throughput from batching is preserved
  → Only prefix cache reuse is scoped
  → Net performance: 95-97% of insecure baseline (acceptable)
```

## 19.11 Interview Quick Reference

```
"How do you implement tenant isolation in shared LLM inference?"

45-SECOND ANSWER:
  "vLLM's prefix caching hashes token content to share KV blocks
   across requests. In multi-tenant deployments, this creates a data
   leakage vector: same system prompt prefix → same hash → shared KV →
   tenant A's context bleeds into tenant B's generation.
   
   Fix: include tenant_id in the block hash function. hash(tenant_id,
   parent_hash, tokens) instead of hash(parent_hash, tokens). Same
   tokens from different tenants → different hashes → impossible to share.
   tenant_id flows from JWT via gateway header → engine → scheduler →
   block allocator. Fail-closed: reject requests without tenant identity.
   
   Performance impact: ~5% lower cache hit rate (cross-tenant 'bonus'
   hits removed — those were the vulnerability). Batching throughput
   preserved (model weights are read-only, safe to share)."

FOLLOW-UP: "What about timing side-channels?"
  "Even with scoped keys, cache hit (45ms) vs miss (120ms) leaks
   whether another user queried a similar prefix. Mitigate with
   constant-time response normalization: buffer fast responses until
   target deadline. Alternative: preemptive cache warming for all
   domain prefixes at session start — eliminates timing differential.
   Post-fix: attack accuracy drops from 94% to 52% (random chance)."

FOLLOW-UP: "How do you detect this in production?"
  "Three metrics: (1) cross_tenant_cache_hit counter — hard alert if > 0,
   (2) per-tenant cache hit rate — validates isolation working,
   (3) per-device latency distribution bimodality — detects timing side-channel.
   Traditional infrastructure monitoring (error rate, aggregate latency)
   is BLIND to information leakage. You need content-level audit trails
   and per-entity timing analysis."
```
