#include <pm_nixl/buffer.hpp>
#include <pm_nixl/descriptor.hpp>
#include <pm_nixl/error.hpp>
#include <gtest/gtest.h>
#include <cstring>
#include <cuda_runtime.h>

using namespace pm_nixl;

/**
 * Test: Default constructor creates invalid buffer
 */
TEST(BufferTest, DefaultConstructor) {
    Buffer buffer;
    EXPECT_FALSE(buffer.is_valid());
    EXPECT_EQ(buffer.ptr(), nullptr);
    EXPECT_EQ(buffer.size(), 0);
}

/**
 * Test: Host pinned buffer creation
 */
TEST(BufferTest, HostPinnedBuffer) {
    const size_t size = 1024;
    Buffer buffer(MemoryKind::HostPinned, size);
    
    EXPECT_TRUE(buffer.is_valid());
    EXPECT_NE(buffer.ptr(), nullptr);
    EXPECT_EQ(buffer.size(), size);
    EXPECT_EQ(buffer.kind(), MemoryKind::HostPinned);
    EXPECT_TRUE(buffer.is_registered());
    EXPECT_GT(buffer.registration_id(), 0);
    
    // Check alignment
    EXPECT_TRUE(buffer.descriptor().is_aligned());
}

/**
 * Test: Host pageable buffer creation
 */
TEST(BufferTest, HostPageableBuffer) {
    const size_t size = 2048;
    Buffer buffer(MemoryKind::HostPageable, size);
    
    EXPECT_TRUE(buffer.is_valid());
    EXPECT_NE(buffer.ptr(), nullptr);
    EXPECT_EQ(buffer.size(), size);
    EXPECT_EQ(buffer.kind(), MemoryKind::HostPageable);
    EXPECT_TRUE(buffer.is_registered());
}

/**
 * Test: Device buffer creation
 */
TEST(BufferTest, DeviceBuffer) {
    const size_t size = 4096;
    Buffer buffer(MemoryKind::CudaDevice, size, 0);
    
    EXPECT_TRUE(buffer.is_valid());
    EXPECT_NE(buffer.ptr(), nullptr);
    EXPECT_EQ(buffer.size(), size);
    EXPECT_EQ(buffer.kind(), MemoryKind::CudaDevice);
    EXPECT_EQ(buffer.device_id(), 0);
    EXPECT_TRUE(buffer.is_registered());
}

/**
 * Test: Helper functions
 */
TEST(BufferTest, HelperFunctions) {
    auto pinned = create_host_pinned_buffer(512);
    EXPECT_TRUE(pinned.is_valid());
    EXPECT_EQ(pinned.kind(), MemoryKind::HostPinned);
    
    auto pageable = create_host_pageable_buffer(512);
    EXPECT_TRUE(pageable.is_valid());
    EXPECT_EQ(pageable.kind(), MemoryKind::HostPageable);
    
    auto device = create_device_buffer(512, 0);
    EXPECT_TRUE(device.is_valid());
    EXPECT_EQ(device.kind(), MemoryKind::CudaDevice);
}

/**
 * Test: Move constructor
 */
TEST(BufferTest, MoveConstructor) {
    Buffer original(MemoryKind::HostPinned, 1024);
    uint64_t original_id = original.registration_id();
    void* original_ptr = original.ptr();
    
    Buffer moved(std::move(original));
    
    EXPECT_TRUE(moved.is_valid());
    EXPECT_EQ(moved.registration_id(), original_id);
    EXPECT_EQ(moved.ptr(), original_ptr);
    
    // Original should be invalid after move
    EXPECT_FALSE(original.is_valid());
    EXPECT_EQ(original.ptr(), nullptr);
}

/**
 * Test: Move assignment
 */
TEST(BufferTest, MoveAssignment) {
    Buffer source(MemoryKind::HostPinned, 1024);
    uint64_t source_id = source.registration_id();
    void* source_ptr = source.ptr();
    
    Buffer dest;
    dest = std::move(source);
    
    EXPECT_TRUE(dest.is_valid());
    EXPECT_EQ(dest.registration_id(), source_id);
    EXPECT_EQ(dest.ptr(), source_ptr);
    
    // Source should be invalid after move
    EXPECT_FALSE(source.is_valid());
}

/**
 * Test: Swap
 */
TEST(BufferTest, Swap) {
    Buffer buffer1(MemoryKind::HostPinned, 1024);
    Buffer buffer2(MemoryKind::CudaDevice, 2048, 0);
    
    uint64_t id1 = buffer1.registration_id();
    uint64_t id2 = buffer2.registration_id();
    void* ptr1 = buffer1.ptr();
    void* ptr2 = buffer2.ptr();
    
    buffer1.swap(buffer2);
    
    EXPECT_EQ(buffer1.registration_id(), id2);
    EXPECT_EQ(buffer1.ptr(), ptr2);
    EXPECT_EQ(buffer1.kind(), MemoryKind::CudaDevice);
    
    EXPECT_EQ(buffer2.registration_id(), id1);
    EXPECT_EQ(buffer2.ptr(), ptr1);
    EXPECT_EQ(buffer2.kind(), MemoryKind::HostPinned);
}

/**
 * Test: Descriptor validity
 */
TEST(BufferTest, DescriptorValidity) {
    Buffer buffer(MemoryKind::HostPinned, 1024);
    auto desc = buffer.descriptor();
    
    EXPECT_TRUE(validate_descriptor(desc));
    EXPECT_TRUE(desc.is_valid());
    EXPECT_TRUE(desc.is_aligned());
}

/**
 * Test: Memory kind utilities
 */
TEST(BufferTest, MemoryKindUtilities) {
    EXPECT_TRUE(is_host_memory(MemoryKind::HostPinned));
    EXPECT_TRUE(is_host_memory(MemoryKind::HostPageable));
    EXPECT_FALSE(is_host_memory(MemoryKind::CudaDevice));
    
    EXPECT_FALSE(is_device_memory(MemoryKind::HostPinned));
    EXPECT_FALSE(is_device_memory(MemoryKind::HostPageable));
    EXPECT_TRUE(is_device_memory(MemoryKind::CudaDevice));
    
    EXPECT_STREQ(memory_kind_to_string(MemoryKind::HostPinned), "HostPinned");
    EXPECT_STREQ(memory_kind_to_string(MemoryKind::HostPageable), "HostPageable");
    EXPECT_STREQ(memory_kind_to_string(MemoryKind::CudaDevice), "CudaDevice");
}

/**
 * Test: Registration ID uniqueness
 */
TEST(BufferTest, RegistrationIdUniqueness) {
    RegistrationIdGenerator::reset();
    
    Buffer buffer1(MemoryKind::HostPinned, 100);
    Buffer buffer2(MemoryKind::HostPinned, 100);
    Buffer buffer3(MemoryKind::HostPinned, 100);
    
    EXPECT_NE(buffer1.registration_id(), buffer2.registration_id());
    EXPECT_NE(buffer2.registration_id(), buffer3.registration_id());
    EXPECT_NE(buffer1.registration_id(), buffer3.registration_id());
}

/**
 * Test: Zero size buffer
 */
TEST(BufferTest, ZeroSizeBuffer) {
    Buffer buffer(MemoryKind::HostPinned, 0);
    EXPECT_FALSE(buffer.is_valid());
}

/**
 * Test: Large buffer
 */
TEST(BufferTest, LargeBuffer) {
    const size_t size = 100 * 1024 * 1024;  // 100 MB
    Buffer buffer(MemoryKind::HostPinned, size);
    
    EXPECT_TRUE(buffer.is_valid());
    EXPECT_EQ(buffer.size(), size);
}

/**
 * Test: Descriptor alignment
 */
TEST(BufferTest, DescriptorAlignment) {
    Buffer pinned(MemoryKind::HostPinned, 1024);
    Buffer pageable(MemoryKind::HostPageable, 1024);
    Buffer device(MemoryKind::CudaDevice, 1024, 0);
    
    EXPECT_TRUE(pinned.descriptor().is_aligned());
    EXPECT_TRUE(pageable.descriptor().is_aligned());
    EXPECT_TRUE(device.descriptor().is_aligned());
    
    // Check alignment values
    EXPECT_GE(pinned.descriptor().alignment, 4096);
    EXPECT_GE(device.descriptor().alignment, 512);
}
