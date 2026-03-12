/// Backend Adapter Trait
/// 
/// Defines the interface for LLM backend communication.
/// Currently supports vLLM-compatible OpenAI-style HTTP API.
/// 
/// Design principles:
/// - Thin abstraction - don't over-engineer
/// - One backend first (vLLM)
/// - Clear timeout and error handling
/// - Request lifecycle integration with inflight tracking

use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::time::Duration;

/// Backend request for text generation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BackendRequest {
    /// Model identifier
    pub model: String,
    /// Prompt or messages
    pub prompt: Option<String>,
    /// Messages for chat API
    pub messages: Option<Vec<ChatMessage>>,
    /// Maximum tokens to generate
    pub max_tokens: u32,
    /// Temperature
    pub temperature: f32,
    /// Stream response
    pub stream: bool,
    /// Request timeout (overrides default)
    pub timeout_ms: Option<u64>,
}

/// Chat message for chat/completions API
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChatMessage {
    pub role: String,
    pub content: String,
}

/// Backend response from text generation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BackendResponse {
    /// Generated text
    pub text: String,
    /// Usage statistics
    pub usage: Option<UsageInfo>,
    /// Model used
    pub model: String,
    /// Finish reason
    pub finish_reason: Option<String>,
}

/// Token usage information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UsageInfo {
    pub prompt_tokens: u32,
    pub completion_tokens: u32,
    pub total_tokens: u32,
}

/// Backend error types
#[derive(Debug, Clone)]
pub enum BackendError {
    /// Request timeout
    Timeout,
    /// Backend unavailable
    Unavailable(String),
    /// Backend returned error
    ApiError { status: u16, message: String },
    /// Network error
    NetworkError(String),
    /// Serialization error
    SerializationError(String),
}

impl std::fmt::Display for BackendError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BackendError::Timeout => write!(f, "Request timeout"),
            BackendError::Unavailable(msg) => write!(f, "Backend unavailable: {}", msg),
            BackendError::ApiError { status, message } => {
                write!(f, "Backend API error {}: {}", status, message)
            }
            BackendError::NetworkError(msg) => write!(f, "Network error: {}", msg),
            BackendError::SerializationError(msg) => {
                write!(f, "Serialization error: {}", msg)
            }
        }
    }
}

impl std::error::Error for BackendError {}

/// Backend trait for LLM inference
#[async_trait]
pub trait Backend: Send + Sync {
    /// Get backend identifier
    fn id(&self) -> &str;

    /// Get backend address
    fn address(&self) -> &str;

    /// Check if backend is healthy
    async fn health_check(&self) -> Result<bool, BackendError>;

    /// Generate text completion
    async fn generate(&self, request: BackendRequest) -> Result<BackendResponse, BackendError>;

    /// Get default timeout
    fn default_timeout(&self) -> Duration {
        Duration::from_secs(30)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_backend_request_serialization() {
        let request = BackendRequest {
            model: "llama-8b".to_string(),
            prompt: Some("Hello, world!".to_string()),
            messages: None,
            max_tokens: 100,
            temperature: 0.7,
            stream: false,
            timeout_ms: Some(5000),
        };

        let json = serde_json::to_string(&request).unwrap();
        assert!(json.contains("llama-8b"));
        assert!(json.contains("Hello, world!"));
    }

    #[test]
    fn test_backend_response_serialization() {
        let response = BackendResponse {
            text: "Generated text".to_string(),
            usage: Some(UsageInfo {
                prompt_tokens: 10,
                completion_tokens: 20,
                total_tokens: 30,
            }),
            model: "llama-8b".to_string(),
            finish_reason: Some("stop".to_string()),
        };

        let json = serde_json::to_string(&response).unwrap();
        assert!(json.contains("Generated text"));
        assert!(json.contains("llama-8b"));
    }

    #[test]
    fn test_backend_error_display() {
        let error = BackendError::Timeout;
        assert_eq!(error.to_string(), "Request timeout");

        let error = BackendError::Unavailable("pod-1".to_string());
        assert_eq!(error.to_string(), "Backend unavailable: pod-1");
    }
}
