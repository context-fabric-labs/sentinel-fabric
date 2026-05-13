# AI System Engineer Interview Readiness Check
## 10 LLM Inference + 10 Traditional ML Questions
## With 30-second answers and "go deeper" prompts

> **How to use this:** Read each question, answer it OUT LOUD in 30 seconds.
> Then check against the answer. If you can't answer 7/10 in each section,
> you need more prep on that topic.

---

# SECTION A: LLM INFERENCE (10 Questions)

---

### Q1: What is the difference between prefill and decode in LLM inference?

**30-second answer:**
Prefill processes all prompt tokens in parallel through the transformer stack,
computing Q/K/V for every token and building the KV cache. It's compute-bound
(matrix-matrix operations). Decode generates one token at a time autoregressively,
reusing the cached K/V and only computing Q/K/V for the new token. It's
memory-bandwidth-bound (matrix-vector operations reading KV cache from HBM).
Prefill determines TTFT; decode determines inter-token latency.

**Go deeper (interviewer might ask):**
- "Why is decode memory-bound?" → Only 1 new token per step, so the arithmetic
  intensity is very low — most time is spent reading model weights + KV cache
  from HBM, not doing math.
- "How does chunked prefill help?" → Breaks prefill into smaller chunks
  interleaved with decode steps, preventing prefill from blocking active
  decode requests and reducing P99 jitter.

---

### Q2: What is KV cache, why does it grow, and how do you manage it?

**30-second answer:**
KV cache stores the Key and Value projections for all previously processed
tokens at every layer, so decode doesn't recompute them. It grows linearly
with sequence length × batch size × num_layers × num_kv_heads × head_dim × dtype.
For Llama 70B at 4096 tokens, KV cache is ~2.5 GB per sequence. Management
techniques: paged attention (vLLM) to avoid fragmentation, FP8 KV cache
to halve memory, GQA to reduce KV heads, and prefix caching to share
KV across requests with common prefixes.

**Go deeper:**
- "What is paged attention?" → Manages KV cache like virtual memory pages —
  non-contiguous blocks mapped via block tables, eliminates fragmentation.
- "When does KV cache cause OOM?" → When concurrent_requests × seq_len × per_token_KV
  exceeds available GPU memory after model weights and activations.

---

### Q3: Explain FP8 vs FP16 vs BF16 — when would you use each for inference?

**30-second answer:**
FP32 (32-bit): reference/debugging only. FP16 (5 exp, 10 mantissa): legacy,
risk of overflow. BF16 (8 exp, 7 mantissa): safe default, same range as FP32.
FP8 E4M3 (4 exp, 3 mantissa): current inference workhorse on H100/H200 —
~2× throughput vs FP16 with per-tensor scaling to preserve accuracy. FP4:
emerging on Blackwell, further 2× bandwidth savings. For inference: use FP8
for weights and KV cache, accumulate reductions (softmax, LayerNorm) in
BF16/FP32 for numerical stability.

**Go deeper:**
- "When does FP8 fail?" → Long context (>2048 tokens) softmax accumulation
  can lose precision. Fix: mixed precision — FP8 weights, BF16 attention accum.
- "What is the Transformer Engine?" → NVIDIA's hardware feature on Hopper
  that dynamically chooses FP8 vs FP16 per layer based on tensor statistics.

---

### Q4: What are CUDA Graphs and when would you use/avoid them for LLM serving?

**30-second answer:**
CUDA Graphs capture a fixed sequence of GPU operations (kernels, memcpys)
into a DAG, instantiate it once, and replay with a single API call —
eliminating per-kernel CPU launch overhead (~5-15µs per kernel × 30-50
kernels per decode step = significant overhead). Use for decode (fixed
shapes, repetitive). Avoid for prefill (variable prompt lengths). Pitfalls:
require static memory addresses and shapes, graph workspace steals from
KV cache pool. In vLLM: PIECEWISE mode is safest (attention stays eager,
rest is graphed).

**Go deeper:**
- "How can CUDA Graphs hurt throughput?" → Graph workspace memory steals
  from KV cache → fewer concurrent sequences → more preemptions → lower TPS.
  Fix: limit capture sizes, use FP8 KV cache to reclaim memory.

---

### Q5: How does tensor parallelism work and what determines optimal TP degree?

**30-second answer:**
Tensor parallelism splits model layers across GPUs — each GPU computes a
portion of each layer's matmul, then AllReduce synchronizes results.
For attention: Q/K/V heads are split across GPUs. For MLP: weight matrices
are column/row split. Optimal TP degree depends on: model size (must fit
in aggregate GPU memory), NVLink topology (AllReduce speed), and latency
vs throughput tradeoff. TP=4 on 4 NVLink-connected H100s is common for 70B.
TP across nodes (PCIe/InfiniBand) is much slower than intra-node (NVLink).

**Go deeper:**
- "TP=4 on 4 GPUs but only 2 have NVLink — what happens?" → Cross-pair
  AllReduce goes through PCIe (~10× slower), killing decode latency.
  Fix: use TP=2 (stay on NVLink pair) or request full NVLink mesh node.

---

### Q6: How would you profile and diagnose slow LLM decode throughput?

**30-second answer:**
Start with vLLM metrics: check TPOT P99, throughput, cache usage, preemptions.
Then Nsight Systems: capture decode timeline with --cuda-graph-trace=node,
look for CPU gaps between kernels (CUDA Graphs disabled?), kernel storms
(unfused ops?), or prefill blocking decode (need chunked prefill?).
Then Nsight Compute on the hot kernel: check SOL (SM% vs DRAM%) —
decode attention should be memory-bound. Check L2 hit rate (low = KV
cache scattered), warp stalls (long_scoreboard = waiting on memory).
Common fixes: enable CUDA Graphs, switch to FP8 KV cache, enable prefix
caching, reduce max-model-len.

**Go deeper:**
- "nvidia-smi shows 78% GPU utilization but throughput is low — why?"
  → nvidia-smi measures % of time ANY kernel is running, not % of peak.
  A tiny kernel that uses 5% of compute still registers as "utilizing."
  Use Nsight Compute SOL for real utilization.

---

### Q7: What is prefix caching and how does it work?

**30-second answer:**
When multiple requests share the same leading tokens (system prompt),
prefix caching stores the computed KV cache for those tokens and reuses
it for subsequent requests. vLLM divides KV cache into fixed-size blocks,
hashes each block (including all preceding tokens), and looks up the hash
in a block pool. Cache hit = skip prefill for that block. For fraud
detection with a shared 200-token system prompt, this gives 87-95%
cache hit rate and ~40% TTFT reduction. Critical: put static content
FIRST in the prompt, dynamic content LAST. Never put timestamps or
request IDs in the prefix.

**Go deeper:**
- "How is this different from prefix tuning?" → Prefix caching is runtime
  KV reuse (no training). Prefix tuning is a PEFT method that learns
  virtual token embeddings (training required, 0.1% params).

---

### Q8: Explain the tradeoffs of prefill/decode disaggregation.

**30-second answer:**
Disaggregation runs prefill and decode on separate GPU pools — prefill
workers are compute-optimized (high FLOPS), decode workers are
bandwidth-optimized (high HBM BW, many concurrent sequences). Benefits:
no prefill blocking decode, optimized GPU utilization per phase. Costs:
KV cache transfer overhead (RDMA/NVLink), engineering complexity, separate
worker pool management. Worth it at scale (OpenAI: millions of concurrent
requests, variable context lengths). NOT worth it for moderate scale
(60 QPS, fixed short prompts) — simpler optimizations (chunked prefill,
prefix caching, FP8) deliver more ROI.

**Go deeper:**
- "How does KV cache transfer work?" → NVIDIA Dynamo uses NIXL for
  direct GPU-to-GPU transfer over NVLink (same node) or RDMA (cross-node).
  32 MB KV cache transfers in ~0.03ms over NVLink.

---

### Q9: How do you eliminate the "Python tax" in an LLM serving pipeline with guardrails?

**30-second answer:**
The Python tax is serialization/deserialization overhead between pipeline
stages (tokenizer → guardrail → LLM → guardrail → detokenizer). Fix:
use a Rust gateway for all CPU work (tokenization, rule-based guardrails,
response formatting) and NVIDIA Triton ensemble for GPU-resident ML
guardrail + LLM execution — tensors stay on GPU between stages, zero
CPU↔GPU copies between guardrail and LLM. This reduces guardrail
overhead from ~40ms to ~3ms and eliminates Python GIL contention entirely.

**Go deeper:**
- "Why Rust for the gateway?" → Zero-cost abstractions, no GIL,
  async runtime (tokio) handles 50K+ concurrent connections,
  memory safety without GC pauses.

---

### Q10: How do you choose GPUs for LLM inference at different scales?

**30-second answer:**
Inference is memory-bandwidth-bound in decode, so bandwidth matters more
than peak FLOPS. H100: flagship, best overall. H200: 141GB HBM3e,
4.8 TB/s — best for long context and high concurrency (more KV cache
capacity). L40S: 48GB GDDR6, excellent cost/token for batch inference.
L4: best perf/watt for small models (7B-13B), Kubernetes-friendly.
B200 (Blackwell): FP4 support, ~8 TB/s, next-gen. A10: legacy but
still deployed, good for 7B-13B at moderate latency SLOs. Decision
depends on: model size, context length, latency SLA, cost budget,
and whether you need training capability too.

**Go deeper:**
- "When would you choose H200 over H100?" → When inference dominates
  (not training), especially with long context or high concurrency.
  H200's 141GB + 4.8 TB/s means more KV cache capacity and faster
  decode than H100's 80GB + 3.35 TB/s.

---
---

# SECTION B: TRADITIONAL ML — BERT / XGBoost (10 Questions)

---

### Q1: In the fraud detection hot path, why use XGBoost + transformer instead of just one model?

**30-second answer:**
They catch different fraud patterns. XGBoost operates on tabular features
(amount, velocity, merchant risk) and catches ~85-90% of fraud through
statistical patterns — it's fast (~0.5ms), interpretable, and proven.
The transformer classifier operates on the transaction SEQUENCE (last
50 transactions as a sequence) and catches the remaining ~5-15% —
sequential patterns like gradual escalation, behavioral drift, and
impossible travel that XGBoost can't see because it treats each
transaction independently. Together: 92% → 97% detection rate, and
the 5% improvement at scale saves millions in fraud losses.

**Go deeper:**
- "Why not just use the transformer for everything?" → Transformer
  adds ~2ms latency + requires GPU. For the 85% of fraud that
  XGBoost catches, that GPU cost and latency is wasted. Layered
  approach: rules catch obvious fraud, XGBoost catches statistical
  patterns, transformer catches sequential patterns.

---

### Q2: How do you serve the transformer classifier at <3ms latency in the hot path?

**30-second answer:**
The hot-path transformer is NOT an LLM — it's a tiny encoder-only model
(5-50M parameters, like a small BERT), not a decoder. It takes a sequence
of 50 transactions as input and outputs a single fraud probability score.
Optimization: export to TensorRT (FP16 or INT8), use CUDA Graphs for
the fixed-shape forward pass, batch multiple requests with micro-batching,
pin GPU memory, and pre-warm the model. On an L4 GPU: ~1-2ms for single
inference, ~2-3ms for a micro-batch of 8. The model runs a SINGLE
forward pass (no autoregressive decode), so it's fundamentally different
from LLM inference.

**Go deeper:**
- "Why not DistilBERT instead of custom transformer?" → We used
  DistilBERT as the base and fine-tuned it on fraud data. DistilBERT
  retains 97% of BERT's accuracy at 60% speed — 6 layers vs 12.

---

### Q3: What is an online feature store and why can't you compute features on the hot path?

**30-second answer:**
An online feature store (Redis/DynamoDB) pre-computes and pre-materializes
ALL historical features so the hot path does ONE key-value lookup (~0.5ms)
instead of querying databases and computing aggregations (~30-80ms).
Features like "user's average spend over 30 days" or "last 50 transactions"
are updated by a streaming pipeline (Flink/Spark) on every transaction
and stored in Redis. The hot path only computes REAL-TIME features that
depend on the current transaction (amount z-score, distance from last
transaction, time delta) — these are cheap arithmetic (~0.03ms) on
data already in L1 cache.

**Go deeper:**
- "Isn't 50M users in Redis expensive?" → Use tiered storage: only
  24h-active users in Redis (~15M, ~50GB), 90d-active in DynamoDB,
  cold users computed on demand. 95% cache hit rate at 10% of the cost.

---

### Q4: Explain how DistilBERT works and why you'd use it over full BERT.

**30-second answer:**
DistilBERT is a compressed version of BERT created through knowledge
distillation — a smaller "student" model (6 layers, 66M params) is
trained to mimic the outputs of the larger "teacher" BERT (12 layers,
110M params). It retains 97% of BERT's language understanding while
being 60% faster and 40% smaller. For fraud detection NLU/classification,
this tradeoff is excellent: the 3% accuracy loss is negligible, but
the 60% speedup means we can run intent classification in ~20ms instead
of ~50ms on Apple Silicon (Metal MPS) or ~15ms on NVIDIA GPU.

**Go deeper:**
- "How does knowledge distillation work?" → Train the student to match
  the teacher's soft probability distribution (not just hard labels).
  The soft distribution contains "dark knowledge" — information about
  which classes are similar, which the student learns from.

---

### Q5: How do you handle multi-turn context in the NLU (intent/entity) model?

**30-second answer:**
Multi-turn requires coreference resolution — "What about tomorrow?"
after "What's the weather?" means the intent is still weather_query
and "tomorrow" is a time entity. Implementation: prepend the last 2-3
turns to the current input with [SEP] tokens, so BERT sees the full
dialogue context. For entity resolution: if the current turn is missing
expected entities (location, topic), carry them forward from previous
turns stored in the session context. Session-affine routing (consistent
hashing) ensures multi-turn requests hit the same node where the
session context is cached — 85% cache hit rate.

**Go deeper:**
- "What if the user changes topic?" → Track topic continuity score.
  If current utterance has low similarity to previous context,
  reset the coreference state and treat as a new topic.

---

### Q6: How do you deploy XGBoost for <1ms inference at 24K TPS?

**30-second answer:**
XGBoost inference is CPU-only and embarrassingly parallel across requests.
Key optimizations: compile model to native code (Treelite converts
XGBoost model to C, compiles to .so), pre-allocate feature buffers
(no malloc on hot path), use the cacheline-aligned FeatureBlock struct
so all features are contiguous in memory, pin inference threads to
NUMA node 0 (close to NIC and feature cache), and use the per-core
arena allocator for zero-allocation processing. Result: ~0.3-0.5ms
per prediction at 24K TPS with zero GC pauses or allocation jitter.

**Go deeper:**
- "Why Treelite instead of native XGBoost predict()?" → Native XGBoost
  Python predict() has Python overhead and isn't optimized for
  single-sample latency. Treelite compiles the tree structure to
  native if/else chains — 3-5× faster for single predictions.

---

### Q7: What is the difference between MHA, MQA, and GQA? Why does it matter for inference?

**30-second answer:**
MHA (Multi-Head Attention): each query head has its own K/V head —
maximum representational power but KV cache scales linearly with heads.
MQA (Multi-Query Attention): ALL query heads share ONE K/V head —
minimal KV cache but quality degrades. GQA (Grouped-Query Attention):
groups of query heads share K/V heads (e.g., 32 Q heads, 8 KV heads) —
intermediate tradeoff. For inference: GQA dramatically reduces KV cache
size (4× less for 32→8 heads) and decode memory bandwidth (fewer KV
reads per token), enabling higher concurrency and longer contexts.
Llama 3.1 uses GQA. This is why model architecture choice directly
impacts serving infrastructure.

**Go deeper:**
- "Quantify the KV cache savings." → Llama 70B MHA: 64 KV heads.
  Llama 70B GQA: 8 KV heads. KV cache per token: 8× smaller.
  At 4096 tokens × 128 concurrent: saves ~160 GB of GPU memory.

---

### Q8: How do you handle feature freshness in the fraud detection pipeline?

**30-second answer:**
Three tiers of freshness. Batch features (30-day averages, account
profiles): updated hourly/daily by Spark, stored in Redis. Streaming
features (transaction count last 1h, velocity, last 50 transactions):
updated per-event by Flink, stored in Redis — fresh within seconds
of the previous transaction. Real-time features (amount z-score,
distance from last transaction, time since last transaction): computed
on the hot path in C++/Rust because they depend on the current
transaction which didn't exist until this moment. The streaming
features are the most important — they're updated when the PREVIOUS
transaction occurred, so they're always "one transaction behind" which
is acceptable for fraud detection.

**Go deeper:**
- "What about cold-start users (new accounts)?" → No transaction
  history = no features. Use population-level defaults (average
  spending for that merchant category, typical transaction velocity).
  Flag as "cold start" and apply stricter rules until enough
  history accumulates (typically 5-10 transactions).

---

### Q9: How do you optimize BERT inference on Apple Silicon (M1/M2)?

**30-second answer:**
Two-level optimization. CPU level: NEON SIMD for audio feature
extraction (mel spectrogram) — processes 4 floats per instruction,
4.4× speedup over scalar code. GPU level: Metal Performance Shaders
(MPS) backend in PyTorch for transformer layers — maps ML operations
to Metal GPU kernels optimized for Apple Silicon. Key advantage:
unified memory architecture means zero-copy between CPU and GPU stages
— the mel spectrogram computed by NEON on CPU is in the same physical
memory that Metal GPU accesses, no PCIe transfer needed. DistilBERT
NLU: 42ms on CPU → 22ms on MPS (2× speedup).

**Go deeper:**
- "How is Apple Silicon unified memory different from NVIDIA?" →
  CPU and GPU share the same physical memory and address space.
  No cudaMemcpy equivalent needed. Tensors created on CPU are
  directly accessible by GPU. Eliminates the PCIe bottleneck
  entirely — but peak GPU compute is lower than discrete GPUs.

---

### Q10: Walk me through a complete fraud detection request — from transaction to decision.

**30-second answer:**
Transaction arrives at Sentinel Gateway (Rust). Parse with SIMD
zero-copy (0.05ms). Redis MGET fetches pre-computed features (0.5ms).
C++ engine computes real-time features — amount z-score, distance from
last transaction, velocity burst (0.03ms). XGBoost predicts on
cacheline-aligned FeatureBlock (0.5ms). Transformer classifier scores
the 50-transaction sequence with current transaction appended (2ms on GPU).
Rule engine applies 200+ business rules (0.2ms). Decision: approve/decline/step-up.
If medium risk (0.4-0.8): route to warm path LLM (Llama 8B) for
explanation (~1 second). Publish decision to Arrow audit boundary
(zero-copy). Total hot path: ~3.3ms. All inter-stage communication
via SPSC lock-free rings and shared memory — zero serialization.

**Go deeper:**
- "What if Redis is down?" → Circuit breaker trips after 3 failures
  in 10-second window. Fallback: use local in-memory cache (HugePage-backed,
  80-90% hit rate). If cache also misses: compute features on demand
  (slower, ~15ms) or degrade to rules-only scoring.

---
---

# SCORING YOUR READINESS

## LLM Inference (Section A):
- **8-10 correct:** Ready for interview
- **6-7 correct:** Review weak areas, focus on the "go deeper" parts
- **< 6 correct:** Need more preparation on LLM serving fundamentals

## Traditional ML (Section B):
- **8-10 correct:** Ready for interview
- **6-7 correct:** Review weak areas
- **< 6 correct:** Need more preparation on ML systems fundamentals

## Critical combinations to nail:
- Q-A1 (prefill/decode) + Q-A4 (CUDA Graphs) + Q-A6 (profiling) = core LLM serving
- Q-B1 (XGBoost+transformer) + Q-B3 (feature store) + Q-B10 (end-to-end) = core fraud detection
- If you can answer A6 + B10 deeply, you can handle most system design questions
