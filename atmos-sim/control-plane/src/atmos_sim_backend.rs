use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;

/// Configuration for an ATMOS simulation scenario.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AtmosSimConfig {
    pub name: String,
    pub topology: TopologyConfig,
    pub model: ModelConfig,
    pub workload: WorkloadConfig,
    pub calibration: CalibrationConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TopologyConfig {
    pub root_complexes: Vec<RootComplexConfig>,
    pub switches: Vec<SwitchConfig>,
    pub devices: Vec<DeviceConfig>,
    pub inter_rc_link: Option<LinkConfig>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RootComplexConfig {
    pub name: String,
    pub aggregate_bandwidth_bytes_sec: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SwitchConfig {
    pub name: String,
    pub port_count: u32,
    pub switch_latency_ns: u64,
    pub rc: String,
    pub uplink: LinkConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LinkConfig {
    pub generation: u8,
    pub lane_count: u32,
    pub latency_ns: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceConfig {
    pub name: String,
    pub index: u32,
    pub switch_name: String,
    pub link: LinkConfig,
    pub module: AtmosModuleConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AtmosModuleConfig {
    pub name: String,
    pub npu: NpuConfig,
    pub hbf: HbfConfig,
    pub lpddr: LpddrConfig,
    pub dma: DmaConfig,
    pub dma_engine_count: u32,
    pub pcie: PcieConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NpuConfig {
    pub peak_flops: u64,
    pub tensor_engine_count: u32,
    pub vector_engine_count: u32,
    pub execution_slots: u32,
    pub local_sram_bytes: u64,
    pub command_queue_depth: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HbfConfig {
    pub channel_count: u32,
    pub capacity_bytes: u64,
    pub bandwidth_per_channel_bytes_sec: u64,
    pub max_outstanding: u32,
    pub protocol_overhead_ns: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LpddrConfig {
    pub channel_count: u32,
    pub capacity_bytes: u64,
    pub bandwidth_per_channel_bytes_sec: u64,
    pub max_outstanding: u32,
    pub read_write_turnaround_ns: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DmaConfig {
    pub descriptor_queue_depth: u32,
    pub max_outstanding: u32,
    pub channel_count: u32,
    pub setup_overhead_ns: u64,
    pub bandwidth_bytes_sec: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PcieConfig {
    pub generation: u8,
    pub lane_count: u32,
    pub max_outstanding: u32,
    pub credit_limit: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModelConfig {
    pub name: String,
    pub num_layers: u32,
    pub hidden_size: u32,
    pub num_heads: u32,
    pub num_kv_heads: u32,
    pub ffn_size: u32,
    pub vocab_size: u32,
    pub bytes_per_element: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WorkloadConfig {
    pub pattern: String,  // "poisson", "uniform", "fixed_interval", "burst"
    pub arrival_rate_rps: f64,
    pub total_requests: u64,
    pub seed: u32,
    pub prompt_tokens_min: u32,
    pub prompt_tokens_max: u32,
    pub output_tokens_min: u32,
    pub output_tokens_max: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CalibrationConfig {
    pub parameters: HashMap<String, CalibrationParam>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CalibrationParam {
    pub nominal: f64,
    pub low: f64,
    pub high: f64,
    pub confidence: String,  // "low", "medium", "high", "measured"
}

/// Results from a simulation experiment.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SimulationResult {
    pub experiment_id: String,
    pub config_name: String,
    pub events_processed: u64,
    pub final_time_ns: u64,
    pub throughput_tokens_per_sec: f64,
    pub p50_latency_ns: u64,
    pub p95_latency_ns: u64,
    pub p99_latency_ns: u64,
    pub resource_utilizations: Vec<ResourceUtilization>,
    pub stall_breakdown: HashMap<String, f64>,
    pub scaling_factor: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ResourceUtilization {
    pub name: String,
    pub utilization: f64,
    pub busy_time_ns: u64,
    pub idle_time_ns: u64,
}

/// ATMOS simulation experiment runner.
pub struct AtmosExperimentRunner {
    experiments: Arc<RwLock<HashMap<String, SimulationResult>>>,
}

impl AtmosExperimentRunner {
    pub fn new() -> Self {
        Self {
            experiments: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    /// Run a simulation experiment from configuration.
    pub async fn run_experiment(&self, config: AtmosSimConfig) -> Result<SimulationResult, String> {
        let experiment_id = uuid::Uuid::new_v4().to_string();

        // Validate configuration
        Self::validate_config(&config)?;

        // The actual C++ simulation is invoked via FFI or subprocess.
        // For now, return a placeholder result structure.
        let result = SimulationResult {
            experiment_id: experiment_id.clone(),
            config_name: config.name,
            events_processed: 0,
            final_time_ns: 0,
            throughput_tokens_per_sec: 0.0,
            p50_latency_ns: 0,
            p95_latency_ns: 0,
            p99_latency_ns: 0,
            resource_utilizations: vec![],
            stall_breakdown: HashMap::new(),
            scaling_factor: 1.0,
        };

        self.experiments.write().await.insert(experiment_id, result.clone());
        Ok(result)
    }

    /// Get experiment results by ID.
    pub async fn get_experiment(&self, id: &str) -> Option<SimulationResult> {
        self.experiments.read().await.get(id).cloned()
    }

    /// Validate simulation configuration.
    fn validate_config(config: &AtmosSimConfig) -> Result<(), String> {
        if config.topology.devices.is_empty() {
            return Err("At least one device required".to_string());
        }
        if config.model.num_layers == 0 {
            return Err("Model must have at least one layer".to_string());
        }
        if config.workload.total_requests == 0 {
            return Err("Workload must have at least one request".to_string());
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_config_serialization() {
        let config = AtmosSimConfig {
            name: "test".to_string(),
            topology: TopologyConfig {
                root_complexes: vec![],
                switches: vec![],
                devices: vec![],
                inter_rc_link: None,
            },
            model: ModelConfig {
                name: "test-model".to_string(),
                num_layers: 2,
                hidden_size: 512,
                num_heads: 8,
                num_kv_heads: 2,
                ffn_size: 1024,
                vocab_size: 32000,
                bytes_per_element: 2,
            },
            workload: WorkloadConfig {
                pattern: "poisson".to_string(),
                arrival_rate_rps: 10.0,
                total_requests: 100,
                seed: 42,
                prompt_tokens_min: 128,
                prompt_tokens_max: 1024,
                output_tokens_min: 64,
                output_tokens_max: 512,
            },
            calibration: CalibrationConfig {
                parameters: HashMap::new(),
            },
        };

        let json = serde_json::to_string(&config).unwrap();
        assert!(json.contains("test-model"));
        assert!(json.contains("poisson"));
    }

    #[tokio::test]
    async fn test_experiment_runner_validation() {
        let runner = AtmosExperimentRunner::new();

        let invalid_config = AtmosSimConfig {
            name: "empty".to_string(),
            topology: TopologyConfig {
                root_complexes: vec![],
                switches: vec![],
                devices: vec![],  // No devices — should fail
                inter_rc_link: None,
            },
            model: ModelConfig {
                name: "test".to_string(),
                num_layers: 2,
                hidden_size: 512,
                num_heads: 8,
                num_kv_heads: 2,
                ffn_size: 1024,
                vocab_size: 32000,
                bytes_per_element: 2,
            },
            workload: WorkloadConfig {
                pattern: "poisson".to_string(),
                arrival_rate_rps: 10.0,
                total_requests: 100,
                seed: 42,
                prompt_tokens_min: 128,
                prompt_tokens_max: 1024,
                output_tokens_min: 64,
                output_tokens_max: 512,
            },
            calibration: CalibrationConfig {
                parameters: HashMap::new(),
            },
        };

        let result = runner.run_experiment(invalid_config).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("device"));
    }
}
