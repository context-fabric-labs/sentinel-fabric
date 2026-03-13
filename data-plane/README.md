# PM-NIXL: Poor-man's NIXL

A minimal C++/CUDA transfer library for GPU memory operations, inspired by NIXL concepts but drastically simplified for learning.

## Goals

- **Educational**: Learn CUDA runtime APIs, async copies, and memory management
- **Practical**: Real working code with benchmarks
- **Minimal**: No plugin frameworks or broad abstractions
- **Benchmark-first**: Every feature includes performance measurements

## Building

### Prerequisites

- CMake 3.18+
- CUDA Toolkit 11.0+
- C++17 compiler (GCC 9+, Clang 10+)
- Google Test (for tests)
- Google Benchmark (optional, for benchmarks)

### Build Commands

```bash
cd data-plane
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build with Benchmarks

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
make -j$(nproc)
```

### Run Tests

```bash
ctest --output-on-failure
```

## Project Structure

```
data-plane/
├── CMakeLists.txt
├── README.md
├── include/pm_nixl/
│   ├── error.hpp           # Error handling
│   ├── descriptor.hpp      # Buffer descriptors
│   └── buffer.hpp          # Buffer management
├── src/
│   ├── error.cpp
│   ├── descriptor.cpp
│   └── buffer.cpp
├── tests/
│   └── test_buffer.cpp
└── bench/
    └── bench_buffer.cpp
```

## Quick Start

```cpp
#include <pm_nixl/buffer.hpp>
#include <pm_nixl/descriptor.hpp>

using namespace pm_nixl;

int main() {
    // Create a host buffer
    Buffer host_buffer(MemoryKind::HostPinned, 1024);
    
    // Create a device buffer
    Buffer device_buffer(MemoryKind::CudaDevice, 1024);
    
    // Get descriptors
    auto host_desc = host_buffer.descriptor();
    auto device_desc = device_buffer.descriptor();
    
    // Descriptors can be passed to transfer engines
    // (to be implemented in future stories)
    
    return 0;
}
```

## Current Status (Story 2.3)

✅ Buffer descriptor types  
✅ Memory kind classification (HostPinned, HostPageable, CudaDevice)  
✅ Buffer registration metadata  
✅ RAII-oriented ownership  
✅ Async transfer engine  
✅ CUDA stream management  
✅ Event-based completion  
✅ H2D and D2H transfers  
✅ **Transfer benchmark harness**  
✅ **Size sweep benchmarks**  
✅ **Pinned vs pageable comparison**  
✅ **CSV/JSON output**  
✅ Unit tests  

## Next Steps

- Story 2.4: KV page copy primitives
- Story 2.5: Page gather/compact operations

## License

MIT License
