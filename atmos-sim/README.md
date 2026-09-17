# ATMOS Digital Twin Simulator

A deterministic discrete-event simulation engine for pre-silicon performance modeling of the ATMOS modular inference card and OEM PCIe topologies.

## Architecture

```
Rust Control Plane (scenario, API, experiment management)
        │
        ▼
C++ Simulation Engine (DES core, virtual hardware, topology)
        │
        ▼
Virtual Hardware Model (NPU, HBF, LPDDR, DMA, PCIe)
        │
        ▼
OEM Topology (E3.S bays, switches, root complexes)
```

## Building

### Prerequisites

- CMake 3.20+
- C++17 compiler (Clang 14+ on macOS, GCC 11+ on Linux)
- Rust 1.70+ (for control plane)
- Google Test (fetched automatically by CMake)

### Build C++ Simulation Engine

```bash
cd atmos-sim
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

### Build Rust Control Plane

```bash
cd atmos-sim/control-plane
cargo build
cargo test
```

## Project Structure

```
atmos-sim/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── PROPOSAL.md
│   └── EPICS_AND_STORIES.md
├── include/atmos_sim/
│   ├── sim_core/          # DES engine, events, resources
│   ├── virtual_devices/   # NPU, HBF, LPDDR, DMA, PCIe
│   ├── topology/          # OEM topology graph
│   ├── model_graph/       # Workload representation
│   ├── metrics/           # Measurement + traces
│   └── calibration/       # Parameter store + sensitivity
├── src/
│   ├── sim_core/
│   ├── virtual_devices/
│   ├── topology/
│   ├── model_graph/
│   ├── metrics/
│   └── calibration/
├── tests/
│   ├── test_sim_core.cpp
│   ├── test_virtual_devices.cpp
│   ├── test_topology.cpp
│   ├── test_model_graph.cpp
│   ├── test_metrics.cpp
│   ├── test_golden_traces.cpp
│   └── test_e2e.cpp
├── configs/
│   └── topologies/        # YAML topology definitions
└── control-plane/         # Rust scenario orchestration
```

## Quick Start

```cpp
#include <atmos_sim/sim_core/sim_engine.h>
#include <atmos_sim/virtual_devices/atmos_module.h>
#include <atmos_sim/topology/topology_graph.h>

// Build topology
auto topology = TopologyGraph::from_yaml("configs/topologies/simple.yaml");

// Create simulation engine
SimEngine engine;

// Register virtual devices
auto module = std::make_shared<AtmosModule>(AtmosConfig{});
engine.register_device(module);

// Create workload
auto workload = WorkloadGenerator::from_config(workload_config);

// Run simulation
auto result = engine.run(workload, topology);

// Analyze
std::cout << "Throughput: " << result.throughput_tokens_per_sec() << " tok/s\n";
std::cout << "P99 latency: " << result.p99_latency_ns() << " ns\n";
```

## Fidelity Levels

| Level | Description | Status |
|-------|-------------|--------|
| L0 — Analytical | Bandwidth + base delay | ✅ Implemented |
| L1 — Transaction | Memory requests, DMA, topology traversal | ✅ MVP Target |
| L2 — Microtrace | NPU instruction stream | 🔜 Future |

## License

Internal — Sentinel Fabric Team
