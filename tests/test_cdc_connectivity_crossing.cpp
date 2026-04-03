#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"

using namespace sv_cdccheck;

struct CDCPipeline {
    ClockDatabase db;
    std::unique_ptr<FFClassifier> classifier;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;

    void run(slang::ast::Compilation& compilation) {
        ClockTreeAnalyzer clockAnalyzer(compilation, db);
        clockAnalyzer.analyze();

        classifier = std::make_unique<FFClassifier>(compilation, db);
        classifier->analyze();

        ConnectivityBuilder connectivity(compilation, classifier->getFFNodes());
        connectivity.analyze();
        edges = connectivity.getEdges();

        CrossingDetector detector(edges, db);
        detector.analyze();
        crossings = detector.getCrossings();
    }
};

TEST_CASE("CDC Connectivity: direct async FF-to-FF crossing is detected", "[cdc][connectivity]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module missing_sync(input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0; else q_b <= q_a;
            end
        endmodule
    )", "cdc_conn");
    REQUIRE(compiled);

    CDCPipeline pipeline;
    pipeline.run(*compiled.compilation);

    REQUIRE(!pipeline.crossings.empty());
    CHECK(pipeline.crossings[0].category == ViolationCategory::Violation);
    CHECK(pipeline.crossings[0].severity == Severity::High);
}

TEST_CASE("CDC Connectivity: assign chain still creates a crossing", "[cdc][connectivity]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module assign_chain(input logic clk_a, clk_b, rst_n, d);
            logic q_a, wire_mid, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            assign wire_mid = q_a;
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0; else q_b <= wire_mid;
            end
        endmodule
    )", "cdc_assign");
    REQUIRE(compiled);

    CDCPipeline pipeline;
    pipeline.run(*compiled.compilation);
    CHECK(!pipeline.crossings.empty());
}
