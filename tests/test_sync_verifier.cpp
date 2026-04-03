#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"

using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_sync");
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

TEST_CASE("SyncVerifier: unsynchronized crossing stays VIOLATION", "[sync]") {
    auto compilation = compileSV(R"(
        module no_sync (
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0;
                else        q_b <= q_a;
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(pipeline.crossings.size() >= 1);
    // No synchronizer → should remain VIOLATION
    bool found_violation = false;
    for (auto& c : pipeline.crossings) {
        if (c.category == ViolationCategory::Violation)
            found_violation = true;
    }
    CHECK(found_violation);
    CHECK(pipeline.crossings[0].sync_type == SyncType::None);
}

TEST_CASE("SyncVerifier: 3-FF sync detected as ThreeFF", "[sync]") {
    auto compilation = compileSV(R"(
        module three_ff_sync (
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2, sync3;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                    sync3 <= 1'b0;
                end else begin
                    sync1 <= q_a;
                    sync2 <= sync1;
                    sync3 <= sync2;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    bool found_three = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::ThreeFF) {
            found_three = true;
            CHECK(c.category == ViolationCategory::Info);
        }
    }
    CHECK(found_three);
}

TEST_CASE("SyncVerifier: 2-FF with required_stages 3 is CAUTION", "[sync]") {
    auto compilation = compileSV(R"(
        module two_ff_caution (
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                end else begin
                    sync1 <= q_a;
                    sync2 <= sync1;
                end
            end
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer clock_analyzer(*compilation, db);
    clock_analyzer.analyze();

    auto classifier = std::make_unique<FFClassifier>(*compilation, db);
    classifier->analyze();

    ConnectivityBuilder conn(*compilation, classifier->getFFNodes());
    conn.analyze();
    auto edges = conn.getEdges();

    CrossingDetector detector(edges, db);
    detector.analyze();
    auto crossings = detector.getCrossings();

    SyncVerifier verifier(crossings, classifier->getFFNodes(), edges);
    verifier.setRequiredStages(3);
    verifier.analyze();

    bool found_caution = false;
    for (auto& c : crossings) {
        if (c.sync_type == SyncType::TwoFF) {
            found_caution = true;
            CHECK(c.category == ViolationCategory::Caution);
        }
    }
    CHECK(found_caution);
}

TEST_CASE("SyncVerifier: no sync remains VIOLATION after verify", "[sync]") {
    auto compilation = compileSV(R"(
        module still_violation (
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0;
                else        q_b <= q_a;
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(pipeline.crossings.size() >= 1);
    bool all_violations_have_no_sync = true;
    for (auto& c : pipeline.crossings) {
        if (c.category == ViolationCategory::Violation) {
            if (c.sync_type != SyncType::None)
                all_violations_have_no_sync = false;
        }
    }
    CHECK(all_violations_have_no_sync);
}

TEST_CASE("SyncVerifier: multiple crossings classified independently", "[sync]") {
    auto compilation = compileSV(R"(
        module mixed_crossings (
            input  logic clk_a, clk_b, rst_n, d1, d2
        );
            logic q_a1, q_a2;
            logic q_b_unsync;
            logic sync1, sync2;

            // Source domain FFs
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) begin q_a1 <= 1'b0; q_a2 <= 1'b0; end
                else begin q_a1 <= d1; q_a2 <= d2; end
            end

            // Unsynchronized crossing
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b_unsync <= 1'b0;
                else        q_b_unsync <= q_a1;
            end

            // Synchronized crossing (2-FF)
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                end else begin
                    sync1 <= q_a2;
                    sync2 <= sync1;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(pipeline.crossings.size() >= 2);
    bool found_violation = false;
    bool found_synced = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::None && c.category == ViolationCategory::Violation)
            found_violation = true;
        if (c.sync_type == SyncType::TwoFF)
            found_synced = true;
    }
    CHECK(found_violation);
    CHECK(found_synced);
}

TEST_CASE("SyncVerifier: findNextFF follows chain not data consumer", "[sync]") {
    // sync1 -> sync2 is a chain (single fanin), sync2 -> consumer is data use
    auto compilation = compileSV(R"(
        module chain_not_consumer (
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2;
            logic consumer;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                end else begin
                    sync1 <= q_a;
                    sync2 <= sync1;
                end
            end

            // Consumer uses sync2 along with other logic -- not part of sync chain
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) consumer <= 1'b0;
                else        consumer <= sync2 & d;
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // The crossing should be synced (not a VIOLATION). Post-processing may
    // reclassify from TwoFF to pulse_sync, but it must NOT be ThreeFF
    // (consumer has multiple fanins and is not part of the sync chain).
    bool found_synced_not_three = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type != SyncType::None && c.sync_type != SyncType::ThreeFF) {
            found_synced_not_three = true;
        }
    }
    CHECK(found_synced_not_three);
}

TEST_CASE("SyncVerifier: gray code via SyncVerifier pipeline", "[sync]") {
    // Gray code detection requires >= 3 individual crossings with the same
    // signal prefix. Use separate single-bit FFs to generate enough crossings.
    // Avoid ptr/addr/fifo naming to not trigger AsyncFIFO classification.
    auto compilation = compileSV(R"(
        module gray_sync (
            input  logic clk_a, clk_b, rst_n,
            input  logic d0, d1, d2, d3
        );
            logic gray_enc_0, gray_enc_1, gray_enc_2, gray_enc_3;
            logic gray_s1_0, gray_s1_1, gray_s1_2, gray_s1_3;
            logic gray_s2_0, gray_s2_1, gray_s2_2, gray_s2_3;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) begin
                    gray_enc_0 <= 1'b0; gray_enc_1 <= 1'b0;
                    gray_enc_2 <= 1'b0; gray_enc_3 <= 1'b0;
                end else begin
                    gray_enc_0 <= d0; gray_enc_1 <= d1;
                    gray_enc_2 <= d2; gray_enc_3 <= d3;
                end
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    gray_s1_0 <= 1'b0; gray_s1_1 <= 1'b0;
                    gray_s1_2 <= 1'b0; gray_s1_3 <= 1'b0;
                    gray_s2_0 <= 1'b0; gray_s2_1 <= 1'b0;
                    gray_s2_2 <= 1'b0; gray_s2_3 <= 1'b0;
                end else begin
                    gray_s1_0 <= gray_enc_0;
                    gray_s1_1 <= gray_enc_1;
                    gray_s1_2 <= gray_enc_2;
                    gray_s1_3 <= gray_enc_3;
                    gray_s2_0 <= gray_s1_0;
                    gray_s2_1 <= gray_s1_1;
                    gray_s2_2 <= gray_s1_2;
                    gray_s2_3 <= gray_s1_3;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    bool has_crossings = !pipeline.crossings.empty();
    CHECK(has_crossings);

    // Gray code detection groups multi-bit crossings with same prefix.
    // With 4 individual signals sharing "gray_enc_" prefix, the detector
    // should classify them as GrayCode (not AsyncFIFO since no ptr/addr name).
    bool found_gray = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::GrayCode)
            found_gray = true;
    }
    CHECK(found_gray);
}

TEST_CASE("SyncVerifier: 2-FF sync detected as TwoFF", "[sync]") {
    auto compilation = compileSV(R"(
        module with_sync (
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                end else begin
                    sync1 <= q_a;
                    sync2 <= sync1;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Should detect crossing with 2-FF synchronizer
    bool found_synced = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::TwoFF) {
            found_synced = true;
            CHECK(c.category == ViolationCategory::Info);
        }
    }
    CHECK(found_synced);
}
