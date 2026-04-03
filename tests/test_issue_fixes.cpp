#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/sdc_parser.h"

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_fixes");
}

// Full pipeline helper including sync verification
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

// ─── C1: AnalysisResult missing ff_nodes/edges ───

TEST_CASE("C1: AnalysisResult ff_nodes and edges are populated for DOT graph", "[C1]") {
    auto compilation = compileSV(R"(
        module c1_test (
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

    ClockDatabase clock_db;
    ClockTreeAnalyzer clock_analyzer(*compilation, clock_db);
    clock_analyzer.analyze();

    FFClassifier classifier(*compilation, clock_db);
    classifier.analyze();

    ConnectivityBuilder connectivity(*compilation, classifier.getFFNodes());
    connectivity.analyze();

    CrossingDetector detector(connectivity.getEdges(), clock_db);
    detector.analyze();
    auto crossings = detector.getCrossings();

    SyncVerifier verifier(crossings, classifier.getFFNodes(), connectivity.getEdges());
    verifier.analyze();

    AnalysisResult result;
    result.clock_db = std::move(clock_db);
    result.crossings = std::move(crossings);
    result.ff_nodes = classifier.releaseFFNodes();
    result.edges = connectivity.getEdges();

    // ff_nodes and edges must be populated
    CHECK(!result.ff_nodes.empty());
    CHECK(!result.edges.empty());

    // DOT generation should produce valid output with node and edge data
    auto dot_path = fs::temp_directory_path() / "test_c1.dot";
    ReportGenerator report(result);
    report.generateDOT(dot_path);

    std::ifstream f(dot_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(dot_path);

    CHECK(content.find("digraph CDC") != std::string::npos);
    CHECK(content.find("->") != std::string::npos);
    // Should have FF node labels
    CHECK(content.find("label=") != std::string::npos);
}

// ─── C2: findNextFF multi-fanout false matching ───

TEST_CASE("C2: findNextFF skips edges with comb logic and multi-fanin", "[C2]") {
    // Create a design where an FF feeds multiple destinations through comb logic.
    // The sync verifier should NOT falsely identify a sync chain through a
    // data-consumer path.
    auto compilation = compileSV(R"(
        module c2_test (
            input  logic clk_a, clk_b, rst_n, d, sel
        );
            logic q_a;
            logic sync1, sync2;
            logic data_consumer;

            // Source domain
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end

            // Dest domain: sync chain (direct FF-to-FF)
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                end else begin
                    sync1 <= q_a;
                    sync2 <= sync1;
                end
            end

            // Dest domain: data consumer (sync1 + sel => comb logic)
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) data_consumer <= 1'b0;
                else        data_consumer <= sync1 & sel;
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // The q_a -> sync1 crossing should still be detected as TwoFF
    bool found_synced = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::TwoFF) {
            found_synced = true;
        }
    }
    CHECK(found_synced);
}

// ─── C3: LogicallyExclusive mapped to PhysicallyExclusive ───

TEST_CASE("C3: LogicallyExclusive gets its own relationship type", "[C3]") {
    // Create clock database and manually add a LogicallyExclusive relationship
    ClockDatabase db;

    auto src_a = std::make_unique<ClockSource>();
    src_a->name = "mux_clk_a";
    src_a->type = ClockSource::Type::Primary;
    auto* a_ptr = db.addSource(std::move(src_a));

    auto src_b = std::make_unique<ClockSource>();
    src_b->name = "mux_clk_b";
    src_b->type = ClockSource::Type::Primary;
    auto* b_ptr = db.addSource(std::move(src_b));

    // Simulate what importSdcRelationships does with LogicallyExclusive
    db.relationships.push_back(
        {a_ptr, b_ptr, DomainRelationship::Type::LogicallyExclusive});

    // LogicallyExclusive should NOT be treated as asynchronous
    auto* dom_a = db.findOrCreateDomain(a_ptr, Edge::Posedge);
    auto* dom_b = db.findOrCreateDomain(b_ptr, Edge::Posedge);
    CHECK_FALSE(db.isAsynchronous(dom_a, dom_b));

    // Verify the relationship type is actually LogicallyExclusive, not PhysicallyExclusive
    REQUIRE(db.relationships.size() == 1);
    CHECK(db.relationships[0].relationship == DomainRelationship::Type::LogicallyExclusive);
}

// ─── H4: Convention check masks real VIOLATION ───

TEST_CASE("H4: Non-standard clock name does not mask VIOLATION category", "[H4]") {
    // Use a non-standard clock name (no clk/clock/ck pattern)
    auto compilation = compileSV(R"(
        module h4_test (
            input  logic fast_osc, slow_osc, rst_n, d
        );
            logic q_a, q_b;
            always_ff @(posedge fast_osc or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end
            always_ff @(posedge slow_osc or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0;
                else        q_b <= q_a;
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(!pipeline.crossings.empty());
    // The crossing should still be classified based on domain relationship
    // (VIOLATION for async), not downgraded to CONVENTION
    bool found_violation = false;
    bool has_convention_note = false;
    for (auto& c : pipeline.crossings) {
        if (c.category == ViolationCategory::Violation) {
            found_violation = true;
            // Convention should be an annotation in recommendation, not the category
            if (c.recommendation.find("naming convention") != std::string::npos)
                has_convention_note = true;
        }
    }
    CHECK(found_violation);
    CHECK(has_convention_note);
}

// ─── H2: Expression walker missing types ───

TEST_CASE("H2: collectReferencedSignals handles concatenation", "[H2]") {
    auto compilation = compileSV(R"(
        module h2_concat (
            input  logic clk_a, clk_b, rst_n,
            input  logic [3:0] a_data, b_data
        );
            logic [3:0] q_a, q_b;
            logic [7:0] q_cat;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 4'h0;
                else        q_a <= a_data;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 4'h0;
                else        q_b <= b_data;
            end

            // Concatenation crossing: {q_a, q_b} used across domains
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_cat <= 8'h0;
                else        q_cat <= {q_a, q_b};
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Should detect q_a -> q_cat as a crossing (clk_a -> clk_b)
    bool found_crossing = false;
    for (auto& c : pipeline.crossings) {
        if (c.dest_signal.find("q_cat") != std::string::npos &&
            c.source_signal.find("q_a") != std::string::npos) {
            found_crossing = true;
        }
    }
    CHECK(found_crossing);
}

TEST_CASE("H2: collectReferencedSignals handles element select", "[H2]") {
    auto compilation = compileSV(R"(
        module h2_elemsel (
            input  logic clk_a, clk_b, rst_n,
            input  logic [7:0] data_in
        );
            logic [7:0] q_a;
            logic q_bit;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 8'h0;
                else        q_a <= data_in;
            end

            // Element select crossing: q_a[0]
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_bit <= 1'b0;
                else        q_bit <= q_a[0];
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Should detect q_a -> q_bit crossing
    bool found_crossing = false;
    for (auto& c : pipeline.crossings) {
        if (c.dest_signal.find("q_bit") != std::string::npos) {
            found_crossing = true;
        }
    }
    CHECK(found_crossing);
}

TEST_CASE("H2: collectReferencedSignals handles range select", "[H2]") {
    auto compilation = compileSV(R"(
        module h2_rangesel (
            input  logic clk_a, clk_b, rst_n,
            input  logic [7:0] data_in
        );
            logic [7:0] q_a;
            logic [3:0] q_slice;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 8'h0;
                else        q_a <= data_in;
            end

            // Range select crossing: q_a[3:0]
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_slice <= 4'h0;
                else        q_slice <= q_a[3:0];
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    bool found_crossing = false;
    for (auto& c : pipeline.crossings) {
        if (c.dest_signal.find("q_slice") != std::string::npos) {
            found_crossing = true;
        }
    }
    CHECK(found_crossing);
}

TEST_CASE("H2: collectReferencedSignals handles conditional condition expr", "[H2]") {
    auto compilation = compileSV(R"(
        module h2_cond (
            input  logic clk_a, clk_b, rst_n,
            input  logic data_in, alt_in
        );
            logic q_sel, q_out;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_sel <= 1'b0;
                else        q_sel <= data_in;
            end

            // Conditional: q_sel is the condition (was not collected before)
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_out <= 1'b0;
                else        q_out <= q_sel ? alt_in : 1'b0;
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    bool found_crossing = false;
    for (auto& c : pipeline.crossings) {
        if (c.dest_signal.find("q_out") != std::string::npos &&
            c.source_signal.find("q_sel") != std::string::npos) {
            found_crossing = true;
        }
    }
    CHECK(found_crossing);
}

// ─── H3: Clock net hier_path incomplete for 3+ levels ───

TEST_CASE("H3: Clock propagation uses full hierarchical path for 3+ levels", "[H3]") {
    auto compilation = compileSV(R"(
        module leaf (
            input logic clk, rst_n, d,
            output logic q
        );
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) q <= 1'b0;
                else        q <= d;
            end
        endmodule

        module mid (
            input logic clk, rst_n, d,
            output logic q
        );
            leaf u_leaf (.clk(clk), .rst_n(rst_n), .d(d), .q(q));
        endmodule

        module h3_top (
            input logic clk_a, rst_n, d
        );
            logic q_out;
            mid u_mid (.clk(clk_a), .rst_n(rst_n), .d(d), .q(q_out));
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compilation, db);
    analyzer.analyze();

    // The clock net for the leaf level should have the full hierarchical path
    // including the top instance, not just the immediate parent
    bool found_deep_net = false;
    for (auto& net : db.nets) {
        // Should contain the full path through mid and leaf
        if (net->hier_path.find("u_mid") != std::string::npos &&
            net->hier_path.find("u_leaf") != std::string::npos) {
            found_deep_net = true;
        }
    }
    CHECK(found_deep_net);
}

// ─── H5: Reset sync check ignores destination domain ───

TEST_CASE("H5: Reset sync check accounts for destination domain", "[H5]") {
    auto compilation = compileSV(R"(
        module h5_test (
            input  logic clk_a, clk_b, clk_c, rst_n, d
        );
            logic q_a, rst_gen;

            // clk_a domain: generates a reset signal
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) rst_gen <= 1'b0;
                else        rst_gen <= d;
            end

            // clk_b domain: uses rst_gen as async reset - synced for clk_b
            logic sync1_b, sync2_b;
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1_b <= 1'b0;
                    sync2_b <= 1'b0;
                end else begin
                    sync1_b <= rst_gen;
                    sync2_b <= sync1_b;
                end
            end

            // clk_c domain: also uses rst_gen - NOT synced for clk_c
            logic q_c;
            always_ff @(posedge clk_c or negedge rst_n) begin
                if (!rst_n) q_c <= 1'b0;
                else        q_c <= rst_gen;
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // The rst_gen -> clk_b crossing should be INFO (synced via sync1_b/sync2_b)
    // The rst_gen -> clk_c crossing should be VIOLATION (no sync)
    // Previously, the sync for clk_b would incorrectly mark rst_gen as synced
    // for ALL domains
    bool found_clk_c_violation = false;
    for (auto& c : pipeline.crossings) {
        if (c.dest_signal.find("q_c") != std::string::npos &&
            c.category == ViolationCategory::Violation) {
            found_clk_c_violation = true;
        }
    }
    CHECK(found_clk_c_violation);
}

// ─── H6: stoi/stod exceptions not caught ───

TEST_CASE("H6: SDC parser handles malformed numeric values gracefully", "[H6]") {
    auto sdc_path = fs::temp_directory_path() / "test_h6.sdc";
    {
        std::ofstream f(sdc_path);
        f << "create_clock -name sys_clk -period not_a_number [get_ports clk]\n";
        f << "create_generated_clock -name gen_clk -source [get_ports clk] "
             "-divide_by bad_value [get_ports gen_out]\n";
        f << "create_clock -name good_clk -period 10.0 [get_ports clk2]\n";
    }

    // Should not throw
    SdcConstraints result;
    REQUIRE_NOTHROW(result = SdcParser::parse(sdc_path));

    // The good clock should still be parsed
    bool found_good = false;
    for (auto& c : result.clocks) {
        if (c.name == "good_clk" && c.period.has_value()) {
            CHECK(c.period.value() == 10.0);
            found_good = true;
        }
    }
    CHECK(found_good);

    fs::remove(sdc_path);
}
