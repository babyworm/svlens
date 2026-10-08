#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"

#include <algorithm>

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

TEST_CASE("CDC CrossingDetector: unsynchronized bus advice requires a coherent CDC scheme", "[cdc][crossing][wide]") {
    ClockDatabase db;
    auto sourceA = std::make_unique<ClockSource>();
    sourceA->name = "clk_a";
    auto* clkA = db.addSource(std::move(sourceA));
    auto sourceB = std::make_unique<ClockSource>();
    sourceB->name = "clk_b";
    auto* clkB = db.addSource(std::move(sourceB));
    FFNode ffA;
    ffA.hier_path = "top.source_q";
    ffA.width = 8;
    ffA.domain = db.findOrCreateDomain(clkA, Edge::Posedge);
    FFNode ffB;
    ffB.hier_path = "top.dest_q";
    ffB.width = 8;
    ffB.domain = db.findOrCreateDomain(clkB, Edge::Posedge);
    std::vector<FFEdge> edges{{&ffA, &ffB}};
    CrossingDetector busDetector(edges, db);
    busDetector.analyze();
    const auto busCrossings = busDetector.getCrossings();
    REQUIRE(busCrossings.size() == 1);
    CHECK(busCrossings[0].category == ViolationCategory::Violation);
    CHECK(busCrossings[0].recommendation.find("multi-bit") != std::string::npos);
    CHECK(busCrossings[0].recommendation.find("handshake") != std::string::npos);
    CHECK(busCrossings[0].recommendation.find("Insert 2-FF synchronizer") == std::string::npos);

    ffB.width = 1;
    CrossingDetector bitDetector(edges, db);
    bitDetector.analyze();
    const auto bitCrossings = bitDetector.getCrossings();
    REQUIRE(bitCrossings.size() == 1);
    CHECK(bitCrossings[0].category == ViolationCategory::Violation);
    CHECK(bitCrossings[0].recommendation.find("Review the clock relationship") != std::string::npos);
    CHECK(bitCrossings[0].recommendation.find("2-FF") != std::string::npos);
    CHECK(bitCrossings[0].recommendation.find("pulse/toggle") != std::string::npos);
    CHECK(bitCrossings[0].recommendation.find("Insert 2-FF synchronizer") == std::string::npos);
}

TEST_CASE("CDC CrossingDetector: SDC false path excludes timing, not synchronization",
          "[cdc][crossing][sdc][false_path]") {
    ClockDatabase db;
    auto sourceA = std::make_unique<ClockSource>();
    sourceA->name = "clk_a";
    auto* clkA = db.addSource(std::move(sourceA));
    auto sourceB = std::make_unique<ClockSource>();
    sourceB->name = "clk_b";
    auto* clkB = db.addSource(std::move(sourceB));
    FFNode ffA;
    ffA.hier_path = "top.q_a";
    ffA.domain = db.findOrCreateDomain(clkA, Edge::Posedge);
    FFNode ffB;
    ffB.hier_path = "top.q_b";
    ffB.domain = db.findOrCreateDomain(clkB, Edge::Posedge);
    std::vector<FFEdge> edges{{&ffA, &ffB}, {&ffB, &ffA}};

    CrossingDetector detector(edges, db);
    detector.setFalsePaths({{"clk_a", "clk_b", true}});
    detector.analyze();
    const auto crossings = detector.getCrossings();
    REQUIRE(crossings.size() == 2);
    CHECK(crossings[0].category == ViolationCategory::Violation);
    CHECK(crossings[0].severity == Severity::High);
    CHECK(crossings[0].waive_reason.empty());
    CHECK(crossings[0].rationale.find("set_false_path") != std::string::npos);
    CHECK(crossings[1].category == ViolationCategory::Violation);
    CHECK(crossings[1].rationale.find("set_false_path") == std::string::npos);

    CrossingDetector pinDetector(edges, db);
    pinDetector.setFalsePaths({{"clk_a", "clk_b", false}});
    pinDetector.analyze();
    const auto pinCrossings = pinDetector.getCrossings();
    REQUIRE(pinCrossings.size() == 2);
    CHECK(pinCrossings[0].category == ViolationCategory::Violation);
    CHECK(pinCrossings[0].rationale.find("set_false_path") == std::string::npos);
}

TEST_CASE("CDC CrossingDetector: declared max delay is context, not measured CDC safety",
          "[cdc][crossing][sdc][max_delay]") {
    ClockDatabase db;
    auto sourceA = std::make_unique<ClockSource>();
    sourceA->name = "clk_a";
    auto* clkA = db.addSource(std::move(sourceA));
    auto sourceB = std::make_unique<ClockSource>();
    sourceB->name = "clk_b";
    auto* clkB = db.addSource(std::move(sourceB));
    FFNode ffA;
    ffA.hier_path = "top.q_a";
    ffA.domain = db.findOrCreateDomain(clkA, Edge::Posedge);
    FFNode ffB;
    ffB.hier_path = "top.q_b";
    ffB.domain = db.findOrCreateDomain(clkB, Edge::Posedge);
    std::vector<FFEdge> edges{{&ffA, &ffB}, {&ffB, &ffA}};
    CrossingDetector detector(edges, db);
    detector.setMaxDelays({{5.0, "clk_a", "clk_b", true}});
    detector.analyze();
    const auto crossings = detector.getCrossings();
    REQUIRE(crossings.size() == 2);
    CHECK(crossings[0].category == ViolationCategory::Violation);
    CHECK(crossings[0].sdc_max_delay_constraint_ns == 5.0);
    CHECK(crossings[0].sdc_max_delay_datapath_only);
    CHECK_FALSE(crossings[0].sdc_max_delay_ambiguous);
    CHECK(crossings[0].rationale.find("declared") != std::string::npos);
    CHECK(crossings[1].category == ViolationCategory::Violation);
    CHECK_FALSE(crossings[1].sdc_max_delay_constraint_ns);

    CrossingDetector ambiguous(edges, db);
    ambiguous.setMaxDelays({{5.0, "clk_a", "clk_b", true}, {7.0, "clk_a", "clk_b", true}});
    ambiguous.analyze();
    const auto ambiguousCrossings = ambiguous.getCrossings();
    REQUIRE(ambiguousCrossings.size() == 2);
    CHECK(ambiguousCrossings[0].category == ViolationCategory::Violation);
    CHECK_FALSE(ambiguousCrossings[0].sdc_max_delay_constraint_ns);
    CHECK(ambiguousCrossings[0].sdc_max_delay_ambiguous);

    CrossingDetector combined(edges, db);
    combined.setFalsePaths({{"clk_a", "clk_b", true}});
    combined.setMaxDelays({{5.0, "clk_a", "clk_b", true}});
    combined.analyze();
    const auto combinedCrossings = combined.getCrossings();
    REQUIRE(combinedCrossings.size() == 2);
    CHECK(combinedCrossings[0].category == ViolationCategory::Violation);
    CHECK(combinedCrossings[0].sdc_false_path);
    CHECK(combinedCrossings[0].sdc_max_delay_constraint_ns == 5.0);
    CHECK(combinedCrossings[0].rationale.find("precedence") != std::string::npos);

    auto duplicateA = std::make_unique<ClockSource>();
    duplicateA->name = "clk_a";
    db.addSource(std::move(duplicateA));
    CrossingDetector duplicateName(edges, db);
    duplicateName.setMaxDelays({{5.0, "clk_a", "clk_b", true}});
    duplicateName.analyze();
    const auto duplicateCrossings = duplicateName.getCrossings();
    REQUIRE(duplicateCrossings.size() == 2);
    CHECK_FALSE(duplicateCrossings[0].sdc_max_delay_constraint_ns);
    CHECK(duplicateCrossings[0].sdc_max_delay_ambiguous);
}

TEST_CASE("CDC CrossingDetector: exclusive clock groups require CDC mode review", "[cdc][crossing][sdc][exclusive]") {
    for (const auto kind :
         {DomainRelationship::Type::PhysicallyExclusive, DomainRelationship::Type::LogicallyExclusive}) {
        ClockDatabase db;
        auto sourceA = std::make_unique<ClockSource>();
        sourceA->name = "clk_a";
        auto* clkA = db.addSource(std::move(sourceA));
        auto sourceB = std::make_unique<ClockSource>();
        sourceB->name = "clk_b";
        auto* clkB = db.addSource(std::move(sourceB));
        db.relationships.push_back({clkA, clkB, kind, /*sdc_declared=*/true});
        FFNode ffA;
        ffA.hier_path = "top.q_a";
        ffA.domain = db.findOrCreateDomain(clkA, Edge::Posedge);
        FFNode ffB;
        ffB.hier_path = "top.q_b";
        ffB.domain = db.findOrCreateDomain(clkB, Edge::Posedge);
        std::vector<FFEdge> edges{{&ffA, &ffB}};

        CrossingDetector detector(edges, db);
        detector.analyze();
        const auto crossings = detector.getCrossings();
        REQUIRE(crossings.size() == 1);
        CHECK(crossings[0].category == ViolationCategory::Caution);
        CHECK(crossings[0].severity == Severity::Medium);
        CHECK(crossings[0].waive_reason.empty());
        CHECK(crossings[0].rationale.find("mode") != std::string::npos);
    }
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

TEST_CASE("CDC Connectivity: same-named sibling data port does not invent a crossing", "[cdc][connectivity][scope]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module source_ip(input logic clk_i, d_i, output logic out_o);
            logic wdata;
            always_ff @(posedge clk_i) wdata <= d_i;
            assign out_o = wdata;
        endmodule
        module sink_ip(input logic clk_i, wdata, output logic q_o);
            always_ff @(posedge clk_i) q_o <= wdata;
        endmodule
        module sibling_data_top(input logic clk_usb_i, clk_main_i, d_i,
                                unrelated_i, output logic q_good, q_bad);
            logic wdata;
            source_ip u_usb(.clk_i(clk_usb_i), .d_i(d_i), .out_o(wdata));
            sink_ip u_good(.clk_i(clk_main_i), .wdata(wdata), .q_o(q_good));
            sink_ip u_bad(.clk_i(clk_main_i), .wdata(unrelated_i), .q_o(q_bad));
        endmodule
    )",
                                                    "sibling_data_top");
    REQUIRE(compiled);

    CDCPipeline pipeline;
    pipeline.run(*compiled.compilation);
    bool linked = false;
    bool unrelated = false;
    for (const auto& edge : pipeline.edges) {
        if (!edge.source || !edge.dest)
            continue;
        if (edge.source->hier_path == "sibling_data_top.u_usb.wdata" &&
            edge.dest->hier_path == "sibling_data_top.u_good.q_o")
            linked = true;
        if (edge.source->hier_path == "sibling_data_top.u_usb.wdata" &&
            edge.dest->hier_path == "sibling_data_top.u_bad.q_o")
            unrelated = true;
    }
    CHECK(linked);
    CHECK_FALSE(unrelated);
}

TEST_CASE("CDC crossing: fast-to-slow related clocks carry data-stability review hint", "[cdc][timing][cdc08]") {
    ClockDatabase db;
    auto fast = std::make_unique<ClockSource>();
    fast->name = "clk_fast";
    fast->period_ns = 5.0;
    auto* fastSource = db.addSource(std::move(fast));
    auto slow = std::make_unique<ClockSource>();
    slow->name = "clk_slow";
    slow->period_ns = 10.0;
    auto* slowSource = db.addSource(std::move(slow));
    db.relationships.push_back({fastSource, slowSource, DomainRelationship::Type::Divided});

    FFNode fastFf;
    fastFf.hier_path = "top.fast_q";
    fastFf.domain = db.findOrCreateDomain(fastSource, Edge::Posedge);
    FFNode slowFf;
    slowFf.hier_path = "top.slow_q";
    slowFf.domain = db.findOrCreateDomain(slowSource, Edge::Posedge);
    std::vector<FFEdge> edges{{&fastFf, &slowFf}};
    CrossingDetector detector(edges, db);
    detector.analyze();
    const auto crossings = detector.getCrossings();
    REQUIRE(crossings.size() == 1);
    CHECK(crossings[0].category == ViolationCategory::Caution);
    CHECK(crossings[0].recommendation.find("Ac_cdc08") != std::string::npos);

    std::vector<FFEdge> reverse{{&slowFf, &fastFf}};
    CrossingDetector reverseDetector(reverse, db);
    reverseDetector.analyze();
    const auto reverseCrossings = reverseDetector.getCrossings();
    REQUIRE(reverseCrossings.size() == 1);
    CHECK(reverseCrossings[0].recommendation.find("Ac_cdc08") == std::string::npos);

    fastSource->period_ns.reset();
    CrossingDetector unknownPeriodDetector(edges, db);
    unknownPeriodDetector.analyze();
    const auto unknownPeriodCrossings = unknownPeriodDetector.getCrossings();
    REQUIRE(unknownPeriodCrossings.size() == 1);
    CHECK(unknownPeriodCrossings[0].recommendation.find("Ac_cdc08") == std::string::npos);
}

TEST_CASE("CDC crossing: guarded wide capture carries Ac_cdc08 review evidence", "[cdc][timing][cdc08]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module guarded_bus(input logic clk_a, clk_b, rst_n, en_i,
                           input logic [7:0] d_i,
                           output logic [7:0] q_o, q_direct);
            logic [7:0] source_q;
            always_ff @(posedge clk_a) source_q <= d_i;
            always_ff @(posedge clk_b) q_direct <= source_q;
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_o <= '0;
                else if (en_i) q_o <= source_q;
            end
        endmodule
    )",
                                                    "guarded_bus");
    REQUIRE(compiled);

    CDCPipeline pipeline;
    pipeline.run(*compiled.compilation);
    bool found = false;
    bool foundDirect = false;
    for (const auto& crossing : pipeline.crossings) {
        if (crossing.dest_signal == "guarded_bus.q_direct") {
            foundDirect = true;
            CHECK(crossing.recommendation.find("Ac_cdc08") == std::string::npos);
            CHECK(crossing.capture_conditions.empty());
        }
        if (crossing.source_signal != "guarded_bus.source_q" || crossing.dest_signal != "guarded_bus.q_o")
            continue;
        found = true;
        CHECK(crossing.category == ViolationCategory::Violation);
        CHECK(crossing.recommendation.find("Ac_cdc08") != std::string::npos);
        CHECK(std::find(crossing.capture_conditions.begin(), crossing.capture_conditions.end(), "en_i") !=
              crossing.capture_conditions.end());
    }
    CHECK(found);
    CHECK(foundDirect);
}
