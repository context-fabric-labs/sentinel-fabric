#pragma once

#include "atmos_sim/topology/pcie_link.h"
#include "atmos_sim/topology/pcie_switch.h"
#include "atmos_sim/topology/root_complex.h"
#include "atmos_sim/sim_core/sim_time.h"

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace atmos_sim {

enum class TopologyNodeType : uint8_t {
    ROOT_COMPLEX,
    PCIE_SWITCH,
    PCIE_ENDPOINT,
    HOST_MEMORY,
};

/// A node in the topology graph.
struct TopologyNode {
    uint64_t id;
    std::string name;
    TopologyNodeType type;
    uint64_t device_index;  // Index into device array (for endpoint nodes)
};

/// A path through the topology (for P2P or device-to-host transfers).
struct TopologyPath {
    std::vector<uint64_t> node_ids;
    std::vector<uint64_t> link_ids;
    SimTime base_latency = 0;
    uint64_t bottleneck_bandwidth = 0;  // Min bandwidth along path
    uint32_t hop_count = 0;
};

/// Configuration for the complete topology.
struct TopologyConfig {
    std::string name = "default_topology";
};

/// Topology graph — models the complete OEM server PCIe topology with
/// root complexes, switches, links, and endpoints.
class TopologyGraph {
public:
    explicit TopologyGraph(const TopologyConfig& config = {});

    /// Add nodes and links.
    uint64_t add_root_complex(const RootComplexConfig& rc_config);
    uint64_t add_switch(const PCIeSwitchConfig& sw_config);
    uint64_t add_endpoint(const std::string& name, uint64_t device_index);
    uint64_t add_host_memory(const std::string& name);
    uint64_t add_link(const PCIeLinkConfig& link_config, uint64_t node_a, uint64_t node_b);

    /// Connect switch to root complex or endpoint.
    void connect_switch_to_rc(uint64_t switch_id, uint64_t rc_id, const PCIeLinkConfig& link);
    void connect_endpoint_to_switch(uint64_t endpoint_id, uint64_t switch_id, const PCIeLinkConfig& link);
    void connect_switch_to_switch(uint64_t sw_a, uint64_t sw_b, const PCIeLinkConfig& link);

    /// Find the path between two nodes.
    TopologyPath find_path(uint64_t from, uint64_t to) const;

    /// Calculate transfer time between two nodes for a given payload.
    SimTime transfer_time(uint64_t from, uint64_t to, uint64_t payload_bytes) const;

    /// Get node by ID.
    const TopologyNode& get_node(uint64_t id) const;
    const std::vector<TopologyNode>& nodes() const { return nodes_; }

    /// Get link by ID.
    const PCIeLink& get_link(uint64_t id) const;

    /// Get switch by ID.
    const PCIeSwitch& get_switch(uint64_t id) const;

    /// Get root complex by ID.
    const RootComplex& get_root_complex(uint64_t id) const;

    /// Node and link counts.
    size_t node_count() const { return nodes_.size(); }
    size_t link_count() const { return links_.size(); }

    /// Adjacent nodes.
    std::vector<uint64_t> neighbors(uint64_t node_id) const;

private:
    TopologyConfig config_;
    std::vector<TopologyNode> nodes_;
    std::vector<std::unique_ptr<PCIeLink>> links_;
    std::vector<std::unique_ptr<PCIeSwitch>> switches_;
    std::vector<std::unique_ptr<RootComplex>> root_complexes_;

    // Adjacency: node_id → list of (neighbor_id, link_id)
    std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, uint64_t>>> adjacency_;

    uint64_t next_node_id_ = 1;
    uint64_t next_link_id_ = 1;
};

} // namespace atmos_sim
