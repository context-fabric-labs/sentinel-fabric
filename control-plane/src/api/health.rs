use axum::{
    extract::State,
    http::StatusCode,
    routing::get,
    Json, Router,
};
use serde::Serialize;
use tracing::info;

use crate::app_state::AppState;

/// Health check response
#[derive(Debug, Serialize)]
pub struct HealthResponse {
    pub status: String,
    pub healthy_pods: usize,
    pub total_pods: usize,
}

/// Create health router
pub fn create_router() -> Router<AppState> {
    Router::new()
        .route("/health", get(health_handler))
        .route("/ready", get(ready_handler))
}

/// Basic health check
pub async fn health_handler() -> &'static str {
    "OK"
}

/// Readiness check (checks if we have healthy pods)
pub async fn ready_handler(State(state): State<AppState>) -> Result<Json<HealthResponse>, StatusCode> {
    let healthy = state.pod_registry.healthy_count().await;
    let total = state.pod_registry.total_count().await;

    info!("Readiness check: {}/{} pods healthy", healthy, total);

    let response = HealthResponse {
        status: if healthy > 0 { "healthy" } else { "unhealthy" }.to_string(),
        healthy_pods: healthy,
        total_pods: total,
    };

    if healthy > 0 {
        Ok(Json(response))
    } else {
        Err(StatusCode::SERVICE_UNAVAILABLE)
    }
}
