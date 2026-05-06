# Unified Syllabus 1: Practical AI/HPC Systems Engineering

**Source reorganized from:** `docs/draft_projects`  
**Audience:** Senior/Staff AI Systems Engineer, HPC Platform Engineer, Solutions Architect  
**Core scenario:** Real-time fraud scoring plus GPU LLM analysis on bare-metal Kubernetes in on-prem and multi-region environments.

---

# 0. Assessment And Reorganization Strategy

`draft_projects` contains strong practical material, but it is mixed as a conversation transcript. The useful content falls into four major bodies of knowledge:

1. **CUDA/LLM troubleshooting:** Eight practical Nsight/vLLM/TensorRT-LLM scenarios.
2. **Guardrail + LLM serving architecture:** How to eliminate Python, serialization, and CPU/GPU copy tax.
3. **Bare-metal Kubernetes infrastructure:** Host, K8s, networking, storage, GPU, and multi-region deployment.
4. **Systems programming implementation:** C++ hot-path engine, Rust gateway, POSIX shared memory, `mmap`, Arrow IPC, and `memmap2`.

This syllabus reorganizes the material top-down:

```text
Story and business constraint
  -> architecture decision
    -> infrastructure/runtime design
      -> code pattern
        -> profiling and troubleshooting runbook
```

The result is a curriculum you can use for interviews and hands-on practice without jumping between unrelated notes.

---

# 1. Story Map

## 1.1 The Unified Narrative

The system is a **Sentinel-style fraud platform** with two latency domains:

- **Hot path:** sub-10 ms fraud decisioning for payment authorization.
- **Warm/LLM path:** sub-200 ms fraud analysis for medium-risk transactions, including guardrails and explanation.

The core design principle is to keep the hot path deterministic and isolated, while using GPU LLM infrastructure for richer reasoning only when the transaction can tolerate it.

```text
Payment request
    |
    v
Rust Sentinel Gateway
    |-- hot path -> C++ zero-copy feature engine -> XGBoost/rules/GPU classifier -> decision
    |
    |-- warm path -> Rust tokenization/guardrails -> Triton ensemble -> LLM analysis -> audit
    |
    v
Arrow / shared-memory audit boundary
```

## 1.2 The Four Stories In The Draft

| Story | Main Problem | Key Decision | Best Interview Angle |
|-------|--------------|--------------|----------------------|
| **Story 1: Bare-metal fraud scoring** | Legacy Java/Python stack had ~35 ms p99, target was sub-10 ms | Tune host, K8s, network, storage, and GPU placement as one system | "I understand bare-metal K8s latency from BIOS/kernel to pod spec." |
| **Story 2: GPU LLM fraud analysis** | Add LLM reasoning under 200 ms with guardrails and 500+ concurrent requests | Rust gateway plus Triton GPU-resident ensemble, vLLM/TensorRT-LLM, H100 topology-aware scheduling | "I know how to make LLM serving production-grade, not just call an API." |
| **Story 3: C++ hot-path engine** | Python hot path spent ~18 ms p99 mostly in glue tax | NUMA arena, SIMD parser, SPSC queues, Arrow IPC, C ABI to Rust | "I can remove allocation, serialization, and lock contention from a real hot path." |
| **Story 4: Rust gateway control plane** | Python Flask router had blocking calls, no circuit breakers, no GPU-aware admission | Tokio async gateway with consistent hashing, circuit breakers, admission, SSE cancellation | "I can protect GPU capacity and p99 with control-plane logic." |

## 1.3 Metrics To Memorize

| Area | Before | After | Why It Mattered |
|------|--------|-------|-----------------|
| Hot-path fraud p99 | 35 ms | 8.7 ms | Met card-network style SLA |
| Hot-path p99.9 | 120 ms | 14.2 ms | Reduced jitter after host/K8s tuning |
| Throughput per node | 8,000 TPS | 14,500 TPS | More capacity per bare-metal node |
| C++ hot-path p99 | 18.4 ms | 4.1 ms | Removed Python/glue tax |
| C++ hot-path allocations | ~340/request | 0/request | Arena allocation |
| Serialization overhead | 3.1 ms | 0 | Arrow in-place publish |
| Rust routing p99 | 8.7 ms | 0.12 ms | Async gateway plus nonblocking routing |
| Prefix cache hit rate | 5 percent | 72 percent | Consistent hash routing |
| LLM path p99 | 850 ms | 185 ms | Rust + Triton + tuned GPU |
| Guardrail overhead | 40 ms | 3 ms | GPU-resident ensemble |
| GPU decode utilization | 45 percent | 78 percent | Graphs, FP8 KV, better batching |
| H100 concurrent requests | 32 | 96 | KV/cache/runtime tuning |
| Model load time | 5 min | 6 sec | NVMe plus GPUDirect Storage |

---

# 2. Practical CUDA/LLM Troubleshooting Runbook

## 2.1 Tool Order

Use the smallest tool that can separate the layer:

```text
1. Runtime metrics: vLLM/TensorRT-LLM, DCGM, Prometheus
2. Nsight Systems: CPU gaps, CUDA launches, copies, NCCL, timeline ordering
3. Nsight Compute: specific kernel bottleneck after Systems identifies it
4. K8s/host tools: topology, throttling, memory pressure, network path
```

Interview line: "I use Nsight Systems to find where time goes, then Nsight Compute to explain why a kernel is slow. I do not start by rewriting kernels."

## 2.2 Eight Day-To-Day CUDA Tasks

| Task | Symptom | First Tool | Key Signal | Root Cause | Fix |
|------|---------|------------|------------|------------|-----|
| **1. TTFT spike after prompt length grows** | p99 TTFT 45 ms -> 190 ms; p50 barely changes | `nsys`, then `ncu` on `flash_attn` | Prefill attention dominates; SM ~62 percent, DRAM ~41 percent | Prefill attention scales with sequence length; 512 -> 2048 increases attention work sharply | Truncate context, chunked prefill, prefix caching, FP8 where safe |
| **2. CUDA Graphs reduce throughput** | Decode TPS 850 -> 510; GPU memory jumps; OOM on long sequences | `nsys` plus vLLM metrics | Graph workspace 847 MB; KV cache 32 GB -> 24 GB; effective seqs 64 -> 38 | Static graph capture steals memory from KV cache | Reduce max length, limit graph buckets, use eager for long sequences |
| **3. Tiny kernel storm in decode** | Decode latency 2x; utilization oscillates 80/20 | `nsys stats` | Many 1-3 us kernels plus ~150 ms CPU gaps | Python eager post-processing launches many small CUDA ops | Fuse with `torch.compile`, CPU overlap, graph capture, batch post-processing |
| **4. TP=4 Llama 70B under-scales** | Single H100 12 tok/s; 4x H100 only 25 tok/s | `nsys -t nccl`, `nvidia-smi topo -m` | AllReduce ~0.8 ms per layer; GPUs cross `SYS` path | Tensor parallel collectives cross PCIe instead of NVLink | TP=2 on NVLink pair, use HGX/DGX mesh, TP=2 x PP=2, overlap AllReduce |
| **5. FP8 gives bad output on long prompts** | Short prompts OK; >2048 token prompts become repetitive or wrong | BF16/FP8 diff test, `ncu` on attention/softmax | Output diverges after long context | FP8 softmax/reduction precision is insufficient for long attention | FP8 weights plus BF16 attention accumulation, FP16/BF16 KV for long prompts |
| **6. p99 TPOT is 3x p50** | P50 18 ms, p99 58 ms | Long `nsys` capture | Slow decode steps coincide with prefill interrupt | Scheduler interleaves compute-heavy prefill with latency-sensitive decode | Chunked prefill, prefill/decode disaggregation, decode priority |
| **7. KV cache OOM too early** | Expected 64+ requests; OOM at 38 | vLLM metrics, memory accounting | GPU cache 97 percent at 38 running requests | KV allocated for max model length, not average actual length | Reduce max model length, FP8 KV, prefix caching, block-size tuning |
| **8. TensorRT-LLM slower than vLLM** | vLLM decode 14 ms; TRT-LLM 19 ms | Side-by-side `nsys` | TRT kernels faster, but graph rebuild adds ~9 ms | Workload has highly variable sequence lengths | Shape buckets, route fixed-shape to TRT-LLM and variable to vLLM |

## 2.3 Commands Worth Practicing

```bash
# Prefill/latency profile.
nsys profile -o prefill_long \
  --trace-fork-before-exec=true \
  --cuda-graph-trace=node \
  -t cuda,nvtx,cublas \
  python benchmarks/benchmark_latency.py \
  --model meta-llama/Llama-3.1-8B-Instruct \
  --input-len 2048 \
  --output-len 1 \
  --batch-size 1

# Kernel-level attention profile.
VLLM_ENFORCE_EAGER=1 ncu -o attn_long \
  -k regex:"flash_attn|paged_attention" \
  --section SpeedOfLight \
  --section MemoryWorkloadAnalysis \
  --section WarpStateStats \
  python benchmarks/benchmark_latency.py \
  --input-len 2048 --output-len 1 --batch-size 1

# Tensor-parallel communication profile.
nsys profile -o tp4_debug \
  --trace-fork-before-exec=true \
  --cuda-graph-trace=node \
  -t cuda,nvtx,nccl \
  vllm serve meta-llama/Llama-3.1-70B-Instruct \
  --tensor-parallel-size 4
```

## 2.4 Mental Models Behind The Tasks

| Concept | What To Say In Interview |
|---------|--------------------------|
| **Prefill vs decode** | "Prefill is compute-heavy and parallel over prompt tokens; decode is step-by-step and often memory/KV-cache bound." |
| **CUDA Graphs** | "Graphs remove launch overhead, but they require stable shapes and reserve static workspace that can reduce KV capacity." |
| **KV cache** | "KV cache is GPU memory. Capacity depends on layers, KV heads, head dim, max sequence length, dtype, and fragmentation." |
| **AllReduce** | "Tensor parallelism trades memory capacity for communication. If collectives cross PCIe/SYS, scaling collapses." |
| **FP8** | "FP8 can improve throughput, but long-context attention often needs BF16/FP16 accumulation or selective routing." |

---

# 3. Guardrail + LLM Serving Architecture

## 3.1 The Problem: Python And Serialization Tax

The draft describes a common serving path:

```text
Request JSON
  -> Python input guardrail
  -> Python tokenizer
  -> CPU to GPU copy
  -> LLM inference
  -> GPU to CPU copy
  -> Python detokenizer
  -> Python output guardrail
  -> Response JSON
```

| Hop | What Happens | Typical Cost |
|-----|--------------|--------------|
| Request -> input guardrail | JSON parse, Python objects | 0.5-2 ms |
| Guardrail -> tokenizer | Python dict/tensor conversion | 0.2-1 ms |
| Tokenizer -> GPU | CPU/GPU copy and launch | 0.05-0.5 ms |
| GPU -> detokenizer | GPU/CPU copy plus Python object creation | 0.1-0.5 ms |
| Detokenizer -> output guardrail | String manipulation, Python overhead | 0.5-2 ms |
| Output guardrail -> response | JSON serialization | 0.2-1 ms |
| Cross-cutting | GIL contention | Dominant at high QPS |

Interview line: "At low QPS the tax is invisible; at high QPS, the GPU sits idle because the CPU serving path cannot feed it."

## 3.2 Architecture Options

| Option | Best For | What It Removes | Tradeoff |
|--------|----------|-----------------|----------|
| **Triton ensemble** | ML guardrails plus LLM on GPU | Python hops, intermediate CPU/GPU copies, multiple network calls | Requires model packaging and static-ish pipeline contracts |
| **Rust/C++ gateway** | Tokenization, detokenization, rules, formatting, streaming | Python GIL, blocking request handling, CPU-side serialization | Requires ownership of full serving stack |
| **Arrow/DLPack/CUDA IPC data plane** | Heterogeneous stages sharing tensors or columnar records | Serialization and object reconstruction | More explicit schema and lifecycle management |
| **Co-located guardrails in one GPU process** | Lowest overhead when models fit together | Inter-process overhead and network hops | Guardrail failure can affect LLM process; GPU memory contention |
| **Compiled pipeline** | Stable classifier and bucketed LLM shapes | Kernel launch overhead and intermediate tensor materialization | Dynamic control flow and variable sequence length limit graph capture |

## 3.3 Recommended Architecture

For payment processing with low latency, high throughput, vLLM/TensorRT-LLM, and regional deployments:

```text
Client
  |
  | gRPC
  v
Rust/C++ Gateway
  - tokenization
  - rule-based input guardrail
  - prompt construction
  - rate limiting
  |
  | preprocessed token IDs
  v
Triton Inference Server
  - TensorRT input guardrail classifier
  - TensorRT-LLM or vLLM backend
  - TensorRT output guardrail classifier
  |
  | generated token IDs
  v
Rust/C++ Gateway
  - detokenization
  - rule-based output guardrail
  - response formatting
  - audit publish
```

## 3.4 Triton Ensemble Sketch

```text
name: "fraud_detection_pipeline"
platform: "ensemble"

input_guardrail(tokens) -> checked_tokens
llama_8b_trtllm(checked_tokens) -> generated_tokens
output_guardrail(generated_tokens) -> response
```

What this design eliminates:

| Tax | How It Is Removed |
|-----|-------------------|
| Python GIL | Gateway CPU work is Rust/C++ |
| Guardrail/LLM serialization | Triton ensemble keeps tensors in the pipeline |
| CPU/GPU copies between stages | ML stages remain GPU-resident |
| Multiple network hops | Gateway -> Triton -> gateway |
| Python tokenization overhead | Rust tokenizer path |

## 3.5 Latency Comparison

| Architecture | Overhead Excluding LLM Compute |
|--------------|--------------------------------|
| Python microservices | 15-40 ms |
| Python monolith | 8-15 ms |
| Triton ensemble | 2-5 ms |
| Rust gateway plus Triton ensemble | Less than 2 ms |

## 3.6 Guardrail Pipeline Results

| Metric | Before: Python Pipeline | After: Rust + Triton + Tuned GPU |
|--------|-------------------------|----------------------------------|
| End-to-end LLM path p50 | 280 ms | 95 ms |
| End-to-end LLM path p99 | 850 ms | 185 ms |
| Guardrail overhead | 40 ms | 3 ms |
| Python serialization tax | 15 ms | 0 ms |
| GPU utilization during decode | 45 percent | 78 percent |
| Concurrent requests per H100 | 32 | 96 |
| Cost per 1M fraud analyses | $847 | $312 |
| Model loading time | 5 min | 6 sec with GDS |

---

# 4. Bare-Metal Kubernetes Infrastructure

## 4.1 Host vs K8s Responsibility Model

This is the most important mental model for on-prem bare-metal K8s:

```text
Host prepares deterministic capacity.
Kubernetes assigns deterministic capacity.
Application consumes the assigned capacity without fighting the scheduler.
```

| Area | Host Side | Kubernetes Side | Avoid Doing |
|------|-----------|-----------------|-------------|
| **CPU/threads** | BIOS performance mode, CPU governor, `isolcpus`, `nohz_full`, `rcu_nocbs`, housekeeping cores | CPU Manager `static`, Guaranteed QoS, integer CPU requests/limits, `PriorityClass` | Ad hoc `taskset` inside production pods |
| **NUMA** | Verify topology with `numactl --hardware`, `lstopo`, disable auto NUMA balancing when explicitly placing workloads | Topology Manager `single-numa-node`, Memory Manager `Static` | Wrapping K8s app containers with `numactl` as the primary placement mechanism |
| **Memory** | Reserve hugepages, disable swap, tune `vm.max_map_count`, locked memory | Hugepage requests, memory request equals limit, large `/dev/shm` via `emptyDir.medium: Memory` | Reserving hugepages on the host but not requesting them in pods |
| **Network** | NIC firmware, queue count, IRQ affinity, SR-IOV VFs, RoCE prerequisites | SR-IOV CNI, Cilium/eBPF, NetworkAttachmentDefinition, topology-aware NIC placement | Assuming host DPDK/AF_XDP helps pods without giving pods the right device path |
| **GPU** | Driver stack, firmware, persistence mode, DCGM exporter, MIG strategy | NVIDIA GPU Operator/device plugin, GPU Feature Discovery, GPU/MIG resource requests | Manual `/dev/nvidia*` mounts that bypass scheduling |
| **Storage** | Local NVMe, mount options, GDS driver stack | Local PVs or explicit node-affine mounts, pre-warmed model cache | Remote object-store-only model load for large LLMs |

Interview line: "I use `numactl` for inspection and benchmarks. In production Kubernetes, CPU Manager, Topology Manager, Memory Manager, device plugins, and pod resource requests should own placement."

## 4.2 Story 1: CPU Fraud Scoring Node

### Host Tuning

```bash
# /etc/default/grub
GRUB_CMDLINE_LINUX="isolcpus=4-19,24-39 nohz_full=4-19,24-39 rcu_nocbs=4-19,24-39"
```

Why:

- `isolcpus`: keep general scheduler off performance cores.
- `nohz_full`: reduce timer interrupt jitter.
- `rcu_nocbs`: move RCU callbacks off isolated cores.
- Housekeeping cores handle OS, kubelet, monitoring, and IRQs.

Hugepages:

```bash
GRUB_CMDLINE_LINUX="... default_hugepagesz=2M hugepagesz=2M hugepages=8192"
```

Kernel/network tuning:

```bash
vm.swappiness = 0
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 65535
```

IRQ affinity:

```bash
systemctl disable irqbalance
systemctl stop irqbalance

for irq in $(grep mlx5 /proc/interrupts | awk '{print $1}' | tr -d ':'); do
  echo 0-3,20-23 > /proc/irq/$irq/smp_affinity_list
done
```

### Kubelet Policy

```yaml
apiVersion: kubelet.config.k8s.io/v1beta1
kind: KubeletConfiguration
cpuManagerPolicy: static
cpuManagerReconcilePeriod: 5s
reservedSystemCPUs: "0-3,20-23"
topologyManagerPolicy: single-numa-node
topologyManagerScope: pod
memoryManagerPolicy: Static
```

### Fraud Scorer Pod Pattern

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: fraud-scorer
spec:
  priorityClassName: ultra-low-latency
  containers:
    - name: fraud-scorer
      image: fraud-scorer:v2.4
      resources:
        requests:
          cpu: "8"
          memory: "16Gi"
          hugepages-2Mi: "4Gi"
          nvidia.com/gpu: "1"
        limits:
          cpu: "8"
          memory: "16Gi"
          hugepages-2Mi: "4Gi"
          nvidia.com/gpu: "1"
      securityContext:
        capabilities:
          add: ["IPC_LOCK", "SYS_NICE"]
      volumeMounts:
        - name: shm
          mountPath: /dev/shm
        - name: hugepage
          mountPath: /dev/hugepages
        - name: nvme-scratch
          mountPath: /scratch
  volumes:
    - name: shm
      emptyDir:
        medium: Memory
        sizeLimit: "8Gi"
    - name: hugepage
      emptyDir:
        medium: HugePages-2Mi
    - name: nvme-scratch
      hostPath:
        path: /mnt/nvme0
        type: Directory
```

### Networking Decision

| Option | Result | Decision |
|--------|--------|----------|
| Standard TCP | Redis RTT about 2.1 ms | Too slow for hot feature access |
| DPDK | Redis RTT about 0.4 ms | Fast but high operational complexity |
| AF_XDP | Redis RTT about 0.7 ms | Chosen compromise |
| SR-IOV VF per pod | Removes veth, overlay, iptables path | Used for critical scorer pods |
| Cilium/eBPF | Good default observability and policy | Used for non-critical traffic |
| RDMA/RoCE | Future path below 500 us | Deferred due to Redis/custom store cost |

### Results

| Metric | Before | After |
|--------|--------|-------|
| P50 latency | 12 ms | 3.2 ms |
| P99 latency | 35 ms | 8.7 ms |
| P99.9 latency | 120 ms | 14.2 ms |
| Throughput | 8,000 TPS/node | 14,500 TPS/node |
| TLB miss rate | 4.2 percent | 0.3 percent |
| NIC to app latency | 2.1 ms | 0.7 ms |
| Cross-NUMA accesses | Frequent | Eliminated |

## 4.3 Story 2: H100 LLM Serving Node

### Host Configuration

```bash
GRUB_CMDLINE_LINUX="isolcpus=8-31,48-71 nohz_full=8-31,48-71 rcu_nocbs=8-31,48-71 \
 default_hugepagesz=2M hugepagesz=2M hugepages=16384 \
 iommu=pt intel_iommu=on"
```

Why `iommu=pt`:

- Required for SR-IOV VF assignment.
- Helps GPUDirect RDMA paths.
- Avoids IOMMU translation overhead for DMA-heavy workloads.

GPU-serving sysctls:

```bash
vm.max_map_count = 1048576
kernel.numa_balancing = 0
fs.aio-max-nr = 1048576
net.core.rmem_max = 67108864
net.core.wmem_max = 67108864
```

### GPU Scheduling Decisions

| Workload | GPU Strategy | Reason |
|----------|--------------|--------|
| Llama 8B FP8 | H100 MIG `3g.40gb` | Isolation and enough memory for model plus KV |
| Llama 70B | Full GPUs, TP=4 | Needs NVLink peer communication |
| TP=8 across nodes | SR-IOV + RDMA for NCCL | Separate high-speed collective traffic from service traffic |

### vLLM Pod Pattern

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: llama70b-server
spec:
  priorityClassName: high-priority-llm
  containers:
    - name: vllm-server
      image: vllm/vllm-openai:v0.8.0
      args:
        - "--model"
        - "meta-llama/Llama-3.1-70B-Instruct"
        - "--tensor-parallel-size"
        - "4"
        - "--dtype"
        - "float16"
        - "--quantization"
        - "fp8"
        - "--max-model-len"
        - "4096"
        - "--enable-chunked-prefill"
        - "--enable-prefix-caching"
        - "--gpu-memory-utilization"
        - "0.92"
        - "--max-num-seqs"
        - "128"
        - "--kv-cache-dtype"
        - "fp8"
      resources:
        requests:
          cpu: "16"
          memory: "64Gi"
          hugepages-2Mi: "16Gi"
          nvidia.com/gpu: "4"
        limits:
          cpu: "16"
          memory: "64Gi"
          hugepages-2Mi: "16Gi"
          nvidia.com/gpu: "4"
      volumeMounts:
        - name: shm
          mountPath: /dev/shm
        - name: model-cache
          mountPath: /models
  volumes:
    - name: shm
      emptyDir:
        medium: Memory
        sizeLimit: "32Gi"
    - name: model-cache
      hostPath:
        path: /mnt/nvme0/model-cache
        type: Directory
```

Key settings:

- `/dev/shm` at 32 GiB for NCCL and multi-process serving.
- FP8 weights and FP8 KV cache for capacity and memory bandwidth.
- Chunked prefill to reduce decode interruption.
- Prefix caching because fraud prompts share system prompt prefixes.
- Local NVMe model cache to avoid object-store cold start.

### NCCL Secondary Network

```yaml
apiVersion: k8s.cni.cncf.io/v1
kind: NetworkAttachmentDefinition
metadata:
  name: nccl-sriov-net
spec:
  config: |
    {
      "type": "sriov",
      "cniVersion": "0.3.1",
      "name": "nccl-network",
      "ipam": { "type": "host-local", "subnet": "10.56.0.0/16" },
      "vlan": 100
    }
```

```bash
NCCL_SOCKET_IFNAME=net1
NCCL_IB_DISABLE=0
NCCL_NET_GDR_LEVEL=5
```

### Multi-Region Placement

| Region | GPU | Model | Reason |
|--------|-----|-------|--------|
| US-East | 4x H100 nodes | Llama 70B TP=4 | Primary traffic and best quality |
| US-West | 2x H100 nodes | Llama 8B on MIG | Cost-optimized secondary |
| EU-West | 2x L40S nodes | Llama 8B FP8 | GDPR region, moderate traffic |

---

# 5. C++ Hot-Path Engine Patterns

## 5.1 Why The C++ Engine Exists

The Python baseline spent ~18 ms p99 mostly in serialization, allocation, GIL contention, and copies. Actual compute was under 3 ms.

The C++ engine removes the glue tax:

```text
Rust gateway
  -> shared memory / FFI pointer handoff
  -> C++ parser and feature engine
  -> XGBoost/rules/GPU classifier
  -> Arrow audit publish
```

## 5.2 NUMA-Pinned Arena

```cpp
class NumaArena {
public:
    NumaArena(int numa_node, size_t arena_size)
        : numa_node_(numa_node), arena_size_(arena_size), offset_(0) {
        base_ = static_cast<uint8_t*>(numa_alloc_onnode(arena_size_, numa_node_));
        if (!base_) throw std::bad_alloc();
        madvise(base_, arena_size_, MADV_HUGEPAGE);
        for (size_t i = 0; i < arena_size_; i += 4096) base_[i] = 0;
    }

    void* allocate(size_t size) {
        size = (size + 63) & ~63;
        size_t current = offset_.load(std::memory_order_relaxed);
        size_t next = current + size;
        if (next > arena_size_) return nullptr;
        offset_.store(next, std::memory_order_relaxed);
        return base_ + current;
    }

    void reset() { offset_.store(0, std::memory_order_relaxed); }

private:
    uint8_t* base_;
    int numa_node_;
    size_t arena_size_;
    std::atomic<size_t> offset_;
};
```

Design point: allocate once on the correct NUMA node, reuse with `reset()`, and avoid `malloc/free` on the hot path.

## 5.3 SIMD Zero-Copy Parser

```cpp
struct FieldView {
    const char* data;
    uint32_t length;
};

struct TransactionView {
    FieldView pan;
    FieldView amount;
    FieldView merchant_id;
    FieldView merchant_category;
    FieldView timestamp;
    FieldView terminal_id;
    FieldView country_code;
};

inline size_t simd_find_delimiter(const char* buf, size_t len, char delim) {
    const __m256i delim_vec = _mm256_set1_epi8(delim);
    size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + i));
        __m256i cmp = _mm256_cmpeq_epi8(chunk, delim_vec);
        uint32_t mask = _mm256_movemask_epi8(cmp);
        if (mask != 0) return i + __builtin_ctz(mask);
    }
    for (; i < len; i++) {
        if (buf[i] == delim) return i;
    }
    return len;
}
```

Design point: parse into views over the receive buffer. The field bytes are not copied into new objects.

## 5.4 Lock-Free SPSC Queue

```cpp
template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0);

public:
    bool try_push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (Capacity - 1);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        item = slots_[tail];
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) T slots_[Capacity];
};
```

Design point: the queue moves pointers to arena objects, not payloads. It is bounded, so it naturally creates backpressure.

## 5.5 Arrow IPC Publish Boundary

```cpp
extern "C" {
int sentinel_hotpath_process(
    const char* raw_buf,
    size_t raw_len,
    int8_t* decisions_out,
    float* scores_out,
    size_t max_transactions);
}
```

The hot path publishes Arrow batches instead of JSON:

- Arrow builders append into columnar memory.
- Audit, retraining, analytics, Velox, Spark, or cuDF can consume the same shape.
- Serialization overhead went from **3.1 ms to 0** in the draft metrics.

## 5.6 C++ Engine Results

| Metric | Python Baseline | C++ Hot-Path Engine |
|--------|-----------------|---------------------|
| P50 end-to-end | 8.2 ms | 1.8 ms |
| P99 end-to-end | 18.4 ms | 4.1 ms |
| Allocations/request | ~340 | 0 |
| Cross-NUMA accesses/request | ~2,800 | 0 |
| Serialization overhead | 3.1 ms | 0 |
| Inter-stage communication | 0.8 ms mutex queue | Less than 0.1 us SPSC |
| TLB miss rate | 4.2 percent | 0.15 percent |
| L1d cache miss rate | 8.7 percent | 1.2 percent |

---

# 6. Rust Gateway Control Plane

## 6.1 Why Rust Exists In The Architecture

Rust owns the control plane:

- Model-aware routing.
- Tenant-aware routing.
- Prefix-cache-aware routing.
- Circuit breaking.
- GPU-aware admission.
- SSE streaming and cancellation.
- Audit envelope publishing.

The C++ engine owns hot-path compute; Rust owns orchestration and safety.

## 6.2 Consistent Hashing For KV Locality

```rust
pub struct Backend {
    pub id: String,
    pub address: String,
    pub model: String,
    pub gpu_memory_free_mb: u64,
    pub active_requests: u32,
    pub is_healthy: bool,
}

let route_key = format!(
    "{}:{}:{}",
    req.tenant_id,
    req.model,
    req.prompt_prefix_hash
);
```

Design decisions:

- Use virtual nodes for smoother distribution.
- Key by tenant, model, and prompt prefix hash.
- Skip unhealthy backends with minimal movement around the ring.

Result: prefix cache hit rate improved from **5 percent to 72 percent**.

## 6.3 Circuit Breaker

```rust
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CircuitState {
    Closed,
    Open,
    HalfOpen,
}

pub async fn check_request(&self) -> Result<RequestPermit, CircuitOpen> {
    let state = *self.state.read().await;
    match state {
        CircuitState::Closed => Ok(RequestPermit { breaker: self }),
        CircuitState::Open => Err(CircuitOpen { retry_after: self.recovery_timeout() }),
        CircuitState::HalfOpen => Ok(RequestPermit { breaker: self }),
    }
}
```

Design decisions:

- Circuit breaker is per backend and per model.
- Trip on error rate, not just raw failure count.
- Half-open allows a controlled probe rather than a probe storm.
- Metrics feed Prometheus.

## 6.4 GPU-Aware Admission Control

```rust
pub struct AdmissionConfig {
    pub max_tokens_per_model: i64,
    pub max_tokens_per_tenant: i64,
    pub gpu_memory_headroom_mb: u64,
    pub gpu_utilization_cap: f32,
    pub kv_cache_pressure_threshold: f32,
}

if gpu_free < self.config.gpu_memory_headroom_mb {
    return AdmissionDecision::Reject {
        reason: "GPU memory below headroom threshold",
        retry_after_ms: 500,
    };
}
```

Design decisions:

- Budget by tokens, not requests.
- Use GPU memory and KV-cache pressure, not just queue depth.
- Degrade before rejecting high-priority traffic.
- Enforce per-tenant fairness.

## 6.5 Request Lifecycle

```text
admission control
  -> consistent hash route
  -> circuit breaker check
  -> backend call with SLO timeout
  -> record success/failure
  -> release token budget
  -> publish audit envelope
```

## 6.6 SSE Streaming And Cancellation

```rust
tokio::select! {
    Some(token) = backend_stream.next() => {
        if tx.send(token).await.is_err() {
            break;
        }
    }
    _ = cancel.cancelled() => {
        tracing::info!("client disconnected; aborting backend");
        break;
    }
}
```

Why it matters:

- Abandoned decode work wastes GPU time.
- Cancellation needs to propagate to the backend, not just close the client socket.
- Draft estimate: abandoned-request waste dropped from **about $490/day to about $12/day**.

## 6.7 Rust Gateway Results

| Metric | Python Flask Baseline | Rust Sentinel Gateway |
|--------|------------------------|-----------------------|
| Routing overhead p50 | 2.1 ms | 0.04 ms |
| Routing overhead p99 | 8.7 ms | 0.12 ms |
| Prefix cache hit rate | 5 percent | 72 percent |
| Circuit breaker recovery | N/A | Less than 30 sec |
| Max concurrent requests | ~200 | 50,000+ |
| Overload behavior | Cascading failure | Graceful 429/503 plus degrade |
| GPU waste from abandoned requests | ~$490/day | ~$12/day |
| Memory per connection | ~2 MB | ~8 KB |

---

# 7. Shared Memory, mmap, Arrow, And memmap2

## 7.1 Where Each Primitive Fits

| Primitive | Where Used | Why |
|-----------|------------|-----|
| `shm_open` | Rust gateway <-> C++ hot path | Shared buffers without socket or serialization |
| `mmap(MAP_SHARED)` | Shared memory regions | Map same physical pages into multiple processes |
| `mmap(MAP_PRIVATE)` | Model files from NVMe | Avoid heap copy and share page cache |
| `mbind` | Shared memory and model mappings | NUMA-pin pages to the right socket |
| `madvise(MADV_HUGEPAGE)` | Large mappings | Reduce TLB misses |
| `madvise(MADV_SEQUENTIAL)` | Model loading | Tell kernel access is linear |
| `MAP_POPULATE` | Model loading | Pre-fault pages before serving |
| `memmap2` | Rust Arrow consumer and model loader | Safer Rust abstraction over mmap |
| Arrow IPC | Audit/retraining/analytics boundary | Columnar, zero-copy record batches |
| DLPack/CUDA IPC | GPU tensor handoff | Avoid CPU bounce for tensor data |

## 7.2 Shared Region Layout

```cpp
struct HotPathSharedRegion {
    alignas(64) uint8_t input_buffer[4 * 1024 * 1024];
    alignas(64) uint64_t input_len;
    alignas(64) uint64_t request_count;

    alignas(64) int8_t decisions[8192];
    alignas(64) float scores[8192];
    alignas(64) uint64_t output_count;

    alignas(64) volatile uint64_t request_ready;
    alignas(64) volatile uint64_t response_ready;
};
```

## 7.3 C++ `shm_open` And `mmap`

```cpp
int fd = shm_open("/sentinel_hotpath", O_CREAT | O_RDWR, 0600);
ftruncate(fd, sizeof(HotPathSharedRegion));

void* ptr = mmap(
    nullptr,
    sizeof(HotPathSharedRegion),
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    fd,
    0);

madvise(ptr, sizeof(HotPathSharedRegion), MADV_HUGEPAGE);
memset(ptr, 0, sizeof(HotPathSharedRegion));
```

## 7.4 Rust `memmap2` Consumer

```rust
use memmap2::{MmapMut, MmapOptions};
use std::fs::OpenOptions;

pub struct ArrowShmConsumer {
    mmap: MmapMut,
    buffer_size: usize,
}

impl ArrowShmConsumer {
    pub fn open(shm_path: &str, buffer_size: usize) -> Result<Self, Box<dyn std::error::Error>> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(format!("/dev/shm/{}", shm_path))?;

        let mmap = unsafe {
            MmapOptions::new().len(buffer_size).map_mut(&file)?
        };

        Ok(Self { mmap, buffer_size })
    }
}
```

## 7.5 Model Loading With mmap

| Metric | `read()` Into Heap | mmap From NVMe |
|--------|--------------------|----------------|
| Memory copies | 1 page-cache-to-heap copy | 0 |
| RAM usage | 2x model size | 1x model size |
| 500 MB load time | ~1.2 sec | ~0.3 sec with `MAP_POPULATE` |
| Multiple processes | Separate copies | Shared physical pages |
| Pod restart | Full reload | Fast if pages remain cached |

## 7.6 Kubernetes `/dev/shm`

```yaml
volumes:
  - name: shm
    emptyDir:
      medium: Memory
      sizeLimit: "32Gi"
```

Use this when multiple containers in the same pod need high-throughput IPC or when PyTorch/CUDA uses shared memory for tensor or NCCL coordination.

---

# 8. Decision Matrix

| Decision | Chosen | Rejected/Deferred | Why |
|----------|--------|-------------------|-----|
| Hot path language | C++ for data plane, Rust for control plane | Python | Python added allocation, GIL, serialization, and blocking overhead |
| Guardrail architecture | Rust gateway plus Triton ensemble | Python guardrail microservices | Removes GIL, network hops, and CPU/GPU copy tax |
| LLM runtime | vLLM for variable shapes, TensorRT-LLM for stable buckets | One runtime for all traffic | Workload shape determines best runtime |
| GPU graphs | CUDA Graphs only for stable paths | Graph all shapes | Graph workspace can reduce KV cache and hurt throughput |
| FP8 | FP8 weights/KV plus BF16 attention when needed | FP8 everything | Long-context attention can lose numerical accuracy |
| Feature networking | AF_XDP plus SR-IOV | Full DPDK everywhere | DPDK was fastest but operationally heavy |
| Placement | K8s CPU/Topology/Memory Manager | Manual per-process `numactl` in pods | K8s should own production placement |
| Storage | Local NVMe and GDS for large model load | Object store on restart path | Object-store cold load caused minute-scale restarts |
| Audit format | Arrow IPC/shared memory | JSON/protobuf hot-path serialization | Arrow preserves columnar zero-copy data for consumers |
| Failure handling | Circuit breaker plus admission | Retries and accept-all queues | Retries amplify failure; admission protects finite GPU/KV budget |

---

# 9. Interview Quick Cards

## Card 1: One-Minute Story

"I designed a Sentinel-style fraud platform with a sub-10 ms hot path and a sub-200 ms GPU LLM path. The hot path used bare-metal K8s tuning, CPU isolation, NUMA-aware placement, SR-IOV/AF_XDP networking, C++ zero-copy feature extraction, XGBoost/rules, and Arrow audit publishing. The LLM path used a Rust gateway for tokenization, routing, admission, and cancellation, then a GPU-resident Triton ensemble for input guardrail, LLM inference, and output guardrail. The main wins were removing Python/serialization tax, preserving KV-cache locality, and making K8s placement match CPU/GPU/NIC topology."

## Card 2: Best "Why" Answers

| Question | Answer |
|----------|--------|
| Why Rust gateway? | "The gateway is control-plane heavy: routing, admission, cancellation, streaming, and fault isolation. Rust gives predictable memory and cheap async tasks without Python GIL." |
| Why C++ hot path? | "The fraud decision path needed explicit memory layout, NUMA control, SIMD, lock-free queues, and a stable C ABI." |
| Why Arrow? | "The hot path publishes once, and audit/retraining/analytics can read the same columnar bytes without rebuilding objects." |
| Why CUDA Graphs selectively? | "They remove launch overhead when shapes are stable, but they can consume workspace and reduce KV capacity." |
| Why Topology Manager? | "CPU pinning alone is incomplete; CPU, memory, GPU, and NIC must land on the same NUMA node." |
| Why AF_XDP over DPDK? | "DPDK was faster, but AF_XDP gave most of the latency win with much lower operational complexity." |
| Why Triton ensemble? | "It keeps guardrail and LLM tensors inside the GPU pipeline and removes Python hops between stages." |
| Why admission by tokens? | "A 4096-token request is not the same as a 256-token request; request count is the wrong unit for GPU/KV capacity." |

## Card 3: Troubleshooting Flow

```text
Symptom: p99 high
  -> Check traces for queue vs compute vs network.
  -> Check K8s for throttling, eviction, placement, disruption.
  -> Check DCGM/vLLM for GPU/KV pressure.
  -> Use Nsight Systems for CPU gaps, copies, NCCL, launch overhead.
  -> Use Nsight Compute only on identified hot kernels.
```

## Card 4: Metrics To Say Out Loud

- Hot-path p99: **35 ms -> 8.7 ms**.
- C++ hot-path p99: **18.4 ms -> 4.1 ms**.
- Routing p99: **8.7 ms -> 0.12 ms**.
- Prefix cache hit rate: **5 percent -> 72 percent**.
- LLM p99: **850 ms -> 185 ms**.
- Guardrail overhead: **40 ms -> 3 ms**.
- GPU decode utilization: **45 percent -> 78 percent**.
- Concurrent requests per H100: **32 -> 96**.
- Model load: **5 min -> 6 sec**.

## Card 5: What I Would Do Differently

| Area | Better Next Step |
|------|------------------|
| CUDA Graphs | Define shape buckets and workspace budgets earlier before broad graph enablement |
| K8s placement | Add automated topology validation tests to catch bad CPU/GPU/NIC placement |
| Guardrails | Standardize guardrail model contracts before building multiple serving paths |
| Shared memory | Add futex/eventfd signaling instead of spin-wait for lower CPU burn under sparse traffic |
| Observability | Ship p50/p95/p99 split by stage from day one, not after the first tail-latency incident |

---

# 10. Study Plan

## Week 1: GPU Serving Debugging

- Reproduce the eight CUDA/LLM tasks as mock interview scenarios.
- Practice explaining prefill vs decode, KV cache, CUDA Graphs, FP8, and NCCL topology.
- Memorize the "Nsight Systems before Nsight Compute" workflow.

## Week 2: Bare-Metal Kubernetes

- Draw the host vs K8s responsibility model.
- Practice writing a Guaranteed QoS GPU pod spec with CPU, memory, hugepages, GPU, `/dev/shm`, PDB, and PriorityClass.
- Explain why `numactl` is inspection/benchmarking, while production placement belongs to K8s policies.

## Week 3: Systems Programming

- Walk through the C++ NUMA arena, SIMD parser, SPSC queue, Arrow IPC, and C ABI.
- Walk through the Rust consistent hash, circuit breaker, admission controller, and SSE cancellation.
- Practice the phrase: "The boundary is a pointer handoff, not a serialization step."

## Week 4: Architecture Interview

- Draw the unified system from memory.
- Explain why the hot path and LLM path are isolated.
- Answer tradeoff questions: vLLM vs TensorRT-LLM, Triton ensemble vs custom co-location, AF_XDP vs DPDK, MIG vs full GPU, FP8 vs BF16.

