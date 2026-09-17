#include "atmos_sim/topology/topology_graph.h"
#include <queue>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace atmos_sim {

TopologyGraph::TopologyGraph(const TopologyConfig& config)
    : config_(config) {}

uint64_t TopologyGraph::add_root_complex(const RootComplexConfig& rc_config) {
    uint64_t id = next_node_id_++;
    root_complexes_.push_back(std::make_unique<RootComplex>(id, rc_config));
    nodes_.push_back({id, rc_config.name, TopologyNodeType::ROOT_COMPLEX, 0});
    return id;
}

uint64_t TopologyGraph::add_switch(const PCIeSwitchConfig& sw_config) {
    uint64_t id = next_node_id_++;
    switches_.push_back(std::make_unique<PCIeSwitch>(id, sw_config));
    nodes_.push_back({id, sw_config.name, TopologyNodeType::PCIE_SWITCH, 0});
    return id;
}

uint64_t TopologyGraph::add_endpoint(const std::string& name, uint64_t device_index) {
    uint64_t id = next_node_id_++;
    nodes_.push_back({id, name, TopologyNodeType::PCIE_ENDPOINT, device_index});
    return id;
}

uint64_t TopologyGraph::add_host_memory(const std::string& name) {
    uint64_t id = next_node_id_++;
    nodes_.push_back({id, name, TopologyNodeType::HOST_MEMORY, 0});
    return id;
}

uint64_t TopologyGraph::add_link(const PCIeLinkConfig& link_config, uint64_t node_a, uint64_t node_b) {
    uint64_t id = next_link_id_++;
    auto link = std::make_unique<PCIeLink>(id, link_config);
    link->set_endpoints(node_a, node_b);
    links_.push_back(std::move(link));
    adjacency_[node_a].push_back({node_b, id});
    adjacency_[node_b].push_back({node_a, id});
    return id;
}

void TopologyGraph::connect_switch_to_rc(uint64_t switch_id, uint64_t rc_id, const PCIeLinkConfig& link_config) {
    add_link(link_config, switch_id, rc_id);
    for (auto& sw : switches_) {
        if (sw->id() == switch_id) {
            sw->set_upstream_port(rc_id);
            break;
        }
    }
    for (auto& rc : root_complexes_) {
        if (rc->id() == rc_id) {
            rc->add_downstream(switch_id);
            break;
        }
    }
}

void TopologyGraph::connect_endpoint_to_switch(uint64_t endpoint_id, uint64_t switch_id, const PCIeLinkConfig& link_config) {
    add_link(link_config, endpoint_id, switch_id);
    for (auto& sw : switches_) {
        if (sw->id() == switch_id) {
            sw->add_downstream_port(endpoint_id);
            break;
        }
    }
}

void TopologyGraph::connect_switch_to_switch(uint64_t sw_a, uint64_t sw_b, const PCIeLinkConfig& link_config) {
    add_link(link_config, sw_a, sw_b);
}

TopologyPath TopologyGraph::find_path(uint64_t from, uint64_t to) const {
    // BFS to find shortest path
    std::queue<uint64_t> q;
    std::unordered_map<uint64_t, uint64_t> parent_node;
    std::unordered_map<uint64_t, uint64_t> parent_link;
    std::unordered_set<uint64_t> visited;

    q.push(from);
    visited.insert(from);

    while (!q.empty()) {
        uint64_t current = q.front();
        q.pop();

        if (current == to) break;

        auto adj_it = adjacency_.find(current);
        if (adj_it == adjacency_.end()) continue;

        for (const auto& [neighbor, link_id] : adj_it->second) {
            if (!visited.count(neighbor)) {
                visited.insert(neighbor);
                parent_node[neighbor] = current;
                parent_link[neighbor] = link_id;
                q.push(neighbor);
            }
        }
    }

    // Reconstruct path
    TopologyPath path;
    if (!visited.count(to)) return path;  // No path found

    uint64_t current = to;
    while (current != from) {
        path.node_ids.push_back(current);
        path.link_ids.push_back(parent_link[current]);
        current = parent_node[current];
    }
    path.node_ids.push_back(from);

    std::reverse(path.node_ids.begin(), path.node_ids.end());
    std::reverse(path.link_ids.begin(), path.link_ids.end());

    // Calculate base latency and bottleneck bandwidth
    path.hop_count = static_cast<uint32_t>(path.link_ids.size());
    path.bottleneck_bandwidth = std::numeric_limits<uint64_t>::max();

    for (uint64_t link_id : path.link_ids) {
        for (const auto& link : links_) {
            if (link->id() == link_id) {
                path.base_latency += link->config().latency_ns;
                path.bottleneck_bandwidth = std::min(path.bottleneck_bandwidth, link->effective_bandwidth());
                break;
            }
        }
    }

    if (path.bottleneck_bandwidth == std::numeric_limits<uint64_t>::max()) {
        path.bottleneck_bandwidth = 0;
    }

    return path;
}

SimTime TopologyGraph::transfer_time(uint64_t from, uint64_t to, uint64_t payload_bytes) const {
    TopologyPath path = find_path(from, to);
    if (path.node_ids.empty()) return SIM_TIME_MAX;

    SimTime latency = ns_to_sim(path.base_latency);
    SimTime transfer = transfer_time_bytes(payload_bytes, path.bottleneck_bandwidth);
    return latency + transfer;
}

const TopologyNode& TopologyGraph::get_node(uint64_t id) const {
    for (const auto& node : nodes_) {
        if (node.id == id) return node;
    }
    throw std::runtime_error("TopologyGraph: node not found");
}

const PCIeLink& TopologyGraph::get_link(uint64_t id) const {
    for (const auto& link : links_) {
        if (link->id() == id) return *link;
    }
    throw std::runtime_error("TopologyGraph: link not found");
}

const PCIeSwitch& TopologyGraph::get_switch(uint64_t id) const {
    for (const auto& sw : switches_) {
        if (sw->id() == id) return *sw;
    }
    throw std::runtime_error("TopologyGraph: switch not found");
}

const RootComplex& TopologyGraph::get_root_complex(uint64_t id) const {
    for (const auto& rc : root_complexes_) {
        if (rc->id() == id) return *rc;
    }
    throw std::runtime_error("TopologyGraph: root complex not found");
}

std::vector<uint64_t> TopologyGraph::neighbors(uint64_t node_id) const {
    std::vector<uint64_t> result;
    auto it = adjacency_.find(node_id);
    if (it != adjacency_.end()) {
        for (const auto& [neighbor, _] : it->second) {
            result.push_back(neighbor);
        }
    }
    return result;
}

} // namespace atmos_sim
