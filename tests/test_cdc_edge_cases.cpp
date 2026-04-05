#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"

using namespace sv_cdccheck;

TEST_CASE("CDC EdgeCases: always_latch is not treated as FF and emits warning", "[cdc][edge]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module with_latch(input logic clk, en, d, output logic q);
            always_latch begin
                if (en) q <= d;
            end
        endmodule
    )", "cdc_edge_latch");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    CHECK(classifier.getFFNodes().empty());
    CHECK(classifier.getLatchWarnings().size() >= 1);
}

TEST_CASE("CDC EdgeCases: legacy always @(posedge) is classified as FF", "[cdc][edge]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module legacy_ff(input logic clk, rst_n, d);
            reg q;
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) q <= 1'b0;
                else q <= d;
            end
        endmodule
    )", "cdc_edge_legacy");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    REQUIRE(classifier.getFFNodes().size() >= 1);
    CHECK(classifier.getFFNodes()[0]->domain != nullptr);
}

TEST_CASE("CDC EdgeCases: combinational always with sensitivity list is not classified as FF", "[cdc][edge]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module cdc_edge_comb(input logic a, b, sel);
            logic y;
            always @(sel or a or b) begin
                if (sel) y = a;
                else     y = b;
            end
        endmodule
    )", "cdc_edge_comb");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    CHECK(classifier.getFFNodes().empty());
}


TEST_CASE("CDC EdgeCases: combinational always @ (...) debug mux is not treated as FF", "[cdc][edge]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module debug_mux_only(input logic sel, a, b, output logic y);
            always @(sel or a or b) begin
                y = sel ? a : b;
            end
        endmodule
    )", "cdc_edge_debug_mux");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    CHECK(classifier.getFFNodes().empty());
    CHECK(classifier.getErrors().empty());
}

TEST_CASE("CDC EdgeCases: empty module produces no crossings", "[cdc][edge]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module empty_mod(input logic clk, rst_n);
        endmodule
    )", "cdc_edge_empty");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();
    CHECK(classifier.getFFNodes().empty());

    ConnectivityBuilder connectivity(*compiled.compilation, classifier.getFFNodes());
    connectivity.analyze();
    CHECK(connectivity.getEdges().empty());

    CrossingDetector detector(connectivity.getEdges(), db);
    detector.analyze();
    CHECK(detector.getCrossings().empty());
}

TEST_CASE("CDC EdgeCases: crossing ids separate VIOLATION and CAUTION classes", "[cdc][edge]") {
    ClockDatabase db;

    auto srcA = std::make_unique<ClockSource>();
    srcA->name = "clk_a";
    auto* a = db.addSource(std::move(srcA));
    auto srcB = std::make_unique<ClockSource>();
    srcB->name = "clk_b";
    auto* b = db.addSource(std::move(srcB));
    auto srcC = std::make_unique<ClockSource>();
    srcC->name = "clk_c";
    srcC->master = a;
    srcC->divide_by = 2;
    auto* c = db.addSource(std::move(srcC));

    db.relationships.push_back({a, b, DomainRelationship::Type::Asynchronous});
    db.relationships.push_back({a, c, DomainRelationship::Type::Divided});

    auto* domA = db.findOrCreateDomain(a, Edge::Posedge);
    auto* domB = db.findOrCreateDomain(b, Edge::Posedge);
    auto* domC = db.findOrCreateDomain(c, Edge::Posedge);

    FFNode ffA{"top.q_a", domA, nullptr, {}};
    FFNode ffB{"top.q_b", domB, nullptr, {}};
    FFNode ffC{"top.q_c", domC, nullptr, {}};

    std::vector<FFEdge> edges;
    edges.push_back({&ffA, &ffB, {}, SyncType::None});
    edges.push_back({&ffA, &ffC, {}, SyncType::None});

    CrossingDetector detector(edges, db);
    detector.analyze();
    auto crossings = detector.getCrossings();

    REQUIRE(crossings.size() == 2);
    bool hasViolation = false;
    bool hasCaution = false;
    for (const auto& crossing : crossings) {
        if (crossing.id.rfind("VIOLATION-", 0) == 0)
            hasViolation = true;
        if (crossing.id.rfind("CAUTION-", 0) == 0)
            hasCaution = true;
    }
    CHECK(hasViolation);
    CHECK(hasCaution);
}
