#include <catch2/catch_test_macros.hpp>
#include "InterfaceGrouper.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 1) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    return p;
}

// Helper: add full AXI4 write+read channels (25 key signals) to a graph for an instance
static void addAxi4Ports(ConnectionGraph& graph, const std::string& inst,
                         const std::string& prefix, bool isMaster) {
    auto out = isMaster ? ArgumentDirection::Out : ArgumentDirection::In;
    auto in  = isMaster ? ArgumentDirection::In  : ArgumentDirection::Out;
    // AW channel
    graph.allPorts.push_back(makePort(inst, prefix + "awvalid", out));
    graph.allPorts.push_back(makePort(inst, prefix + "awready", in));
    graph.allPorts.push_back(makePort(inst, prefix + "awaddr",  out, 32));
    graph.allPorts.push_back(makePort(inst, prefix + "awlen",   out, 8));
    graph.allPorts.push_back(makePort(inst, prefix + "awsize",  out, 3));
    graph.allPorts.push_back(makePort(inst, prefix + "awburst", out, 2));
    // W channel
    graph.allPorts.push_back(makePort(inst, prefix + "wvalid", out));
    graph.allPorts.push_back(makePort(inst, prefix + "wready", in));
    graph.allPorts.push_back(makePort(inst, prefix + "wdata",  out, 64));
    graph.allPorts.push_back(makePort(inst, prefix + "wstrb",  out, 8));
    graph.allPorts.push_back(makePort(inst, prefix + "wlast",  out));
    // B channel
    graph.allPorts.push_back(makePort(inst, prefix + "bvalid", in));
    graph.allPorts.push_back(makePort(inst, prefix + "bready", out));
    graph.allPorts.push_back(makePort(inst, prefix + "bresp",  in, 2));
    // AR channel
    graph.allPorts.push_back(makePort(inst, prefix + "arvalid", out));
    graph.allPorts.push_back(makePort(inst, prefix + "arready", in));
    graph.allPorts.push_back(makePort(inst, prefix + "araddr",  out, 32));
    graph.allPorts.push_back(makePort(inst, prefix + "arlen",   out, 8));
    graph.allPorts.push_back(makePort(inst, prefix + "arsize",  out, 3));
    graph.allPorts.push_back(makePort(inst, prefix + "arburst", out, 2));
    // R channel
    graph.allPorts.push_back(makePort(inst, prefix + "rvalid", in));
    graph.allPorts.push_back(makePort(inst, prefix + "rready", out));
    graph.allPorts.push_back(makePort(inst, prefix + "rdata",  in, 64));
    graph.allPorts.push_back(makePort(inst, prefix + "rresp",  in, 2));
    graph.allPorts.push_back(makePort(inst, prefix + "rlast",  in));
}

TEST_CASE("InterfaceGrouper: detects AXI4 interface from matching suffixes") {
    ConnectionGraph graph;
    addAxi4Ports(graph, "top.u_master", "m_axi_", true);

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    REQUIRE(groups.size() == 1);
    CHECK(groups[0].protocol == "AXI4");
    CHECK(groups[0].instancePath == "top.u_master");
    CHECK(groups[0].matchedPorts.size() >= 25);
}

TEST_CASE("InterfaceGrouper: non-protocol ports are not grouped") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_core", "clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_core", "reset_n", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_core", "irq", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_core", "data_out", ArgumentDirection::Out, 32));

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    CHECK(groups.empty());
}

TEST_CASE("InterfaceGrouper: detects master role from VALID output direction") {
    ConnectionGraph graph;
    addAxi4Ports(graph, "top.u_master", "m_axi_", true);

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    REQUIRE(groups.size() == 1);
    CHECK(groups[0].role == "master");
}

TEST_CASE("InterfaceGrouper: detects slave role from VALID input direction") {
    ConnectionGraph graph;
    addAxi4Ports(graph, "top.u_slave", "s_axi_", false);

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    REQUIRE(groups.size() == 1);
    CHECK(groups[0].role == "slave");
}

TEST_CASE("InterfaceGrouper: IRQ port mixed with AXI is not included in group") {
    ConnectionGraph graph;
    addAxi4Ports(graph, "top.u_dma", "axi_", true);
    // Non-protocol port
    graph.allPorts.push_back(makePort("top.u_dma", "irq", ArgumentDirection::In));

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    REQUIRE(groups.size() == 1);
    // irq should NOT be in the matched ports
    for (const auto& port : groups[0].matchedPorts) {
        CHECK(port.portName != "irq");
    }
}

TEST_CASE("InterfaceGrouper: detects APB interface") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_apb", "apb_psel", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_penable", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_pwrite", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_paddr", ArgumentDirection::In, 32));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_pwdata", ArgumentDirection::In, 32));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_prdata", ArgumentDirection::Out, 32));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_pready", ArgumentDirection::Out));

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    REQUIRE(groups.size() == 1);
    CHECK(groups[0].protocol == "APB");
    CHECK(groups[0].instancePath == "top.u_apb");
    CHECK(groups[0].role == "slave");
}

TEST_CASE("InterfaceGrouper: prefix detection extracts common prefix") {
    ConnectionGraph graph;
    addAxi4Ports(graph, "top.u_master", "m_axi_", true);

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    REQUIRE(groups.size() == 1);
    CHECK(groups[0].prefix == "m_axi_");
}

TEST_CASE("InterfaceGrouper: AXI4 superset suppresses AXI4-Lite for same instance") {
    ConnectionGraph graph;
    addAxi4Ports(graph, "top.u_bus", "axi_", true);

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    // Should detect exactly one AXI4, not both AXI4 and AXI4-Lite
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].protocol == "AXI4");
}
