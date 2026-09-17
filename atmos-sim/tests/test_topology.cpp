#include <gtest/gtest.h>
#include "atmos_sim/topology/topology_graph.h"
#include "atmos_sim/topology/pcie_link.h"
#include "atmos_sim/topology/pcie_switch.h"
#include "atmos_sim/topology/root_complex.h"

using namespace atmos_sim;

// ==================== PCIeLink Tests ====================

TEST(PCIeLinkTest, AutoBandwidth) {
    PCIeLinkConfig config;
    config.generation = 4;
    config.lane_count = 8;
    config.bandwidth_bytes_sec = 0;  // Auto
    PCIeLink link(1, config);

    uint64_t expected = pcie_lane_bandwidth(PCIeGen::GEN4) * 8;
    EXPECT_EQ(link.effective_bandwidth(), expected);
}

TEST(PCIeLinkTest, ExplicitBandwidth) {
    PCIeLinkConfig config;
    config.bandwidth_bytes_sec = 10e9;
    config.latency_ns = 100;
    PCIeLink link(1, config);

    EXPECT_EQ(link.effective_bandwidth(), 10'000'000'000ULL);

    // Transfer time includes base latency
    SimTime time = link.transfer_time(1000);
    EXPECT_GT(time, 100u);  // At least the base latency
}

// ==================== PCIeSwitch Tests ====================

TEST(PCIeSwitchTest, DownstreamPorts) {
    PCIeSwitchConfig config;
    config.port_count = 8;
    PCIeSwitch sw(1, config);

    sw.add_downstream_port(10);
    sw.add_downstream_port(11);
    EXPECT_EQ(sw.downstream_ports().size(), 2u);
    EXPECT_TRUE(sw.can_route());
}

TEST(PCIeSwitchTest, TransitLatency) {
    PCIeSwitchConfig config;
    config.switch_latency_ns = 200;
    PCIeSwitch sw(1, config);

    // Single port: just base latency
    SimTime t1 = sw.transit_latency(1);
    EXPECT_EQ(t1, 200u);

    // Two ports: additional contention
    SimTime t2 = sw.transit_latency(2);
    EXPECT_GT(t2, t1);
}

// ==================== RootComplex Tests ====================

TEST(RootComplexTest, TransferTime) {
    RootComplexConfig config;
    config.aggregate_bandwidth_bytes_sec = 1000;  // 1000 B/s for clean math
    RootComplex rc(1, config);

    // Single endpoint: 1000 bytes at 1000 B/s = 1 ns
    SimTime t1 = rc.transfer_time(1000, 1);
    EXPECT_EQ(t1, 1u);

    // Two endpoints: each gets 500 B/s → 1000/500 = 2 ns
    SimTime t2 = rc.transfer_time(1000, 2);
    EXPECT_EQ(t2, 2u);
    EXPECT_GT(t2, t1);
}

// ==================== TopologyGraph Tests ====================

class TopologyGraphTest : public ::testing::Test {
protected:
    // Build a simple topology: RC → Switch → 2 endpoints
    void SetUp() override {
        TopologyConfig config;
        config.name = "test_topology";
        topology = std::make_unique<TopologyGraph>(config);

        RootComplexConfig rc_config;
        rc_config.name = "RC0";
        rc_config.aggregate_bandwidth_bytes_sec = 64e9;
        rc_id = topology->add_root_complex(rc_config);

        PCIeSwitchConfig sw_config;
        sw_config.name = "SW0";
        sw_config.switch_latency_ns = 200;
        sw_id = topology->add_switch(sw_config);

        ep0_id = topology->add_endpoint("ATMOS_0", 0);
        ep1_id = topology->add_endpoint("ATMOS_1", 1);

        PCIeLinkConfig link_config;
        link_config.generation = 4;
        link_config.lane_count = 8;
        link_config.latency_ns = 100;
        link_config.bandwidth_bytes_sec = 1000;  // 1000 B/s for clean math

        topology->connect_switch_to_rc(sw_id, rc_id, link_config);
        topology->connect_endpoint_to_switch(ep0_id, sw_id, link_config);
        topology->connect_endpoint_to_switch(ep1_id, sw_id, link_config);
    }

    std::unique_ptr<TopologyGraph> topology;
    uint64_t rc_id, sw_id, ep0_id, ep1_id;
};

TEST_F(TopologyGraphTest, NodeCount) {
    EXPECT_EQ(topology->node_count(), 4u);  // RC, SW, 2 endpoints
}

TEST_F(TopologyGraphTest, PathFinding) {
    // Path from ep0 to ep1: ep0 → SW → ep1
    auto path = topology->find_path(ep0_id, ep1_id);
    EXPECT_EQ(path.hop_count, 2u);
    EXPECT_FALSE(path.node_ids.empty());
    EXPECT_GT(path.bottleneck_bandwidth, 0u);
}

TEST_F(TopologyGraphTest, PathThroughRC) {
    // Path from ep0 to RC
    auto path = topology->find_path(ep0_id, rc_id);
    EXPECT_EQ(path.hop_count, 2u);  // ep0 → SW → RC
}

TEST_F(TopologyGraphTest, TransferTime) {
    SimTime time = topology->transfer_time(ep0_id, ep1_id, 4096);
    EXPECT_GT(time, 0u);

    // Much larger payload should take longer (latency is fixed, BW dominates)
    SimTime time_large = topology->transfer_time(ep0_id, ep1_id, 100 * 1024 * 1024);
    EXPECT_GT(time_large, time);
}

TEST_F(TopologyGraphTest, NonexistentPath) {
    // Add isolated endpoint
    uint64_t isolated = topology->add_endpoint("ISOLATED", 99);
    auto path = topology->find_path(ep0_id, isolated);
    EXPECT_TRUE(path.node_ids.empty());
}

TEST_F(TopologyGraphTest, Neighbors) {
    auto sw_neighbors = topology->neighbors(sw_id);
    EXPECT_EQ(sw_neighbors.size(), 3u);  // RC + 2 endpoints
}

// ==================== Multi-RC Topology Test ====================

TEST(TopologyGraphMultiRCTest, MultiRCTopology) {
    TopologyGraph topo;

    // Two root complexes, each with a switch
    RootComplexConfig rc0_config, rc1_config;
    rc0_config.name = "RC0";
    rc1_config.name = "RC1";
    auto rc0 = topo.add_root_complex(rc0_config);
    auto rc1 = topo.add_root_complex(rc1_config);

    PCIeSwitchConfig sw0_config, sw1_config;
    sw0_config.name = "SW0";
    sw1_config.name = "SW1";
    auto sw0 = topo.add_switch(sw0_config);
    auto sw1 = topo.add_switch(sw1_config);

    auto ep0 = topo.add_endpoint("ATMOS_0", 0);
    auto ep1 = topo.add_endpoint("ATMOS_1", 1);

    PCIeLinkConfig link;
    link.generation = 4;
    link.lane_count = 8;
    link.latency_ns = 100;

    topo.connect_switch_to_rc(sw0, rc0, link);
    topo.connect_switch_to_rc(sw1, rc1, link);
    topo.connect_endpoint_to_switch(ep0, sw0, link);
    topo.connect_endpoint_to_switch(ep1, sw1, link);

    // Cross-RC path: ep0 → sw0 → rc0 → ? → rc1 → sw1 → ep1
    // Without inter-RC link, no path exists
    auto path = topo.find_path(ep0, ep1);
    EXPECT_TRUE(path.node_ids.empty());

    // Add inter-RC link
    PCIeLinkConfig inter_link;
    inter_link.generation = 4;
    inter_link.lane_count = 16;
    inter_link.latency_ns = 500;
    topo.add_link(inter_link, rc0, rc1);

    auto path2 = topo.find_path(ep0, ep1);
    EXPECT_FALSE(path2.node_ids.empty());
    EXPECT_EQ(path2.hop_count, 5u);  // ep0 → sw0 → rc0 → rc1 → sw1 → ep1 (5 links)
}
