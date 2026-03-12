/// vLLM Backend Implementation
/// 
/// Implements the Backend trait for vLLM's OpenAI-compatible API.
/// 
/// Supported endpoints:
/// - POST /v1/completions - text completion
/// - POST /v1/chat/completions - chat completion
/// - GET /health - health check
/// 
/// vLLM API reference: https://docs.vllm.ai/en/latest/serving/openai_compatible_server.html

use super::backend::{
    Backend, BackendError, BackendRequest, BackendResponse, ChatMessage, UsageInfo,
};
use async_trait::async_trait;
use reqwest::{Client, StatusCode};
use serde::{Deserialize, Serialize};
use std::time::Duration;

/// vLLM completion request
#[derive(Debug, Clone, Serialize)]
struct VllmCompletionRequest {
    model: String,
    prompt: String,
    max_tokens: u32,
    temperature: f32,
    stream: bool,
}

/// vLLM chat completion request
#[derive(Debug, Clone, Serialize)]
struct VllmChatRequest {
    model: String,
    messages: Vec<VllmMessage>,
    max_tokens: u32,
    temperature: f32,
    stream: bool,
}

#[derive(Debug, Clone, Serialize)]
struct VllmMessage {
    role: String,
    content: String,
}

/// vLLM completion response
#[derive(Debug, Clone, Deserialize)]
struct VllmCompletionResponse {
    id: String,
    object: String,
    created: i64,
    model: String,
    choices: Vec<VllmChoice>,
    usage: Option<VllmUsage>,
}

#[derive(Debug, Clone, Deserialize)]
struct VllmChoice {
    text: String,
    index: u32,
    finish_reason: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
struct VllmUsage {
    prompt_tokens: u32,
    completion_tokens: u32,
    total_tokens: u32,
}

/// vLLM backend implementation
pub struct VllmBackend {
    id: String,
    address: String,
    client: Client,
    timeout: Duration,
}

impl VllmBackend {
    /// Create a new vLLM backend
    pub fn new(id: &str, address: &str, timeout_secs: u64) -> Self {
        let client = Client::builder()
            .timeout(Duration::from_secs(timeout_secs))
            .build()
            .unwrap_or_default();

        Self {
            id: id.to_string(),
            address: address.trim_end_matches('/').to_string(),
            client,
            timeout: Duration::from_secs(timeout_secs),
        }
    }

    /// Create with custom client
    pub fn with_client(id: &str, address: &str, client: Client) -> Self {
        Self {
            id: id.to_string(),
            address: address.trim_end_matches('/').to_string(),
            client,
            timeout: Duration::from_secs(30),
        }
    }
}

#[async_trait]
impl Backend for VllmBackend {
    fn id(&self) -> &str {
        &self.id
    }

    fn address(&self) -> &str {
        &self.address
    }

    fn default_timeout(&self) -> Duration {
        self.timeout
    }

    async fn health_check(&self) -> Result<bool, BackendError> {
        let url = format!("{}/health", self.address);
        
        match self.client.get(&url).send().await {
            Ok(response) => Ok(response.status().is_success()),
            Err(e) => Err(BackendError::NetworkError(e.to_string())),
        }
    }

    async fn generate(&self, request: BackendRequest) -> Result<BackendResponse, BackendError> {
        // Determine timeout
        let timeout = request
            .timeout_ms
            .map(Duration::from_millis)
            .unwrap_or(self.timeout);

        // Choose endpoint based on request type
        let (endpoint, body) = if let Some(prompt) = &request.prompt {
            // Text completion
            let vllm_req = VllmCompletionRequest {
                model: request.model.clone(),
                prompt: prompt.clone(),
                max_tokens: request.max_tokens,
                temperature: request.temperature,
                stream: request.stream,
            };
            ("/v1/completions", serde_json::to_value(&vllm_req)?)
        } else if let Some(messages) = &request.messages {
            // Chat completion
            let vllm_messages = messages
                .iter()
                .map(|m| VllmMessage {
                    role: m.role.clone(),
                    content: m.content.clone(),
                })
                .collect();

            let vllm_req = VllmChatRequest {
                model: request.model.clone(),
                messages: vllm_messages,
                max_tokens: request.max_tokens,
                temperature: request.temperature,
                stream: request.stream,
            };
            ("/v1/chat/completions", serde_json::to_value(&vllm_req)?)
        } else {
            return Err(BackendError::SerializationError(
                "Request must have either prompt or messages".to_string(),
            ));
        };

        let url = format!("{}{}", self.address, endpoint);

        // Send request
        let response = match self
            .client
            .post(&url)
            .timeout(timeout)
            .json(&body)
            .send()
            .await
        {
            Ok(resp) => resp,
            Err(e) => {
                if e.is_timeout() {
                    return Err(BackendError::Timeout);
                }
                return Err(BackendError::NetworkError(e.to_string()));
            }
        };

        // Check status
        let status = response.status();
        if !status.is_success() {
            let error_text = response.text().await.unwrap_or_default();
            return Err(BackendError::ApiError {
                status: status.as_u16(),
                message: error_text,
            });
        }

        // Parse response
        let vllm_response: VllmCompletionResponse = response.json().await.map_err(|e| {
            BackendError::SerializationError(format!("Failed to parse response: {}", e))
        })?;

        // Extract first choice
        let choice = vllm_response.choices.first().ok_or_else(|| {
            BackendError::SerializationError("No choices in response".to_string())
        })?;

        Ok(BackendResponse {
            text: choice.text.clone(),
            usage: vllm_response.usage.map(|u| UsageInfo {
                prompt_tokens: u.prompt_tokens,
                completion_tokens: u.completion_tokens,
                total_tokens: u.total_tokens,
            }),
            model: vllm_response.model,
            finish_reason: choice.finish_reason.clone(),
        })
    }
}

// Helper trait implementation for serde_json errors
impl From<serde_json::Error> for BackendError {
    fn from(err: serde_json::Error) -> Self {
        BackendError::SerializationError(err.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_vllm_backend_creation() {
        let backend = VllmBackend::new("pod-1", "http://localhost:8000", 30);
        assert_eq!(backend.id(), "pod-1");
        assert_eq!(backend.address(), "http://localhost:8000");
        assert_eq!(backend.default_timeout(), Duration::from_secs(30));
    }

    #[test]
    fn test_vllm_backend_address_normalization() {
        let backend = VllmBackend::new("pod-1", "http://localhost:8000/", 30);
        assert_eq!(backend.address(), "http://localhost:8000");
    }

    #[tokio::test]
    async fn test_vllm_health_check_failure() {
        let backend = VllmBackend::new("pod-1", "http://invalid-address-12345:8000", 5);
        let result = backend.health_check().await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_vllm_generate_timeout() {
        let backend = VllmBackend::new("pod-1", "http://invalid-address-12345:8000", 1);
        
        let request = BackendRequest {
            model: "llama-8b".to_string(),
            prompt: Some("test".to_string()),
            messages: None,
            max_tokens: 100,
            temperature: 0.7,
            stream: false,
            timeout_ms: Some(100), // 100ms timeout
        };

        let result = backend.generate(request).await;
        assert!(result.is_err());
    }

    #[test]
    fn test_vllm_request_serialization() {
        let req = VllmCompletionRequest {
            model: "llama-8b".to_string(),
            prompt: "Hello".to_string(),
            max_tokens: 100,
            temperature: 0.7,
            stream: false,
        };

        let json = serde_json::to_string(&req).unwrap();
        assert!(json.contains("llama-8b"));
        assert!(json.contains("Hello"));
        assert!(json.contains("100"));
    }
}
