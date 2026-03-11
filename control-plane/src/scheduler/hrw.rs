use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};

/// Compute HRW (Highest Random Weight) hash for a session-pod pair
/// 
/// This implements rendezvous hashing: for each candidate pod, compute
/// hash(session_id + pod_id) and select the pod with the highest hash value.
/// 
/// Properties:
/// - Deterministic: same session_id + pod_id always produces same hash
/// - Uniform: hash values are uniformly distributed
/// - Simple: no state required, works with any set of pods
pub fn hrw_hash(session_id: &str, pod_id: &str) -> u64 {
    let mut hasher = DefaultHasher::new();
    session_id.hash(&mut hasher);
    pod_id.hash(&mut hasher);
    hasher.finish()
}

/// Result of HRW-based pod selection
#[derive(Debug, Clone)]
pub struct HrwSelection {
    /// Selected pod ID
    pub pod_id: String,
    /// HRW hash value (for debugging/explanation)
    pub hash_value: u64,
    /// Rank (0 = highest hash, 1 = second highest, etc.)
    pub rank: usize,
}

/// Select pods using HRW/rendezvous hashing
/// 
/// Returns all pods sorted by HRW hash (descending), so:
/// - result[0] is the best choice (highest hash)
/// - result[1] is the fallback (2nd highest)
/// - etc.
/// 
/// This allows easy fallback: if result[0] is unhealthy, try result[1], etc.
pub fn hrw_select_pods(session_id: &str, pod_ids: &[String]) -> Vec<HrwSelection> {
    let mut selections: Vec<HrwSelection> = pod_ids
        .iter()
        .map(|pod_id| {
            let hash = hrw_hash(session_id, pod_id);
            HrwSelection {
                pod_id: pod_id.clone(),
                hash_value: hash,
                rank: 0, // Will be set after sorting
            }
        })
        .collect();

    // Sort by hash descending (highest first)
    selections.sort_by(|a, b| b.hash_value.cmp(&a.hash_value));

    // Assign ranks
    for (i, selection) in selections.iter_mut().enumerate() {
        selection.rank = i;
    }

    selections
}

/// Get the preferred pod for a session (highest HRW hash)
pub fn hrw_preferred_pod(session_id: &str, pod_ids: &[String]) -> Option<String> {
    hrw_select_pods(session_id, pod_ids)
        .first()
        .map(|s| s.pod_id.clone())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hash_deterministic() {
        let hash1 = hrw_hash("session-123", "pod-1");
        let hash2 = hrw_hash("session-123", "pod-1");
        assert_eq!(hash1, hash2);
    }

    #[test]
    fn test_hash_different_sessions() {
        let hash1 = hrw_hash("session-123", "pod-1");
        let hash2 = hrw_hash("session-456", "pod-1");
        assert_ne!(hash1, hash2);
    }

    #[test]
    fn test_hash_different_pods() {
        let hash1 = hrw_hash("session-123", "pod-1");
        let hash2 = hrw_hash("session-123", "pod-2");
        assert_ne!(hash1, hash2);
    }

    #[test]
    fn test_selection_deterministic() {
        let pods = vec!["pod-1".to_string(), "pod-2".to_string(), "pod-3".to_string()];
        
        let result1 = hrw_select_pods("session-123", &pods);
        let result2 = hrw_select_pods("session-123", &pods);
        
        assert_eq!(result1.len(), 3);
        assert_eq!(result2.len(), 3);
        
        // Same session should produce same ordering
        assert_eq!(result1[0].pod_id, result2[0].pod_id);
        assert_eq!(result1[1].pod_id, result2[1].pod_id);
        assert_eq!(result1[2].pod_id, result2[2].pod_id);
    }

    #[test]
    fn test_different_sessions_different_ordering() {
        let pods = vec!["pod-1".to_string(), "pod-2".to_string(), "pod-3".to_string()];
        
        let result1 = hrw_select_pods("session-123", &pods);
        let result2 = hrw_select_pods("session-456", &pods);
        
        // Different sessions should (with high probability) have different orderings
        // Note: there's a tiny chance they could be the same, but it's very unlikely
        assert_ne!(result1[0].hash_value, result2[0].hash_value);
    }

    #[test]
    fn test_ranks_assigned() {
        let pods = vec!["pod-1".to_string(), "pod-2".to_string(), "pod-3".to_string()];
        let result = hrw_select_pods("session-123", &pods);
        
        assert_eq!(result[0].rank, 0);
        assert_eq!(result[1].rank, 1);
        assert_eq!(result[2].rank, 2);
    }

    #[test]
    fn test_preferred_pod() {
        let pods = vec!["pod-1".to_string(), "pod-2".to_string()];
        
        let preferred1 = hrw_preferred_pod("session-123", &pods);
        let preferred2 = hrw_preferred_pod("session-123", &pods);
        
        assert!(preferred1.is_some());
        assert_eq!(preferred1, preferred2);
    }

    #[test]
    fn test_empty_pods() {
        let pods: Vec<String> = vec![];
        let result = hrw_select_pods("session-123", &pods);
        assert!(result.is_empty());
        
        let preferred = hrw_preferred_pod("session-123", &pods);
        assert!(preferred.is_none());
    }
}
