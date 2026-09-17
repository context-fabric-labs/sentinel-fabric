#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include "atmos_sim/sim_core/resource.h"
#include "atmos_sim/sim_core/bounded_queue.h"
#include "atmos_sim/topology/pcie_link.h"
#include <memory>
#include <string>

namespace atmos_sim {

/// Configuration for a PCIe endpoint.
struct PCIeEndpointConfig {
    std::string name = "PCIeEndpoint";
    PCIeGen gen = PCIeGen::GEN4;
    uint32_t lane_count = 8;
    uint32_t max_outstanding = 32;
    uint32_t credit_limit = 256;
    uint64_t packet_overhead_ns = 200;  // TLP header + framing
    uint32_t max_payload_size = 4096;   // Max payload per TLP
};

/// PCIe Endpoint — models a device-side PCIe interface with credit-based flow control.
class PCIeEndpoint {
public:
    explicit PCIeEndpoint(ResourceId id, const PCIeEndpointConfig& config);

    ResourceId id() const { return id_; }
    const std::string& name() const { return config_.name; }
    const PCIeEndpointConfig& config() const { return config_; }

    /// Effective bandwidth for this endpoint (lanes × per-lane BW).
    uint64_t effective_bandwidth() const;

    bool can_accept() const;

    /// Calculate transfer time for a given payload size.
    SimTime transfer_time(uint64_t payload_bytes) const;

    /// Consume credits. Returns false if insufficient.
    bool consume_credits(uint32_t count);

    /// Return credits.
    void return_credits(uint32_t count);

    uint32_t available_credits() const { return config_.credit_limit - credits_used_; }
    double utilization() const;

private:
    ResourceId id_;
    PCIeEndpointConfig config_;
    uint32_t credits_used_ = 0;
    uint64_t bytes_transferred_ = 0;
    SimTime total_transfer_time_ = 0;
};

} // namespace atmos_sim
