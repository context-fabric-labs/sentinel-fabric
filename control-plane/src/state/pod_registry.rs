use crate::config::PodConfig;
use chrono::{DateTime, Utc};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;

/// Runtime health snapshot for a pod
#[derive(Debug, Clone)]
pub struct PodHealthSnapshot {
    /// Is the pod healthy (responding to health checks)
    pub is_healthy: bool,
    /// Number of inflight requests
    pub inflight_count: u32,
    /// Estimated GPU memory used (MB)
    pub gpu_memory_used_mb: u64,
    /// Recent latency EWMA (milliseconds)
    pub latency_ewma_ms: f64,
    /// Recent error rate (0.0 to 1.0)
    pub error_rate: f64,
    /// Last health check timestamp
    pub last_check: DateTime<Utc>,
    /// Consecutive health check failures
    pub consecutive_failures: u32,
}

impl Default for PodHealthSnapshot {
    fn default() -> Self {
        Self {
            is_healthy: true,
            inflight_count: 0,
            gpu_memory_used_mb: 0,
            latency_ewma_ms: 0.0,
            error_rate: 0.0,
            last_check: Utc::now(),
            consecutive_failures: 0,
        }
    }
}

/// Combined pod state: config + runtime snapshot
#[derive(Debug, Clone)]
pub struct PodState {
    pub config: PodConfig,
    pub snapshot: PodHealthSnapshot,
}

impl PodState {
    pub fn new(config: PodConfig) -> Self {
        Self {
            config,
            snapshot: PodHealthSnapshot::default(),
        }
    }

    /// Calculate available GPU memory in MB
    pub fn gpu_memory_free_mb(&self) -> u64 {
        self.config.gpu_memory_mb.saturating_sub(self.snapshot.gpu_memory_used_mb)
    }

    /// Calculate GPU memory utilization ratio (0.0 to 1.0)
    pub fn gpu_memory_utilization(&self) -> f64 {
        if self.config.gpu_memory_mb == 0 {
            return 1.0;
        }
        self.snapshot.gpu_memory_used_mb as f64 / self.config.gpu_memory_mb as f64
    }
}

/// Registry holding all pod states
#[derive(Debug, Clone)]
pub struct PodRegistry {
    pods: Arc<RwLock<HashMap<String, PodState>>>,
}

impl PodRegistry {
    pub fn new(pod_configs: Vec<PodConfig>) -> Self {
        let pods: HashMap<String, PodState> = pod_configs
            .into_iter()
            .map(|config| (config.id.clone(), PodState::new(config)))
            .collect();
        Self {
            pods: Arc::new(RwLock::new(pods)),
        }
    }

    /// Get all pods (read-only snapshot)
    pub async fn get_all_pods(&self) -> Vec<PodState> {
        let lock = self.pods.read().await;
        lock.values().cloned().collect()
    }

    /// Get a specific pod by ID
    pub async fn get_pod(&self, id: &str) -> Option<PodState> {
        let lock = self.pods.read().await;
        lock.get(id).cloned()
    }

    /// Update a pod's health snapshot
    pub async fn update_pod_snapshot(&self, pod_id: &str, snapshot: PodHealthSnapshot) {
        let mut lock = self.pods.write().await;
        if let Some(pod) = lock.get_mut(pod_id) {
            pod.snapshot = snapshot;
        }
    }

    /// Get count of healthy pods
    pub async fn healthy_count(&self) -> usize {
        let lock = self.pods.read().await;
        lock.values().filter(|p| p.snapshot.is_healthy).count()
    }

    /// Get total pod count
    pub async fn total_count(&self) -> usize {
        let lock = self.pods.read().await;
        lock.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::PodConfig;

    #[tokio::test]
    async fn test_pod_registry() {
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

        let registry = PodRegistry::new(configs);
        assert_eq!(registry.total_count().await, 2);
        assert_eq!(registry.healthy_count().await, 2);

        // Update one pod to unhealthy
        let mut snapshot = PodHealthSnapshot::default();
        snapshot.is_healthy = false;
        registry.update_pod_snapshot("pod-1", snapshot).await;

        assert_eq!(registry.healthy_count().await, 1);
    }

    #[tokio::test]
    async fn test_gpu_memory_calculation() {
        let config = PodConfig {
            id: "pod-1".to_string(),
            address: "http://localhost:8001".to_string(),
            name: None,
            model_id: "llama-8b".to_string(),
            gpu_memory_mb: 80000,
            num_layers: 32,
            hidden_size: 4096,
            weight: 1.0,
        };

        let mut pod = PodState::new(config);
        assert_eq!(pod.gpu_memory_free_mb(), 80000);
        assert_eq!(pod.gpu_memory_utilization(), 0.0);

        pod.snapshot.gpu_memory_used_mb = 40000;
        assert_eq!(pod.gpu_memory_free_mb(), 40000);
        assert_eq!(pod.gpu_memory_utilization(), 0.5);
    }
}
