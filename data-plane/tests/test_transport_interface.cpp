#include <pm_nixl/cuda_transport.hpp>
#include <pm_nixl/transport_interface.hpp>
#include <pm_nixl/buffer.hpp>
#include <gtest/gtest.h>

using namespace pm_nixl;

/**
 * Test: Transport capabilities
 */
TEST(TransportInterfaceTest, TransportCapabilities) {
    CudaTransport transport;
    
    TransportCaps caps = transport.capabilities();
    
    // CUDA transport should support H2D and D2H
    EXPECT_TRUE(has_cap(caps, TransportCaps::HostToDevice));
    EXPECT_TRUE(has_cap(caps, TransportCaps::DeviceToHost));
    EXPECT_TRUE(has_cap(caps, TransportCaps::Async));
}

/**
 * Test: Transport name
 */
TEST(TransportInterfaceTest, TransportName) {
    CudaTransport transport;
    EXPECT_EQ(transport.name(), "CUDA");
}

/**
 * Test: Transport initialization
 */
TEST(TransportInterfaceTest, DISABLED_TransportInitialization) {
    CudaTransport transport;
    
    auto result = transport.initialize();
    EXPECT_TRUE(result.has_value());
    
    result = transport.shutdown();
    EXPECT_TRUE(result.has_value());
}

/**
 * Test: Transport statistics
 */
TEST(TransportInterfaceTest, TransportStats) {
    CudaTransport transport;
    
    TransportStats stats = transport.get_stats();
    EXPECT_EQ(stats.bytes_transferred, 0);
    EXPECT_EQ(stats.operations_completed, 0);
    EXPECT_EQ(stats.operations_failed, 0);
    
    transport.reset_stats();
    
    stats = transport.get_stats();
    EXPECT_EQ(stats.bytes_transferred, 0);
}

/**
 * Test: Transport registry
 */
TEST(TransportInterfaceTest, TransportRegistry) {
    // Register CUDA transport
    TransportRegistry::register_transport("cuda", create_cuda_transport);
    
    // Get available transports
    auto transports = TransportRegistry::get_available_transports();
    EXPECT_FALSE(transports.empty());
    
    // Create CUDA transport
    auto result = TransportRegistry::create("cuda");
    EXPECT_TRUE(result.has_value());
    
    if (result.has_value()) {
        auto transport = std::move(result.value());
        EXPECT_EQ(transport->name(), "CUDA");
    }
}

/**
 * Test: Transport registry - unknown transport
 */
TEST(TransportInterfaceTest, TransportRegistryUnknown) {
    auto result = TransportRegistry::create("unknown_transport");
    EXPECT_FALSE(result.has_value());
}

/**
 * Test: CUDA transport submit (disabled without GPU)
 */
TEST(TransportInterfaceTest, DISABLED_CudaTransportSubmit) {
    CudaTransport transport;
    transport.initialize();
    
    // Create buffers
    Buffer src(MemoryKind::HostPinned, 1024);
    Buffer dst(MemoryKind::CudaDevice, 1024);
    
    // Fill source
    std::memset(src.ptr(), 0xAB, 1024);
    
    // Submit transfer
    auto result = transport.submit(dst.descriptor(), src.descriptor(), 1024);
    
    if (result.has_value()) {
        transport.synchronize();
        
        // Verify
        std::vector<char> h_dst(1024);
        cudaMemcpy(h_dst.data(), dst.ptr(), 1024, cudaMemcpyDeviceToHost);
        
        for (size_t i = 0; i < 1024; ++i) {
            EXPECT_EQ(h_dst[i], 0xAB);
        }
    }
    
    transport.shutdown();
}

/**
 * Test: Transport statistics update
 */
TEST(TransportInterfaceTest, DISABLED_TransportStatsUpdate) {
    CudaTransport transport;
    transport.initialize();
    
    // Submit some transfers
    for (int i = 0; i < 10; ++i) {
        Buffer src(MemoryKind::HostPinned, 1024);
        Buffer dst(MemoryKind::CudaDevice, 1024);
        
        auto result = transport.submit(dst.descriptor(), src.descriptor(), 1024);
        if (result.has_value()) {
            transport.synchronize();
        }
    }
    
    TransportStats stats = transport.get_stats();
    EXPECT_EQ(stats.operations_completed, 10);
    EXPECT_GT(stats.bytes_transferred, 0);
    
    transport.shutdown();
}
