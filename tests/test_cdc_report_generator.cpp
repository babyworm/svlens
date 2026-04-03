#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/types.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static AnalysisResult makeCdcResult() {
    AnalysisResult result;

    auto sys = std::make_unique<ClockSource>();
    sys->name = "sys_clk";
    sys->type = ClockSource::Type::Primary;
    auto* sysPtr = result.clock_db.addSource(std::move(sys));
    auto* sysDom = result.clock_db.findOrCreateDomain(sysPtr, Edge::Posedge);

    auto ext = std::make_unique<ClockSource>();
    ext->name = "ext_clk";
    ext->type = ClockSource::Type::AutoDetected;
    auto* extPtr = result.clock_db.addSource(std::move(ext));
    auto* extDom = result.clock_db.findOrCreateDomain(extPtr, Edge::Posedge);

    result.clock_db.relationships.push_back({sysPtr, extPtr, DomainRelationship::Type::Asynchronous});

    CrossingReport violation;
    violation.id = "VIOLATION-001";
    violation.category = ViolationCategory::Violation;
    violation.severity = Severity::High;
    violation.source_signal = "top.u_a.q_data";
    violation.dest_signal = "top.u_b.q_data";
    violation.source_domain = sysDom;
    violation.dest_domain = extDom;
    violation.sync_type = SyncType::None;
    violation.recommendation = "Insert 2-FF synchronizer";
    result.crossings.push_back(violation);

    CrossingReport info;
    info.id = "INFO-001";
    info.category = ViolationCategory::Info;
    info.severity = Severity::Info;
    info.source_signal = "top.u_a.q_sync";
    info.dest_signal = "top.u_b.sync_ff2";
    info.source_domain = sysDom;
    info.dest_domain = extDom;
    info.sync_type = SyncType::TwoFF;
    result.crossings.push_back(info);

    return result;
}

TEST_CASE("CDC ReportGenerator: counts reflect crossing categories", "[cdc][report]") {
    auto result = makeCdcResult();
    CHECK(result.violation_count() == 1);
    CHECK(result.info_count() == 1);
    CHECK(result.caution_count() == 0);
}

TEST_CASE("CDC ReportGenerator: markdown output contains summary and domains", "[cdc][report]") {
    auto result = makeCdcResult();
    ReportGenerator generator(result);

    auto path = fs::temp_directory_path() / "svlens_cdc_report.md";
    generator.generateMarkdown(path);

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("# CDC Analysis Report") != std::string::npos);
    CHECK(content.find("VIOLATION | 1") != std::string::npos);
    CHECK(content.find("sys_clk") != std::string::npos);
    CHECK(content.find("ext_clk") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: json output contains summary and crossing ids", "[cdc][report]") {
    auto result = makeCdcResult();
    ReportGenerator generator(result);

    auto path = fs::temp_directory_path() / "svlens_cdc_report.json";
    generator.generateJSON(path);

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"summary\"") != std::string::npos);
    CHECK(content.find("\"violations\": 1") != std::string::npos);
    CHECK(content.find("VIOLATION-001") != std::string::npos);
    CHECK(content.find("INFO-001") != std::string::npos);
}
