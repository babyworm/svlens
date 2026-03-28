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

TEST_CASE("ClockResetAnalyzer: InOut direction port with clock name is not classified", "[ClockReset]") {
    ConnectionGraph graph;
    graph.topModule = "top";

    // InOut port named clk -- should NOT be classified (only In ports are)
    graph.allPorts.push_back(makePort("top.u_pad", "clk", ArgumentDirection::InOut));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    CHECK(topo.clockGroups.empty());
    CHECK(topo.resetGroups.empty());
    CHECK(topo.warnings.empty());
}

TEST_CASE("ClockResetAnalyzer: ambiguous clk_rst_n matches clock not reset", "[ClockReset]") {
    // isClockPort checks "clk" first; isResetPort is in the else-if branch.
    // A port named "clk_rst_n" contains both "clk" and "rst" words.
    // Since isClockPort is checked first in analyze(), it should be classified as clock.

    CHECK(ClockResetAnalyzer::isClockPort("clk_rst_n"));  // contains "clk"
    CHECK(ClockResetAnalyzer::isResetPort("clk_rst_n"));  // contains "rst"

    // But in the analyzer, the else-if means clock wins
    ConnectionGraph graph;
    graph.topModule = "top";
    graph.allPorts.push_back(makePort("top.u_dut", "clk_rst_n", ArgumentDirection::In));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    // Should appear in clock groups, NOT reset groups
    CHECK(topo.clockGroups.count("clk_rst_n") == 1);
    CHECK(topo.resetGroups.count("clk_rst_n") == 0);
    // Instance has clock but no reset -> warning
    CHECK(topo.warnings.size() == 1);
}

TEST_CASE("ClockResetAnalyzer: exact name 'reset' is classified as reset", "[ClockReset]") {
    CHECK(ClockResetAnalyzer::isResetPort("reset"));

    ConnectionGraph graph;
    graph.topModule = "top";
    graph.allPorts.push_back(makePort("top.u_core", "reset", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_core", "clk", ArgumentDirection::In));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    CHECK(topo.resetGroups.count("reset") == 1);
    CHECK(topo.resetGroups.at("reset").size() == 1);
}

TEST_CASE("ClockResetAnalyzer: uppercase CLOCK is classified as clock", "[ClockReset]") {
    CHECK(ClockResetAnalyzer::isClockPort("CLOCK"));
    CHECK(ClockResetAnalyzer::isClockPort("SYS_CLOCK"));
    CHECK(ClockResetAnalyzer::isClockPort("CLOCK_200MHZ"));

    ConnectionGraph graph;
    graph.topModule = "top";
    graph.allPorts.push_back(makePort("top.u_dut", "CLOCK", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_dut", "RST_N", ArgumentDirection::In));

    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(graph);

    CHECK(topo.clockGroups.count("CLOCK") == 1);
    CHECK(topo.warnings.empty()); // has both clock and reset
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
