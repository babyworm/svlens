#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"

using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_phase3");
}

struct FullPipeline {
    ClockDatabase db;
    std::unique_ptr<FFClassifier> classifier;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;

    void run(slang::ast::Compilation& compilation) {
        ClockTreeAnalyzer clock_analyzer(compilation, db);
        clock_analyzer.analyze();

        classifier = std::make_unique<FFClassifier>(compilation, db);
        classifier->analyze();

        ConnectivityBuilder conn(compilation, classifier->getFFNodes());
        conn.analyze();
        edges = conn.getEdges();

        CrossingDetector detector(edges, db);
        detector.analyze();
        crossings = detector.getCrossings();

        SyncVerifier verifier(crossings, classifier->getFFNodes(), edges);
        verifier.analyze();
    }
};

// =============================================================================
// Reconvergence Detection Tests
// =============================================================================

TEST_CASE("Phase3: reconvergence detected for independent syncs from same source", "[phase3][reconvergence]") {
    auto compilation = compileSV(R"(
        module reconvergence (
            input logic clk_a, clk_b, rst_n
        );
            logic sig1_a, sig2_a;
            logic sig1_b, sig2_b;
            logic sync1_1, sync1_2, sync2_1, sync2_2;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) begin sig1_a <= 0; sig2_a <= 0; end
                else begin sig1_a <= 1; sig2_a <= 1; end
            end

            // Two independent 2-FF syncs from same source domain
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1_1 <= 0; sync1_2 <= 0;
                    sync2_1 <= 0; sync2_2 <= 0;
                end else begin
                    sync1_1 <= sig1_a; sync1_2 <= sync1_1;
                    sync2_1 <= sig2_a; sync2_2 <= sync2_1;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Should have crossings detected
    REQUIRE(pipeline.crossings.size() >= 2);

    // At least one crossing should be flagged as CAUTION with reconvergence recommendation
    bool found_reconvergence = false;
    for (auto& c : pipeline.crossings) {
        if (c.recommendation.find("Reconvergence") != std::string::npos ||
            c.recommendation.find("reconvergence") != std::string::npos) {
            found_reconvergence = true;
            CHECK(c.category == ViolationCategory::Caution);
        }
    }
    CHECK(found_reconvergence);
}

TEST_CASE("Phase3: single crossing from a domain does not trigger reconvergence", "[phase3][reconvergence]") {
    auto compilation = compileSV(R"(
        module single_crossing (
            input logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 0; sync2 <= 0; end
                else begin sync1 <= q_a; sync2 <= sync1; end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Single crossing — no reconvergence warning
    for (auto& c : pipeline.crossings) {
        CHECK(c.recommendation.find("Reconvergence") == std::string::npos);
    }
}

// =============================================================================
// Combinational Logic Before Sync FF Tests
// =============================================================================

TEST_CASE("Phase3: combinational logic before sync FF detected", "[phase3][comb_before_sync]") {
    auto compilation = compileSV(R"(
        module comb_before_sync (
            input logic clk_a, clk_b, rst_n, enable
        );
            logic q_a;
            logic glitch_wire;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= 1;

            assign glitch_wire = q_a & enable;  // combinational!

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 0; sync2 <= 0; end
                else begin sync1 <= glitch_wire; sync2 <= sync1; end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(pipeline.crossings.size() >= 1);

    // Should detect glitch risk: combinational logic before first sync FF
    bool found_glitch = false;
    for (auto& c : pipeline.crossings) {
        if (c.recommendation.find("glitch") != std::string::npos ||
            c.recommendation.find("ombinational") != std::string::npos) {
            found_glitch = true;
            CHECK(c.category == ViolationCategory::Caution);
        }
    }
    CHECK(found_glitch);
}

TEST_CASE("Phase3: direct FF-to-FF sync does not trigger comb-before-sync", "[phase3][comb_before_sync]") {
    auto compilation = compileSV(R"(
        module direct_sync (
            input logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 0; sync2 <= 0; end
                else begin sync1 <= q_a; sync2 <= sync1; end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Direct FF-to-FF — no glitch warning
    for (auto& c : pipeline.crossings) {
        bool has_glitch = (c.recommendation.find("glitch") != std::string::npos ||
                           c.recommendation.find("ombinational") != std::string::npos);
        CHECK_FALSE(has_glitch);
    }
}

// =============================================================================
// Reset Synchronizer Check Tests
// =============================================================================

TEST_CASE("Phase3: async reset from different domain flagged without reset sync", "[phase3][reset_sync]") {
    auto compilation = compileSV(R"(
        module bad_reset (
            input logic clk_a, clk_b, rst_n
        );
            logic rst_from_a;
            logic data_b;

            // Generate reset in domain A
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) rst_from_a <= 0; else rst_from_a <= 1;

            // Use it as async reset in domain B — no sync!
            always_ff @(posedge clk_b or negedge rst_from_a)
                if (!rst_from_a) data_b <= 0; else data_b <= 1;
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Should flag the async reset crossing
    bool found_reset_warning = false;
    for (auto& c : pipeline.crossings) {
        if (c.recommendation.find("reset") != std::string::npos ||
            c.recommendation.find("Reset") != std::string::npos) {
            found_reset_warning = true;
            CHECK(c.category == ViolationCategory::Caution);
        }
    }
    CHECK(found_reset_warning);
}

TEST_CASE("Phase3: properly synchronized reset not flagged", "[phase3][reset_sync]") {
    auto compilation = compileSV(R"(
        module good_reset (
            input logic clk_a, clk_b, rst_n
        );
            logic rst_from_a;
            logic rst_sync1, rst_sync2;
            logic data_b;

            // Generate reset in domain A
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) rst_from_a <= 0; else rst_from_a <= 1;

            // Reset synchronizer: 2-FF sync of the reset
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin rst_sync1 <= 0; rst_sync2 <= 0; end
                else begin rst_sync1 <= rst_from_a; rst_sync2 <= rst_sync1; end
            end

            // Use synced reset
            always_ff @(posedge clk_b or negedge rst_sync2)
                if (!rst_sync2) data_b <= 0; else data_b <= 1;
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // The reset crossing should be properly synced (Info), not flagged as reset issue
    bool found_bad_reset = false;
    for (auto& c : pipeline.crossings) {
        if (c.recommendation.find("reset synchronizer") != std::string::npos &&
            c.category == ViolationCategory::Caution) {
            found_bad_reset = true;
        }
    }
    CHECK_FALSE(found_bad_reset);
}

// =============================================================================
// Combined / Regression Tests
// =============================================================================

TEST_CASE("Phase3: existing 2-FF sync still detected correctly after phase3 additions", "[phase3][regression]") {
    auto compilation = compileSV(R"(
        module with_sync (
            input logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 0; sync2 <= 0; end
                else begin sync1 <= q_a; sync2 <= sync1; end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    bool found_synced = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::TwoFF) {
            found_synced = true;
            CHECK(c.category == ViolationCategory::Info);
        }
    }
    CHECK(found_synced);
}
