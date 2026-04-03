#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/types.h"
#include "sv-cdccheck/sdc_parser.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/report_generator.h"

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static fs::path writeTempSdc(const std::string& content) {
    static int counter = 0;
    auto path = fs::temp_directory_path() / ("test_gap_" + std::to_string(counter++) + ".sdc");
    std::ofstream(path) << content;
    return path;
}

// ─── GAP-3: SDC parser non-existent file ───

TEST_CASE("GAP: SDC parser non-existent file returns empty", "[gap]") {
    auto sdc = SdcParser::parse("/nonexistent/path/does_not_exist.sdc");
    CHECK(sdc.clocks.empty());
    CHECK(sdc.generated_clocks.empty());
    CHECK(sdc.clock_groups.empty());
}

// ─── GAP-5: domainForSignal ───

TEST_CASE("GAP: ClockDatabase domainForSignal valid and invalid", "[gap]") {
    ClockDatabase db;
    auto src = std::make_unique<ClockSource>();
    src->name = "clk";
    auto* s = db.addSource(std::move(src));
    auto* dom = db.findOrCreateDomain(s, Edge::Posedge);

    auto net = std::make_unique<ClockNet>();
    net->hier_path = "top.clk";
    net->source = s;
    net->edge = Edge::Posedge;
    db.addNet(std::move(net));

    CHECK(db.domainForSignal("top.clk") == dom);
    CHECK(db.domainForSignal("nonexistent") == nullptr);
}

// ─── GAP-6: isAsynchronous with nullptr ───

TEST_CASE("GAP: isAsynchronous with nullptr domains", "[gap]") {
    ClockDatabase db;
    CHECK(db.isAsynchronous(nullptr, nullptr) == true);

    auto src = std::make_unique<ClockSource>();
    src->name = "clk";
    auto* s = db.addSource(std::move(src));
    auto* dom = db.findOrCreateDomain(s, Edge::Posedge);
    CHECK(db.isAsynchronous(dom, nullptr) == true);
    CHECK(db.isAsynchronous(nullptr, dom) == true);
    CHECK(db.isAsynchronous(dom, dom) == false);
}

// ─── GAP-7: null domain edge skipped ───

TEST_CASE("GAP: CrossingDetector skips edges with null domain", "[gap]") {
    FFNode ff_a{"top.a", nullptr, nullptr, {}};
    FFNode ff_b{"top.b", nullptr, nullptr, {}};
    std::vector<FFEdge> edges;
    edges.push_back({&ff_a, &ff_b, {}, SyncType::None, false});
    ClockDatabase db;
    CrossingDetector det(edges, db);
    det.analyze();
    CHECK(det.getCrossings().empty());
}

// ─── GAP-8: 3-FF sync end-to-end ───

TEST_CASE("GAP: 3-FF sync detected end-to-end", "[gap]") {
    auto compilation = test::compileSV(R"(
        module three_ff_e2e (input logic clk_a, clk_b, rst_n, d);
            logic q_a, s1, s2, s3;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) begin s1 <= 0; s2 <= 0; s3 <= 0; end
                else begin s1 <= q_a; s2 <= s1; s3 <= s2; end
        endmodule
    )", "gap");

    ClockDatabase db;
    ClockTreeAnalyzer ct(*compilation, db); ct.analyze();
    FFClassifier ff(*compilation, db); ff.analyze();
    ConnectivityBuilder conn(*compilation, ff.getFFNodes()); conn.analyze();
    CrossingDetector det(conn.getEdges(), db); det.analyze();
    auto crossings = det.getCrossings();
    SyncVerifier sv(crossings, ff.getFFNodes(), conn.getEdges()); sv.analyze();

    bool found_3ff = false;
    for (auto& c : crossings)
        if (c.sync_type == SyncType::ThreeFF) found_3ff = true;
    CHECK(found_3ff);
}

// ─── GAP-13: SDC -invert ───

TEST_CASE("GAP: SDC generated clock with -invert", "[gap]") {
    auto path = writeTempSdc(
        "create_generated_clock -name inv_clk -source [get_ports sys_clk] "
        "-invert [get_pins u_inv/Q]\n");
    auto sdc = SdcParser::parse(path);
    REQUIRE(sdc.generated_clocks.size() == 1);
    CHECK(sdc.generated_clocks[0].invert == true);
    CHECK(sdc.generated_clocks[0].name == "inv_clk");
}

// ─── GAP-14: SDC -multiply_by ───

TEST_CASE("GAP: SDC generated clock with -multiply_by", "[gap]") {
    auto path = writeTempSdc(
        "create_generated_clock -name fast_clk -source [get_ports sys_clk] "
        "-multiply_by 4 [get_pins u_pll/Q]\n");
    auto sdc = SdcParser::parse(path);
    REQUIRE(sdc.generated_clocks.size() == 1);
    CHECK(sdc.generated_clocks[0].multiply_by == 4);
}

// ─── GAP-25: Circular assign depth guard ───

TEST_CASE("GAP: circular assign does not hang", "[gap]") {
    auto compilation = test::compileSV(R"(
        module circ_assign (input logic clk, rst_n);
            logic a, b;
            assign a = b;
            assign b = a;
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) a <= 0;
        endmodule
    )", "gap");

    ClockDatabase db;
    ClockTreeAnalyzer ct(*compilation, db); ct.analyze();
    FFClassifier ff(*compilation, db); ff.analyze();
    ConnectivityBuilder conn(*compilation, ff.getFFNodes()); conn.analyze();
    CHECK(true); // Must reach here without hanging
}

// ─── GAP: AnalysisResult counts ───

TEST_CASE("GAP: AnalysisResult counts are zero for empty result", "[gap]") {
    AnalysisResult result;
    CHECK(result.violation_count() == 0);
    CHECK(result.caution_count() == 0);
    CHECK(result.info_count() == 0);
    CHECK(result.waived_count() == 0);
    CHECK(result.convention_count() == 0);
}

// ─── Task 1: DOT output with special chars in signal names ───

TEST_CASE("GAP: DOT output escapes quotes and backslashes in labels", "[gap][report]") {
    AnalysisResult result;

    // Create a clock source/domain
    auto src = std::make_unique<ClockSource>();
    src->name = "clk";
    auto* s = result.clock_db.addSource(std::move(src));
    auto* dom = result.clock_db.findOrCreateDomain(s, Edge::Posedge);

    // Create FF nodes with special characters
    auto ff1 = std::make_unique<FFNode>();
    ff1->hier_path = "top.u_a.sig\"quote";
    ff1->domain = dom;
    auto ff2 = std::make_unique<FFNode>();
    ff2->hier_path = "top.u_b.sig\\back";
    ff2->domain = dom;

    result.ff_nodes.push_back(std::move(ff1));
    result.ff_nodes.push_back(std::move(ff2));

    auto dot_path = fs::temp_directory_path() / "test_dot_escape.dot";
    ReportGenerator report(result);
    report.generateDOT(dot_path);

    std::ifstream in(dot_path);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    // Quotes in labels must be escaped
    CHECK(content.find("sig\\\"quote") != std::string::npos);
    // Backslashes in labels must be escaped
    CHECK(content.find("sig\\\\back") != std::string::npos);
    fs::remove(dot_path);
}

// ─── Task 1: Markdown output with | in signal names ───

TEST_CASE("GAP: Markdown output escapes pipe in signal names", "[gap][report]") {
    AnalysisResult result;

    auto src = std::make_unique<ClockSource>();
    src->name = "clk_a";
    auto* sa = result.clock_db.addSource(std::move(src));
    auto* dom_a = result.clock_db.findOrCreateDomain(sa, Edge::Posedge);

    auto src2 = std::make_unique<ClockSource>();
    src2->name = "clk_b";
    auto* sb = result.clock_db.addSource(std::move(src2));
    auto* dom_b = result.clock_db.findOrCreateDomain(sb, Edge::Posedge);

    CrossingReport cr;
    cr.id = "VIOLATION-001";
    cr.category = ViolationCategory::Violation;
    cr.severity = Severity::High;
    cr.source_signal = "top.bus|data[0]";
    cr.dest_signal = "top.sync|out";
    cr.source_domain = dom_a;
    cr.dest_domain = dom_b;
    cr.sync_type = SyncType::None;
    cr.recommendation = "Add 2FF | synchronizer";
    result.crossings.push_back(cr);

    auto md_path = fs::temp_directory_path() / "test_md_escape.md";
    ReportGenerator report(result);
    report.generateMarkdown(md_path);

    std::ifstream in(md_path);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    // Pipe chars in signal names must be escaped
    CHECK(content.find("top.bus\\|data[0]") != std::string::npos);
    CHECK(content.find("top.sync\\|out") != std::string::npos);
    CHECK(content.find("2FF \\| synchronizer") != std::string::npos);
    fs::remove(md_path);
}

// ─── Task 2: Waiver template generation ───

TEST_CASE("GAP: Waiver template generates entries for violations", "[gap][report]") {
    AnalysisResult result;

    auto src = std::make_unique<ClockSource>();
    src->name = "clk_a";
    auto* sa = result.clock_db.addSource(std::move(src));
    auto* dom_a = result.clock_db.findOrCreateDomain(sa, Edge::Posedge);

    auto src2 = std::make_unique<ClockSource>();
    src2->name = "clk_b";
    auto* sb = result.clock_db.addSource(std::move(src2));
    auto* dom_b = result.clock_db.findOrCreateDomain(sb, Edge::Posedge);

    // Add a VIOLATION crossing
    CrossingReport v;
    v.id = "VIOLATION-001";
    v.category = ViolationCategory::Violation;
    v.severity = Severity::High;
    v.source_signal = "top.src_ff";
    v.dest_signal = "top.dst_ff";
    v.source_domain = dom_a;
    v.dest_domain = dom_b;
    v.sync_type = SyncType::None;
    result.crossings.push_back(v);

    // Add a CAUTION crossing (should NOT appear in waiver template)
    CrossingReport c;
    c.id = "CAUTION-001";
    c.category = ViolationCategory::Caution;
    c.severity = Severity::Medium;
    c.source_signal = "top.src2";
    c.dest_signal = "top.dst2";
    c.source_domain = dom_a;
    c.dest_domain = dom_b;
    c.sync_type = SyncType::TwoFF;
    result.crossings.push_back(c);

    auto waiver_path = fs::temp_directory_path() / "test_waiver.yaml";
    ReportGenerator report(result);
    report.generateWaiverTemplate(waiver_path);

    std::ifstream in(waiver_path);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    CHECK(content.find("waivers:") != std::string::npos);
    CHECK(content.find("WAIVE-001") != std::string::npos);
    CHECK(content.find("top.src_ff -> top.dst_ff") != std::string::npos);
    CHECK(content.find("reason: \"\"") != std::string::npos);
    CHECK(content.find("owner: \"\"") != std::string::npos);
    CHECK(content.find("date: \"\"") != std::string::npos);
    // CAUTION crossing should not generate a waiver entry
    CHECK(content.find("WAIVE-002") == std::string::npos);
    CHECK(content.find("top.src2") == std::string::npos);
    fs::remove(waiver_path);
}

// ─── Task 4: SDC comment inside brackets preserved ───

TEST_CASE("GAP: SDC comment inside brackets is preserved", "[gap][sdc]") {
    static int sdc_ctr = 100;
    auto path = fs::temp_directory_path() / ("test_bracket_comment_" + std::to_string(sdc_ctr++) + ".sdc");
    {
        std::ofstream out(path);
        out << "create_clock -name sys_clk -period 10 [get_ports clk#0]\n";
        out << "create_clock -name aux_clk -period 5 [get_ports aux] # real comment\n";
    }
    auto sdc = SdcParser::parse(path);
    REQUIRE(sdc.clocks.size() == 2);
    // The first clock target should contain the # character (inside brackets)
    CHECK(sdc.clocks[0].name == "sys_clk");
    CHECK(sdc.clocks[0].target == "clk#0");
    // The second clock should have its comment stripped correctly
    CHECK(sdc.clocks[1].name == "aux_clk");
    fs::remove(path);
}
