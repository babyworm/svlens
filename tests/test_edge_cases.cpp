#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"

using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_edge");
}

// ─── Latch Warning (spec 4.2.3) ───

TEST_CASE("FFClassifier: always_latch flagged as warning", "[edge]") {
    auto compilation = compileSV(R"(
        module with_latch (
            input  logic clk,
            input  logic en,
            input  logic d,
            output logic q
        );
            always_latch begin
                if (en) q <= d;
            end
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer clock_analyzer(*compilation, db);
    clock_analyzer.analyze();

    FFClassifier classifier(*compilation, db);
    classifier.analyze();

    // Latch should NOT be counted as an FF
    auto& ffs = classifier.getFFNodes();
    CHECK(ffs.empty());

    // Latch warnings should be recorded
    CHECK(classifier.getLatchWarnings().size() >= 1);
}

// ─── Legacy always @(posedge ...) ───

TEST_CASE("FFClassifier: legacy always @(posedge) detected as FF", "[edge]") {
    auto compilation = compileSV(R"(
        module legacy_ff (
            input  logic clk,
            input  logic rst_n,
            input  logic d
        );
            reg q;
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) q <= 1'b0;
                else        q <= d;
            end
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer clock_analyzer(*compilation, db);
    clock_analyzer.analyze();

    FFClassifier classifier(*compilation, db);
    classifier.analyze();

    auto& ffs = classifier.getFFNodes();
    REQUIRE(ffs.size() >= 1);
    CHECK(ffs[0]->domain != nullptr);
}

// ─── Empty design ───

TEST_CASE("Pipeline: empty module produces no violations", "[edge]") {
    auto compilation = compileSV(R"(
        module empty_mod (
            input logic clk,
            input logic rst_n
        );
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer clock_analyzer(*compilation, db);
    clock_analyzer.analyze();

    FFClassifier classifier(*compilation, db);
    classifier.analyze();
    CHECK(classifier.getFFNodes().empty());

    ConnectivityBuilder conn(*compilation, classifier.getFFNodes());
    conn.analyze();
    CHECK(conn.getEdges().empty());

    CrossingDetector detector(conn.getEdges(), db);
    detector.analyze();
    CHECK(detector.getCrossings().empty());
}

// ─── Crossing counter separation ───

TEST_CASE("CrossingDetector: separate counters for VIOLATION and CAUTION", "[edge]") {
    // Create a scenario with both async and related crossings
    ClockDatabase db;

    auto src_a = std::make_unique<ClockSource>();
    src_a->name = "clk_a";
    auto* a = db.addSource(std::move(src_a));

    auto src_b = std::make_unique<ClockSource>();
    src_b->name = "clk_b";
    auto* b = db.addSource(std::move(src_b));

    auto src_c = std::make_unique<ClockSource>();
    src_c->name = "clk_c";
    src_c->master = a;
    src_c->divide_by = 2;
    auto* c = db.addSource(std::move(src_c));

    // a-b: async, a-c: divided
    db.relationships.push_back({a, b, DomainRelationship::Type::Asynchronous});
    db.relationships.push_back({a, c, DomainRelationship::Type::Divided});

    auto* dom_a = db.findOrCreateDomain(a, Edge::Posedge);
    auto* dom_b = db.findOrCreateDomain(b, Edge::Posedge);
    auto* dom_c = db.findOrCreateDomain(c, Edge::Posedge);

    // Create FF nodes and edges manually
    FFNode ff_a{"top.q_a", dom_a, nullptr, {}};
    FFNode ff_b{"top.q_b", dom_b, nullptr, {}};
    FFNode ff_c{"top.q_c", dom_c, nullptr, {}};

    std::vector<FFEdge> edges;
    edges.push_back({&ff_a, &ff_b, {}, SyncType::None});  // async
    edges.push_back({&ff_a, &ff_c, {}, SyncType::None});  // divided

    CrossingDetector detector(edges, db);
    detector.analyze();
    auto crossings = detector.getCrossings();

    REQUIRE(crossings.size() == 2);

    // Check IDs are properly categorized
    bool has_violation = false, has_caution = false;
    for (auto& c : crossings) {
        if (c.id.starts_with("VIOLATION-")) has_violation = true;
        if (c.id.starts_with("CAUTION-")) has_caution = true;
    }
    CHECK(has_violation);
    CHECK(has_caution);
}
