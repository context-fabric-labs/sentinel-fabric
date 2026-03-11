use serde::{Deserialize, Serialize};
use std::time::Duration;

/// Configuration for a single backend pod
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PodConfig {
    /// Unique identifier for this pod
    pub id: String,
    /// HTTP address (e.g., "http://10.0.1.5:8000")
    pub address: String,
    /// Optional human-readable name
    pub name: Option<String>,
    /// Model served by this pod (for future filtering)
    pub model_id: String,
    /// GPU memory capacity in MB (for headroom calculations)
    pub gpu_memory_mb: u64,
    /// Number of transformer layers (for KV estimation)
    pub num_layers: u32,
    /// Hidden size (for KV estimation)
    pub hidden_size: u32,
    /// Weight for scheduling (higher = preferred)
    pub weight: f64,
}

/// Top-level configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    /// List of backend pods
    pub pods: Vec<PodConfig>,
    /// Health check interval
    #[serde(default = "default_health_interval")]
    pub health_check_interval_secs: u64,
    /// Health check timeout
    #[serde(default = "default_health_timeout")]
    pub health_check_timeout_secs: u64,
    /// Metrics endpoint port
    #[serde(default = "default_metrics_port")]
    pub metrics_port: u16,
    /// API server port
    #[serde(default = "default_api_port")]
    pub api_port: u16,
}

fn default_health_interval() -> u64 {
    5
}

fn default_health_timeout() -> u64 {
    3
}

fn default_metrics_port() -> u16 {
    9090
}

fn default_api_port() -> u16 {
    8080
}

impl Config {
    pub fn load_from_path(path: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let content = std::fs::read_to_string(path)?;
        let config: Config = serde_yaml::from_str(&content)?;
        Ok(config)
    }

    pub fn health_check_interval(&self) -> Duration {
        Duration::from_secs(self.health_check_interval_secs)
    }

    pub fn health_check_timeout(&self) -> Duration {
        Duration::from_secs(self.health_check_timeout_secs)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_config_load() {
        let yaml = r#"
pods:
  - id: pod-1
    address: http://localhost:8001
    model_id: llama-8b
    gpu_memory_mb: 80000
    num_layers: 32
    hidden_size: 4096
    weight: 1.0
health_check_interval_secs: 5
api_port: 8080
"#;
        let config: Config = serde_yaml::from_str(yaml).unwrap();
        assert_eq!(config.pods.len(), 1);
        assert_eq!(config.pods[0].id, "pod-1");
        assert_eq!(config.pods[0].gpu_memory_mb, 80000);
    }
}
