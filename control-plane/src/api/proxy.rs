/// Proxy endpoint for forwarding requests to backends
/// 
/// This module handles:
/// - Request forwarding to selected backend
/// - Response relay back to client
/// - Inflight tracking (increment/decrement)
/// - Timeout and error handling
/// - Metrics collection

use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::post,
    Json, Router,
};
use serde::{Deserialize, Serialize};
use tracing::{error, info, warn};

use crate::app_state::AppState;
use crate::backends::{Backend, BackendError, BackendRequest, ChatMessage};
use crate::scheduler::RequestShape;
use crate::telemetry;

/// Proxy request (OpenAI-compatible)
#[derive(Debug, Deserialize)]
pub struct ProxyRequest {
    /// Model identifier
    pub model: String,
    /// Prompt for completion API
    pub prompt: Option<String>,
    /// Messages for chat API
    pub messages: Option<Vec<ChatMessageRequest>>,
    /// Maximum tokens
    pub max_tokens: Option<u32>,
    /// Temperature
    pub temperature: Option<f32>,
    /// Stream response
    pub stream: Option<bool>,
}

#[derive(Debug, Deserialize)]
pub struct ChatMessageRequest {
    pub role: String,
    pub content: String,
}

/// Proxy response
#[derive(Debug, Serialize)]
pub struct ProxyResponse {
    pub id: String,
    pub object: String,
    pub created: i64,
    pub model: String,
    pub choices: Vec<ChoiceResponse>,
    pub usage: Option<UsageResponse>,
}

#[derive(Debug, Serialize)]
pub struct ChoiceResponse {
    pub text: String,
    pub index: u32,
    pub finish_reason: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct UsageResponse {
    pub prompt_tokens: u32,
    pub completion_tokens: u32,
    pub total_tokens: u32,
}

/// Create proxy router
pub fn create_router() -> Router<AppState> {
    Router::new()
        .route("/v1/completions", post(completions_handler))
        .route("/v1/chat/completions", post(chat_completions_handler))
}

/// Handle /v1/completions
pub async fn completions_handler(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<ProxyRequest>,
) -> Result<Json<ProxyResponse>, StatusCode> {
    info!("Received completion request for model: {}", req.model);

    // Extract session ID from headers if present
    let session_id = headers
        .get("X-Session-ID")
        .and_then(|v| v.to_str().ok())
        .map(|s| s.to_string());

    // Convert to request shape for routing
    let request_shape = RequestShape {
        model_id: req.model.clone(),
        prompt_tokens_est: estimate_prompt_tokens(&req.prompt, &req.messages),
        max_tokens: req.max_tokens.unwrap_or(256),
        session_id,
        priority: 0,
    };

    // Route request (this will be integrated with Story 3's sticky routing)
    route_and_forward(&state, request_shape, req).await
}

/// Handle /v1/chat/completions
pub async fn chat_completions_handler(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<ProxyRequest>,
) -> Result<Json<ProxyResponse>, StatusCode> {
    info!("Received chat completion request for model: {}", req.model);

    let session_id = headers
        .get("X-Session-ID")
        .and_then(|v| v.to_str().ok())
        .map(|s| s.to_string());

    let request_shape = RequestShape {
        model_id: req.model.clone(),
        prompt_tokens_est: estimate_prompt_tokens(&req.prompt, &req.messages),
        max_tokens: req.max_tokens.unwrap_or(256),
        session_id,
        priority: 0,
    };

    route_and_forward(&state, request_shape, req).await
}

/// Estimate prompt tokens (simple heuristic)
fn estimate_prompt_tokens(
    prompt: &Option<String>,
    messages: &Option<Vec<ChatMessageRequest>>,
) -> u32 {
    if let Some(p) = prompt {
        // Rough estimate: 1 token ≈ 4 characters
        (p.len() / 4) as u32
    } else if let Some(msgs) = messages {
        msgs.iter().map(|m| (m.content.len() / 4) as u32).sum()
    } else {
        0
    }
}

/// Route request and forward to selected backend
async fn route_and_forward(
    state: &AppState,
    _request_shape: RequestShape,
    proxy_req: ProxyRequest,
) -> Result<Json<ProxyResponse>, StatusCode> {
    // TODO: Integrate with Stories 1-5 routing logic
    // For now, just pick first healthy pod
    
    let pods = state.pod_registry.get_all_pods().await;
    let healthy_pod = pods.iter().find(|p| p.snapshot.is_healthy).ok_or_else(|| {
        warn!("No healthy pods available");
        StatusCode::SERVICE_UNAVAILABLE
    })?;

    // Create backend for selected pod
    let backend = crate::backends::VllmBackend::new(
        &healthy_pod.config.id,
        &healthy_pod.config.address,
        30,
    );

    // Increment inflight (Story 2 integration)
    // TODO: Use admission controller from Story 2
    // state.admission_controller.admit_request(&healthy_pod.config.id).await;

    // Convert proxy request to backend request
    let backend_request = BackendRequest {
        model: proxy_req.model,
        prompt: proxy_req.prompt,
        messages: proxy_req.messages.map(|msgs| {
            msgs.into_iter()
                .map(|m| ChatMessage {
                    role: m.role,
                    content: m.content,
                })
                .collect()
        }),
        max_tokens: proxy_req.max_tokens.unwrap_or(256),
        temperature: proxy_req.temperature.unwrap_or(0.7),
        stream: proxy_req.stream.unwrap_or(false),
        timeout_ms: None,
    };

    // Forward to backend
    let start = std::time::Instant::now();
    let result = backend.generate(backend_request).await;
    let latency_ms = start.elapsed().as_millis();

    match result {
        Ok(response) => {
            info!(
                pod_id = %healthy_pod.config.id,
                latency_ms = latency_ms,
                "Request completed successfully"
            );

            // Decrement inflight
            // TODO: state.admission_controller.release_request(&healthy_pod.config.id).await;

            // Record metrics
            telemetry::record_routing_decision(true, false, None);

            Ok(Json(ProxyResponse {
                id: format!("cmpl-{}", uuid::Uuid::new_v4()),
                object: "text_completion".to_string(),
                created: chrono::Utc::now().timestamp(),
                model: response.model,
                choices: vec![ChoiceResponse {
                    text: response.text,
                    index: 0,
                    finish_reason: response.finish_reason,
                }],
                usage: response.usage.map(|u| UsageResponse {
                    prompt_tokens: u.prompt_tokens,
                    completion_tokens: u.completion_tokens,
                    total_tokens: u.total_tokens,
                }),
            }))
        }
        Err(e) => {
            error!(
                pod_id = %healthy_pod.config.id,
                error = %e,
                "Request failed"
            );

            // Decrement inflight on error
            // TODO: state.admission_controller.release_request(&healthy_pod.config.id).await;

            // Map backend error to HTTP status
            let status = match e {
                BackendError::Timeout => StatusCode::GATEWAY_TIMEOUT,
                BackendError::Unavailable(_) => StatusCode::SERVICE_UNAVAILABLE,
                BackendError::ApiError { status, .. } => StatusCode::from_u16(status).unwrap_or(StatusCode::BAD_GATEWAY),
                BackendError::NetworkError(_) => StatusCode::BAD_GATEWAY,
                BackendError::SerializationError(_) => StatusCode::INTERNAL_SERVER_ERROR,
            };

            Err(status)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::{Config, PodConfig};

    fn create_test_state() -> AppState {
        let configs = vec![PodConfig {
            id: "pod-1".to_string(),
            address: "http://localhost:8001".to_string(),
            name: None,
            model_id: "llama-8b".to_string(),
            gpu_memory_mb: 80000,
            num_layers: 32,
            hidden_size: 4096,
            weight: 1.0,
        }];

        let config = Config {
            pods: configs,
            health_check_interval_secs: 5,
            health_check_timeout_secs: 3,
            metrics_port: 9090,
            api_port: 8080,
        };

        AppState::new(config)
    }

    #[test]
    fn test_estimate_prompt_tokens() {
        let prompt = Some("Hello, how are you?".to_string());
        let est = estimate_prompt_tokens(&prompt, &None);
        assert!(est > 0);
    }

    #[test]
    fn test_estimate_message_tokens() {
        let messages = Some(vec![ChatMessageRequest {
            role: "user".to_string(),
            content: "Hello".to_string(),
        }]);
        let est = estimate_prompt_tokens(&None, &messages);
        assert!(est > 0);
    }
}
