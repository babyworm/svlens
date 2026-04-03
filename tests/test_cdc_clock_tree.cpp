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
