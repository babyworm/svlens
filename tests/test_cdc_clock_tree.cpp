#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"

using namespace sv_cdccheck;

TEST_CASE("CDC ClockTreeAnalyzer: clock/reset name classification", "[cdc][clock_tree]") {
    CHECK(ClockTreeAnalyzer::isClockName("clk"));
    CHECK(ClockTreeAnalyzer::isClockName("sys_clk"));
    CHECK(ClockTreeAnalyzer::isClockName("core_clock"));
    CHECK_FALSE(ClockTreeAnalyzer::isClockName("data_in"));

    CHECK(ClockTreeAnalyzer::isResetName("rst_n"));
    CHECK(ClockTreeAnalyzer::isResetName("sys_reset"));
    CHECK_FALSE(ClockTreeAnalyzer::isResetName("clk"));
}

TEST_CASE("CDC ClockTreeAnalyzer: grandchild generated clocks share root and are not async", "[cdc][clock_tree]") {
    // Build a trivial compilation (empty module) just to construct the analyzer
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module empty_mod(input logic clk);
        endmodule
    )", "cdc_root_chain");
    REQUIRE(compiled);

    ClockDatabase db;

    // Pre-populate 3 sources: pll (primary) -> div2 (generated) -> div4 (generated)
    auto pll = std::make_unique<ClockSource>();
    pll->id = "pll_out";
    pll->name = "pll_out";
    pll->type = ClockSource::Type::Primary;
    pll->origin_signal = "pll_out";
    auto* pll_ptr = db.addSource(std::move(pll));

    auto div2 = std::make_unique<ClockSource>();
    div2->id = "div2_clk";
    div2->name = "div2_clk";
    div2->type = ClockSource::Type::Generated;
    div2->origin_signal = "div2_clk";
    div2->master = pll_ptr;
    div2->divide_by = 2;
    auto* div2_ptr = db.addSource(std::move(div2));

    auto div4 = std::make_unique<ClockSource>();
    div4->id = "div4_clk";
    div4->name = "div4_clk";
    div4->type = ClockSource::Type::Generated;
    div4->origin_signal = "div4_clk";
    div4->master = div2_ptr;
    div4->divide_by = 2;
    db.addSource(std::move(div4));

    // Create domains for each source
    auto* domPll = db.findOrCreateDomain(pll_ptr, Edge::Posedge);
    auto* domDiv2 = db.findOrCreateDomain(div2_ptr, Edge::Posedge);
    auto* domDiv4 = db.findOrCreateDomain(db.sources[2].get(), Edge::Posedge);

    // Run analyzer — this will call inferRelationships() on the pre-populated sources
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    // All three should be related (Divided), not Asynchronous
    CHECK_FALSE(db.isAsynchronous(domPll, domDiv2));
    CHECK_FALSE(db.isAsynchronous(domPll, domDiv4));   // This was the bug: grandchild was wrongly async
    CHECK_FALSE(db.isAsynchronous(domDiv2, domDiv4));
}

TEST_CASE("CDC ClockTreeAnalyzer: auto-detects clock ports in single-domain design", "[cdc][clock_tree]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module single_domain(input logic clk, rst_n, input logic [7:0] data_in, output logic [7:0] data_out);
            logic [7:0] stage1;
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) stage1 <= '0;
                else stage1 <= data_in;
            end
            assign data_out = stage1;
        endmodule
    )", "cdc_clock_tree");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    bool foundClk = false;
    for (const auto& src : db.sources) {
        if (src->origin_signal == "clk")
            foundClk = true;
    }
    CHECK(foundClk);
}
