# Observability Dashboard Guide

## Prometheus Metrics

### Request Metrics

```promql
# Total requests by method
sum(rate(requests_total[5m])) by (method)

# Request rate
rate(requests_total[5m])
```

### Admission Metrics

```promql
# Admission rate
rate(admission_requests_total[5m])

# Rejection rate by reason
rate(admission_rejected_total[5m]) by (reason)

# Degradation rate
rate(admission_degraded_total[5m])

# Global inflight utilization
admission_global_inflight / admission_global_limit
```

### Routing Metrics

```promql
# Routing decisions by sticky status
rate(routing_decisions_total[5m]) by (sticky)

# Sticky hit rate
rate(routing_decisions_total{sticky="hit"}[5m]) / rate(routing_decisions_total[5m])

# Score components (average)
avg(routing_score) by (component)
```

### Pod Metrics

```promql
# Healthy pods
sum(pod_healthy) by (pod_id)

# Inflight by pod
pod_inflight by (pod_id)

# GPU headroom by pod
pod_gpu_headroom_ratio by (pod_id)

# KV pressure by pod
pod_kv_pressure_ratio by (pod_id)

# Pod latency (EWMA)
pod_latency_ewma_ms by (pod_id)

# Pod error rate
pod_error_rate by (pod_id)
```

### Backend Metrics

```promql
# Backend success rate
rate(backend_requests_total{status="success"}[5m]) / rate(backend_requests_total[5m])

# Backend latency (p50, p95, p99)
histogram_quantile(0.50, rate(backend_latency_ms_bucket[5m]))
histogram_quantile(0.95, rate(backend_latency_ms_bucket[5m]))
histogram_quantile(0.99, rate(backend_latency_ms_bucket[5m]))

# Backend timeout rate
rate(backend_requests_total{status="timeout"}[5m]) / rate(backend_requests_total[5m])
```

### Session Metrics

```promql
# Active sessions
sessions_active_total

# Sticky hit rate
rate(session_sticky_hits_total[5m]) / (rate(session_sticky_hits_total[5m]) + rate(session_sticky_misses_total[5m]))
```

## Grafana Dashboard Panels

### Panel 1: Request Overview
- **Type:** Stat + Time series
- **Metrics:**
  - `sum(rate(requests_total[5m]))` - Request rate
  - `sum(rate(admission_rejected_total[5m]))` - Rejection rate
  - `sum(rate(admission_degraded_total[5m]))` - Degradation rate

### Panel 2: Admission Status
- **Type:** Gauge + Time series
- **Metrics:**
  - `admission_global_inflight / admission_global_limit` - Global utilization
  - `rate(admission_rejected_total[5m]) by (reason)` - Rejections by reason

### Panel 3: Pod Health
- **Type:** Table + Time series
- **Metrics:**
  - `pod_healthy by (pod_id)` - Health status
  - `pod_inflight by (pod_id)` - Current load
  - `pod_gpu_headroom_ratio by (pod_id)` - Memory headroom

### Panel 4: KV Pressure
- **Type:** Heatmap + Time series
- **Metrics:**
  - `pod_kv_pressure_ratio by (pod_id)` - Pressure per pod
  - `pod_kv_pressure_level by (pod_id)` - Pressure level

### Panel 5: Routing Decisions
- **Type:** Pie chart + Time series
- **Metrics:**
  - `rate(routing_decisions_total[5m]) by (sticky)` - Sticky distribution
  - `rate(routing_decisions_total{sticky="hit"}[5m]) / rate(routing_decisions_total[5m])` - Hit rate

### Panel 6: Backend Performance
- **Type:** Time series
- **Metrics:**
  - `histogram_quantile(0.95, rate(backend_latency_ms_bucket[5m])) by (pod_id)` - P95 latency
  - `rate(backend_requests_total{status="error"}[5m]) by (pod_id)` - Error rate
  - `rate(backend_requests_total{status="timeout"}[5m]) by (pod_id)` - Timeout rate

### Panel 7: Score Components
- **Type:** Time series
- **Metrics:**
  - `avg(routing_score) by (component)` - Average score per component

### Panel 8: Session Stickiness
- **Type:** Stat + Time series
- **Metrics:**
  - `sessions_active_total` - Active sessions
  - Sticky hit rate over time

## Alerting Rules (Example)

```yaml
groups:
  - name: sentinel-alerts
    rules:
      # No healthy pods
      - alert: NoHealthyPods
        expr: sum(pod_healthy) == 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "No healthy pods available"
      
      # High rejection rate
      - alert: HighRejectionRate
        expr: rate(admission_rejected_total[5m]) / rate(admission_requests_total[5m]) > 0.1
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Rejection rate above 10%"
      
      # High KV pressure
      - alert: HighKVPressure
        expr: pod_kv_pressure_ratio > 0.85
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "KV pressure above 85% on {{ $labels.pod_id }}"
      
      # Backend latency
      - alert: HighBackendLatency
        expr: histogram_quantile(0.95, rate(backend_latency_ms_bucket[5m])) > 5000
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "P95 backend latency above 5s"
      
      # Backend error rate
      - alert: HighBackendErrorRate
        expr: rate(backend_requests_total{status="error"}[5m]) / rate(backend_requests_total[5m]) > 0.05
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Backend error rate above 5%"
```

## Tracing Integration

### Jaeger/Zipkin Setup

Add to `Cargo.toml`:
```toml
tracing-opentelemetry = "0.22"
opentelemetry = "0.21"
opentelemetry-jaeger = "0.20"
```

Initialize in `main.rs`:
```rust
use tracing_opentelemetry::OpenTelemetryLayer;
use opentelemetry_jaeger::new_collector_pipeline;

// Initialize OpenTelemetry
let tracer = new_collector_pipeline()
    .with_endpoint("http://localhost:14268/api/traces")
    .with_service_name("sentinel-control-plane")
    .install_simple()
    .unwrap();

// Add OpenTelemetry layer
tracing_subscriber::registry()
    .with(EnvFilter::new("info,sentinel_control_plane=debug"))
    .with(tracing_subscriber::fmt::layer().json())
    .with(OpenTelemetryLayer::new(tracer))
    .init();
```

### Log Correlation

All logs include trace IDs for correlation:
```json
{
  "timestamp": "2026-03-11T12:00:00Z",
  "level": "INFO",
  "fields": {
    "message": "Routing decision made",
    "chosen_pod": "pod-2",
    "score": 0.85,
    "trace_id": "abc123"
  },
  "target": "sentinel_control_plane"
}
```

## Metric Naming Conventions

- **Counters:** `{name}_total` (e.g., `requests_total`)
- **Gauges:** `{name}` (e.g., `pod_inflight`)
- **Histograms:** `{name}_bucket`, `{name}_sum`, `{name}_count` (e.g., `backend_latency_ms_bucket`)
- **Labels:** snake_case (e.g., `pod_id`, `gpu_headroom_ratio`)

## Best Practices

1. **Use rate() for counters** - Always use `rate()` or `increase()` with counters
2. **Label cardinality** - Keep label values low-cardinality (e.g., pod_id, not request_id)
3. **Histogram buckets** - Use default buckets or customize based on SLOs
4. **Recording rules** - Pre-compute expensive queries for dashboards
5. **Retention** - Set appropriate retention for metrics (e.g., 30d for detailed, 1y for aggregates)
