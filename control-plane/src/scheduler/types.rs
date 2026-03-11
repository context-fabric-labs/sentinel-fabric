use serde::{Deserialize, Serialize};

/// Shape of an incoming inference request
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RequestShape {
    /// Model identifier
    pub model_id: String,
    /// Estimated prompt tokens (from tokenizer or estimate)
    pub prompt_tokens_est: u32,
    /// Maximum tokens to generate
    pub max_tokens: u32,
    /// Optional session ID for sticky routing
    #[serde(default)]
    pub session_id: Option<String>,
    /// Optional priority (lower = higher priority)
    #[serde(default)]
    pub priority: u32,
}

impl RequestShape {
    /// Estimate total tokens for this request
    pub fn total_tokens_est(&self) -> u32 {
        self.prompt_tokens_est + self.max_tokens
    }
}

/// Score breakdown for a pod candidate
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PodScore {
    /// Pod ID
    pub pod_id: String,
    /// Total score (higher = better)
    pub total_score: f64,
    /// Score from inflight ratio component
    pub score_inflight: f64,
    /// Score from GPU memory headroom
    pub score_gpu_headroom: f64,
    /// Score from latency EWMA
    pub score_latency: f64,
    /// Score from error rate
    pub score_error_rate: f64,
    /// Configured weight multiplier
    pub weight_multiplier: f64,
    /// Is this pod healthy (filter criterion)
    pub is_healthy: bool,
    /// Reason if excluded from consideration
    pub exclusion_reason: Option<String>,
}

impl PodScore {
    pub fn new(pod_id: String) -> Self {
        Self {
            pod_id,
            total_score: 0.0,
            score_inflight: 0.0,
            score_gpu_headroom: 0.0,
            score_latency: 0.0,
            score_error_rate: 0.0,
            weight_multiplier: 1.0,
            is_healthy: true,
            exclusion_reason: None,
        }
    }

    /// Calculate total score from components
    pub fn calculate_total(&mut self) {
        self.total_score = (self.score_inflight
            + self.score_gpu_headroom
            + self.score_latency
            + self.score_error_rate)
            * self.weight_multiplier;
    }
}

/// Routing decision with full explanation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RoutingDecision {
    /// Chosen pod ID (if any)
    pub chosen_pod_id: Option<String>,
    /// Chosen pod address (if any)
    pub chosen_pod_address: Option<String>,
    /// All candidate scores (sorted by score descending)
    pub candidate_scores: Vec<PodScore>,
    /// Reason for the decision
    pub reason: String,
    /// Request shape that was routed
    pub request_shape: RequestShape,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_request_shape_total_tokens() {
        let shape = RequestShape {
            model_id: "llama-8b".to_string(),
            prompt_tokens_est: 1000,
            max_tokens: 500,
            session_id: Some("abc".to_string()),
            priority: 0,
        };
        assert_eq!(shape.total_tokens_est(), 1500);
    }

    #[test]
    fn test_pod_score_calculation() {
        let mut score = PodScore::new("pod-1".to_string());
        score.score_inflight = 0.8;
        score.score_gpu_headroom = 0.6;
        score.score_latency = 0.9;
        score.score_error_rate = 1.0;
        score.weight_multiplier = 1.5;
        score.calculate_total();

        let expected = (0.8 + 0.6 + 0.9 + 1.0) * 1.5;
        assert!((score.total_score - expected).abs() < 0.001);
    }
}
