#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"

#include <algorithm>

using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_p2");
}

// Full pipeline helper
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

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Feature 1: Inter-module connectivity — recursive child instance traversal
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: hierarchy crossing detected via port connection", "[phase2][hierarchy]") {
    auto compilation = compileSV(R"(
        module inner_a (input logic clk_a, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module inner_b (input logic clk_b, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module top_hierarchy (input logic clk_a, clk_b, rst_n, input logic data_in);
            logic wire_ab;
            inner_a u_a (.clk_a(clk_a), .rst_n(rst_n), .d(data_in), .q(wire_ab));
            inner_b u_b (.clk_b(clk_b), .rst_n(rst_n), .d(wire_ab), .q());
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // FF classifier must find FFs inside child instances
    auto& ffs = pipeline.classifier->getFFNodes();
    REQUIRE(ffs.size() >= 2);

    // FFs should be in different clock domains
    bool found_different_domain = false;
    for (size_t i = 0; i < ffs.size(); i++) {
        for (size_t j = i + 1; j < ffs.size(); j++) {
            if (ffs[i]->domain && ffs[j]->domain &&
                !ffs[i]->domain->isSameDomain(*ffs[j]->domain)) {
                found_different_domain = true;
            }
        }
    }
    CHECK(found_different_domain);

    // Connectivity must find the cross-domain edge through wire_ab
    CHECK(pipeline.edges.size() >= 1);

    // Crossing detector must flag the async crossing
    REQUIRE(pipeline.crossings.size() >= 1);
    CHECK(pipeline.crossings[0].severity == Severity::High);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Feature 2: Clock propagation — collectSensitivityClocks
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: sensitivity clocks collected from always_ff blocks", "[phase2][clock]") {
    auto compilation = compileSV(R"(
        module sens_test (
            input logic clk_fast,
            input logic clk_slow,
            input logic rst_n,
            input logic d
        );
            logic q1, q2;
            always_ff @(posedge clk_fast or negedge rst_n)
                if (!rst_n) q1 <= 1'b0; else q1 <= d;
            always_ff @(posedge clk_slow or negedge rst_n)
                if (!rst_n) q2 <= 1'b0; else q2 <= q1;
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compilation, db);
    analyzer.analyze();

    // After analysis, both clk_fast and clk_slow should have clock nets
    bool found_fast = false, found_slow = false;
    for (auto& net : db.nets) {
        if (net->hier_path.find("clk_fast") != std::string::npos)
            found_fast = true;
        if (net->hier_path.find("clk_slow") != std::string::npos)
            found_slow = true;
    }
    CHECK(found_fast);
    CHECK(found_slow);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Feature 3: Port connection expression resolution
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: same clock through different port names recognized as same domain", "[phase2][port_resolve]") {
    auto compilation = compileSV(R"(
        module sub_module (input logic proc_clk, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge proc_clk or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module top_same_source (input logic sys_clk, input logic rst_n, input logic data_in);
            logic q;
            sub_module u_sub (.proc_clk(sys_clk), .rst_n(rst_n), .d(data_in), .q(q));
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // The sub_module FF uses proc_clk which is connected to sys_clk.
    // These should be recognized as the same clock source / domain.
    // Therefore: no CDC crossings.
    CHECK(pipeline.crossings.empty());

    // The FF inside sub_module should be found
    auto& ffs = pipeline.classifier->getFFNodes();
    REQUIRE(ffs.size() >= 1);

    // The FF's domain should trace back to sys_clk's source
    bool domain_traces_to_sys_clk = false;
    for (auto& ff : ffs) {
        if (ff->domain && ff->domain->source) {
            if (ff->domain->source->origin_signal == "sys_clk" ||
                ff->domain->source->name == "sys_clk") {
                domain_traces_to_sys_clk = true;
            }
        }
    }
    CHECK(domain_traces_to_sys_clk);
}

TEST_CASE("Phase2: port expression resolution distinguishes actual clock connections", "[phase2][port_resolve]") {
    // non-clock port name "proc_clk" should NOT inherit clock from parent
    // just because it matches isClockName — it should resolve the actual expression
    auto compilation = compileSV(R"(
        module child_mod (input logic proc_clk, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge proc_clk or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module top_two_clocks (
            input logic clk_a, input logic clk_b, input logic rst_n, input logic data_in
        );
            logic q1, q2;
            child_mod u1 (.proc_clk(clk_a), .rst_n(rst_n), .d(data_in), .q(q1));
            child_mod u2 (.proc_clk(clk_b), .rst_n(rst_n), .d(q1), .q(q2));
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // u1 uses clk_a, u2 uses clk_b — different domains
    auto& ffs = pipeline.classifier->getFFNodes();
    REQUIRE(ffs.size() >= 2);

    bool found_different_domain = false;
    for (size_t i = 0; i < ffs.size(); i++) {
        for (size_t j = i + 1; j < ffs.size(); j++) {
            if (ffs[i]->domain && ffs[j]->domain &&
                !ffs[i]->domain->isSameDomain(*ffs[j]->domain)) {
                found_different_domain = true;
            }
        }
    }
    CHECK(found_different_domain);

    // Should detect a crossing between clk_a and clk_b domains
    CHECK(pipeline.crossings.size() >= 1);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Feature 4: FF output map scoping per instance
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: FF output map does not collide across instances", "[phase2][ff_map]") {
    // Both inner_a and inner_b have output 'q' — must not collide
    auto compilation = compileSV(R"(
        module inner_a (input logic clk_a, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module inner_b (input logic clk_b, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module top_collision (input logic clk_a, clk_b, rst_n, input logic data_in);
            logic wire_ab;
            inner_a u_a (.clk_a(clk_a), .rst_n(rst_n), .d(data_in), .q(wire_ab));
            inner_b u_b (.clk_b(clk_b), .rst_n(rst_n), .d(wire_ab), .q());
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Both instances have 'q' — they should be separate FFNodes
    auto& ffs = pipeline.classifier->getFFNodes();
    REQUIRE(ffs.size() >= 2);

    // Each FF should have a unique hier_path
    std::vector<std::string> paths;
    for (auto& ff : ffs) {
        paths.push_back(ff->hier_path);
    }
    // No duplicates
    auto sorted = paths;
    std::sort(sorted.begin(), sorted.end());
    auto last = std::unique(sorted.begin(), sorted.end());
    CHECK(last == sorted.end());
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Feature 5: FFNode::fanin_signals populated during FF classification
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: FFNode fanin_signals populated", "[phase2][fanin]") {
    auto compilation = compileSV(R"(
        module fanin_test (
            input  logic clk,
            input  logic rst_n,
            input  logic data_in
        );
            logic stage1, stage2;
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    stage1 <= 1'b0;
                    stage2 <= 1'b0;
                end else begin
                    stage1 <= data_in;
                    stage2 <= stage1;
                end
            end
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer clock_analyzer(*compilation, db);
    clock_analyzer.analyze();

    FFClassifier classifier(*compilation, db);
    classifier.analyze();

    auto& ffs = classifier.getFFNodes();
    REQUIRE(ffs.size() >= 2);

    // Find stage1 and stage2
    FFNode* stage1_ff = nullptr;
    FFNode* stage2_ff = nullptr;
    for (auto& ff : ffs) {
        if (ff->hier_path.find("stage1") != std::string::npos)
            stage1_ff = ff.get();
        if (ff->hier_path.find("stage2") != std::string::npos)
            stage2_ff = ff.get();
    }

    REQUIRE(stage1_ff != nullptr);
    REQUIRE(stage2_ff != nullptr);

    // stage1's fanin should include data_in
    CHECK(!stage1_ff->fanin_signals.empty());
    bool has_data_in = std::find(stage1_ff->fanin_signals.begin(),
                                  stage1_ff->fanin_signals.end(),
                                  "data_in") != stage1_ff->fanin_signals.end();
    CHECK(has_data_in);

    // stage2's fanin should include stage1
    CHECK(!stage2_ff->fanin_signals.empty());
    bool has_stage1 = std::find(stage2_ff->fanin_signals.begin(),
                                 stage2_ff->fanin_signals.end(),
                                 "stage1") != stage2_ff->fanin_signals.end();
    CHECK(has_stage1);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Feature 6: FFEdge::comb_path populated during connectivity
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: FFEdge comb_path populated for direct FF-to-FF", "[phase2][comb_path]") {
    auto compilation = compileSV(R"(
        module comb_path_test (
            input  logic clk_a, clk_b,
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

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Should have at least one edge
    REQUIRE(pipeline.edges.size() >= 1);

    // The edge should have a non-empty comb_path
    bool found_path = false;
    for (auto& edge : pipeline.edges) {
        if (!edge.comb_path.empty()) {
            found_path = true;
            // The comb_path should include q_a (the signal connecting them)
            bool has_q_a = std::find(edge.comb_path.begin(),
                                      edge.comb_path.end(),
                                      "q_a") != edge.comb_path.end();
            CHECK(has_q_a);
        }
    }
    CHECK(found_path);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Integration: full hierarchy crossing with correct clock resolution
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: integration — hierarchy with renamed clocks, no false crossing", "[phase2][integration]") {
    // sys_clk is passed as proc_clk to both sub-modules — same source, no crossing
    auto compilation = compileSV(R"(
        module sub_a (input logic proc_clk, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge proc_clk or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module sub_b (input logic proc_clk, input logic rst_n, input logic d, output logic q);
            always_ff @(posedge proc_clk or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module top_no_false (input logic sys_clk, input logic rst_n, input logic data_in);
            logic mid;
            sub_a u_a (.proc_clk(sys_clk), .rst_n(rst_n), .d(data_in), .q(mid));
            sub_b u_b (.proc_clk(sys_clk), .rst_n(rst_n), .d(mid), .q());
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Both FFs should be in the SAME domain (sys_clk) — no crossings
    CHECK(pipeline.crossings.empty());
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Feature 7: Inter-module wire crossing — child FF output read by top always_ff
// Regression test for: wire_map not checked directly in findFFByName
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST_CASE("Phase2: inter-module wire — child FF output read directly by top-level always_ff", "[phase2][intermodule]") {
    // inner_mod u_a drives wire_ab via its output port q.
    // The top-level always_ff (clk_b domain) reads wire_ab directly (not via a port).
    // Before the fix, findFFByName("wire_ab", ...) skipped wire_map and returned nullptr,
    // producing 0 edges and 0 violations. After the fix it must find u_a.q and emit 1 edge.
    auto compilation = compileSV(R"(
        module inner_mod (input logic clk_a, rst_n, d, output logic q);
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q <= 1'b0; else q <= d;
        endmodule

        module top (input logic clk_a, clk_b, rst_n, d);
            logic wire_ab;
            inner_mod u_a (.clk_a(clk_a), .rst_n(rst_n), .d(d), .q(wire_ab));
            logic q_b;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 1'b0; else q_b <= wire_ab;
        endmodule
    )");

    CDCPipeline pipeline;
    pipeline.run(*compilation);

    // Must detect both FFs: u_a.q (clk_a) and top.q_b (clk_b)
    auto& ffs = pipeline.classifier->getFFNodes();
    REQUIRE(ffs.size() >= 2);

    // Must detect the FF-to-FF edge across the wire
    REQUIRE(pipeline.edges.size() >= 1);

    // Must flag a CDC violation (clk_a -> clk_b crossing)
    REQUIRE(pipeline.crossings.size() >= 1);
    CHECK(pipeline.crossings[0].severity == Severity::High);
}
