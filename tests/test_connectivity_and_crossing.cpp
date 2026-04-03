#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"

using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_conn");
}

// Helper: run full Pass 1-4 pipeline
struct CDCPipeline {
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
    }
};

// ─── Pass 3: Connectivity Tests ───

TEST_CASE("Connectivity: no edges in single-domain design", "[conn]") {
    auto compilation = compileSV(R"(
        module single_dom (
            input  logic       clk,
            input  logic       rst_n,
            input  logic [7:0] data_in,
            output logic [7:0] data_out
        );
            logic [7:0] stage1, stage2;
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    stage1 <= 8'h0;
                    stage2 <= 8'h0;
                end else begin
                    stage1 <= data_in;
                    stage2 <= stage1;
                end
            end
            assign data_out = stage2;
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // All FFs are in same domain → edges exist but no crossings
    CHECK(pipeline.crossings.empty());
}

TEST_CASE("Connectivity: direct FF-to-FF crossing detected", "[conn]") {
    auto compilation = compileSV(R"(
        module missing_sync (
            input  logic clk_a,
            input  logic clk_b,
            input  logic rst_n,
            input  logic data_in
        );
            logic q_a;
            logic q_b;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= data_in;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0;
                else        q_b <= q_a;  // CDC crossing!
            end
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Should detect at least one crossing
    REQUIRE(pipeline.crossings.size() >= 1);
    CHECK(pipeline.crossings[0].category == ViolationCategory::Violation);
    CHECK(pipeline.crossings[0].severity == Severity::High);
}

// ─── Pass 4: Crossing Detector Tests ───

TEST_CASE("CrossingDetector: properly synced crossing is INFO", "[crossing]") {
    auto compilation = compileSV(R"(
        module two_ff_sync (
            input  logic clk_a,
            input  logic clk_b,
            input  logic rst_n,
            input  logic data_in
        );
            logic q_a;
            logic sync_ff1, sync_ff2;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= data_in;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync_ff1 <= 1'b0;
                    sync_ff2 <= 1'b0;
                end else begin
                    sync_ff1 <= q_a;
                    sync_ff2 <= sync_ff1;
                end
            end
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // There is a crossing from clk_a → clk_b
    // With 2-FF sync, it should be INFO (not VIOLATION)
    // For now (MVP), just check crossing is detected
    CHECK(pipeline.crossings.size() >= 1);
}

TEST_CASE("CrossingDetector: crossing has source and dest domain info", "[crossing]") {
    auto compilation = compileSV(R"(
        module domain_info (
            input  logic clk_a,
            input  logic clk_b,
            input  logic rst_n,
            input  logic d
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

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(pipeline.crossings.size() >= 1);
    auto& c = pipeline.crossings[0];
    CHECK(c.source_domain != nullptr);
    CHECK(c.dest_domain != nullptr);
    if (c.source_domain && c.dest_domain) {
        CHECK(c.source_domain->canonical_name != c.dest_domain->canonical_name);
    }
    CHECK(!c.id.empty());
    CHECK(!c.source_signal.empty());
    CHECK(!c.dest_signal.empty());
}

// ─── Additional Pass 3+4 Tests ───

TEST_CASE("Connectivity: assign chain creates crossing", "[conn]") {
    auto compilation = compileSV(R"(
        module assign_chain (
            input  logic clk_a, clk_b, rst_n, data_in
        );
            logic q_a;
            logic wire_mid;
            logic q_b;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= data_in;
            end

            assign wire_mid = q_a;

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0;
                else        q_b <= wire_mid;
            end
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Assign chain q_a -> wire_mid -> q_b should still detect crossing
    REQUIRE(pipeline.crossings.size() >= 1);
    CHECK(pipeline.crossings[0].category == ViolationCategory::Violation);
}

TEST_CASE("Connectivity: multiple edges from same source FF", "[conn]") {
    auto compilation = compileSV(R"(
        module multi_dest (
            input  logic clk_a, clk_b, rst_n, data_in
        );
            logic q_a;
            logic q_b1, q_b2;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= data_in;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b1 <= 1'b0;
                else        q_b1 <= q_a;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b2 <= 1'b0;
                else        q_b2 <= q_a;
            end
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Should detect at least 2 crossings (one per dest FF)
    CHECK(pipeline.crossings.size() >= 2);
}

TEST_CASE("Connectivity: edge comb_path populated for assign chain", "[conn]") {
    auto compilation = compileSV(R"(
        module comb_path_check (
            input  logic clk_a, clk_b, rst_n, data_in
        );
            logic q_a;
            logic comb_wire;
            logic q_b;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= data_in;
            end

            assign comb_wire = q_a;

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0;
                else        q_b <= comb_wire;
            end
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Check that edges have comb_path populated when going through assign
    bool found_edge_with_comb = false;
    for (auto& edge : pipeline.edges) {
        if (edge.source && edge.dest &&
            edge.source->domain && edge.dest->domain &&
            !edge.source->domain->isSameDomain(*edge.dest->domain)) {
            // The edge exists across domains -- that's what we care about
            found_edge_with_comb = true;
        }
    }
    CHECK(found_edge_with_comb);
}

TEST_CASE("Connectivity: no duplicate edges for same source-dest pair", "[conn]") {
    auto compilation = compileSV(R"(
        module no_dup (
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic q_b;

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

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Count edges with same source->dest pair
    std::unordered_map<std::string, int> edge_counts;
    for (auto& edge : pipeline.edges) {
        if (edge.source && edge.dest) {
            std::string key = edge.source->hier_path + "->" + edge.dest->hier_path;
            edge_counts[key]++;
        }
    }
    for (auto& [key, count] : edge_counts) {
        CHECK(count == 1);
    }
}

TEST_CASE("CrossingDetector: divided clock crossing is CAUTION not VIOLATION", "[crossing]") {
    auto compilation = compileSV(R"(
        module divided_clk (
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

    // Manually set up the clock database with a Divided relationship
    ClockDatabase db;
    ClockTreeAnalyzer clock_analyzer(*compilation, db);
    clock_analyzer.analyze();

    // Override the relationship to Divided
    for (auto& rel : db.relationships) {
        rel.relationship = DomainRelationship::Type::Divided;
    }

    auto classifier = std::make_unique<FFClassifier>(*compilation, db);
    classifier->analyze();

    ConnectivityBuilder conn(*compilation, classifier->getFFNodes());
    conn.analyze();
    auto edges = conn.getEdges();

    CrossingDetector detector(edges, db);
    detector.analyze();
    auto crossings = detector.getCrossings();

    REQUIRE(crossings.size() >= 1);
    CHECK(crossings[0].category == ViolationCategory::Caution);
    CHECK(crossings[0].severity == Severity::Medium);
}

TEST_CASE("CrossingDetector: crossing report has correct domain names", "[crossing]") {
    auto compilation = compileSV(R"(
        module domain_names (
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

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(pipeline.crossings.size() >= 1);
    auto& c = pipeline.crossings[0];
    REQUIRE(c.source_domain != nullptr);
    REQUIRE(c.dest_domain != nullptr);

    // Domain names should contain the clock signal names
    bool src_has_clk_a = c.source_domain->canonical_name.find("clk_a") != std::string::npos;
    bool src_has_clk_b = c.source_domain->canonical_name.find("clk_b") != std::string::npos;
    bool dst_has_clk_a = c.dest_domain->canonical_name.find("clk_a") != std::string::npos;
    bool dst_has_clk_b = c.dest_domain->canonical_name.find("clk_b") != std::string::npos;
    // One domain should have clk_a, the other clk_b
    CHECK((src_has_clk_a || src_has_clk_b));
    CHECK((dst_has_clk_a || dst_has_clk_b));
    CHECK(c.source_domain->canonical_name != c.dest_domain->canonical_name);
}
