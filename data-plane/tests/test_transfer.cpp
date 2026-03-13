#include <pm_nixl/buffer.hpp>
#include <pm_nixl/transfer_engine.hpp>
#include <pm_nixl/error.hpp>
#include <gtest/gtest.h>
#include <cstring>
#include <cuda_runtime.h>

using namespace pm_nixl;

/**
 * Test: Transfer descriptor validation
 */
TEST(TransferTest, DescriptorValidation) {
    Buffer host(MemoryKind::HostPinned, 1024);
    Buffer device(MemoryKind::CudaDevice, 1024, 0);
    
    // Valid H2D transfer
    TransferDescriptor h2d(host.descriptor(), device.descriptor(), 512);
    EXPECT_TRUE(h2d.is_valid());
    EXPECT_EQ(h2d.type, TransferType::HostToDevice);
    
    // Valid D2H transfer
    TransferDescriptor d2h(device.descriptor(), host.descriptor(), 512);
    EXPECT_TRUE(d2h.is_valid());
    EXPECT_EQ(d2h.type, TransferType::DeviceToHost);
    
    // Invalid: size too large
    TransferDescriptor invalid(host.descriptor(), device.descriptor(), 2048);
    EXPECT_FALSE(invalid.is_valid());
    
    // Invalid: zero size
    TransferDescriptor zero(host.descriptor(), device.descriptor(), 0);
    EXPECT_FALSE(zero.is_valid());
}

/**
 * Test: Transfer type inference
 */
TEST(TransferTest, TransferTypeInference) {
    Buffer host(MemoryKind::HostPinned, 1024);
    Buffer device(MemoryKind::CudaDevice, 1024, 0);
    
    auto type1 = infer_transfer_type(host.descriptor(), device.descriptor());
    EXPECT_EQ(type1, TransferType::HostToDevice);
    
    auto type2 = infer_transfer_type(device.descriptor(), host.descriptor());
    EXPECT_EQ(type2, TransferType::DeviceToHost);
    
    Buffer device2(MemoryKind::CudaDevice, 1024, 0);
    auto type3 = infer_transfer_type(device.descriptor(), device2.descriptor());
    EXPECT_EQ(type3, TransferType::DeviceToDevice);
}

/**
 * Test: Completion default construction
 */
TEST(TransferTest, CompletionDefault) {
    Completion completion;
    EXPECT_FALSE(completion.is_valid());
    EXPECT_EQ(completion.operation_id(), 0);
    EXPECT_FALSE(completion.is_complete());
}

/**
 * Test: Completion construction
 */
TEST(TransferTest, CompletionConstruction) {
    Completion completion(123);
    EXPECT_TRUE(completion.is_valid());
    EXPECT_EQ(completion.operation_id(), 123);
    EXPECT_FALSE(completion.is_complete());
}

/**
 * Test: Completion move semantics
 */
TEST(TransferTest, CompletionMove) {
    Completion original(456);
    Completion moved(std::move(original));
    
    EXPECT_TRUE(moved.is_valid());
    EXPECT_EQ(moved.operation_id(), 456);
    EXPECT_FALSE(original.is_valid());
}

/**
 * Test: Transfer engine construction
 */
TEST(TransferTest, EngineConstruction) {
    TransferEngineConfig config;
    config.device_id = 0;
    config.num_streams = 4;
    
    TransferEngine engine(config);
    EXPECT_FALSE(engine.is_initialized());
    EXPECT_EQ(engine.device_id(), 0);
    EXPECT_EQ(engine.num_streams(), 4);
}

/**
 * Test: Transfer engine initialization
 * 
 * Note: This test will fail if CUDA is not available.
 * Marked as DISABLED for systems without GPU.
 */
TEST(TransferTest, DISABLED_EngineInitialization) {
    TransferEngineConfig config;
    TransferEngine engine(config);
    
    auto result = engine.initialize();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(engine.is_initialized());
    
    result = engine.shutdown();
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(engine.is_initialized());
}

/**
 * Test: Transfer engine submit (disabled without CUDA)
 */
TEST(TransferTest, DISABLED_EngineSubmit) {
    TransferEngineConfig config;
    TransferEngine engine(config);
    
    engine.initialize();
    
    Buffer host(MemoryKind::HostPinned, 1024);
    Buffer device(MemoryKind::CudaDevice, 1024, 0);
    
    // Fill host buffer with data
    std::memset(host.ptr(), 0xAB, 1024);
    
    // Submit H2D transfer
    auto result = engine.submit_h2d(host.descriptor(), device.descriptor(), 1024);
    EXPECT_TRUE(result.has_value());
    
    if (result.has_value()) {
        Completion completion = result.value();
        EXPECT_TRUE(completion.is_valid());
        
        // Wait for completion
        auto wait_result = engine.wait(completion);
        EXPECT_TRUE(wait_result.has_value());
        EXPECT_TRUE(completion.is_complete());
    }
    
    engine.shutdown();
}

/**
 * Test: Transfer engine D2H (disabled without CUDA)
 */
TEST(TransferTest, DISABLED_EngineD2H) {
    TransferEngineConfig config;
    TransferEngine engine(config);
    engine.initialize();
    
    Buffer host(MemoryKind::HostPinned, 1024);
    Buffer device(MemoryKind::CudaDevice, 1024, 0);
    
    // Fill host buffer with pattern
    std::memset(host.ptr(), 0xCD, 1024);
    
    // H2D
    auto h2d = engine.submit_h2d(host.descriptor(), device.descriptor(), 1024);
    if (h2d.has_value()) {
        engine.wait(h2d.value());
    }
    
    // Clear host buffer
    std::memset(host.ptr(), 0x00, 1024);
    
    // D2H
    auto d2h = engine.submit_d2h(device.descriptor(), host.descriptor(), 1024);
    if (d2h.has_value()) {
        engine.wait(d2h.value());
    }
    
    // Verify data transferred correctly
    unsigned char* data = static_cast<unsigned char*>(host.ptr());
    for (size_t i = 0; i < 1024; ++i) {
        EXPECT_EQ(data[i], 0xCD);
    }
    
    engine.shutdown();
}

/**
 * Test: Multiple streams
 */
TEST(TransferTest, DISABLED_MultipleStreams) {
    TransferEngineConfig config;
    config.num_streams = 8;
    
    TransferEngine engine(config);
    engine.initialize();
    
    EXPECT_EQ(engine.num_streams(), 8);
    
    // Submit multiple transfers
    std::vector<Completion> completions;
    for (int i = 0; i < 10; ++i) {
        Buffer host(MemoryKind::HostPinned, 256);
        Buffer device(MemoryKind::CudaDevice, 256, 0);
        
        auto result = engine.submit_h2d(host.descriptor(), device.descriptor(), 256);
        if (result.has_value()) {
            completions.push_back(std::move(result.value()));
        }
    }
    
    EXPECT_EQ(engine.pending_count(), completions.size());
    
    // Wait for all
    for (auto& completion : completions) {
        engine.wait(completion);
    }
    
    EXPECT_EQ(engine.pending_count(), 0);
    
    engine.shutdown();
}

/**
 * Test: Synchronize all
 */
TEST(TransferTest, DISABLED_Synchronize) {
    TransferEngineConfig config;
    TransferEngine engine(config);
    engine.initialize();
    
    // Submit several transfers
    for (int i = 0; i < 5; ++i) {
        Buffer host(MemoryKind::HostPinned, 512);
        Buffer device(MemoryKind::CudaDevice, 512, 0);
        engine.submit_h2d(host.descriptor(), device.descriptor(), 512);
    }
    
    EXPECT_GT(engine.pending_count(), 0);
    
    // Synchronize
    engine.synchronize();
    
    EXPECT_EQ(engine.pending_count(), 0);
    
    engine.shutdown();
}
