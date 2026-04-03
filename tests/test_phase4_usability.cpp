#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/waiver.h"
#include "sv-cdccheck/types.h"
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"

#include <filesystem>
#include <string>
#include <sstream>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_phase4");
}

// ─── Helper: run full CDC pipeline ───
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

// ─── Helper: build a test AnalysisResult ───
static AnalysisResult makeTestResult() {
    AnalysisResult result;

    auto src_a = std::make_unique<ClockSource>();
    src_a->name = "clk_a";
    src_a->type = ClockSource::Type::Primary;
    auto* a_ptr = result.clock_db.addSource(std::move(src_a));
    auto* dom_a = result.clock_db.findOrCreateDomain(a_ptr, Edge::Posedge);

    auto src_b = std::make_unique<ClockSource>();
    src_b->name = "clk_b";
    src_b->type = ClockSource::Type::AutoDetected;
    auto* b_ptr = result.clock_db.addSource(std::move(src_b));
    auto* dom_b = result.clock_db.findOrCreateDomain(b_ptr, Edge::Posedge);

    result.clock_db.relationships.push_back(
        {a_ptr, b_ptr, DomainRelationship::Type::Asynchronous});

    CrossingReport v1;
    v1.id = "VIOLATION-001";
    v1.category = ViolationCategory::Violation;
    v1.severity = Severity::High;
    v1.source_signal = "top.u_ctrl.o_cfg_data";
    v1.dest_signal = "top.u_slow.i_cfg_data";
    v1.source_domain = dom_a;
    v1.dest_domain = dom_b;
    v1.sync_type = SyncType::None;
    v1.path = {"top.u_ctrl.o_cfg_data", "wire_cfg", "top.u_slow.i_cfg_data"};
    v1.recommendation = "Insert 2-FF synchronizer";
    result.crossings.push_back(v1);

    return result;
}

// =============================================================================
// 1. Waiver Mechanism Tests
// =============================================================================

TEST_CASE("Phase4: waiver YAML parsing with exact crossing", "[phase4][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-001
    crossing: "top.u_ctrl.o_cfg_data -> top.u_slow.i_cfg_data"
    reason: "Quasi-static configuration, stable before use"
    owner: "john@example.com"
)";
    REQUIRE(mgr.loadString(yaml));
    REQUIRE(mgr.getWaivers().size() == 1);

    auto& w = mgr.getWaivers()[0];
    CHECK(w.id == "WAIVE-001");
    CHECK(w.crossing == "top.u_ctrl.o_cfg_data -> top.u_slow.i_cfg_data");
    CHECK(w.reason == "Quasi-static configuration, stable before use");
    CHECK(w.owner == "john@example.com");
}

TEST_CASE("Phase4: waiver YAML parsing with pattern", "[phase4][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-002
    pattern: "top.u_debug.*"
    reason: "Debug-only signals"
)";
    REQUIRE(mgr.loadString(yaml));
    REQUIRE(mgr.getWaivers().size() == 1);
    CHECK(mgr.getWaivers()[0].pattern == "top.u_debug.*");
}

TEST_CASE("Phase4: waiver multiple entries parsed", "[phase4][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-001
    crossing: "a -> b"
    reason: "reason1"
  - id: WAIVE-002
    pattern: "top.u_debug.*"
    reason: "reason2"
  - id: WAIVE-003
    crossing: "c -> d"
    reason: "reason3"
    owner: "owner@test.com"
)";
    REQUIRE(mgr.loadString(yaml));
    REQUIRE(mgr.getWaivers().size() == 3);
    CHECK(mgr.getWaivers()[0].id == "WAIVE-001");
    CHECK(mgr.getWaivers()[1].id == "WAIVE-002");
    CHECK(mgr.getWaivers()[2].id == "WAIVE-003");
    CHECK(mgr.getWaivers()[2].owner == "owner@test.com");
}

TEST_CASE("Phase4: waiver isWaived with exact crossing match", "[phase4][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-001
    crossing: "top.u_a.sig -> top.u_b.sig"
    reason: "known safe"
)";
    mgr.loadString(yaml);

    CHECK(mgr.isWaived("top.u_a.sig", "top.u_b.sig"));
    CHECK_FALSE(mgr.isWaived("top.u_a.sig", "top.u_c.sig"));
    CHECK_FALSE(mgr.isWaived("top.u_c.sig", "top.u_b.sig"));
}

TEST_CASE("Phase4: waiver isWaived with pattern match", "[phase4][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-002
    pattern: "top.u_debug.*"
    reason: "debug signals"
)";
    mgr.loadString(yaml);

    CHECK(mgr.isWaived("top.u_debug.foo", "top.u_core.bar"));
    CHECK(mgr.isWaived("top.u_core.bar", "top.u_debug.baz"));
    CHECK_FALSE(mgr.isWaived("top.u_core.a", "top.u_core.b"));
}

TEST_CASE("Phase4: waiver findWaiver returns entry details", "[phase4][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-010
    crossing: "x -> y"
    reason: "safe crossing"
    owner: "alice@test.com"
)";
    mgr.loadString(yaml);

    auto found = mgr.findWaiver("x", "y");
    REQUIRE(found.has_value());
    CHECK(found->id == "WAIVE-010");
    CHECK(found->reason == "safe crossing");
    CHECK(found->owner == "alice@test.com");

    auto not_found = mgr.findWaiver("a", "b");
    CHECK_FALSE(not_found.has_value());
}

TEST_CASE("Phase4: waiver file loading", "[phase4][waiver]") {
    auto path = fs::temp_directory_path() / "test_waiver.yaml";
    {
        std::ofstream f(path);
        f << "waivers:\n"
          << "  - id: WAIVE-FILE\n"
          << "    crossing: \"a -> b\"\n"
          << "    reason: \"from file\"\n";
    }

    WaiverManager mgr;
    REQUIRE(mgr.loadFile(path.string()));
    REQUIRE(mgr.getWaivers().size() == 1);
    CHECK(mgr.getWaivers()[0].id == "WAIVE-FILE");

    fs::remove(path);
}

TEST_CASE("Phase4: waiver loadFile returns false for missing file", "[phase4][waiver]") {
    WaiverManager mgr;
    CHECK_FALSE(mgr.loadFile("/nonexistent/waiver.yaml"));
}

TEST_CASE("Phase4: waiver empty YAML produces no waivers", "[phase4][waiver]") {
    WaiverManager mgr;
    CHECK_FALSE(mgr.loadString(""));
    CHECK(mgr.getWaivers().empty());
}

// =============================================================================
// 2. JSON Escaping Tests
// =============================================================================

TEST_CASE("Phase4: JSON output escapes special characters", "[phase4][json]") {
    AnalysisResult result;

    auto src = std::make_unique<ClockSource>();
    src->name = "clk_a";
    src->type = ClockSource::Type::Primary;
    auto* s_ptr = result.clock_db.addSource(std::move(src));
    auto* dom = result.clock_db.findOrCreateDomain(s_ptr, Edge::Posedge);

    auto src2 = std::make_unique<ClockSource>();
    src2->name = "clk_b";
    src2->type = ClockSource::Type::AutoDetected;
    auto* s2_ptr = result.clock_db.addSource(std::move(src2));
    auto* dom2 = result.clock_db.findOrCreateDomain(s2_ptr, Edge::Posedge);

    CrossingReport c;
    c.id = "VIOLATION-001";
    c.category = ViolationCategory::Violation;
    c.severity = Severity::High;
    c.source_signal = "top.u_a.sig\"with_quote";
    c.dest_signal = "top.u_b.sig\\with_backslash";
    c.source_domain = dom;
    c.dest_domain = dom2;
    c.sync_type = SyncType::None;
    c.recommendation = "Fix the \"issue\" here";
    result.crossings.push_back(c);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_escape.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    // Escaped quotes should appear as \"
    CHECK(content.find("sig\\\"with_quote") != std::string::npos);
    // Escaped backslash should appear as \\  (double-escaped)
    CHECK(content.find("sig\\\\with_backslash") != std::string::npos);
}

TEST_CASE("Phase4: JSON output includes path and recommendation fields", "[phase4][json]") {
    auto result = makeTestResult();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_json_fields.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"path\"") != std::string::npos);
    CHECK(content.find("\"recommendation\"") != std::string::npos);
    CHECK(content.find("wire_cfg") != std::string::npos);
    CHECK(content.find("Insert 2-FF synchronizer") != std::string::npos);
}

TEST_CASE("Phase4: JSON waived count in summary", "[phase4][json]") {
    AnalysisResult result;

    auto src = std::make_unique<ClockSource>();
    src->name = "clk_a";
    auto* s_ptr = result.clock_db.addSource(std::move(src));
    auto* dom = result.clock_db.findOrCreateDomain(s_ptr, Edge::Posedge);

    CrossingReport c;
    c.id = "WAIVED-001";
    c.category = ViolationCategory::Waived;
    c.severity = Severity::None;
    c.source_domain = dom;
    c.dest_domain = dom;
    c.sync_type = SyncType::None;
    result.crossings.push_back(c);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_waived_json.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"waived\": 1") != std::string::npos);
}

// =============================================================================
// 3. DOT Graph Export Tests
// =============================================================================

TEST_CASE("Phase4: DOT graph export basic structure", "[phase4][dot]") {
    auto result = makeTestResult();

    // Add some FF nodes
    auto ff1 = std::make_unique<FFNode>();
    ff1->hier_path = "top.u_ctrl.o_cfg_data";
    ff1->domain = result.clock_db.domain_by_name["clk_a"];
    result.ff_nodes.push_back(std::move(ff1));

    auto ff2 = std::make_unique<FFNode>();
    ff2->hier_path = "top.u_slow.i_cfg_data";
    ff2->domain = result.clock_db.domain_by_name["clk_b"];
    result.ff_nodes.push_back(std::move(ff2));

    FFEdge edge;
    edge.source = result.ff_nodes[0].get();
    edge.dest = result.ff_nodes[1].get();
    result.edges.push_back(edge);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_graph.dot";
    gen.generateDOT(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("digraph") != std::string::npos);
    CHECK(content.find("top.u_ctrl.o_cfg_data") != std::string::npos);
    CHECK(content.find("top.u_slow.i_cfg_data") != std::string::npos);
    CHECK(content.find("->") != std::string::npos);
    CHECK(content.find("color=") != std::string::npos);
}

TEST_CASE("Phase4: DOT graph colors nodes by domain", "[phase4][dot]") {
    auto result = makeTestResult();

    auto ff1 = std::make_unique<FFNode>();
    ff1->hier_path = "top.ff_a";
    ff1->domain = result.clock_db.domain_by_name["clk_a"];
    result.ff_nodes.push_back(std::move(ff1));

    auto ff2 = std::make_unique<FFNode>();
    ff2->hier_path = "top.ff_b";
    ff2->domain = result.clock_db.domain_by_name["clk_b"];
    result.ff_nodes.push_back(std::move(ff2));

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_domain_color.dot";
    gen.generateDOT(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    // Different domains should get different colors
    // At minimum both nodes should have fillcolor
    CHECK(content.find("fillcolor=") != std::string::npos);
}

TEST_CASE("Phase4: DOT graph highlights crossing edges in red", "[phase4][dot]") {
    auto result = makeTestResult();

    auto ff1 = std::make_unique<FFNode>();
    ff1->hier_path = "top.ff_a";
    ff1->domain = result.clock_db.domain_by_name["clk_a"];
    result.ff_nodes.push_back(std::move(ff1));

    auto ff2 = std::make_unique<FFNode>();
    ff2->hier_path = "top.ff_b";
    ff2->domain = result.clock_db.domain_by_name["clk_b"];
    result.ff_nodes.push_back(std::move(ff2));

    FFEdge edge;
    edge.source = result.ff_nodes[0].get();
    edge.dest = result.ff_nodes[1].get();
    result.edges.push_back(edge);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_crossing_color.dot";
    gen.generateDOT(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    // Cross-domain edge should be red
    CHECK(content.find("color=red") != std::string::npos);
}

TEST_CASE("Phase4: DOT graph empty result produces valid graph", "[phase4][dot]") {
    AnalysisResult result;
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_empty_dot.dot";
    gen.generateDOT(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("digraph") != std::string::npos);
    CHECK(content.find("}") != std::string::npos);
}

// =============================================================================
// 4. CrossingReport::path population
// =============================================================================

TEST_CASE("Phase4: crossing path populated from comb_path", "[phase4][path]") {
    auto compilation = compileSV(R"(
        module path_test (
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

    REQUIRE(pipeline.crossings.size() >= 1);

    // The crossing from q_a -> sync1 should have a populated path
    for (auto& c : pipeline.crossings) {
        if (c.source_signal.find("q_a") != std::string::npos) {
            // path should contain at least source and dest signals
            CHECK(c.path.size() >= 1);
        }
    }
}

TEST_CASE("Phase4: crossing path includes comb intermediate signals", "[phase4][path]") {
    auto compilation = compileSV(R"(
        module comb_path_test (
            input logic clk_a, clk_b, rst_n, enable
        );
            logic q_a;
            logic mixed;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= 1;

            assign mixed = q_a & enable;

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 0; sync2 <= 0; end
                else begin sync1 <= mixed; sync2 <= sync1; end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    bool found_comb_path = false;
    for (auto& c : pipeline.crossings) {
        if (!c.path.empty()) {
            found_comb_path = true;
        }
    }
    CHECK(found_comb_path);
}

// =============================================================================
// 5. Markdown output includes path
// =============================================================================

TEST_CASE("Phase4: markdown report includes crossing path", "[phase4][report]") {
    auto result = makeTestResult();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_md_path.md";
    gen.generateMarkdown(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("Path:") != std::string::npos);
    CHECK(content.find("wire_cfg") != std::string::npos);
}

// =============================================================================
// 6. CONVENTION violation category
// =============================================================================

TEST_CASE("Phase4: CONVENTION warning for non-standard clock naming", "[phase4][convention]") {
    auto compilation = compileSV(R"(
        module bad_naming (
            input logic my_trigger, fast_tick, rst_n, d
        );
            logic q_a;
            logic q_b;

            always_ff @(posedge my_trigger or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;

            always_ff @(posedge fast_tick or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Non-standard clock names should still be classified by domain relationship
    // (VIOLATION for async), with a naming convention annotation in recommendation
    bool found_convention_annotation = false;
    for (auto& c : pipeline.crossings) {
        if (c.recommendation.find("naming convention") != std::string::npos) {
            found_convention_annotation = true;
            // Category should reflect the real domain relationship, not Convention
            CHECK((c.category == ViolationCategory::Violation ||
                   c.category == ViolationCategory::Caution));
        }
    }
    CHECK(found_convention_annotation);
}

TEST_CASE("Phase4: standard clock names do not trigger CONVENTION", "[phase4][convention]") {
    auto compilation = compileSV(R"(
        module good_naming (
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

    for (auto& c : pipeline.crossings) {
        CHECK(c.category != ViolationCategory::Convention);
    }
}

// =============================================================================
// 7. Waiver count in AnalysisResult
// =============================================================================

TEST_CASE("Phase4: AnalysisResult waived_count works", "[phase4][result]") {
    AnalysisResult result;

    CrossingReport c1;
    c1.category = ViolationCategory::Violation;
    result.crossings.push_back(c1);

    CrossingReport c2;
    c2.category = ViolationCategory::Waived;
    result.crossings.push_back(c2);

    CrossingReport c3;
    c3.category = ViolationCategory::Waived;
    result.crossings.push_back(c3);

    CHECK(result.violation_count() == 1);
    CHECK(result.waived_count() == 2);
}

// =============================================================================
// 8. Waiver integration with crossing detector
// =============================================================================

TEST_CASE("Phase4: waiver applied to crossings changes category to Waived", "[phase4][waiver][integration]") {
    auto compilation = compileSV(R"(
        module waiver_integration (
            input logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic q_b;

            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;

            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    REQUIRE(pipeline.crossings.size() >= 1);

    // Build waiver that matches the crossing
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-INT
    pattern: "waiver_integration.*"
    reason: "test waiver"
)";
    mgr.loadString(yaml);

    // Apply waivers
    for (auto& c : pipeline.crossings) {
        if (mgr.isWaived(c.source_signal, c.dest_signal)) {
            c.category = ViolationCategory::Waived;
        }
    }

    bool found_waived = false;
    for (auto& c : pipeline.crossings) {
        if (c.category == ViolationCategory::Waived) {
            found_waived = true;
        }
    }
    CHECK(found_waived);
}

// =============================================================================
// 9. Markdown waived summary
// =============================================================================

TEST_CASE("Phase4: markdown report includes WAIVED count", "[phase4][report]") {
    AnalysisResult result;

    auto src = std::make_unique<ClockSource>();
    src->name = "clk_a";
    auto* s_ptr = result.clock_db.addSource(std::move(src));
    auto* dom = result.clock_db.findOrCreateDomain(s_ptr, Edge::Posedge);

    CrossingReport c;
    c.id = "WAIVED-001";
    c.category = ViolationCategory::Waived;
    c.severity = Severity::None;
    c.source_domain = dom;
    c.dest_domain = dom;
    c.sync_type = SyncType::None;
    result.crossings.push_back(c);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_waived_md.md";
    gen.generateMarkdown(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("WAIVED") != std::string::npos);
}

// =============================================================================
// 10. Convention count in AnalysisResult
// =============================================================================

TEST_CASE("Phase4: AnalysisResult convention_count works", "[phase4][result]") {
    AnalysisResult result;

    CrossingReport c1;
    c1.category = ViolationCategory::Convention;
    result.crossings.push_back(c1);

    CrossingReport c2;
    c2.category = ViolationCategory::Convention;
    result.crossings.push_back(c2);

    CrossingReport c3;
    c3.category = ViolationCategory::Violation;
    result.crossings.push_back(c3);

    CHECK(result.convention_count() == 2);
    CHECK(result.violation_count() == 1);
}
