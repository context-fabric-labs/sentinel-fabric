use axum::{
    extract::State,
    http::StatusCode,
    routing::post,
    Json, Router,
};
use serde::{Deserialize, Serialize};
use tracing::info;

use crate::app_state::AppState;
use crate::scheduler::{PodScorer, RequestShape, RoutingDecision, ScoringWeights};
use crate::admission::AdmissionDecision;
use crate::telemetry;

/// Request to debug routing decision
#[derive(Debug, Deserialize)]
pub struct RouteDebugRequest {
    #[serde(flatten)]
    pub request_shape: RequestShape,
    /// Optional: override max inflight for testing
    pub max_inflight_override: Option<u32>,
    /// Optional: skip admission check for testing
    #[serde(default)]
    pub skip_admission: bool,
}

/// Response from debug routing endpoint
#[derive(Debug, Serialize, Deserialize)]
pub struct RouteDebugResponse {
    pub admission: Option<AdmissionDecision>,
    pub decision: Option<RoutingDecision>,
    pub error: Option<String>,
}

/// Create the debug router
pub fn create_router() -> Router<AppState> {
    Router::new().route("/debug/route", post(route_debug_handler))
}

/// Handler for /debug/route endpoint
pub async fn route_debug_handler(
    State(state): State<AppState>,
    Json(req): Json<RouteDebugRequest>,
) -> Result<Json<RouteDebugResponse>, StatusCode> {
    info!(
        model_id = %req.request_shape.model_id,
        prompt_tokens = req.request_shape.prompt_tokens_est,
        max_tokens = req.request_shape.max_tokens,
        session_id = ?req.request_shape.session_id,
        "Debug route request received"
    );

    // Check admission first (unless skipped)
    if !req.skip_admission {
        let admission = state.admission_controller.check_admission(req.request_shape.max_tokens).await;
        
        // Record metrics
        telemetry::record_admission(admission.admitted, admission.was_degraded, &admission.reason);
        
        // Update admission metrics
        let metrics = state.admission_controller.get_metrics().await;
        telemetry::update_admission_metrics(&metrics);

        if !admission.admitted {
            info!(
                reason = ?admission.reason,
                global_inflight = metrics.global_inflight,
                "Request rejected by admission controller"
            );
            
            return Ok(Json(RouteDebugResponse {
                admission: Some(admission),
                decision: None,
                error: Some("Request rejected by admission controller".to_string()),
            }));
        }

        // If degraded, use degraded max_tokens for routing
        let mut request_shape = req.request_shape;
        if admission.was_degraded {
            request_shape.max_tokens = admission.degraded_max_tokens;
            info!(
                original = admission.original_max_tokens,
                degraded = admission.degraded_max_tokens,
                "Request degraded"
            );
        }

        // Get all pods
        let pods: Vec<_> = state.pod_registry.get_all_pods().await;

        if pods.is_empty() {
            return Ok(Json(RouteDebugResponse {
                admission: Some(admission),
                decision: None,
                error: Some("No pods available".to_string()),
            }));
        }

        // Create scorer with optional override
        let max_inflight = req.max_inflight_override.unwrap_or(100);
        let scorer = PodScorer::new(ScoringWeights::default(), max_inflight);

        // Score all candidates
        let candidate_scores = scorer.score_all_candidates(&pods, &request_shape);

        // Choose best healthy pod
        let chosen = candidate_scores.iter().find(|s| s.is_healthy);
        let chosen_pod_id = chosen.map(|s| s.pod_id.clone());
        let chosen_pod_address = chosen.and_then(|s| {
            pods.iter()
                .find(|p| p.config.id == s.pod_id)
                .map(|p| p.config.address.clone())
        });
        let reason = if chosen.is_some() {
            "Best healthy pod by composite score".to_string()
        } else {
            "No healthy pods available".to_string()
        };

        let decision = RoutingDecision {
            chosen_pod_id,
            chosen_pod_address,
            candidate_scores,
            reason,
            request_shape,
        };

        Ok(Json(RouteDebugResponse {
            admission: Some(admission),
            decision: Some(decision),
            error: None,
        }))
    } else {
        // Skip admission check (for testing)
        let pods: Vec<_> = state.pod_registry.get_all_pods().await;

        if pods.is_empty() {
            return Err(StatusCode::SERVICE_UNAVAILABLE);
        }

        let max_inflight = req.max_inflight_override.unwrap_or(100);
        let scorer = PodScorer::new(ScoringWeights::default(), max_inflight);
        let candidate_scores = scorer.score_all_candidates(&pods, &req.request_shape);

        let chosen = candidate_scores.iter().find(|s| s.is_healthy);
        let chosen_pod_id = chosen.map(|s| s.pod_id.clone());
        let chosen_pod_address = chosen.and_then(|s| {
            pods.iter()
                .find(|p| p.config.id == s.pod_id)
                .map(|p| p.config.address.clone())
        });
        let reason = if chosen.is_some() {
            "Best healthy pod by composite score".to_string()
        } else {
            "No healthy pods available".to_string()
        };

        let decision = RoutingDecision {
            chosen_pod_id,
            chosen_pod_address,
            candidate_scores,
            reason,
            request_shape: req.request_shape,
        };

        Ok(Json(RouteDebugResponse {
            admission: None,
            decision: Some(decision),
            error: None,
        }))
    }
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
    async fn test_debug_route_basic() {
        let state = create_test_state();
        let app = create_router().with_state(state);

        let request_body = serde_json::json!({
            "model_id": "llama-8b",
            "prompt_tokens_est": 1000,
            "max_tokens": 500,
            "skip_admission": true,
        });

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/debug/route")
                    .header("Content-Type", "application/json")
                    .body(Body::from(request_body.to_string()))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn test_debug_route_with_admission() {
        let state = create_test_state();
        let app = create_router().with_state(state);

        let request_body = serde_json::json!({
            "model_id": "llama-8b",
            "prompt_tokens_est": 2048,
            "max_tokens": 1024,
        });

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/debug/route")
                    .header("Content-Type", "application/json")
                    .body(Body::from(request_body.to_string()))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let result: RouteDebugResponse = serde_json::from_slice(&body).unwrap();
        assert!(result.admission.is_some());
        assert!(result.admission.unwrap().admitted);
    }
}
