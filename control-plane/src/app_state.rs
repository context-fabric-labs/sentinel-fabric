use crate::config::Config;
use crate::state::pod_registry::PodRegistry;

/// Shared application state
#[derive(Clone)]
pub struct AppState {
    pub config: Config,
    pub pod_registry: PodRegistry,
}

impl AppState {
    pub fn new(config: Config) -> Self {
        let pod_registry = PodRegistry::new(config.pods.clone());
        Self {
            config,
            pod_registry,
        }
    }
}
