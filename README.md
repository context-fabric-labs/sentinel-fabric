# sentinel-fabric

A small, opinionated learning project for building KV-aware LLM inference orchestration.

## Learning Goals

- **Rust** for control-plane / systems orchestration
- **C++/CUDA** for data-plane / transfer-path / kernel work  
- **LLM serving systems design** for multi-GPU / multi-pod inference
- **GPU memory / KV-cache reasoning**
- **Nsight-based CUDA performance analysis**
- **Interview-ready systems artifacts**

## Project Structure

```
sentinel-fabric/
├── control-plane/     # Rust inference router (Epic 1)
├── data-plane/        # C++/CUDA transfer paths (future)
├── cuda-lab/          # CUDA kernel labs (future)
├── experiments/       # Benchmark scenarios (future)
└── docs/              # Writeups and interview notes
```

## Epic 1: Poor-man's Dynamo

Build a minimal Rust control plane for multi-pod LLM inference with:
- Session stickiness for KV reuse
- GPU-headroom-aware routing
- Load-aware scheduling
- Observable routing decisions

**Status:** Story 1.1 implemented ✅

### Quick Start

```bash
cd control-plane
cargo run
```

See [control-plane/README.md](control-plane/README.md) for details.

## Design Principles

- Small and understandable end-to-end
- Practical over generic
- Real working prototype over architecture diagrams
- Every feature includes: implementation + metrics + benchmark + explanation