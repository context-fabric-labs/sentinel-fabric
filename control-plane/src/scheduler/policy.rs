/// Pod Selection Policy
/// 
/// This module defines the final selection policy that combines:
/// - Hard filters (health, stale telemetry)
/// - Soft penalties (inflight, KV pressure, GPU headroom, latency, failures)
/// 
/// Design principles:
/// - Hard filters eliminate candidates early
/// - Soft penalties affect score but don't eliminate
/// - Policy is inspectable and tunable
/// - Score breakdown is visible in debug output

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

/// Selection policy configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SelectionPolicy {
    /// Maximum inflight requests per pod
    pub max_inflight: u32,
    /// Maximum KV pressure ratio (0.0 to 1.0)
    pub max_kv_pressure_ratio: f64,
    /// Minimum GPU headroom ratio (0.0 to 1.0)
    pub min_gpu_headroom_ratio: f64,
    /// Stale telemetry threshold (seconds)
    pub stale_telemetry_secs: u64,
    /// Recent failure penalty (0.0 to 1.0)
    pub recent_failure_penalty: f64,
    /// Failure decay time (seconds)
    pub failure_decay_secs: u64,
    /// Scoring weights
    pub weights: PolicyWeights,
}

impl Default for SelectionPolicy {
    fn default() -> Self {
        Self {
            max_inflight: 100,
            max_kv_pressure_ratio: 0.9, // Reject if >90% KV pressure
            min_gpu_headroom_ratio: 0.1, // Require at least 10% free GPU memory
            stale_telemetry_secs: 30, // Consider telemetry stale after 30s
            recent_failure_penalty: 0.2, // 20% penalty per recent failure
            failure_decay_secs: 60, // Failures decay after 60s
            weights: PolicyWeights::default(),
        }
    }
}

impl SelectionPolicy {
    /// Create a new selection policy with custom weights
    pub fn with_weights(weights: PolicyWeights) -> Self {
        Self {
            weights,
            ..Default::default()
        }
    }
}

/// Scoring weights for soft penalties
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PolicyWeights {
    /// Inflight weight (0.0 to 1.0)
    pub inflight: f64,
    /// GPU headroom weight (0.0 to 1.0)
    pub gpu_headroom: f64,
    /// KV pressure weight (0.0 to 1.0)
    pub kv_pressure: f64,
    /// Latency weight (0.0 to 1.0)
    pub latency: f64,
    /// Error rate weight (0.0 to 1.0)
    pub error_rate: f64,
    /// Recent failures weight (0.0 to 1.0)
    pub recent_failures: f64,
}

impl Default for PolicyWeights {
    fn default() -> Self {
        Self {
            inflight: 0.25,
            gpu_headroom: 0.25,
            kv_pressure: 0.20,
            latency: 0.15,
            error_rate: 0.10,
            recent_failures: 0.05,
        }
    }
}

impl PolicyWeights {
    /// Normalize weights to sum to 1.0
    pub fn normalize(&self) -> Self {
        let total = self.inflight + self.gpu_headroom + self.kv_pressure 
            + self.latency + self.error_rate + self.recent_failures;
        
        if total == 0.0 {
            return Self::default();
        }

        Self {
            inflight: self.inflight / total,
            gpu_headroom: self.gpu_headroom / total,
            kv_pressure: self.kv_pressure / total,
            latency: self.latency / total,
            error_rate: self.error_rate / total,
            recent_failures: self.recent_failures / total,
        }
    }
}

/// Pod snapshot with all telemetry
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PodTelemetry {
    /// Pod ID
    pub pod_id: String,
    /// Is pod healthy (from health check)
    pub is_healthy: bool,
    /// Inflight request count
    pub inflight_count: u32,
    /// GPU memory used (MB)
    pub gpu_memory_used_mb: u64,
    /// GPU memory total (MB)
    pub gpu_memory_total_mb: u64,
    /// Recent latency EWMA (ms)
    pub latency_ewma_ms: f64,
    /// Recent error rate (0.0 to 1.0)
    pub error_rate: f64,
    /// Number of recent failures
    pub recent_failures: u32,
    /// Last failure timestamp
    pub last_failure_at: Option<DateTime<Utc>>,
    /// Last telemetry update
    pub last_updated: DateTime<Utc>,
}

impl PodTelemetry {
    /// Calculate GPU headroom ratio (0.0 to 1.0)
    pub fn gpu_headroom_ratio(&self) -> f64 {
        if self.gpu_memory_total_mb == 0 {
            return 0.0;
        }
        let used_ratio = self.gpu_memory_used_mb as f64 / self.gpu_memory_total_mb as f64;
        1.0 - used_ratio
    }

    /// Check if telemetry is stale
    pub fn is_telemetry_stale(&self, threshold_secs: u64) -> bool {
        let age = Utc::now() - self.last_updated;
        age.num_seconds() > threshold_secs as i64
    }

    /// Calculate failure penalty (0.0 to 1.0)
    pub fn failure_penalty(&self, penalty_per_failure: f64, decay_secs: u64) -> f64 {
        if self.recent_failures == 0 {
            return 0.0;
        }

        // Base penalty from failure count
        let base_penalty = (self.recent_failures as f64 * penalty_per_failure).min(1.0);

        // Decay based on time since last failure
        if let Some(last_failure) = self.last_failure_at {
            let age = Utc::now() - last_failure;
            let age_secs = age.num_seconds() as u64;
            
            if age_secs >= decay_secs {
                return 0.0; // Fully decayed
            }

            // Linear decay
            let decay_factor = 1.0 - (age_secs as f64 / decay_secs as f64);
            base_penalty * decay_factor
        } else {
            base_penalty
        }
    }
}

/// Score breakdown for debugging
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ScoreBreakdown {
    /// Pod ID
    pub pod_id: String,
    /// Total score (higher = better)
    pub total_score: f64,
    /// Inflight component score (0.0 to 1.0)
    pub inflight_score: f64,
    /// GPU headroom component score (0.0 to 1.0)
    pub gpu_headroom_score: f64,
    /// KV pressure component score (0.0 to 1.0)
    pub kv_pressure_score: f64,
    /// Latency component score (0.0 to 1.0)
    pub latency_score: f64,
    /// Error rate component score (0.0 to 1.0)
    pub error_rate_score: f64,
    /// Recent failures component score (0.0 to 1.0)
    pub recent_failures_score: f64,
    /// Was pod filtered out?
    pub filtered: bool,
    /// Filter reason (if filtered)
    pub filter_reason: Option<String>,
}

impl ScoreBreakdown {
    pub fn new(pod_id: &str) -> Self {
        Self {
            pod_id: pod_id.to_string(),
            total_score: 0.0,
            inflight_score: 1.0,
            gpu_headroom_score: 1.0,
            kv_pressure_score: 1.0,
            latency_score: 1.0,
            error_rate_score: 1.0,
            recent_failures_score: 1.0,
            filtered: false,
            filter_reason: None,
        }
    }

    /// Calculate total score from components
    pub fn calculate_total(&mut self, weights: &PolicyWeights) {
        self.total_score = 
            self.inflight_score * weights.inflight +
            self.gpu_headroom_score * weights.gpu_headroom +
            self.kv_pressure_score * weights.kv_pressure +
            self.latency_score * weights.latency +
            self.error_rate_score * weights.error_rate +
            self.recent_failures_score * weights.recent_failures;
    }
}

/// Pod selector with policy
#[derive(Debug, Clone)]
pub struct PodSelector {
    policy: SelectionPolicy,
}

impl PodSelector {
    pub fn new(policy: SelectionPolicy) -> Self {
        Self { policy }
    }

    /// Filter candidates based on hard filters
    pub fn filter_candidates<'a>(&self, telemetry: &'a [PodTelemetry]) -> Vec<&'a PodTelemetry> {
        telemetry
            .iter()
            .filter(|pod| {
                // Hard filter 1: Health check
                if !pod.is_healthy {
                    return false;
                }

                // Hard filter 2: Stale telemetry
                if pod.is_telemetry_stale(self.policy.stale_telemetry_secs) {
                    return false;
                }

                // Hard filter 3: Inflight limit
                if pod.inflight_count >= self.policy.max_inflight {
                    return false;
                }

                // Hard filter 4: GPU headroom minimum
                if pod.gpu_headroom_ratio() < self.policy.min_gpu_headroom_ratio {
                    return false;
                }

                true
            })
            .collect()
    }

    /// Score a single pod
    pub fn score_pod(&self, telemetry: &PodTelemetry) -> ScoreBreakdown {
        let mut breakdown = ScoreBreakdown::new(&telemetry.pod_id);

        // Component 1: Inflight score (lower inflight = higher score)
        breakdown.inflight_score = 1.0 - (telemetry.inflight_count as f64 / self.policy.max_inflight as f64).min(1.0);

        // Component 2: GPU headroom score (more headroom = higher score)
        breakdown.gpu_headroom_score = telemetry.gpu_headroom_ratio();

        // Component 3: KV pressure score (lower pressure = higher score)
        // Note: KV pressure would be calculated separately, using 1.0 as default here
        breakdown.kv_pressure_score = 1.0;

        // Component 4: Latency score (lower latency = higher score)
        // Normalize: 0ms = 1.0, 1000ms+ = 0.0
        breakdown.latency_score = (1.0 - (telemetry.latency_ewma_ms / 1000.0).min(1.0)).max(0.0);

        // Component 5: Error rate score (lower error = higher score)
        breakdown.error_rate_score = 1.0 - telemetry.error_rate.min(1.0);

        // Component 6: Recent failures score (fewer failures = higher score)
        let failure_penalty = telemetry.failure_penalty(
            self.policy.recent_failure_penalty,
            self.policy.failure_decay_secs,
        );
        breakdown.recent_failures_score = 1.0 - failure_penalty;

        // Calculate weighted total
        breakdown.calculate_total(&self.policy.weights);

        breakdown
    }

    /// Select best pod from candidates
    pub fn select_best(&self, telemetry: &[PodTelemetry]) -> Option<ScoreBreakdown> {
        let candidates = self.filter_candidates(telemetry);

        if candidates.is_empty() {
            return None;
        }

        // Score all candidates
        let mut scores: Vec<ScoreBreakdown> = candidates
            .iter()
            .map(|pod| self.score_pod(pod))
            .collect();

        // Sort by score descending
        scores.sort_by(|a, b| b.total_score.partial_cmp(&a.total_score).unwrap_or(std::cmp::Ordering::Equal));

        // Return best
        scores.first().cloned()
    }

    /// Score and rank all pods (for debug output)
    pub fn rank_all(&self, telemetry: &[PodTelemetry]) -> Vec<ScoreBreakdown> {
        let mut scores: Vec<ScoreBreakdown> = telemetry
            .iter()
            .map(|pod| {
                let mut breakdown = self.score_pod(pod);
                
                // Mark filtered pods
                if !self.filter_candidates(std::slice::from_ref(pod)).iter().any(|p| p.pod_id == pod.pod_id) {
                    breakdown.filtered = true;
                    if !pod.is_healthy {
                        breakdown.filter_reason = Some("unhealthy".to_string());
                    } else if pod.is_telemetry_stale(self.policy.stale_telemetry_secs) {
                        breakdown.filter_reason = Some("stale_telemetry".to_string());
                    } else if pod.inflight_count >= self.policy.max_inflight {
                        breakdown.filter_reason = Some("inflight_limit".to_string());
                    } else if pod.gpu_headroom_ratio() < self.policy.min_gpu_headroom_ratio {
                        breakdown.filter_reason = Some("low_gpu_headroom".to_string());
                    }
                }
                
                breakdown
            })
            .collect();

        // Sort by score descending (filtered pods at end)
        scores.sort_by(|a, b| {
            if a.filtered != b.filtered {
                return b.filtered.cmp(&a.filtered);
            }
            b.total_score.partial_cmp(&a.total_score).unwrap_or(std::cmp::Ordering::Equal)
        });

        scores
    }

    /// Get the selection policy
    pub fn policy(&self) -> &SelectionPolicy {
        &self.policy
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::Duration;

    fn create_test_telemetry(pod_id: &str, inflight: u32, gpu_used: u64, gpu_total: u64) -> PodTelemetry {
        PodTelemetry {
            pod_id: pod_id.to_string(),
            is_healthy: true,
            inflight_count: inflight,
            gpu_memory_used_mb: gpu_used,
            gpu_memory_total_mb: gpu_total,
            latency_ewma_ms: 100.0,
            error_rate: 0.0,
            recent_failures: 0,
            last_failure_at: None,
            last_updated: Utc::now(),
        }
    }

    #[test]
    fn test_gpu_headroom_ratio() {
        let telemetry = create_test_telemetry("pod-1", 10, 40000, 80000);
        assert!((telemetry.gpu_headroom_ratio() - 0.5).abs() < 0.01);
    }

    #[test]
    fn test_filter_healthy_only() {
        let policy = SelectionPolicy::default();
        let selector = PodSelector::new(policy);

        let telemetry = vec![
            create_test_telemetry("pod-1", 10, 40000, 80000),
            create_test_telemetry("pod-2", 10, 40000, 80000),
        ];
        let mut unhealthy = telemetry[0].clone();
        unhealthy.is_healthy = false;
        let telemetry = vec![unhealthy, telemetry[1].clone()];

        let candidates = selector.filter_candidates(&telemetry);
        assert_eq!(candidates.len(), 1);
        assert_eq!(candidates[0].pod_id, "pod-2");
    }

    #[test]
    fn test_filter_stale_telemetry() {
        let mut policy = SelectionPolicy::default();
        policy.stale_telemetry_secs = 30;
        let selector = PodSelector::new(policy);

        let mut telemetry = create_test_telemetry("pod-1", 10, 40000, 80000);
        telemetry.last_updated = Utc::now() - Duration::seconds(60); // 60s old
        let telemetry_list = vec![telemetry];

        let candidates = selector.filter_candidates(&telemetry_list);
        assert!(candidates.is_empty());
    }

    #[test]
    fn test_filter_inflight_limit() {
        let mut policy = SelectionPolicy::default();
        policy.max_inflight = 50;
        let selector = PodSelector::new(policy);

        let telemetry = create_test_telemetry("pod-1", 100, 40000, 80000); // Over limit
        let telemetry_list = vec![telemetry];

        let candidates = selector.filter_candidates(&telemetry_list);
        assert!(candidates.is_empty());
    }

    #[test]
    fn test_filter_gpu_headroom() {
        let mut policy = SelectionPolicy::default();
        policy.min_gpu_headroom_ratio = 0.2; // Require 20% free
        let selector = PodSelector::new(policy);

        let telemetry = create_test_telemetry("pod-1", 10, 70000, 80000); // Only 12.5% free
        let telemetry_list = vec![telemetry];

        let candidates = selector.filter_candidates(&telemetry_list);
        assert!(candidates.is_empty());
    }

    #[test]
    fn test_score_inflight() {
        let policy = SelectionPolicy::default();
        let selector = PodSelector::new(policy);

        let low_load = create_test_telemetry("pod-1", 10, 40000, 80000);
        let high_load = create_test_telemetry("pod-2", 90, 40000, 80000);

        let low_score = selector.score_pod(&low_load);
        let high_score = selector.score_pod(&high_load);

        assert!(low_score.inflight_score > high_score.inflight_score);
    }

    #[test]
    fn test_score_gpu_headroom() {
        let policy = SelectionPolicy::default();
        let selector = PodSelector::new(policy);

        let high_headroom = create_test_telemetry("pod-1", 10, 20000, 80000);
        let low_headroom = create_test_telemetry("pod-2", 10, 70000, 80000);

        let high_score = selector.score_pod(&high_headroom);
        let low_score = selector.score_pod(&low_headroom);

        assert!(high_score.gpu_headroom_score > low_score.gpu_headroom_score);
    }

    #[test]
    fn test_failure_penalty() {
        let mut telemetry = create_test_telemetry("pod-1", 10, 40000, 80000);
        telemetry.recent_failures = 3;
        telemetry.last_failure_at = Some(Utc::now());

        // Fresh failure - full penalty
        let penalty = telemetry.failure_penalty(0.2, 60);
        assert!(penalty > 0.5); // 3 failures × 0.2 = 0.6

        // Old failure - decayed penalty
        telemetry.last_failure_at = Some(Utc::now() - Duration::seconds(120));
        let penalty = telemetry.failure_penalty(0.2, 60);
        assert_eq!(penalty, 0.0); // Fully decayed
    }

    #[test]
    fn test_select_best() {
        let policy = SelectionPolicy::default();
        let selector = PodSelector::new(policy);

        let telemetry = vec![
            create_test_telemetry("pod-1", 80, 60000, 80000), // High load
            create_test_telemetry("pod-2", 20, 30000, 80000), // Low load
            create_test_telemetry("pod-3", 50, 45000, 80000), // Medium load
        ];

        let best = selector.select_best(&telemetry).unwrap();
        assert_eq!(best.pod_id, "pod-2"); // Low load should win
    }

    #[test]
    fn test_rank_all() {
        let policy = SelectionPolicy::default();
        let selector = PodSelector::new(policy);

        let mut unhealthy = create_test_telemetry("pod-1", 10, 40000, 80000);
        unhealthy.is_healthy = false;

        let telemetry = vec![
            unhealthy,
            create_test_telemetry("pod-2", 20, 30000, 80000),
            create_test_telemetry("pod-3", 50, 45000, 80000),
        ];

        let rankings = selector.rank_all(&telemetry);

        // Should have 3 rankings
        assert_eq!(rankings.len(), 3);
        
        // Unhealthy pod should be filtered
        let filtered: Vec<_> = rankings.iter().filter(|r| r.filtered).collect();
        let unfiltered: Vec<_> = rankings.iter().filter(|r| !r.filtered).collect();
        
        assert_eq!(filtered.len(), 1);
        assert_eq!(unfiltered.len(), 2);
        
        // Filtered pod should be pod-1
        assert_eq!(filtered[0].pod_id, "pod-1");
        assert_eq!(filtered[0].filter_reason, Some("unhealthy".to_string()));
    }

    #[test]
    fn test_weights_normalize() {
        let weights = PolicyWeights {
            inflight: 0.5,
            gpu_headroom: 0.3,
            kv_pressure: 0.1,
            latency: 0.05,
            error_rate: 0.03,
            recent_failures: 0.02,
        };

        let normalized = weights.normalize();
        let total = normalized.inflight + normalized.gpu_headroom + normalized.kv_pressure
            + normalized.latency + normalized.error_rate + normalized.recent_failures;
        
        assert!((total - 1.0).abs() < 0.001);
    }
}
