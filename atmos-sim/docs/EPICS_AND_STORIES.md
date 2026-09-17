# ATMOS Digital Twin — Epics & Stories

**Branch:** `atmos-digital-twin`  
**Development Model:** Solo + Copilot  
**Target Platform:** macOS (initial), Linux (CI/CD)

---

## Epic 1: Simulation Core (DES Engine)

**Goal:** Deterministic discrete-event simulation kernel with resource modeling  
**Duration:** Week 1  

### Story 1.1: Event System + SimClock
- `SimTime` (uint64_t nanoseconds, no floating point)
- `Event` struct (timestamp, sequence, type, target, payload)
- `EventQueue` (deterministic priority queue: timestamp → sequence)
- `SimClock` (virtual time, advance, no wall-clock dependency)
- Unit tests: ordering, determinism, priority

### Story 1.2: Resource Abstractions
- `Resource` base class (id, capacity, occupancy, state)
- `ResourcePool` (fixed-capacity pool with acquire/release events)
- `BoundedQueue` (finite-capacity FIFO with enqueue/dequeue events)
- `Arbiter` (round-robin, weighted, priority policies)
- Unit tests: capacity limits, blocking, ordering

### Story 1.3: Dependency Tracker
- `DependencyTracker` (DAG-based dependency resolution)
- `DependencyState` (PENDING, READY, SATISFIED)
- Event-driven dependency satisfaction
- Unit tests: DAG traversal, partial satisfaction, cycles detection

### Story 1.4: SimEngine Orchestrator
- `SimEngine` (event loop, resource registry, clock)
- `run_until(SimTime)` / `run_until_empty()`
- `SimResult` (events processed, final time, resource utilization)
- Deterministic replay: same inputs = same outputs
- Unit tests: basic simulation, replay determinism

### Story 1.5: Metrics + Trace Recorder
- `MetricsCollector` (counters, gauges, histograms)
- `TraceRecorder` (timestamped event log)
- Resource utilization tracking
- Unit tests: metric accumulation, trace ordering

---

## Epic 2: Virtual ATMOS Device

**Goal:** Model ATMOS module components as finite-capacity resources  
**Duration:** Week 2  

### Story 2.1: Virtual NPU
- `VirtualNPU` with command queue, tensor/vector engines, load-store engines, local SRAM
- `ComputeOp` (flops, bytes_read, bytes_write, dependencies, compute_class)
- Resource acquisition: input_ready → engine_available → sramp_available → execute
- Unit tests: blocking on dependencies, engine contention

### Story 2.2: HBF Controller
- `HBFController` with channels, request queues, read/write arbitration
- `MemoryTransaction` (address, size, read/write, channel)
- Channel contention, outstanding request limits
- Level 0 model: bandwidth + protocol delay + queue capacity + outstanding limits
- Unit tests: channel contention, queue full, bandwidth limiting

### Story 2.3: LPDDR Controller
- `LPDDRController` with channels, bank groups, refresh overhead
- Read/write arbitration, turnaround penalties
- Level 0 model initially, Level 1 hooks for later
- Unit tests: read/write contention, refresh stalls

### Story 2.4: DMA Engine
- `DMAEngine` with descriptor queue, outstanding limits, channel scheduling
- `DMATransfer` (src, dst, size, descriptor_id)
- Setup overhead, multi-engine coordination
- Unit tests: descriptor queue depth, outstanding limits, setup overhead

### Story 2.5: PCIe Endpoint
- `PCIeEndpoint` (lane count, generation, credit-based flow control)
- Packetization overhead, credit limits
- Unit tests: credit exhaustion, bandwidth limiting

### Story 2.6: ATMOS Module Assembly
- `AtmosModule` (NPU + HBF + LPDDR + DMA[] + PCIe endpoint)
- Module-level resource coordination
- Inter-module resource dependency resolution
- Unit tests: module-level compute + memory flow

---

## Epic 3: PCIe / OEM Topology

**Goal:** Model real server topologies with switches, root complexes, shared links  
**Duration:** Week 3  

### Story 3.1: Topology Graph
- `TopologyNode` (RootComplex, PCIeSwitch, PCIeEndpoint, HostMemory)
- `PCIeLink` (bandwidth, lane_count, max_outstanding, queue_depth, arbitration)
- Graph construction from YAML/JSON config
- Unit tests: graph construction, path finding

### Story 3.2: Switch Arbitration + Contention
- `PCIeSwitch` with ingress queues, uplink arbitration, port mapping
- Contention modeling: multiple endpoints sharing uplink
- Unit tests: uplink saturation, fairness, queue overflow

### Story 3.3: Root Complex + Host Memory
- `RootComplex` (aggregate bandwidth, host memory interface)
- `HostMemoryController` (bandwidth, transaction queue)
- Multi-RC topologies with host interconnect
- Unit tests: RC bandwidth limits, multi-device aggregation

### Story 3.4: P2P Transfers
- Device-to-device path traversal through topology graph
- Path cost calculation (hops, switches, shared links)
- Unit tests: direct P2P, switch-mediated, RC-crossing

### Story 3.5: Topology YAML Config
- Parse OEM server topology from YAML
- E3.S bay mapping, switch hierarchy, link configurations
- Validation + error reporting
- Unit tests: config parsing, validation, error cases

---

## Epic 4: Model Graph + Workload

**Goal:** Represent LLM workloads as executable simulation DAGs  
**Duration:** Week 3-4  

### Story 4.1: Simulation IR (SimIR)
- `SimOp` (id, compute_requirement, memory_reads, memory_writes, dependencies, placement)
- `ModelGraph` (DAG of SimOps with metadata)
- Serialization: JSON-based SimIR format
- Unit tests: DAG construction, serialization round-trip

### Story 4.2: Transformer Graph Compiler
- Compile transformer layer structure into SimIR
- Support: RMSNorm, QKV projection, attention, KV read/write, MLP, residual
- Configurable: layer count, hidden size, head count, FFN size
- Unit tests: graph structure validation, op count, dependency correctness

### Story 4.3: Request Generator
- `RequestGenerator` from trace files or statistical distributions
- `RequestShape` (prompt_tokens, max_output_tokens, arrival_time, priority)
- Poisson / uniform / burst arrival patterns
- Unit tests: distribution properties, determinism with seed

### Story 4.4: Workload Scheduler
- Schedule model graph ops onto virtual hardware resources
- Prefill vs decode phase handling
- KV cache placement strategy (HBF vs LPDDR)
- Unit tests: scheduling correctness, phase transitions

---

## Epic 5: Metrics + Calibration

**Goal:** Measurement, calibration, and confidence estimation  
**Duration:** Week 5-6  

### Story 5.1: Simulation Metrics
- Throughput (tokens/s), TTFT, TPOT, P50/P95/P99
- Resource utilization per component
- Queue occupancy histograms
- Unit tests: metric correctness, percentile calculation

### Story 5.2: Stall Attribution
- Per-operation stall reason: WAIT_INPUT, WAIT_HBF, WAIT_LPDDR, WAIT_DMA, WAIT_PCIE, WAIT_COMPUTE, WAIT_DEPENDENCY, WAIT_SCHEDULER
- Aggregate stall breakdown per request phase
- Unit tests: attribution correctness

### Story 5.3: Calibration Database
- `CalibrationParameter` (nominal, low, high, source, confidence)
- Parameter sweep engine
- Sensitivity analysis: "Does conclusion change if HBF efficiency is ±15%?"
- Unit tests: parameter storage, sweep generation

### Story 5.4: Uncertainty Propagation
- Monte-Carlo parameter variation
- Confidence intervals on output metrics
- "P99 estimate: 7.2–9.1 ms, dominant sensitivity: PCIe uplink arbitration"
- Unit tests: confidence interval calculation, sensitivity ranking

---

## Epic 6: Rust Control Plane + API

**Goal:** Scenario orchestration, REST API, experiment management  
**Duration:** Week 6-7  

### Story 6.1: Simulation Backend Trait
- `SimulationBackend` implementing Sentinel's `Backend` trait
- FFI bridge to C++ simulation engine
- Unit tests: backend lifecycle, request forwarding

### Story 6.2: Scenario Configuration
- YAML-based scenario definition (topology, workload, model, policy)
- Scenario validation + defaults
- Unit tests: config parsing, validation

### Story 6.3: Experiment Runner API
- `POST /sim/experiments` — create and run experiment
- `GET /sim/experiments/:id` — get results
- `GET /sim/experiments/:id/metrics` — detailed metrics
- `POST /sim/sweep` — parameter sweep
- Unit tests: API endpoints, result format

### Story 6.4: ATMOS Topology + Device Config
- ATMOS card configuration (module count, HBF size, NPU config)
- E3.S bay mapping
- Unit tests: config validation, defaults

---

## Epic 7: Integration + End-to-End Tests

**Goal:** Full pipeline validation, golden traces, demo experiments  
**Duration:** Week 7-8  

### Story 7.1: Golden Trace Validation
- Hand-calculated 20-event traces
- Exact timestamp verification
- Regression suite
- Unit tests: trace matching

### Story 7.2: Single-Device E2E
- 1 ATMOS device, simplified transformer, basic workload
- Validate compute/memory/DMA flow
- Unit tests: end-to-end correctness

### Story 7.3: Multi-Device Scaling Experiments
- 1 → 2 → 4 ATMOS devices
- Shared uplink contention demonstration
- Scaling factor measurement
- Unit tests: scaling results

### Story 7.4: OEM Topology Comparison
- Topology A (independent links) vs Topology B (shared uplink)
- Same workload, different results from topology alone
- Unit tests: topology-driven result differences

### Story 7.5: Build System + CI
- CMake for C++ (macOS compatible, no CUDA dependency for sim)
- Cargo for Rust
- GitHub Actions CI
- Unit test execution in CI
