#include <pm_nixl/multi_gpu_transfer.hpp>
#include <pm_nixl/cuda/p2p_copy.hpp>
#include <gtest/gtest.h>

using namespace pm_nixl;

/**
 * Test: Check if multiple GPUs are available
 */
TEST(MultiGpuTest, CheckDeviceCount) {
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    
    if (err == cudaSuccess && device_count >= 2) {
        SUCCEED() << "Found " << device_count << " GPUs";
    } else {
        GTEST_SKIP() << "Less than 2 GPUs available (found " << device_count << ")";
    }
}

/**
 * Test: P2P support detection
 */
TEST(MultiGpuTest, DISABLED_PeerAccessSupport) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count < 2) {
        GTEST_SKIP() << "Less than 2 GPUs available";
    }
    
    bool supported = cuda::is_peer_access_supported(0, 1);
    
    if (supported) {
        SUCCEED() << "P2P access supported between GPU 0 and GPU 1";
    } else {
        GTEST_SKIP() << "P2P access not supported between GPU 0 and GPU 1";
    }
}

/**
 * Test: P2P copy support
 */
TEST(MultiGpuTest, DISABLED_P2PCopySupport) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count < 2) {
        GTEST_SKIP() << "Less than 2 GPUs available";
    }
    
    bool supported = cuda::is_p2p_copy_supported(0, 1);
    
    if (supported) {
        SUCCEED() << "P2P copy supported between GPU 0 and GPU 1";
    } else {
        GTEST_SKIP() << "P2P copy not supported between GPU 0 and GPU 1";
    }
}

/**
 * Test: Device topology detection
 */
TEST(MultiGpuTest, DISABLED_DeviceTopology) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count < 2) {
        GTEST_SKIP() << "Less than 2 GPUs available";
    }
    
    auto topo1 = cuda::get_device_topology(0);
    auto topo2 = cuda::get_device_topology(1);
    
    bool same_bus = cuda::are_devices_on_same_bus(0, 1);
    bool can_communicate = cuda::can_devices_communicate_p2p(0, 1);
    
    std::cout << "GPU 0: PCI " << topo1.pci_domain_id << ":" 
              << topo1.pci_bus_id << ":" << topo1.pci_device_id << std::endl;
    std::cout << "GPU 1: PCI " << topo2.pci_domain_id << ":" 
              << topo2.pci_bus_id << ":" << topo2.pci_device_id << std::endl;
    std::cout << "Same bus: " << (same_bus ? "Yes" : "No") << std::endl;
    std::cout << "Can communicate P2P: " << (can_communicate ? "Yes" : "No") << std::endl;
    
    SUCCEED();
}

/**
 * Test: Multi-GPU engine construction
 */
TEST(MultiGpuTest, EngineConstruction) {
    MultiGpuConfig config;
    config.device_ids = {0, 1};
    config.enable_p2p = false;  // Don't enable P2P for this test
    
    MultiGpuTransferEngine engine(config);
    
    EXPECT_EQ(engine.device_count(), 2);
    EXPECT_FALSE(engine.is_initialized());
}

/**
 * Test: Multi-GPU engine initialization
 */
TEST(MultiGpuTest, DISABLED_EngineInitialization) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count < 2) {
        GTEST_SKIP() << "Less than 2 GPUs available";
    }
    
    MultiGpuConfig config;
    config.device_ids = {0, 1};
    config.enable_p2p = true;
    
    MultiGpuTransferEngine engine(config);
    
    auto result = engine.initialize();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(engine.is_initialized());
    EXPECT_EQ(engine.device_count(), 2);
    
    // Print topology
    engine.print_topology();
    
    result = engine.shutdown();
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(engine.is_initialized());
}

/**
 * Test: D2D transfer (disabled without GPU)
 */
TEST(MultiGpuTest, DISABLED_D2DTransfer) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count < 2) {
        GTEST_SKIP() << "Less than 2 GPUs available";
    }
    
    MultiGpuConfig config;
    config.device_ids = {0, 1};
    config.enable_p2p = true;
    
    MultiGpuTransferEngine engine(config);
    engine.initialize();
    
    // Create buffers on different devices
    const size_t size = 1024 * 1024;  // 1MB
    Buffer src(MemoryKind::CudaDevice, size, 0);
    Buffer dst(MemoryKind::CudaDevice, size, 1);
    
    // Initialize source
    std::vector<char> h_src(size, 0xAB);
    cudaMemcpy(src.ptr(), h_src.data(), size, cudaMemcpyHostToDevice);
    
    // Submit D2D transfer
    auto result = engine.submit_d2d(dst.descriptor(), src.descriptor(), size);
    
    if (result.has_value()) {
        // Wait for completion (simplified)
        cudaDeviceSynchronize();
        
        // Verify destination
        std::vector<char> h_dst(size);
        cudaMemcpy(h_dst.data(), dst.ptr(), size, cudaMemcpyDeviceToHost);
        
        for (size_t i = 0; i < size; ++i) {
            EXPECT_EQ(h_dst[i], 0xAB);
        }
    } else {
        GTEST_SKIP() << "D2D transfer not supported";
    }
    
    engine.shutdown();
}

/**
 * Test: Topology detection
 */
TEST(MultiGpuTest, DISABLED_TopologyDetection) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count < 2) {
        GTEST_SKIP() << "Less than 2 GPUs available";
    }
    
    MultiGpuConfig config;
    config.device_ids = {0, 1};
    config.auto_detect_topology = true;
    
    MultiGpuTransferEngine engine(config);
    engine.initialize();
    
    const auto* topo = engine.get_topology(0, 1);
    
    if (topo != nullptr) {
        std::cout << "Device pair 0-1:" << std::endl;
        std::cout << "  P2P supported: " << (topo->p2p_supported ? "Yes" : "No") << std::endl;
        std::cout << "  Same bus: " << (topo->same_bus ? "Yes" : "No") << std::endl;
        std::cout << "  Performance rank: " << topo->performance_rank << std::endl;
        
        SUCCEED();
    } else {
        FAIL() << "Topology not found";
    }
    
    engine.shutdown();
}

/**
 * Test: P2P enable/disable
 */
TEST(MultiGpuTest, DISABLED_P2PEnableDisable) {
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    
    if (device_count < 2) {
        GTEST_SKIP() << "Less than 2 GPUs available";
    }
    
    if (!cuda::can_devices_communicate_p2p(0, 1)) {
        GTEST_SKIP() << "P2P not supported between GPU 0 and 1";
    }
    
    MultiGpuConfig config;
    config.device_ids = {0, 1};
    config.enable_p2p = true;
    
    MultiGpuTransferEngine engine(config);
    
    // Enable P2P
    auto result = engine.initialize();
    EXPECT_TRUE(result.has_value());
    
    // Verify P2P is enabled
    EXPECT_TRUE(engine.is_p2p_supported(0, 1));
    
    // Disable P2P
    result = engine.shutdown();
    EXPECT_TRUE(result.has_value());
}
