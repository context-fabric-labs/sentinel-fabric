# Day 2 — Prefix Caching and Serving Behavior

**Prerequisite:** Day 1 has a saved GPU-only baseline: topology, vLLM log, five-request TTFT/E2E data, GPU/I/O telemetry, and a stopped instance. Day 2 does not begin optimization until those artifacts are present.

## Mission

Measure whether vLLM automatic prefix caching improves a repeated-context workload, what it costs in GPU-resident KV capacity, and when it provides no benefit. This is a controlled comparison against Day 1, not a generic “faster is better” exercise.

The implementation remains inside `sentinel-fabric`. The vLLM endpoint remains a backend under study; do not change the existing control-plane routing policy until the backend behavior is measured.

## Hypotheses

1. Repeated long prompt prefixes reduce TTFT after the first request when prefix caching is enabled.
2. Unique prompts show little or no cache-reuse benefit.
3. Prefix reuse competes for the same GPU-resident KV capacity as active sequences; cache value depends on reuse probability, prompt length, and concurrency.
4. Steady-state inference remains largely free of model-file I/O once weights are resident; this is measured again, not assumed.
5. Session-aware routing is valuable only when a request can reach a replica holding reusable KV state.

## Build and study scope

```text
Client workload
  ├─ unique prompts        -> no intended reuse
  └─ shared long prefix    -> intended reuse
                              |
                        vLLM backend
                     prefix cache OFF / ON
                              |
                 L40S GPU-resident GDDR6 KV blocks
```

The L40S tier is GPU-resident GDDR6, not HBM. Day 2 studies application-visible cache behavior. It does not make claims about HBF maturity, SSD firmware, controller internals, or NAND behavior.

## Execution plan

1. Verify Day 1 artifacts and record the exact AMI, vLLM version, model revision, max model length, GPU-memory utilization, and command line.
2. Recreate or start the tagged Day 1 EC2 environment only for the active session. Keep model cache/results on EBS; treat local NVMe as ephemeral.
3. Run two server configurations with all other settings unchanged:

   ```bash
   # A: baseline, no reuse
   vllm serve Qwen/Qwen2.5-7B-Instruct \
     --served-model-name flashkv-qwen7b --dtype bfloat16 \
     --max-model-len 16384 --gpu-memory-utilization 0.90 \
     --no-enable-prefix-caching --host 127.0.0.1 --port 8000

   # B: automatic prefix caching enabled
   vllm serve Qwen/Qwen2.5-7B-Instruct \
     --served-model-name flashkv-qwen7b --dtype bfloat16 \
     --max-model-len 16384 --gpu-memory-utilization 0.90 \
     --enable-prefix-caching --host 127.0.0.1 --port 8000
   ```

4. For each configuration, run:

   - five unique-prompt requests;
   - one cold request containing a shared long prefix;
   - four warm requests with precisely the same prefix and different suffixes;
   - a small concurrency sweep (1, 4, and 8), keeping output tokens fixed.

5. Capture TTFT, E2E latency, output-token throughput, GPU utilization/memory, vLLM logs, and `iostat` for every run. Save raw data under `experiments/results/day02/`.
6. Inspect logs/metrics for allocated GPU KV blocks, cache hits/misses where available, preemptions, and maximum concurrency. Do not infer hits solely from faster responses.
7. Write `architecture/day02-prefix-cache-analysis.md`, explicitly separating measured facts from hypotheses.
8. Stop the instance and verify results are on EBS/S3.

## Metrics and decision table

| Dimension | Record | Decision use |
|---|---|---|
| TTFT | cold vs warm, p50/p95 | user-visible reuse value |
| E2E latency | all workload classes | end-to-end SLO impact |
| Throughput | output tokens/s | goodput effect |
| GPU memory | weights, KV/cache, peak | capacity trade-off |
| KV blocks / preemption | vLLM logs/metrics | cache pressure and safety |
| I/O | load versus inference | data-path validation |
| Reuse ratio | shared-prefix hit candidates / total | routing/admission value |

## Sentinel Fabric follow-on

If repeated-prefix benefit is demonstrated, add a narrowly scoped experiment under `experiments/` that sends a stable session identifier to the existing control plane and records selected backend, route reason, and expected reuse. Do not claim KV-aware routing until backend cache residency can be observed or reliably estimated.

## Completion criteria

- [ ] Day 1 artifacts validated before comparison
- [ ] Prefix OFF and ON runs use identical model/runtime limits
- [ ] Unique and repeated-prefix workloads captured
- [ ] Latency, throughput, GPU/KV, and I/O evidence saved
- [ ] Results distinguish cold from warm behavior
- [ ] Architecture analysis written with a capacity trade-off conclusion
- [ ] Instance stopped or terminated after persistent-copy verification
