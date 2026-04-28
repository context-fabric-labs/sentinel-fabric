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
9. [Module 8: Hands-On Labs](#module-8-hands-on-labs)
10. [Interview Question Bank](#interview-question-bank)
11. [Resource Library](#resource-library)
12. [Progress Tracker](#progress-tracker)

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
- ✅ Answer **interview questions** with depth and real-world examples

### **Training Duration:**
- **Total Hours:** 40–50 hours
- **Duration:** 4–6 weeks (part-time)
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

## Module 8: Hands-On Labs

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

---

## Resource Library

### **Papers:**

1. **vLLM:** "vLLM: Easy, Fast, and Cheap LLM Serving with PagedAttention" (2023)
2. **FlashAttention:** "FlashAttention: Fast and Memory-Efficient Exact Attention" (2022)
3. **Speculative Decoding:** "Speculative Decoding" (Chen et al., 2023)
4. **TensorRT-LLM:** NVIDIA Technical Blog (2023)

### **Documentation:**

1. **vLLM:** https://vllm.readthedocs.io
2. **TensorRT-LLM:** https://nvidia.github.io/TensorRT-LLM
3. **Triton:** https://triton-inference-server.readthedocs.io
4. **ONNX Runtime:** https://onnxruntime.ai

### **Videos:**

1. **vLLM Deep Dive:** NVIDIA GTC 2024
2. **LLM Serving Systems:** Stanford CS329S
3. **CUDA Optimization:** NVIDIA GTC talks
4. **FlashAttention:** Author talk (Tri Dao)

### **Blogs:**

1. **Anyscale:** "Optimizing LLM Serving"
2. **Hugging Face:** "LLM Inference Optimization"
3. **Fireworks AI:** "LLM Inference Optimization Techniques"
4. **Modal:** "LLM Performance Guide"

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
| 6 | Module 6-7: Advanced | 8 | ☐ | |

### **Hands-On Labs:**

| Lab | Status | Date Completed | Notes |
|-----|--------|----------------|-------|
| Lab 1: vLLM Deployment | ☐ | | |
| Lab 2: Dynamic Batcher | ☐ | | |
| Lab 3: KV-Cache | ☐ | | |

### **Confidence Self-Assessment:**

| Topic | Before (1-10) | After (1-10) | Improvement |
|-------|---------------|--------------|-------------|
| Inference Fundamentals | | | |
| vLLM & PagedAttention | | | |
| Batching Strategies | | | |
| Multi-Model Orchestration | | | |
| KV-Cache Management | | | |
| Prompt Optimization | | | |
| Advanced Techniques | | | |

---

**Good luck with your training! 🚀**

**Remember:** The goal is **30% depth** across all topics. For deeper expertise, refer to individual papers and documentation linked in the Resource Library.

**You've got this!** 💪
