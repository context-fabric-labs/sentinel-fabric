use axum::{
    extract::State,
    http::StatusCode,
    routing::post,
    Json, Router,
};
use serde::{Deserialize, Serialize};
use tracing::info;

use crate::app_state::AppState;
use crate::scheduler::{PodScorer, RequestShape, RoutingDecision, ScoringWeights, StickyRoutingInfo, hrw_select_pods};
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

        // Route with sticky logic
        route_with_sticky(&state, request_shape, req.max_inflight_override).await
    } else {
        // Skip admission check (for testing)
        route_with_sticky(&state, req.request_shape, req.max_inflight_override).await
    }
}

/// Perform routing with sticky session support
async fn route_with_sticky(
    state: &AppState,
    request_shape: RequestShape,
    max_inflight_override: Option<u32>,
) -> Result<Json<RouteDebugResponse>, StatusCode> {
    // Get all pods
    let pods: Vec<_> = state.pod_registry.get_all_pods().await;

    if pods.is_empty() {
        return Ok(Json(RouteDebugResponse {
            admission: None,
            decision: None,
            error: Some("No pods available".to_string()),
        }));
    }

    // Get pod IDs for HRW
    let pod_ids: Vec<String> = pods.iter().map(|p| p.config.id.clone()).collect();

    // Determine sticky routing info if session_id provided
    let sticky_info = if let Some(ref session_id) = request_shape.session_id {
        // Get HRW ranking
        let hrw_ranking = hrw_select_pods(session_id, &pod_ids);
        
        if hrw_ranking.is_empty() {
            None
        } else {
            let preferred_pod = &hrw_ranking[0].pod_id;
            
            // Check if preferred pod is healthy and has capacity
            let preferred_pod_state = pods.iter().find(|p| p.config.id == *preferred_pod);
            let (sticky_hit, fallback_reason, chosen_rank) = if let Some(pod) = preferred_pod_state {
                if !pod.snapshot.is_healthy {
                    // Fallback: preferred pod is unhealthy
                    (false, Some("unhealthy".to_string()), hrw_ranking[0].rank)
                } else {
                    // Preferred pod is healthy - check if it's the best by score
                    let max_inflight = max_inflight_override.unwrap_or(100);
                    let scorer = PodScorer::new(ScoringWeights::default(), max_inflight);
                    let scores = scorer.score_all_candidates(&pods, &request_shape);
                    
                    // Find preferred pod in scores
                    let preferred_score = scores.iter().find(|s| s.pod_id == *preferred_pod);
                    let best_healthy = scores.iter().find(|s| s.is_healthy);
                    
                    if let (Some(pref), Some(best)) = (preferred_score, best_healthy) {
                        if pref.pod_id == best.pod_id {
                            // Preferred pod is also the best by score - sticky hit!
                            (true, None, hrw_ranking[0].rank)
                        } else {
                            // Preferred is healthy but not best - still use it for stickiness
                            (true, None, hrw_ranking[0].rank)
                        }
                    } else {
                        (false, Some("capacity".to_string()), hrw_ranking[0].rank)
                    }
                }
            } else {
                (false, Some("unhealthy".to_string()), hrw_ranking[0].rank)
            };
            
            // Record metrics
            telemetry::record_routing_decision(true, sticky_hit, fallback_reason.as_deref());
            
            Some(StickyRoutingInfo {
                session_id: session_id.clone(),
                hrw_preferred_pod: preferred_pod.clone(),
                sticky_hit,
                fallback_reason,
                chosen_pod_hrw_rank: chosen_rank,
            })
        }
    } else {
        // No session - just record as miss
        telemetry::record_routing_decision(true, false, None);
        None
    };

    // Create scorer
    let max_inflight = max_inflight_override.unwrap_or(100);
    let scorer = PodScorer::new(ScoringWeights::default(), max_inflight);

    // Score all candidates
    let candidate_scores = scorer.score_all_candidates(&pods, &request_shape);

    // Choose pod: prefer sticky pod if available, otherwise best by score
    let chosen = if let Some(ref sticky) = sticky_info {
        // Try to use sticky preferred pod
        candidate_scores.iter().find(|s| s.pod_id == sticky.hrw_preferred_pod && s.is_healthy)
            .or_else(|| candidate_scores.iter().find(|s| s.is_healthy))
    } else {
        // No sticky - just pick best healthy
        candidate_scores.iter().find(|s| s.is_healthy)
    };

    let chosen_pod_id = chosen.map(|s| s.pod_id.clone());
    let chosen_pod_address = chosen.and_then(|s| {
        pods.iter()
            .find(|p| p.config.id == s.pod_id)
            .map(|p| p.config.address.clone())
    });
    
    let reason = if sticky_info.is_some() {
        if let Some(ref sticky) = sticky_info {
            if sticky.sticky_hit {
                format!("Sticky routing to HRW-preferred pod (rank {})", sticky.chosen_pod_hrw_rank)
            } else {
                format!("Sticky fallback: {} (using next best)", sticky.fallback_reason.as_deref().unwrap_or("unknown"))
            }
        } else {
            "Sticky routing with fallback".to_string()
        }
    } else if chosen.is_some() {
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
        sticky_info,
    };

    // Update session metrics
    let session_count = state.session_map.active_count().await;
    telemetry::update_session_metrics(session_count);

    Ok(Json(RouteDebugResponse {
        admission: None, // Already handled in caller
        decision: Some(decision),
        error: None,
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
    async fn test_sticky_routing_with_session() {
        let state = create_test_state();
        let app = create_router().with_state(state);

        let request_body = serde_json::json!({
            "model_id": "llama-8b",
            "prompt_tokens_est": 1000,
            "max_tokens": 500,
            "session_id": "test-session-123",
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
        
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let result: RouteDebugResponse = serde_json::from_slice(&body).unwrap();
        assert!(result.decision.is_some());
        assert!(result.decision.as_ref().unwrap().sticky_info.is_some());
    }

    #[tokio::test]
    async fn test_sticky_routing_without_session() {
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
        
        let body = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        let result: RouteDebugResponse = serde_json::from_slice(&body).unwrap();
        assert!(result.decision.is_some());
        assert!(result.decision.as_ref().unwrap().sticky_info.is_none());
    }

    #[tokio::test]
    async fn test_same_session_same_pod() {
        let state = create_test_state();
        
        let session_id = "consistent-session-test";
        
        // Make two requests with same session
        for _ in 0..2 {
            let app = create_router().with_state(state.clone());
            let request_body = serde_json::json!({
                "model_id": "llama-8b",
                "prompt_tokens_est": 1000,
                "max_tokens": 500,
                "session_id": session_id,
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
            
            let body = axum::body::to_bytes(response.into_body(), usize::MAX)
                .await
                .unwrap();
            let result: RouteDebugResponse = serde_json::from_slice(&body).unwrap();
            
            // Should have sticky info
            assert!(result.decision.as_ref().unwrap().sticky_info.is_some());
            // Should be a sticky hit (or fallback with reason)
            let sticky = result.decision.as_ref().unwrap().sticky_info.as_ref().unwrap();
            assert!(sticky.sticky_hit || sticky.fallback_reason.is_some());
        }
    }
}
