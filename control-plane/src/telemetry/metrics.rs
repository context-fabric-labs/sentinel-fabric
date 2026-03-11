use metrics::{counter, gauge};
use std::sync::Once;

static REGISTER: Once = Once::new();

/// Register Prometheus metrics
pub fn register_metrics() {
    REGISTER.call_once(|| {
        // Pod health metrics
        gauge!("pods_healthy_total").set(0);
        gauge!("pods_unhealthy_total").set(0);

        // Request metrics
        counter!("requests_total");
        counter!("requests_admitted_total");
        counter!("requests_rejected_total");

        // Admission metrics
        gauge!("admission_global_inflight").set(0);
        gauge!("admission_global_limit").set(0);
        counter!("admission_rejected_global_overload");
        counter!("admission_rejected_no_capacity");
        counter!("admission_rejected_pod_overload");
        counter!("admission_degraded_total");

        // Scoring metrics
        gauge!("scoring_inflight_avg").set(0.0);
        gauge!("scoring_gpu_headroom_avg").set(0.0);
        gauge!("scoring_latency_avg").set(0.0);
        gauge!("scoring_error_rate_avg").set(0.0);

        // Routing decision metrics
        counter!("routing_decisions_total");
        counter!("routing_sticky_hits_total");
        counter!("routing_sticky_misses_total");

        // Per-pod inflight
        gauge!("pod_inflight_count");
    });
}

/// Record admission decision
pub fn record_admission(admitted: bool, degraded: bool, reason: &crate::admission::AdmissionReason) {
    counter!("requests_total").increment(1);
    
    if admitted {
        counter!("requests_admitted_total").increment(1);
        if degraded {
            counter!("admission_degraded_total").increment(1);
        }
    } else {
        counter!("requests_rejected_total").increment(1);
        
        // Record rejection reason
        match reason {
            crate::admission::AdmissionReason::GlobalOverload { .. } => {
                counter!("admission_rejected_global_overload").increment(1);
            }
            crate::admission::AdmissionReason::NoPodCapacity { .. } => {
                counter!("admission_rejected_no_capacity").increment(1);
            }
            crate::admission::AdmissionReason::PodOverload { .. } => {
                counter!("admission_rejected_pod_overload").increment(1);
            }
            _ => {}
        }
    }
}

/// Update admission metrics
pub fn update_admission_metrics(metrics: &crate::admission::AdmissionMetrics) {
    gauge!("admission_global_inflight").set(metrics.global_inflight as f64);
    gauge!("admission_global_limit").set(metrics.global_limit as f64);
    
    // Update per-pod inflight
    for (pod_id, count) in &metrics.pod_counts {
        gauge!("pod_inflight_count", "pod_id" => pod_id.clone()).set(*count as f64);
    }
}

/// Update pod health metrics
pub fn update_pod_health_metrics(healthy: usize, unhealthy: usize) {
    gauge!("pods_healthy_total").set(healthy as f64);
    gauge!("pods_unhealthy_total").set(unhealthy as f64);
}

/// Record a routing decision
pub fn record_routing_decision(chosen: bool, sticky_hit: bool) {
    counter!("routing_decisions_total").increment(1);
    if chosen {
        if sticky_hit {
            counter!("routing_sticky_hits_total").increment(1);
        } else {
            counter!("routing_sticky_misses_total").increment(1);
        }
    }
}

/// Record request admission/rejection
pub fn record_request_admission(admitted: bool) {
    counter!("requests_total").increment(1);
    if admitted {
        counter!("requests_admitted_total").increment(1);
    } else {
        counter!("requests_rejected_total").increment(1);
    }
}
