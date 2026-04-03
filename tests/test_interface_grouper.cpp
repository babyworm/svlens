#include <catch2/catch_test_macros.hpp>
#include "InterfaceGrouper.h"
#include "TestUtils.h"

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

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

// ---------- Part A: Missing protocol tests ----------

TEST_CASE("InterfaceGrouper: detects AXI4-Lite interface (no LEN/SIZE/BURST/LAST)") {
    ConnectionGraph graph;
    std::string inst = "top.u_axilite";
    std::string pfx = "s_axil_";
    // AXI4-Lite has 17 key signals — same as AXI4 but without LEN, SIZE, BURST, LAST
    auto in  = ArgumentDirection::In;   // slave: VALID comes in
    auto out = ArgumentDirection::Out;
    // AW channel (3 signals, no LEN/SIZE/BURST)
    graph.allPorts.push_back(makePort(inst, pfx + "awvalid", in));
    graph.allPorts.push_back(makePort(inst, pfx + "awready", out));
    graph.allPorts.push_back(makePort(inst, pfx + "awaddr",  in, 32));
    // W channel (4 signals, no WLAST)
    graph.allPorts.push_back(makePort(inst, pfx + "wvalid", in));
    graph.allPorts.push_back(makePort(inst, pfx + "wready", out));
    graph.allPorts.push_back(makePort(inst, pfx + "wdata",  in, 32));
    graph.allPorts.push_back(makePort(inst, pfx + "wstrb",  in, 4));
    // B channel (3 signals)
    graph.allPorts.push_back(makePort(inst, pfx + "bvalid", out));
    graph.allPorts.push_back(makePort(inst, pfx + "bready", in));
    graph.allPorts.push_back(makePort(inst, pfx + "bresp",  out, 2));
    // AR channel (3 signals, no LEN/SIZE/BURST)
    graph.allPorts.push_back(makePort(inst, pfx + "arvalid", in));
    graph.allPorts.push_back(makePort(inst, pfx + "arready", out));
    graph.allPorts.push_back(makePort(inst, pfx + "araddr",  in, 32));
    // R channel (4 signals, no RLAST)
    graph.allPorts.push_back(makePort(inst, pfx + "rvalid", out));
    graph.allPorts.push_back(makePort(inst, pfx + "rready", in));
    graph.allPorts.push_back(makePort(inst, pfx + "rdata",  out, 32));
    graph.allPorts.push_back(makePort(inst, pfx + "rresp",  out, 2));

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    // Should detect AXI4-Lite, NOT AXI4 (only 17/25 = 68% of AXI4 signals,
    // but 17/17 = 100% of AXI4-Lite signals).
    // However, AXI4 at 68% > 60% threshold also matches, and superset rule
    // only suppresses AXI4-Lite when AXI4 matched. So we may get both or just AXI4.
    // Let's check what actually happens — the key is AXI4-Lite should be detected.
    bool foundLite = false;
    for (const auto& g : groups) {
        if (g.protocol == "AXI4-Lite") {
            foundLite = true;
            CHECK(g.instancePath == inst);
            CHECK(g.role == "slave");
            CHECK(g.matchedPorts.size() >= 17);
        }
    }
    CHECK(foundLite);
}

TEST_CASE("InterfaceGrouper: detects AXI-Stream interface") {
    ConnectionGraph graph;
    std::string inst = "top.u_stream";
    // AXI-Stream key signals: TVALID, TREADY, TDATA, TLAST, TKEEP
    // Master: TVALID is Out
    graph.allPorts.push_back(makePort(inst, "s_axis_tvalid", ArgumentDirection::Out));
    graph.allPorts.push_back(makePort(inst, "s_axis_tready", ArgumentDirection::In));
    graph.allPorts.push_back(makePort(inst, "s_axis_tdata",  ArgumentDirection::Out, 64));
    graph.allPorts.push_back(makePort(inst, "s_axis_tlast",  ArgumentDirection::Out));
    graph.allPorts.push_back(makePort(inst, "s_axis_tkeep",  ArgumentDirection::Out, 8));

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    // Should detect AXI-Stream
    bool foundStream = false;
    for (const auto& g : groups) {
        if (g.protocol == "AXI-Stream") {
            foundStream = true;
            CHECK(g.instancePath == inst);
            CHECK(g.role == "master"); // TVALID is Out
            CHECK(g.matchedPorts.size() >= 3);
        }
    }
    CHECK(foundStream);
}

TEST_CASE("InterfaceGrouper: detects AHB interface") {
    ConnectionGraph graph;
    std::string inst = "top.u_ahb_slave";
    std::string pfx = "ahb_";
    // AHB key signals: HSEL, HADDR, HTRANS, HWRITE, HSIZE, HBURST, HWDATA, HRDATA, HREADY, HRESP
    // Slave perspective: most inputs come In, responses go Out
    graph.allPorts.push_back(makePort(inst, pfx + "hsel",   ArgumentDirection::In));
    graph.allPorts.push_back(makePort(inst, pfx + "haddr",  ArgumentDirection::In, 32));
    graph.allPorts.push_back(makePort(inst, pfx + "htrans", ArgumentDirection::In, 2));
    graph.allPorts.push_back(makePort(inst, pfx + "hwrite", ArgumentDirection::In));
    graph.allPorts.push_back(makePort(inst, pfx + "hsize",  ArgumentDirection::In, 3));
    graph.allPorts.push_back(makePort(inst, pfx + "hburst", ArgumentDirection::In, 3));
    graph.allPorts.push_back(makePort(inst, pfx + "hwdata", ArgumentDirection::In, 32));
    graph.allPorts.push_back(makePort(inst, pfx + "hrdata", ArgumentDirection::Out, 32));
    graph.allPorts.push_back(makePort(inst, pfx + "hready", ArgumentDirection::In));
    graph.allPorts.push_back(makePort(inst, pfx + "hresp",  ArgumentDirection::Out));

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    bool foundAHB = false;
    for (const auto& g : groups) {
        if (g.protocol == "AHB") {
            foundAHB = true;
            CHECK(g.instancePath == inst);
            // AHB role detection uses HTRANS as valid signal.
            // HTRANS is In here => slave
            CHECK(g.role == "slave");
            CHECK(g.matchedPorts.size() >= 10);
        }
    }
    CHECK(foundAHB);
}

TEST_CASE("InterfaceGrouper: 60% threshold boundary — exactly at threshold detects") {
    ConnectionGraph graph;
    std::string inst = "top.u_partial_axi";
    std::string pfx = "axi_";
    // AXI4 has 25 key signals. 60% of 25 = 15.0, so exactly 15 signals should match.
    // Include AXI4-exclusive signals (LEN, SIZE, BURST, LAST) so the superset rule
    // keeps AXI4 over AXI4-Lite (AXI4 ratio = 15/25 = 60%, AXI4-Lite ratio must be lower).
    // AXI4-Lite has 17 signals — overlap with our 15 will be at most 11 (15 - 4 exclusive) = 64.7%.
    // Actually we need AXI4-Lite ratio < AXI4 ratio (60%). So we want at most 10/17 = 58.8% Lite overlap.
    // Strategy: pick signals that maximize AXI4-exclusive content.
    auto out = ArgumentDirection::Out;
    auto in  = ArgumentDirection::In;
    // AW channel (6): all 6 — 3 are AXI4-exclusive (AWLEN, AWSIZE, AWBURST)
    graph.allPorts.push_back(makePort(inst, pfx + "awvalid", out));
    graph.allPorts.push_back(makePort(inst, pfx + "awready", in));
    graph.allPorts.push_back(makePort(inst, pfx + "awaddr",  out, 32));
    graph.allPorts.push_back(makePort(inst, pfx + "awlen",   out, 8));
    graph.allPorts.push_back(makePort(inst, pfx + "awsize",  out, 3));
    graph.allPorts.push_back(makePort(inst, pfx + "awburst", out, 2));
    // W channel (5): all 5 — WLAST is AXI4-exclusive
    graph.allPorts.push_back(makePort(inst, pfx + "wvalid", out));
    graph.allPorts.push_back(makePort(inst, pfx + "wready", in));
    graph.allPorts.push_back(makePort(inst, pfx + "wdata",  out, 64));
    graph.allPorts.push_back(makePort(inst, pfx + "wstrb",  out, 8));
    graph.allPorts.push_back(makePort(inst, pfx + "wlast",  out));
    // AR channel (3 of 6): ARLEN, ARSIZE, ARBURST — all 3 are AXI4-exclusive
    graph.allPorts.push_back(makePort(inst, pfx + "arlen",   out, 8));
    graph.allPorts.push_back(makePort(inst, pfx + "arsize",  out, 3));
    graph.allPorts.push_back(makePort(inst, pfx + "arburst", out, 2));
    // R channel (1): RLAST — AXI4-exclusive
    graph.allPorts.push_back(makePort(inst, pfx + "rlast", in));
    // Total: 15 AXI4 signals = 60.0%
    // AXI4-Lite overlap: AWVALID,AWREADY,AWADDR,WVALID,WREADY,WDATA,WSTRB = 7/17 = 41.2%
    // So AXI4 (60%) > AXI4-Lite (41.2%) — superset rule keeps AXI4.

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    bool foundAXI4 = false;
    for (const auto& g : groups) {
        if (g.protocol == "AXI4" && g.instancePath == inst) {
            foundAXI4 = true;
        }
    }
    CHECK(foundAXI4);
}

TEST_CASE("InterfaceGrouper: 60% threshold boundary — below threshold does not detect") {
    ConnectionGraph graph;
    std::string inst = "top.u_insufficient";
    std::string pfx = "axi_";
    // 14 of 25 AXI4 signals = 56%, below 60% threshold
    auto out = ArgumentDirection::Out;
    auto in  = ArgumentDirection::In;
    // AW channel: all 6
    graph.allPorts.push_back(makePort(inst, pfx + "awvalid", out));
    graph.allPorts.push_back(makePort(inst, pfx + "awready", in));
    graph.allPorts.push_back(makePort(inst, pfx + "awaddr",  out, 32));
    graph.allPorts.push_back(makePort(inst, pfx + "awlen",   out, 8));
    graph.allPorts.push_back(makePort(inst, pfx + "awsize",  out, 3));
    graph.allPorts.push_back(makePort(inst, pfx + "awburst", out, 2));
    // W channel: all 5
    graph.allPorts.push_back(makePort(inst, pfx + "wvalid", out));
    graph.allPorts.push_back(makePort(inst, pfx + "wready", in));
    graph.allPorts.push_back(makePort(inst, pfx + "wdata",  out, 64));
    graph.allPorts.push_back(makePort(inst, pfx + "wstrb",  out, 8));
    graph.allPorts.push_back(makePort(inst, pfx + "wlast",  out));
    // B channel: 3 signals
    graph.allPorts.push_back(makePort(inst, pfx + "bvalid", in));
    graph.allPorts.push_back(makePort(inst, pfx + "bready", out));
    graph.allPorts.push_back(makePort(inst, pfx + "bresp",  in, 2));
    // Total: 14 signals = 56% — no AR/R channel signals

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    // Should NOT detect AXI4 for this instance
    bool foundAXI4 = false;
    for (const auto& g : groups) {
        if (g.protocol == "AXI4" && g.instancePath == inst) {
            foundAXI4 = true;
        }
    }
    CHECK_FALSE(foundAXI4);
}

TEST_CASE("InterfaceGrouper: multiple protocols on same design — AXI4 and APB") {
    ConnectionGraph graph;

    // Instance 1: AXI4 master
    addAxi4Ports(graph, "top.u_dma", "m_axi_", true);

    // Instance 2: APB slave
    std::string apbInst = "top.u_uart";
    graph.allPorts.push_back(makePort(apbInst, "apb_psel",    ArgumentDirection::In));
    graph.allPorts.push_back(makePort(apbInst, "apb_penable", ArgumentDirection::In));
    graph.allPorts.push_back(makePort(apbInst, "apb_pwrite",  ArgumentDirection::In));
    graph.allPorts.push_back(makePort(apbInst, "apb_paddr",   ArgumentDirection::In, 12));
    graph.allPorts.push_back(makePort(apbInst, "apb_pwdata",  ArgumentDirection::In, 32));
    graph.allPorts.push_back(makePort(apbInst, "apb_prdata",  ArgumentDirection::Out, 32));
    graph.allPorts.push_back(makePort(apbInst, "apb_pready",  ArgumentDirection::Out));

    InterfaceGrouper grouper;
    auto groups = grouper.classify(graph);

    bool foundAXI4 = false;
    bool foundAPB  = false;
    for (const auto& g : groups) {
        if (g.protocol == "AXI4" && g.instancePath == "top.u_dma") {
            foundAXI4 = true;
            CHECK(g.role == "master");
        }
        if (g.protocol == "APB" && g.instancePath == "top.u_uart") {
            foundAPB = true;
            CHECK(g.role == "slave");
        }
    }
    CHECK(foundAXI4);
    CHECK(foundAPB);
}
