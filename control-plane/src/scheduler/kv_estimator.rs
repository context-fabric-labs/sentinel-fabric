/// KV Cache Memory Estimator
/// 
/// Estimates the GPU memory required for KV cache based on:
/// - Sequence length (prompt + max_tokens)
/// - Model architecture (layers, hidden_size)
/// - Data type (FP16/FP32)
/// - Batch size (inflight requests)
/// 
/// Formula:
/// KV_memory = seq_len × num_layers × 2 × hidden_size × bytes_per_element × batch_size
/// 
/// This is an estimate for routing decisions, not exact runtime measurement.

use serde::{Deserialize, Serialize};

/// KV cache data type
#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub enum KvDType {
    /// 16-bit floating point (2 bytes)
    FP16,
    /// 32-bit floating point (4 bytes)
    FP32,
    /// 8-bit floating point (1 byte)
    FP8,
    /// Brain floating point (2 bytes)
    BF16,
}

impl KvDType {
    /// Get bytes per element for this dtype
    pub fn bytes_per_element(&self) -> u32 {
        match self {
            KvDType::FP16 => 2,
            KvDType::FP32 => 4,
            KvDType::FP8 => 1,
            KvDType::BF16 => 2,
        }
    }
}

impl Default for KvDType {
    fn default() -> Self {
        KvDType::FP16 // Most common for inference
    }
}

/// Model configuration for KV estimation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModelConfig {
    /// Model identifier (e.g., "llama-8b")
    pub model_id: String,
    /// Number of transformer layers
    pub num_layers: u32,
    /// Hidden size (embedding dimension)
    pub hidden_size: u32,
    /// KV cache data type
    #[serde(default)]
    pub kv_dtype: KvDType,
    /// Optional: number of KV heads (for GQA/MQA)
    /// If None, assumes MHA (num_heads = hidden_size / head_dim)
    pub num_kv_heads: Option<u32>,
    /// Optional: head dimension
    /// If None, assumes 128 (common default)
    pub head_dim: Option<u32>,
}

impl ModelConfig {
    /// Create a new model config
    pub fn new(model_id: &str, num_layers: u32, hidden_size: u32) -> Self {
        Self {
            model_id: model_id.to_string(),
            num_layers,
            hidden_size,
            kv_dtype: KvDType::default(),
            num_kv_heads: None,
            head_dim: None,
        }
    }

    /// Calculate KV cache size per token (in bytes)
    /// This is the memory needed for one token in the KV cache
    pub fn kv_bytes_per_token(&self) -> u64 {
        let bytes_per_element = self.kv_dtype.bytes_per_element() as u64;
        
        // For MHA (Multi-Head Attention):
        // KV_bytes_per_token = num_layers × 2 × hidden_size × bytes_per_element
        //
        // For GQA (Grouped-Query Attention) or MQA (Multi-Query Attention):
        // KV_bytes_per_token = num_layers × 2 × (num_kv_heads × head_dim) × bytes_per_element
        
        let effective_hidden = if let (Some(num_kv_heads), Some(head_dim)) = (self.num_kv_heads, self.head_dim) {
            // GQA/MQA: use actual KV heads
            (num_kv_heads as u64) * (head_dim as u64)
        } else {
            // MHA: assume hidden_size covers all heads
            self.hidden_size as u64
        };

        // 2 for K and V caches
        (self.num_layers as u64) * 2 * effective_hidden * bytes_per_element
    }
}

/// KV estimate for a single request
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RequestKvEstimate {
    /// Prompt tokens
    pub prompt_tokens: u32,
    /// Max tokens to generate
    pub max_tokens: u32,
    /// Total sequence length
    pub seq_len: u32,
    /// KV bytes per token for this model
    pub kv_bytes_per_token: u64,
    /// Total KV memory for this request (in bytes)
    pub total_kv_bytes: u64,
    /// Total KV memory in MB (for readability)
    pub total_kv_mb: f64,
}

impl RequestKvEstimate {
    /// Estimate KV memory for a request
    pub fn estimate(prompt_tokens: u32, max_tokens: u32, model: &ModelConfig) -> Self {
        let seq_len = prompt_tokens + max_tokens;
        let kv_bytes_per_token = model.kv_bytes_per_token();
        let total_kv_bytes = (seq_len as u64) * kv_bytes_per_token;
        let total_kv_mb = total_kv_bytes as f64 / (1024.0 * 1024.0);

        Self {
            prompt_tokens,
            max_tokens,
            seq_len,
            kv_bytes_per_token,
            total_kv_bytes,
            total_kv_mb,
        }
    }
}

/// Aggregate KV pressure for a pod
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PodKvPressure {
    /// Pod ID
    pub pod_id: String,
    /// Number of inflight requests
    pub inflight_count: u32,
    /// Total KV memory in use (bytes)
    pub total_kv_bytes: u64,
    /// Total KV memory in use (MB)
    pub total_kv_mb: f64,
    /// GPU memory capacity (MB)
    pub gpu_memory_mb: u64,
    /// KV pressure as fraction of GPU memory (0.0 to 1.0)
    pub pressure_ratio: f64,
    /// Average KV per request (MB)
    pub avg_kv_per_request_mb: f64,
}

impl PodKvPressure {
    /// Calculate aggregate KV pressure for a pod
    pub fn calculate(
        pod_id: &str,
        inflight_count: u32,
        avg_seq_len: u32,
        model: &ModelConfig,
        gpu_memory_mb: u64,
    ) -> Self {
        let kv_bytes_per_token = model.kv_bytes_per_token();
        let avg_kv_bytes = (avg_seq_len as u64) * kv_bytes_per_token;
        let total_kv_bytes = avg_kv_bytes * (inflight_count as u64);
        let total_kv_mb = total_kv_bytes as f64 / (1024.0 * 1024.0);
        let pressure_ratio = if gpu_memory_mb > 0 {
            total_kv_mb / (gpu_memory_mb as f64)
        } else {
            1.0
        };
        let avg_kv_per_request_mb = if inflight_count > 0 {
            total_kv_mb / (inflight_count as f64)
        } else {
            0.0
        };

        Self {
            pod_id: pod_id.to_string(),
            inflight_count,
            total_kv_bytes,
            total_kv_mb,
            gpu_memory_mb,
            pressure_ratio,
            avg_kv_per_request_mb,
        }
    }

    /// Get pressure level (for routing decisions)
    pub fn pressure_level(&self) -> KvPressureLevel {
        if self.pressure_ratio < 0.3 {
            KvPressureLevel::Low
        } else if self.pressure_ratio < 0.6 {
            KvPressureLevel::Medium
        } else if self.pressure_ratio < 0.85 {
            KvPressureLevel::High
        } else {
            KvPressureLevel::Critical
        }
    }
}

/// KV pressure level for routing decisions
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum KvPressureLevel {
    /// < 30% GPU memory - plenty of room
    Low,
    /// 30-60% GPU memory - normal operation
    Medium,
    /// 60-85% GPU memory - getting full
    High,
    /// > 85% GPU memory - risk of OOM
    Critical,
}

impl KvPressureLevel {
    /// Get score multiplier for this pressure level
    /// Lower pressure = higher score (better for routing)
    pub fn score_multiplier(&self) -> f64 {
        match self {
            KvPressureLevel::Low => 1.0,
            KvPressureLevel::Medium => 0.7,
            KvPressureLevel::High => 0.4,
            KvPressureLevel::Critical => 0.1,
        }
    }
}

/// KV pressure estimator
#[derive(Debug, Clone)]
pub struct KvPressureEstimator {
    /// Default model config (used if request doesn't specify)
    default_model: ModelConfig,
}

impl KvPressureEstimator {
    pub fn new(default_model: ModelConfig) -> Self {
        Self { default_model }
    }

    /// Estimate KV memory for a request
    pub fn estimate_request(&self, prompt_tokens: u32, max_tokens: u32) -> RequestKvEstimate {
        RequestKvEstimate::estimate(prompt_tokens, max_tokens, &self.default_model)
    }

    /// Estimate KV pressure for a pod
    pub fn estimate_pod_pressure(
        &self,
        pod_id: &str,
        inflight_count: u32,
        avg_seq_len: u32,
        gpu_memory_mb: u64,
    ) -> PodKvPressure {
        PodKvPressure::calculate(
            pod_id,
            inflight_count,
            avg_seq_len,
            &self.default_model,
            gpu_memory_mb,
        )
    }

    /// Get the default model config
    pub fn default_model(&self) -> &ModelConfig {
        &self.default_model
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn llama_8b_config() -> ModelConfig {
        // Llama 2 7B/8B config
        ModelConfig {
            model_id: "llama-8b".to_string(),
            num_layers: 32,
            hidden_size: 4096,
            kv_dtype: KvDType::FP16,
            num_kv_heads: None,
            head_dim: None,
        }
    }

    #[test]
    fn test_kv_bytes_per_token_llama() {
        let model = llama_8b_config();
        let bytes_per_token = model.kv_bytes_per_token();
        
        // Expected: 32 layers × 2 × 4096 × 2 bytes = 524,288 bytes per token
        assert_eq!(bytes_per_token, 524_288);
    }

    #[test]
    fn test_request_estimate_basic() {
        let model = llama_8b_config();
        let estimate = RequestKvEstimate::estimate(1000, 500, &model);
        
        assert_eq!(estimate.prompt_tokens, 1000);
        assert_eq!(estimate.max_tokens, 500);
        assert_eq!(estimate.seq_len, 1500);
        
        // 1500 tokens × 524,288 bytes/token = 786,432,000 bytes
        assert_eq!(estimate.total_kv_bytes, 786_432_000);
        assert!((estimate.total_kv_mb - 750.0).abs() < 1.0); // ~750 MB
    }

    #[test]
    fn test_request_estimate_heavier_prompt() {
        let model = llama_8b_config();
        
        // Short prompt
        let short = RequestKvEstimate::estimate(100, 100, &model);
        // Long prompt
        let long = RequestKvEstimate::estimate(8000, 100, &model);
        
        // Long prompt should have higher KV estimate
        assert!(long.total_kv_bytes > short.total_kv_bytes);
        assert!(long.total_kv_mb > short.total_kv_mb);
    }

    #[test]
    fn test_request_estimate_max_tokens_impact() {
        let model = llama_8b_config();
        
        // Small max_tokens
        let small = RequestKvEstimate::estimate(1000, 100, &model);
        // Large max_tokens
        let large = RequestKvEstimate::estimate(1000, 2000, &model);
        
        // Larger max_tokens should have higher KV estimate
        assert!(large.total_kv_bytes > small.total_kv_bytes);
        assert!(large.seq_len > small.seq_len);
    }

    #[test]
    fn test_pod_pressure_calculation() {
        let model = llama_8b_config();
        
        // Pod with 10 inflight requests, avg 1000 tokens each
        let pressure = PodKvPressure::calculate(
            "pod-1",
            10,
            1000,
            &model,
            80_000, // 80 GB GPU
        );
        
        assert_eq!(pressure.inflight_count, 10);
        assert_eq!(pressure.pod_id, "pod-1");
        
        // Each request: 1000 × 524,288 = 524,288,000 bytes = ~500 MB
        // 10 requests: ~5000 MB
        assert!(pressure.total_kv_mb > 4900.0 && pressure.total_kv_mb < 5100.0);
        
        // Pressure ratio: 5000 MB / 80000 MB = 0.0625
        assert!((pressure.pressure_ratio - 0.0625).abs() < 0.01);
    }

    #[test]
    fn test_pod_pressure_levels() {
        let model = llama_8b_config();
        
        // Low pressure: 1 request, 80 GB GPU
        let low = PodKvPressure::calculate("pod-1", 1, 1000, &model, 80_000);
        assert_eq!(low.pressure_level(), KvPressureLevel::Low);
        
        // Medium pressure: 50 requests
        let medium = PodKvPressure::calculate("pod-2", 50, 1000, &model, 80_000);
        assert_eq!(medium.pressure_level(), KvPressureLevel::Medium);
        
        // High pressure: 100 requests
        let high = PodKvPressure::calculate("pod-3", 100, 1000, &model, 80_000);
        assert_eq!(high.pressure_level(), KvPressureLevel::High);
        
        // Critical pressure: 150 requests
        let critical = PodKvPressure::calculate("pod-4", 150, 1000, &model, 80_000);
        assert_eq!(critical.pressure_level(), KvPressureLevel::Critical);
    }

    #[test]
    fn test_pressure_score_multiplier() {
        assert_eq!(KvPressureLevel::Low.score_multiplier(), 1.0);
        assert_eq!(KvPressureLevel::Medium.score_multiplier(), 0.7);
        assert_eq!(KvPressureLevel::High.score_multiplier(), 0.4);
        assert_eq!(KvPressureLevel::Critical.score_multiplier(), 0.1);
    }

    #[test]
    fn test_different_dtypes() {
        let mut model = llama_8b_config();
        
        model.kv_dtype = KvDType::FP16;
        let fp16_bytes = model.kv_bytes_per_token();
        
        model.kv_dtype = KvDType::FP32;
        let fp32_bytes = model.kv_bytes_per_token();
        
        // FP32 should use 2x memory of FP16
        assert_eq!(fp32_bytes, fp16_bytes * 2);
        
        model.kv_dtype = KvDType::FP8;
        let fp8_bytes = model.kv_bytes_per_token();
        
        // FP8 should use 0.5x memory of FP16
        assert_eq!(fp8_bytes, fp16_bytes / 2);
    }

    #[test]
    fn test_estimator() {
        let model = llama_8b_config();
        let estimator = KvPressureEstimator::new(model);
        
        let estimate = estimator.estimate_request(2000, 500);
        assert_eq!(estimate.seq_len, 2500);
        assert!(estimate.total_kv_mb > 0.0);
        
        let pressure = estimator.estimate_pod_pressure("pod-1", 5, 2000, 80_000);
        assert_eq!(pressure.inflight_count, 5);
        assert!(pressure.pressure_ratio > 0.0);
    }
}
