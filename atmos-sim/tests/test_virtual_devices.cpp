#include <gtest/gtest.h>
#include "atmos_sim/virtual_devices/virtual_npu.h"
#include "atmos_sim/virtual_devices/hbf_controller.h"
#include "atmos_sim/virtual_devices/lpddr_controller.h"
#include "atmos_sim/virtual_devices/dma_engine.h"
#include "atmos_sim/virtual_devices/pcie_endpoint.h"
#include "atmos_sim/virtual_devices/atmos_module.h"

using namespace atmos_sim;

// ==================== VirtualNPU Tests ====================

class VirtualNPUTest : public ::testing::Test {
protected:
    void SetUp() override {
        VirtualNPUConfig config;
        config.command_queue_depth = 16;
        config.tensor_engine_count = 4;
        config.vector_engine_count = 4;
        config.load_store_engine_count = 2;
        config.local_sram_bytes = 256 * 1024 * 1024;
        config.execution_slots = 8;
        config.peak_flops = 500'000'000'000ULL;
        npu = std::make_unique<VirtualNPU>(1, config);
    }
    std::unique_ptr<VirtualNPU> npu;
};

TEST_F(VirtualNPUTest, InitialState) {
    EXPECT_TRUE(npu->can_accept());
    EXPECT_EQ(npu->available_sram(), 256ULL * 1024 * 1024);
    EXPECT_EQ(npu->command_queue().available(), 16u);
}

TEST_F(VirtualNPUTest, SRAMAllocation) {
    uint64_t alloc_size = 100 * 1024 * 1024;
    EXPECT_TRUE(npu->allocate_sram(alloc_size));
    EXPECT_EQ(npu->available_sram(), 156ULL * 1024 * 1024);

    npu->release_sram(alloc_size);
    EXPECT_EQ(npu->available_sram(), 256ULL * 1024 * 1024);
}

TEST_F(VirtualNPUTest, SRAMOverflow) {
    uint64_t too_large = 300ULL * 1024 * 1024;
    EXPECT_FALSE(npu->allocate_sram(too_large));
}

TEST_F(VirtualNPUTest, ComputeTimeEstimation) {
    ComputeOp op;
    op.flops = 500'000'000'000ULL;  // 500 GFLOPS
    SimTime time = npu->estimate_compute_time(op);
    EXPECT_EQ(time, 1'000'000'000u);  // 1 second in ns

    op.flops = 250'000'000'000ULL;  // 250 GFLOPS
    time = npu->estimate_compute_time(op);
    EXPECT_EQ(time, 500'000'000u);  // 0.5 seconds in ns
}

TEST_F(VirtualNPUTest, ResourcePoolDepletion) {
    // Exhaust tensor engines
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(npu->tensor_engines().acquire());
    }
    EXPECT_FALSE(npu->tensor_engines().is_available());

    // Release one
    EXPECT_TRUE(npu->tensor_engines().release());
    EXPECT_TRUE(npu->tensor_engines().is_available());
}

// ==================== HBFController Tests ====================

class HBFControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        HBFConfig config;
        config.channel_count = 4;
        config.capacity_bytes = 128ULL * 1024 * 1024 * 1024;
        config.bandwidth_per_channel_bytes_sec = 14ULL * 1000 * 1000 * 1000;
        config.queue_depth_per_channel = 32;
        config.max_outstanding = 64;
        config.protocol_overhead_ns = 500;
        hbf = std::make_unique<HBFController>(1, config);
    }
    std::unique_ptr<HBFController> hbf;
};

TEST_F(HBFControllerTest, InitialState) {
    EXPECT_TRUE(hbf->can_accept());
    EXPECT_EQ(hbf->outstanding(), 0u);
    EXPECT_EQ(hbf->total_bandwidth(), 56'000'000'000ULL);
}

TEST_F(HBFControllerTest, ReadTransaction) {
    SimTime time = hbf->submit_read(0, 4096, 1);
    EXPECT_GT(time, 0u);
    EXPECT_EQ(hbf->outstanding(), 1u);
}

TEST_F(HBFControllerTest, StorageAllocation) {
    uint64_t alloc = 64ULL * 1024 * 1024 * 1024;
    EXPECT_TRUE(hbf->allocate_storage(alloc));
    EXPECT_EQ(hbf->used_bytes(), alloc);
}

TEST_F(HBFControllerTest, StorageOverflow) {
    uint64_t too_large = 200ULL * 1024 * 1024 * 1024;
    EXPECT_FALSE(hbf->allocate_storage(too_large));
}

// ==================== LPDDRController Tests ====================

class LPDDRControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        LPDDRConfig config;
        config.channel_count = 4;
        config.capacity_bytes = 32ULL * 1024 * 1024 * 1024;
        config.bandwidth_per_channel_bytes_sec = 6ULL * 1000 * 1000 * 1000;
        lpddr = std::make_unique<LPDDRController>(1, config);
    }
    std::unique_ptr<LPDDRController> lpddr;
};

TEST_F(LPDDRControllerTest, ReadWriteTurnaround) {
    // First read — no turnaround
    SimTime t1 = lpddr->submit_read(0, 1024, 1);
    // Write after read — turnaround penalty
    SimTime t2 = lpddr->submit_write(0, 1024, 2);
    // Second write — no turnaround (already in write mode)
    SimTime t3 = lpddr->submit_write(0, 1024, 3);

    EXPECT_GT(t2, t1);  // Write has turnaround penalty after read
}

TEST_F(LPDDRControllerTest, TotalBandwidth) {
    EXPECT_EQ(lpddr->total_bandwidth(), 24'000'000'000ULL);
}

// ==================== DMAEngine Tests ====================

class DMAEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        DMAConfig config;
        config.descriptor_queue_depth = 32;
        config.max_outstanding = 8;
        config.channel_count = 2;
        config.setup_overhead_ns = 200;
        config.bandwidth_bytes_sec = 32ULL * 1000 * 1000 * 1000;
        dma = std::make_unique<DMAEngine>(1, config);
    }
    std::unique_ptr<DMAEngine> dma;
};

TEST_F(DMAEngineTest, TransferSubmit) {
    DMATransfer xfer;
    xfer.size_bytes = 4096;

    SimTime time = dma->submit_transfer(xfer);
    EXPECT_GT(time, 0u);
    EXPECT_EQ(dma->outstanding(), 1u);
}

TEST_F(DMAEngineTest, SetupOverheadIncluded) {
    DMATransfer xfer;
    xfer.size_bytes = 0;  // Zero transfer, only setup

    SimTime time = dma->submit_transfer(xfer);
    EXPECT_GE(time, 200u);  // At least the setup overhead
}

// ==================== PCIeEndpoint Tests ====================

class PCIeEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        PCIeEndpointConfig config;
        config.gen = PCIeGen::GEN4;
        config.lane_count = 8;
        config.credit_limit = 256;
        config.packet_overhead_ns = 200;
        config.max_payload_size = 4096;
        ep = std::make_unique<PCIeEndpoint>(1, config);
    }
    std::unique_ptr<PCIeEndpoint> ep;
};

TEST_F(PCIeEndpointTest, EffectiveBandwidth) {
    uint64_t expected = pcie_lane_bandwidth(PCIeGen::GEN4) * 8;
    EXPECT_EQ(ep->effective_bandwidth(), expected);
}

TEST_F(PCIeEndpointTest, TransferTime) {
    SimTime time = ep->transfer_time(4096);
    EXPECT_GT(time, 0u);
}

TEST_F(PCIeEndpointTest, CreditManagement) {
    EXPECT_TRUE(ep->consume_credits(100));
    EXPECT_EQ(ep->available_credits(), 156u);

    EXPECT_TRUE(ep->consume_credits(156));
    EXPECT_EQ(ep->available_credits(), 0u);

    EXPECT_FALSE(ep->consume_credits(1));  // No credits

    ep->return_credits(50);
    EXPECT_EQ(ep->available_credits(), 50u);
}

// ==================== AtmosModule Tests ====================

class AtmosModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        AtmosModuleConfig config;
        config.name = "TestModule";
        config.dma_engine_count = 2;
        config.npu_config.peak_flops = 500'000'000'000ULL;
        config.hbf_config.capacity_bytes = 128ULL * 1024 * 1024 * 1024;
        config.lpddr_config.capacity_bytes = 32ULL * 1024 * 1024 * 1024;
        module = std::make_unique<AtmosModule>(1, config);
    }
    std::unique_ptr<AtmosModule> module;
};

TEST_F(AtmosModuleTest, ComponentAccess) {
    EXPECT_EQ(module->name(), "TestModule");
    EXPECT_EQ(module->dma_engine_count(), 2u);
    EXPECT_TRUE(module->npu().can_accept());
    EXPECT_TRUE(module->hbf().can_accept());
    EXPECT_TRUE(module->lpddr().can_accept());
}

TEST_F(AtmosModuleTest, Summary) {
    auto s = module->summary();
    EXPECT_EQ(s.name, "TestModule");
    EXPECT_EQ(s.hbf_used_bytes, 0u);
    EXPECT_EQ(s.lpddr_used_bytes, 0u);
}
