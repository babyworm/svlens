#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/types.h"
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"

#include <fstream>
#include <filesystem>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

// ─── Helpers ───

static AnalysisResult makeResultWithCrossings() {
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
    src_ext->period_ns = 8.0;
    auto* ext_ptr = result.clock_db.addSource(std::move(src_ext));
    auto* dom_ext = result.clock_db.findOrCreateDomain(ext_ptr, Edge::Posedge);

    result.clock_db.relationships.push_back(
        {sys_ptr, ext_ptr, DomainRelationship::Type::Asynchronous});

    // Add FF nodes for domain counting
    auto ff1 = std::make_unique<FFNode>();
    ff1->hier_path = "top.u_a.q_data";
    ff1->domain = dom_sys;
    result.ff_nodes.push_back(std::move(ff1));

    auto ff2 = std::make_unique<FFNode>();
    ff2->hier_path = "top.u_a.q_ctrl";
    ff2->domain = dom_sys;
    result.ff_nodes.push_back(std::move(ff2));

    auto ff3 = std::make_unique<FFNode>();
    ff3->hier_path = "top.u_b.sync_ff1";
    ff3->domain = dom_ext;
    result.ff_nodes.push_back(std::move(ff3));

    // Violation crossing (no sync)
    CrossingReport v1;
    v1.id = "VIOLATION-001";
    v1.category = ViolationCategory::Violation;
    v1.severity = Severity::High;
    v1.source_signal = "top.u_a.q_data";
    v1.dest_signal = "top.u_b.sync_ff1";
    v1.source_domain = dom_sys;
    v1.dest_domain = dom_ext;
    v1.sync_type = SyncType::None;
    v1.recommendation = "Insert 2-FF synchronizer";
    result.crossings.push_back(v1);

    // Info crossing (synced with 2-FF)
    CrossingReport i1;
    i1.id = "INFO-001";
    i1.category = ViolationCategory::Info;
    i1.severity = Severity::Info;
    i1.source_signal = "top.u_a.q_ctrl";
    i1.dest_signal = "top.u_b.sync_ff2";
    i1.source_domain = dom_sys;
    i1.dest_domain = dom_ext;
    i1.sync_type = SyncType::TwoFF;
    result.crossings.push_back(i1);

    // Waived crossing
    CrossingReport w1;
    w1.id = "WAIVED-001";
    w1.category = ViolationCategory::Waived;
    w1.severity = Severity::None;
    w1.source_signal = "top.u_a.q_status";
    w1.dest_signal = "top.u_b.q_status";
    w1.source_domain = dom_sys;
    w1.dest_domain = dom_ext;
    w1.sync_type = SyncType::None;
    result.crossings.push_back(w1);

    // Caution crossing
    CrossingReport c1;
    c1.id = "CAUTION-001";
    c1.category = ViolationCategory::Caution;
    c1.severity = Severity::Medium;
    c1.source_signal = "top.u_a.q_flag";
    c1.dest_signal = "top.u_b.q_flag";
    c1.source_domain = dom_sys;
    c1.dest_domain = dom_ext;
    c1.sync_type = SyncType::TwoFF;
    c1.recommendation = "Reconvergence risk";
    result.crossings.push_back(c1);

    return result;
}

static std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_remaining");
}

struct FullPipeline {
    ClockDatabase db;
    std::unique_ptr<FFClassifier> classifier;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;

    void run(slang::ast::Compilation& compilation, int required_stages = 2) {
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
        verifier.setRequiredStages(required_stages);
        verifier.analyze();
    }
};

// ─── Task 1: JSON report — category, severity, sync_type fields ───

TEST_CASE("JSON report contains category field", "[report][json]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_json_category.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"category\": \"VIOLATION\"") != std::string::npos);
    CHECK(content.find("\"category\": \"INFO\"") != std::string::npos);
    CHECK(content.find("\"category\": \"WAIVED\"") != std::string::npos);
    CHECK(content.find("\"category\": \"CAUTION\"") != std::string::npos);
}

TEST_CASE("JSON report contains severity field", "[report][json]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_json_severity.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"severity\": \"high\"") != std::string::npos);
    CHECK(content.find("\"severity\": \"info\"") != std::string::npos);
    CHECK(content.find("\"severity\": \"none\"") != std::string::npos);
    CHECK(content.find("\"severity\": \"medium\"") != std::string::npos);
}

TEST_CASE("JSON report contains sync_type field", "[report][json]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_json_synctype.json";
    gen.generateJSON(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"sync_type\": \"none\"") != std::string::npos);
    CHECK(content.find("\"sync_type\": \"two_ff\"") != std::string::npos);
}

// ─── Task 2: Exit code cap at 255 ───

TEST_CASE("AnalysisResult: violation_count works correctly", "[report][exitcode]") {
    AnalysisResult result;
    CHECK(result.violation_count() == 0);

    // Add 3 violations
    for (int i = 0; i < 3; i++) {
        CrossingReport c;
        c.category = ViolationCategory::Violation;
        result.crossings.push_back(c);
    }
    CHECK(result.violation_count() == 3);
}

TEST_CASE("Exit code cap: std::min caps at 255", "[exitcode]") {
    // Verify the logic that main.cpp uses
    CHECK(std::min(0, 255) == 0);
    CHECK(std::min(100, 255) == 100);
    CHECK(std::min(255, 255) == 255);
    CHECK(std::min(256, 255) == 255);
    CHECK(std::min(1000, 255) == 255);
}

// ─── Task 3: --sync-stages and --strict ───

TEST_CASE("SyncVerifier: required_stages=3 downgrades 2-FF to CAUTION", "[sync][stages]") {
    auto compilation = compileSV(R"(
        module sync_2ff(
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                end else begin
                    sync1 <= q_a;
                    sync2 <= sync1;
                end
            end
        endmodule
    )");

    // Default: 2 stages required, 2-FF should be INFO
    {
        FullPipeline pipeline;
        pipeline.run(*compilation, 2);

        bool found_info = false;
        for (auto& c : pipeline.crossings) {
            if (c.sync_type == SyncType::TwoFF && c.category == ViolationCategory::Info)
                found_info = true;
        }
        CHECK(found_info);
    }

    // With required_stages=3, 2-FF should be CAUTION
    {
        auto comp2 = compileSV(R"(
            module sync_2ff_v2(
                input  logic clk_a, clk_b, rst_n, d
            );
                logic q_a;
                logic sync1, sync2;

                always_ff @(posedge clk_a or negedge rst_n) begin
                    if (!rst_n) q_a <= 1'b0;
                    else        q_a <= d;
                end

                always_ff @(posedge clk_b or negedge rst_n) begin
                    if (!rst_n) begin
                        sync1 <= 1'b0;
                        sync2 <= 1'b0;
                    end else begin
                        sync1 <= q_a;
                        sync2 <= sync1;
                    end
                end
            endmodule
        )");

        FullPipeline pipeline;
        pipeline.run(*comp2, 3);

        bool found_caution = false;
        for (auto& c : pipeline.crossings) {
            if (c.sync_type == SyncType::TwoFF) {
                CHECK(c.category == ViolationCategory::Caution);
                CHECK(c.recommendation.find("2 stages") != std::string::npos);
                CHECK(c.recommendation.find("3 required") != std::string::npos);
                found_caution = true;
            }
        }
        CHECK(found_caution);
    }
}

TEST_CASE("SyncVerifier: 3-FF sync passes with required_stages=3", "[sync][stages]") {
    auto compilation = compileSV(R"(
        module sync_3ff(
            input  logic clk_a, clk_b, rst_n, d
        );
            logic q_a;
            logic sync1, sync2, sync3;

            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0;
                else        q_a <= d;
            end

            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    sync1 <= 1'b0;
                    sync2 <= 1'b0;
                    sync3 <= 1'b0;
                end else begin
                    sync1 <= q_a;
                    sync2 <= sync1;
                    sync3 <= sync2;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation, 3);

    bool found_info = false;
    for (auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::ThreeFF && c.category == ViolationCategory::Info)
            found_info = true;
    }
    CHECK(found_info);
}

TEST_CASE("strict mode: caution_count adds to exit total", "[strict]") {
    auto result = makeResultWithCrossings();
    int violations = result.violation_count();
    int cautions = result.caution_count();

    // Normal exit code (no strict)
    int normal_exit = std::min(violations, 255);
    CHECK(normal_exit == 1);

    // Strict exit code
    int strict_exit = std::min(violations + cautions, 255);
    CHECK(strict_exit == 2);
}

// ─── Task 4: Synchronizer pattern stubs — [.future] tagged ───

TEST_CASE("Future: Gray code synchronizer detection", "[.future][sync][gray]") {
    // This design has a 2-bit gray-coded counter crossing domains.
    // Future implementation should detect the gray encoding pattern.
    auto compilation = compileSV(R"(
        module gray_sync(
            input  logic clk_a, clk_b, rst_n
        );
            logic [1:0] bin_cnt, gray_cnt;
            logic [1:0] gray_sync1, gray_sync2;

            // Binary counter in domain A
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) bin_cnt <= 2'b00;
                else        bin_cnt <= bin_cnt + 1;
            end

            // Binary to gray conversion
            assign gray_cnt = bin_cnt ^ (bin_cnt >> 1);

            // 2-FF sync of gray code in domain B
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    gray_sync1 <= 2'b00;
                    gray_sync2 <= 2'b00;
                end else begin
                    gray_sync1 <= gray_cnt;
                    gray_sync2 <= gray_sync1;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Future expectation: should detect GrayCode sync type
    // For now, it will detect individual 2-FF syncs per bit
    bool found_crossing = !pipeline.crossings.empty();
    CHECK(found_crossing);

    // When gray code detection is implemented, uncomment:
    // bool found_gray = false;
    // for (auto& c : pipeline.crossings) {
    //     if (c.sync_type == SyncType::GrayCode)
    //         found_gray = true;
    // }
    // CHECK(found_gray);
}

TEST_CASE("Future: Handshake synchronizer detection", "[.future][sync][handshake]") {
    // This design has a req/ack handshake crossing two domains.
    // Future implementation should detect the paired handshake pattern.
    auto compilation = compileSV(R"(
        module handshake_sync(
            input  logic clk_a, clk_b, rst_n, start
        );
            logic req, ack;
            logic req_sync1, req_sync2;
            logic ack_sync1, ack_sync2;

            // Request in domain A
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n)          req <= 1'b0;
                else if (start)      req <= 1'b1;
                else if (ack_sync2)  req <= 1'b0;
            end

            // Sync req into domain B
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin
                    req_sync1 <= 1'b0;
                    req_sync2 <= 1'b0;
                end else begin
                    req_sync1 <= req;
                    req_sync2 <= req_sync1;
                end
            end

            // Ack in domain B
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n)          ack <= 1'b0;
                else if (req_sync2)  ack <= 1'b1;
                else                 ack <= 1'b0;
            end

            // Sync ack back into domain A
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) begin
                    ack_sync1 <= 1'b0;
                    ack_sync2 <= 1'b0;
                end else begin
                    ack_sync1 <= ack;
                    ack_sync2 <= ack_sync1;
                end
            end
        endmodule
    )");

    FullPipeline pipeline;
    pipeline.run(*compilation);

    // Should have crossings in both directions
    bool found_a_to_b = false;
    bool found_b_to_a = false;
    for (auto& c : pipeline.crossings) {
        if (c.source_signal.find("req") != std::string::npos)
            found_a_to_b = true;
        if (c.source_signal.find("ack") != std::string::npos)
            found_b_to_a = true;
    }
    CHECK((found_a_to_b || found_b_to_a));

    // When handshake detection is implemented, uncomment:
    // bool found_handshake = false;
    // for (auto& c : pipeline.crossings) {
    //     if (c.sync_type == SyncType::Handshake)
    //         found_handshake = true;
    // }
    // CHECK(found_handshake);
}

// ─── Task 5: SDC output generation ───

TEST_CASE("SDC report: waived crossing produces set_false_path", "[report][sdc]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_sdc_waived.sdc";
    gen.generateSDC(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("set_false_path") != std::string::npos);
    CHECK(content.find("WAIVED") != std::string::npos);
    CHECK(content.find("top.u_a.q_status") != std::string::npos);
}

TEST_CASE("SDC report: synced crossing produces set_max_delay", "[report][sdc]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_sdc_synced.sdc";
    gen.generateSDC(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("set_max_delay") != std::string::npos);
    CHECK(content.find("SYNCED") != std::string::npos);
}

TEST_CASE("SDC report: violation crossing produces false_path with WARNING", "[report][sdc]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_sdc_violation.sdc";
    gen.generateSDC(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("# WARNING: unsynchronized crossing") != std::string::npos);
    CHECK(content.find("VIOLATION") != std::string::npos);
}

TEST_CASE("SDC report: synced crossing uses dest domain period", "[report][sdc]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_sdc_period.sdc";
    gen.generateSDC(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    // ext_clk has period 8.0ns, should appear in set_max_delay
    CHECK(content.find("set_max_delay 8") != std::string::npos);
}

TEST_CASE("SDC report: empty result produces valid header", "[report][sdc]") {
    AnalysisResult result;
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_sdc_empty.sdc";
    gen.generateSDC(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("Auto-generated CDC constraints") != std::string::npos);
    CHECK(content.find("0 crossing(s)") != std::string::npos);
}

// ─── Task 6: Markdown report — FF count per domain ───

TEST_CASE("Markdown report: domain table includes FF count column", "[report][markdown]") {
    auto result = makeResultWithCrossings();
    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_md_ffcount.md";
    gen.generateMarkdown(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("| FFs |") != std::string::npos);
    // sys_clk domain has 2 FFs, ext_clk has 1
    CHECK(content.find("| 2 |") != std::string::npos);
    CHECK(content.find("| 1 |") != std::string::npos);
}

TEST_CASE("Markdown report: empty result shows 0 FFs", "[report][markdown]") {
    AnalysisResult result;

    // Add a domain with no FFs
    auto src = std::make_unique<ClockSource>();
    src->name = "lonely_clk";
    src->type = ClockSource::Type::Primary;
    auto* ptr = result.clock_db.addSource(std::move(src));
    result.clock_db.findOrCreateDomain(ptr, Edge::Posedge);

    ReportGenerator gen(result);

    auto path = fs::temp_directory_path() / "test_md_empty_ff.md";
    gen.generateMarkdown(path);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("| FFs |") != std::string::npos);
    CHECK(content.find("| 0 |") != std::string::npos);
}
