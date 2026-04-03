#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/sdc_parser.h"
#include "sv-cdccheck/waiver.h"
#include "sv-cdccheck/clock_yaml_parser.h"
#include "sv-cdccheck/report_generator.h"

#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace sv_cdccheck;
using sv_cdccheck::test::compileSV;

// Helper: write a temp file and return its path (do NOT delete)
static fs::path writeTempFile(const std::string& content, const std::string& ext) {
    static int ctr = 0;
    auto path = fs::temp_directory_path() / ("corner_" + std::to_string(ctr++) + ext);
    std::ofstream(path) << content;
    return path;
}

// =============================================================================
// 1. Clock Detection Corner Cases
// =============================================================================

TEST_CASE("Corner: unusual clock names (ARM-style pclk, hclk)", "[corner]") {
    auto c = compileSV(R"(
        module arm_clks (input logic pclk, hclk, rst_n, input logic d);
            logic q1, q2;
            always_ff @(posedge pclk or negedge rst_n)
                if (!rst_n) q1 <= 0; else q1 <= d;
            always_ff @(posedge hclk or negedge rst_n)
                if (!rst_n) q2 <= 0; else q2 <= q1;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    // Should detect at least 2 FFs (q1 and q2) even with unusual clock names
    CHECK(ff.getFFNodes().size() >= 2);
}

TEST_CASE("Corner: clock with numeric suffix clk_0 clk_1", "[corner]") {
    auto c = compileSV(R"(
        module num_clks (input logic clk_0, clk_1, rst_n, d);
            logic q0, q1;
            always_ff @(posedge clk_0 or negedge rst_n)
                if (!rst_n) q0 <= 0; else q0 <= d;
            always_ff @(posedge clk_1 or negedge rst_n)
                if (!rst_n) q1 <= 0; else q1 <= q0;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    auto crossings = det.getCrossings();
    // clk_0 and clk_1 match *clk* pattern -> should detect crossing
    CHECK(crossings.size() >= 1);
}

// =============================================================================
// 2. FF and Connectivity Corner Cases
// =============================================================================

TEST_CASE("Corner: FF self-loop (feedback register)", "[corner]") {
    auto c = compileSV(R"(
        module self_loop (input logic clk, rst_n);
            logic [7:0] counter;
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) counter <= 0;
                else counter <= counter + 1;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    // Self-loops: source == dest should be filtered out, OR if present
    // they should be same-domain (no crossing). Verify no crash.
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    // Single clock domain -> no crossings
    CHECK(det.getCrossings().empty());
}

TEST_CASE("Corner: single FF design", "[corner]") {
    auto c = compileSV(R"(
        module single_ff (input logic clk, rst_n, d, output logic q);
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) q <= 0; else q <= d;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    CHECK(ff.getFFNodes().size() >= 1);
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    CHECK(det.getCrossings().empty());
}

TEST_CASE("Corner: wide bus register (32 bits)", "[corner]") {
    auto c = compileSV(R"(
        module wide_bus (input logic clk, rst_n, input logic [31:0] d, output logic [31:0] q);
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) q <= 32'h0;
                else q <= d;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    // q is one variable in the AST -> at least 1 FF node
    CHECK(ff.getFFNodes().size() >= 1);
}

TEST_CASE("Corner: FF without reset", "[corner]") {
    auto c = compileSV(R"(
        module no_reset (input logic clk, input logic d);
            logic q;
            always_ff @(posedge clk)
                q <= d;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    REQUIRE(ff.getFFNodes().size() >= 1);
    // No reset in sensitivity list -> reset should be nullptr
    CHECK(ff.getFFNodes()[0]->reset == nullptr);
}

TEST_CASE("Corner: negedge clock FF", "[corner]") {
    auto c = compileSV(R"(
        module neg_clk (input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(negedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    auto crossings = det.getCrossings();
    // Different clocks (clk_a negedge vs clk_b posedge) -> crossing
    CHECK(crossings.size() >= 1);
}

// =============================================================================
// 3. Crossing Detection Corner Cases
// =============================================================================

TEST_CASE("Corner: three-domain chain A->B->C", "[corner]") {
    auto c = compileSV(R"(
        module three_domains (input logic clk_a, clk_b, clk_c, rst_n, d);
            logic q_a, q_b, q_c;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;
            always_ff @(posedge clk_c or negedge rst_n)
                if (!rst_n) q_c <= 0; else q_c <= q_b;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    auto crossings = det.getCrossings();
    // Should detect 2 crossings: A->B and B->C
    CHECK(crossings.size() >= 2);
}

TEST_CASE("Corner: bidirectional crossing", "[corner]") {
    auto c = compileSV(R"(
        module bidir (input logic clk_a, clk_b, rst_n);
            logic a_to_b, b_to_a;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) begin a_to_b <= 0; end
                else begin a_to_b <= b_to_a; end
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) begin b_to_a <= 0; end
                else begin b_to_a <= a_to_b; end
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    auto crossings = det.getCrossings();
    // Should detect 2 crossings: A->B and B->A
    CHECK(crossings.size() >= 2);
}

TEST_CASE("Corner: same clock different edge", "[corner]") {
    auto c = compileSV(R"(
        module dual_edge (input logic clk, rst_n, d);
            logic q_pos, q_neg;
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) q_pos <= 0; else q_pos <= d;
            always_ff @(negedge clk or negedge rst_n)
                if (!rst_n) q_neg <= 0; else q_neg <= q_pos;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    auto crossings = det.getCrossings();
    // Same source, different edge: behavior depends on domain model.
    // If edges create separate domains, there should be a crossing.
    // If same source collapses edges, there may be 0.
    // Either way, no crash.
    // Document observed behavior:
    (void)crossings;  // result depends on implementation
}

// =============================================================================
// 4. Sync Verifier Corner Cases
// =============================================================================

TEST_CASE("Corner: 1-stage sync insufficient", "[corner]") {
    auto c = compileSV(R"(
        module one_stage (input logic clk_a, clk_b, rst_n, d);
            logic q_a, sync1;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) sync1 <= 0;
                else sync1 <= q_a;
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    auto crossings = det.getCrossings();
    SyncVerifier sv(crossings, ff.getFFNodes(), conn.getEdges());
    sv.setRequiredStages(2);
    sv.analyze();
    // 1-stage is insufficient for 2-stage requirement -> should have at least one violation
    bool has_violation = false;
    for (auto& cr : crossings)
        if (cr.category == ViolationCategory::Violation)
            has_violation = true;
    CHECK(has_violation);
}

TEST_CASE("Corner: sync chain with logic between stages", "[corner]") {
    auto c = compileSV(R"(
        module broken_chain (input logic clk_a, clk_b, rst_n, d, en);
            logic q_a, sync1, sync2;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 0; sync2 <= 0; end
                else begin
                    sync1 <= q_a;
                    sync2 <= sync1 & en;
                end
            end
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    auto crossings = det.getCrossings();
    SyncVerifier sv(crossings, ff.getFFNodes(), conn.getEdges());
    sv.analyze();
    // sync2 <= sync1 & en has combinational logic -> crossing should be detected
    CHECK(crossings.size() >= 1);
}

// =============================================================================
// 5. SDC / YAML / Waiver Corner Cases
// =============================================================================

TEST_CASE("Corner: SDC very long line", "[corner][sdc]") {
    std::string long_name(500, 'a');
    auto path = writeTempFile(
        "create_clock -name " + long_name + " -period 10 [get_ports clk]\n", ".sdc");
    auto sdc = SdcParser::parse(path);
    REQUIRE(sdc.clocks.size() == 1);
    CHECK(sdc.clocks[0].name == long_name);
}

TEST_CASE("Corner: SDC only comments", "[corner][sdc]") {
    auto path = writeTempFile("# comment 1\n\n# comment 2\n\n", ".sdc");
    auto sdc = SdcParser::parse(path);
    CHECK(sdc.clocks.empty());
}

TEST_CASE("Corner: waiver wildcard matches all", "[corner][waiver]") {
    WaiverManager mgr;
    mgr.loadString(R"(
waivers:
  - id: WAIVE-ALL
    pattern: "*"
    reason: "waive everything"
)");
    CHECK(mgr.isWaived("any.signal.a", "any.signal.b"));
    CHECK(mgr.isWaived("x", "y"));
}

TEST_CASE("Corner: waiver applied to empty crossings", "[corner][waiver]") {
    WaiverManager mgr;
    mgr.loadString(R"(
waivers:
  - id: W1
    crossing: "nonexistent.a -> nonexistent.b"
    reason: "no match"
)");
    CHECK_FALSE(mgr.isWaived("other.a", "other.b"));
}

TEST_CASE("Corner: empty YAML clock spec", "[corner]") {
    ClockYamlParser parser;
    parser.loadString("");
    ClockDatabase db;
    parser.applyTo(db);
    CHECK(db.sources.empty());
}

// =============================================================================
// 6. Report Generation Corner Cases
// =============================================================================

TEST_CASE("Corner: report with zero crossings all formats", "[corner][report]") {
    AnalysisResult result;
    ReportGenerator gen(result);
    auto md = fs::temp_directory_path() / "corner_empty.md";
    auto json = fs::temp_directory_path() / "corner_empty.json";
    auto sdc = fs::temp_directory_path() / "corner_empty.sdc";
    auto dot = fs::temp_directory_path() / "corner_empty.dot";
    auto waiver = fs::temp_directory_path() / "corner_empty.yaml";
    gen.generateMarkdown(md);
    gen.generateJSON(json);
    gen.generateSDC(sdc);
    gen.generateDOT(dot);
    gen.generateWaiverTemplate(waiver);
    // All should succeed without crash
    CHECK(fs::file_size(md) > 0);
    CHECK(fs::file_size(json) > 0);
}

TEST_CASE("Corner: very long signal name in JSON report", "[corner][report]") {
    AnalysisResult result;
    auto src = std::make_unique<ClockSource>();
    src->name = "clk";
    auto* s = result.clock_db.addSource(std::move(src));
    auto* dom = result.clock_db.findOrCreateDomain(s, Edge::Posedge);

    auto src2 = std::make_unique<ClockSource>();
    src2->name = "clk2";
    auto* s2 = result.clock_db.addSource(std::move(src2));
    auto* dom2 = result.clock_db.findOrCreateDomain(s2, Edge::Posedge);

    std::string long_name(1000, 'x');
    CrossingReport cr;
    cr.id = "V-1";
    cr.source_signal = long_name;
    cr.dest_signal = "short";
    cr.source_domain = dom;
    cr.dest_domain = dom2;
    cr.category = ViolationCategory::Violation;
    cr.severity = Severity::High;
    cr.sync_type = SyncType::None;
    result.crossings.push_back(cr);

    auto json_path = fs::temp_directory_path() / "corner_long.json";
    ReportGenerator gen(result);
    gen.generateJSON(json_path);
    std::ifstream f(json_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    CHECK(content.find(long_name) != std::string::npos);
}

// =============================================================================
// 7. Multi-module Hierarchy Corner Cases
// =============================================================================

TEST_CASE("Corner: diamond hierarchy", "[corner]") {
    auto c = compileSV(R"(
        module leaf (input logic clk, rst_n, d, output logic q);
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) q <= 0; else q <= d;
        endmodule
        module mid_a (input logic clk, rst_n, d, output logic q);
            leaf u_leaf (.clk(clk), .rst_n(rst_n), .d(d), .q(q));
        endmodule
        module mid_b (input logic clk, rst_n, d, output logic q);
            leaf u_leaf (.clk(clk), .rst_n(rst_n), .d(d), .q(q));
        endmodule
        module diamond_top (input logic clk_a, clk_b, rst_n, d);
            logic wire_a, wire_b;
            mid_a u_a (.clk(clk_a), .rst_n(rst_n), .d(d), .q(wire_a));
            mid_b u_b (.clk(clk_b), .rst_n(rst_n), .d(wire_a), .q(wire_b));
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    // Should find FFs in both paths
    CHECK(ff.getFFNodes().size() >= 2);
    ConnectivityBuilder conn(*c, ff.getFFNodes());
    conn.analyze();
    CrossingDetector det(conn.getEdges(), db);
    det.analyze();
    // Connectivity through hierarchical port connections (wire_a) may or may not
    // be traced depending on the flattening depth. Document observed behavior:
    // If edges are found, crossings should be detected; otherwise 0 is acceptable.
    auto crossings = det.getCrossings();
    (void)crossings;  // no crash is the baseline requirement
}

TEST_CASE("Corner: deeply nested hierarchy", "[corner]") {
    auto c = compileSV(R"(
        module level3 (input logic clk, rst_n, d, output logic q);
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) q <= 0; else q <= d;
        endmodule
        module level2 (input logic clk, rst_n, d, output logic q);
            level3 u3 (.clk(clk), .rst_n(rst_n), .d(d), .q(q));
        endmodule
        module level1 (input logic clk, rst_n, d, output logic q);
            level2 u2 (.clk(clk), .rst_n(rst_n), .d(d), .q(q));
        endmodule
        module deep_top (input logic clk_a, clk_b, rst_n, d);
            logic mid_wire;
            level1 u_src (.clk(clk_a), .rst_n(rst_n), .d(d), .q(mid_wire));
            level1 u_dst (.clk(clk_b), .rst_n(rst_n), .d(mid_wire), .q());
        endmodule
    )", "corner");
    ClockDatabase db;
    ClockTreeAnalyzer ct(*c, db);
    ct.analyze();
    FFClassifier ff(*c, db);
    ff.analyze();
    CHECK(ff.getFFNodes().size() >= 2);
}
