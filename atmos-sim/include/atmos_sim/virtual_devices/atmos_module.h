#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/virtual_devices/virtual_npu.h"
#include "atmos_sim/virtual_devices/hbf_controller.h"
#include "atmos_sim/virtual_devices/lpddr_controller.h"
#include "atmos_sim/virtual_devices/dma_engine.h"
#include "atmos_sim/virtual_devices/pcie_endpoint.h"
#include <memory>
#include <vector>
#include <string>

namespace atmos_sim {

/// Configuration for a complete ATMOS module.
struct AtmosModuleConfig {
    std::string name = "ATMOS_Module";
    VirtualNPUConfig npu_config;
    HBFConfig hbf_config;
    LPDDRConfig lpddr_config;
    DMAConfig dma_config;
    PCIeEndpointConfig pcie_config;
    uint32_t dma_engine_count = 2;
};

/// ATMOS Module — assembles NPU + HBF + LPDDR + DMA[] + PCIe into one module.
class AtmosModule {
public:
    explicit AtmosModule(ResourceId id, const AtmosModuleConfig& config);

    ResourceId id() const { return id_; }
    const std::string& name() const { return config_.name; }

    VirtualNPU& npu() { return *npu_; }
    HBFController& hbf() { return *hbf_; }
    LPDDRController& lpddr() { return *lpddr_; }
    DMAEngine& dma(uint32_t idx) { return *dma_engines_.at(idx); }
    PCIeEndpoint& pcie() { return *pcie_; }

    const VirtualNPU& npu() const { return *npu_; }
    const HBFController& hbf() const { return *hbf_; }
    const LPDDRController& lpddr() const { return *lpddr_; }
    const DMAEngine& dma(uint32_t idx) const { return *dma_engines_.at(idx); }
    const PCIeEndpoint& pcie() const { return *pcie_; }

    uint32_t dma_engine_count() const { return static_cast<uint32_t>(dma_engines_.size()); }

    /// Overall module utilization (weighted average of components).
    double utilization() const;

    /// Module-level summary.
    struct Summary {
        std::string name;
        double npu_utilization;
        double hbf_utilization;
        double lpddr_utilization;
        double dma_utilization;
        double pcie_utilization;
        uint64_t hbf_used_bytes;
        uint64_t lpddr_used_bytes;
    };
    Summary summary() const;

private:
    ResourceId id_;
    AtmosModuleConfig config_;
    std::unique_ptr<VirtualNPU> npu_;
    std::unique_ptr<HBFController> hbf_;
    std::unique_ptr<LPDDRController> lpddr_;
    std::vector<std::unique_ptr<DMAEngine>> dma_engines_;
    std::unique_ptr<PCIeEndpoint> pcie_;
};

} // namespace atmos_sim
