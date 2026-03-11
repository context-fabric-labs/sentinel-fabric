use axum::{
    extract::State,
    http::StatusCode,
    routing::{get, post},
    Json, Router,
};
use serde::{Deserialize, Serialize};
use tracing::info;

use crate::app_state::AppState;
use crate::admission::{AdmissionDecision, AdmissionMetrics};
use crate::telemetry;

/// Response from admission status endpoint
#[derive(Debug, Serialize)]
pub struct AdmissionStatusResponse {
    pub metrics: AdmissionMetrics,
    pub policy: AdmissionPolicyResponse,
}

#[derive(Debug, Serialize)]
pub struct AdmissionPolicyResponse {
    pub global_max_inflight: u32,
    pub per_pod_max_inflight: u32,
    pub degradation_enabled: bool,
    pub degradation_threshold: f64,
    pub degradation_factor: f64,
}

/// Request to simulate load for testing
#[derive(Debug, Deserialize)]
pub struct LoadTestRequest {
    /// Number of requests to admit
    pub admit_count: u32,
    /// Optional specific pod to target
    pub pod_id: Option<String>,
}

/// Response from load test endpoint
#[derive(Debug, Serialize, Deserialize)]
pub struct LoadTestResponse {
    pub admitted: u32,
    pub rejected: u32,
    pub final_metrics: AdmissionMetrics,
}

/// Create admission router
pub fn create_router() -> Router<AppState> {
    Router::new()
        .route("/admission/status", get(admission_status_handler))
        .route("/admission/test/load", post(load_test_handler))
}

/// Get current admission status
pub async fn admission_status_handler(
    State(state): State<AppState>,
) -> Result<Json<AdmissionStatusResponse>, StatusCode> {
    let metrics = state.admission_controller.get_metrics().await;
    let policy = state.admission_controller.policy();

    let response = AdmissionStatusResponse {
        metrics,
        policy: AdmissionPolicyResponse {
            global_max_inflight: policy.global_max_inflight,
            per_pod_max_inflight: policy.per_pod_max_inflight,
            degradation_enabled: policy.degrade_policy.enabled,
            degradation_threshold: policy.degrade_policy.threshold,
            degradation_factor: policy.degrade_policy.degradation_factor,
        },
    };

    Ok(Json(response))
}

/// Simulate load for testing admission control
pub async fn load_test_handler(
    State(state): State<AppState>,
    Json(req): Json<LoadTestRequest>,
) -> Result<Json<LoadTestResponse>, StatusCode> {
    info!(
        admit_count = req.admit_count,
        pod_id = ?req.pod_id,
        "Load test request"
    );

    let mut admitted = 0u32;
    let mut rejected = 0u32;

    for _ in 0..req.admit_count {
        // Check admission
        let decision = state.admission_controller.check_admission(512).await;
        
        if decision.admitted {
            // Try to admit globally first
            let global_admitted = state.admission_controller.counters().try_admit_global();
            
            if global_admitted {
                // Try to admit to a specific pod or any pod
                let success = if let Some(ref pod_id) = req.pod_id {
                    state.admission_controller.counters().try_admit_pod(pod_id).await
                } else {
                    // Try to find a pod with capacity
                    let pod_counts = state.admission_controller.counters().get_all_pod_counts().await;
                    let mut success = false;
                    for (pod_id, _) in pod_counts {
                        if state.admission_controller.counters().try_admit_pod(&pod_id).await {
                            success = true;
                            break;
                        }
                    }
                    if !success {
                        // Rollback global admission
                        state.admission_controller.counters().release_global();
                    }
                    success
                };

                if success {
                    admitted += 1;
                } else {
                    rejected += 1;
                }
            } else {
                rejected += 1;
            }
        } else {
            rejected += 1;
        }
    }

    let final_metrics = state.admission_controller.get_metrics().await;
    telemetry::update_admission_metrics(&final_metrics);

    Ok(Json(LoadTestResponse {
        admitted,
        rejected,
        final_metrics,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::{Config, PodConfig};
    use axum::body::Body;
    use axum::http::Request;
    use tower::util::ServiceExt;

    fn create_test_state() -> AppState {
        let configs = vec![
            PodConfig {
                id: "pod-1".to_string(),
                address: "http://localhost:8001".to_string(),
                name: None,
                model_id: "llama-8b".to_string(),
                gpu_memory_mb: 80000,
                num_layers: 32,
                hidden_size: 4096,
                weight: 1.0,
            },
            PodConfig {
                id: "pod-2".to_string(),
                address: "http://localhost:8002".to_string(),
                name: None,
                model_id: "llama-8b".to_string(),
                gpu_memory_mb: 80000,
                num_layers: 32,
                hidden_size: 4096,
                weight: 1.0,
            },
        ];

        let config = Config {
            pods: configs,
            health_check_interval_secs: 5,
            health_check_timeout_secs: 3,
            metrics_port: 9090,
            api_port: 8080,
        };

        AppState::new(config)
    }

    #[tokio::test]
    async fn test_admission_status() {
        let state = create_test_state();
        let app = create_router().with_state(state);

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/admission/status")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn test_load_test_admission() {
        let state = create_test_state();
        let app = create_router().with_state(state);

        // Admit 5 requests
        let request_body = serde_json::json!({
            "admit_count": 5,
        });

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/admission/test/load")
                    .header("Content-Type", "application/json")
                    .body(Body::from(request_body.to_string()))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        
        // All 5 should be admitted (default limit is 1000)
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let result: LoadTestResponse = serde_json::from_slice(&body).unwrap();
        assert_eq!(result.admitted, 5);
        assert_eq!(result.rejected, 0);
        assert_eq!(result.final_metrics.global_inflight, 5);
    }
}
