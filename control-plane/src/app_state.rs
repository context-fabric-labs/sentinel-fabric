use crate::config::Config;
use crate::state::pod_registry::PodRegistry;
use crate::admission::AdmissionController;

/// Shared application state
#[derive(Clone)]
pub struct AppState {
    pub config: Config,
    pub pod_registry: PodRegistry,
    pub admission_controller: AdmissionController,
}

impl AppState {
    pub fn new(config: Config) -> Self {
        let pod_registry = PodRegistry::new(config.pods.clone());
        
        // Extract pod IDs for admission controller
        let pod_ids: Vec<String> = config.pods.iter().map(|p| p.id.clone()).collect();
        
        // Create admission controller with default policy
        // (In production, this would come from config)
        let admission_policy = crate::admission::AdmissionPolicy::default();
        let admission_controller = crate::admission::AdmissionController::new(
            admission_policy,
            &pod_ids,
        );
        
        Self {
            config,
            pod_registry,
            admission_controller,
        }
    }
}
