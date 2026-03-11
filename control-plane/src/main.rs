mod admission;
mod api;
mod app_state;
mod config;
mod scheduler;
mod state;
mod telemetry;

use api::{create_admission_router, create_debug_router, create_health_router};
use app_state::AppState;
use config::Config;
use std::net::SocketAddr;
use tower_http::trace::TraceLayer;
use tracing::{error, info};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing
    telemetry::init_tracing();

    // Register metrics
    telemetry::register_metrics();

    // Load configuration
    let config_path = std::env::var("CONFIG_PATH")
        .unwrap_or_else(|_| "config/pods.yaml".to_string());

    info!("Loading configuration from: {}", config_path);
    let config = Config::load_from_path(&config_path)
        .unwrap_or_else(|e| {
            error!("Failed to load config: {}", e);
            eprintln!("Usage: CONFIG_PATH=<path> cargo run");
            std::process::exit(1);
        });

    info!(
        "Loaded {} pods from configuration",
        config.pods.len()
    );

    // Create shared state
    let state = AppState::new(config.clone());

    // Build admission router
    let admission_router = create_admission_router()
        .with_state(state.clone());

    // Build debug router
    let debug_router = create_debug_router()
        .with_state(state.clone());

    // Build health router
    let health_router = create_health_router()
        .with_state(state.clone());

    // Merge routers
    let app = admission_router
        .merge(debug_router)
        .merge(health_router)
        .layer(TraceLayer::new_for_http());

    // Start server
    let addr = SocketAddr::from(([0, 0, 0, 0], config.api_port));
    info!("Starting API server on {}", addr);

    let listener = tokio::net::TcpListener::bind(addr).await?;
    axum::serve(listener, app).await?;

    Ok(())
}
