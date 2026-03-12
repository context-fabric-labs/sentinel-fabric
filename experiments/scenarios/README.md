# Experiment Scenarios

This folder contains experiment configurations for benchmarking the control plane.

## Available Scenarios

1. **01_random_vs_sticky** - Compare random routing vs sticky routing
2. **02_kv_pressure_mix** - Low-KV vs high-KV workload mix
3. **03_gpu_headroom_stress** - GPU headroom stress with skew
4. **04_overload_bounded** - Overload test with bounded inflight
5. **05_unhealthy_pod** - Unhealthy/failing pod behavior

## Running Experiments

```bash
# Run all experiments
./scripts/run_all_experiments.sh

# Run specific experiment
./scripts/run_experiment.sh 01_random_vs_sticky

# View results
cat results/01_random_vs_sticky/results.json
```

## Experiment Structure

Each experiment has:
- `config.yaml` - Experiment configuration
- `run.sh` - Execution script
- `results.json` - Results output
- `analysis.md` - Analysis and findings

## Metrics to Observe

- **Latency** - p50, p95, p99
- **Throughput** - requests/second
- **Rejection rate** - % requests rejected
- **KV cache hit rate** - % requests with sticky routing
- **GPU utilization** - memory pressure per pod
- **Error rate** - % requests failing
