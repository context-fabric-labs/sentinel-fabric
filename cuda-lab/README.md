# CUDA Lab: KV-Style Data Movement

A hands-on CUDA performance lab focused on KV cache-like data movement operations.

## Overview

This lab explores CUDA performance optimization through a series of kernel implementations, from naive baseline to optimized versions. Each story includes:

1. **Baseline measurement** - Before optimization
2. **Optimization** - Implement improvement
3. **After measurement** - After optimization
4. **Nsight analysis** - Why it changed
5. **Write-up** - What we learned

## Kernel Family: Page Copy

We use **page copy** as our kernel family because:
- ✅ Simple to understand - copying data is intuitive
- ✅ Memory-bound - perfect for learning memory optimization
- ✅ KV-relevant - directly applicable to KV cache operations
- ✅ Scalable complexity - can add gather/strided access gradually
- ✅ Easy to verify - correctness is obvious

## Quick Start

### Prerequisites

- CUDA Toolkit 11.0+
- CMake 3.18+
- C++17 compiler (GCC 9+, Clang 10+)
- Google Test (for tests)
- Google Benchmark (optional)
- Nsight Compute (for analysis)

### Build

```bash
cd cuda-lab
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run Benchmarks

```bash
# Run default benchmark
./bench_page_copy

# Run with custom parameters
./bench_page_copy --page-size 65536 --num-pages 100
```

### Run Tests

```bash
ctest --output-on-failure
```

## Current Status (Story 3.2)

✅ Baseline naive page copy kernel  
✅ **Coalesced optimized kernel**  
✅ **Vectorized 16-byte copies**  
✅ **Benchmark comparison**  
✅ Correctness tests  
✅ Benchmark harness  
✅ Input/output descriptor model  
✅ Simple documentation  

## Kernel Progression

| Story | Kernel | Optimization Focus | Status |
|-------|--------|-------------------|--------|
| 3.1 | `page_copy_naive` | Baseline (no optimization) | ✅ Complete |
| 3.2 | `page_copy_coalesced` | Memory coalescing + vectorization | ✅ Complete |
| 3.3 | `page_copy_occupancy` | Occupancy/register optimization | Pending |
| 3.4 | `page_copy_tiled` | Shared memory/tiling | Pending |
| 3.5 | `page_copy_multi_stream` | Stream overlap | Pending |
| 3.6 | PyTorch extension | Framework integration | Pending |
| 3.7 | Analysis | Comprehensive write-up | Pending |

## File Structure

```
cuda-lab/
├── CMakeLists.txt
├── README.md
├── docs/
│   └── STORY_3.1_COMPLETE.md
├── src/
│   ├── kernels/
│   │   └── page_copy_naive.cu
│   └── benchmarks/
│       ├── bench_page_copy.cpp
│       └── bench_utils.hpp
├── tests/
│   └── test_kernels.cu
└── results/
    └── benchmark_results.json
```

## License

MIT License
