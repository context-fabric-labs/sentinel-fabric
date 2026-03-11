use serde::{Deserialize, Serialize};
use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};
use tracing::debug;

/// Admission decision for a request
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AdmissionDecision {
    /// Was the request admitted?
    pub admitted: bool,
    /// Reason for the decision
    pub reason: AdmissionReason,
    /// Original max_tokens from request
    pub original_max_tokens: u32,
    /// Potentially degraded max_tokens
    pub degraded_max_tokens: u32,
    /// Was degradation applied?
    pub was_degraded: bool,
}

/// Reason for admission/rejection decision
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum AdmissionReason {
    /// Request admitted successfully
    Admitted,
    /// Global inflight limit exceeded
    GlobalOverload {
        current: u32,
        limit: u32,
    },
    /// No pod has capacity
    NoPodCapacity {
        pods_checked: usize,
    },
    /// Specific pod at capacity
    PodOverload {
        pod_id: String,
        current: u32,
        limit: u32,
    },
    /// Request admitted but degraded
    AdmittedDegraded {
        reason: String,
    },
}

/// Degradation policy configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DegradePolicy {
    /// Enable degradation (clamp max_tokens under load)
    pub enabled: bool,
    /// Degradation threshold (0.0 to 1.0) - degrade when load > threshold
    pub threshold: f64,
    /// Minimum max_tokens when degraded
    pub min_max_tokens: u32,
    /// Degradation factor (multiply max_tokens by this when degraded)
    pub degradation_factor: f64,
}

impl Default for DegradePolicy {
    fn default() -> Self {
        Self {
            enabled: true,
            threshold: 0.8,
            min_max_tokens: 64,
            degradation_factor: 0.5,
        }
    }
}

impl DegradePolicy {
    /// Calculate degraded max_tokens
    pub fn apply_degradation(&self, original_max_tokens: u32, load_ratio: f64) -> u32 {
        if !self.enabled {
            return original_max_tokens;
        }

        // More aggressive degradation as load increases
        let severity = ((load_ratio - self.threshold) / (1.0 - self.threshold)).min(1.0);
        let factor = self.degradation_factor * (1.0 - severity * 0.5); // 0.5x to 0.75x
        
        let degraded = (original_max_tokens as f64 * factor) as u32;
        degraded.max(self.min_max_tokens)
    }
}

/// Admission policy configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AdmissionPolicy {
    /// Global inflight limit across all pods
    pub global_max_inflight: u32,
    /// Per-pod inflight limit
    pub per_pod_max_inflight: u32,
    /// Degradation policy
    #[serde(default)]
    pub degrade_policy: DegradePolicy,
}

impl Default for AdmissionPolicy {
    fn default() -> Self {
        Self {
            global_max_inflight: 1000,
            per_pod_max_inflight: 100,
            degrade_policy: DegradePolicy::default(),
        }
    }
}

/// Thread-safe inflight counter for a single pod
#[derive(Debug, Clone)]
pub struct PodInflightCounter {
    count: Arc<AtomicU32>,
    limit: u32,
}

impl PodInflightCounter {
    pub fn new(limit: u32) -> Self {
        Self {
            count: Arc::new(AtomicU32::new(0)),
            limit,
        }
    }

    pub fn current(&self) -> u32 {
        self.count.load(Ordering::Relaxed)
    }

    pub fn has_capacity(&self) -> bool {
        self.current() < self.limit
    }

    pub fn increment(&self) -> u32 {
        self.count.fetch_add(1, Ordering::SeqCst)
    }

    pub fn decrement(&self) {
        self.count.fetch_sub(1, Ordering::SeqCst);
    }

    pub fn load_ratio(&self) -> f64 {
        if self.limit == 0 {
            return 1.0;
        }
        self.current() as f64 / self.limit as f64
    }
}

/// Global inflight tracker
#[derive(Debug)]
pub struct InflightCounters {
    /// Global counter
    global: AtomicU32,
    global_limit: u32,
    /// Per-pod counters
    pod_counters: Arc<tokio::sync::RwLock<std::collections::HashMap<String, PodInflightCounter>>>,
}

impl InflightCounters {
    pub fn new(global_limit: u32, per_pod_limit: u32, pod_ids: &[String]) -> Arc<Self> {
        let pod_counters: std::collections::HashMap<String, PodInflightCounter> = pod_ids
            .iter()
            .map(|id| (id.clone(), PodInflightCounter::new(per_pod_limit)))
            .collect();

        Arc::new(Self {
            global: AtomicU32::new(0),
            global_limit,
            pod_counters: Arc::new(tokio::sync::RwLock::new(pod_counters)),
        })
    }

    pub fn global_current(&self) -> u32 {
        self.global.load(Ordering::Relaxed)
    }

    pub fn global_has_capacity(&self) -> bool {
        self.global_current() < self.global_limit
    }

    pub fn global_load_ratio(&self) -> f64 {
        if self.global_limit == 0 {
            return 1.0;
        }
        self.global_current() as f64 / self.global_limit as f64
    }

    pub async fn get_pod_counter(&self, pod_id: &str) -> Option<PodInflightCounter> {
        let lock = self.pod_counters.read().await;
        lock.get(pod_id).cloned()
    }

    pub async fn get_pod_current(&self, pod_id: &str) -> Option<u32> {
        let lock = self.pod_counters.read().await;
        lock.get(pod_id).map(|c| c.current())
    }

    pub async fn get_all_pod_counts(&self) -> std::collections::HashMap<String, u32> {
        let lock = self.pod_counters.read().await;
        lock.iter().map(|(k, v)| (k.clone(), v.current())).collect()
    }

    /// Try to admit a request globally
    pub fn try_admit_global(&self) -> bool {
        // Use compare-and-swap to atomically check and increment
        let mut current = self.global.load(Ordering::Relaxed);
        loop {
            if current >= self.global_limit {
                return false;
            }
            match self.global.compare_exchange_weak(
                current,
                current + 1,
                Ordering::SeqCst,
                Ordering::Relaxed,
            ) {
                Ok(_) => {
                    debug!("Global inflight: {} -> {}", current, current + 1);
                    return true;
                }
                Err(new_current) => current = new_current,
            }
        }
    }

    /// Try to admit a request for a specific pod
    pub async fn try_admit_pod(&self, pod_id: &str) -> bool {
        let lock = self.pod_counters.read().await;
        if let Some(counter) = lock.get(pod_id) {
            let mut current = counter.count.load(Ordering::Relaxed);
            loop {
                if current >= counter.limit {
                    return false;
                }
                match counter.limit {
                    0 => return false,
                    _ => {
                        match counter.count.compare_exchange_weak(
                            current,
                            current + 1,
                            Ordering::SeqCst,
                            Ordering::Relaxed,
                        ) {
                            Ok(_) => {
                                debug!("Pod {} inflight: {} -> {}", pod_id, current, current + 1);
                                return true;
                            }
                            Err(new_current) => current = new_current,
                        }
                    }
                }
            }
        }
        false
    }

    /// Release a global slot
    pub fn release_global(&self) {
        let current = self.global.fetch_sub(1, Ordering::SeqCst);
        debug!("Global inflight: {} -> {}", current, current - 1);
    }

    /// Release a pod slot
    pub async fn release_pod(&self, pod_id: &str) {
        let lock = self.pod_counters.read().await;
        if let Some(counter) = lock.get(pod_id) {
            counter.decrement();
            debug!("Pod {} inflight decremented", pod_id);
        }
    }

    /// Add a new pod counter (for dynamic pod discovery)
    pub async fn add_pod(&self, pod_id: &str, limit: u32) {
        let mut lock = self.pod_counters.write().await;
        lock.insert(pod_id.to_string(), PodInflightCounter::new(limit));
    }
}

/// Admission controller
#[derive(Clone)]
pub struct AdmissionController {
    policy: AdmissionPolicy,
    counters: Arc<InflightCounters>,
}

impl AdmissionController {
    pub fn new(policy: AdmissionPolicy, pod_ids: &[String]) -> Self {
        let counters = InflightCounters::new(
            policy.global_max_inflight,
            policy.per_pod_max_inflight,
            pod_ids,
        );
        Self { policy, counters }
    }

    pub fn policy(&self) -> &AdmissionPolicy {
        &self.policy
    }

    pub fn counters(&self) -> &InflightCounters {
        &self.counters
    }

    /// Check if a request should be admitted
    pub async fn check_admission(&self, request_max_tokens: u32) -> AdmissionDecision {
        // Check global limit first
        if !self.counters.global_has_capacity() {
            return AdmissionDecision {
                admitted: false,
                reason: AdmissionReason::GlobalOverload {
                    current: self.counters.global_current(),
                    limit: self.policy.global_max_inflight,
                },
                original_max_tokens: request_max_tokens,
                degraded_max_tokens: request_max_tokens,
                was_degraded: false,
            };
        }

        // Check if any pod has capacity
        let pod_counts = self.counters.get_all_pod_counts().await;
        let has_capacity = pod_counts.values().any(|&count| {
            let limit = self.policy.per_pod_max_inflight;
            count < limit
        });

        if !has_capacity {
            return AdmissionDecision {
                admitted: false,
                reason: AdmissionReason::NoPodCapacity {
                    pods_checked: pod_counts.len(),
                },
                original_max_tokens: request_max_tokens,
                degraded_max_tokens: request_max_tokens,
                was_degraded: false,
            };
        }

        // Check if degradation should be applied
        let global_load = self.counters.global_load_ratio();
        let (degraded_max_tokens, was_degraded) = if self.policy.degrade_policy.enabled 
            && global_load > self.policy.degrade_policy.threshold 
        {
            let degraded = self.policy.degrade_policy.apply_degradation(request_max_tokens, global_load);
            (degraded, true)
        } else {
            (request_max_tokens, false)
        };

        AdmissionDecision {
            admitted: true,
            reason: if was_degraded {
                AdmissionReason::AdmittedDegraded {
                    reason: format!("Global load {:.1}% exceeds threshold {:.1}%", 
                        global_load * 100.0, 
                        self.policy.degrade_policy.threshold * 100.0),
                }
            } else {
                AdmissionReason::Admitted
            },
            original_max_tokens: request_max_tokens,
            degraded_max_tokens: degraded_max_tokens,
            was_degraded,
        }
    }

    /// Admit a request for a specific pod (call after check_admission)
    pub async fn admit_request(&self, pod_id: &str) -> bool {
        if self.counters.try_admit_pod(pod_id).await {
            true
        } else {
            // Rollback global admission
            self.counters.release_global();
            false
        }
    }

    /// Release resources after request completes
    pub async fn release_request(&self, pod_id: &str) {
        self.counters.release_global();
        self.counters.release_pod(pod_id).await;
    }

    /// Get current metrics
    pub async fn get_metrics(&self) -> AdmissionMetrics {
        AdmissionMetrics {
            global_inflight: self.counters.global_current(),
            global_limit: self.policy.global_max_inflight,
            global_load_ratio: self.counters.global_load_ratio(),
            pod_counts: self.counters.get_all_pod_counts().await,
            per_pod_limit: self.policy.per_pod_max_inflight,
        }
    }
}

/// Current admission metrics
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AdmissionMetrics {
    pub global_inflight: u32,
    pub global_limit: u32,
    pub global_load_ratio: f64,
    pub pod_counts: std::collections::HashMap<String, u32>,
    pub per_pod_limit: u32,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn create_test_controller(global: u32, per_pod: u32, pods: usize) -> AdmissionController {
        let pod_ids: Vec<String> = (0..pods).map(|i| format!("pod-{}", i)).collect();
        let policy = AdmissionPolicy {
            global_max_inflight: global,
            per_pod_max_inflight: per_pod,
            degrade_policy: DegradePolicy::default(),
        };
        AdmissionController::new(policy, &pod_ids)
    }

    #[tokio::test]
    async fn test_global_admission() {
        let controller = create_test_controller(2, 10, 3);

        // First request should be admitted
        let decision = controller.check_admission(512).await;
        assert!(decision.admitted);
        assert_eq!(decision.reason, AdmissionReason::Admitted);

        // Admit two requests
        controller.counters.try_admit_global();
        controller.counters.try_admit_global();

        // Third should be rejected (global limit)
        let decision = controller.check_admission(512).await;
        assert!(!decision.admitted);
        match decision.reason {
            AdmissionReason::GlobalOverload { current, limit } => {
                assert_eq!(current, 2);
                assert_eq!(limit, 2);
            }
            _ => panic!("Expected GlobalOverload"),
        }
    }

    #[tokio::test]
    async fn test_per_pod_admission() {
        let controller = create_test_controller(100, 2, 3);

        // Admit to pod-0 twice
        assert!(controller.counters.try_admit_pod("pod-0").await);
        assert!(controller.counters.try_admit_pod("pod-0").await);

        // Third should fail
        assert!(!controller.counters.try_admit_pod("pod-0").await);

        // But pod-1 should still have capacity
        assert!(controller.counters.try_admit_pod("pod-1").await);
    }

    #[tokio::test]
    async fn test_no_pod_capacity() {
        let controller = create_test_controller(100, 1, 2);

        // Fill up both pods
        controller.counters.try_admit_pod("pod-0").await;
        controller.counters.try_admit_pod("pod-1").await;

        // Should reject due to no capacity
        let decision = controller.check_admission(512).await;
        assert!(!decision.admitted);
        match decision.reason {
            AdmissionReason::NoPodCapacity { pods_checked } => {
                assert_eq!(pods_checked, 2);
            }
            _ => panic!("Expected NoPodCapacity"),
        }
    }

    #[tokio::test]
    async fn test_degradation() {
        let policy = AdmissionPolicy {
            global_max_inflight: 10,
            per_pod_max_inflight: 10,
            degrade_policy: DegradePolicy {
                enabled: true,
                threshold: 0.5,
                min_max_tokens: 64,
                degradation_factor: 0.5,
            },
        };
        
        let pod_ids = vec!["pod-0".to_string()];
        let controller = AdmissionController::new(policy, &pod_ids);

        // Admit 6 requests to reach 60% load (above 50% threshold)
        for _ in 0..6 {
            controller.counters.try_admit_global();
        }

        // Next request should be degraded
        let decision = controller.check_admission(1024).await;
        assert!(decision.admitted);
        assert!(decision.was_degraded);
        assert!(decision.degraded_max_tokens < decision.original_max_tokens);
        assert!(decision.degraded_max_tokens >= 64);
    }

    #[tokio::test]
    async fn test_inflight_counter_lifecycle() {
        let controller = create_test_controller(100, 10, 2);

        // Initial state
        assert_eq!(controller.counters.global_current(), 0);
        assert_eq!(controller.counters.get_pod_current("pod-0").await, Some(0));

        // Admit request
        controller.counters.try_admit_global();
        controller.counters.try_admit_pod("pod-0").await;

        assert_eq!(controller.counters.global_current(), 1);
        assert_eq!(controller.counters.get_pod_current("pod-0").await, Some(1));

        // Release request
        controller.counters.release_global();
        controller.counters.release_pod("pod-0").await;

        assert_eq!(controller.counters.global_current(), 0);
        assert_eq!(controller.counters.get_pod_current("pod-0").await, Some(0));
    }

    #[tokio::test]
    async fn test_concurrent_inflight_updates() {
        let controller = create_test_controller(1000, 100, 10);
        let mut handles = vec![];

        // Spawn 100 concurrent admissions
        for i in 0..100 {
            let controller = controller.clone();
            let handle = tokio::spawn(async move {
                let pod_id = format!("pod-{}", i % 10);
                controller.counters.try_admit_global();
                controller.counters.try_admit_pod(&pod_id).await;
            });
            handles.push(handle);
        }

        // Wait for all to complete
        for handle in handles {
            handle.await.unwrap();
        }

        // Verify counts
        assert_eq!(controller.counters.global_current(), 100);
        let pod_counts = controller.counters.get_all_pod_counts().await;
        assert_eq!(pod_counts.len(), 10);
        for (_, count) in pod_counts {
            assert_eq!(count, 10); // Each pod got 10 requests
        }

        // Release all
        for _ in 0..100 {
            controller.counters.release_global();
        }
        for i in 0..10 {
            controller.counters.release_pod(&format!("pod-{}", i)).await;
        }

        assert_eq!(controller.counters.global_current(), 0);
    }
}
