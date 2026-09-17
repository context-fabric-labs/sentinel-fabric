# ATMOS Digital Twin — System Proposal

**Project:** Sentinel Fabric ATMOS Digital Twin Simulator  
**Branch:** `atmos-digital-twin`  
**Date:** 2026-09-16  
**Author:** Sentinel Fabric Team  

---

## 1. Executive Summary

We extend Sentinel Fabric from an AI inference control/data plane into a **pre-silicon performance modeling platform** by building a deterministic discrete-event simulation engine that models the ATMOS modular inference card, its HBF/LPDDR memory hierarchy, NPU compute, DMA engines, and OEM PCIe topology.

**The key insight:** We do not predict "this request takes 4.3 ms." We simulate which resources a request needs, when they become available, how long each operation occupies them, and what blocks what. Latency and throughput **emerge** from resource occupancy, dependencies, arbitration, topology, and contention.

---

## 2. Problem Statement

The ATMOS card is a proposed modular inference card with **8 compute-and-memory modules** (HBF + NPU + LPDDR per module), ~1 TB HBF capacity, 200W class. Before silicon exists, we need to answer:

- How does adding ATMOS devices affect throughput under real OEM topologies?
- Where do bottlenecks emerge: NPU compute, HBF bandwidth, DMA, PCIe uplink?
- What is the optimal placement for KV cache: HBF or LPDDR?
- At what request concurrency does shared uplink contention dominate?
- How do different OEM server topologies affect multi-device scaling?

**Today:** Sentinel treats inference backends as opaque execution targets with fixed latency.  
**Goal:** Replace that with a deterministic virtual hardware system whose behavior emerges from simulation.

---

## 3. Architecture Overview

```
                         Sentinel Fabric
                               |
                    Rust control / scenario plane
                               |
             workload + model + topology + policy
                               |
                    C++ simulation data plane
                               |
                 Deterministic DES event core
                               |
       +-----------------------+----------------------+
       |                       |                      |
   Virtual NPU           Virtual Memory         Virtual I/O
   compute queues        HBF / LPDDR            DMA / PCIe
       |                       |                      |
       +-----------------------+----------------------+
                               |
                    OEM topology model
                               |
           E3.S bays / switches / uplinks /
         oversubscription / RC crossings / P2P
                               |
                       Metrics + traces
```

### Language Split

| Layer | Language | Responsibility |
|-------|----------|---------------|
| **Scenario orchestration** | Rust | Configuration, REST/gRPC API, experiment management, workload ingestion, topology ingestion, result database, visualization, parameter sweeps |
| **Simulation engine** | C++17 | Deterministic discrete-event engine, virtual hardware, queues, resource arbitration, model execution, memory transactions, DMA, PCIe topology, fast simulation |

---

## 4. Reuse from Existing Sentinel

The existing codebase provides substantial reuse:

### Control-Plane (Rust) Reuse

| Component | Reuse |
|-----------|-------|
| `Backend` trait | ATMOS simulation as a new backend variant |
| `PodScorer` | Extend with HBF headroom, NPU utilization, module transfer latency |
| `SelectionPolicy` | Add HBF residency as hard filter; tier-migration cost as soft penalty |
| `KvPressureEstimator` | Model ATMOS LPDDR working-set pressure separately |
| `AdmissionController` | Per-module + per-card aggregate inflight tracking |
| `RequestShape` | Add HBF-resident model size, context length, tier access pattern |
| `SessionMap` + HRW | Session→module affinity for KV cache locality |

### Data-Plane (C++) Reuse

| Component | Reuse |
|-----------|-------|
| `ITransport` interface | ATMOS transport for HBF→LPDDR→NPU pipeline |
| `BufferDescriptor` + `MemoryKind` | Extend with HBF, LPDDR, NPU_SRAM variants |
| `KvPageTable` | Map KV pages across HBF and LPDDR tiers |
| `CompletionQueue` | Track tier-migration operations |
| `Result<T>` error model | Unified error handling |

---

## 5. ATMOS Hardware Model

### Card Structure

```
ATMOS Card
├── Module 0
│   ├── HBF Controller (large model capacity tier)
│   ├── NPU (neural compute engine)
│   └── LPDDR (working memory / execution state)
├── Module 1
│   ├── HBF
│   ├── NPU
│   └── LPDDR
...
└── Module 7
    ├── HBF
    ├── NPU
    └── LPDDR
```

### Per-Module Resources (Finite Capacity)

```
VirtualNPU
├── command_queue (depth: N)
├── tensor_engines[] (count: T)
├── vector_engines[] (count: V)
├── load_store_engines[] (count: L)
├── local_sram (size: S bytes)
└── execution_slots[] (count: E)

HBFController
├── channels[] (count: C)
├── request_queues[] (per channel)
├── read_queue / write_queue
├── outstanding_limit
├── bandwidth_per_channel
└── arbitration_policy

LPDDRController
├── channels[] (count: C)
├── bank_groups[] (per channel)
├── request_queue
├── outstanding_limit
├── bandwidth_per_channel
└── refresh_overhead

DMAEngine
├── descriptor_queue (depth: D)
├── max_outstanding
├── channels[]
└── setup_overhead
```

---

## 6. OEM PCIe Topology Model

```
CPU Socket 0
    └── Root Complex 0
        └── PCIe Switch A
            ├── x8 → E3.S Bay 0 (ATMOS Module Group)
            ├── x8 → E3.S Bay 1
            └── x16 shared uplink → Root Complex

CPU Socket 1
    └── Root Complex 1
        └── PCIe Switch B
            ├── x8 → E3.S Bay 2
            └── x8 → E3.S Bay 3

P2P transfers may require:
  Device → Switch → RC → Host Interconnect → RC → Switch → Device
```

---

## 7. Fidelity Levels

| Level | Description | Use Case |
|-------|-------------|----------|
| **L0 — Analytical** | Bandwidth + base delay + queue capacity | Sanity bounds, pre-hardware |
| **L1 — Transaction** | Actual memory requests, DMA transfers, topology traversal, resource queues | **MVP target** |
| **L2 — Microtrace** | NPU instruction/micro-op stream, memory access stream | High fidelity, post-silicon |

---

## 8. Success Criteria

### MVP (Week 4)

- 1 CPU, 1 RC, 1 PCIe switch, 2 ATMOS E3.S devices
- Simplified transformer (prefill + decode + KV cache)
- Experiments: 1 device → 2 devices (ideal) → 2 devices (shared uplink)
- Demonstrate emergent bottleneck (PCIe uplink saturation)
- Result example: "4 devices provide only 2.7× throughput because shared PCIe uplink reaches 94% utilization"

### Engineering Simulator (Week 8)

- Multiple scheduling policies, topologies, devices
- Calibration against measured hardware
- Stall-reason attribution: WAIT_INPUT, WAIT_HBF, WAIT_DMA, WAIT_PCIE, WAIT_COMPUTE
- Sensitivity analysis

### Pre-Silicon Tool (Week 12-14)

- Automated design-space exploration
- Monte-Carlo parameter sweeps
- Confidence intervals on all predictions
- OEM topology comparison

---

## 9. Target Platform

**Development:** macOS (Apple Silicon) — no CUDA dependency for simulation engine  
**Deployment:** Linux for CI/CD and large-scale sweeps  
**No GPU required:** The simulation is CPU-bound discrete-event simulation, not GPU compute
