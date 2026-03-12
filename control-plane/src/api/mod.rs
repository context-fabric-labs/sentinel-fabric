pub use admission::{create_router as create_admission_router, AdmissionStatusResponse, LoadTestResponse};
pub use debug_route::{create_router as create_debug_router, RouteDebugRequest, RouteDebugResponse};
pub use health::{create_router as create_health_router, HealthResponse};
pub use proxy::{create_router as create_proxy_router, ProxyRequest, ProxyResponse};

mod admission;
mod debug_route;
mod health;
mod proxy;
