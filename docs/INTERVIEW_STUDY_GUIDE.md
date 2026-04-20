# AI Systems Engineering Interview Study Guide
## Comprehensive Preparation for Staff/Principal Engineer Roles

**Based on Four Major Projects:** Broadcom Cloud SWG (2015–2021), Apple Siri (2021–2023), CapitalOne Fraud (2024–2025), Fiserv Loan Director (2026–Present)

---

## Table of Contents

1. [Study Guide Overview](#study-guide-overview)
2. [Priority Legend & Time Allocation](#priority-legend--time-allocation)
3. [Phase 1: Foundational Systems Concepts (Weeks 1-2)](#phase-1-foundational-systems-concepts)
4. [Phase 2: Programming Languages Deep Dive (Weeks 3-4)](#phase-2-programming-languages-deep-dive)
5. [Phase 3: AI/ML Infrastructure (Weeks 5-7)](#phase-3-ai-ml-infrastructure)
6. [Phase 4: GPU & CUDA Optimization (Weeks 8-9)](#phase-4-gpu--cuda-optimization)
7. [Phase 5: Distributed Systems & Orchestration (Weeks 10-11)](#phase-5-distributed-systems--orchestration)
8. [Phase 6: Zero-Copy & Performance Optimization (Week 12)](#phase-6-zero-copy--performance-optimization)
9. [Phase 7: Project-Specific Deep Dives (Weeks 13-14)](#phase-7-project-specific-deep-dives)
10. [Phase 8: System Design & Architecture (Week 15)](#phase-8-system-design--architecture)
11. [Phase 9: Behavioral & Leadership (Week 16)](#phase-9-behavioral--leadership)
12. [Mock Interview Schedule](#mock-interview-schedule)
13. [Resource Library](#resource-library)
14. [Progress Tracker](#progress-tracker)

---

## Study Guide Overview

### **Target Roles:**
- Sr. Staff / Principal / Distinguished Engineer
- AI Infrastructure Engineer
- GPU/Inference Platform Engineer
- HPC Systems Engineer
- Low-Latency Distributed Systems Engineer

### **Target Companies:**
- **FAANG+:** Google, Meta, Amazon, Apple, Microsoft, NVIDIA
- **AI Infrastructure:** OpenAI, Anthropic, Cohere, Hugging Face, Databricks
- **FinTech:** Stripe, PayPal, Square, Coinbase, Plaid
- **Cybersecurity:** Palo Alto Networks, CrowdStrike, Zscaler, Cloudflare

### **Total Study Duration:** 16 Weeks (4 Months)
- **Weekday Study:** 2–3 hours/day
- **Weekend Study:** 6–8 hours/day
- **Total Hours:** ~350–400 hours

### **Study Approach:**
1. **Foundational First:** Build strong base before advanced topics
2. **Project-Backed Learning:** Every concept tied to real project experience
3. **Active Recall:** Practice questions after each section
4. **Mock Interviews:** Weekly starting Week 8
5. **Iterative Review:** Revisit earlier topics with new context

---

## Priority Legend & Time Allocation

| Priority | Description | Time Allocation | Mastery Level Required |
|----------|-------------|-----------------|----------------------|
| **P0 - Critical** | Must know cold, will be tested deeply | 50% of study time | Expert (can teach others) |
| **P1 - Important** | Should know well, may be tested | 35% of study time | Advanced (can implement) |
| **P2 - Nice to Have** | Good to know, surface-level questions | 15% of study time | Proficient (can discuss) |

### **Topic Weighting by Interview Type:**

| Interview Type | Systems Design | Coding | AI/ML Infra | Behavioral |
|----------------|----------------|--------|-------------|------------|
| **FAANG+ Staff/Principal** | 35% | 25% | 25% | 15% |
| **AI Infrastructure Companies** | 25% | 20% | 40% | 15% |
| **FinTech (Stripe, PayPal)** | 30% | 30% | 25% | 15% |
| **Cybersecurity (Palo Alto, CrowdStrike)** | 30% | 25% | 30% | 15% |

---

## Phase 1: Foundational Systems Concepts (Weeks 1-2)

### **Priority: P0 - Critical**
### **Time Allocation: 50 hours**

### **Week 1: Memory & CPU Architecture**

#### **Topics:**
1. **Memory Hierarchy** (P0)
   - L1/L2/L3 cache, cache lines (64 bytes), cache coherence
   - NUMA architecture, memory affinity, huge pages (2MB, 1GB)
   - Virtual memory, page tables, TLB misses
   - **Project Connection:** Broadcom (cacheline-aligned feature blocks), CapitalOne (per-core arenas)

2. **CPU Architecture** (P0)
   - Superscalar execution, branch prediction, instruction pipelining
   - SIMD (SSE, AVX2, AVX-512, NEON), vectorization
   - Hyperthreading, core affinity, context switching
   - **Project Connection:** Apple (NEON SIMD 4× speedup), Broadcom (AVX-512 for CPU inference)

3. **Memory Allocation Patterns** (P0)
   - Stack vs. heap, arena/region allocators, slab allocators
   - Memory pools, object pools, lock-free allocators
   - False sharing, cacheline padding, alignment
   - **Project Connection:** CapitalOne (per-core arena with bulk reset), Apple (shared memory arena)

#### **Study Resources:**
- Book: "Computer Systems: A Programmer's Perspective" (Chapters 6, 9)
- Book: "Effective Modern C++" (Items 16, 40)
- Article: "What Every Programmer Should Know About Memory" (Ulrich Drepper)
- Video: "CPU Caches and Why You Care" (CppCon 2014)

#### **Practice Questions:**
1. Explain the cost of a cache miss vs. L1 cache hit. How does this affect your data structure design?
2. What is false sharing? How do you detect and prevent it?
3. When would you use an arena allocator vs. standard malloc/free?
4. How does NUMA affect multi-threaded performance? How do you optimize for it?
5. What are huge pages? When should you use them?

#### **Hands-On Exercises:**
1. **Cache Optimization:** Take a matrix multiplication implementation and optimize for cache locality. Measure speedup.
2. **Arena Allocator:** Implement a simple arena allocator in C++. Compare performance vs. malloc/free for 1M allocations.
3. **False Sharing Demo:** Create a multi-threaded counter with and without false sharing. Measure the difference.

---

### **Week 2: Concurrency & Parallelism**

#### **Topics:**
1. **Lock-Free Programming** (P0)
   - Atomic operations, memory ordering (relaxed, acquire, release, seq_cst)
   - Lock-free data structures: SPSC queues, MPMC queues, ring buffers
   - ABA problem, hazard pointers, RCU (Read-Copy-Update)
   - **Project Connection:** CapitalOne (SPSC ring buffers with atomic indices), Broadcom (lock-free telemetry pipelines)

2. **Threading Models** (P0)
   - Thread pools, work stealing, producer-consumer patterns
   - Thread affinity, core pinning, scheduler behavior
   - Async/await (Rust, Python), coroutines (C++20)
   - **Project Connection:** Fiserv (Tokio async runtime), Apple (gRPC streaming threads)

3. **Synchronization Primitives** (P1)
   - Mutexes, rwlocks, semaphores, condition variables
   - Spinlocks, ticket locks, MCS locks
   - Lock contention, priority inversion, deadlock detection
   - **Project Connection:** Broadcom (tenant GPU memory pools with mutexes)

#### **Study Resources:**
- Book: "C++ Concurrency in Action" (Chapters 5-7)
- Book: "Rust in Action" (Chapter 9: Concurrent Programming)
- Article: "Lock-Free Programming" (1024cores.net)
- Video: "Lock-Free Programming" (CppCon 2014)

#### **Practice Questions:**
1. What are the different memory orderings in C++ atomics? When would you use `memory_order_relaxed` vs. `memory_order_seq_cst`?
2. Implement a lock-free SPSC queue. What are the key challenges?
3. What is the ABA problem? How do you solve it?
4. When would you use a spinlock vs. a mutex?
5. How does work stealing improve thread pool performance?

#### **Hands-On Exercises:**
1. **SPSC Queue:** Implement a lock-free SPSC queue in C++ or Rust. Benchmark against a mutex-based queue.
2. **Thread Pool:** Build a thread pool with work stealing. Measure throughput with varying workloads.
3. **Producer-Consumer:** Implement a multi-producer, multi-consumer queue with bounded capacity and backpressure.

---

## Phase 2: Programming Languages Deep Dive (Weeks 3-4)

### **Priority: P0 - Critical**
### **Time Allocation: 60 hours**

### **Week 3: C++ Systems Programming**

#### **Topics:**
1. **Modern C++ (17/20)** (P0)
   - Move semantics, perfect forwarding, RVO/NRVO
   - Smart pointers (unique_ptr, shared_ptr, weak_ptr)
   - Templates, SFINAE, constexpr, concepts (C++20)
   - **Project Connection:** All four projects (primary language for performance-critical paths)

2. **Memory Management** (P0)
   - RAII pattern, custom deleters, allocator-aware containers
   - Placement new, aligned allocation, overaligned types
   - Memory-mapped files, shared memory (POSIX shm, Windows shared memory)
   - **Project Connection:** Apple (shared memory arena), Broadcom (zero-copy request context)

3. **Performance Optimization** (P0)
   - Inline functions, LTO (Link-Time Optimization), PGO (Profile-Guided Optimization)
   - Branch optimization, loop unrolling, vectorization hints
   - Profiling with perf, VTune, Nsight Systems
   - **Project Connection:** Apple (4× NEON speedup), Broadcom (SIMD feature extraction)

#### **Study Resources:**
- Book: "Effective Modern C++" (Scott Meyers)
- Book: "C++ High Performance" (Björn Andrist, Viktor Sehr)
- Website: cppreference.com
- Video: "C++17/20 Features" (CppCon talks)

#### **Practice Questions:**
1. Explain move semantics. When is a move constructor called vs. copy constructor?
2. What is perfect forwarding? How does `std::forward` work?
3. When would you use `std::unique_ptr` vs. `std::shared_ptr`?
4. What is RAII? Give examples from your projects.
5. How do you implement a custom allocator for `std::vector`?

#### **Hands-On Exercises:**
1. **Move Semantics:** Implement a class with move constructor/assignment. Measure performance improvement.
2. **Custom Allocator:** Build a pool allocator for fixed-size objects. Compare with default allocator.
3. **SIMD Optimization:** Take a compute-intensive function and vectorize with AVX2/AVX-512 intrinsics.

---

### **Week 4: Rust & Python**

#### **Rust Systems Programming** (P0)
1. **Ownership & Borrowing** (P0)
   - Ownership rules, borrowing (immutable & mutable), lifetimes
   - Smart pointers (Box, Rc, Arc, RefCell, Mutex)
   - **Project Connection:** Fiserv (control plane with DashMap), CapitalOne (per-core state management)

2. **Async Programming** (P0)
   - Tokio runtime, async/await, futures
   - Channels (mpsc, oneshot, broadcast), synchronization primitives
   - **Project Connection:** Fiserv (Axum web server with Tokio)

3. **Unsafe Rust & FFI** (P1)
   - Raw pointers, unsafe blocks, FFI with C/C++
   - When to use unsafe, safety invariants
   - **Project Connection:** Fiserv (zero-copy shared memory with unsafe)

#### **Python for AI/ML** (P1)
1. **Python Performance** (P1)
   - GIL, multiprocessing vs. threading, async (asyncio)
   - C extensions, Cython, pybind11
   - **Project Connection:** All projects (model training, deployment scripts)

2. **ML Frameworks** (P0)
   - PyTorch: tensors, autograd, nn.Module, DataLoader
   - TensorFlow: graphs, sessions, tf.data
   - **Project Connection:** Fiserv/CapitalOne (PyTorch), Broadcom/Apple (TensorFlow)

#### **Study Resources:**
- Book: "Programming Rust" (Jim Blandy, Jason Orendorff)
- Book: "Rust for Rustaceans" (Jon Gjengset)
- Website: Rust documentation (doc.rust-lang.org)
- Course: "FastAI Practical Deep Learning" (Python/PyTorch)

#### **Practice Questions:**
1. Explain Rust's ownership system. How does it prevent data races?
2. What is the difference between `Rc` and `Arc`? When would you use each?
3. How does Tokio's async runtime work? What is a future?
4. What is Python's GIL? How do you work around it for CPU-bound tasks?
5. Compare PyTorch and TensorFlow. When would you choose one over the other?

#### **Hands-On Exercises:**
1. **Rust Async Service:** Build a simple HTTP server with Axum and Tokio. Add middleware for logging and metrics.
2. **Rust FFI:** Create a Rust library that calls C++ code via FFI. Benchmark the overhead.
3. **PyTorch Model:** Implement a simple neural network in PyTorch. Train it on a sample dataset.

---

## Phase 3: AI/ML Infrastructure (Weeks 5-7)

### **Priority: P0 - Critical**
### **Time Allocation: 75 hours**

### **Week 5: Model Architectures & Inference**

#### **Topics:**
1. **Transformer Models** (P0)
   - Self-attention mechanism, encoder-decoder architecture
   - BERT, DistilBERT, GPT architecture differences
   - KV-cache for autoregressive generation
   - **Project Connection:** Apple (BERT for NLU), Fiserv (70B LLM for reasoning)

2. **CNN & RNN Variants** (P1)
   - Convolutional layers, pooling, residual connections
   - RNN, LSTM, GRU, attention mechanisms
   - Conformer (CNN + Transformer for ASR)
   - **Project Connection:** Broadcom (CNN for malware), Apple (Conformer ASR)

3. **Model Optimization** (P0)
   - Quantization (INT8, FP16), pruning, knowledge distillation
   - Operator fusion, constant folding, layer merging
   - **Project Connection:** Apple (DistilBERT for 97% accuracy with 40% speedup)

#### **Study Resources:**
- Paper: "Attention Is All You Need" (Vaswani et al., 2017)
- Paper: "BERT: Pre-training of Deep Bidirectional Transformers" (Devlin et al., 2018)
- Book: "Deep Learning" (Goodfellow, Bengio, Courville)
- Course: "CS224N: Natural Language Processing with Deep Learning" (Stanford)

#### **Practice Questions:**
1. Explain the self-attention mechanism. What is the computational complexity?
2. What is KV-cache? How does it reduce inference latency for autoregressive models?
3. Compare BERT and GPT architectures. When would you use each?
4. What is knowledge distillation? How does it help with model compression?
5. How does quantization work? What are the trade-offs?

#### **Hands-On Exercises:**
1. **Transformer Implementation:** Implement a simple transformer encoder from scratch in PyTorch.
2. **KV-Cache Optimization:** Add KV-cache to a text generation model. Measure latency improvement.
3. **Quantization:** Quantize a pre-trained model to INT8. Measure accuracy drop and speedup.

---

### **Week 6: Model Serving & Orchestration**

#### **Topics:**
1. **Inference Engines** (P0)
   - vLLM (PagedAttention, continuous batching)
   - TensorRT-LLM (optimization for NVIDIA GPUs)
   - Triton Inference Server (multi-framework support)
   - ONNX Runtime (cross-platform inference)
   - **Project Connection:** Fiserv/CapitalOne (vLLM), Broadcom (ONNX Runtime, TensorFlow Serving)

2. **Batching Strategies** (P0)
   - Static batching, dynamic batching, continuous batching
   - Micro-batching for GPU utilization
   - Request prioritization, preemption
   - **Project Connection:** Broadcom (10× throughput with dynamic batching), CapitalOne (micro-batching for GPU)

3. **Multi-Model Orchestration** (P0)
   - Model composition, ensemble methods
   - Dependency-aware execution (DAG)
   - Early exit, cascading models
   - **Project Connection:** Broadcom (6+ models with dependency DAG), Fiserv (multi-model LLM pipeline)

#### **Study Resources:**
- Paper: "vLLM: Easy, Fast, and Cheap LLM Serving with PagedAttention" (2023)
- Documentation: TensorRT-LLM, Triton Inference Server
- Video: "vLLM Deep Dive" (NVIDIA GTC 2024)
- Blog: "Optimizing LLM Serving" (Anyscale, Hugging Face)

#### **Practice Questions:**
1. How does vLLM's PagedAttention work? What problem does it solve?
2. Compare static batching vs. dynamic batching. When would you use each?
3. What is continuous batching? How does it improve GPU utilization?
4. How do you orchestrate multiple models with dependencies?
5. What is early exit? How does it reduce inference latency?

#### **Hands-On Exercises:**
1. **vLLM Deployment:** Deploy a LLM using vLLM. Benchmark throughput vs. HuggingFace transformers.
2. **Dynamic Batching:** Implement a simple dynamic batcher for a model server. Measure throughput improvement.
3. **Model Ensemble:** Build a two-model cascade (fast filter + slow accurate model). Measure accuracy/latency trade-off.

---

### **Week 7: LLM-Specific Optimization**

#### **Topics:**
1. **KV-Cache Management** (P0)
   - PagedAttention (vLLM), radix attention (FlashAttention)
   - Cache eviction policies (LRU, LFU)
   - Prefix caching, shared KV-cache across sessions
   - **Project Connection:** Fiserv (70% KV-cache hit rate with session stickiness)

2. **Prompt Optimization** (P1)
   - Prompt caching, prompt templates, few-shot learning
   - Context window management, sliding window attention
   - **Project Connection:** Fiserv (60% token reduction with hierarchical context)

3. **Speculative Decoding** (P1)
   - Draft models, verification, acceptance rates
   - Medusa, Lookahead Decoding
   - **Project Connection:** (Emerging tech - good to know for interviews)

#### **Study Resources:**
- Paper: "FlashAttention: Fast and Memory-Efficient Exact Attention" (2022)
- Paper: "Speculative Decoding" (Chen et al., 2023)
- Blog: "LLM Inference Optimization" (Fireworks AI, Modal)
- Video: "LLM Serving Systems" (Stanford CS329S)

#### **Practice Questions:**
1. What is PagedAttention? How does it improve memory utilization?
2. How do you manage KV-cache for multi-turn conversations?
3. What is speculative decoding? When does it help?
4. How do you reduce token usage for large context workflows?
5. What is prefix caching? How does it help with batched inference?

#### **Hands-On Exercises:**
1. **KV-Cache Implementation:** Implement a simple KV-cache for a transformer model. Measure memory savings.
2. **Session Stickiness:** Build a router that routes same session to same model instance. Measure cache hit rate.
3. **Prompt Caching:** Implement prompt prefix caching for repeated prompts. Measure latency improvement.

---

## Phase 4: GPU & CUDA Optimization (Weeks 8-9)

### **Priority: P0 - Critical**
### **Time Allocation: 60 hours**

### **Week 8: CUDA Fundamentals**

#### **Topics:**
1. **GPU Architecture** (P0)
   - SM (Streaming Multiprocessor), warps, threads, blocks
   - Memory hierarchy: global, shared, local, constant, texture
   - Occupancy, warp scheduling, latency hiding
   - **Project Connection:** All projects (NVIDIA T4/V100/A100/H100)

2. **CUDA Programming Model** (P0)
   - Kernel launch configuration (grid, block, thread)
   - Shared memory, synchronization (__syncthreads())
   - Atomic operations, warp primitives (__shfl_sync)
   - **Project Connection:** CapitalOne (custom CUDA kernels for fraud scoring)

3. **Memory Optimization** (P0)
   - Coalesced memory access, memory alignment
   - Shared memory tiling, register usage
   - Pinned memory, zero-copy memory, unified memory
   - **Project Connection:** Fiserv (pinned memory for 3.2× faster H2D transfers)

#### **Study Resources:**
- Book: "Programming Massively Parallel Processors" (Hwu, Kirk)
- Course: "CUDA C Programming" (NVIDIA DLI)
- Documentation: CUDA C Programming Guide
- Video: "CUDA Optimization" (GTC talks)

#### **Practice Questions:**
1. Explain the GPU memory hierarchy. What is the latency of each level?
2. What is warp divergence? How do you minimize it?
3. What is coalesced memory access? Why is it important?
4. When would you use shared memory vs. global memory?
5. What is occupancy? How do you optimize it?

#### **Hands-On Exercises:**
1. **Matrix Multiplication:** Implement tiled matrix multiplication with shared memory. Compare with naive version.
2. **Reduction Kernel:** Implement a parallel reduction kernel. Optimize with warp shuffles.
3. **Memory Coalescing:** Take a non-coalesced kernel and optimize for coalesced access. Measure speedup.

---

### **Week 9: Advanced CUDA Optimization**

#### **Topics:**
1. **CUDA Graphs** (P0)
   - Graph capture, instantiation, launch
   - When to use CUDA Graphs (fixed execution path)
   - Overhead reduction (150µs → 5µs per inference)
   - **Project Connection:** CapitalOne (145µs launch overhead elimination for sub-5ms p99)

2. **Streams & Concurrency** (P0)
   - CUDA streams, asynchronous execution
   - H2D + Compute + D2H overlap
   - Multi-stream concurrency, priority
   - **Project Connection:** Broadcom (4 CUDA streams per worker), Fiserv (async DMA with streams)

3. **Tensor Cores & Mixed Precision** (P1)
   - Tensor Core architecture (Volta, Turing, Ampere, Hopper)
   - FP16, BF16, INT8, FP8 operations
   - WMMA API, CUTLASS
   - **Project Connection:** CapitalOne (FP16 for fraud scoring), Fiserv (H100 FP8)

#### **Study Resources:**
- Video: "CUDA Graphs" (GTC 2020)
- Video: "Tensor Core Optimization" (GTC 2022)
- Documentation: CUDA Graphs, Tensor Core Programming Guide
- Paper: "Cutlass: Efficient CUDA Linear Algebra" (NVIDIA)

#### **Practice Questions:**
1. What are CUDA Graphs? When can you use them?
2. How do CUDA streams enable concurrency? What are the limitations?
3. What are Tensor Cores? How do they differ from CUDA cores?
4. When would you use FP16 vs. FP32? What are the trade-offs?
5. How do you overlap H2D transfer with compute?

#### **Hands-On Exercises:**
1. **CUDA Graphs:** Capture a multi-kernel pipeline as a CUDA Graph. Measure launch overhead reduction.
2. **Stream Concurrency:** Implement a 3-stage pipeline (H2D → Compute → D2H) with streams. Measure throughput improvement.
3. **Tensor Core GEMM:** Implement a matrix multiplication using Tensor Cores (WMMA API). Compare with FP32.

---

## Phase 5: Distributed Systems & Orchestration (Weeks 10-11)

### **Priority: P0 - Critical**
### **Time Allocation: 50 hours**

### **Week 10: Kubernetes & Container Orchestration**

#### **Topics:**
1. **Kubernetes Fundamentals** (P0)
   - Pods, Deployments, Services, ConfigMaps, Secrets
   - Resource requests/limits, QoS classes
   - Horizontal Pod Autoscaler (HPA), Vertical Pod Autoscaler (VPA)
   - **Project Connection:** All projects (Kubernetes for model serving)

2. **GPU Orchestration** (P0)
   - NVIDIA Device Plugin, GPU sharing (MIG, time-slicing)
   - Multi-tenant GPU isolation, priority classes
   - GPU monitoring (DCGM, Prometheus metrics)
   - **Project Connection:** Broadcom (multi-tenant GPU pools), CapitalOne (GPU isolation per tenant)

3. **Advanced Scheduling** (P1)
   - Node affinity, taints/tolerations, topology spread constraints
   - Custom schedulers, gang scheduling, volcano
   - **Project Connection:** Fiserv (GPU-headroom-aware scheduling)

#### **Study Resources:**
- Book: "Kubernetes Up & Running" (Kelsey Hightower)
- Course: "Certified Kubernetes Administrator (CKA)"
- Documentation: Kubernetes, NVIDIA Device Plugin
- Video: "Kubernetes for ML Workloads" (KubeCon talks)

#### **Practice Questions:**
1. How does Kubernetes schedule pods? What factors does it consider?
2. How do you request GPU resources in Kubernetes?
3. What is the difference between HPA and VPA? When would you use each?
4. How do you isolate GPU resources between tenants?
5. What are taints and tolerations? Give an example use case.

#### **Hands-On Exercises:**
1. **Deploy Model on K8s:** Deploy a model server on Kubernetes with GPU support. Configure HPA.
2. **Multi-Tenant GPU:** Set up GPU time-slicing or MIG for multi-tenancy. Measure isolation.
3. **Custom Metrics:** Create a custom metric (e.g., queue depth) for HPA. Test autoscaling.

---

### **Week 11: Distributed Systems Patterns**

#### **Topics:**
1. **Consistency & Availability** (P0)
   - CAP theorem, PACELC, consistency models
   - Distributed transactions, two-phase commit, saga pattern
   - **Project Connection:** Broadcom (99.99% availability), CapitalOne (99.999% availability)

2. **Load Balancing & Routing** (P0)
   - Layer 4 vs. Layer 7 load balancing
   - Consistent hashing, session affinity, sticky sessions
   - Circuit breakers, retries, timeouts, bulkheads
   - **Project Connection:** Apple (consistent hashing for 85% cache hit rate), Fiserv (health-aware routing)

3. **Message Queues & Event Streaming** (P1)
   - Kafka, Pulsar, RabbitMQ
   - Pub/sub, event sourcing, CQRS
   - Exactly-once semantics, idempotency
   - **Project Connection:** Broadcom (Kafka for telemetry), CapitalOne (Kafka for transaction events)

#### **Study Resources:**
- Book: "Designing Data-Intensive Applications" (Martin Kleppmann)
- Book: "Site Reliability Engineering" (Google)
- Paper: "CAP Twelve Years Later" (Eric Brewer)
- Video: "Distributed Systems Patterns" (GOTO Conference)

#### **Practice Questions:**
1. Explain the CAP theorem. Can you have all three?
2. What is consistent hashing? How does it help with load balancing?
3. How do circuit breakers work? When would you use one?
4. What is the difference between at-least-once and exactly-once semantics?
5. How do you handle distributed transactions?

#### **Hands-On Exercises:**
1. **Consistent Hashing:** Implement a consistent hashing ring. Test with node additions/removals.
2. **Circuit Breaker:** Implement a circuit breaker pattern. Test with simulated failures.
3. **Event Streaming:** Build a simple event streaming pipeline with Kafka. Test exactly-once processing.

---

## Phase 6: Zero-Copy & Performance Optimization (Week 12)

### **Priority: P0 - Critical**
### **Time Allocation: 40 hours**

### **Week 12: Zero-Copy Architectures**

#### **Topics:**
1. **Zero-Copy Techniques** (P0)
   - Shared memory (POSIX shm, mmap)
   - Apache Arrow (columnar format, C Data Interface)
   - Memory-mapped files, huge pages
   - **Project Connection:** All four projects (zero-copy as recurring theme)

2. **Serialization Optimization** (P0)
   - Protocol Buffers, FlatBuffers, Cap'n Proto
   - Zero-copy deserialization, view-based parsing
   - **Project Connection:** Broadcom (eliminated 200ms serialization overhead)

3. **Network Optimization** (P1)
   - RDMA, InfiniBand, RoCE
   - DPDK, netmap, kernel bypass
   - TCP tuning, socket options
   - **Project Connection:** Broadcom (10 Gbps per node telemetry)

#### **Study Resources:**
- Documentation: Apache Arrow, FlatBuffers
- Paper: "RDMA Over Ethernet" (RoCE)
- Video: "Zero-Copy Data Movement" (Performance Summit)
- Blog: "High-Performance Networking" (Cloudflare)

#### **Practice Questions:**
1. What is zero-copy? Give examples from your projects.
2. How does Apache Arrow enable zero-copy data exchange?
3. Compare Protocol Buffers, FlatBuffers, and Cap'n Proto.
4. What is RDMA? When would you use it?
5. How does DPDK achieve kernel bypass?

#### **Hands-On Exercises:**
1. **Shared Memory IPC:** Build two processes that communicate via shared memory. Measure latency vs. sockets.
2. **Arrow Integration:** Use Apache Arrow to pass data between Python and C++. Measure serialization overhead.
3. **FlatBuffers:** Serialize/deserialize data with FlatBuffers. Compare with Protocol Buffers.

---

## Phase 7: Project-Specific Deep Dives (Weeks 13-14)

### **Priority: P0 - Critical**
### **Time Allocation: 50 hours**

### **Week 13: Broadcom & Apple Deep Dive**

#### **Broadcom Cloud SWG (2015–2021)**
1. **Architecture Review** (P0)
   - 6+ model orchestration (malware, URL, DLP, content, behavioral, threat intel)
   - Zero-copy shared memory request context
   - Hybrid deployment (on-premise + GCP)
   - **Key Metrics:** 350ms→85ms (4.1×), 1K→10K req/sec/GPU (10×)

2. **Technical Deep Dive** (P0)
   - XGBoost + CNN malware detection (99.5% accuracy)
   - BERT + GBRT URL classification (98.5% accuracy)
   - NER + pattern matching DLP (99.2% recall)
   - Dynamic batching for 10× throughput

3. **Interview Questions** (P0)
   - "How did you orchestrate 6+ models with dependencies?"
   - "Explain your zero-copy shared memory design."
   - "How did you handle hybrid cloud deployment?"
   - "What was your canary deployment strategy?"

#### **Apple Siri (2021–2023)**
1. **Architecture Review** (P0)
   - 5-stage pipeline (ASR → NLU → Search → Orchestration → TTS)
   - Shared memory arena for stage handoff
   - Streaming ASR with partial results
   - **Key Metrics:** 850ms→280ms (3.0×), 40%→85% cache hit rate

2. **Technical Deep Dive** (P0)
   - Conformer ASR with incremental decoding
   - DistilBERT for NLU (97% accuracy retention)
   - FastSpeech TTS with streaming synthesis
   - Apple Silicon optimization (NEON, Metal GPU)

3. **Interview Questions** (P0)
   - "How did you reduce stage-to-stage overhead?"
   - "Explain your streaming ASR implementation."
   - "How did you optimize for Apple Silicon?"
   - "How did you handle backpressure across 5 stages?"

#### **Study Tasks:**
1. Re-read BROADCOM_CLOUD_SWG_STORY.md and APPLE_SIRI_CONVERSATIONAL_AI_STORY.md
2. Draw architecture diagrams from memory
3. Practice explaining zero-copy design decisions
4. Prepare 3-minute story for each project

---

### **Week 14: CapitalOne & Fiserv Deep Dive**

#### **CapitalOne Fraud (2024–2025)**
1. **Architecture Review** (P0)
   - Three-tier architecture (decision <5ms, reasoning 2–5s, triage 5–10s)
   - Per-core zero-copy scoring pipeline
   - CUDA Graphs for sub-5ms p99
   - **Key Metrics:** Sub-5ms at 24,500 TPS, 99.999% availability

2. **Technical Deep Dive** (P0)
   - Ensemble scoring (XGBoost, GBDT, scorecards, rules, neural)
   - CUDA Graphs (145µs → 5µs launch overhead)
   - Sentinel/Helios governance (token admission, priority lanes, degrade modes)
   - Multi-tenant GPU isolation

3. **Interview Questions** (P0)
   - "How did you achieve sub-5ms p99 latency?"
   - "Explain CUDA Graphs and their impact."
   - "How did Sentinel protect against overload?"
   - "How did you isolate GPU resources between tenants?"

#### **Fiserv Loan Director (2026–Present)**
1. **Architecture Review** (P0)
   - Multi-model LLM orchestration (8B + 70B)
   - Hierarchical context assembly
   - Session-aware KV-cache management
   - **Key Metrics:** 8.5s→2.3s (3.7×), 60% token reduction, 70% KV-cache hit rate

2. **Technical Deep Dive** (P0)
   - Rust control plane (Axum, Tokio, DashMap)
   - Zero-copy GPU transfers (pinned memory, async DMA)
   - KV-cache reuse with session stickiness
   - vLLM integration (PagedAttention, continuous batching)

3. **Interview Questions** (P0)
   - "How did you reduce token usage by 60%?"
   - "Explain your KV-cache management strategy."
   - "Why did you choose Rust for the control plane?"
   - "How did you handle large context workflows?"

#### **Study Tasks:**
1. Re-read CAPITALONE_TRANSACTION_PROCESSING_STORY.md and FISERV_LOAN_DIRECTOR_STORY.md
2. Draw architecture diagrams from memory
3. Practice explaining CUDA Graphs and KV-cache management
4. Prepare 3-minute story for each project

---

## Phase 8: System Design & Architecture (Week 15)

### **Priority: P0 - Critical**
### **Time Allocation: 40 hours**

### **Week 15: System Design Practice**

#### **Topics:**
1. **System Design Framework** (P0)
   - Requirements gathering (functional, non-functional)
   - Capacity estimation (QPS, storage, bandwidth)
   - High-level design (components, data flow)
   - Deep dive (1–2 components)
   - Bottleneck analysis, scaling strategies

2. **AI-Specific System Design** (P0)
   - LLM serving platform design
   - Multi-model orchestration
   - GPU cluster design
   - KV-cache management at scale
   - **Project Connection:** All four projects

3. **Practice Problems** (P0)
   - Design a real-time fraud detection system (CapitalOne)
   - Design a conversational AI platform (Apple)
   - Design a multi-model security gateway (Broadcom)
   - Design a loan processing AI platform (Fiserv)

#### **Study Resources:**
- Book: "System Design Interview" (Alex Xu) Vol 1 & 2
- Book: "Designing Data-Intensive Applications" (Martin Kleppmann)
- Website: systemdesignprimer.com, bytebytego.com
- Video: "System Design Interview" (Exponent, Tech Dummies)

#### **Practice Questions:**
1. Design a URL shortening service (classic)
2. Design a real-time chat system (classic)
3. Design an LLM inference platform (AI-specific)
4. Design a fraud detection system (AI-specific)
5. Design a video transcoding platform (GPU-specific)

#### **Hands-On Exercises:**
1. **Mock Design Session:** Practice designing a system in 45 minutes. Present to a friend/colleague.
2. **Capacity Estimation:** Estimate QPS, storage, bandwidth for Twitter, YouTube, Uber.
3. **Bottleneck Analysis:** Take an existing system design. Identify bottlenecks and propose solutions.

---

## Phase 9: Behavioral & Leadership (Week 16)

### **Priority: P1 - Important**
### **Time Allocation: 30 hours**

### **Week 16: Behavioral & Leadership Preparation**

#### **Topics:**
1. **STAR Method** (P0)
   - Situation, Task, Action, Result
   - Quantify results (metrics, percentages)
   - Focus on YOUR contributions (not "we")
   - **Project Connection:** All four projects with quantified metrics

2. **Leadership & Influence** (P0)
   - Technical leadership (architecture decisions, design reviews)
   - Mentoring (team growth, knowledge sharing)
   - Cross-functional collaboration (product, security, compliance)
   - **Project Connection:** Broadcom (led 6+ model orchestration), CapitalOne (Sentinel governance)

3. **Conflict Resolution** (P1)
   - Technical disagreements (data-driven decisions)
   - Priority conflicts (stakeholder management)
   - Failure post-mortems (blameless culture)
   - **Project Connection:** CapitalOne (round-robin vs. scoring debate)

#### **Study Resources:**
- Book: "Crucial Conversations" (Kerry Patterson)
- Book: "The Manager's Path" (Camille Fournier)
- Article: "STAR Method for Behavioral Interviews"
- Video: "Behavioral Interview Tips" (Exponent)

#### **Practice Questions:**
1. "Tell me about a time you had a technical disagreement." (CapitalOne scoring debate)
2. "Describe a time you led a complex project." (Broadcom multi-model platform)
3. "Tell me about a time you failed." (Pick a real failure, show learning)
4. "How do you prioritize competing demands?" (Fiserv latency vs. accuracy)
5. "Describe your leadership style." (Servant leadership, data-driven)

#### **Hands-On Exercises:**
1. **STAR Stories:** Write 10 STAR stories from your four projects. Practice delivering them in 2 minutes.
2. **Mock Behavioral Interview:** Practice with a friend. Get feedback on clarity and impact.
3. **Leadership Philosophy:** Write a 1-page leadership philosophy. Be ready to discuss it.

---

## Mock Interview Schedule

### **Weeks 8-16: Weekly Mock Interviews**

| Week | Type | Focus | Duration |
|------|------|-------|----------|
| **8** | Systems Coding | C++/Rust concurrency | 60 min |
| **9** | AI/ML Infra | Model serving, batching | 60 min |
| **10** | System Design | LLM serving platform | 60 min |
| **11** | Behavioral | Leadership, conflict | 60 min |
| **12** | Systems Coding | CUDA optimization | 60 min |
| **13** | Project Deep Dive | Broadcom/Apple | 90 min |
| **14** | Project Deep Dive | CapitalOne/Fiserv | 90 min |
| **15** | System Design | AI-specific problem | 60 min |
| **16** | Full Loop | 4 × 45 min sessions | 3 hours |

### **Mock Interview Rubric:**

| Category | Weight | Evaluation Criteria |
|----------|--------|---------------------|
| **Technical Depth** | 35% | Correctness, optimization, trade-offs |
| **Problem Solving** | 25% | Approach, clarification, edge cases |
| **Communication** | 20% | Clarity, structure, listening |
| **Leadership** | 20% | Decision-making, influence, mentorship |

---

## Resource Library

### **Books:**
1. "Computer Systems: A Programmer's Perspective" - Bryant & O'Hallaron
2. "Effective Modern C++" - Scott Meyers
3. "Programming Rust" - Jim Blandy, Jason Orendorff
4. "Designing Data-Intensive Applications" - Martin Kleppmann
5. "System Design Interview" Vol 1 & 2 - Alex Xu
6. "Deep Learning" - Goodfellow, Bengio, Courville
7. "Programming Massively Parallel Processors" - Hwu, Kirk

### **Online Courses:**
1. "CUDA C Programming" - NVIDIA DLI
2. "CS224N: NLP with Deep Learning" - Stanford
3. "CS329S: Machine Learning Systems Design" - Stanford
4. "Certified Kubernetes Administrator (CKA)" - Linux Foundation
5. "FastAI Practical Deep Learning" - fast.ai

### **Documentation:**
1. CUDA C Programming Guide - NVIDIA
2. Kubernetes Documentation - kubernetes.io
3. vLLM Documentation - vllm.readthedocs.io
4. TensorRT-LLM Documentation - NVIDIA
5. Apache Arrow Documentation - arrow.apache.org

### **Video Channels:**
1. NVIDIA GTC Talks
2. CppCon (C++ Conference)
3. KubeCon (Kubernetes Conference)
4. Stanford Online (CS lectures)
5. Exponent (Interview prep)

### **Practice Platforms:**
1. LeetCode (coding problems)
2. HackerRank (coding problems)
3. Pramp (mock interviews)
4. Interviewing.io (mock interviews)
5. ByteByteGo (system design)

---

## Progress Tracker

### **Weekly Checkpoints:**

| Week | Topic | Hours Planned | Hours Actual | Completion % | Notes |
|------|-------|---------------|--------------|--------------|-------|
| 1 | Memory & CPU | 25 | | | |
| 2 | Concurrency | 25 | | | |
| 3 | C++ | 30 | | | |
| 4 | Rust & Python | 30 | | | |
| 5 | Model Architectures | 25 | | | |
| 6 | Model Serving | 25 | | | |
| 7 | LLM Optimization | 25 | | | |
| 8 | CUDA Fundamentals | 30 | | | |
| 9 | Advanced CUDA | 30 | | | |
| 10 | Kubernetes | 25 | | | |
| 11 | Distributed Systems | 25 | | | |
| 12 | Zero-Copy | 40 | | | |
| 13 | Broadcom & Apple | 25 | | | |
| 14 | CapitalOne & Fiserv | 25 | | | |
| 15 | System Design | 40 | | | |
| 16 | Behavioral | 30 | | | |

### **Milestone Assessments:**

| Milestone | Target Date | Status | Notes |
|-----------|-------------|--------|-------|
| **Phase 1-3 Complete** (Foundations) | Week 7 | ☐ | |
| **Phase 4-6 Complete** (GPU & Distributed) | Week 12 | ☐ | |
| **Phase 7-8 Complete** (Projects & Design) | Week 15 | ☐ | |
| **Phase 9 Complete** (Behavioral) | Week 16 | ☐ | |
| **Ready for Interviews** | Week 17 | ☐ | |

### **Confidence Self-Assessment:**

Rate your confidence (1-10) for each topic before and after study:

| Topic | Before | After | Improvement |
|-------|--------|-------|-------------|
| C++ Systems Programming | | | |
| Rust Programming | | | |
| CUDA Optimization | | | |
| Transformer Models | | | |
| Model Serving (vLLM, TensorRT) | | | |
| Kubernetes Orchestration | | | |
| Distributed Systems | | | |
| Zero-Copy Architectures | | | |
| System Design | | | |
| Behavioral Interviews | | | |

---

## Final Checklist (Week 17)

### **Before Starting Interviews:**

- [ ] All 16 weeks of study complete
- [ ] 9+ mock interviews completed
- [ ] 10+ STAR stories prepared and practiced
- [ ] 4 project stories ready (3-minute version)
- [ ] System design framework internalized
- [ ] Coding problems (50+ LeetCode) practiced
- [ ] Resume updated with all four projects
- [ ] GitHub portfolio polished (Sentinel Fabric, CUDA labs)
- [ ] Reference list prepared (3–5 colleagues)
- [ ] Interview schedule set (3–5 companies)

### **Mental Preparation:**

- [ ] Sleep 7–8 hours before each interview
- [ ] Exercise regularly (stress management)
- [ ] Practice meditation/breathing exercises
- [ ] Prepare interview space (quiet, good internet)
- [ ] Test video/audio setup beforehand
- [ ] Have water and notes ready

### **During Interviews:**

- [ ] Clarify requirements before solving
- [ ] Think out loud (show thought process)
- [ ] Ask for hints if stuck
- [ ] Manage time (don't get stuck on one part)
- [ ] Be honest about what you don't know
- [ ] Ask questions at the end (show interest)

### **After Interviews:**

- [ ] Send thank-you emails
- [ ] Reflect on what went well/poorly
- [ ] Update study plan based on gaps
- [ ] Continue practicing (don't stop after one offer)

---

## Good Luck! 🚀

**Remember:**
- You have **11 years of real-world experience** building production AI systems at massive scale
- Your four projects demonstrate **progressive complexity** and **measurable impact**
- You're not just preparing for interviews—you're **articulating expertise you already have**
- Confidence comes from preparation. Trust the process.

**You've got this!** 💪

---

**Last Updated:** April 17, 2026  
**Next Review:** Weekly (every Sunday)  
**Accountability Partner:** [Name/Contact]
