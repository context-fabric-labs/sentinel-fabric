# Nsight Compute Profiling Guide

## Overview

This guide explains how to use Nsight Compute (ncu) to analyze page copy kernel performance with detailed hardware counters.

## Prerequisites

1. **Nsight Compute** installed
   - Download from: https://developer.nvidia.com/nsight-compute
   - Version 2023.1 or later recommended

2. **Built benchmark executable**
   ```bash
   cd cuda-lab/build
   make -j$(nproc)
   ```

3. **CUDA-capable GPU**
   - Any NVIDIA GPU with compute capability 6.0+

4. **Permissions** (may require sudo for some metrics)
   ```bash
   sudo ncu --help
   ```

## Quick Start

### Profile All Kernels

```bash
cd cuda-lab
./nsight/scripts/profile_ncu.sh ./nsight/reports
```

### Profile Specific Kernel

```bash
./nsight/scripts/profile_ncu.sh ./nsight/reports coalesced
```

### View Results

```bash
# GUI (recommended)
ncu-ui ./nsight/reports/ncu_page_copy_coalesced.ncu-rep

# Command line
ncu --view ./nsight/reports/ncu_page_copy_coalesced.ncu-rep
```

## Profiling Scripts

### 1. profile_ncu.sh

Profiles kernels with Nsight Compute, collecting key metrics.

**Usage:**
```bash
./nsight/scripts/profile_ncu.sh [output_dir] [kernel_name]
```

**Metrics Collected:**
- Memory Workload Analysis
- Occupancy
- Cache Statistics
- Instruction Statistics

**Output:**
- `.ncu-rep` files for Nsight Compute GUI
- Log files with console output

### 2. compare_kernels.sh

Compares metrics across kernel variants.

**Usage:**
```bash
./nsight/scripts/compare_kernels.sh [reports_dir]
```

**Output:**
- `kernel_comparison.csv` with all metrics
- Summary table in terminal

## Key Metrics to Collect

### Memory Metrics

**Why:** Page copy is memory-bound, so memory metrics are most important.

**Key Counters:**
```
dram__throughput.avg.pct_of_peak_sustained_active
  - GPU DRAM throughput as % of peak
  - Higher is better
  - Coalesced should show 10-50x improvement

l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum
  - L1/TEX memory load transactions
  - Lower is better (fewer transactions)

l1tex__t_sectors_pipe_lsu_mem_global_op_st.sum
  - L1/TEX memory store transactions
  - Lower is better

l2__lu_read_txns
  - L2 read transactions
  - Lower indicates better coalescing

l2__lu_write_txns
  - L2 write transactions
  - Lower indicates better coalescing
```

**How to collect:**
```bash
ncu profile \
    --metrics dram__throughput.avg.pct_of_peak_sustained_active \
    --metrics l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum \
    --metrics l1tex__t_sectors_pipe_lsu_mem_global_op_st.sum \
    ./bench_page_copy --kernel coalesced
```

### Occupancy Metrics

**Why:** Shows how well GPU resources are utilized.

**Key Counters:**
```
sm__occupancy.avg
  - Average occupancy percentage
  - Higher can hide latency better
  - But doesn't always mean faster!

sm__warps_per_sm.avg
  - Average warps per SM
  - Higher indicates better parallelism

gpu__occupancy_emulated_acquired
  - Emulated occupancy
  - Theoretical maximum
```

**How to collect:**
```bash
ncu profile \
    --section Occupancy \
    ./bench_page_copy --kernel coalesced
```

### Cache Metrics

**Why:** Shows cache efficiency.

**Key Counters:**
```
l2__read_hit_rate.pct
  - L2 read hit rate percentage
  - Higher is better
  - Coalesced should show improvement

l2__write_hit_rate.pct
  - L2 write hit rate percentage
  - Higher is better

l1tex__data_pipe_lsu_lookups
  - L1 cache lookups
  - Indicates memory access patterns
```

**How to collect:**
```bash
ncu profile \
    --section CacheStats \
    ./bench_page_copy --kernel coalesced
```

### Instruction Metrics

**Why:** Shows instruction mix and efficiency.

**Key Counters:**
```
smsp__inst_executed
  - Total instructions executed
  - Lower is better for same work

smsp__threads_executed
  - Total threads executed
  - Should match expected thread count

smsp__inst_integer_executed
  - Integer instructions
  - Address calculations, etc.
```

**How to collect:**
```bash
ncu profile \
    --section InstructionStats \
    ./bench_page_copy --kernel coalesced
```

## Recommended Metric Set

For page copy analysis, use this metric set:

```bash
NCU_METRICS="\
    --metrics dram__throughput.avg.pct_of_peak_sustained_active \
    --metrics l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum \
    --metrics l1tex__t_sectors_pipe_lsu_mem_global_op_st.sum \
    --metrics l2__lu_read_txns \
    --metrics l2__lu_write_txns \
    --metrics sm__occupancy.avg \
    --metrics sm__warps_per_sm.avg \
    --metrics smsp__inst_executed"
```

## Comparison Methodology

### Step 1: Profile All Kernels

```bash
./nsight/scripts/profile_ncu.sh ./nsight/reports all
```

### Step 2: Generate Comparison

```bash
./nsight/scripts/compare_kernels.sh ./nsight/reports
```

### Step 3: Analyze Key Differences

**Compare:**
1. Memory throughput (dram__throughput)
2. Memory transactions (l1tex, l2)
3. Occupancy (sm__occupancy)
4. Instructions (smsp__inst_executed)

### Step 4: Interpret Results

**Naive vs Coalesced:**
- Expect 10-50x throughput improvement
- Expect fewer memory transactions
- Expect better occupancy

**Coalesced vs Shared Mem:**
- Expect shared mem to be slower
- Look for extra shared memory operations
- Check for synchronization overhead

## Bandwidth-Bound vs Latency-Bound

### Identifying Bandwidth-Bound Kernels

**Signs:**
- High DRAM throughput (>50% of peak)
- Low L2 hit rate
- Many memory transactions
- Kernel time dominated by memory access

**For Page Copy:**
- **Naive:** Bandwidth-bound (terrible coalescing)
- **Coalesced:** Still bandwidth-bound but much better
- **Shared Mem:** Bandwidth-bound with extra overhead

**Optimization Strategy:**
- Improve memory coalescing
- Reduce memory transactions
- Use vectorized loads/stores (float4)

### Identifying Latency-Bound Kernels

**Signs:**
- Low DRAM throughput (<20% of peak)
- High occupancy needed to hide latency
- Many dependent instructions
- Kernel time dominated by instruction latency

**For Page Copy:**
- Not latency-bound (simple copy operation)

**Optimization Strategy:**
- Increase occupancy
- Reduce instruction dependencies
- Use more threads

## Occupancy Interpretation

### What Occupancy Tells You

**High Occupancy (>50%):**
- Many active warps
- Good for hiding latency
- Doesn't guarantee performance

**Low Occupancy (<25%):**
- Few active warps
- May limit performance
- Could be register-bound or shared-memory-bound

### For Page Copy Kernels

**Naive:**
- Occupancy: Very low (~3%)
- Reason: 1 thread = 1 byte (terrible work per thread)
- Impact: Can't hide memory latency

**Coalesced:**
- Occupancy: Better (~25-50%)
- Reason: 1 thread = 16 bytes (better work per thread)
- Impact: Better latency hiding

**Shared Mem:**
- Occupancy: Medium (~25%)
- Reason: Shared memory limits block size
- Impact: Extra overhead without benefit

### When Occupancy Matters

**Important for:**
- Latency-bound kernels
- Kernels with memory latency
- Kernels with instruction dependencies

**Less important for:**
- Bandwidth-bound kernels (like page copy)
- Simple memory copy operations
- Kernels already at high occupancy

## Register Usage Interpretation

### What Register Usage Tells You

**High Register Usage (>32 registers/thread):**
- Complex per-thread computation
- May limit occupancy
- Could cause register spilling

**Low Register Usage (<16 registers/thread):**
- Simple per-thread computation
- Allows higher occupancy
- Generally good for memory-bound kernels

### For Page Copy Kernels

**Expected:**
- Naive: ~16-24 registers (simple loop)
- Coalesced: ~20-28 registers (vectorized ops)
- Shared Mem: ~24-32 registers (shared mem indexing)

### Register Spilling

**Signs:**
- `gld__transfers_from_sysmem` high
- `gmem__read_transactions` very high
- Performance much worse than expected

**Solution:**
- Reduce register usage
- Simplify per-thread computation
- Use `--maxrregcount` compiler flag

## Shared Memory Interpretation

### What Shared Memory Tells You

**High Usage:**
- Data reuse across threads
- Explicit caching by programmer
- Can improve performance if reused

**Low/No Usage:**
- No explicit shared memory
- Relying on L1/L2 cache
- Fine if no data reuse

### For Page Copy Kernels

**Expected:**
- Naive/Coalesced: No shared memory
- Shared Mem: 4KB per block (4096 bytes)

**Why Shared Mem Doesn't Help:**
- No data reuse (read once, write once)
- Extra copy overhead (global→shared→global)
- Synchronization overhead (`__syncthreads()`)

## Command Line Examples

### Profile with Full Metrics

```bash
ncu profile \
    --section MemoryWorkloadAnalysis \
    --section Occupancy \
    --section CacheStats \
    --section InstructionStats \
    --launch-skip 0 \
    --launch-count 1 \
    --output ./nsight/reports/full_profile \
    ./bench_page_copy --kernel coalesced
```

### Profile Specific Metric

```bash
ncu profile \
    --metrics dram__throughput.avg.pct_of_peak_sustained_active \
    --output ./nsight/reports/throughput \
    ./bench_page_copy --kernel coalesced
```

### Compare Two Kernels

```bash
# Profile both
ncu profile --output ./nsight/reports/naive ./bench_page_copy --kernel naive
ncu profile --output ./nsight/reports/coalesced ./bench_page_copy --kernel coalesced

# Compare in GUI
ncu-ui ./nsight/reports/naive.ncu-rep ./nsight/reports/coalesced.ncu-rep
```

### Export to CSV

```bash
ncu --csv --view ./nsight/reports/coalesced.ncu-rep > metrics.csv
```

## GUI Analysis Tips

### 1. Use Comparison View

- File → Open → Select multiple reports
- Timeline shows all reports overlaid
- Metrics panel shows comparison table

### 2. Focus on Key Sections

- **Memory Workload Analysis** - Throughput, transactions
- **Occupancy** - Active warps, limits
- **Cache Stats** - Hit rates, lookups
- **Instruction Stats** - Instruction mix

### 3. Use Search

- Ctrl+F to search for specific metrics
- Search for "dram", "l2", "occupancy", etc.

### 4. Export Data

- File → Export → CSV
- Useful for creating custom charts

## Common Issues

### Issue: "Permission denied"

**Solution:**
```bash
sudo ncu profile ...
```

Or configure permissions:
```bash
sudo ncu-config set enable_profiling 1
```

### Issue: "No kernels found"

**Solution:**
- Ensure kernel is actually launched
- Use `--target-processes all` for child processes
- Check that benchmark runs correctly

### Issue: Profile is very large

**Solution:**
- Reduce number of sections
- Use `--metrics` instead of `--section`
- Profile fewer launches

### Issue: Metrics not available

**Solution:**
- Some metrics require specific GPU architectures
- Use `ncu --list-metrics` to see available metrics
- Fall back to sections instead of specific metrics

## Advanced Profiling

### Profile with Source Import

```bash
ncu profile \
    --import-source yes \
    --output ./nsight/reports/with_source \
    ./bench_page_copy --kernel coalesced
```

### Profile Multiple Launches

```bash
ncu profile \
    --launch-skip 0 \
    --launch-count 10 \
    --output ./nsight/reports/multi_launch \
    ./bench_page_copy --kernel coalesced
```

### Profile with Custom Timeout

```bash
ncu profile \
    --timeout 300 \
    --output ./nsight/reports/timeout \
    ./bench_page_copy --kernel coalesced
```

## Summary Checklist

When analyzing with Nsight Compute:

- [ ] Profile all kernel variants
- [ ] Collect memory metrics (throughput, transactions)
- [ ] Collect occupancy metrics
- [ ] Collect cache metrics (L2 hit rate)
- [ ] Collect instruction metrics
- [ ] Generate comparison CSV
- [ ] Identify bandwidth-bound vs latency-bound
- [ ] Interpret occupancy correctly
- [ ] Check register usage
- [ ] Check shared memory usage
- [ ] Document findings

## Next Steps

After profiling:

1. **Document findings** in analysis report
2. **Compare with benchmarks** (timing should match)
3. **Identify bottlenecks** (memory, occupancy, registers)
4. **Plan optimizations** based on findings
5. **Share insights** with team

## Resources

- [Nsight Compute Documentation](https://docs.nvidia.com/nsight-compute/)
- [CUDA Profiling Guide](https://docs.nvidia.com/cuda/profiler-users-guide/)
- [Nsight Compute Metrics Reference](https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#metrics)
