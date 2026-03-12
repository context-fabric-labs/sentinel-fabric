use crate::scheduler::types::{PodScore, RequestShape};
use crate::scheduler::kv_estimator::{KvPressureEstimator, ModelConfig};
use crate::state::pod_registry::PodState;

/// Scoring weights (configurable in future)
#[derive(Debug, Clone)]
pub struct ScoringWeights {
    pub inflight_weight: f64,
    pub gpu_headroom_weight: f64,
    pub latency_weight: f64,
    pub error_rate_weight: f64,
}

impl Default for ScoringWeights {
    fn default() -> Self {
        Self {
            inflight_weight: 0.3,
            gpu_headroom_weight: 0.4,
            latency_weight: 0.2,
            error_rate_weight: 0.1,
        }
    }
}

/// Score calculator for pod candidates
pub struct PodScorer {
    weights: ScoringWeights,
    /// Max inflight threshold (pods at or above this get score 0 for inflight)
    max_inflight: u32,
    /// Optional KV pressure estimator
    kv_estimator: Option<KvPressureEstimator>,
}

impl PodScorer {
    pub fn new(weights: ScoringWeights, max_inflight: u32) -> Self {
        Self {
            weights,
            max_inflight,
            kv_estimator: None,
        }
    }

    /// Create scorer with KV pressure estimation
    pub fn with_kv_pressure(
        weights: ScoringWeights,
        max_inflight: u32,
        model_config: ModelConfig,
    ) -> Self {
        Self {
            weights,
            max_inflight,
            kv_estimator: Some(KvPressureEstimator::new(model_config)),
        }
    }

    /// Score a single pod based on its state and request shape
    pub fn score_pod(&self, pod: &PodState, request: &RequestShape) -> PodScore {
        let mut score = PodScore::new(pod.config.id.clone());

        // Filter: unhealthy pods are excluded
        if !pod.snapshot.is_healthy {
            score.is_healthy = false;
            score.exclusion_reason = Some("Pod is unhealthy".to_string());
            return score;
        }

        // Component 1: Inflight ratio score (0.0 to 1.0)
        // Lower inflight = higher score
        score.score_inflight = self.calculate_inflight_score(&pod.snapshot);

        // Component 2: GPU headroom score (0.0 to 1.0)
        // More free memory = higher score
        score.score_gpu_headroom = self.calculate_gpu_headroom_score(pod);

        // Component 3: Latency score (0.0 to 1.0)
        // Lower latency = higher score
        score.score_latency = self.calculate_latency_score(&pod.snapshot);

        // Component 4: Error rate score (0.0 to 1.0)
        // Lower error rate = higher score
        score.score_error_rate = self.calculate_error_rate_score(&pod.snapshot);

        // Component 5: KV pressure score (0.0 to 1.0) - if enabled
        if let Some(ref estimator) = self.kv_estimator {
            score.score_kv_pressure = self.calculate_kv_pressure_score(
                pod,
                request,
                estimator,
            );
        } else {
            score.score_kv_pressure = 1.0; // No penalty if not enabled
        }

        // Apply configured weight
        score.weight_multiplier = pod.config.weight;

        // Calculate total
        score.calculate_total();

        score
    }

    /// Calculate inflight component score
    fn calculate_inflight_score(&self, snapshot: &crate::state::pod_registry::PodHealthSnapshot) -> f64 {
        if self.max_inflight == 0 {
            return 0.0;
        }

        let ratio = snapshot.inflight_count as f64 / self.max_inflight as f64;
        // Clamp to [0, 1]
        let ratio = ratio.min(1.0);
        // Invert: lower inflight = higher score
        1.0 - ratio
    }

    /// Calculate GPU headroom component score
    fn calculate_gpu_headroom_score(&self, pod: &PodState) -> f64 {
        // Higher free memory = higher score
        // Use utilization ratio: lower utilization = higher score
        1.0 - pod.gpu_memory_utilization()
    }

    /// Calculate latency component score
    fn calculate_latency_score(&self, snapshot: &crate::state::pod_registry::PodHealthSnapshot) -> f64 {
        // Normalize latency to 0-1 score
        // Assume 0ms = perfect (1.0), 1000ms+ = terrible (0.0)
        let normalized = snapshot.latency_ewma_ms / 1000.0;
        (1.0 - normalized.min(1.0)).max(0.0)
    }

    /// Calculate error rate component score
    fn calculate_error_rate_score(&self, snapshot: &crate::state::pod_registry::PodHealthSnapshot) -> f64 {
        // Error rate is already 0.0 to 1.0
        // Invert: lower error rate = higher score
        1.0 - snapshot.error_rate.min(1.0)
    }

    /// Calculate KV pressure component score
    fn calculate_kv_pressure_score(
        &self,
        pod: &PodState,
        request: &RequestShape,
        estimator: &KvPressureEstimator,
    ) -> f64 {
        // Estimate KV pressure for this pod
        let avg_seq_len = request.prompt_tokens_est + request.max_tokens;
        let pressure = estimator.estimate_pod_pressure(
            &pod.config.id,
            pod.snapshot.inflight_count,
            avg_seq_len,
            pod.config.gpu_memory_mb,
        );

        // Use pressure level to determine score multiplier
        pressure.pressure_level().score_multiplier()
    }

    /// Score all pods and return sorted candidates (best first)
    pub fn score_all_candidates(
        &self,
        pods: &[PodState],
        request: &RequestShape,
    ) -> Vec<PodScore> {
        let mut scores: Vec<PodScore> = pods
            .iter()
            .map(|pod| self.score_pod(pod, request))
            .collect();

        // Sort by total score descending, but put unhealthy pods at the end
        scores.sort_by(|a, b| {
            // First, sort by health (healthy first)
            if a.is_healthy != b.is_healthy {
                return b.is_healthy.cmp(&a.is_healthy);
            }
            // Then by score descending
            b.total_score.partial_cmp(&a.total_score).unwrap_or(std::cmp::Ordering::Equal)
        });

        scores
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::PodConfig;

    fn create_test_pod(id: &str, gpu_used_mb: u64, inflight: u32) -> PodState {
        let config = PodConfig {
            id: id.to_string(),
            address: format!("http://localhost:800{}", id.chars().last().unwrap()),
            name: None,
            model_id: "llama-8b".to_string(),
            gpu_memory_mb: 80000,
            num_layers: 32,
            hidden_size: 4096,
            weight: 1.0,
        };

        let mut pod = PodState::new(config);
        pod.snapshot.gpu_memory_used_mb = gpu_used_mb;
        pod.snapshot.inflight_count = inflight;
        pod
    }

    #[test]
    fn test_inflight_score() {
        let scorer = PodScorer::new(ScoringWeights::default(), 100);

        // Pod with 0 inflight should get perfect score
        let pod = create_test_pod("1", 0, 0);
        let score = scorer.calculate_inflight_score(&pod.snapshot);
        assert!((score - 1.0).abs() < 0.001);

        // Pod with 50 inflight (50% of max) should get 0.5
        let pod = create_test_pod("2", 0, 50);
        let score = scorer.calculate_inflight_score(&pod.snapshot);
        assert!((score - 0.5).abs() < 0.001);

        // Pod at max inflight should get 0
        let pod = create_test_pod("3", 0, 100);
        let score = scorer.calculate_inflight_score(&pod.snapshot);
        assert!((score - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_gpu_headroom_score() {
        let scorer = PodScorer::new(ScoringWeights::default(), 100);

        // Pod with 0% utilization should get perfect score
        let pod = create_test_pod("1", 0, 0);
        let score = scorer.calculate_gpu_headroom_score(&pod);
        assert!((score - 1.0).abs() < 0.001);

        // Pod with 50% utilization should get 0.5
        let pod = create_test_pod("2", 40000, 0);
        let score = scorer.calculate_gpu_headroom_score(&pod);
        assert!((score - 0.5).abs() < 0.001);

        // Pod with 100% utilization should get 0
        let pod = create_test_pod("3", 80000, 0);
        let score = scorer.calculate_gpu_headroom_score(&pod);
        assert!((score - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_unhealthy_pod_exclusion() {
        let scorer = PodScorer::new(ScoringWeights::default(), 100);
        let request = RequestShape {
            model_id: "llama-8b".to_string(),
            prompt_tokens_est: 1000,
            max_tokens: 500,
            session_id: None,
            priority: 0,
        };

        let mut pod = create_test_pod("1", 0, 0);
        pod.snapshot.is_healthy = false;

        let score = scorer.score_pod(&pod, &request);
        assert!(!score.is_healthy);
        assert!(score.exclusion_reason.is_some());
        assert_eq!(score.total_score, 0.0);
    }

    #[test]
    fn test_candidate_ranking() {
        let scorer = PodScorer::new(ScoringWeights::default(), 100);
        let request = RequestShape {
            model_id: "llama-8b".to_string(),
            prompt_tokens_est: 1000,
            max_tokens: 500,
            session_id: None,
            priority: 0,
        };

        let pods = vec![
            create_test_pod("pod-1", 60000, 80), // High load
            create_test_pod("pod-2", 20000, 20), // Low load
            create_test_pod("pod-3", 40000, 50), // Medium load
        ];

        let scores = scorer.score_all_candidates(&pods, &request);

        // Pod 2 should be ranked first (lowest load)
        assert_eq!(scores[0].pod_id, "pod-2");
        assert_eq!(scores[1].pod_id, "pod-3");
        assert_eq!(scores[2].pod_id, "pod-1");

        // Scores should be descending
        assert!(scores[0].total_score >= scores[1].total_score);
        assert!(scores[1].total_score >= scores[2].total_score);
    }

    #[test]
    fn test_kv_pressure_scoring() {
        // Create model config for Llama 8B
        let model_config = ModelConfig::new("llama-8b", 32, 4096);
        
        // Create scorer with KV pressure enabled
        let scorer = PodScorer::with_kv_pressure(
            ScoringWeights::default(),
            100,
            model_config,
        );

        let request = RequestShape {
            model_id: "llama-8b".to_string(),
            prompt_tokens_est: 4096,
            max_tokens: 1024,
            session_id: None,
            priority: 0,
        };

        // Pod with low KV pressure (low inflight, lots of GPU memory)
        let low_pressure_pod = create_test_pod("pod-1", 10000, 5);
        
        // Pod with high KV pressure (high inflight, less GPU memory)
        let high_pressure_pod = create_test_pod("pod-2", 70000, 80);

        let low_score = scorer.score_pod(&low_pressure_pod, &request);
        let high_score = scorer.score_pod(&high_pressure_pod, &request);

        // Low pressure pod should have better KV score
        assert!(low_score.score_kv_pressure > high_score.score_kv_pressure);
    }

    #[test]
    fn test_kv_pressure_with_heavier_request() {
        let model_config = ModelConfig::new("llama-8b", 32, 4096);
        let scorer = PodScorer::with_kv_pressure(
            ScoringWeights::default(),
            100,
            model_config,
        );

        let light_request = RequestShape {
            model_id: "llama-8b".to_string(),
            prompt_tokens_est: 100,
            max_tokens: 100,
            session_id: None,
            priority: 0,
        };

        let heavy_request = RequestShape {
            model_id: "llama-8b".to_string(),
            prompt_tokens_est: 8000,
            max_tokens: 2000,
            session_id: None,
            priority: 0,
        };

        let pod = create_test_pod("pod-1", 40000, 50);

        let light_score = scorer.score_pod(&pod, &light_request);
        let heavy_score = scorer.score_pod(&pod, &heavy_request);

        // Heavy request should result in lower KV score due to higher pressure
        assert!(light_score.score_kv_pressure >= heavy_score.score_kv_pressure);
    }
}
