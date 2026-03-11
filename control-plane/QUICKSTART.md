# Quick Reference - Story 1.1

## Start Server
```bash
cd control-plane
cargo run
```

## Test Endpoints

### Health Check
```bash
curl http://localhost:8080/health
```

### Readiness Check
```bash
curl http://localhost:8080/ready
```

### Debug Routing
```bash
curl -X POST http://localhost:8080/debug/route \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "llama-8b",
    "prompt_tokens_est": 4096,
    "max_tokens": 512,
    "session_id": "conversation-123"
  }' | jq
```

### Test with Load Override
```bash
curl -X POST http://localhost:8080/debug/route \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "llama-8b",
    "prompt_tokens_est": 2048,
    "max_tokens": 1024,
    "max_inflight_override": 10
  }' | jq
```

## Run Tests
```bash
# All tests
cargo test

# Specific module
cargo test scheduler::score

# With output
cargo test -- --nocapture

# Single test
cargo test test_candidate_ranking -- --nocapture
```

## Build
```bash
# Debug build
cargo build

# Release build
cargo build --release

# Check only
cargo check
```

## Configuration

### Edit Pod Config
```yaml
# config/pods.yaml
pods:
  - id: pod-1
    address: http://localhost:8001
    model_id: llama-8b
    gpu_memory_mb: 80000
    num_layers: 32
    hidden_size: 4096
    weight: 1.0
```

### Custom Config Path
```bash
CONFIG_PATH=config/my-pods.yaml cargo run
```

## Key Files

| File | Purpose |
|------|---------|
| `src/main.rs` | Entry point, server startup |
| `src/config.rs` | PodConfig, Config structs |
| `src/scheduler/score.rs` | Scoring algorithm |
| `src/state/pod_registry.rs` | Pod state management |
| `src/api/debug_route.rs` | /debug/route endpoint |
| `config/pods.yaml` | Pod configuration |

## Scoring Formula

```
score = (inflight + gpu_headroom + latency + error_rate) * weight

Default weights:
- GPU headroom: 0.4
- Inflight: 0.3
- Latency: 0.2
- Error rate: 0.1
```

## Common Issues

### Port Already in Use
```bash
# Kill process on port 8080
lsof -ti:8080 | xargs kill -9
```

### Tests Failing
```bash
# Clean and rebuild
cargo clean
cargo test
```

### Config Load Error
```bash
# Verify YAML syntax
cat config/pods.yaml | python3 -c "import yaml,sys; yaml.safe_load(sys.stdin)"
```

## Architecture Diagram

```
┌─────────────────────────────────────────┐
│         API Server (Axum)               │
│  /health  /ready  /debug/route          │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│         PodRegistry                     │
│  ┌────────┐ ┌────────┐ ┌────────┐      │
│  │ Config │ │Snapshot│ │ State  │      │
│  └────────┘ └────────┘ └────────┘      │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│         PodScorer                       │
│  ┌──────────┐ ┌──────────┐             │
│  │ Inflight │ │ GPU      │             │
│  │ Score    │ │ Headroom │             │
│  └──────────┘ └──────────┘             │
│  ┌──────────┐ ┌──────────┐             │
│  │ Latency  │ │ Error    │             │
│  │ Score    │ │ Rate     │             │
│  └──────────┘ └──────────┘             │
└─────────────────────────────────────────┘
```
