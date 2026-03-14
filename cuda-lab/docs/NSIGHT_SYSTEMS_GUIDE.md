# Nsight Systems Profiling Guide

## Overview

This guide explains how to use Nsight Systems to profile and analyze the page copy kernels.

## Prerequisites

1. **Nsight Systems** installed
   - Download from: https://developer.nvidia.com/nsight-systems
   - Version 2023.1 or later recommended

2. **Built benchmark executable**
   ```bash
   cd cuda-lab/build
   make -j$(nproc)
   ```

3. **CUDA-capable GPU**
   - Any NVIDIA GPU with compute capability 6.0+

## Quick Start

### Profile All Kernels

```bash
cd cuda-lab
./nsight/scripts/profile_page_copy.sh ./nsight/reports
```

### View Results

```bash
# GUI (recommended)
nsys-ui ./nsight/reports/page_copy_coalesced.nsys-rep

# Command line export
nsys export --type text ./nsight/reports/page_copy_coalesced.nsys-rep
```

## Profiling Scripts

### 1. profile_page_copy.sh

Profiles all kernel variants individually.

**Usage:**
```bash
./nsight/scripts/profile_page_copy.sh [output_dir]
```

**Output:**
- `page_copy_naive.nsys-rep`
- `page_copy_coalesced_scalar.nsys-rep`
- `page_copy_coalesced.nsys-rep`
- `page_copy_shared_mem.nsys-rep`
- `page_copy_shared_mem_vec.nsys-rep`

### 2. profile_multi_stream.sh

Profiles concurrent multi-stream execution.

**Usage:**
```bash
./nsight/scripts/profile_multi_stream.sh [output_dir]
```

**Output:**
- `multi_stream.nsys-rep`

### 3. profile_comparison.sh

Runs comparative benchmark and generates summary.

**Usage:**
```bash
./nsight/scripts/profile_comparison.sh [output_dir]
```

**Output:**
- `comparison_summary.json`

## What to Inspect in Timeline

### 1. Kernel Duration

**Where:** Timeline view → CUDA API → Kernel launches

**What to look for:**
- Kernel execution time (width of kernel bars)
- Compare naive vs coalesced vs shared_mem
- Expected: coalesced should be much faster

**Example observation:**
```
Naive kernel:        ~250 ms
Coalesced kernel:    ~5 ms  (50x faster!)
Shared mem kernel:   ~15 ms (slower than coalesced)
```

### 2. Memory Copy Operations

**Where:** Timeline view → CUDA API → cudaMemcpy

**What to look for:**
- Host-to-device copies (initialization)
- Device-to-host copies (verification)
- These should be minimal compared to kernel time

### 3. GPU Utilization

**Where:** Timeline view → GPU Utilization

**What to look for:**
- GPU should be busy during kernel execution
- Gaps indicate idle time
- Coalesced should show better utilization

### 4. Kernel Occupancy

**Where:** Timeline view → CUDA API → Kernel details

**What to look for:**
- Grid/block dimensions
- Registers per thread
- Shared memory per block
- Compare across kernel variants

### 5. Concurrent Execution (Multi-Stream)

**Where:** Timeline view → Streams

**What to look for:**
- Multiple kernels running concurrently
- Overlap between streams
- GPU utilization across streams

**Expected for multi-stream:**
```
Stream 0: [████████████]
Stream 1:   [████████████]
Stream 2:     [████████████]
Stream 3:       [████████████]
```

## Expected Findings

### Kernel Performance Comparison

| Kernel | Expected Duration | Relative Speed |
|--------|------------------|----------------|
| Naive | ~250 ms | 1.0x (baseline) |
| Coalesced Scalar | ~18 ms | 14x faster |
| Coalesced Vectorized | ~5 ms | 50x faster |
| Shared Mem | ~15 ms | 17x faster (but slower than coalesced) |
| Shared Mem Vec | ~12 ms | 21x faster (but slower than coalesced) |

### Timeline Observations

**Naive Kernel:**
- Long kernel duration
- Low GPU utilization
- Many small kernel launches

**Coalesced Kernel:**
- Short kernel duration
- High GPU utilization
- Fewer, larger kernel launches

**Shared Memory Kernel:**
- Medium duration
- Extra shared memory operations visible
- `__syncthreads()` barriers visible

### Multi-Stream Observations

**Good Overlap:**
- Kernels from different streams execute concurrently
- GPU utilization stays high
- Total time < sum of individual times

**Poor Overlap:**
- Kernels execute sequentially
- Gaps between kernels
- No benefit from multiple streams

## Command Line Analysis

### Export Timeline as Text

```bash
nsys export --type text page_copy_coalesced.nsys-rep > timeline.txt
```

### Extract Kernel Statistics

```bash
nsys stats --report gputrace page_copy_coalesced.nsys-rep
```

### Extract Memory Copy Statistics

```bash
nsys stats --report memcopy page_copy_coalesced.nsys-rep
```

### Generate CSV Report

```bash
nsys export --type csv page_copy_coalesced.nsys-rep > timeline.csv
```

## GUI Analysis Tips

### 1. Zoom to Region of Interest

- Use mouse wheel to zoom
- Drag to pan
- Double-click to reset

### 2. Filter Timeline

- Click "Filter" button
- Show only CUDA API calls
- Hide irrelevant events

### 3. Compare Multiple Reports

- File → Open → Select multiple .nsys-rep files
- Timeline will show all reports overlaid
- Useful for comparing kernel variants

### 4. Use Statistics Panel

- View → Statistics
- Shows aggregated metrics
- Good for quick comparison

## Common Issues

### Issue: "No CUDA events captured"

**Solution:**
- Ensure CUDA toolkit is installed
- Check that GPU is active
- Try running with `--trace=cuda,nvtx`

### Issue: "Permission denied"

**Solution:**
```bash
sudo modprobe nvidia
sudo nsys profile --help
```

### Issue: Profile file is very large

**Solution:**
- Reduce number of iterations
- Use `--backtrace=none`
- Filter traces: `--trace=cuda`

## Advanced Profiling

### Profile with NVTX Markers

Add NVTX markers to code:
```cpp
#include <nvtx3/nvToolsExt.h>

nvtxRangePush("Page Copy Kernel");
launch_page_copy_coalesced(...);
nvtxRangePop();
```

Then profile:
```bash
nsys profile --trace=cuda,nvtx ./bench_page_copy
```

### Profile Memory Transactions

For detailed memory analysis, use Nsight Compute instead:
```bash
ncu --set full --launch-skip 0 --launch-count 1 ./bench_page_copy --kernel coalesced
```

### Profile Power Consumption

If supported:
```bash
nsys profile --metrics=power ./bench_page_copy
```

## Interpretation Guidelines

### When Coalesced is Faster

**Look for:**
- Fewer memory transactions
- Better memory throughput
- Higher occupancy

**Why:**
- Coalesced access combines memory requests
- Vectorized operations (float4) reduce instructions
- Better GPU utilization

### When Shared Memory is Slower

**Look for:**
- Extra global→shared copies
- `__syncthreads()` barriers
- No data reuse

**Why:**
- Extra copy overhead
- Synchronization overhead
- No benefit without data reuse

### When Multi-Stream Helps

**Look for:**
- Concurrent kernel execution
- Overlapping memory copies
- High GPU utilization

**Why:**
- Hides kernel launch latency
- Better GPU utilization
- Overlaps memory transfers

## Summary Checklist

When analyzing a profile:

- [ ] Check kernel duration
- [ ] Compare across variants
- [ ] Look at GPU utilization
- [ ] Check memory copy operations
- [ ] Verify occupancy
- [ ] Look for concurrent execution (if multi-stream)
- [ ] Identify bottlenecks
- [ ] Compare with expected performance

## Next Steps

After profiling:

1. **Document findings** in timeline analysis report
2. **Compare with benchmarks** (timing should match)
3. **Identify optimization opportunities**
4. **Share insights** with team

## Resources

- [Nsight Systems Documentation](https://docs.nvidia.com/nsight-systems/)
- [CUDA Profiling Guide](https://docs.nvidia.com/cuda/profiler-users-guide/)
- [Nsight Systems User Guide](https://docs.nvidia.com/nsight-systems/UserGuide/)
