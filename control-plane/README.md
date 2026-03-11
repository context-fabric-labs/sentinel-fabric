# Control Plane - Story 1.1

Pod registry + health model + routing score skeleton.

## What This Story Implements

✅ Static configuration of backend pods  
✅ In-memory pod state tracking  
✅ Health snapshot ingestion (ready for real health checks)  
✅ Request shape extraction  
✅ Candidate scoring with explainable breakdown  
✅ `/debug/route` endpoint showing routing decisions  
✅ Basic Prometheus-compatible metrics  

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    API Server (Axum)                     │
├─────────────────────────────────────────────────────────┤
│  /health      │  /ready       │  /debug/route          │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                   PodRegistry (State)                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  PodConfig   │  │ PodSnapshot  │  │  PodState    │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                  PodScorer (Scheduler)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  Inflight    │  │ GPU Headroom │  │   Latency    │  │
│  │   Score      │  │    Score     │  │    Score     │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
│  ┌──────────────┐  ┌──────────────┐                     │
│  │  Error Rate  │  │    Weight    │                     │
│  │    Score     │  │  Multiplier  │                     │
│  └──────────────┘  └──────────────┘                     │
└─────────────────────────────────────────────────────────┘
```

## File Structure

```
src/
├── main.rs              # Entry point, server startup
├── app_state.rs         # Shared application state
├── config.rs            # Configuration structs (PodConfig, Config)
├── api/
│   ├── mod.rs
│   ├── health.rs        # /health, /ready endpoints
│   └── debug_route.rs   # /debug/route endpoint
├── scheduler/
│   ├── mod.rs
│   ├── types.rs         # RequestShape, PodScore, RoutingDecision
│   └── score.rs         # PodScorer, scoring logic
├── state/
│   ├── mod.rs
│   └── pod_registry.rs  # PodRegistry, PodState, PodHealthSnapshot
└── telemetry/
    ├── mod.rs
    ├── metrics.rs       # Prometheus metrics
    └── tracing.rs       # Tracing setup
```

## Key Data Structures

### PodConfig
```rust
pub struct PodConfig {
    pub id: String,              // "pod-1"
    pub address: String,         // "http://localhost:8001"
    pub model_id: String,        // "llama-8b"
    pub gpu_memory_mb: u64,      // 80000
    pub num_layers: u32,         // 32
    pub hidden_size: u32,        // 4096
    pub weight: f64,             // 1.0
}
```

### PodHealthSnapshot
```rust
pub struct PodHealthSnapshot {
    pub is_healthy: bool,        // Health check status
    pub inflight_count: u32,     // Active requests
    pub gpu_memory_used_mb: u64, // GPU memory usage
    pub latency_ewma_ms: f64,    // Recent latency
    pub error_rate: f64,         // Recent error rate
}
```

### PodScore
```rust
pub struct PodScore {
    pub pod_id: String,
    pub total_score: f64,
    pub score_inflight: f64,      // Component breakdown
    pub score_gpu_headroom: f64,  // Component breakdown
    pub score_latency: f64,       // Component breakdown
    pub score_error_rate: f64,    // Component breakdown
    pub weight_multiplier: f64,
    pub is_healthy: bool,
    pub exclusion_reason: Option<String>,
}
```

## Scoring Model

Each pod receives a composite score from weighted components:

```
total_score = (inflight + gpu_headroom + latency + error_rate) * weight

where:
  - inflight_score = 1.0 - (inflight_count / max_inflight)
  - gpu_headroom_score = 1.0 - gpu_memory_utilization
  - latency_score = 1.0 - min(latency_ms / 1000, 1.0)
  - error_rate_score = 1.0 - error_rate
```

**Default weights:**
- Inflight: 0.3
- GPU headroom: 0.4 (highest priority)
- Latency: 0.2
- Error rate: 0.1

Unhealthy pods are filtered out before scoring.

## API Endpoints

### GET /health
Basic liveness check. Returns `200 OK` with body `OK`.

### GET /ready
Readiness check. Returns pod health status:
```json
{
  "status": "healthy",
  "healthy_pods": 3,
  "total_pods": 3
}
```

### POST /debug/route
Debug routing decision. Shows which pod would be chosen and why.

**Request:**
```json
{
  "model_id": "llama-8b",
  "prompt_tokens_est": 4096,
  "max_tokens": 512,
  "session_id": "abc-123"
}
```

**Response:**
```json
{
  "decision": {
    "chosen_pod_id": "pod-2",
    "chosen_pod_address": "http://localhost:8002",
    "candidate_scores": [
      {
        "pod_id": "pod-2",
        "total_score": 1.35,
        "score_inflight": 0.9,
        "score_gpu_headroom": 0.8,
        "score_latency": 0.95,
        "score_error_rate": 1.0,
        "weight_multiplier": 1.0,
        "is_healthy": true
      },
      {
        "pod_id": "pod-1",
        "total_score": 1.10,
        "score_inflight": 0.7,
        "score_gpu_headroom": 0.6,
        "score_latency": 0.9,
        "score_error_rate": 0.9,
        "weight_multiplier": 1.0,
        "is_healthy": true
      }
    ],
    "reason": "Best healthy pod by composite score",
    "request_shape": {
      "model_id": "llama-8b",
      "prompt_tokens_est": 4096,
      "max_tokens": 512,
      "session_id": "abc-123"
    }
  }
}
```

## Running the Server

```bash
cd control-plane

# Use default config
cargo run

# Or specify custom config path
CONFIG_PATH=config/my-pods.yaml cargo run
```

Server starts on port 8080 (configurable in `config/pods.yaml`).

## Testing

```bash
# Run all tests
cargo test

# Run specific test module
cargo test scheduler::score::tests

# Run with output
cargo test -- --nocapture
```

## Example Usage

### 1. Check health
```bash
curl http://localhost:8080/health
# OK

curl http://localhost:8080/ready
# {"status":"healthy","healthy_pods":3,"total_pods":3}
```

### 2. Debug routing decision
```bash
curl -X POST http://localhost:8080/debug/route \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "llama-8b",
    "prompt_tokens_est": 2048,
    "max_tokens": 512
  }' | jq
```

### 3. Test with different load scenarios
```bash
# Simulate high-load scenario (override max_inflight to 10)
curl -X POST http://localhost:8080/debug/route \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "llama-8b",
    "prompt_tokens_est": 4096,
    "max_tokens": 1024,
    "max_inflight_override": 10
  }' | jq
```

## Metrics

Prometheus-compatible metrics (future: exposed on `/metrics`):

- `pods_healthy_total` - Number of healthy pods
- `pods_unhealthy_total` - Number of unhealthy pods
- `requests_total` - Total requests received
- `requests_admitted_total` - Requests admitted
- `requests_rejected_total` - Requests rejected
- `routing_decisions_total` - Total routing decisions
- `scoring_inflight_avg` - Average inflight score
- `scoring_gpu_headroom_avg` - Average GPU headroom score

## Acceptance Criteria

✅ Can configure multiple backend pods via YAML  
✅ Pods tracked in-memory with health snapshots  
✅ Scoring algorithm ranks pods by composite score  
✅ Unhealthy pods excluded from consideration  
✅ `/debug/route` shows full decision breakdown  
✅ All unit tests pass  
✅ Code is understandable end-to-end  

## What's Next (Story 2)

Story 2 will add:
- Real health check loop (HTTP polling of pod `/health` endpoints)
- Background task that updates `PodHealthSnapshot` every N seconds
- Actual inflight request tracking (increment/decrement on proxy)
- Basic request proxying to chosen backend

**Not changing:**
- Scoring algorithm (already solid)
- Data structures (already support snapshots)
- API contract (already compatible)

## Design Notes

### Why additive scoring?
Simple, explainable, easy to tune. Each component contributes independently.

### Why filter unhealthy first?
Separation of concerns: health is binary (can we route?), score is ordinal (which is best?).

### Why not weighted sum in one formula?
Component breakdown makes debugging and experiments easier. You can see exactly why a pod scored low.

### Why static config first?
K8s service discovery adds complexity. Static config lets us validate scoring logic first.

## Interview Talking Points

1. **Separation of concerns**: Config vs runtime state vs scoring logic
2. **Explainability**: Every routing decision includes full breakdown
3. **Testability**: Pure functions for scoring, mockable state
4. **Observability**: Metrics and tracing from day one
5. **Incremental design**: Score components ready for KV pressure, circuit breaker
