#include "pm_nixl/cuda/p2p_copy.hpp"
#include <iostream>
#include <cstring>

namespace pm_nixl {
namespace cuda {

bool is_peer_access_supported(int dev_id, int peer_id) {
    int can_access_peer = 0;
    cudaError_t err = cudaDeviceCanAccessPeer(&can_access_peer, dev_id, peer_id);
    
    if (err != cudaSuccess) {
        return false;
    }
    
    return can_access_peer != 0;
}

cudaError_t enable_peer_access(int dev_id, int peer_id) {
    // Save current device
    int current_dev = -1;
    cudaError_t err = cudaGetDevice(&current_dev);
    if (err != cudaSuccess) {
        return err;
    }
    
    // Set to source device
    err = cudaSetDevice(dev_id);
    if (err != cudaSuccess) {
        return err;
    }
    
    // Enable peer access
    err = cudaDeviceEnablePeerAccess(peer_id, 0);
    
    // Restore original device
    if (current_dev != -1) {
        cudaSetDevice(current_dev);
    }
    
    // cudaDeviceEnablePeerAccess returns cudaErrorPeerAccessAlreadyEnabled if already enabled
    if (err == cudaSuccess || err == cudaErrorPeerAccessAlreadyEnabled) {
        return cudaSuccess;
    }
    
    return err;
}

void disable_peer_access(int dev_id, int peer_id) {
    // Save current device
    int current_dev = -1;
    cudaError_t err = cudaGetDevice(&current_dev);
    if (err != cudaSuccess) {
        return;
    }
    
    // Set to source device
    err = cudaSetDevice(dev_id);
    if (err != cudaSuccess) {
        return;
    }
    
    // Disable peer access (ignore errors)
    cudaDeviceDisablePeerAccess(peer_id);
    
    // Restore original device
    if (current_dev != -1) {
        cudaSetDevice(current_dev);
    }
}

bool is_p2p_copy_supported(int dev_id, int peer_id) {
    // First check if peer access is supported
    if (!is_peer_access_supported(dev_id, peer_id)) {
        return false;
    }
    
    // Check if P2P copy is supported
    int p2p_supported = 0;
    cudaError_t err = cudaDeviceGetP2PAttribute(
        &p2p_supported,
        cudaDevP2PAttrPerformanceRank,
        dev_id,
        peer_id
    );
    
    if (err != cudaSuccess) {
        return false;
    }
    
    return p2p_supported > 0;
}

DeviceTopology get_device_topology(int dev_id) {
    DeviceTopology topo;
    std::memset(&topo, 0, sizeof(DeviceTopology));
    
    topo.device_id = dev_id;
    
    // Save current device
    int current_dev = -1;
    cudaError_t err = cudaGetDevice(&current_dev);
    if (err != cudaSuccess) {
        return topo;
    }
    
    // Set to target device
    err = cudaSetDevice(dev_id);
    if (err != cudaSuccess) {
        return topo;
    }
    
    // Get PCI bus ID
    char pci_bus_id_str[16];
    err = cudaDeviceGetPCIBusId(pci_bus_id_str, sizeof(pci_bus_id_str), dev_id);
    if (err == cudaSuccess) {
        // Parse PCI bus ID (format: BBBB:BB:DD.D)
        sscanf(pci_bus_id_str, "%x:%x:%x", 
               &topo.pci_domain_id, 
               &topo.pci_bus_id, 
               &topo.pci_device_id);
    }
    
    // Check if integrated
    int integrated = 0;
    cudaDeviceGetAttribute(&integrated, cudaDevAttrIntegrated, dev_id);
    topo.is_integrated = (integrated != 0);
    
    // Check P2P support
    int p2p_supported = 0;
    cudaDeviceGetAttribute(&p2p_supported, cudaDevAttrP2PAttributeSupported, dev_id);
    topo.supports_p2p = (p2p_supported != 0);
    
    // Restore original device
    if (current_dev != -1) {
        cudaSetDevice(current_dev);
    }
    
    return topo;
}

bool are_devices_on_same_bus(int dev_id1, int dev_id2) {
    DeviceTopology topo1 = get_device_topology(dev_id1);
    DeviceTopology topo2 = get_device_topology(dev_id2);
    
    // Same PCIe bus if domain and bus match
    return (topo1.pci_domain_id == topo2.pci_domain_id) &&
           (topo1.pci_bus_id == topo2.pci_bus_id);
}

bool can_devices_communicate_p2p(int dev_id1, int dev_id2) {
    if (dev_id1 == dev_id2) {
        return true;  // Same device
    }
    
    // Check if both devices support P2P
    DeviceTopology topo1 = get_device_topology(dev_id1);
    DeviceTopology topo2 = get_device_topology(dev_id2);
    
    if (!topo1.supports_p2p || !topo2.supports_p2p) {
        return false;
    }
    
    // Check if peer access is supported
    if (!is_peer_access_supported(dev_id1, dev_id2) ||
        !is_peer_access_supported(dev_id2, dev_id1)) {
        return false;
    }
    
    // Check P2P copy support
    return is_p2p_copy_supported(dev_id1, dev_id2) &&
           is_p2p_copy_supported(dev_id2, dev_id1);
}

cudaError_t launch_memcpy_d2d(
    void* dst,
    const void* src,
    size_t total_size,
    int src_dev_id,
    int dst_dev_id,
    cudaStream_t stream
) {
    if (src_dev_id == dst_dev_id) {
        // Same device - use regular cudaMemcpyAsync
        return cudaMemcpyAsync(dst, src, total_size, cudaMemcpyDeviceToDevice, stream);
    }
    
    // Different devices - use peer copy if available
    return launch_memcpy_peer_async(dst, dst_dev_id, src, src_dev_id, total_size, stream);
}

cudaError_t launch_memcpy_peer_async(
    void* dst,
    int dst_dev_id,
    const void* src,
    int src_dev_id,
    size_t total_size,
    cudaStream_t stream
) {
    if (src_dev_id == dst_dev_id) {
        // Same device
        return cudaMemcpyAsync(dst, src, total_size, cudaMemcpyDeviceToDevice, stream);
    }
    
    // Check if peer access is enabled
    if (!is_peer_access_supported(src_dev_id, dst_dev_id)) {
        return cudaErrorInvalidDevice;
    }
    
    // Use cudaMemcpyPeerAsync for P2P copy
    cudaError_t err = cudaMemcpyPeerAsync(dst, dst_dev_id, src, src_dev_id, total_size, stream);
    
    return err;
}

} // namespace cuda
} // namespace pm_nixl
