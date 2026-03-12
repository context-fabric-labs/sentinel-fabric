use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;
use chrono::{DateTime, Utc};

/// Session-to-pod mapping with TTL
#[derive(Debug, Clone)]
pub struct SessionMapping {
    /// Pod ID this session is mapped to
    pub pod_id: String,
    /// When this mapping was created
    pub created_at: DateTime<Utc>,
    /// When this mapping expires
    pub expires_at: DateTime<Utc>,
}

/// Session map for tracking active sessions
/// 
/// This is optional - HRW works without state, but this allows:
/// - Metrics on session duration
/// - Explicit session expiration
/// - Debugging/observability of active sessions
#[derive(Debug, Clone)]
pub struct SessionMap {
    sessions: Arc<RwLock<HashMap<String, SessionMapping>>>,
    default_ttl_secs: u64,
}

impl SessionMap {
    pub fn new(default_ttl_secs: u64) -> Self {
        Self {
            sessions: Arc::new(RwLock::new(HashMap::new())),
            default_ttl_secs,
        }
    }

    /// Get or create a mapping for a session
    pub async fn get_or_create(&self, session_id: &str, preferred_pod: &str) -> SessionMapping {
        let now = Utc::now();
        
        // Check if mapping exists and is not expired
        {
            let lock = self.sessions.read().await;
            if let Some(mapping) = lock.get(session_id) {
                if mapping.expires_at > now {
                    return mapping.clone();
                }
            }
        }
        
        // Create new mapping
        let mapping = SessionMapping {
            pod_id: preferred_pod.to_string(),
            created_at: now,
            expires_at: now + chrono::Duration::seconds(self.default_ttl_secs as i64),
        };
        
        // Store mapping
        let mut lock = self.sessions.write().await;
        lock.insert(session_id.to_string(), mapping.clone());
        
        mapping
    }

    /// Get mapping if it exists and is not expired
    pub async fn get(&self, session_id: &str) -> Option<SessionMapping> {
        let lock = self.sessions.read().await;
        let mapping = lock.get(session_id)?;
        
        if mapping.expires_at > Utc::now() {
            Some(mapping.clone())
        } else {
            None
        }
    }

    /// Remove a session mapping (e.g., when session ends)
    pub async fn remove(&self, session_id: &str) {
        let mut lock = self.sessions.write().await;
        lock.remove(session_id);
    }

    /// Clean up expired mappings
    pub async fn cleanup_expired(&self) -> usize {
        let now = Utc::now();
        let mut lock = self.sessions.write().await;
        let initial_count = lock.len();
        
        lock.retain(|_, mapping| mapping.expires_at > now);
        
        initial_count - lock.len()
    }

    /// Get count of active sessions
    pub async fn active_count(&self) -> usize {
        let lock = self.sessions.read().await;
        lock.len()
    }

    /// Get all active sessions (for debugging)
    pub async fn get_all(&self) -> HashMap<String, SessionMapping> {
        let lock = self.sessions.read().await;
        lock.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_session_mapping_create() {
        let map = SessionMap::new(3600); // 1 hour TTL
        
        let mapping = map.get_or_create("session-123", "pod-1").await;
        assert_eq!(mapping.pod_id, "pod-1");
        assert!(mapping.created_at <= Utc::now());
        assert!(mapping.expires_at > Utc::now());
    }

    #[tokio::test]
    async fn test_session_mapping_reuse() {
        let map = SessionMap::new(3600);
        
        // Create mapping
        let _mapping1 = map.get_or_create("session-123", "pod-1").await;
        
        // Get existing mapping
        let mapping2 = map.get("session-123").await;
        assert!(mapping2.is_some());
        assert_eq!(mapping2.unwrap().pod_id, "pod-1");
    }

    #[tokio::test]
    async fn test_session_mapping_removal() {
        let map = SessionMap::new(3600);
        
        // Create and remove
        map.get_or_create("session-123", "pod-1").await;
        map.remove("session-123").await;
        
        // Should not exist
        let mapping = map.get("session-123").await;
        assert!(mapping.is_none());
    }

    #[tokio::test]
    async fn test_active_count() {
        let map = SessionMap::new(3600);
        
        assert_eq!(map.active_count().await, 0);
        
        map.get_or_create("session-1", "pod-1").await;
        map.get_or_create("session-2", "pod-2").await;
        
        assert_eq!(map.active_count().await, 2);
    }
}
