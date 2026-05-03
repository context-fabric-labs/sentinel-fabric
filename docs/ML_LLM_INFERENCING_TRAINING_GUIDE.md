# ML/LLM Inferencing Mastery Guide
## Advanced Training for Staff/Principal Engineer Interview Readiness

**A Comprehensive One-Stop Guide to Production ML/LLM Inference Systems**

---

## Table of Contents

1. [Training Overview](#training-overview)
2. [Module 1: Inference Fundamentals](#module-1-inference-fundamentals)
3. [Module 2: Inference Engines Deep Dive](#module-2-inference-engines-deep-dive)
4. [Module 3: Batching Strategies](#module-3-batching-strategies)
5. [Module 4: Multi-Model Orchestration](#module-4-multi-model-orchestration)
6. [Module 5: KV-Cache Management](#module-5-kv-cache-management)
7. [Module 6: Prompt Optimization](#module-6-prompt-optimization)
8. [Module 7: Advanced Optimization Techniques](#module-7-advanced-optimization-techniques)
9. [Module 8: Traditional ML Model Inference at Scale](#module-8-traditional-ml-model-inference-at-scale)
10. [Module 9: Traditional ML Optimization & Production Patterns](#module-9-traditional-ml-optimization--production-patterns)
11. [Module 10: Hands-On Labs](#module-10-hands-on-labs)
12. [Interview Question Bank](#interview-question-bank)
13. [Resource Library](#resource-library)
14. [Progress Tracker](#progress-tracker)

---

## Training Overview

### **Target Audience:**
- Engineers with basic ML/LLM deployment experience
- Preparing for Staff/Principal AI Infrastructure roles
- Want to go from "deployed models" to "optimized inference at scale"

### **Learning Objectives:**
By the end of this training, you will:
- ✅ Understand **how inference engines work internally** (vLLM, TensorRT-LLM, Triton)
- ✅ Implement **advanced batching strategies** for 10× throughput improvement
- ✅ Design **multi-model orchestration** with dependency-aware execution
- ✅ Master **KV-cache management** for 70%+ hit rates
- ✅ Optimize **prompt handling** for 60% token reduction
- ✅ Apply **advanced techniques** (speculative decoding, prefix caching, quantization)
- ✅ Build **production traditional ML pipelines** (fraud, payments, risk) at 100K+ TPS
- ✅ Implement **ML production patterns** (drift detection, circuit breakers, shadow scoring)
- ✅ Answer **interview questions** with depth and real-world examples

### **Training Duration:**
- **Total Hours:** 55–70 hours
- **Duration:** 6–8 weeks (part-time)
- **Format:** 30% theory, 70% hands-on labs

### **Prerequisites:**
- Basic understanding of transformer models
- Some experience deploying ML/LLM models
- Familiarity with Python and PyTorch
- Basic Linux/command-line skills

---

## Module 1: Inference Fundamentals

### **1.1 Inference Pipeline Anatomy**

#### **What Happens During Inference?**

```
Input Text → Tokenization → Embedding → Transformer Layers → Logits → Sampling → Output Text
```

**Key Stages:**

1. **Tokenization** (1–5 ms)
   - Convert text to token IDs
   - Handle special tokens, truncation, padding
   - Tools: HuggingFace Tokenizers, SentencePiece

2. **Embedding Lookup** (2–10 ms)
   - Map token IDs to embedding vectors
   - Memory-bound operation
   - Embedding table: vocab_size × hidden_size (e.g., 32K × 4096 = 512MB)

3. **Transformer Forward Pass** (50–500 ms)
   - Self-attention: O(n²) complexity
   - Feed-forward networks: O(n) complexity
   - Compute-bound operation (GPU-accelerated)

4. **Logits Processing** (1–5 ms)
   - Apply temperature, top-k, top-p sampling
   - Convert to probabilities (softmax)

5. **Token Sampling** (1–2 ms)
   - Greedy, beam search, or nucleus sampling
   - Generate next token ID

6. **Detokenization** (1–3 ms)
   - Convert token IDs back to text
   - Handle special cases (BPE merges, spaces)

#### **Latency Breakdown (7B Model, A100 GPU):**

| Stage | Latency | % of Total | Optimization Opportunity |
|-------|---------|------------|-------------------------|
| Tokenization | 3 ms | 3% | Batch tokenization, caching |
| Embedding | 5 ms | 5% | Fused kernels, quantization |
| Attention | 150 ms | 50% | FlashAttention, sparse attention |
| FFN | 100 ms | 33% | Quantization, pruning |
| Sampling | 5 ms | 5% | Fused sampling kernels |
| Detokenization | 2 ms | 2% | Minimal |
| **Total (1 token)** | **265 ms** | **100%** | |

**For 100 tokens:** 26.5 seconds (naive) → 2–3 seconds (optimized with KV-cache)

---

### **1.2 Inference vs. Training**

| Aspect | Training | Inference |
|--------|----------|-----------|
| **Goal** | Learn weights | Generate predictions |
| **Batch Size** | Large (256–1024) | Small (1–32), dynamic |
| **Precision** | FP16/BF16 | FP16/INT8/INT4 |
| **Memory** | Gradients + optimizer states | Weights + KV-cache |
| **Compute** | Forward + backward | Forward only |
| **Latency** | Throughput-focused | Latency-focused (p99) |
| **Optimization** | Gradient checkpointing | KV-cache, quantization |

**Key Insight:** Inference optimization is about **reducing latency while maintaining throughput** under dynamic request patterns.

---

### **1.3 Performance Metrics**

#### **Latency Metrics:**

1. **Time to First Token (TTFT):**
   - Time from request arrival to first output token
   - Critical for user experience (chat, assistants)
   - Target: < 500 ms for interactive applications

2. **Time per Output Token (TPOT):**
   - Time to generate each subsequent token
   - Affects streaming quality
   - Target: < 50 ms/token for smooth streaming

3. **End-to-End Latency:**
   - Total time from request to complete response
   - Depends on output length
   - Target: < 3 seconds for typical queries

4. **Percentiles:**
   - **p50:** Median latency
   - **p95:** 95% of requests faster
   - **p99:** Worst-case latency (critical for SLAs)

#### **Throughput Metrics:**

1. **Requests per Second (RPS):**
   - Number of complete requests handled per second
   - Depends on batch size, model size, hardware

2. **Tokens per Second:**
   - Total tokens generated per second (input + output)
   - Better metric for LLMs (variable-length responses)

3. **GPU Utilization:**
   - Percentage of GPU compute capacity used
   - Target: 70–90% (higher = better efficiency)

#### **Cost Metrics:**

1. **Cost per Request:**
   - (GPU cost/hour) / (requests/hour)
   - Optimization goal: minimize while meeting SLAs

2. **Cost per Token:**
   - (GPU cost/hour) / (tokens/hour)
   - Useful for usage-based pricing

---

### **1.4 Bottleneck Analysis**

#### **Memory-Bound vs. Compute-Bound:**

**Memory-Bound Operations:**
- Embedding lookup
- KV-cache reads/writes
- Weight loading (for large models)
- **Optimization:** Reduce memory access, increase batch size

**Compute-Bound Operations:**
- Matrix multiplications (attention, FFN)
- Softmax, layer norm
- **Optimization:** Use Tensor Cores, mixed precision

**Rule of Thumb:**
- Small batches (< 8): Memory-bound
- Large batches (> 32): Compute-bound
- **Sweet spot:** 16–64 batch size (balance both)

#### **Amdahl's Law Applied to Inference:**

```
Speedup = 1 / ((1 - P) + P / S)

Where:
- P = Parallelizable portion
- S = Speedup of parallel portion
```

**Example:** If 80% of inference is parallelizable on GPU:
- 10× GPU speedup → Overall 4.2× speedup
- 100× GPU speedup → Overall 5× speedup (diminishing returns)

**Key Insight:** Optimize the **sequential bottlenecks** (tokenization, sampling) for maximum impact.

---

### **1.5 Quick Check: Fundamentals**

**Practice Questions:**

1. What is the difference between TTFT and TPOT? Which is more important for chat applications?
2. Why is inference memory-bound for small batches?
3. How does KV-cache reduce inference latency?
4. What is the computational complexity of self-attention? Why?
5. Explain the trade-off between batch size and latency.

**Answers:**

1. **TTFT** is time to first token (user waits for response to start). **TPOT** is time per output token (streaming speed). For chat, **TTFT is more critical** (users notice initial delay more than streaming speed).

2. Small batches don't have enough parallel work to saturate GPU compute units. Memory access latency dominates over compute time.

3. **KV-cache** stores Key,Value matrices from previous tokens, avoiding re-computation. Reduces O(n²) to O(n) for autoregressive generation.

4. **O(n²)** because each token attends to all previous tokens (n tokens × n attention scores).

5. **Larger batch size** → higher throughput (better GPU utilization) but **higher latency** (requests wait for batch to fill). Optimal batch size balances both.

---

## Module 2: Inference Engines Deep Dive

### **2.1 vLLM: PagedAttention & Continuous Batching**

#### **The Problem vLLM Solves:**

**Traditional LLM Serving Issues:**

1. **Memory Fragmentation:**
   - KV-cache allocated contiguously per request
   - Wasted memory due to over-allocation
   - Example: Request needs 100 tokens, allocate for 512 → 80% waste

2. **Static Batching:**
   - All requests in batch must have same sequence length
   - Padding waste for shorter sequences
   - Batching inefficiency under variable load

3. **No Preemption:**
   - Once a request starts, it runs to completion
   - High-priority requests must wait
   - Poor SLA management

#### **vLLM's Solution: PagedAttention**

**Key Innovation:** Borrowed from OS virtual memory (paging)

```
Traditional Approach:
Request 1: [KV-block][KV-block][KV-block]... (contiguous, 512 tokens allocated)
Request 2: [KV-block][KV-block][KV-block]... (contiguous, 512 tokens allocated)
→ Wasted memory if requests use only 100 tokens

vLLM PagedAttention:
Request 1: Block Table → [Block 5][Block 12][Block 3]... (non-contiguous, allocated on-demand)
Request 2: Block Table → [Block 8][Block 15][Block 1]... (non-contiguous, allocated on-demand)
→ Only allocate blocks as needed, share common prefixes
```

**How It Works:**

1. **Block Table:** Maps logical token positions to physical memory blocks
2. **On-Demand Allocation:** Allocate blocks only when tokens are generated
3. **Block Sharing:** Multiple requests can share blocks (common prefixes)
4. **Swapping:** Move blocks between GPU and CPU memory (handle overflow)

**Memory Efficiency:**

| Approach | Memory Utilization | Max Requests (A100 80GB) |
|----------|-------------------|--------------------------|
| Traditional | 20–40% | ~50 requests |
| vLLM PagedAttention | 60–80% | ~200 requests |

#### **Continuous Batching (In-Flight Batching)**

**Traditional Static Batching:**
```
Batch 1: [Req1][Req2][Req3][Req4] → All must complete before next batch
Batch 2: [Req5][Req6][Req7][Req8] → Wait for Batch 1 to finish
→ GPU idle time between batches
```

**vLLM Continuous Batching:**
```
Iteration 1: [Req1][Req2][Req3][Req4] → Req2 completes
Iteration 2: [Req1][Req3][Req4][Req5] → Add new request immediately
Iteration 3: [Req1][Req3][Req5][Req6] → Req1 completes, add Req7
→ GPU always busy, no idle time
```

**Benefits:**
- **2–4× throughput improvement** vs. static batching
- **Lower average latency** (requests don't wait for batch boundaries)
- **Better GPU utilization** (no idle time between batches)

#### **vLLM Architecture:**

```
┌─────────────────────────────────────────────────────────┐
│                    vLLM Engine                           │
├─────────────────────────────────────────────────────────┤
│  Request Queue (Priority-based)                         │
│  • High-priority (P0): Interactive chat                 │
│  • Normal (P1): Batch processing                        │
│  • Low (P2): Offline jobs                               │
├─────────────────────────────────────────────────────────┤
│  Scheduler (Continuous Batching)                        │
│  • Selects requests for next iteration                  │
│  • Respects priority, deadlines, memory constraints     │
├─────────────────────────────────────────────────────────┤
│  Block Manager (PagedAttention)                         │
│  • Allocates/frees KV blocks on-demand                  │
│  • Manages block tables per request                     │
│  • Handles CPU-GPU swapping                             │
├─────────────────────────────────────────────────────────┤
│  Model Executor (GPU)                                   │
│  • Runs transformer forward pass                        │
│  • Uses PagedAttention kernels                          │
│  • Supports multiple models (LoRA, adapters)            │
└─────────────────────────────────────────────────────────┘
```

#### **Hands-On: Deploy vLLM**

```bash
# Install vLLM
pip install vllm

# Deploy Llama-2-7B
python -m vllm.entrypoints.api_server \
    --model meta-llama/Llama-2-7b-chat-hf \
    --port 8000 \
    --tensor-parallel-size 1 \
    --max-num-seqs 256 \
    --gpu-memory-utilization 0.9

# Query the model
curl http://localhost:8000/generate \
    -d '{"prompt": "What is AI?", "max_tokens": 100}'
```

**Benchmark: vLLM vs. HuggingFace**

```python
import time
import requests

def benchmark_huggingface():
    start = time.time()
    # HuggingFace transformers (naive)
    from transformers import pipeline
    pipe = pipeline("text-generation", model="meta-llama/Llama-2-7b-chat-hf")
    result = pipe("What is AI?", max_length=100)
    return time.time() - start

def benchmark_vllm():
    start = time.time()
    # vLLM API
    response = requests.post("http://localhost:8000/generate",
                            json={"prompt": "What is AI?", "max_tokens": 100})
    return time.time() - start

print(f"HuggingFace: {benchmark_huggingface():.2f}s")
print(f"vLLM: {benchmark_vllm():.2f}s")
print(f"Speedup: {benchmark_huggingface() / benchmark_vllm():.1f}×")
```

**Expected Results:**
- HuggingFace: ~5 seconds (naive, no optimization)
- vLLM: ~1 second (PagedAttention + continuous batching)
- **Speedup: 5×**

---

### **2.2 TensorRT-LLM: NVIDIA GPU Optimization**

#### **What is TensorRT-LLM?**

NVIDIA's optimized inference engine for LLMs on NVIDIA GPUs.

**Key Features:**
- **Layer Fusion:** Combine multiple operations into single kernel
- **Quantization:** INT8/FP8 inference with minimal accuracy loss
- **Multi-GPU:** Tensor parallelism, pipeline parallelism
- **In-Flight Batching:** Similar to vLLM's continuous batching
- **Custom Kernels:** Optimized for NVIDIA Tensor Cores

#### **Optimization Techniques:**

**1. Layer Fusion:**

```
Traditional:
Input → LayerNorm → Attention → LayerNorm → FFN → Output
         (kernel 1)   (kernel 2)   (kernel 3)  (kernel 4)
→ 4 kernel launches, 4 memory round-trips

TensorRT-LLM:
Input → [LayerNorm + Attention + LayerNorm + FFN] → Output
         (fused kernel)
→ 1 kernel launch, 1 memory round-trip
→ 3–4× speedup
```

**2. Quantization:**

| Precision | Memory | Compute Speed | Accuracy |
|-----------|--------|---------------|----------|
| FP32 | 4 bytes/op | 1× (baseline) | 100% |
| FP16 | 2 bytes/op | 2–3× faster | 99.9% |
| BF16 | 2 bytes/op | 2–3× faster | 99.9% |
| INT8 | 1 byte/op | 4–5× faster | 98–99% |
| FP8 | 1 byte/op | 5–6× faster | 97–99% |

**TensorRT-LLM INT8 Quantization:**

```python
from tensorrt_llm import Quantization

# Quantize model to INT8
quant_config = Quantization(
    algorithm="INT8",
    calibration_dataset="cnn_dailymail",
    num_calib_samples=512
)

quantized_model = quantize_model(
    model="meta-llama/Llama-2-7b",
    quant_config=quant_config,
    output_dir="./llama2-7b-int8"
)

# Deploy quantized model
# 4× faster, 50% less memory, <1% accuracy loss
```

**3. Multi-GPU Parallelism:**

**Tensor Parallelism:**
- Split model layers across GPUs
- Each GPU processes part of the matrix multiplication
- Low communication overhead (within node)

**Pipeline Parallelism:**
- Split model layers into stages
- Each GPU handles different layers
- Higher latency, but scales to more GPUs

```
Tensor Parallelism (2 GPUs):
Layer 1: [GPU0: 50%][GPU1: 50%] → AllReduce
Layer 2: [GPU0: 50%][GPU1: 50%] → AllReduce

Pipeline Parallelism (2 GPUs):
Layer 1-16: [GPU0]
Layer 17-32: [GPU1]
→ Bubble overhead (GPU1 waits for GPU0)
```

#### **Hands-On: TensorRT-LLM Deployment**

```bash
# Install TensorRT-LLM
pip install tensorrt-llm

# Build optimized engine
trtllm-build \
    --checkpoint_dir ./llama2-7b \
    --output_dir ./llama2-7b-engine \
    --max_batch_size 32 \
    --max_input_len 1024 \
    --max_output_len 512 \
    --precision fp16

# Run inference
python inference.py \
    --engine_dir ./llama2-7b-engine \
    --input "What is AI?" \
    --max_output_len 100
```

---

### **2.3 Triton Inference Server: Multi-Framework Support**

#### **What is Triton?**

NVIDIA's inference serving platform supporting multiple frameworks (TensorFlow, PyTorch, ONNX, TensorRT).

**Key Features:**
- **Multi-Model Serving:** Host multiple models on same server
- **Dynamic Batching:** Automatically batch requests
- **Model Ensembles:** Chain models together
- **Concurrency:** Multiple model instances per GPU
- **Metrics:** Built-in Prometheus metrics

#### **Triton Architecture:**

```
┌─────────────────────────────────────────────────────────┐
│                    Triton Server                         │
├─────────────────────────────────────────────────────────┤
│  HTTP/gRPC Endpoints                                    │
│  • REST API                                             │
│  • gRPC (streaming)                                     │
├─────────────────────────────────────────────────────────┤
│  Scheduler (Dynamic Batching)                           │
│  • Batches requests by model                            │
│  • Configurable batch timeout, max batch size           │
├─────────────────────────────────────────────────────────┤
│  Model Backends                                         │
│  • TensorFlow Backend                                   │
│  • PyTorch Backend                                      │
│  • ONNX Runtime Backend                                 │
│  • TensorRT Backend                                     │
│  • Python Backend (custom logic)                        │
├─────────────────────────────────────────────────────────┤
│  GPU Resource Manager                                   │
│  • Multi-instance GPUs (MIG)                            │
│  • Concurrent model execution                           │
│  • Memory management                                    │
└─────────────────────────────────────────────────────────┘
```

#### **Triton Configuration (config.pbtxt):**

```protobuf
name: "llama2-7b"
platform: "pytorch_libtorch"
max_batch_size: 32

dynamic_batching {
  preferred_batch_size: [ 8, 16, 32 ]
  max_queue_delay_microseconds: 10000  # 10ms max wait
}

instance_group [
  {
    count: 2  # 2 instances per GPU
    kind: KIND_GPU
  }
]

input [
  {
    name: "input_ids"
    data_type: TYPE_INT32
    dims: [ -1 ]  # Variable length
  }
]

output [
  {
    name: "output_ids"
    data_type: TYPE_INT32
    dims: [ -1 ]
  }
]
```

#### **Model Ensemble (Pipeline):**

```protobuf
name: "llm_pipeline"

input [
  {
    name: "raw_text"
    data_type: TYPE_STRING
    dims: [ 1 ]
  }
]

output [
  {
    name: "response_text"
    data_type: TYPE_STRING
    dims: [ 1 ]
  }
]

ensemble_scheduling {
  step [
    {
      model_name: "tokenizer"
      model_version: -1
      input_map {
        key: "text"
        value: "raw_text"
      }
      output_map {
        key: "input_ids"
        value: "tokenized_ids"
      }
    },
    {
      model_name: "llama2-7b"
      model_version: -1
      input_map {
        key: "input_ids"
        value: "tokenized_ids"
      }
      output_map {
        key: "output_ids"
        value: "generated_ids"
      }
    },
    {
      model_name: "detokenizer"
      model_version: -1
      input_map {
        key: "token_ids"
        value: "generated_ids"
      }
      output_map {
        key: "text"
        value: "response_text"
      }
    }
  ]
}
```

---

### **2.4 ONNX Runtime: Cross-Platform Inference**

#### **What is ONNX Runtime?**

Microsoft's high-performance inference engine for ONNX models (framework-agnostic).

**Key Features:**
- **Cross-Platform:** Windows, Linux, macOS, mobile, edge
- **Multi-Hardware:** CPU, GPU, NPU, custom accelerators
- **Optimization:** Graph optimization, operator fusion
- **Quantization:** INT8, UINT8, FP16

#### **ONNX Export & Optimization:**

```python
from transformers import AutoModelForCausalLM
import torch
import onnx

# Export model to ONNX
model = AutoModelForCausalLM.from_pretrained("meta-llama/Llama-2-7b")
dummy_input = torch.randint(0, 1000, (1, 512))

torch.onnx.export(
    model,
    dummy_input,
    "llama2-7b.onnx",
    input_names=["input_ids"],
    output_names=["logits"],
    dynamic_axes={
        "input_ids": {1: "sequence_length"},
        "logits": {1: "sequence_length"}
    },
    opset_version=14
)

# Optimize ONNX model
import onnxruntime as ort
from onnxruntime.transformers.optimizer import optimize_model

optimized_model = optimize_model(
    "llama2-7b.onnx",
    model_type="bert",  # or "gpt2" for decoder models
    num_heads=32,
    hidden_size=4096
)
optimized_model.save_model_to_file("llama2-7b-optimized.onnx")
```

#### **ONNX Runtime Inference:**

```python
import onnxruntime as ort

# Create session with optimization
session_options = ort.SessionOptions()
session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
session_options.intra_op_num_threads = 8

session = ort.InferenceSession(
    "llama2-7b-optimized.onnx",
    sess_options=session_options,
    providers=["CUDAExecutionProvider", "CPUExecutionProvider"]
)

# Run inference
input_ids = tokenizer.encode("What is AI?", return_tensors="np")
outputs = session.run(None, {"input_ids": input_ids})
```

---

### **2.5 Engine Comparison**

| Feature | vLLM | TensorRT-LLM | Triton | ONNX Runtime |
|---------|------|--------------|--------|--------------|
| **Primary Use Case** | LLM serving | NVIDIA GPU optimization | Multi-model serving | Cross-platform |
| **LLM Support** | Excellent | Excellent | Good | Good |
| **Non-LLM Models** | Limited | Limited | Excellent | Excellent |
| **Ease of Use** | Very Easy | Moderate | Moderate | Easy |
| **Performance** | 10/10 | 10/10 | 8/10 | 7/10 |
| **Flexibility** | 7/10 | 6/10 | 9/10 | 9/10 |
| **Multi-GPU** | Yes | Yes | Yes | Limited |
| **Quantization** | Limited | Excellent | Good | Good |
| **Best For** | Production LLM serving | Max performance on NVIDIA | Multi-model platforms | Edge/cross-platform |

---

## Module 3: Batching Strategies

### **3.1 Static Batching**

**How It Works:**
- Collect N requests before processing
- Pad all sequences to same length
- Process as single batch

```python
def static_batch(requests, batch_size=32):
    batches = []
    current_batch = []
    
    for request in requests:
        current_batch.append(request)
        if len(current_batch) == batch_size:
            batches.append(current_batch)
            current_batch = []
    
    if current_batch:  # Remaining requests
        batches.append(current_batch)
    
    return batches
```

**Pros:**
- Simple to implement
- Predictable memory usage
- Good for offline batch processing

**Cons:**
- **Padding waste:** Short sequences padded to match longest
- **Latency:** Requests wait for batch to fill
- **Inefficient:** Under variable-length workloads

**Use Cases:**
- Offline batch inference
- Training data preprocessing
- Non-interactive workloads

---

### **3.2 Dynamic Batching**

**How It Works:**
- Wait for configurable time window (e.g., 10ms)
- Batch all requests that arrive in window
- Pad only within batch (less waste)

```python
import time
from threading import Lock

class DynamicBatcher:
    def __init__(self, max_batch_size=32, max_wait_ms=10):
        self.max_batch_size = max_batch_size
        self.max_wait_ms = max_wait_ms
        self.queue = []
        self.lock = Lock()
    
    def add_request(self, request):
        with self.lock:
            self.queue.append(request)
            
            # Check if batch is ready
            if len(self.queue) >= self.max_batch_size:
                return self._flush_batch()
            elif len(self.queue) == 1:
                # Start timer for first request
                self.start_time = time.time()
                return None
            
            # Check if wait time exceeded
            if (time.time() - self.start_time) * 1000 > self.max_wait_ms:
                return self._flush_batch()
            
            return None
    
    def _flush_batch(self):
        batch = self.queue[:self.max_batch_size]
        self.queue = self.queue[self.max_batch_size:]
        return batch
```

**Pros:**
- **Better latency:** Requests don't wait as long
- **Less padding:** Batch only similar-length sequences
- **Adaptive:** Handles variable load

**Cons:**
- More complex implementation
- Still some padding waste
- Timer overhead

**Use Cases:**
- Real-time inference with variable traffic
- Microservice architectures
- Multi-tenant serving

---

### **3.3 Continuous Batching (In-Flight Batching)**

**How It Works:**
- Add/remove requests at **iteration boundaries**
- No waiting for batch to complete
- Maximize GPU utilization

```python
class ContinuousBatcher:
    def __init__(self, max_batch_size=32):
        self.max_batch_size = max_batch_size
        self.active_requests = []  # Currently processing
        self.pending_queue = []    # Waiting to start
    
    def iteration_step(self):
        # Check for completed requests
        completed = [req for req in self.active_requests if req.is_done()]
        self.active_requests = [req for req in self.active_requests if not req.is_done()]
        
        # Add new requests to fill available slots
        available_slots = self.max_batch_size - len(self.active_requests)
        new_requests = self.pending_queue[:available_slots]
        self.pending_queue = self.pending_queue[available_slots:]
        self.active_requests.extend(new_requests)
        
        # Process one iteration for all active requests
        outputs = []
        for req in self.active_requests:
            output = req.process_next_token()
            outputs.append(output)
        
        return outputs
    
    def add_request(self, request):
        if len(self.active_requests) < self.max_batch_size:
            self.active_requests.append(request)
        else:
            self.pending_queue.append(request)
```

**Pros:**
- **Maximum GPU utilization:** No idle time
- **Best throughput:** 2–4× vs. static batching
- **Fair latency:** All requests progress together

**Cons:**
- Complex implementation
- Requires iteration-level control
- Not supported by all frameworks

**Use Cases:**
- High-throughput LLM serving (vLLM, TensorRT-LLM)
- Production systems with strict SLAs
- Multi-tenant platforms

---

### **3.4 Micro-Batching**

**How It Works:**
- Split large batches into smaller micro-batches
- Process micro-batches sequentially on GPU
- Overlap micro-batch execution with data transfer

```python
def micro_batch_inference(large_batch, micro_batch_size=8):
    micro_batches = [
        large_batch[i:i+micro_batch_size]
        for i in range(0, len(large_batch), micro_batch_size)
    ]
    
    results = []
    for micro_batch in micro_batches:
        # Process micro-batch on GPU
        output = run_on_gpu(micro_batch)
        results.append(output)
    
    return concatenate(results)
```

**Pros:**
- **Memory efficiency:** Process larger logical batches
- **Overlap:** Compute + data transfer
- **Flexibility:** Tune micro-batch size for hardware

**Cons:**
- Sequential overhead
- More kernel launches
- Complex synchronization

**Use Cases:**
- Memory-constrained environments
- Large batch inference
- Multi-GPU setups

---

### **3.5 Request Prioritization & Preemption**

**Priority Levels:**

```python
from enum import Enum

class Priority(Enum):
    P0_CRITICAL = 0    # Interactive chat, high-value users
    P1_STANDARD = 1    # Regular API requests
    P2_BATCH = 2       # Offline processing, analytics
```

**Priority Queue Implementation:**

```python
import heapq

class PriorityBatcher:
    def __init__(self, max_batch_size=32):
        self.max_batch_size = max_batch_size
        self.priority_queue = []  # Min-heap (lower = higher priority)
        self.counter = 0  # For FIFO within same priority
    
    def add_request(self, request, priority):
        # Heap entry: (priority, counter, request)
        heapq.heappush(self.priority_queue, (priority, self.counter, request))
        self.counter += 1
    
    def get_next_batch(self):
        batch = []
        while len(batch) < self.max_batch_size and self.priority_queue:
            _, _, request = heapq.heappop(self.priority_queue)
            batch.append(request)
        return batch
```

**Preemption Strategy:**

```python
def preempt_low_priority(active_requests, pending_high_priority):
    """Preempt low-priority requests to make room for high-priority."""
    
    # Sort active by priority (lowest first)
    active_sorted = sorted(active_requests, key=lambda r: r.priority, reverse=True)
    
    # Preempt lowest priority requests
    to_preempt = active_sorted[:len(pending_high_priority)]
    to_keep = active_sorted[len(pending_high_priority):]
    
    # Save state of preempted requests (for resumption)
    for req in to_preempt:
        save_checkpoint(req)
        req.status = "PREEMPTED"
    
    return to_keep + pending_high_priority
```

**Use Cases:**
- Multi-tenant SaaS (premium vs. free users)
- Mixed workloads (interactive + batch)
- SLA-driven systems

---

### **3.6 Batching Strategy Comparison**

| Strategy | Throughput | Latency | GPU Utilization | Complexity | Best For |
|----------|------------|---------|-----------------|------------|----------|
| **Static** | Low | High | 60–70% | Low | Offline batch |
| **Dynamic** | Medium | Medium | 70–80% | Medium | Real-time API |
| **Continuous** | High | Low | 85–95% | High | Production LLM |
| **Micro-Batch** | Medium-High | Medium | 75–85% | Medium | Memory-constrained |
| **Priority** | Variable | Variable | 70–90% | High | Multi-tenant SLA |

---

## Module 4: Multi-Model Orchestration

### **4.1 Model Composition Patterns**

#### **Pattern 1: Cascade (Early Exit)**

```
Input → [Fast Model] → Confidence > 0.9? → Output (80% of requests)
                     ↓ No
                [Slow Model] → Output (20% of requests)
```

**Use Case:** Broadcom malware detection (XGBoost fast filter + CNN deep analyzer)

**Implementation:**

```python
class CascadeInference:
    def __init__(self, fast_model, slow_model, threshold=0.9):
        self.fast_model = fast_model
        self.slow_model = slow_model
        self.threshold = threshold
    
    def infer(self, input):
        # Fast model
        fast_output, confidence = self.fast_model.predict(input)
        
        if confidence > self.threshold:
            return fast_output  # Early exit (80% of cases)
        
        # Slow model (only for uncertain cases)
        slow_output = self.slow_model.predict(input)
        return slow_output
```

**Benefits:**
- **2–5× throughput improvement** (most requests handled by fast model)
- **Lower average latency** (fast path for easy cases)
- **Cost savings** (expensive model only for hard cases)

---

#### **Pattern 2: Ensemble (Voting/Averaging)**

```
Input → [Model A] → Score A ─┐
         [Model B] → Score B ─┼→ Aggregate → Output
         [Model C] → Score C ─┘
```

**Use Case:** CapitalOne fraud scoring (XGBoost + GBDT + neural ensemble)

**Implementation:**

```python
class EnsembleInference:
    def __init__(self, models, weights=None):
        self.models = models
        self.weights = weights or [1.0] * len(models)
    
    def infer(self, input):
        scores = []
        for model in self.models:
            score = model.predict(input)
            scores.append(score)
        
        # Weighted average
        final_score = sum(s * w for s, w in zip(scores, self.weights))
        return final_score
```

**Benefits:**
- **Higher accuracy** (models complement each other)
- **Robustness** (single model failure doesn't break system)
- **Flexibility** (tune weights per use case)

---

#### **Pattern 3: Pipeline (Sequential)**

```
Input → [Model A: Preprocessing] → [Model B: Core] → [Model C: Postprocessing] → Output
```

**Use Case:** Apple Siri (ASR → NLU → Search → Orchestration → TTS)

**Implementation:**

```python
class PipelineInference:
    def __init__(self, models):
        self.models = models  # Ordered list
    
    def infer(self, input):
        intermediate = input
        for model in self.models:
            intermediate = model.predict(intermediate)
        return intermediate
```

**Benefits:**
- **Separation of concerns** (each model does one thing well)
- **Reusability** (swap individual models)
- **Parallelism** (pipeline different requests through stages)

---

#### **Pattern 4: Router (Conditional)**

```
Input → [Router Model] → Class A? → [Model A]
                       → Class B? → [Model B]
                       → Class C? → [Model C]
```

**Use Case:** Fiserv loan classification (route to specialized models by loan type)

**Implementation:**

```python
class RouterInference:
    def __init__(self, router_model, specialist_models):
        self.router_model = router_model
        self.specialist_models = specialist_models  # Dict: class → model
    
    def infer(self, input):
        # Router determines which specialist to use
        route_class = self.router_model.classify(input)
        
        # Route to specialist
        specialist = self.specialist_models[route_class]
        return specialist.predict(input)
```

**Benefits:**
- **Specialization** (each model optimized for specific class)
- **Efficiency** (don't run all models for every request)
- **Scalability** (add new specialists without retraining others)

---

### **4.2 Dependency-Aware Execution (DAG)**

#### **Model Dependency Graph:**

```python
from networkx import DiGraph

class ModelDAG:
    def __init__(self):
        self.graph = DiGraph()
    
    def add_model(self, model_id, model, dependencies=[]):
        self.graph.add_node(model_id, model=model)
        for dep in dependencies:
            self.graph.add_edge(dep, model_id)
    
    def get_execution_order(self):
        # Topological sort
        return list(nx.topological_sort(self.graph))
    
    def get_parallel_groups(self):
        # Models that can run in parallel
        return list(nx.topological_generations(self.graph))
```

#### **Example: Fraud Detection DAG:**

```python
dag = ModelDAG()

# Add models with dependencies
dag.add_model("feature_extractor", FeatureExtractor())
dag.add_model("xgboost_scorer", XGBoostScorer(), dependencies=["feature_extractor"])
dag.add_model("neural_scorer", NeuralScorer(), dependencies=["feature_extractor"])
dag.add_model("rule_engine", RuleEngine(), dependencies=["feature_extractor"])
dag.add_model("ensemble_aggregator", EnsembleAggregator(),
              dependencies=["xgboost_scorer", "neural_scorer", "rule_engine"])
dag.add_model("decision_engine", DecisionEngine(), dependencies=["ensemble_aggregator"])

# Execution order:
# 1. feature_extractor (no dependencies)
# 2. xgboost_scorer, neural_scorer, rule_engine (parallel, all depend on features)
# 3. ensemble_aggregator (depends on all scorers)
# 4. decision_engine (final decision)
```

#### **Parallel Execution:**

```python
import asyncio

async def execute_dag_parallel(dag, input_data):
    results = {}
    
    for group in dag.get_parallel_groups():
        # Execute models in this group in parallel
        tasks = []
        for model_id in group:
            model = dag.graph.nodes[model_id]["model"]
            
            # Gather dependencies
            deps = list(dag.graph.predecessors(model_id))
            dep_inputs = {dep: results[dep] for dep in deps}
            
            # Schedule task
            task = asyncio.create_task(model.predict_async(dep_inputs))
            tasks.append((model_id, task))
        
        # Wait for all in group to complete
        for model_id, task in tasks:
            results[model_id] = await task
    
    return results
```

---

### **4.3 Early Exit Optimization**

#### **Confidence-Based Early Exit:**

```python
class EarlyExitModel:
    def __init__(self, layers, exit_thresholds):
        self.layers = layers
        self.exit_thresholds = exit_thresholds  # Per-layer thresholds
    
    def infer(self, input):
        hidden = input
        
        for i, layer in enumerate(self.layers):
            hidden = layer(hidden)
            
            # Check if can exit early
            if i in self.exit_thresholds:
                confidence = self.compute_confidence(hidden)
                if confidence > self.exit_thresholds[i]:
                    return self.classify(hidden), confidence  # Early exit
        
        # Final layer (no early exit)
        return self.classify(hidden), self.compute_confidence(hidden)
```

**Benefits:**
- **30–50% latency reduction** for easy cases
- **Dynamic compute** (spend more on hard cases)
- **Energy efficiency** (less compute for confident predictions)

---

### **4.4 Model Versioning & Canary Deployments**

#### **Version Management:**

```python
class ModelRegistry:
    def __init__(self):
        self.models = {}  # model_name → {version → model_instance}
        self.active_versions = {}  # model_name → active_version
    
    def register(self, model_name, version, model):
        if model_name not in self.models:
            self.models[model_name] = {}
        self.models[model_name][version] = model
    
    def deploy(self, model_name, version, traffic_percent=100):
        """Deploy model version with traffic splitting."""
        self.active_versions[model_name] = {
            "version": version,
            "traffic_percent": traffic_percent
        }
    
    def get_model(self, model_name):
        active = self.active_versions[model_name]
        return self.models[model_name][active["version"]]
```

#### **Canary Deployment:**

```python
class CanaryDeployment:
    def __init__(self, old_model, new_model, canary_percent=10):
        self.old_model = old_model
        self.new_model = new_model
        self.canary_percent = canary_percent
    
    def infer(self, input):
        import random
        
        if random.randint(1, 100) <= self.canary_percent:
            # Route to new model (canary)
            return self.new_model.predict(input)
        else:
            # Route to old model (baseline)
            return self.old_model.predict(input)
    
    def monitor_metrics(self):
        """Compare canary vs. baseline metrics."""
        canary_metrics = self.new_model.get_metrics()
        baseline_metrics = self.old_model.get_metrics()
        
        # Check if canary is performing well
        if canary_metrics["error_rate"] > baseline_metrics["error_rate"] * 1.5:
            return "ROLLBACK"  # Canary performing worse
        elif canary_metrics["latency_p99"] > baseline_metrics["latency_p99"] * 1.2:
            return "ROLLBACK"  # Canary too slow
        else:
            return "PROCEED"  # Canary looks good
```

---

## Module 5: KV-Cache Management

### **5.1 Understanding KV-Cache**

#### **What is KV-Cache?**

During autoregressive generation, transformer models compute Key (K) and Value (V) matrices for each token. These can be **cached** to avoid re-computation.

**Without KV-Cache:**
```
Generate token 1: Process tokens [1]
Generate token 2: Process tokens [1, 2]  # Re-process token 1!
Generate token 3: Process tokens [1, 2, 3]  # Re-process tokens 1, 2!
→ O(n²) complexity
```

**With KV-Cache:**
```
Generate token 1: Compute K,V for token 1 → Cache
Generate token 2: Compute K,V for token 2 → Cache (reuse token 1's K,V)
Generate token 3: Compute K,V for token 3 → Cache (reuse tokens 1,2's K,V)
→ O(n) complexity
```

**Memory Savings:**
- **7B model, 512 tokens:** ~2GB without cache → ~500MB with cache
- **Latency:** 26 seconds → 3 seconds (8× speedup)

---

### **5.2 KV-Cache Memory Layout**

#### **Traditional Approach (Contiguous):**

```python
# Allocate contiguous memory per request
kv_cache = torch.zeros(num_layers, 2, batch_size, num_heads, seq_len, head_dim)
# Problem: Over-allocate for max_seq_len, waste memory
```

**Issues:**
- **Over-allocation:** Reserve for max_seq_len (most requests use less)
- **Fragmentation:** Can't share memory between requests
- **Inflexible:** Can't grow beyond pre-allocated size

#### **PagedAttention (vLLM):**

```python
# Allocate in fixed-size blocks (e.g., 16 tokens per block)
class PagedKVCache:
    def __init__(self, block_size=16):
        self.block_size = block_size
        self.blocks = {}  # request_id → list of block_ids
        self.free_blocks = list(range(max_blocks))
    
    def allocate_block(self, request_id):
        block_id = self.free_blocks.pop()
        if request_id not in self.blocks:
            self.blocks[request_id] = []
        self.blocks[request_id].append(block_id)
        return block_id
    
    def get_block_table(self, request_id):
        return self.blocks[request_id]
```

**Benefits:**
- **On-demand allocation:** Only allocate blocks as needed
- **No fragmentation:** Fixed-size blocks
- **Sharing:** Multiple requests can share blocks (common prefixes)

---

### **5.3 Cache Eviction Policies**

#### **LRU (Least Recently Used):**

```python
from collections import OrderedDict

class LRUKVCache:
    def __init__(self, max_size):
        self.cache = OrderedDict()
        self.max_size = max_size
    
    def get(self, key):
        if key in self.cache:
            self.cache.move_to_end(key)  # Mark as recently used
            return self.cache[key]
        return None
    
    def put(self, key, value):
        if key in self.cache:
            self.cache.move_to_end(key)
        self.cache[key] = value
        
        if len(self.cache) > self.max_size:
            # Evict least recently used
            self.cache.popitem(last=False)
```

**Use Case:** General-purpose caching, good for temporal locality

---

#### **LFU (Least Frequently Used):**

```python
from collections import defaultdict

class LFUKVCache:
    def __init__(self, max_size):
        self.cache = {}
        self.freq = defaultdict(int)
        self.max_size = max_size
    
    def get(self, key):
        if key in self.cache:
            self.freq[key] += 1
            return self.cache[key]
        return None
    
    def put(self, key, value):
        if key in self.cache:
            self.freq[key] += 1
        else:
            self.freq[key] = 1
        
        self.cache[key] = value
        
        if len(self.cache) > self.max_size:
            # Evict least frequently used
            lfu_key = min(self.freq, key=self.freq.get)
            del self.cache[lfu_key]
            del self.freq[lfu_key]
```

**Use Case:** Hot sessions (frequently accessed conversations)

---

#### **Session-Aware Eviction:**

```python
class SessionKVCache:
    def __init__(self, max_size):
        self.cache = {}
        self.sessions = {}  # session_id → list of keys
        self.max_size = max_size
    
    def add_to_session(self, session_id, key, value):
        if session_id not in self.sessions:
            self.sessions[session_id] = []
        
        self.sessions[session_id].append(key)
        self.cache[key] = value
        
        # Evict oldest session if over capacity
        if len(self.cache) > self.max_size:
            oldest_session = min(self.sessions.keys())
            for key in self.sessions[oldest_session]:
                del self.cache[key]
            del self.sessions[oldest_session]
```

**Use Case:** Multi-turn conversations (keep entire session in cache)

---

### **5.4 Prefix Caching**

#### **What is Prefix Caching?**

Many requests share common prefixes (e.g., system prompts, few-shot examples). Cache these prefixes to avoid re-computation.

```
Request 1: "System: You are helpful. User: What is AI?"
Request 2: "System: You are helpful. User: What is ML?"
Request 3: "System: You are helpful. User: What is DL?"
→ All share "System: You are helpful." prefix
```

#### **Implementation:**

```python
class PrefixKVCache:
    def __init__(self):
        self.prefix_cache = {}  # prefix_hash → (kv_cache, ref_count)
    
    def compute_prefix_hash(self, tokens):
        # Hash first N tokens (prefix)
        prefix = tuple(tokens[:32])  # First 32 tokens
        return hash(prefix)
    
    def get_cached_prefix(self, tokens):
        prefix_hash = self.compute_prefix_hash(tokens)
        if prefix_hash in self.prefix_cache:
            self.prefix_cache[prefix_hash]["ref_count"] += 1
            return self.prefix_cache[prefix_hash]["kv_cache"]
        return None
    
    def cache_prefix(self, tokens, kv_cache):
        prefix_hash = self.compute_prefix_hash(tokens)
        self.prefix_cache[prefix_hash] = {
            "kv_cache": kv_cache,
            "ref_count": 1
        }
```

**Benefits:**
- **60% token reduction** for few-shot learning
- **Faster cold starts** for common prompts
- **Cost savings** for repeated system prompts

---

### **5.5 Multi-Turn Conversation Management**

#### **Session Stickiness:**

```python
class SessionRouter:
    def __init__(self, num_instances):
        self.num_instances = num_instances
        self.session_to_instance = {}
        self.instance_load = [0] * num_instances
    
    def route(self, session_id):
        if session_id in self.session_to_instance:
            # Route to same instance (KV-cache hit)
            return self.session_to_instance[session_id]
        
        # New session: route to least loaded instance
        instance_id = min(range(self.num_instances),
                         key=lambda i: self.instance_load[i])
        self.session_to_instance[session_id] = instance_id
        self.instance_load[instance_id] += 1
        return instance_id
```

**Benefits:**
- **70% KV-cache hit rate** (Fiserv metric)
- **400ms latency savings** per session
- **3× GPU memory efficiency**

---

## Module 6: Prompt Optimization

### **6.1 Prompt Caching Strategies**

#### **Full Prompt Caching:**

```python
class PromptCache:
    def __init__(self):
        self.cache = {}  # prompt_hash → response
    
    def get(self, prompt):
        prompt_hash = hash(prompt)
        return self.cache.get(prompt_hash)
    
    def set(self, prompt, response):
        prompt_hash = hash(prompt)
        self.cache[prompt_hash] = response
```

**Use Case:** FAQ bots, common queries

---

#### **Prefix Caching (for Few-Shot):**

```python
class FewShotPromptCache:
    def __init__(self):
        self.prefix_cache = {}  # prefix → kv_cache
    
    def cache_few_shot_prefix(self, examples):
        # Cache KV for few-shot examples
        prefix_tokens = tokenize(examples)
        kv_cache = compute_kv_cache(prefix_tokens)
        self.prefix_cache[hash(examples)] = kv_cache
    
    def generate_with_cached_prefix(self, examples, query):
        # Reuse cached KV for examples
        cached_kv = self.prefix_cache[hash(examples)]
        
        # Only compute KV for query
        query_tokens = tokenize(query)
        output = generate(query_tokens, cached_kv=cached_kv)
        return output
```

**Benefits:**
- **80% latency reduction** for few-shot inference
- **Enables longer few-shot** (more examples without memory blowup)

---

### **6.2 Context Window Management**

#### **Sliding Window Attention:**

```python
class SlidingWindowContext:
    def __init__(self, max_context=4096, window_size=2048):
        self.max_context = max_context
        self.window_size = window_size
    
    def truncate_context(self, tokens):
        if len(tokens) <= self.max_context:
            return tokens
        
        # Keep first 10% (system prompt, instructions)
        prefix_len = int(len(tokens) * 0.1)
        prefix = tokens[:prefix_len]
        
        # Keep last window_size - prefix_len (recent context)
        suffix = tokens[prefix_len - self.max_context:]
        
        return prefix + suffix
```

**Use Case:** Long conversations, document Q&A

---

#### **Hierarchical Context Assembly:**

```python
class HierarchicalContext:
    def __init__(self):
        self.extractor = DocumentExtractor()
        self.summarizer = SummarizerLLM()
        self.assembler = ContextAssembler()
    
    def build_context(self, documents, max_tokens=8000):
        # Tier 1: Extract structured data
        structured = self.extractor.extract(documents)
        
        # Tier 2: Generate summaries
        summaries = self.summarizer.summarize(structured)
        
        # Tier 3: Assemble targeted context
        context = self.assembler.assemble(
            summaries=summaries,
            max_tokens=max_tokens,
            priority=["critical", "important", "optional"]
        )
        
        return context
```

**Benefits:**
- **60% token reduction** (Fiserv metric)
- **Better accuracy** (focused context vs. document dump)
- **Cost savings** (fewer tokens = lower API costs)

---

## Module 7: Advanced Optimization Techniques

### **7.1 Speculative Decoding**

#### **Concept:**

Use a small "draft" model to generate tokens quickly, then verify with large model.

```
Draft Model (small, fast): Generate tokens [A, B, C, D]
Large Model (accurate, slow): Verify all tokens in parallel
→ Accept correct tokens, reject and re-sample incorrect ones
```

**Speedup:** 2–3× if draft model has high acceptance rate (>70%)

#### **Implementation:**

```python
class SpeculativeDecoder:
    def __init__(self, draft_model, large_model, num_draft_tokens=4):
        self.draft_model = draft_model
        self.large_model = large_model
        self.num_draft_tokens = num_draft_tokens
    
    def decode(self, input_tokens):
        # Draft model generates K tokens
        draft_tokens = self.draft_model.generate(
            input_tokens,
            max_tokens=self.num_draft_tokens
        )
        
        # Large model verifies all tokens in parallel
        acceptance_probs = self.large_model.verify(
            input_tokens,
            draft_tokens
        )
        
        # Accept tokens above threshold
        accepted_tokens = []
        for token, prob in zip(draft_tokens, acceptance_probs):
            if prob > 0.8:
                accepted_tokens.append(token)
            else:
                # Reject, sample from large model
                new_token = self.large_model.sample(input_tokens)
                accepted_tokens.append(new_token)
                break
        
        return accepted_tokens
```

---

### **7.2 Quantization**

#### **Post-Training Quantization (PTQ):**

```python
from transformers import AutoModelForCausalLM
from optimum.quanto import quantize_model, QBytes

# Load model
model = AutoModelForCausalLM.from_pretrained("meta-llama/Llama-2-7b")

# Quantize to INT8
quantized_model = quantize_model(
    model,
    weights=QBytes.INT8,
    activations=QBytes.INT8
)

# 4× smaller, 2× faster, <1% accuracy loss
```

#### **Quantization-Aware Training (QAT):**

```python
# Train with quantization simulation
model = AutoModelForCausalLM.from_pretrained("meta-llama/Llama-2-7b")

# Add fake quantization layers
from torch.ao.quantization import QuantStub, DeQuantStub
model.quant = QuantStub()
model.dequant = DeQuantStub()

# Fine-tune with quantization aware training
# Better accuracy than PTQ, but requires training
```

---

### **7.3 FlashAttention**

#### **What is FlashAttention?**

IO-aware attention algorithm that reduces memory reads/writes.

**Traditional Attention:**
```
Load Q, K, V from HBM → Compute attention → Write to HBM
→ O(n²) memory accesses
```

**FlashAttention:**
```
Load Q, K, V in blocks → Compute attention in SRAM → Write once
→ O(n) memory accesses
→ 2–3× speedup
```

#### **Usage:**

```python
from flash_attn import flash_attn_qkvpacked_func

# Use FlashAttention instead of standard attention
output = flash_attn_qkvpacked_func(
    qkv,  # [batch, seq_len, 3, num_heads, head_dim]
    dropout_p=0.0,
    softmax_scale=None,
    causal=True
)
```

---

## Module 8: Traditional ML Model Inference at Scale

### **8.1 Traditional ML Inference Pipeline**

#### **Why Traditional ML Still Dominates:**

Despite the LLM hype, **70–80% of production inference workloads** are traditional ML models:

| Domain | Models Used | Latency Requirement | Volume |
|--------|-------------|-------------------|--------|
| **Fraud Detection** | XGBoost, LightGBM, Random Forest | < 10ms (real-time) | 50K–500K TPS |
| **Payment Processing** | Gradient Boosted Trees, Logistic Regression | < 5ms (inline) | 100K+ TPS |
| **Credit Scoring** | Ensemble (GBDT + LR) | < 50ms | 10K+ TPS |
| **Recommendation** | Two-tower embeddings, ANN | < 20ms | 1M+ QPS |
| **Ad Ranking** | Wide & Deep, DeepFM | < 10ms | 10M+ QPS |
| **Risk Assessment** | XGBoost + Rule Engine | < 100ms | 5K–50K TPS |

**Key Insight:** Traditional ML inference differs fundamentally from LLM inference:
- **No autoregressive generation** (single forward pass)
- **CPU-friendly** (tree models don't need GPUs)
- **Feature engineering dominates** (80% of latency in feature computation)
- **Extreme throughput** (millions of predictions/second)
- **Ultra-low latency** (single-digit milliseconds)

---

#### **Traditional ML Inference Pipeline:**

```
Raw Event → Feature Store Lookup → Feature Engineering → Model Prediction → Post-Processing → Decision
              (2–5 ms)              (1–3 ms)              (0.5–2 ms)          (0.5–1 ms)       (0.5 ms)
```

**Total Budget: 5–15 ms end-to-end**

---

### **8.2 Feature Store & Real-Time Feature Engineering**

#### **Feature Store Architecture:**

```
┌─────────────────────────────────────────────────────────┐
│                    Feature Store                          │
├─────────────────────────────────────────────────────────┤
│  Online Store (Redis/DynamoDB)                           │
│  • Pre-computed features                                 │
│  • p99 < 2ms lookup                                      │
│  • User profile, historical aggregates                   │
├─────────────────────────────────────────────────────────┤
│  Real-Time Compute (Flink/Kafka Streams)                 │
│  • Windowed aggregations (last 1h, 24h, 7d)             │
│  • Streaming features (velocity, frequency)              │
│  • Event-driven updates                                  │
├─────────────────────────────────────────────────────────┤
│  Offline Store (Spark/BigQuery)                           │
│  • Batch features (daily, weekly)                        │
│  • Training data generation                              │
│  • Backfill pipelines                                    │
└─────────────────────────────────────────────────────────┘
```

#### **Real-Time Feature Engineering (Fraud Detection Example):**

```python
class FraudFeatureEngine:
    def __init__(self, feature_store, stream_processor):
        self.feature_store = feature_store
        self.stream = stream_processor
    
    def compute_features(self, transaction):
        # 1. Static features (pre-computed, O(1) lookup)
        user_profile = self.feature_store.get(f"user:{transaction.user_id}")
        merchant_profile = self.feature_store.get(f"merchant:{transaction.merchant_id}")
        
        # 2. Real-time streaming features (windowed aggregations)
        velocity_features = self.stream.get_windowed_features(
            key=transaction.user_id,
            windows=["1min", "5min", "1hour", "24hour"]
        )
        
        # 3. Derived features (computed inline)
        derived = {
            "amount_vs_avg_ratio": transaction.amount / (user_profile["avg_amount"] + 1e-6),
            "distance_from_last_txn": haversine(
                transaction.location, user_profile["last_location"]
            ),
            "time_since_last_txn_sec": (
                transaction.timestamp - user_profile["last_txn_time"]
            ).total_seconds(),
            "is_new_merchant": merchant_profile.get("first_seen") is None,
            "txn_count_1h": velocity_features["count_1hour"],
            "amount_sum_24h": velocity_features["sum_24hour"],
        }
        
        # 4. Assemble feature vector
        feature_vector = {**user_profile, **merchant_profile, **velocity_features, **derived}
        return feature_vector
```

#### **Feature Computation Optimization:**

```python
class OptimizedFeatureEngine:
    """Parallel feature computation with caching."""
    
    def __init__(self, feature_store, cache_ttl_ms=100):
        self.feature_store = feature_store
        self.local_cache = TTLCache(maxsize=10000, ttl=cache_ttl_ms / 1000)
    
    async def compute_features_parallel(self, transaction):
        """Fetch all features in parallel (not sequentially)."""
        
        # Launch all lookups concurrently
        user_task = asyncio.create_task(
            self.feature_store.get_async(f"user:{transaction.user_id}")
        )
        merchant_task = asyncio.create_task(
            self.feature_store.get_async(f"merchant:{transaction.merchant_id}")
        )
        velocity_task = asyncio.create_task(
            self.stream.get_windowed_features_async(transaction.user_id)
        )
        
        # Await all concurrently (2ms instead of 6ms sequential)
        user, merchant, velocity = await asyncio.gather(
            user_task, merchant_task, velocity_task
        )
        
        return self.assemble_features(transaction, user, merchant, velocity)
```

**Performance Impact:**
- Sequential lookup: 6–8ms
- Parallel lookup: 2–3ms
- With local cache: 0.5–1ms (for repeated users)

---

### **8.3 Model Serving Frameworks for Traditional ML**

#### **Framework Comparison:**

| Framework | Best For | Latency | Throughput | Language |
|-----------|----------|---------|------------|----------|
| **ONNX Runtime** | Cross-platform, tree models | < 1ms | Very High | C++/Python |
| **Triton** | Multi-model, GPU/CPU | < 5ms | High | C++ |
| **TensorFlow Serving** | TF models | < 10ms | High | C++ |
| **TorchServe** | PyTorch models | < 10ms | Medium | Java/Python |
| **Seldon Core** | Kubernetes-native | < 20ms | Medium | Python |
| **BentoML** | Easy deployment | < 15ms | Medium | Python |
| **Custom C++** | Ultra-low latency | < 0.5ms | Very High | C++ |

#### **ONNX Runtime for Tree Models (Production Standard):**

```python
import onnxruntime as ort
import numpy as np
from skl2onnx import convert_sklearn
from skl2onnx.common.data_types import FloatTensorType

# Convert XGBoost/LightGBM to ONNX
from onnxmltools import convert_xgboost

onnx_model = convert_xgboost(
    xgb_model,
    initial_types=[("features", FloatTensorType([None, num_features]))]
)

# Deploy with ONNX Runtime (sub-millisecond inference)
session_options = ort.SessionOptions()
session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
session_options.intra_op_num_threads = 4
session_options.inter_op_num_threads = 1

session = ort.InferenceSession(
    "fraud_model.onnx",
    sess_options=session_options,
    providers=["CPUExecutionProvider"]
)

# Inference (< 0.5ms for single request)
def predict(features: np.ndarray) -> float:
    input_name = session.get_inputs()[0].name
    result = session.run(None, {input_name: features.astype(np.float32)})
    return result[0][0]
```

**Why ONNX for Tree Models:**
- **0.1–0.5ms latency** (vs. 1–5ms with native scikit-learn)
- **Graph optimizations** (operator fusion, constant folding)
- **Thread-efficient** (no GIL issues)
- **Portable** (same model runs on any platform)

---

#### **Custom C++ Inference for Ultra-Low Latency:**

```cpp
#include <treelite/c_api.h>

class FastTreeInference {
private:
    TreeliteModelHandle model_;
    TreelitePredictorHandle predictor_;
    
public:
    FastTreeInference(const std::string& model_path) {
        TreeliteLoadModel(model_path.c_str(), &model_);
        TreelitePredictorCreate(model_, 4 /* num_threads */, &predictor_);
    }
    
    float predict(const float* features, size_t num_features) {
        float result;
        TreelitePredictorPredict(
            predictor_, features, num_features, &result
        );
        return result;
    }
    
    // Batch prediction (vectorized, SIMD-optimized)
    void predict_batch(const float* features, size_t batch_size,
                       size_t num_features, float* results) {
        TreelitePredictorPredictBatch(
            predictor_, features, batch_size, num_features, results
        );
    }
};
```

**Performance:**
- Single prediction: **< 100 microseconds**
- Batch of 1000: **< 5ms** (SIMD vectorized)
- **10× faster** than Python-based inference

---

### **8.4 Tree Model Optimization**

#### **Model Compilation with Treelite:**

```python
import treelite
import treelite_runtime

# Compile XGBoost model to native code
model = treelite.Model.from_xgboost(xgb_model)

# Compile to shared library (C code → .so)
model.export_lib(
    toolchain="gcc",
    libpath="./fraud_model.so",
    params={
        "parallel_comp": 32,  # Parallel compilation
        "quantize": 1         # Quantize thresholds
    }
)

# Load compiled model (10× faster than native XGBoost)
predictor = treelite_runtime.Predictor("./fraud_model.so")
result = predictor.predict(features_batch)
```

**Speedup:** 5–10× over native XGBoost/LightGBM predict()

---

#### **Model Pruning & Compression:**

```python
class TreeModelOptimizer:
    """Optimize tree models for inference speed."""
    
    def prune_model(self, model, max_depth=6, min_samples_leaf=100):
        """Reduce tree depth for faster inference."""
        # Shallower trees = fewer comparisons = lower latency
        # Trade-off: slight accuracy loss for significant speed gain
        pruned = clone_model_with_params(model, {
            "max_depth": max_depth,
            "min_samples_leaf": min_samples_leaf
        })
        return pruned
    
    def reduce_trees(self, model, target_n_estimators=100):
        """Use fewer trees with higher learning rate."""
        # 500 trees at lr=0.01 ≈ 100 trees at lr=0.05
        # 5× fewer tree traversals
        model.n_estimators = target_n_estimators
        return model
    
    def quantize_thresholds(self, model):
        """Quantize split thresholds to reduce memory footprint."""
        # Float64 → Float32 thresholds
        # Reduces model size by 50%, fits in CPU cache
        for tree in model.get_booster().trees:
            tree.thresholds = tree.thresholds.astype(np.float32)
        return model
```

**Optimization Impact:**

| Technique | Latency Reduction | Accuracy Impact | Memory Savings |
|-----------|-------------------|-----------------|----------------|
| Depth pruning (8→6) | 25% | < 0.1% AUC | 40% |
| Tree reduction (500→100) | 80% | 0.2–0.5% AUC | 80% |
| Threshold quantization | 10% | Negligible | 50% |
| ONNX compilation | 70% | None | - |

---

### **8.5 Embedding Model Inference (Recommendations/Search)**

#### **Two-Tower Architecture at Scale:**

```python
class TwoTowerInference:
    """
    Recommendation system with pre-computed item embeddings
    and real-time user embedding computation.
    """
    
    def __init__(self, user_model, item_index, top_k=100):
        self.user_model = user_model      # Small model, computed per-request
        self.item_index = item_index      # ANN index (FAISS/ScaNN)
        self.top_k = top_k
    
    def recommend(self, user_features):
        # Step 1: Compute user embedding (1–2ms)
        user_embedding = self.user_model.predict(user_features)
        
        # Step 2: ANN search against item embeddings (1–5ms)
        scores, item_ids = self.item_index.search(
            user_embedding.reshape(1, -1), self.top_k
        )
        
        return item_ids[0], scores[0]
```

#### **ANN Index Optimization (FAISS):**

```python
import faiss

class OptimizedANNIndex:
    def __init__(self, dimension, num_items):
        self.dimension = dimension
        
        # Choose index type based on dataset size
        if num_items < 100_000:
            # Flat index (exact, fast for small datasets)
            self.index = faiss.IndexFlatIP(dimension)
        elif num_items < 10_000_000:
            # IVF index (approximate, good balance)
            quantizer = faiss.IndexFlatIP(dimension)
            self.index = faiss.IndexIVFFlat(quantizer, dimension, 1024)
        else:
            # IVF + PQ (highly compressed, 100M+ items)
            quantizer = faiss.IndexFlatIP(dimension)
            self.index = faiss.IndexIVFPQ(
                quantizer, dimension, 4096, 32, 8
            )
    
    def build(self, embeddings):
        if hasattr(self.index, 'train'):
            self.index.train(embeddings)
        self.index.add(embeddings)
    
    def search(self, query, k=100):
        # Set nprobe for recall/speed trade-off
        if hasattr(self.index, 'nprobe'):
            self.index.nprobe = 64  # Search 64 clusters
        
        distances, indices = self.index.search(query, k)
        return distances, indices
```

**ANN Index Performance:**

| Index Type | Build Time | Search Latency | Recall@100 | Memory |
|-----------|------------|----------------|------------|--------|
| Flat (exact) | O(1) | 10ms (1M items) | 100% | 100% |
| IVF-Flat | O(n) | 1–2ms | 95% | 100% |
| IVF-PQ | O(n) | 0.5–1ms | 90% | 10% |
| HNSW | O(n log n) | 0.5ms | 98% | 130% |

---

### **8.6 Real-Time Scoring Architecture (Payment Processing)**

#### **Inline vs. Async Scoring:**

```
Inline Scoring (Payment Authorization):
Transaction → [Feature Compute] → [Model Score] → [Decision] → Approve/Deny
                                                                 (< 10ms total)

Async Scoring (Post-Authorization):
Transaction → Approve → [Queue] → [Feature Compute] → [Model Score] → Flag/Alert
                                                                        (< 1 sec)
```

#### **High-Throughput Scoring Service:**

```python
import asyncio
from concurrent.futures import ProcessPoolExecutor

class ScoringService:
    """
    Production scoring service handling 100K+ TPS.
    Uses process pool for CPU-bound model inference.
    """
    
    def __init__(self, model_path, num_workers=8):
        self.executor = ProcessPoolExecutor(max_workers=num_workers)
        self.model = load_model(model_path)  # ONNX session per worker
        self.feature_engine = OptimizedFeatureEngine()
        self.decision_engine = DecisionEngine()
    
    async def score_transaction(self, transaction):
        # 1. Feature computation (async I/O, 2ms)
        features = await self.feature_engine.compute_features_parallel(transaction)
        
        # 2. Model inference (CPU-bound, offload to process pool, 0.5ms)
        loop = asyncio.get_event_loop()
        score = await loop.run_in_executor(
            self.executor,
            self.model.predict,
            features
        )
        
        # 3. Decision (rule engine + threshold, 0.2ms)
        decision = self.decision_engine.decide(score, transaction)
        
        return decision
    
    async def score_batch(self, transactions):
        """Score batch of transactions concurrently."""
        tasks = [self.score_transaction(txn) for txn in transactions]
        return await asyncio.gather(*tasks)
```

#### **Connection Pooling & Resource Management:**

```python
class ResourcePool:
    """
    Manage model instances and feature store connections.
    Critical for sustaining 100K+ TPS.
    """
    
    def __init__(self, pool_size=16):
        self.model_pool = queue.Queue(maxsize=pool_size)
        self.redis_pool = redis.ConnectionPool(max_connections=pool_size * 2)
        
        # Pre-warm model instances
        for _ in range(pool_size):
            session = ort.InferenceSession("model.onnx", providers=["CPUExecutionProvider"])
            self.model_pool.put(session)
    
    def get_model(self):
        return self.model_pool.get(timeout=5)
    
    def return_model(self, session):
        self.model_pool.put(session)
    
    def predict_with_pool(self, features):
        session = self.get_model()
        try:
            result = session.run(None, {"features": features})
            return result
        finally:
            self.return_model(session)
```

---

### **8.7 Model Warm-Up & Cold Start Mitigation**

#### **Model Pre-Loading:**

```python
class ModelWarmer:
    """Pre-warm models to avoid cold start latency."""
    
    def __init__(self, model_path):
        self.model_path = model_path
        self.session = None
    
    def warm_up(self, num_warmup_requests=100):
        """Run dummy predictions to warm up CPU caches and JIT."""
        
        # Load model
        self.session = ort.InferenceSession(self.model_path)
        
        # Generate synthetic data matching production distribution
        dummy_features = np.random.randn(num_warmup_requests, self.num_features).astype(np.float32)
        
        # Run warmup predictions (fills CPU cache, triggers JIT compilation)
        for i in range(num_warmup_requests):
            self.session.run(None, {"features": dummy_features[i:i+1]})
        
        print(f"Model warmed up with {num_warmup_requests} predictions")
        return self.session
    
    def verify_latency(self, target_p99_ms=1.0):
        """Verify model meets latency target after warmup."""
        latencies = []
        test_data = np.random.randn(1000, self.num_features).astype(np.float32)
        
        for i in range(1000):
            start = time.perf_counter()
            self.session.run(None, {"features": test_data[i:i+1]})
            latencies.append((time.perf_counter() - start) * 1000)
        
        p99 = np.percentile(latencies, 99)
        print(f"p99 latency: {p99:.2f}ms (target: {target_p99_ms}ms)")
        assert p99 < target_p99_ms, f"Model too slow: {p99:.2f}ms > {target_p99_ms}ms"
```

**Cold Start Impact:**
- First prediction: 10–50ms (model loading, memory allocation)
- After warmup: 0.1–0.5ms
- **100× difference** — warmup is critical for production

---

### **8.8 Ensemble Scoring Patterns (Fraud/Risk)**

#### **Multi-Stage Scoring Pipeline:**

```python
class FraudScoringPipeline:
    """
    Production fraud scoring with multi-stage evaluation.
    CapitalOne/Fiserv pattern.
    """
    
    def __init__(self):
        self.rule_engine = RuleEngine()           # Fast rules (< 0.1ms)
        self.fast_model = XGBoostScorer()         # XGBoost (< 0.5ms)
        self.deep_model = NeuralScorer()          # Neural net (< 2ms)
        self.ensemble = EnsembleAggregator()
    
    def score(self, transaction, features):
        # Stage 1: Rule-based fast filter (< 0.1ms)
        rule_result = self.rule_engine.evaluate(transaction)
        if rule_result.action == "BLOCK":
            return Score(1.0, reason="rule_match", stage="rules")
        if rule_result.action == "ALLOW":
            return Score(0.0, reason="trusted", stage="rules")
        
        # Stage 2: Fast ML model (< 0.5ms)
        fast_score = self.fast_model.predict(features)
        if fast_score < 0.1:
            return Score(fast_score, reason="low_risk", stage="fast_model")
        if fast_score > 0.95:
            return Score(fast_score, reason="high_risk", stage="fast_model")
        
        # Stage 3: Deep model (only for uncertain cases, < 2ms)
        deep_score = self.deep_model.predict(features)
        
        # Stage 4: Ensemble
        final_score = self.ensemble.combine(
            fast_score=fast_score,
            deep_score=deep_score,
            rule_score=rule_result.score,
            weights=[0.4, 0.5, 0.1]
        )
        
        return Score(final_score, reason="ensemble", stage="full")
```

**Performance Profile:**
- 60% of transactions: resolved at Stage 1 (rules) — **< 0.1ms**
- 25% of transactions: resolved at Stage 2 (fast model) — **< 0.5ms**
- 15% of transactions: full pipeline — **< 3ms**
- **Weighted average: < 0.5ms**

---

## Module 9: Traditional ML Optimization & Production Patterns

### **9.1 CPU Optimization for ML Inference**

#### **NUMA-Aware Deployment:**

```python
import os

class NUMAOptimizedInference:
    """
    Pin model workers to specific NUMA nodes for
    optimal memory access patterns on multi-socket servers.
    """
    
    def __init__(self, numa_node=0):
        # Pin to specific NUMA node
        os.sched_setaffinity(0, self.get_cpus_for_numa(numa_node))
        
        # Set memory policy
        os.environ["OMP_NUM_THREADS"] = "8"
        os.environ["KMP_AFFINITY"] = "granularity=fine,compact,1,0"
        os.environ["MALLOC_CONF"] = "background_thread:true,metadata_thp:auto"
    
    def get_cpus_for_numa(self, node):
        """Get CPU cores belonging to NUMA node."""
        import subprocess
        result = subprocess.run(
            ["numactl", "--hardware"],
            capture_output=True, text=True
        )
        # Parse NUMA topology
        # Node 0: CPUs 0-15
        # Node 1: CPUs 16-31
        return set(range(node * 16, (node + 1) * 16))
```

#### **SIMD Vectorization for Feature Processing:**

```python
import numpy as np

class VectorizedFeatureProcessor:
    """
    Process features using NumPy vectorization (SIMD under the hood).
    Avoid Python loops for feature computation.
    """
    
    def compute_batch_features(self, transactions_batch):
        """
        Process batch of 1000 transactions simultaneously.
        NumPy uses AVX2/AVX-512 SIMD instructions.
        """
        amounts = np.array([t.amount for t in transactions_batch])
        avg_amounts = np.array([t.user_avg for t in transactions_batch])
        
        # Vectorized operations (SIMD, no Python loop)
        amount_ratios = amounts / (avg_amounts + 1e-6)
        log_amounts = np.log1p(amounts)
        z_scores = (amounts - avg_amounts) / (np.std(amounts) + 1e-6)
        
        # Stack into feature matrix
        features = np.column_stack([
            amounts, amount_ratios, log_amounts, z_scores
        ])
        
        return features.astype(np.float32)
```

**Performance:**
- Python loop (1000 features): 50ms
- NumPy vectorized (1000 features): 0.5ms
- **100× speedup** from vectorization

---

#### **Memory Layout Optimization:**

```python
class CacheOptimizedFeatures:
    """
    Store features in column-major (Fortran) order for
    tree model traversal efficiency.
    """
    
    def __init__(self, num_features):
        self.num_features = num_features
        # Pre-allocate aligned memory
        self.buffer = np.empty(
            (1024, num_features),
            dtype=np.float32,
            order='C'  # Row-major for batch prediction
        )
    
    def prepare_batch(self, raw_features):
        """Copy features into pre-allocated aligned buffer."""
        batch_size = len(raw_features)
        np.copyto(self.buffer[:batch_size], raw_features)
        return self.buffer[:batch_size]
```

**Why This Matters:**
- CPU L1 cache line: 64 bytes (16 float32 values)
- Row-major layout: consecutive features in same cache line
- **2× throughput** from cache-friendly access patterns

---

### **9.2 Model Monitoring & Drift Detection**

#### **Production Monitoring Framework:**

```python
from dataclasses import dataclass
from collections import deque
import numpy as np

@dataclass
class PredictionMetrics:
    timestamp: float
    latency_ms: float
    score: float
    features_hash: str
    model_version: str

class ModelMonitor:
    """
    Real-time monitoring for model health, drift, and performance.
    """
    
    def __init__(self, window_size=10000):
        self.predictions = deque(maxlen=window_size)
        self.baseline_stats = None
    
    def record_prediction(self, metrics: PredictionMetrics):
        self.predictions.append(metrics)
    
    def check_score_drift(self, threshold=0.05):
        """Detect if prediction score distribution has shifted."""
        recent_scores = [p.score for p in list(self.predictions)[-1000:]]
        
        # Compare to baseline
        current_mean = np.mean(recent_scores)
        baseline_mean = self.baseline_stats["score_mean"]
        
        drift = abs(current_mean - baseline_mean) / baseline_mean
        
        if drift > threshold:
            return {
                "alert": "SCORE_DRIFT",
                "current_mean": current_mean,
                "baseline_mean": baseline_mean,
                "drift_pct": drift * 100
            }
        return None
    
    def check_feature_drift(self, current_features, threshold=0.1):
        """PSI (Population Stability Index) for feature drift."""
        psi_scores = {}
        
        for feature_name in current_features.columns:
            current_dist = np.histogram(current_features[feature_name], bins=10)[0]
            baseline_dist = self.baseline_stats["feature_distributions"][feature_name]
            
            # Normalize
            current_dist = current_dist / current_dist.sum() + 1e-6
            baseline_dist = baseline_dist / baseline_dist.sum() + 1e-6
            
            # PSI calculation
            psi = np.sum(
                (current_dist - baseline_dist) * np.log(current_dist / baseline_dist)
            )
            psi_scores[feature_name] = psi
        
        # Flag features with significant drift
        drifted = {k: v for k, v in psi_scores.items() if v > threshold}
        return drifted if drifted else None
    
    def check_latency_degradation(self, target_p99_ms=5.0):
        """Alert if model latency exceeds SLA."""
        recent_latencies = [p.latency_ms for p in list(self.predictions)[-1000:]]
        p99 = np.percentile(recent_latencies, 99)
        
        if p99 > target_p99_ms:
            return {
                "alert": "LATENCY_SLA_BREACH",
                "current_p99_ms": p99,
                "target_p99_ms": target_p99_ms
            }
        return None
```

#### **Drift Detection Strategies:**

| Method | Detects | Latency | Use Case |
|--------|---------|---------|----------|
| **PSI (Population Stability Index)** | Feature distribution shift | Low | Feature monitoring |
| **KS Test** | Distribution shape change | Medium | Statistical rigor |
| **ADWIN** | Concept drift (streaming) | Very Low | Real-time detection |
| **DDM (Drift Detection Method)** | Error rate increase | Low | Accuracy monitoring |
| **Page-Hinkley** | Mean shift detection | Very Low | Score monitoring |

---

### **9.3 A/B Testing & Shadow Scoring**

#### **Shadow Mode Deployment:**

```python
class ShadowScorer:
    """
    Run new model in shadow mode alongside production model.
    Compare outputs without affecting decisions.
    """
    
    def __init__(self, production_model, shadow_model, sample_rate=0.1):
        self.production = production_model
        self.shadow = shadow_model
        self.sample_rate = sample_rate
        self.comparison_log = []
    
    async def score(self, features):
        # Always run production model (this drives decisions)
        prod_score = self.production.predict(features)
        
        # Conditionally run shadow model (non-blocking)
        if random.random() < self.sample_rate:
            asyncio.create_task(self._shadow_score(features, prod_score))
        
        return prod_score  # Return production result immediately
    
    async def _shadow_score(self, features, prod_score):
        """Score with shadow model and log comparison."""
        shadow_score = self.shadow.predict(features)
        
        self.comparison_log.append({
            "timestamp": time.time(),
            "prod_score": prod_score,
            "shadow_score": shadow_score,
            "abs_diff": abs(prod_score - shadow_score),
            "agreement": (prod_score > 0.5) == (shadow_score > 0.5)
        })
    
    def get_comparison_report(self):
        """Analyze shadow vs. production agreement."""
        if not self.comparison_log:
            return None
        
        agreements = [r["agreement"] for r in self.comparison_log]
        diffs = [r["abs_diff"] for r in self.comparison_log]
        
        return {
            "agreement_rate": np.mean(agreements),
            "mean_score_diff": np.mean(diffs),
            "p95_score_diff": np.percentile(diffs, 95),
            "total_comparisons": len(self.comparison_log)
        }
```

---

### **9.4 Feature Importance & Model Explainability at Serving Time**

#### **Real-Time SHAP (Approximate):**

```python
class FastExplainer:
    """
    Approximate SHAP values for real-time explanation.
    Full SHAP is too slow (100ms+), use TreeSHAP or approximation.
    """
    
    def __init__(self, model, background_data):
        # Pre-compute TreeSHAP explainer (one-time cost)
        import shap
        self.explainer = shap.TreeExplainer(model, data=background_data)
        
        # Cache top-K feature contributions
        self.feature_names = model.feature_names_
    
    def explain(self, features, top_k=5):
        """Return top-K contributing features for a prediction."""
        
        # TreeSHAP is O(T * L * D) - fast for tree models
        shap_values = self.explainer.shap_values(features.reshape(1, -1))
        
        # Get top-K features by absolute SHAP value
        abs_shap = np.abs(shap_values[0])
        top_indices = np.argsort(abs_shap)[-top_k:][::-1]
        
        explanations = []
        for idx in top_indices:
            explanations.append({
                "feature": self.feature_names[idx],
                "contribution": float(shap_values[0][idx]),
                "value": float(features[idx])
            })
        
        return explanations
```

**Latency:**
- Full KernelSHAP: 100–500ms (too slow for real-time)
- TreeSHAP: 1–5ms (suitable for inline scoring)
- Pre-computed lookup: < 0.1ms (for rule-based explanations)

---

### **9.5 Model Fallback & Graceful Degradation**

#### **Circuit Breaker Pattern:**

```python
import time
from enum import Enum

class CircuitState(Enum):
    CLOSED = "closed"      # Normal operation
    OPEN = "open"          # Failing, use fallback
    HALF_OPEN = "half_open"  # Testing recovery

class ModelCircuitBreaker:
    """
    Circuit breaker for model inference.
    Falls back to simpler model if primary fails.
    """
    
    def __init__(self, primary_model, fallback_model,
                 failure_threshold=5, recovery_timeout_sec=30):
        self.primary = primary_model
        self.fallback = fallback_model
        self.state = CircuitState.CLOSED
        self.failure_count = 0
        self.failure_threshold = failure_threshold
        self.recovery_timeout = recovery_timeout_sec
        self.last_failure_time = 0
    
    def predict(self, features):
        if self.state == CircuitState.OPEN:
            # Check if recovery timeout elapsed
            if time.time() - self.last_failure_time > self.recovery_timeout:
                self.state = CircuitState.HALF_OPEN
            else:
                return self._fallback_predict(features)
        
        try:
            # Try primary model
            start = time.perf_counter()
            result = self.primary.predict(features)
            latency = (time.perf_counter() - start) * 1000
            
            # Check latency SLA
            if latency > 10:  # > 10ms = too slow
                raise TimeoutError(f"Model too slow: {latency:.1f}ms")
            
            # Success: reset failures
            self.failure_count = 0
            self.state = CircuitState.CLOSED
            return result
            
        except Exception as e:
            self.failure_count += 1
            self.last_failure_time = time.time()
            
            if self.failure_count >= self.failure_threshold:
                self.state = CircuitState.OPEN
            
            return self._fallback_predict(features)
    
    def _fallback_predict(self, features):
        """Use simpler, faster fallback model."""
        # Fallback: logistic regression (always < 0.1ms)
        return self.fallback.predict(features)
```

#### **Multi-Level Fallback:**

```python
class GracefulDegradation:
    """
    Progressive fallback strategy for scoring services.
    
    Level 0: Full ensemble (XGBoost + Neural + Rules) — 3ms
    Level 1: Fast model only (XGBoost) — 0.5ms  
    Level 2: Logistic Regression — 0.1ms
    Level 3: Rule-based scoring — 0.01ms
    Level 4: Default decision (approve/deny based on amount) — 0ms
    """
    
    def __init__(self):
        self.levels = [
            ("full_ensemble", FullEnsembleModel()),
            ("fast_model", XGBoostModel()),
            ("simple_model", LogisticRegressionModel()),
            ("rules", RuleBasedScorer()),
            ("default", DefaultDecision()),
        ]
        self.current_level = 0
    
    def predict(self, features, max_latency_ms=10):
        for level_name, model in self.levels[self.current_level:]:
            try:
                start = time.perf_counter()
                result = model.predict(features)
                latency = (time.perf_counter() - start) * 1000
                
                if latency <= max_latency_ms:
                    return result, level_name
                    
            except Exception:
                continue  # Try next level
        
        # Last resort
        return self.levels[-1][1].predict(features), "default"
```

---

### **9.6 Load Testing & Capacity Planning**

#### **Inference Load Testing Framework:**

```python
import asyncio
import aiohttp
import time
import numpy as np
from dataclasses import dataclass

@dataclass
class LoadTestResult:
    total_requests: int
    successful: int
    failed: int
    mean_latency_ms: float
    p50_latency_ms: float
    p95_latency_ms: float
    p99_latency_ms: float
    throughput_rps: float
    error_rate: float

class InferenceLoadTester:
    """
    Load test a scoring service to determine capacity limits.
    """
    
    def __init__(self, endpoint_url, num_features=50):
        self.endpoint = endpoint_url
        self.num_features = num_features
    
    async def run_load_test(self, target_rps, duration_sec=60):
        """Run constant-rate load test."""
        interval = 1.0 / target_rps
        latencies = []
        errors = 0
        start_time = time.time()
        
        async with aiohttp.ClientSession() as session:
            tasks = []
            
            while time.time() - start_time < duration_sec:
                features = np.random.randn(self.num_features).tolist()
                task = asyncio.create_task(
                    self._send_request(session, features, latencies)
                )
                tasks.append(task)
                await asyncio.sleep(interval)
            
            # Wait for all in-flight requests
            results = await asyncio.gather(*tasks, return_exceptions=True)
            errors = sum(1 for r in results if isinstance(r, Exception))
        
        elapsed = time.time() - start_time
        
        return LoadTestResult(
            total_requests=len(latencies) + errors,
            successful=len(latencies),
            failed=errors,
            mean_latency_ms=np.mean(latencies) if latencies else 0,
            p50_latency_ms=np.percentile(latencies, 50) if latencies else 0,
            p95_latency_ms=np.percentile(latencies, 95) if latencies else 0,
            p99_latency_ms=np.percentile(latencies, 99) if latencies else 0,
            throughput_rps=len(latencies) / elapsed,
            error_rate=errors / (len(latencies) + errors)
        )
    
    async def find_breaking_point(self):
        """Find maximum RPS before SLA breach."""
        for target_rps in [100, 500, 1000, 5000, 10000, 50000, 100000]:
            result = await self.run_load_test(target_rps, duration_sec=30)
            
            print(f"  {target_rps:>6} RPS → p99={result.p99_latency_ms:.1f}ms, "
                  f"errors={result.error_rate:.1%}")
            
            if result.p99_latency_ms > 10 or result.error_rate > 0.01:
                print(f"  Breaking point: ~{target_rps} RPS")
                return target_rps
        
        return 100000  # Didn't break
```

#### **Capacity Planning Formula:**

```
Required Instances = (Peak TPS × Safety Factor) / (Instance Capacity)

Where:
- Peak TPS: Maximum expected transactions per second
- Safety Factor: 1.5–2.0× (headroom for bursts)
- Instance Capacity: Max RPS per instance at p99 < SLA

Example (Fraud Detection):
- Peak TPS: 50,000
- Safety Factor: 2.0
- Instance Capacity: 10,000 RPS (measured via load test)
- Required Instances = (50,000 × 2.0) / 10,000 = 10 instances
```

---

### **9.7 Model Serialization & Versioning Best Practices**

#### **Model Artifact Management:**

```python
class ModelArtifact:
    """
    Standard model artifact format for production deployment.
    """
    
    def __init__(self, model, metadata):
        self.model = model
        self.metadata = {
            "model_id": str(uuid.uuid4()),
            "model_type": type(model).__name__,
            "version": metadata["version"],
            "trained_at": datetime.utcnow().isoformat(),
            "features": metadata["features"],
            "feature_count": len(metadata["features"]),
            "metrics": metadata["metrics"],  # AUC, precision, recall
            "thresholds": metadata["thresholds"],
            "min_latency_ms": None,  # Filled during validation
            "max_batch_size": None,
        }
    
    def save(self, path):
        """Save model + metadata together."""
        os.makedirs(path, exist_ok=True)
        
        # Save ONNX model
        onnx_path = os.path.join(path, "model.onnx")
        self.export_to_onnx(onnx_path)
        
        # Save metadata
        meta_path = os.path.join(path, "metadata.json")
        with open(meta_path, 'w') as f:
            json.dump(self.metadata, f, indent=2)
        
        # Save feature schema (for validation)
        schema_path = os.path.join(path, "feature_schema.json")
        self.save_feature_schema(schema_path)
    
    def validate_before_deploy(self, test_data, latency_target_ms=5.0):
        """Gate: model must pass validation before production."""
        
        # 1. Accuracy check
        predictions = self.model.predict(test_data.features)
        auc = roc_auc_score(test_data.labels, predictions)
        assert auc >= self.metadata["metrics"]["min_auc"], \
            f"AUC {auc:.4f} below threshold {self.metadata['metrics']['min_auc']}"
        
        # 2. Latency check
        latencies = []
        for i in range(1000):
            start = time.perf_counter()
            self.model.predict(test_data.features[i:i+1])
            latencies.append((time.perf_counter() - start) * 1000)
        
        p99 = np.percentile(latencies, 99)
        assert p99 < latency_target_ms, \
            f"p99 latency {p99:.2f}ms exceeds target {latency_target_ms}ms"
        
        # 3. Feature schema compatibility
        assert set(test_data.feature_names) == set(self.metadata["features"]), \
            "Feature schema mismatch"
        
        return True
```

---

### **9.8 Production Deployment Patterns**

#### **Blue-Green Deployment for ML Models:**

```python
class BlueGreenDeployment:
    """
    Zero-downtime model deployment with instant rollback.
    """
    
    def __init__(self):
        self.blue_model = None   # Current production
        self.green_model = None  # New version (staging)
        self.active = "blue"
    
    def deploy_new_version(self, new_model):
        """Deploy new model to green slot."""
        self.green_model = new_model
        
        # Run validation
        if not self.validate_green():
            self.green_model = None
            raise ValueError("Green model failed validation")
    
    def switch_traffic(self):
        """Atomically switch traffic to green."""
        self.active = "green"
        # Old blue can be rolled back to instantly
    
    def rollback(self):
        """Instant rollback to blue."""
        self.active = "blue"
    
    def predict(self, features):
        if self.active == "blue":
            return self.blue_model.predict(features)
        else:
            return self.green_model.predict(features)
```

#### **Feature Flag Controlled Rollout:**

```python
class FeatureFlaggedModel:
    """
    Gradual rollout controlled by feature flags.
    Useful for observing new model behavior in production.
    """
    
    def __init__(self, old_model, new_model, flag_service):
        self.old_model = old_model
        self.new_model = new_model
        self.flag_service = flag_service
    
    def predict(self, features, user_id=None):
        # Check if user is in rollout group
        if self.flag_service.is_enabled("new_fraud_model_v2", user_id=user_id):
            return self.new_model.predict(features)
        else:
            return self.old_model.predict(features)
```

---

### **9.9 Batch Inference at Scale (Offline)**

#### **Distributed Batch Scoring with Spark:**

```python
from pyspark.sql import SparkSession
from pyspark.sql.functions import pandas_udf
import pandas as pd

class DistributedBatchScorer:
    """
    Score millions of records using Spark + ONNX.
    Use for: daily risk assessments, batch fraud review, model monitoring.
    """
    
    def __init__(self, model_path, spark_config=None):
        self.spark = SparkSession.builder \
            .appName("batch_scoring") \
            .config("spark.executor.instances", "50") \
            .config("spark.executor.memory", "8g") \
            .config("spark.executor.cores", "4") \
            .getOrCreate()
        
        self.model_path = model_path
    
    def score_dataset(self, input_path, output_path):
        """Score entire dataset in parallel across cluster."""
        
        df = self.spark.read.parquet(input_path)
        
        # Broadcast model to all executors (loaded once per executor)
        model_broadcast = self.spark.sparkContext.broadcast(self.model_path)
        
        @pandas_udf("float")
        def score_batch(features_series: pd.Series) -> pd.Series:
            import onnxruntime as ort
            
            # Load model (cached per executor via broadcast)
            session = ort.InferenceSession(model_broadcast.value)
            
            # Convert to numpy and predict
            features = np.stack(features_series.values)
            scores = session.run(None, {"features": features})[0]
            
            return pd.Series(scores.flatten())
        
        # Apply scoring UDF
        scored_df = df.withColumn("fraud_score", score_batch(df["features"]))
        
        # Write results
        scored_df.write.parquet(output_path)
        
        return scored_df.count()
```

**Performance:**
- 100M records on 50 executors: ~10 minutes
- Throughput: ~170K records/second
- Cost: ~$5 on cloud (spot instances)

---

### **9.10 Interview-Ready Traditional ML Patterns Summary**

#### **Quick Reference Card:**

| Pattern | When to Use | Latency | Example |
|---------|-------------|---------|---------|
| **Rule → Fast → Deep cascade** | High-volume, mixed difficulty | 0.1–3ms | Fraud detection |
| **ONNX + Process Pool** | CPU inference at scale | 0.5ms | Payment scoring |
| **Feature Store + Parallel Fetch** | Complex features needed | 2–3ms | Risk assessment |
| **ANN Index (FAISS/ScaNN)** | Similarity search | 0.5–5ms | Recommendations |
| **Circuit Breaker + Fallback** | High availability required | 0.1–10ms | Any critical path |
| **Shadow Scoring** | Safe model validation | 0 (async) | Model upgrades |
| **Treelite Compilation** | Max tree model speed | 0.05–0.1ms | Latency-critical |
| **Blue-Green + Canary** | Zero-downtime deploys | 0 | Any model update |

#### **Key Metrics for Traditional ML Serving:**

```
┌─────────────────────────────────────────────────────────┐
│  Traditional ML Inference Metrics Dashboard             │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Latency:                                               │
│    • p50: 0.3ms  │  p95: 1.2ms  │  p99: 3.5ms        │
│                                                         │
│  Throughput:                                            │
│    • Current: 45,000 TPS  │  Capacity: 100,000 TPS    │
│                                                         │
│  Model Health:                                          │
│    • AUC: 0.943  │  PSI: 0.02  │  Drift: None        │
│                                                         │
│  Operational:                                           │
│    • Error Rate: 0.001%  │  Circuit: CLOSED            │
│    • Cache Hit: 85%  │  Fallback Rate: 0.1%           │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## Module 10: Hands-On Labs

### **Lab 1: Deploy vLLM and Benchmark**

**Objective:** Deploy Llama-2-7B with vLLM and compare with HuggingFace.

**Steps:**
1. Install vLLM
2. Deploy model with vLLM
3. Benchmark throughput vs. HuggingFace
4. Measure p99 latency
5. Analyze GPU utilization

**Expected Results:**
- vLLM: 100+ tokens/sec, p99 < 500ms
- HuggingFace: 20 tokens/sec, p99 > 2000ms
- **5× throughput improvement**

---

### **Lab 2: Implement Dynamic Batcher**

**Objective:** Build a dynamic batcher with configurable wait time.

**Steps:**
1. Implement DynamicBatcher class
2. Test with variable-length requests
3. Measure throughput vs. static batching
4. Tune max_wait_ms parameter
5. Analyze latency distribution

**Expected Results:**
- Optimal max_wait_ms: 5–20ms
- **2× throughput improvement** vs. static batching
- p99 latency: < 100ms

---

### **Lab 3: Build KV-Cache with Session Stickiness**

**Objective:** Implement session-aware KV-cache routing.

**Steps:**
1. Implement PagedKVCache class
2. Add session routing logic
3. Deploy 2+ model instances
4. Test with multi-turn conversations
5. Measure cache hit rate

**Expected Results:**
- Cache hit rate: > 70%
- Latency reduction: 400ms per session
- **3× memory efficiency**

---

### **Lab 4: Build a Fraud Scoring Pipeline**

**Objective:** Implement a multi-stage fraud scoring service with cascade, feature store, and fallback.

**Steps:**
1. Train XGBoost + Logistic Regression models on synthetic fraud data
2. Export both to ONNX format
3. Implement cascade scoring (rules → XGBoost → fallback to LR)
4. Add circuit breaker with automatic fallback
5. Load test and measure throughput/latency at each stage
6. Implement shadow scoring for model comparison

**Expected Results:**
- XGBoost (ONNX): < 0.5ms p99 latency
- Cascade: 60% resolved by rules (< 0.1ms)
- Throughput: > 50,000 TPS per instance
- Circuit breaker triggers fallback within 5 failures

---

### **Lab 5: Feature Store + Real-Time Scoring**

**Objective:** Build end-to-end feature pipeline with parallel feature fetching and scoring.

**Steps:**
1. Set up Redis as online feature store
2. Implement parallel async feature fetching
3. Pre-compute windowed aggregation features
4. Build scoring service with process pool
5. Benchmark: sequential vs. parallel feature fetch
6. Measure end-to-end latency breakdown

**Expected Results:**
- Parallel feature fetch: 2ms (vs. 8ms sequential)
- End-to-end scoring: < 5ms p99
- Feature store hit rate: > 95%

---

### **Lab 6: Model Drift Detection & Monitoring**

**Objective:** Build real-time model monitoring with drift detection and alerting.

**Steps:**
1. Generate baseline statistics from training data
2. Simulate feature drift (shift distributions)
3. Implement PSI-based feature drift detection
4. Implement score distribution monitoring
5. Build latency degradation alerting
6. Create dashboard metrics

**Expected Results:**
- PSI detects feature drift within 1000 samples
- Score drift alert triggers within 5% shift
- Latency alert triggers within 2× SLA breach

---

## Interview Question Bank

### **Fundamentals:**

1. Explain the difference between TTFT and TPOT. Which is more important for chat applications?
2. Why is inference memory-bound for small batches?
3. How does KV-cache reduce inference latency from O(n²) to O(n)?
4. What is the computational complexity of self-attention? Why?

### **vLLM & PagedAttention:**

5. How does vLLM's PagedAttention work? What problem does it solve?
6. Compare continuous batching vs. static batching. When would you use each?
7. How does vLLM handle GPU memory overflow?
8. What is block sharing in PagedAttention? Give an example.

### **Batching:**

9. Explain the trade-off between batch size and latency.
10. How does dynamic batching reduce padding waste?
11. What is micro-batching? When is it useful?
12. How would you implement request prioritization in a batcher?

### **Multi-Model Orchestration:**

13. Describe a cascade model architecture. When is it beneficial?
14. How do you handle dependencies in a multi-model pipeline?
15. What is early exit? How does it improve latency?
16. How would you implement canary deployments for models?

### **KV-Cache:**

17. Compare LRU vs. LFU cache eviction policies for LLM serving.
18. How does prefix caching work? What's the use case?
19. How would you manage KV-cache for multi-turn conversations?
20. What is session stickiness? Why is it important?

### **Optimization:**

21. Explain speculative decoding. When does it help?
22. What is FlashAttention? How does it reduce memory accesses?
23. Compare INT8 vs. FP16 quantization. Trade-offs?
24. How do you reduce token usage for large context workflows?

### **Traditional ML Inference:**

25. How do you achieve sub-millisecond inference for tree models in production?
26. Describe the cascade scoring pattern. What percentage of requests should the fast path handle?
27. How does a feature store architecture differ for online vs. offline serving?
28. What is PSI (Population Stability Index)? How do you detect model drift in production?
29. Explain the circuit breaker pattern for model inference. When does it trigger?
30. How would you design a fraud scoring system handling 100K+ TPS with < 10ms latency?
31. Compare ONNX Runtime vs. Treelite for tree model inference. When would you use each?
32. How do you handle model fallback and graceful degradation under load?
33. What CPU optimization techniques improve ML inference throughput? (NUMA, SIMD, cache alignment)
34. Describe blue-green deployment for ML models. How do you ensure zero-downtime rollout?
35. How does shadow scoring work? Why is it safer than A/B testing for model validation?
36. What are the key differences between real-time scoring and batch scoring architectures?

---

## Resource Library

### **Papers:**

1. **vLLM:** "vLLM: Easy, Fast, and Cheap LLM Serving with PagedAttention" (2023)
2. **FlashAttention:** "FlashAttention: Fast and Memory-Efficient Exact Attention" (2022)
3. **Speculative Decoding:** "Speculative Decoding" (Chen et al., 2023)
4. **TensorRT-LLM:** NVIDIA Technical Blog (2023)
5. **Treelite:** "Treelite: Toolbox for Decision Tree Deployment" (2018)
6. **SHAP:** "A Unified Approach to Interpreting Model Predictions" (Lundberg & Lee, 2017)

### **Documentation:**

1. **vLLM:** https://vllm.readthedocs.io
2. **TensorRT-LLM:** https://nvidia.github.io/TensorRT-LLM
3. **Triton:** https://triton-inference-server.readthedocs.io
4. **ONNX Runtime:** https://onnxruntime.ai
5. **Feast (Feature Store):** https://feast.dev
6. **FAISS:** https://faiss.ai
7. **Treelite:** https://treelite.readthedocs.io
8. **BentoML:** https://docs.bentoml.com

### **Videos:**

1. **vLLM Deep Dive:** NVIDIA GTC 2024
2. **LLM Serving Systems:** Stanford CS329S
3. **CUDA Optimization:** NVIDIA GTC talks
4. **FlashAttention:** Author talk (Tri Dao)
5. **ML Systems at Scale:** Stanford CS329S (Full Course)
6. **Feature Stores in Production:** Tecton/Feast talks

### **Blogs:**

1. **Anyscale:** "Optimizing LLM Serving"
2. **Hugging Face:** "LLM Inference Optimization"
3. **Fireworks AI:** "LLM Inference Optimization Techniques"
4. **Modal:** "LLM Performance Guide"
5. **Uber Engineering:** "Michelangelo ML Platform"
6. **Netflix Tech Blog:** "ML Infrastructure at Scale"
7. **Stripe Engineering:** "Fraud Detection ML Systems"
8. **DoorDash Engineering:** "Real-Time ML Prediction Service"

---

## Progress Tracker

### **Weekly Goals:**

| Week | Module | Hours | Completion | Notes |
|------|--------|-------|------------|-------|
| 1 | Module 1: Fundamentals | 8 | ☐ | |
| 2 | Module 2: Inference Engines | 10 | ☐ | |
| 3 | Module 3: Batching | 8 | ☐ | |
| 4 | Module 4: Orchestration | 8 | ☐ | |
| 5 | Module 5: KV-Cache | 8 | ☐ | |
| 6 | Module 6-7: Advanced LLM | 8 | ☐ | |
| 7 | Module 8: Traditional ML Inference | 10 | ☐ | |
| 8 | Module 9: Traditional ML Production | 10 | ☐ | |

### **Hands-On Labs:**

| Lab | Status | Date Completed | Notes |
|-----|--------|----------------|-------|
| Lab 1: vLLM Deployment | ☐ | | |
| Lab 2: Dynamic Batcher | ☐ | | |
| Lab 3: KV-Cache | ☐ | | |
| Lab 4: Fraud Scoring Pipeline | ☐ | | |
| Lab 5: Feature Store + Scoring | ☐ | | |
| Lab 6: Drift Detection | ☐ | | |

### **Confidence Self-Assessment:**

| Topic | Before (1-10) | After (1-10) | Improvement |
|-------|---------------|--------------|-------------|
| Inference Fundamentals | | | |
| vLLM & PagedAttention | | | |
| Batching Strategies | | | |
| Multi-Model Orchestration | | | |
| KV-Cache Management | | | |
| Prompt Optimization | | | |
| Advanced LLM Techniques | | | |
| Traditional ML Inference | | | |
| ML Production Patterns | | | |

---

**Good luck with your training! 🚀**

**Remember:** The goal is **30% depth** across all topics. For deeper expertise, refer to individual papers and documentation linked in the Resource Library.

**You've got this!** 💪
