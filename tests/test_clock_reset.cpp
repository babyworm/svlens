#include <catch2/catch_test_macros.hpp>
#include "ClockResetAnalyzer.h"

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

// -----------------------------------------------------------------------
// Port classification unit tests
// -----------------------------------------------------------------------

TEST_CASE("ClockResetAnalyzer: isClockPort classifies clock names", "[ClockReset]") {
    // Exact matches
    CHECK(ClockResetAnalyzer::isClockPort("clk"));
    CHECK(ClockResetAnalyzer::isClockPort("clock"));
    CHECK(ClockResetAnalyzer::isClockPort("CLK"));
    CHECK(ClockResetAnalyzer::isClockPort("CLOCK"));

    // Contains _clk or clk_
    CHECK(ClockResetAnalyzer::isClockPort("sys_clk"));
    CHECK(ClockResetAnalyzer::isClockPort("peri_clk"));
    CHECK(ClockResetAnalyzer::isClockPort("clk_200mhz"));
    CHECK(ClockResetAnalyzer::isClockPort("axi_clock"));
    CHECK(ClockResetAnalyzer::isClockPort("clock_gen"));

    // Not clocks
    CHECK_FALSE(ClockResetAnalyzer::isClockPort("data"));
    CHECK_FALSE(ClockResetAnalyzer::isClockPort("i_data"));
    CHECK_FALSE(ClockResetAnalyzer::isClockPort("rst_n"));
}

TEST_CASE("ClockResetAnalyzer: isResetPort classifies reset names", "[ClockReset]") {
    // Exact matches
    CHECK(ClockResetAnalyzer::isResetPort("rst"));
    CHECK(ClockResetAnalyzer::isResetPort("rst_n"));
    CHECK(ClockResetAnalyzer::isResetPort("reset"));
    CHECK(ClockResetAnalyzer::isResetPort("reset_n"));
    CHECK(ClockResetAnalyzer::isResetPort("RST_N"));

    // Contains _rst or rst_
    CHECK(ClockResetAnalyzer::isResetPort("sys_rst_n"));
    CHECK(ClockResetAnalyzer::isResetPort("peri_rst_n"));
    CHECK(ClockResetAnalyzer::isResetPort("rst_sync"));
    CHECK(ClockResetAnalyzer::isResetPort("axi_reset"));
    CHECK(ClockResetAnalyzer::isResetPort("reset_gen"));

    // Not resets
    CHECK_FALSE(ClockResetAnalyzer::isResetPort("data"));
    CHECK_FALSE(ClockResetAnalyzer::isResetPort("clk"));
    CHECK_FALSE(ClockResetAnalyzer::isResetPort("o_result"));
}

// -----------------------------------------------------------------------
// Topology analysis tests (unit test data, no slang compilation)
// -----------------------------------------------------------------------

TEST_CASE("ClockResetAnalyzer: groups clock ports by name", "[ClockReset]") {
    ConnectionGraph graph;
    graph.topModule = "clk_rst_top";

    // Two instances on sys_clk
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_norst", "sys_clk", ArgumentDirection::In));
    // One instance on peri_clk
    graph.allPorts.push_back(makePort("clk_rst_top.u_periph", "peri_clk", ArgumentDirection::In));
    // Reset ports (so u_core and u_periph don't warn)
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_rst_n", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_periph", "peri_rst_n", ArgumentDirection::In));
    // Data ports (should be ignored)
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "i_data", ArgumentDirection::In, 32));
    // Top-module port (should be skipped)
    graph.allPorts.push_back(makePort("clk_rst_top", "sys_clk", ArgumentDirection::In));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    REQUIRE(topo.clockGroups.size() == 2);
    CHECK(topo.clockGroups.count("sys_clk") == 1);
    CHECK(topo.clockGroups.at("sys_clk").size() == 2);
    CHECK(topo.clockGroups.count("peri_clk") == 1);
    CHECK(topo.clockGroups.at("peri_clk").size() == 1);
}

TEST_CASE("ClockResetAnalyzer: groups reset ports by name", "[ClockReset]") {
    ConnectionGraph graph;
    graph.topModule = "clk_rst_top";

    // Clock ports (needed so instances exist)
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_periph", "peri_clk", ArgumentDirection::In));
    // Reset ports
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_rst_n", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_periph", "peri_rst_n", ArgumentDirection::In));
    // Top-module reset port (should be skipped)
    graph.allPorts.push_back(makePort("clk_rst_top", "sys_rst_n", ArgumentDirection::In));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    REQUIRE(topo.resetGroups.size() == 2);
    CHECK(topo.resetGroups.count("sys_rst_n") == 1);
    CHECK(topo.resetGroups.at("sys_rst_n").size() == 1);
    CHECK(topo.resetGroups.count("peri_rst_n") == 1);
    CHECK(topo.resetGroups.at("peri_rst_n").size() == 1);
}

TEST_CASE("ClockResetAnalyzer: warns on instance with clock but no reset", "[ClockReset]") {
    ConnectionGraph graph;
    graph.topModule = "clk_rst_top";

    // u_core has clock and reset -- no warning
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_rst_n", ArgumentDirection::In));

    // u_norst has clock but NO reset -- should warn
    graph.allPorts.push_back(makePort("clk_rst_top.u_norst", "clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_norst", "i_val", ArgumentDirection::In, 4));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    REQUIRE(topo.warnings.size() == 1);
    CHECK(topo.warnings[0] == "clk_rst_top.u_norst");
}

TEST_CASE("ClockResetAnalyzer: output ports are not classified", "[ClockReset]") {
    ConnectionGraph graph;
    graph.topModule = "top";

    // Output port named clk -- should NOT be classified as clock
    graph.allPorts.push_back(makePort("top.u_pll", "clk", ArgumentDirection::Out));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    CHECK(topo.clockGroups.empty());
    CHECK(topo.resetGroups.empty());
    CHECK(topo.warnings.empty());
}

TEST_CASE("ClockResetAnalyzer: top-module ports are skipped", "[ClockReset]") {
    ConnectionGraph graph;
    graph.topModule = "chip_top";

    // These are top-module ports, should be skipped entirely
    graph.allPorts.push_back(makePort("chip_top", "sys_clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("chip_top", "sys_rst_n", ArgumentDirection::In));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    CHECK(topo.clockGroups.empty());
    CHECK(topo.resetGroups.empty());
    CHECK(topo.warnings.empty());
}
