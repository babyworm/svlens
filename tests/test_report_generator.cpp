#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/types.h"
#include "sv-cdccheck/report_generator.h"

#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static AnalysisResult makeTestResult() {
    AnalysisResult result;

    // Create sources and domains
    auto src_sys = std::make_unique<ClockSource>();
    src_sys->name = "sys_clk";
    src_sys->type = ClockSource::Type::Primary;
    auto* sys_ptr = result.clock_db.addSource(std::move(src_sys));
    auto* dom_sys = result.clock_db.findOrCreateDomain(sys_ptr, Edge::Posedge);

    auto src_ext = std::make_unique<ClockSource>();
    src_ext->name = "ext_clk";
    src_ext->type = ClockSource::Type::AutoDetected;
    auto* ext_ptr = result.clock_db.addSource(std::move(src_ext));
    auto* dom_ext = result.clock_db.findOrCreateDomain(ext_ptr, Edge::Posedge);

    result.clock_db.relationships.push_back(
        {sys_ptr, ext_ptr, DomainRelationship::Type::Asynchronous});

    // Create crossings
    CrossingReport v1;
    v1.id = "VIOLATION-001";
    v1.category = ViolationCategory::Violation;
    v1.severity = Severity::High;
    v1.source_signal = "top.u_ctrl.q_frame_start";
    v1.dest_signal = "top.u_display.q_frame_start";
    v1.source_domain = dom_sys;
    v1.dest_domain = dom_ext;
    v1.sync_type = SyncType::None;
    v1.recommendation = "Insert 2-FF synchronizer";
    result.crossings.push_back(v1);

    CrossingReport i1;
    i1.id = "INFO-001";
    i1.category = ViolationCategory::Info;
    i1.severity = Severity::Info;
    i1.source_signal = "top.u_a.q_data";
    i1.dest_signal = "top.u_b.sync_ff2";
    i1.source_domain = dom_sys;
    i1.dest_domain = dom_ext;
    i1.sync_type = SyncType::TwoFF;
    result.crossings.push_back(i1);

    return result;
}

TEST_CASE("ReportGenerator: violation and info counts", "[report]") {
    auto result = makeTestResult();
    CHECK(result.violation_count() == 1);
    CHECK(result.caution_count() == 0);
    CHECK(result.info_count() == 1);
}

TEST_CASE("ReportGenerator: markdown output contains key sections", "[report]") {
    auto result = makeTestResult();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_report.md";
    gen.generateMarkdown(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("# CDC Analysis Report") != std::string::npos);
    CHECK(content.find("VIOLATION") != std::string::npos);
    CHECK(content.find("INFO") != std::string::npos);
    CHECK(content.find("sys_clk") != std::string::npos);
    CHECK(content.find("ext_clk") != std::string::npos);
    CHECK(content.find("VIOLATION-001") != std::string::npos);
    CHECK(content.find("Insert 2-FF synchronizer") != std::string::npos);
}

TEST_CASE("ReportGenerator: JSON output is valid structure", "[report]") {
    auto result = makeTestResult();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_report.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    // Basic JSON structure checks
    CHECK(content.find("\"summary\"") != std::string::npos);
    CHECK(content.find("\"violations\": 1") != std::string::npos);
    CHECK(content.find("\"domains\"") != std::string::npos);
    CHECK(content.find("\"crossings\"") != std::string::npos);
    CHECK(content.find("VIOLATION-001") != std::string::npos);
    CHECK(content.find("sys_clk") != std::string::npos);
}

TEST_CASE("ReportGenerator: empty result produces valid output", "[report]") {
    AnalysisResult result;
    ReportGenerator gen(result);

    auto md_path = fs::temp_directory_path() / "test_empty.md";
    auto json_path = fs::temp_directory_path() / "test_empty.json";
    gen.generateMarkdown(md_path);
    gen.generateJSON(json_path);

    std::ifstream md(md_path);
    std::string md_content((std::istreambuf_iterator<char>(md)),
                            std::istreambuf_iterator<char>());

    std::ifstream json(json_path);
    std::string json_content((std::istreambuf_iterator<char>(json)),
                              std::istreambuf_iterator<char>());

    fs::remove(md_path);
    fs::remove(json_path);

    CHECK(md_content.find("VIOLATION | 0") != std::string::npos);
    CHECK(json_content.find("\"violations\": 0") != std::string::npos);
}

static AnalysisResult makeMixedResult() {
    AnalysisResult result;

    auto src_sys = std::make_unique<ClockSource>();
    src_sys->name = "sys_clk";
    src_sys->type = ClockSource::Type::Primary;
    src_sys->period_ns = 10.0;
    auto* sys_ptr = result.clock_db.addSource(std::move(src_sys));
    auto* dom_sys = result.clock_db.findOrCreateDomain(sys_ptr, Edge::Posedge);

    auto src_ext = std::make_unique<ClockSource>();
    src_ext->name = "ext_clk";
    src_ext->type = ClockSource::Type::AutoDetected;
    auto* ext_ptr = result.clock_db.addSource(std::move(src_ext));
    auto* dom_ext = result.clock_db.findOrCreateDomain(ext_ptr, Edge::Posedge);

    result.clock_db.relationships.push_back(
        {sys_ptr, ext_ptr, DomainRelationship::Type::Asynchronous});

    // VIOLATION crossing
    CrossingReport v1;
    v1.id = "VIOLATION-001";
    v1.category = ViolationCategory::Violation;
    v1.severity = Severity::High;
    v1.source_signal = "top.u_a.q_data";
    v1.dest_signal = "top.u_b.q_data";
    v1.source_domain = dom_sys;
    v1.dest_domain = dom_ext;
    v1.sync_type = SyncType::None;
    v1.rule = "Ac_cdc01";
    v1.recommendation = "Insert 2-FF synchronizer";
    result.crossings.push_back(v1);

    // CAUTION crossing
    CrossingReport c1;
    c1.id = "CAUTION-001";
    c1.category = ViolationCategory::Caution;
    c1.severity = Severity::Medium;
    c1.source_signal = "top.u_a.q_ctrl";
    c1.dest_signal = "top.u_b.q_ctrl";
    c1.source_domain = dom_sys;
    c1.dest_domain = dom_ext;
    c1.sync_type = SyncType::TwoFF;
    c1.rule = "Ac_cdc03";
    c1.recommendation = "Reconvergence risk";
    result.crossings.push_back(c1);

    // INFO crossing (synced)
    CrossingReport i1;
    i1.id = "INFO-001";
    i1.category = ViolationCategory::Info;
    i1.severity = Severity::Info;
    i1.source_signal = "top.u_a.q_sync";
    i1.dest_signal = "top.u_b.sync_ff2";
    i1.source_domain = dom_sys;
    i1.dest_domain = dom_ext;
    i1.sync_type = SyncType::TwoFF;
    i1.rule = "";
    result.crossings.push_back(i1);

    // WAIVED crossing
    CrossingReport w1;
    w1.id = "WAIVED-001";
    w1.category = ViolationCategory::Waived;
    w1.severity = Severity::None;
    w1.source_signal = "top.u_a.q_waived";
    w1.dest_signal = "top.u_b.q_waived";
    w1.source_domain = dom_sys;
    w1.dest_domain = dom_ext;
    w1.sync_type = SyncType::None;
    w1.rule = "";
    result.crossings.push_back(w1);

    return result;
}

TEST_CASE("ReportGenerator: SDC output has set_false_path and set_max_delay", "[report]") {
    auto result = makeMixedResult();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_report.sdc";
    gen.generateSDC(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    // Waived crossing should produce set_false_path
    CHECK(content.find("set_false_path") != std::string::npos);
    CHECK(content.find("WAIVED") != std::string::npos);

    // Synced crossing should produce set_max_delay
    CHECK(content.find("set_max_delay") != std::string::npos);
    CHECK(content.find("SYNCED") != std::string::npos);

    // Violation should have a WARNING comment
    CHECK(content.find("WARNING: unsynchronized crossing") != std::string::npos);
}

TEST_CASE("ReportGenerator: DOT output has digraph and red edges", "[report]") {
    auto result = makeTestResult();

    // Add FF nodes and edges so DOT has something to render
    auto ff1 = std::make_unique<FFNode>();
    ff1->hier_path = "top.u_a.q_data";
    ff1->domain = result.clock_db.domains[0].get();
    auto ff2 = std::make_unique<FFNode>();
    ff2->hier_path = "top.u_b.q_data";
    ff2->domain = result.clock_db.domains[1].get();

    FFEdge edge;
    edge.source = ff1.get();
    edge.dest = ff2.get();
    result.edges.push_back(edge);
    result.ff_nodes.push_back(std::move(ff1));
    result.ff_nodes.push_back(std::move(ff2));

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_report.dot";
    gen.generateDOT(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("digraph CDC") != std::string::npos);
    CHECK(content.find("rankdir=LR") != std::string::npos);
    // Cross-domain edge should be red
    CHECK(content.find("color=red") != std::string::npos);
}

TEST_CASE("ReportGenerator: waiver template generates YAML structure", "[report]") {
    auto result = makeTestResult();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_waivers.yaml";
    gen.generateWaiverTemplate(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("waivers:") != std::string::npos);
    CHECK(content.find("- id: WAIVE-001") != std::string::npos);
    CHECK(content.find("crossing:") != std::string::npos);
    CHECK(content.find("reason:") != std::string::npos);
    CHECK(content.find("owner:") != std::string::npos);
    CHECK(content.find("date:") != std::string::npos);
    // Only violations get waiver entries
    CHECK(content.find("q_frame_start") != std::string::npos);
}

TEST_CASE("ReportGenerator: JSON has all required fields", "[report]") {
    auto result = makeMixedResult();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_fields.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    // Each crossing should have category, severity, sync_type, rule, path
    CHECK(content.find("\"category\"") != std::string::npos);
    CHECK(content.find("\"severity\"") != std::string::npos);
    CHECK(content.find("\"sync_type\"") != std::string::npos);
    CHECK(content.find("\"rule\"") != std::string::npos);
    CHECK(content.find("\"path\"") != std::string::npos);
    CHECK(content.find("\"recommendation\"") != std::string::npos);

    // Specific category values present
    CHECK(content.find("\"VIOLATION\"") != std::string::npos);
    CHECK(content.find("\"CAUTION\"") != std::string::npos);
    CHECK(content.find("\"INFO\"") != std::string::npos);
}

TEST_CASE("ReportGenerator: markdown crossing section has Path when comb_path exists", "[report]") {
    AnalysisResult result;
    auto src = std::make_unique<ClockSource>();
    src->name = "clk_a";
    src->type = ClockSource::Type::Primary;
    auto* src_ptr = result.clock_db.addSource(std::move(src));
    auto* dom_a = result.clock_db.findOrCreateDomain(src_ptr, Edge::Posedge);

    auto src2 = std::make_unique<ClockSource>();
    src2->name = "clk_b";
    src2->type = ClockSource::Type::Primary;
    auto* src2_ptr = result.clock_db.addSource(std::move(src2));
    auto* dom_b = result.clock_db.findOrCreateDomain(src2_ptr, Edge::Posedge);

    result.clock_db.relationships.push_back(
        {src_ptr, src2_ptr, DomainRelationship::Type::Asynchronous});

    CrossingReport c1;
    c1.id = "VIOLATION-001";
    c1.category = ViolationCategory::Violation;
    c1.severity = Severity::High;
    c1.source_signal = "top.q_a";
    c1.dest_signal = "top.q_b";
    c1.source_domain = dom_a;
    c1.dest_domain = dom_b;
    c1.sync_type = SyncType::None;
    c1.path = {"top.q_a", "top.comb_wire", "top.q_b"};
    c1.recommendation = "Insert synchronizer";
    result.crossings.push_back(c1);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "test_path.md";
    gen.generateMarkdown(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("Path:") != std::string::npos);
    CHECK(content.find("top.comb_wire") != std::string::npos);
}

TEST_CASE("ReportGenerator: mixed categories in same report", "[report]") {
    auto result = makeMixedResult();
    ReportGenerator gen(result);

    auto md_path = fs::temp_directory_path() / "test_mixed.md";
    gen.generateMarkdown(md_path);

    std::ifstream f(md_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(md_path);

    CHECK(result.violation_count() == 1);
    CHECK(result.caution_count() == 1);
    CHECK(result.info_count() == 1);
    CHECK(result.waived_count() == 1);

    CHECK(content.find("VIOLATION | 1") != std::string::npos);
    CHECK(content.find("CAUTION | 1") != std::string::npos);
    CHECK(content.find("INFO | 1") != std::string::npos);
    CHECK(content.find("WAIVED | 1") != std::string::npos);
}
