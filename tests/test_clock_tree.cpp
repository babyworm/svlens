#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"

#include <filesystem>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_ct");
}

TEST_CASE("ClockTreeAnalyzer: isClockName pattern matching", "[clock_tree]") {
    CHECK(ClockTreeAnalyzer::isClockName("clk"));
    CHECK(ClockTreeAnalyzer::isClockName("sys_clk"));
    CHECK(ClockTreeAnalyzer::isClockName("clk_a"));
    CHECK(ClockTreeAnalyzer::isClockName("core_clock"));
    CHECK(ClockTreeAnalyzer::isClockName("pcie_ck"));
    CHECK(ClockTreeAnalyzer::isClockName("CLK"));
    CHECK(ClockTreeAnalyzer::isClockName("SYS_CLK"));

    CHECK_FALSE(ClockTreeAnalyzer::isClockName("data_in"));
    CHECK_FALSE(ClockTreeAnalyzer::isClockName("reset"));
    CHECK_FALSE(ClockTreeAnalyzer::isClockName("enable"));
    CHECK_FALSE(ClockTreeAnalyzer::isClockName("clkout_valid")); // "clk" not at boundary? debatable
}

TEST_CASE("ClockTreeAnalyzer: isResetName pattern matching", "[clock_tree]") {
    CHECK(ClockTreeAnalyzer::isResetName("rst"));
    CHECK(ClockTreeAnalyzer::isResetName("rst_n"));
    CHECK(ClockTreeAnalyzer::isResetName("sys_reset"));
    CHECK(ClockTreeAnalyzer::isResetName("RSTN"));

    CHECK_FALSE(ClockTreeAnalyzer::isResetName("clk"));
    CHECK_FALSE(ClockTreeAnalyzer::isResetName("data"));
    CHECK_FALSE(ClockTreeAnalyzer::isResetName("enable"));
}

TEST_CASE("ClockTreeAnalyzer: auto-detect clock ports from single domain design", "[clock_tree]") {
    auto compilation = compileSV(R"(
        module single_domain (
            input  logic       clk,
            input  logic       rst_n,
            input  logic [7:0] data_in,
            output logic [7:0] data_out
        );
            logic [7:0] stage1;
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) stage1 <= 8'h0;
                else        stage1 <= data_in;
            end
            assign data_out = stage1;
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compilation, db);
    analyzer.analyze();

    // Should detect 'clk' as a clock source
    REQUIRE(db.sources.size() >= 1);
    bool found_clk = false;
    for (auto& src : db.sources) {
        if (src->origin_signal == "clk") {
            found_clk = true;
            CHECK(src->type == ClockSource::Type::AutoDetected);
        }
    }
    CHECK(found_clk);
}

TEST_CASE("ClockTreeAnalyzer: auto-detect two clock ports", "[clock_tree]") {
    auto compilation = compileSV(R"(
        module two_clk (
            input  logic clk_a,
            input  logic clk_b,
            input  logic rst_n,
            input  logic data_in
        );
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= data_in;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0;
                else        q_b <= q_a;
            end
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compilation, db);
    analyzer.analyze();

    // Should detect both clk_a and clk_b
    REQUIRE(db.sources.size() >= 2);
    bool found_a = false, found_b = false;
    for (auto& src : db.sources) {
        if (src->origin_signal == "clk_a") found_a = true;
        if (src->origin_signal == "clk_b") found_b = true;
    }
    CHECK(found_a);
    CHECK(found_b);
}

TEST_CASE("ClockTreeAnalyzer: SDC overrides auto-detect", "[clock_tree]") {
    auto compilation = compileSV(R"(
        module with_sdc (
            input logic sys_clk,
            input logic ext_clk,
            input logic rst_n
        );
        endmodule
    )");

    // Parse SDC
    auto sdc_path = fs::temp_directory_path() / "test_override.sdc";
    {
        std::ofstream f(sdc_path);
        f << "create_clock -name sys_clk -period 10 [get_ports sys_clk]\n"
          << "create_clock -name ext_clk -period 25 [get_ports ext_clk]\n"
          << "set_clock_groups -asynchronous -group {sys_clk} -group {ext_clk}\n";
    }
    auto sdc = SdcParser::parse(sdc_path);
    fs::remove(sdc_path);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();

    // SDC sources should be Primary, not AutoDetected
    int primary_count = 0;
    for (auto& src : db.sources) {
        if (src->type == ClockSource::Type::Primary)
            primary_count++;
    }
    CHECK(primary_count == 2);

    // Async relationship should be registered
    bool found_async = false;
    for (auto& rel : db.relationships) {
        if (rel.relationship == DomainRelationship::Type::Asynchronous)
            found_async = true;
    }
    CHECK(found_async);
}
