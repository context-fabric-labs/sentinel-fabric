# Day 0 — What We Are Building and Studying

**Target date:** August 6, 2026 (14 days)

## The build

We are extending the existing **`sentinel-fabric`** project as the sole codebase for this work. We will not create a separate `flashkv-fabric` repository. **FlashKV Fabric** is the name of the experimental architecture and benchmark program implemented within `sentinel-fabric`.

The current project provides the starting point for a measurable LLM-serving laboratory and KV-aware serving architecture: its Rust control plane is the place for routing, admission, session, and observability work; its C++/CUDA data plane and CUDA lab are the place for transfer-path and kernel experiments; and its experiments area is the place for reproducible scenarios and results.

Its purpose is to understand how requests, model data, and KV-cache state move through a memory-and-storage hierarchy:

```text
Persistent repository (EBS/S3)
          -> host DRAM / Linux page cache
          -> PCIe
          -> GPU-resident memory
          -> vLLM model weights + active KV cache

Local NVMe: experimental warm/cold cache tier for model and KV-related data
```

The initial runtime is vLLM. The initial model is Qwen2.5-7B-Instruct. LMCache is the starting point for later KV offload experiments. We will connect these measurements and integrations to the existing Sentinel Fabric control plane, evolving its session stickiness, KV-aware routing, admission control, and observable decisions.

The laboratory uses AWS EC2, ideally an L40S-based `g6e.2xlarge`, EBS, and local instance NVMe. The L40S has GPU-resident **GDDR6**, not HBM. We will use it to learn tiering methodology, while keeping HBM and Sandisk’s prospective HBF (high-bandwidth flash) technically distinct.

## Cost-optimized operating model

GPU compute is deliberately disposable. We will launch a tagged GPU instance only for an active laboratory session, stop it immediately when work ends, and terminate it when its persistent state has been copied and the next session can recreate it. A stopped instance stops GPU/vCPU charges but continues to incur EBS charges; a terminated instance removes compute entirely and can remove its root volume if configured to delete on termination.

The durable source of truth is Git plus persistent EBS and, for final artifacts, S3. Local instance NVMe is an ephemeral performance tier only: it may disappear on stop, terminate, host failure, or replacement. No unique source, model result, benchmark result, or configuration belongs only on local NVMe.

Each session must be reproducible from an explicit launch configuration: region, AMI, instance type, EBS size, tags, security group, instance profile, bootstrap commands, model ID, and benchmark command. We will favor On-Demand for short, first-time baselines and use Spot only after interruption handling and artifact persistence have been validated. Budget alerts are guardrails, not automatic shutdown.

## What we are studying

### LLM inference and KV cache

- Prefill versus decode behavior
- Model weights versus KV-cache capacity
- KV-cache bytes per token, context length, and concurrency
- GQA versus full multi-head attention effects on KV size
- vLLM PagedAttention, continuous batching, blocks, and preemption
- Prefix caching and when reuse improves or harms the baseline
- GPU-only KV cache, then CPU DRAM and local-NVMe offload
- Cache admission, eviction, promotion, and demotion

### Data path, storage, and systems behavior

- Model download and model-load behavior from persistent storage
- Linux filesystem/page-cache behavior and host-DRAM role
- Local NVMe performance, queueing, write volume, and tail latency
- PCIe/NUMA/GPU topology and data-movement implications
- GPU idle time during loading, prefill, decode, and storage pressure
- Application-visible storage economics: capacity, latency, bandwidth, power proxies, and endurance implications

We will not claim visibility into SSD controller firmware, NAND internals, or Sandisk-specific hardware from AWS instance storage. We will measure Linux-visible and application-visible behavior only.

### HPC-style serving and multi-tenancy

- TTFT, end-to-end latency, token throughput, and SLO goodput
- Queueing, backpressure, token admission, and priority lanes
- Noisy-neighbor control and tail-latency protection
- Session affinity for KV reuse
- KV-aware and GPU-headroom-aware routing
- Concurrent model loading and KV-cache traffic
- Cost per million tokens and tokens per GPU-hour

## The progression to the target date

| Phase | Outcome |
|---|---|
| Establish | Add reproducible GPU-only vLLM baseline assets to `sentinel-fabric`; capture topology, capacity math, TTFT, throughput, GPU and I/O telemetry |
| Explain | Tie measured results to prefill/decode, memory headroom, vLLM block capacity, and storage path behavior |
| Extend | Implement and compare prefix reuse, then CPU and NVMe cache tiers within the project where the tooling supports them |
| Control | Extend and exercise the existing Sentinel Fabric control plane for admission policies, session affinity, and KV-aware routing |
| Stress | Add multi-tenant/context/concurrency/model-load scenarios under `experiments/` and identify SLO/tail-latency boundaries |
| Synthesize | Keep architecture artifacts, benchmark results, economics, HBF workload requirements, and executive-ready conclusions in `sentinel-fabric/docs` and `sentinel-fabric/experiments` |

## Success at the target date

By August 6, we should be able to demonstrate—not merely describe—a repeatable comparison of GPU-only, prefix-reuse, and offloaded KV approaches; explain their effects on latency, capacity, GPU use, storage traffic, and cost; and articulate which workload requirements a future HBF tier would need to meet.

The central question is: **for a given context-length and concurrency distribution, which placement and routing policy delivers the required SLO at the best capacity, performance, and cost trade-off?**
