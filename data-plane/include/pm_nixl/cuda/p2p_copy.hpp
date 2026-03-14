#include <cuda_runtime.h>

namespace pm_nixl {
namespace cuda {

/**
 * Check if peer access is supported between two devices
 * 
 * @param dev_id Source device ID
 * @param peer_id Peer device ID
 * @return true if peer access is supported
 */
bool is_peer_access_supported(int dev_id, int peer_id);

/**
 * Enable peer access from dev_id to peer_id
 * 
 * @param dev_id Source device ID
 * @param peer_id Peer device ID
 * @return cudaSuccess on success
 */
cudaError_t enable_peer_access(int dev_id, int peer_id);

/**
 * Disable peer access from dev_id to peer_id
 * 
 * @param dev_id Source device ID
 * @param peer_id Peer device ID
 */
void disable_peer_access(int dev_id, int peer_id);

/**
 * Check if P2P copy is supported between two devices
 * 
 * @param dev_id Source device ID
 * @param peer_id Peer device ID
 * @return true if P2P copy is supported
 */
bool is_p2p_copy_supported(int dev_id, int peer_id);

/**
 * Get device topology information
 */
struct DeviceTopology {
    int device_id;
    int pci_domain_id;
    int pci_bus_id;
    int pci_device_id;
    bool is_integrated;
    bool supports_p2p;
};

/**
 * Get device topology information
 * 
 * @param dev_id Device ID
 * @return DeviceTopology structure
 */
DeviceTopology get_device_topology(int dev_id);

/**
 * Check if two devices are on the same PCIe bus
 * 
 * @param dev_id1 First device ID
 * @param dev_id2 Second device ID
 * @return true if on same PCIe bus
 */
bool are_devices_on_same_bus(int dev_id1, int dev_id2);

/**
 * Check if two devices can communicate via P2P
 * 
 * @param dev_id1 First device ID
 * @param dev_id2 Second device ID
 * @return true if P2P communication is possible
 */
bool can_devices_communicate_p2p(int dev_id1, int dev_id2);

/**
 * Launch device-to-device copy kernel
 * 
 * @param dst Destination device pointer
 * @param src Source device pointer
 * @param total_size Total size to copy in bytes
 * @param src_dev_id Source device ID
 * @param dst_dev_id Destination device ID
 * @param stream CUDA stream for async execution
 * @return cudaSuccess on success
 */
cudaError_t launch_memcpy_d2d(
    void* dst,
    const void* src,
    size_t total_size,
    int src_dev_id,
    int dst_dev_id,
    cudaStream_t stream = 0
);

/**
 * Launch device-to-device copy using cudaMemcpyPeerAsync
 * 
 * @param dst Destination device pointer
 * @param dst_dev_id Destination device ID
 * @param src Source device pointer
 * @param src_dev_id Source device ID
 * @param total_size Total size to copy in bytes
 * @param stream CUDA stream for async execution
 * @return cudaSuccess on success
 */
cudaError_t launch_memcpy_peer_async(
    void* dst,
    int dst_dev_id,
    const void* src,
    int src_dev_id,
    size_t total_size,
    cudaStream_t stream = 0
);

} // namespace cuda
} // namespace pm_nixl
