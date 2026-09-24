# ATMOS Digital Twin Emulator — System Proposal

**Project:** ATMOS Digital Twin Emulator
**Date:** 2026-09-17
**Audience:** Executive management, product, architecture, silicon, firmware, platform software, OEM engineering, performance, and POC teams
**Decision requested:** Approve a focused emulator workstream that can guide ATMOS product choices before complete silicon exists, remain useful as partial hardware arrives, and become the regression and calibration harness for real devices.

---

## 1. Why This Proposal Is Necessary

ATMOS decisions are being made right now — form factor, HBF capacity, LPDDR sizing, NPU balance, PCIe/CXL lane width, and OEM topology — without a system-level way to test any of them. Every one of these decisions gets harder and more expensive to change the later it is caught. A memory-tier size chosen in a spreadsheet today becomes a BOM commitment in a quarter and a fixed silicon constraint after tape-out. This proposal exists to move that evidence earlier, while it is still cheap to be wrong.

### 1.1 The Scale We Are Actually Building For

Sandisk's target deployment is not one device on one bench. It is hundreds of E3.S devices, attached either through OEM chassis bays or as ATMOS PCIe add-in cards, spread across multiple OEM server families, each running a different mix of dense and MoE models for different tenants. No single physical device, no static spreadsheet, and no vendor RTL testbench can predict how that fleet behaves. Only a system-level, swappable-backend simulator can, and it needs to exist before we commit to the hardware that fleet will run on.

### 1.2 Why We Must Have This Now

| Reason | What happens without it |
|---|---|
| Silicon and OEM platforms arrive in pieces (HBF, NPU, PCIe/CXL, full silicon) on different schedules | Product and architecture decisions stall waiting for the last piece, or get made blind on the pieces that have not arrived yet |
| Hundreds of E3.S devices across multiple OEMs is the target, not one bench unit | We discover fleet-scale bottlenecks (shared uplinks, control-plane limits, weight-distribution stalls) only after hardware is racked and committed |
| Dense and MoE workloads stress HBF, LPDDR, NPU, and topology very differently | We size memory and compute for the wrong workload mix and find out during OEM qualification, not before it |
| NVIDIA and AMD accelerators are already being measured in the AI lab today | ATMOS is compared against competitors using anecdote and marketing slides instead of matched, evidence-based reports |
| Physical POC iteration is slow, expensive, and hardware-constrained | Every architecture question costs a lab cycle instead of a simulation run, and only a handful of configurations ever get tested |
| Assumptions used for sizing decisions are rarely labeled by confidence | Executives cannot tell the difference between a guess and a measured result when approving a design |

### 1.3 What We Get By Approving This

- A single workload and reporting contract that works before silicon, during partial bring-up, after full silicon, and at fleet scale in production — the investment is not thrown away at each hardware milestone.
- The ability to test hundreds-of-device, multi-OEM, mixed dense/MoE fleet scenarios in software, long before that many physical devices could ever be racked.
- Evidence-labeled results, so a management decision can distinguish an assumed range from a measured, partner-modeled, or silicon-calibrated number.
- A direct, apples-to-apples comparison against NVIDIA RTX/DGX and AMD systems already in the AI lab, using the same traces and the same report format.
- A pre-silicon way to catch an under-sized memory tier, an oversubscribed PCIe topology, or a starved NPU before it becomes a fixed cost in silicon or a failed OEM qualification.

This is not a research nice-to-have. It is the only practical way to make hundreds-of-device, multi-OEM ATMOS decisions with evidence instead of guesswork, on the timeline the business actually has.

---

## 2. Executive Summary

ATMOS needs a practical digital twin: a deterministic software emulator that models how token-serving workloads consume NPU compute, HBF model capacity, LPDDR working memory, DMA engines, PCIe/CXL links, and OEM server topology. The emulator should be useful before silicon, during bring-up, and after silicon by replacing assumptions with measured component behavior as hardware becomes available.

The proposed system starts from **TokenSim as an integral serving layer**. TokenSim provides realistic request arrivals, batching behavior, token phases, prefill/decode scheduling, KV pressure, and serving-policy traces. Those token-serving events feed a C++ discrete-event simulation data plane that models the ATMOS hardware resources underneath.

The central principle is simple: the emulator does not hard-code a result such as "this request takes 4.3 ms." It simulates which resources each request needs, when each resource is available, how long each operation occupies that resource, and what blocks dependent work. Latency, throughput, queue depth, utilization, and bottlenecks emerge from resource occupancy, topology, dependencies, arbitration, and contention.

The first value is not a customer-facing performance claim. The first value is a decision engine:

- Which use cases are realistic for ATMOS E3.S versus a larger card?
- What HBF capacity and bandwidth are required for Search, neural reranking, and MoE serving?
- When does LPDDR working memory, DMA, NPU compute, or PCIe topology become the limiter?
- What measurements are needed first when partial silicon arrives?
- How should hardware, firmware, runtime, and OEM teams prioritize bring-up evidence?

---

## 3. Product Problem

ATMOS is expected to combine persistent model capacity, local neural compute, active memory, and a host interface in a modular inference device. Before complete silicon exists, product and engineering teams still need to make decisions about form factor, memory size, NPU capability, firmware queues, host interface, OEM topology, and workload fit.

The emulator answers questions that cannot be answered by static spreadsheets alone:

- How many requests can a device sustain before the service misses TTFT, TPOT, or p99 goals?
- Which phase dominates: prefill, decode, expert dispatch, KV movement, HBF read, LPDDR access, DMA, or PCIe?
- Does a model fit in HBF, and how much of that model is actually touched during serving?
- Does HBF provide value for expert residency, long-context KV, Search indexes, or neural reranking weights?
- What happens if HBF hardware is ready before the NPU, or if an NPU model is ready before final HBF media?
- Which measurements should be captured from partial hardware to reduce prediction uncertainty?

The emulator should be explicit about confidence. A result based on assumed bandwidth ranges is different from a result calibrated against measured HBF, measured PCIe, partner NPU timing, or final silicon.

---

## 4. Architecture Overview

The emulator has two cooperating layers — **TokenSim** as the serving/scheduling intelligence, and the **C++ discrete-event data plane** as the hardware simulation underneath. TokenSim is not a side integration; it is the default way to represent token-serving behavior.

```
                              ATMOS Digital Twin
                                       |
                    ┌──────────────────┴──────────────────┐
                    │                                      │
            Rust Control /                       C++ Simulation
            Scenario Plane                          Data Plane
                    │                                      │
        ┌───────────┼───────────┐              ┌──────────┼──────────┐
        │           │           │              │          │          │
   Scenario     TokenSim    Topology       DES Core   Virtual    OEM
   Config      Serving      Profile       Event      ATMOS     Topology
               Layer                    Engine       Device     Model
        │           │           │              │          │          │
        │     ┌─────┴─────┐     │         ┌────┴────┐  ┌──┴──┐  ┌───┴───┐
        │     │           │     │         │         │  │     │  │       │
        │  Batch      KV Cache  │      Event    Resource NPU  HBF   PCIe
        │  Sched.     Manager   │      Queue    Pools   /LPDDR Switches
        │     │           │     │         │         │  │ DMA │  │  RC   │
        │  Token       Latency  │      SimClock  Dependency Endpoint
        │  State       Model    │                   Tracker
        │     │           │     │
        │     └─────┬─────┘     │
        │           │           │
        └───────────┼───────────┘
                    │
         Workload Normalizer
         (TokenSim → TimedRequest)
                    │
            ModelGraph / SimIR
                    │
         ┌──────────┼──────────┐
         │          │          │
     Transformer  MoE       Search /
     Compiler    Compiler    Reranking
         │          │          │
         └──────────┼──────────┘
                    │
              Metrics + Traces
              Stall Attribution
              Calibration Ledger
                    │
         ┌──────────┼──────────┐
         │          │          │
     Fit/No-Fit   Sensitivity  Side-by-Side
     Reports      Analysis     vs RTX/DGX
```

### How Data Flows

1. **Scenario config** defines the experiment: model, workload, hardware profile, topology, scheduling policy, calibration sources.
2. **TokenSim** consumes the config and produces realistic token-level events — request arrivals, batching decisions, prefill/decode phase markers, KV pressure signals, and completion traces.
3. **Workload normalizer** converts TokenSim events into deterministic `TimedRequest` streams and phase markers for the C++ data plane.
4. **ModelGraph compiler** (Transformer, MoE, Search/Reranking) compiles model profiles into SimIR operation DAGs with dependency edges.
5. **C++ DES engine** processes events in deterministic virtual time — acquiring resources, scheduling completions, recording stalls, and tracking utilization.
6. **Virtual ATMOS device** models NPU compute, HBF/LPDDR memory, DMA engines, and PCIe endpoints as finite-capacity resources.
7. **Topology model** models root complexes, switches, E3.S bays, shared uplinks, and peer-to-peer paths.
8. **Metrics and stall attribution** explain what happened: TTFT, TPOT, throughput, utilization, queue depth, bytes moved, and stall-reason breakdown.
9. **Calibration ledger** tracks parameter confidence as assumptions are replaced with measured hardware data.

### Language Split

| Layer                            | Language         | Responsibility                                                                                                                                |
| -------------------------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| **Scenario orchestration** | Rust             | Configuration, REST/gRPC API, experiment management, workload ingestion, topology ingestion, result database, visualization, parameter sweeps |
| **TokenSim serving layer** | C++17 (embedded) | Batching, scheduling policies, KV cache management, token state machines, TTFT/TPOT metrics                                                   |
| **Simulation engine**      | C++17            | Deterministic discrete-event engine, virtual hardware, queues, resource arbitration, model execution, memory transactions, DMA, PCIe topology |

### Reuse from Existing Sentinel

| Component                             | Reuse                                                                 |
| ------------------------------------- | --------------------------------------------------------------------- |
| `Backend` trait                     | ATMOS simulation as a new backend variant                             |
| `PodScorer`                         | Extend with HBF headroom, NPU utilization, module transfer latency    |
| `SelectionPolicy`                   | Add HBF residency as hard filter; tier-migration cost as soft penalty |
| `KvPressureEstimator`               | Model ATMOS LPDDR working-set pressure separately                     |
| `AdmissionController`               | Per-module + per-card aggregate inflight tracking                     |
| `RequestShape`                      | Add HBF-resident model size, context length, tier access pattern      |
| `SessionMap` + HRW                  | Session→module affinity for KV cache locality                        |
| `ITransport` interface              | ATMOS transport for HBF→LPDDR→NPU pipeline                          |
| `BufferDescriptor` + `MemoryKind` | Extend with HBF, LPDDR, NPU_SRAM variants                             |
| `KvPageTable`                       | Map KV pages across HBF and LPDDR tiers                               |
| `CompletionQueue`                   | Track tier-migration operations                                       |
| `Result<T>` error model             | Unified error handling                                                |

---

## 5. Proposed System

The system has two cooperating planes:

- **Scenario and serving plane:** owns workload definitions, TokenSim execution or trace import, model profiles, topology profiles, experiment sweeps, manifests, and results.
- **C++ simulation data plane:** owns deterministic virtual time, event ordering, hardware resource state, queues, memory transactions, DMA movement, PCIe traversal, dependency resolution, metrics, and stall attribution.

```mermaid
flowchart TD
    A[Scenario Config] --> B[TokenSim Serving Layer]
    C[Model Profile] --> B
    D[Topology Profile] --> H[Topology Model]
    E[Hardware Profile] --> I[Virtual ATMOS Device]
    B --> F[Token Events and Request Phases]
    F --> G[Workload Normalizer]
    G --> J[ModelGraph / SimIR]
    J --> K[C++ Simulation Data Plane]
    H --> K
    I --> K
    K --> L[Deterministic DES Event Store]
    L --> M[Metrics, Traces, Stall Attribution]
    M --> N[Reports, Fit/No-Fit Maps, Calibration Ledger]
```

TokenSim is not a side integration. It is the default way to represent token-serving behavior. Native synthetic workload generation remains useful for small tests, analytical bounds, and debugging, but the proposal treats TokenSim traces as the main workload contract for realistic serving experiments.

---

## 6. Component Roles

| Component                  | Role                                             | How it works under the hood                                                                                                                                                                                       |
| -------------------------- | ------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scenario config            | Defines what experiment is being run             | Versioned YAML/JSON captures model, workload, hardware profile, topology, placement policy, seed, fidelity level, and calibration sources.                                                                        |
| TokenSim serving layer     | Produces realistic token-serving pressure        | Simulates or replays request arrivals, batching, queueing, prefill/decode boundaries, token generation cadence, priorities, cancellations, and KV pressure.                                                       |
| Workload normalizer        | Converts serving events into simulator inputs    | Converts TokenSim events into deterministic`TimedRequest` streams and phase markers, preserving request id, timestamp, prompt length, output limit, model id, priority, and trace lineage.                      |
| ModelGraph / SimIR         | Represents model execution as a dependency graph | Compiles model profiles into operations such as RMSNorm, QKV projection, attention, KV read/write, MLP, residual, expert dispatch, expert compute, combine, embedding, pooling, and reranking score.              |
| C++ simulation data plane  | Runs fast deterministic hardware simulation      | Uses integer nanosecond virtual time, a priority event store, dependency tracking, finite resources, and event handlers that acquire resources, calculate service times, schedule completions, and record stalls. |
| Virtual ATMOS device       | Models device-local hardware resources           | Represents NPU queues, tensor/vector engines, local SRAM, HBF channels, LPDDR channels, DMA descriptors, PCIe endpoint state, and per-module capacity limits.                                                     |
| Topology model             | Models server and OEM interconnect effects       | Represents root complexes, PCIe switches, E3.S bays, shared uplinks, peer-to-peer paths, link width, generation, queue depth, latency, and arbitration.                                                           |
| Metrics and trace recorder | Explains what happened                           | Records request latency, TTFT, TPOT, throughput, utilization, queue depth, bytes moved, phase time, stall reasons, and timestamped event traces.                                                                  |
| Calibration ledger         | Tracks evidence quality                          | Stores parameter source, owner, date, confidence class, measured range, allowed use, and replacement history as assumptions become measured values.                                                               |

---

## 7. C++ Simulation Data Plane

The C++ data plane is responsible for speed, determinism, and hardware-like resource accounting. It does not generate semantic model outputs. It answers: given this workload and this hardware profile, when can each operation run, what does it wait for, and which resources limit throughput?

### 5.1 Data Plane Inputs

The data plane consumes normalized simulation inputs:

- TokenSim request stream: arrival time, request id, priority, prompt tokens, target output tokens, model id.
- TokenSim phase stream: prefill start/end, decode-token ready, batching decision, queue admission, cancellation, completion.
- Model profile: layers, hidden size, attention heads, KV heads, expert count, active experts, embedding dimensions, precision, operator coverage.
- Hardware profile: NPU peak throughput, engine counts, SRAM size, HBF capacity/bandwidth/channels, LPDDR capacity/bandwidth/channels, DMA bandwidth, queue depths, outstanding limits.
- Topology profile: PCIe/CXL generation, lane count, switches, root complexes, shared uplinks, peer path rules, host memory path, NUMA placement.

### 5.2 DES Event Store

The DES event store is a deterministic priority queue. Each event has:

- `timestamp`: virtual simulation time in integer nanoseconds.
- `sequence`: monotonic tie-breaker for events with the same timestamp.
- `type`: request, memory, DMA, PCIe, compute, dependency, scheduler, or custom event.
- `target`: resource or entity being acted on.
- `payload`: compact metadata such as bytes, operation id, source, destination, or request id.

```mermaid
flowchart LR
    A[Event Handler] --> B[Create Follow-up Event]
    B --> C[DES Event Store]
    C --> D{Earliest timestamp}
    D --> E{Lowest sequence if tie}
    E --> F[Advance SimClock]
    F --> G[Invoke Handler]
    G --> A
```

The event store is deterministic because it uses integer time, deterministic ordering, and a fixed sequence counter. There is no dependency on wall-clock time, OS scheduling, thread timing, or floating-point accumulation for event order.

### 5.3 Event Processing Loop

At runtime, the data plane repeatedly performs this loop:

1. Pop the earliest event from the DES event store.
2. Advance the virtual clock to that event's timestamp.
3. Update resource busy/idle accounting up to the new time.
4. Dispatch the event to registered C++ handlers.
5. The handler checks dependencies and resource availability.
6. If resources are available, acquire them and schedule a completion event.
7. If resources are unavailable, enqueue, block, retry, or record a stall.
8. On completion, release resources and mark dependent operations ready.
9. Continue until the event store is empty or the experiment time limit is reached.

```text
REQUEST_ARRIVED
  -> SCHEDULER_DISPATCHED
  -> MEMORY_READ_STARTED
  -> MEMORY_READ_COMPLETED
  -> DMA_STARTED
  -> DMA_COMPLETED
  -> COMPUTE_STARTED
  -> COMPUTE_COMPLETED
  -> DEPENDENCY_SATISFIED
  -> REQUEST_COMPLETED
```

### 5.4 Resource Model

Every hardware block is modeled as a finite resource. A resource has capacity, occupancy, state, queue depth, service time, and utilization tracking.

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Busy: acquire resource
    Busy --> Idle: release resource
    Busy --> Blocked: queue full or dependency missing
    Blocked --> Busy: resource becomes available
    Busy --> Failed: injected fault or unavailable hardware profile
    Failed --> Idle: reset/recovery event
```

This is the key modeling choice: the simulator does not pretend to be real silicon instruction-by-instruction. It models the resource constraints that determine system behavior.

---

## 8. Virtual ATMOS Hardware Model

ATMOS is modeled as one or more modules. Each module contains persistent HBF capacity, active LPDDR memory, NPU compute, local SRAM, DMA engines, and a host endpoint.

```mermaid
flowchart TB
    A[ATMOS Device]
    A --> M0[Module 0]
    A --> M1[Module 1]
    A --> M7[Module N]
    M0 --> H0[HBF Controller]
    M0 --> L0[LPDDR Controller]
    M0 --> N0[Virtual NPU]
    M0 --> D0[DMA Engines]
    M0 --> P0[PCIe/CXL Endpoint]
```

### 6.1 Virtual NPU

The Virtual NPU models neural compute as finite execution capacity:

- Command queue controls how many operations can be admitted.
- Tensor engines execute GEMM, attention, and expert matrix work.
- Vector engines execute elementwise, normalization, activation, pooling, and reduction work.
- Load/store engines model data movement between local SRAM and memory-facing resources.
- Local SRAM capacity controls whether tiles, activations, and command metadata fit.
- Execution slots control concurrent in-flight compute operations.

For each compute operation, the simulator calculates an estimated service time from FLOPs, compute class, precision, configured peak throughput, and future calibration factors. The operation cannot start until dependencies are satisfied and required NPU resources are available. If tensor engines are full, the operation waits in `WAIT_COMPUTE`. If SRAM is insufficient, it waits in `WAIT_SRAM` or triggers a tiling/spill path depending on the fidelity level.

### 6.2 HBF Controller

The HBF controller models persistent model capacity and high-bandwidth read-mostly access:

- Capacity tracks resident weights, experts, embeddings, tables, Search indexes, and model packs.
- Channels model parallel media/controller access.
- Request queues model backpressure when too many reads or writes arrive.
- Outstanding limits bound concurrency inside the controller.
- Bandwidth and protocol overhead determine transfer service time.
- Arbitration decides which channel or requester is served next.

HBF is where the emulator tests whether capacity-side benefits survive real access behavior. A model may fit in HBF but still bottleneck on channel pressure, controller queues, or DMA into active memory.

### 6.3 LPDDR Controller

LPDDR models active writable memory:

- KV cache state.
- Activations and temporary tensors.
- Expert working tiles.
- Search/reranking intermediate buffers.
- Command rings and software-visible queues.

LPDDR differs from HBF because it is smaller, more active, and sensitive to read/write direction changes and refresh overhead. Decode-heavy workloads can stress LPDDR even when HBF capacity is sufficient.

### 6.4 DMA Engines

DMA engines model movement between memory tiers and endpoints:

- HBF to LPDDR.
- LPDDR to local SRAM.
- Host memory to device.
- Device to host.
- Device to device through PCIe/CXL topology when supported.

Each transfer consumes descriptor queue entries, outstanding slots, channels, setup overhead, and bandwidth. This lets the emulator expose cases where compute is available but data movement cannot feed it fast enough.

### 6.5 PCIe/CXL Endpoint and OEM Topology

The endpoint and topology model represent the server around the device:

- E3.S bay mapping.
- Link generation and lane width.
- Switches and root complexes.
- Shared uplinks and oversubscription.
- Peer-to-peer path availability.
- Host-memory staging and NUMA penalties.
- Management and reset path assumptions.

```mermaid
flowchart TB
    CPU0[CPU Socket / Root Complex 0] --> SWA[PCIe Switch A]
    SWA --> E0[E3.S Bay 0 / ATMOS]
    SWA --> E1[E3.S Bay 1 / ATMOS]
    SWA --> U0[Shared Uplink]
    CPU1[CPU Socket / Root Complex 1] --> SWB[PCIe Switch B]
    SWB --> E2[E3.S Bay 2 / ATMOS]
    SWB --> E3[E3.S Bay 3 / ATMOS]
    CPU0 <--> CPU1
```

This is essential because a device-level result is not enough. A workload can look healthy on one device and fail when multiple devices share an uplink, cross root complexes, or stage through host memory.

---

## 9. TokenSim as the Serving Contract

TokenSim is part of the base architecture because ATMOS value depends on serving dynamics, not just isolated operator timing. Search and MoE workloads both create time-varying request pressure, batch composition, memory reuse, and queueing behavior.

TokenSim should provide or replay:

- Request arrival patterns: fixed, Poisson, burst, trace-driven, priority classes.
- Batching decisions: batch creation, admission, split prefill, chunked prefill, decode batching.
- Phase markers: queued, prefill, decode, paused, resumed, completed, cancelled.
- KV behavior: allocation, reuse, eviction, restore, recompute, migration hints.
- MoE behavior: token-to-expert routing, active expert set, dispatch/combine phases, hot expert reuse.
- Search behavior: query arrival, embedding, candidate fetch, reranking batch, result assembly.

The C++ data plane then maps those serving events to hardware actions:

```mermaid
flowchart LR
    A[TokenSim Request Event] --> B[TimedRequest]
    B --> C[SimIR Operations]
    C --> D{Operation Type}
    D --> E[NPU Compute]
    D --> F[HBF Transaction]
    D --> G[LPDDR Transaction]
    D --> H[DMA Transfer]
    D --> I[PCIe/CXL Transfer]
    E --> J[Completion / Stall]
    F --> J
    G --> J
    H --> J
    I --> J
```

This split keeps the serving model honest and the hardware model explainable. TokenSim decides what the service is trying to do; the ATMOS digital twin decides when the hardware can actually do it.

---

## 10. Partial Silicon and Bring-Up Strategy

The emulator should become more valuable as hardware arrives in pieces. It must support replacing individual assumptions with measured components without waiting for the full device.

```mermaid
flowchart TD
    A[Assumed Profile] --> B[Measured HBF]
    A --> C[Measured PCIe/CXL Path]
    A --> D[Partner or RTL NPU Timing]
    A --> E[Measured LPDDR / DMA]
    B --> F[Calibrated Component Model]
    C --> F
    D --> F
    E --> F
    F --> G[Updated Digital Twin]
    G --> H[Differential Tests vs Full Silicon]
    H --> I[Qualified Prediction Envelope]
```

### 8.1 If HBF Arrives Before NPU

The emulator can use measured HBF bandwidth, latency, queue depth, access granularity, endurance constraints, and controller behavior while keeping NPU timing analytical or partner-modeled. This supports early decisions about:

- Model and expert residency.
- Search index and embedding-table placement.
- Sustained read behavior under concurrent requests.
- DMA staging requirements from HBF into active memory.
- Whether HBF bandwidth or access granularity is sufficient for the target workloads.

### 8.2 If NPU Timing Arrives Before HBF

The emulator can consume measured or partner-modeled NPU operator timing while HBF remains an assumed or synthetic tier. This supports early decisions about:

- Operator coverage and unsupported fallback paths.
- Tensor/vector engine balance.
- SRAM tile sizes.
- Compute efficiency by operator class.
- Whether compute is likely to outrun memory movement.

### 8.3 If PCIe/OEM Platform Arrives First

The emulator can use measured host topology, link bandwidth, peer-to-peer behavior, NUMA effects, and shared-uplink contention while keeping device internals virtual. This supports early decisions about:

- E3.S bay placement.
- x4 versus x8 requirements.
- Root-complex crossing cost.
- Host staging cost.
- Multi-device scaling limits.

### 8.4 After Full Silicon

When full silicon is available, the emulator becomes a regression and planning harness:

- Replay the same TokenSim traces against the emulator and real hardware.
- Compare predicted and measured TTFT, TPOT, throughput, queue depth, bytes moved, and utilization.
- Replace broad assumptions with calibrated distributions.
- Track prediction error by workload, topology, model, and firmware version.
- Use the calibrated twin for design-space exploration beyond the exact hardware tested.

---

## 11. Current Priority Use Cases

The initial use cases should be narrow enough to produce credible evidence and broad enough to guide product direction.

### 9.1 Search, Embedding, and Neural Reranking

The Search track is based on the ATMOS E3.S product/workstream framing: independent neural services and medium-size model serving in a modular E3-class device.

Primary workloads:

- Embedding generation for query and document chunks.
- Neural reranking over candidate query-document pairs.
- Guard/classification models around retrieval and response flows.
- RAG preprocessing pools where embedding, reranking, and guard stages are isolated from the main LLM engine.
- Private domain models where one model or service fits on one device.

What the emulator measures:

- Embeddings per second, pairs per second, request p50/p95/p99.
- Batch-size sensitivity and queueing behavior.
- HBF residency of embedding/reranking weights and tables.
- LPDDR workspace pressure for candidate batches and intermediate tensors.
- Host ingress/egress bytes and PCIe/CXL path cost.
- Device-pool scaling: one model per device, replicated pools, tenant/domain isolation.

Why ATMOS may fit:

- Search services are often independently scalable and highly batchable.
- Neural reranking uses transformer scoring over candidate batches and can benefit from dedicated devices.
- Persistent local model capacity can reduce cold-load and model-switch costs.
- A modular E3.S deployment model can map naturally to per-service or per-tenant inference pools.

### 9.2 MoE Serving and HBF Evaluation

The MoE track is based on the HBF/OEM/LLM evaluation framing: high-concurrency serving with varied context sizes, expert placement, KV reuse, and topology sensitivity.

Primary workloads:

- Small resident control model to validate the serving harness.
- Resident MoE where experts fit in fast local memory.
- Tiered MoE where hot experts or larger expert sets use HBF as a lower persistent tier.
- Long-context decode to isolate KV capacity and attention cost.
- Reusable prefix and paused-session scenarios to compare retain, restore, and recompute.
- Mixed arrivals and contention with short/long requests, bursts, priorities, and interference.

What the emulator measures:

- Sustainable requests per second under fixed latency and correctness requirements.
- TTFT, TPOT, output tokens per second, and input-token processing separately.
- Expert dispatch/combine cost versus actual expert weight movement.
- HBF bytes fetched, hot-expert hit behavior, prefetch effectiveness, and NPU starvation.
- KV footprint, active/shared KV, activations, workspaces, communication buffers, and I/O rings.
- Topology impact across tensor parallelism, expert parallelism, replicas, and multi-device layouts.

Important modeling rule:

Changing which expert a token selects is not the same as reloading expert weights. The emulator must separate dispatch/combine traffic to resident expert owners from additional weight-residency changes. Otherwise HBF value can be overstated or understated.

---

## 12. Fidelity Ladder

| Level | Name                       | What it models                                                                          | Primary use                                           |
| ----- | -------------------------- | --------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| L0    | Capacity math              | Weights, KV, activations, HBF capacity, LPDDR capacity, PCIe bandwidth bounds           | Fast fit/no-fit and sanity checks                     |
| L1    | Analytical timing          | FLOPs, bandwidth, protocol delay, simple overlap, queue capacity                        | Early sensitivity and architecture thresholds         |
| L2    | Discrete-event system      | TokenSim traces, request phases, queues, dependencies, NPU/HBF/LPDDR/DMA/PCIe resources | MVP target for bottleneck discovery                   |
| L3    | Calibrated component model | Measured HBF, LPDDR, PCIe, DMA, NPU tables, firmware queue behavior                     | Partial-silicon and lab calibration                   |
| L4    | Partner/RTL/FPGA backend   | Higher-fidelity block or command timing behind the same workload contract               | Differential validation before full silicon           |
| L5    | Silicon-calibrated twin    | Measured device behavior and production software stack                                  | Qualified prediction envelope and regression planning |

---

## 13. Outputs and Success Criteria

### MVP Outputs

- TokenSim-driven workload replay into the ATMOS C++ data plane.
- One-device and multi-device topology experiments.
- Search/reranking workload profile and MoE serving workload profile.
- HBF versus LPDDR placement experiments.
- Stall attribution across scheduler, dependency, HBF, LPDDR, DMA, PCIe/CXL, and compute.
- Fit/no-fit report with evidence labels and parameter sensitivity.

### Engineering Outputs

- Versioned model, workload, hardware, and topology profiles.
- Repeatable traces with seeds, trace hashes, build version, and calibration metadata.
- Calibration ledger that tracks assumed, measured, partner-modeled, RTL/FPGA-calibrated, and silicon-measured parameters.
- Differential reports when partial or full hardware is available.
- OEM compatibility matrix for slots, bays, power, cooling, firmware, reset, and management paths.

### Decision Outputs

- Which workloads justify ATMOS E3.S first.
- Whether HBF capacity, bandwidth, and access granularity are sufficient.
- Whether NPU compute, memory movement, or topology is the gating factor.
- Which hardware blocks need measurement next.
- Which configurations should advance to physical POC.

---

## 14. Target Platform

Development should start on macOS and Linux because the emulator is CPU-bound and does not require CUDA. Large sweeps and CI should run on Linux.

Required software:

- C++17 compiler.
- CMake 3.20 or newer.
- GoogleTest for unit tests, fetched by CMake.
- Rust only if the scenario/API plane is implemented in Rust.
- Python only if TokenSim execution, trace generation, or analysis notebooks require it.

No GPU is required for the emulator itself. GPU systems remain useful as measured controls for comparison against existing RTX/NVMe/GDS and RTX/CXL paths.

---

## 15. End-to-End Evaluation Framework

The proposal should be treated as more than an ATMOS-only simulator. The larger goal is an end-to-end evaluation suite that can run the same workload, trace, topology, and reporting flow against multiple execution targets:

- ATMOS digital twin without silicon.
- ATMOS partial-silicon profiles, such as measured HBF with simulated NPU timing.
- Full ATMOS silicon when available.
- NVIDIA RTX or DGX systems in the AI lab.
- AMD accelerator systems when available.
- CPU, NVMe/GDS, CXL, or host-memory staging baselines.

The framework enables apples-to-apples comparison by keeping the workload contract stable while swapping the execution backend.

```mermaid
flowchart TD
    A[Common Workload Suite] --> B[TokenSim Serving Contract]
    B --> C[Common Scenario Runner]
    C --> D{Execution Backend}
    D --> E[ATMOS Digital Twin]
    D --> F[ATMOS Partial Silicon]
    D --> G[ATMOS Full Silicon]
    D --> H[NVIDIA RTX / DGX Lab]
    D --> I[AMD Accelerator Lab]
    D --> J[CPU / NVMe / CXL Baselines]
    E --> K[Common Metrics Schema]
    F --> K
    G --> K
    H --> K
    I --> K
    J --> K
    K --> L[Comparison Reports]
```

### 13.1 Swappable Backend Contract

Each backend should implement the same logical contract:

- `describe_device`: reports memory tiers, compute capability, topology, precision support, queue limits, and software stack version.
- `load_model`: stages or registers model weights, experts, embedding tables, indexes, and runtime metadata.
- `run_trace`: consumes the TokenSim request/phase trace or a normalized workload trace.
- `report_metrics`: returns request metrics, token metrics, resource utilization, bytes moved, queue depth, errors, and stall categories.
- `evidence`: labels whether results are simulated, measured, partially measured, partner-modeled, or silicon-measured.

This lets the same Search or MoE workload run in several modes:

| Mode                 | Backend                         | Purpose                                                                                    |
| -------------------- | ------------------------------- | ------------------------------------------------------------------------------------------ |
| Pre-silicon          | ATMOS digital twin              | Estimate fit, bottlenecks, and design sensitivity before hardware.                         |
| Partial hardware     | ATMOS component-calibrated twin | Replace assumptions with measured HBF, NPU, DMA, LPDDR, or PCIe behavior as pieces arrive. |
| Full hardware        | ATMOS silicon                   | Validate predictions, calibrate the model, and qualify the tested envelope.                |
| Competitive baseline | NVIDIA RTX / DGX                | Compare against known GPU paths in the AI lab under the same traces and reports.           |
| Competitive baseline | AMD accelerator                 | Compare against alternative accelerator architecture when available.                       |
| System baseline      | CPU / NVMe / CXL                | Separate storage, memory, and host-staging effects from accelerator effects.               |

### 13.2 AI Lab Integration

The AI lab can provide measured control runs for RTX and DGX systems while ATMOS remains simulated or partially available. This is valuable because management can see the same workload reported across both proposed and existing platforms.

The lab integration should capture:

- GPU model, count, driver, runtime, serving engine, precision, and framework version.
- Host CPU, memory, NUMA, PCIe topology, NVMe/GDS path, CXL path, and network topology.
- Model load time, warm state, batch policy, prompt/output settings, and tokenizer/version.
- TTFT, TPOT, p50/p95/p99, output tokens per second, input-token processing rate, request throughput, and error rate.
- GPU utilization, memory utilization, host-device bytes, storage bytes, cache behavior, and queue depth where available.

The comparison should not claim that simulated ATMOS results are measured hardware results. Reports must label every result by evidence class. The value is that all systems are evaluated through one workload harness, one metrics schema, and one report format.

### 13.3 Why This Matters

This framing turns the workstream into a reusable product and architecture evaluation platform:

- Before silicon, it answers whether ATMOS should be pursued for a workload and what must be true for success.
- During partial silicon, it directs which measured component should replace which assumption.
- With full silicon, it becomes the regression harness and prediction-calibration tool.
- With AI lab systems, it compares ATMOS against NVIDIA and AMD alternatives using the same use-case definitions.
- For executives, it produces workload-fit maps and investment gates.
- For engineering, it produces bottleneck attribution, sensitivity ranking, and measurement priorities.

---

## 16. Six-to-Eight Week Implementation Plan

The implementation plan targets a complete end-to-end emulator and evaluation suite in 6-8 weeks. The goal is not only to build simulator components, but to produce repeatable reports that can compare simulated ATMOS, partial ATMOS hardware, and AI lab accelerator baselines under a common workload contract.

### Week 1: Foundation and Contracts

- Finalize the scenario schema for model, workload, hardware, topology, backend, and reporting profiles.
- Define the TokenSim trace schema for request arrivals, batching, prefill/decode phases, KV events, MoE routing, and Search stages.
- Define the swappable backend contract for simulated, partial-hardware, full-hardware, NVIDIA, AMD, and CPU/CXL/NVMe backends.
- Establish deterministic run manifests: seed, trace hash, scenario version, backend version, calibration sources, and evidence class.
- Deliverable: architecture contract, schema examples, and one dry-run scenario manifest.

### Week 2: C++ DES Core and ATMOS Resource Model

- Implement deterministic event ordering with integer nanosecond virtual time and sequence-based tie breaking.
- Implement finite resources, bounded queues, dependency tracking, arbitration, utilization accounting, and trace recording.
- Implement first-pass NPU, HBF, LPDDR, DMA, and PCIe/CXL endpoint models.
- Add unit tests for ordering, replay determinism, queue limits, dependency release, and resource saturation.
- Deliverable: simulator core that can run synthetic operations and explain resource waits.

### Week 3: TokenSim Integration and ModelGraph / SimIR

- Convert TokenSim traces into normalized timed requests and phase events.
- Compile Search, embedding, reranking, dense transformer, and MoE profiles into SimIR operations.
- Add operator classes for embedding, pooling, reranking score, attention, MLP, expert dispatch, expert compute, combine, KV read, and KV write.
- Preserve trace lineage from TokenSim event to hardware operation to metric output.
- Deliverable: TokenSim-driven workload replay through the ATMOS simulator.

### Week 4: Topology, Memory Placement, and Stall Attribution

- Implement topology profiles for one device, two devices with independent paths, and multiple devices behind shared uplinks.
- Add HBF versus LPDDR placement policies for weights, experts, KV, Search indexes, and intermediate buffers.
- Add stall attribution for scheduler, dependency, HBF, LPDDR, SRAM, DMA, PCIe/CXL, and compute waits.
- Produce first bottleneck reports for Search/reranking and MoE serving.
- Deliverable: end-to-end simulated ATMOS reports with bottleneck breakdown.

### Week 5: AI Lab Backend Harness

- Add backend adapters for measured NVIDIA RTX/DGX lab runs using the same scenario and metrics schema.
- Capture lab metadata: GPU, driver, runtime, serving framework, precision, topology, storage path, and model version.
- Add baseline scenarios for RTX plus accelerator memory, RTX plus NVMe/GDS, and RTX plus CXL where available.
- Normalize measured lab results into the same report format as simulated ATMOS.
- Deliverable: first side-by-side ATMOS digital twin versus RTX/DGX report for one Search workload and one MoE workload.

### Week 6: Calibration, Sensitivity, and Report Pack

- Add calibration profiles for assumed, measured-control, partial-silicon, partner-modeled, and silicon-measured parameters.
- Run parameter sweeps for HBF bandwidth, HBF latency, NPU throughput, LPDDR bandwidth, DMA bandwidth, PCIe lane width, and shared-uplink contention.
- Generate fit/no-fit maps and sensitivity rankings.
- Produce executive and technical report templates.
- Deliverable: decision-ready report pack with confidence labels and top sensitivity drivers.

### Week 7: Partial-Silicon Readiness

- Add component replacement workflow so measured HBF, NPU, DMA, LPDDR, or PCIe data can override assumptions independently.
- Add differential report templates for predicted versus measured behavior.
- Define microbenchmarks for HBF-only, NPU-only, DMA-only, PCIe-only, and combined paths.
- Validate that partial-hardware results can be mixed with simulated components without changing workload definitions.
- Deliverable: partial-silicon bring-up kit and calibration runbook.

### Week 8: Hardening, Demo, and Decision Package

- Harden scenario validation, error messages, reproducibility checks, and report generation.
- Run the complete Search and MoE workload suite across available backends.
- Prepare an executive summary with workload-fit maps, baseline comparisons, sensitivity results, and recommended next measurements.
- Prepare a technical appendix with topology, traces, resource parameters, event counts, queue behavior, and stall attribution.
- Deliverable: complete end-to-end demo and decision package.

### Delivery Criteria

The workstream is complete when the team can:

- Run a TokenSim-driven Search workload and MoE workload through the ATMOS digital twin.
- Run at least one comparable AI lab backend using the same scenario and report schema.
- Produce side-by-side reports with evidence labels for simulated and measured results.
- Show bottleneck attribution down to scheduler, memory tier, DMA, PCIe/CXL, and compute categories.
- Swap backend profiles without changing workload definitions.
- Replace an assumed component with measured partial-silicon data and regenerate the report.
- Provide an executive summary and technical appendix for each run.

---

## 17. Lab Setup and At-Scale Testing Strategy

Building the simulator is not the end goal. The end goal is being able to answer, with evidence, how ATMOS behaves once Sandisk is running hundreds of E3.S devices, attached either through OEM chassis bays or as ATMOS PCIe add-in cards, spread across multiple OEM server families, each carrying its own mix of dense and MoE models. This section defines how we practically leverage the simulator to get there, and what the physical lab looks like alongside it. It also has to cover every candidate server architecture from Section 18 (full-length card, direct-attached E3.S, E3.S + switch AIC, OEM-integrated switched E3.S, hybrid node, and rack-scale pool), not just one assumed packaging.

### 17.1 The Deployment Shape We Must Validate For

- Hundreds of E3.S devices, not a handful.
- All six architecture patterns from Section 18 in scope, not just one: full-length card, direct-attached E3.S, E3.S behind a separate switch AIC, OEM-integrated switched E3.S, hybrid nodes, and rack-scale pools.
- Multiple OEM server families in parallel, each owning its own group of E3.S devices, its own bay count, its own PCIe/CXL lane budget, and its own shared-uplink behavior.
- A mixed workload fleet: some devices serving dense models, others serving MoE, often on the same OEM group, sometimes on the same device pool.
- Multiple tenants and priority classes sharing the same physical fleet.

No lab can rack enough pre-silicon hardware to physically cover this matrix. The strategy has to combine a real, representative physical lab with a calibrated fleet-scale digital twin.

### 17.2 Three-Tier Lab Model

```mermaid
flowchart TD
    A[Tier A: OEM Bench] --> D[Calibration Loop]
    B[Tier B: Multi-OEM Rack] --> D
    D --> C[Tier C: Fleet Digital Twin]
    C --> E[Top Sensitivity / Risk Configs]
    E --> A
    E --> B
    C --> F[Fleet Readiness Report]
```

| Tier | What it is | Device count | Purpose |
|---|---|---|---|
| Tier A — OEM Bench | One OEM server, populated with as many real or prototype E3.S bays and ATMOS PCIe cards as available | 1-8 devices per OEM | Validate per-OEM topology assumptions, bay/PCIe behavior, single-node serving; the fixture where architectures A (full card), B (direct-attach), and C (switch AIC) get their first physical measurement |
| Tier B — Multi-OEM Rack | Two or more OEM server families side by side, shared top-of-rack network, mixed dense/MoE workloads | 8-32 devices across 2-3 OEMs | Validate cross-node, cross-OEM, and shared-fabric behavior; the fixture where architecture E (hybrid node) and the network layer for architecture F (rack pool) get validated |
| Tier C — Fleet Digital Twin | Pure software: the ATMOS simulator running every OEM topology profile at full target scale | Hundreds to thousands, simulated | Explore the actual production scale, sweep device count, OEM mix, model mix, and architecture pattern (A-F) far beyond what the physical lab can hold |

The tiers are not independent. Tier A and Tier B produce measured results that calibrate Tier C. Tier C then tells us which configurations in the hundreds-of-device space carry the most risk, and those specific configurations get reproduced at reduced scale in Tier A/B to confirm the simulator is still tracking reality. Architecture D (OEM-integrated switched E3.S) only enters Tier A once a partner OEM commits to co-design; until then it is evaluated purely in Tier C using the same topology-profile mechanism described in Section 18.1.

### 17.3 OEM Topology Profile Registry

Every OEM family Sandisk works with — and every OEM added later — is captured as one versioned topology profile, used identically by the physical lab and the simulator:

- Bay count and E3.S versus ATMOS-PCIe-card slot mix.
- PCIe/CXL generation, lane width per slot, and switch hierarchy.
- Shared-uplink and oversubscription behavior.
- Power and thermal caps per bay/slot.
- Firmware, BMC, and reset/management assumptions.

This registry is what makes the multi-OEM fleet testable at all: a topology profile authored once from Tier A/B measurements can be replayed at Tier C for a fleet of any size, without re-deriving OEM-specific behavior by hand each time.

### 17.4 Fleet Test Matrix

Tier C sweeps the dimensions that a physical lab cannot afford to sweep exhaustively:

| Dimension | Example values |
|---|---|
| Device count | 1, 4, 8, 16, 32, 64, 128, 256, 512+ |
| Architecture pattern (Section 18) | A. Full-length card, B. Direct-attached E3.S, C. E3.S + switch AIC, D. OEM-integrated switched E3.S, E. Hybrid node, F. Rack-scale pool |
| OEM mix | Single-OEM fleet, two-OEM split, three-plus-OEM split |
| Model mix | Dense-only, MoE-only, 70/30 dense/MoE, 50/50 dense/MoE |
| Workload class | Search/reranking pools, MoE serving pools, mixed per-OEM pools |
| Tenancy | Single-tenant isolated pools, multi-tenant shared pools with priority classes |
| Fault condition | Baseline, single-device failure, single-link failure, degraded-bay condition, degraded switch/card condition |

The purpose of this matrix is to find where fleet-level bottlenecks emerge — shared uplinks, control-plane scheduling limits, weight-distribution stalls when many devices load models at once — that never show up on a single device or a single OEM bench. Sweeping architecture pattern alongside device count and model mix is what turns Section 18's comparison table from a handful of spot checks into a full sensitivity map per architecture.

### 17.5 Physical Lab Composition

- **Reference OEM servers:** at least two distinct chassis families representative of the target OEM base, each with a documented, supported bay/slot configuration.
- **Devices under test:** E3.S bays populated with whatever is available at each stage — prototype boards, FPGA/emulation stand-ins pre-silicon, partial-silicon modules, and eventually qualified ATMOS silicon — plus ATMOS PCIe add-in cards where that attach mode is supported.
- **Architecture fixtures:** a full-length ATMOS accelerator card and slot (architecture A), direct-attached bays with no switch in the path (architecture B), and a separate PCIe switch AIC with cabling to front bays (architecture C) so all three can be measured side by side on the same reference server; an OEM-integrated switched sample (architecture D) is added once a partner OEM commits to co-design.
- **Network fabric:** top-of-rack switch and NICs matching the shared-uplink and cross-node paths the topology profiles need to validate, including the rack-pool network layer that architecture F (Section 18.1) depends on.
- **Control plane:** the scenario runner and TokenSim serving layer running live against the physical devices, not just against the simulator, so the same workload contract drives both.
- **Model distribution:** a real weight/model-provisioning path exercised at Tier B scale, since loading models onto dozens of devices at once is itself a fleet-scale problem worth measuring.
- **Telemetry:** the same metrics schema used by the simulator (TTFT, TPOT, throughput, utilization, queue depth, stall attribution), collected from real devices so Tier A/B results can be compared line-for-line against Tier C predictions.

### 17.6 Practical Workflow

1. Define or update the OEM topology profile and model-mix scenario for the fleet configuration in question.
2. Run the full-scale scenario in Tier C (hundreds of devices, target OEM mix, target dense/MoE mix).
3. Identify the highest-sensitivity and highest-risk configurations from the Tier C sweep — the ones where small parameter changes move the result the most.
4. Reproduce only those specific configurations at reduced scale in Tier A/B, on real or partial hardware.
5. Compare predicted versus measured results and update the calibration ledger.
6. Re-run Tier C with the updated calibration and regenerate the fleet readiness report.

This loop means the physical lab never needs to hold hundreds of devices. It only needs to hold enough devices, across enough OEMs, to keep the simulator honest at the configurations that matter most.

### 17.7 Continuous Regression at Scale

- Run the full fleet test matrix in Tier C on a scheduled basis (nightly or weekly) across every registered OEM topology profile, architecture pattern (A-F), and model mix.
- Run a small, fixed set of Tier A/B physical spot-checks on a slower cadence (for example, quarterly, or whenever new hardware arrives) against the highest-value configurations.
- Track prediction error over time per OEM profile, per architecture pattern, per workload class, and per device count, and feed that history back into the calibration ledger.
- Treat a growing gap between Tier C predictions and Tier A/B measurements as a signal that a topology profile or hardware model needs to be revisited before it is trusted for the next fleet-sizing decision.

---

## 18. Covering the ATMOS E3.S Server Architecture Options

The ATMOS E3.S Server Architecture Options proposal defines six candidate ways to package and connect ATMOS in a server: (A) the full-length ATMOS accelerator card, (B) a direct-attached E3.S device pool, (C) an E3.S pool behind a separate PCIe switch add-in card, (D) an OEM-integrated switched E3.S platform, (E) a hybrid node combining an E3.S pool with a full-length card, and (F) a multi-node/rack-scale ATMOS service. The emulator does not need a separate model per architecture — it needs to represent each one as a configuration of the same topology graph, then run the identical workload contract through each configuration to turn the document's qualitative pros/cons table into a measured comparison.

### 18.1 Do We Have to Accommodate All Six Plans?

Yes, but not as six different simulators. The topology graph already used throughout this proposal — root complexes, PCIe switches, links, and endpoints connected in an arbitrary graph — is general enough to express five of the six architectures directly as topology profiles. Only one architecture introduces a genuinely new modeling concern.

| Architecture | Coverage today | What the topology profile looks like |
|---|---|---|
| A. Full-length ATMOS card | Covered | One switch node with a wide host-facing link and eight endpoint nodes behind it, using tight intra-card link latency/bandwidth to represent the on-card switch fabric. |
| B. Direct-attached E3.S pool | Covered | Endpoint nodes connected straight to root-complex nodes, no switch node in the path, one link per bay. |
| C. E3.S pool + separate PCIe switch AIC | Covered | Exactly the switch/root-complex/endpoint pattern already used by the existing example topologies, with the switch uplink modeled as the shared, potentially oversubscribed link. |
| D. OEM-integrated switched E3.S | Covered as a topology profile | Same graph shape as C, but authored as a qualified, versioned OEM topology profile (Section 17.3) with OEM-specific lane maps, switch hierarchy, and population rules instead of a generic switch card. |
| E. Hybrid node | Covered | Two subgraphs under one set of root complexes: a full-card subgraph (as in A) and a direct/switched E3.S subgraph (as in B or C), with the placement/routing policy deciding which device class serves which request. |
| F. Multi-node/rack-scale service | Partial gap | Needs a network-fabric layer above PCIe — NICs, top-of-rack switches, cross-node latency/bandwidth, and failure/rebuild behavior — which the current topology graph does not yet model. |

So the answer is: the emulator already has to accommodate five of the six plans, because they are all instances of the same PCIe topology graph with different parameters. Architecture F is the one real gap, and it is additive, not a redesign: it needs a thin network-topology layer (node types for NIC and top-of-rack switch, a cross-node link type with its own latency/bandwidth/oversubscription model, and simple failure/rebuild event handling) sitting above the existing PCIe graph. That layer belongs in Epic 3 alongside the existing topology work, not as a new simulator.

### 18.2 How the Emulator Turns the Document's Table Into Measured Pros/Cons

The architecture options document produces a qualitative decision matrix (service unit, serviceability, communication locality, host lane pressure, large-model fit, P2P potential, and so on) built from engineering judgment. The emulator's job is to run the same workload suite through each architecture's topology profile and produce the numeric version of that same table, using the metrics schema already defined for every other comparison in this proposal.

```mermaid
flowchart TD
    A[Architecture Options A-F] --> B[One Topology Profile per Architecture]
    C[Common Workload Suite: Dense + MoE + Search] --> D[Same Scenario Run per Architecture]
    B --> D
    D --> E[Common Metrics Schema]
    E --> F[Quantified Pros/Cons per Architecture]
    F --> G[Side-by-Side Comparison Report]
```

For each architecture profile, the same run produces:

- **Throughput and latency:** TTFT, TPOT, tokens/second, and request p50/p95/p99 under identical arrival and batching behavior from TokenSim.
- **Communication cost:** bytes moved across each link class (device-to-switch, switch-to-root-complex, device-to-device peer path), and how much of that traffic is host-routed versus kept local.
- **Contention behavior:** shared-uplink utilization and oversubscription ratio, queue depth at switches and root complexes, and the point at which added devices stop producing proportional throughput.
- **Failure/service impact:** for architectures where a switch, card, or node is a shared component, the modeled blast radius when that component is degraded or unavailable.
- **Fit by workload class:** whether a large dense/MoE model versus an independent Search/reranking pool behaves better under each architecture, using the same dense/MoE/Search workload mixes defined in Section 11 and Section 17.4.

### 18.3 Concrete Comparison Output

The practical deliverable is one comparison table per workload class, generated directly from simulator runs rather than authored by hand:

| Metric | A. Full card | B. Direct E3.S | C. E3.S + switch AIC | D. OEM-integrated | E. Hybrid | F. Rack pool |
|---|---|---|---|---|---|---|
| TTFT / TPOT at target load | measured | measured | measured | measured | measured | measured (once Section 18.1's network layer exists) |
| Sustainable requests/sec before SLO miss | measured | measured | measured | measured | measured | measured |
| Shared-uplink utilization at saturation | n/a (internal switch) | n/a (no switch) | measured | measured | measured | measured |
| Host-routed vs local bytes | measured | measured | measured | measured | measured | measured |
| Throughput lost per added device beyond knee point | measured | measured | measured | measured | measured | measured |
| Degraded-component blast radius | modeled | modeled | modeled | modeled | modeled | modeled |

This is the same fit/no-fit and sensitivity output already produced elsewhere in this proposal (Section 12, Section 13, Section 15) — the architecture options document simply becomes one more dimension that gets swept, alongside device count, OEM mix, and model mix, using the Tier C fleet digital twin from Section 17 before any of these architectures are built or racked.

### 18.4 What the Emulator Cannot Settle by Itself

The simulator quantifies communication, contention, and throughput trade-offs, but it does not replace the qualification work the architecture options document also calls for: mechanical fit, power/thermal validation under sustained compute, firmware/BMC lifecycle, RMA and service-model decisions, and OEM support commercials. Those remain physical-lab and program decisions (Tier A/B in Section 17), informed by, but not answered by, the simulated comparison.
