#include "atmos_sim/virtual_devices/atmos_module.h"

namespace atmos_sim {

AtmosModule::AtmosModule(ResourceId id, const AtmosModuleConfig& config)
    : id_(id), config_(config) {
    npu_ = std::make_unique<VirtualNPU>(id * 10 + 1, config.npu_config);
    hbf_ = std::make_unique<HBFController>(id * 10 + 2, config.hbf_config);
    lpddr_ = std::make_unique<LPDDRController>(id * 10 + 3, config.lpddr_config);
    pcie_ = std::make_unique<PCIeEndpoint>(id * 10 + 4, config.pcie_config);

    for (uint32_t i = 0; i < config.dma_engine_count; ++i) {
        dma_engines_.push_back(std::make_unique<DMAEngine>(id * 10 + 5 + i, config.dma_config));
    }
}

double AtmosModule::utilization() const {
    double total = npu_->utilization() + hbf_->utilization() +
                   lpddr_->utilization() + pcie_->utilization();
    for (const auto& dma : dma_engines_) {
        total += dma->utilization();
    }
    return total / (4 + dma_engines_.size());
}

AtmosModule::Summary AtmosModule::summary() const {
    return Summary{
        config_.name,
        npu_->utilization(),
        hbf_->utilization(),
        lpddr_->utilization(),
        dma_engines_.empty() ? 0.0 : dma_engines_[0]->utilization(),
        pcie_->utilization(),
        hbf_->used_bytes(),
        lpddr_->used_bytes(),
    };
}

} // namespace atmos_sim
