#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"

using namespace sv_cdccheck;

struct FullPipeline {
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

        SyncVerifier verifier(crossings, classifier->getFFNodes(), edges);
        verifier.analyze();
    }
};

TEST_CASE("CDC SyncVerifier: unsynchronized crossing remains VIOLATION", "[cdc][sync]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module no_sync(input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0; else q_b <= q_a;
            end
        endmodule
    )", "cdc_sync_none");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    REQUIRE(!pipeline.crossings.empty());
    CHECK(pipeline.crossings[0].sync_type == SyncType::None);
    CHECK(pipeline.crossings[0].category == ViolationCategory::Violation);
}

TEST_CASE("CDC SyncVerifier: 2-FF synchronizer is recognized", "[cdc][sync]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module two_ff_sync(input logic clk_a, clk_b, rst_n, d);
            logic q_a, sync1, sync2;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 1'b0; sync2 <= 1'b0; end
                else begin sync1 <= q_a; sync2 <= sync1; end
            end
        endmodule
    )", "cdc_sync_twoff");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundTwoFF = false;
    for (const auto& crossing : pipeline.crossings) {
        if (crossing.sync_type == SyncType::TwoFF) {
            foundTwoFF = true;
            CHECK(crossing.category == ViolationCategory::Info);
        }
    }
    CHECK(foundTwoFF);
}
