#include <catch2/catch_test_macros.hpp>
#include "CdcRunner.h"
#include "CompilationSession.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string cdcFixture(const std::string& name) {
    return (fs::path(TEST_CDC_DIR) / name).string();
}

TEST_CASE("CdcRunner: missing_sync fixture returns violation exit code and writes report", "[cdc][runner]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_runner_violation";
    fs::remove_all(out);

    cdccli::CdcCliOptions opts;
    opts.outputDir = out.string();
    opts.format = "json";

    int exitCode = cdccli::runCdcWithCompilation(session.compilation(), opts);
    CHECK(exitCode == 1);
    CHECK(fs::exists(out / "cdc_report.json"));
}

TEST_CASE("CdcRunner: synchronized fixture returns clean exit code", "[cdc][runner]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("03_two_ff_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_runner_clean";
    fs::remove_all(out);

    cdccli::CdcCliOptions opts;
    opts.outputDir = out.string();
    opts.format = "json";

    int exitCode = cdccli::runCdcWithCompilation(session.compilation(), opts);
    CHECK(exitCode == 0);
    CHECK(fs::exists(out / "cdc_report.json"));
}

TEST_CASE("CdcRunner: --sync-stages=3 downgrades 2-FF chain from INFO to CAUTION",
          "[cdc][runner][sync_stages]") {
    // Round 15 US-E01: a 2-FF synchronizer fixture (03_two_ff_sync)
    // classifies as INFO at the default --sync-stages=2. Bumping
    // the requirement to 3 stages makes that 2-FF chain
    // structurally insufficient -- it is downgraded to CAUTION.
    // Locks the CLI flag so a future refactor doesn't drop it.
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("03_two_ff_sync.sv")};
    REQUIRE(session.compile(args));

    // Default 2-stage requirement -> INFO, exit 0.
    const auto out_def = fs::temp_directory_path() / "svlens_stages_default";
    fs::remove_all(out_def);
    cdccli::CdcCliOptions opts_def;
    opts_def.outputDir = out_def.string();
    opts_def.format = "json";
    opts_def.syncStages = 2;
    int rc_def = cdccli::runCdcWithCompilation(session.compilation(), opts_def);
    CHECK(rc_def == 0);

    // 3-stage requirement -> 2-FF is short; classification becomes
    // CAUTION (caution count > 0, exit code stays 0 unless --strict).
    const auto out3 = fs::temp_directory_path() / "svlens_stages_3";
    fs::remove_all(out3);
    cdccli::CdcCliOptions opts3;
    opts3.outputDir = out3.string();
    opts3.format = "json";
    opts3.syncStages = 3;
    int rc3 = cdccli::runCdcWithCompilation(session.compilation(), opts3);
    CHECK(rc3 == 0);  // CAUTION alone doesn't change exit code

    // Read JSON and verify the caution count climbed under stages=3.
    // Match `"cautions"` followed by any whitespace and then `1` so
    // a future change to the JSON serializer's spacing does not
    // silently break this assertion.
    std::ifstream f3((out3 / "cdc_report.json").string());
    REQUIRE(f3.good());
    std::string body((std::istreambuf_iterator<char>(f3)),
                     std::istreambuf_iterator<char>());
    auto key_pos = body.find("\"cautions\"");
    REQUIRE(key_pos != std::string::npos);
    auto colon = body.find(':', key_pos);
    REQUIRE(colon != std::string::npos);
    size_t i = colon + 1;
    while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i])))
        ++i;
    CHECK(i < body.size());
    CHECK(body[i] == '1');
}

TEST_CASE("CdcRunner: --ignore-gated suppresses Low-severity gated entries",
          "[cdc][runner][ignore_gated]") {
    // Round 16: fixture 51 produces a single Severity::Low INFO
    // crossing under the companion SDC (gated_clk declared as
    // generated from ca). With --ignore-gated, that entry is
    // erased from the report; without it, the entry remains.
    // Locks the CdcRunnerUtils.cpp:212-218 erase filter.
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("51_gated_clock_low_sev.sv")};
    REQUIRE(session.compile(args));

    // Without --ignore-gated: 1 INFO crossing.
    const auto out_keep = fs::temp_directory_path() / "svlens_gated_keep";
    fs::remove_all(out_keep);
    cdccli::CdcCliOptions opts_keep;
    opts_keep.outputDir = out_keep.string();
    opts_keep.format = "json";
    opts_keep.sdcFile = cdcFixture("51_gated_clock_low_sev.sdc");
    opts_keep.ignoreGated = false;
    int rc_keep = cdccli::runCdcWithCompilation(session.compilation(), opts_keep);
    CHECK(rc_keep == 0);
    std::ifstream fk((out_keep / "cdc_report.json").string());
    REQUIRE(fk.good());
    std::string body_keep((std::istreambuf_iterator<char>(fk)),
                          std::istreambuf_iterator<char>());
    CHECK(body_keep.find("INFO-1") != std::string::npos);

    // With --ignore-gated: the Low-severity crossing is erased.
    const auto out_drop = fs::temp_directory_path() / "svlens_gated_drop";
    fs::remove_all(out_drop);
    cdccli::CdcCliOptions opts_drop;
    opts_drop.outputDir = out_drop.string();
    opts_drop.format = "json";
    opts_drop.sdcFile = cdcFixture("51_gated_clock_low_sev.sdc");
    opts_drop.ignoreGated = true;
    int rc_drop = cdccli::runCdcWithCompilation(session.compilation(), opts_drop);
    CHECK(rc_drop == 0);
    std::ifstream fd((out_drop / "cdc_report.json").string());
    REQUIRE(fd.good());
    std::string body_drop((std::istreambuf_iterator<char>(fd)),
                          std::istreambuf_iterator<char>());
    CHECK(body_drop.find("INFO-1") == std::string::npos);
}

TEST_CASE("CdcRunner: --emit-sva writes SVA file alongside the JSON report",
          "[cdc][runner][emit_sva]") {
    // Phase C: --emit-sva flag produces an SVA assertion file beside
    // the JSON report. For a VIOLATION crossing the file contains a
    // `cover property (cdc_<id>_src_toggle)` block; identifiers are
    // sanitized so dashes from auto-generated ids do not break SVA
    // parsing. Locks the wiring between CdcCliOptions::svaOutputFile
    // and ReportGenerator::generateSVA in emitCdcReports.
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_emit_sva";
    fs::remove_all(out);
    fs::create_directories(out);

    cdccli::CdcCliOptions opts;
    opts.topModule = "missing_sync";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();

    int exitCode = cdccli::runCdcWithCompilation(session.compilation(), opts);
    CHECK(exitCode == 1);

    REQUIRE(fs::exists(opts.svaOutputFile));
    std::ifstream ifs(opts.svaOutputFile);
    std::string sva((std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>());

    CHECK(sva.find("// Top:        missing_sync") != std::string::npos);
    // Sanitized identifier (no dash) and cover-property wrapping.
    CHECK(sva.find("property cdc_VIOLATION_1_src_toggle") != std::string::npos);
    CHECK(sva.find("cover property (cdc_VIOLATION_1_src_toggle)") !=
          std::string::npos);
}

TEST_CASE("CdcRunner: --strict elevates CAUTION-only fixture to non-zero exit",
          "[cdc][runner][strict]") {
    // Round 14 US-D03: fixture 15 produces 1 CAUTION (Ac_cdc04
    // wide-bus). Without --strict the exit code is 0 (no
    // violations); with --strict the CAUTION counts as a
    // violation and the exit code becomes non-zero. Locks the
    // CLI flag's behavior so a future refactor doesn't silently
    // drop it.
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("15_bus_cdc_no_gray.sv")};
    REQUIRE(session.compile(args));

    const auto out_off = fs::temp_directory_path() / "svlens_strict_off";
    fs::remove_all(out_off);
    cdccli::CdcCliOptions opts_off;
    opts_off.outputDir = out_off.string();
    opts_off.format = "json";
    opts_off.strict = false;
    int rc_off = cdccli::runCdcWithCompilation(session.compilation(), opts_off);
    CHECK(rc_off == 0);

    const auto out_on = fs::temp_directory_path() / "svlens_strict_on";
    fs::remove_all(out_on);
    cdccli::CdcCliOptions opts_on;
    opts_on.outputDir = out_on.string();
    opts_on.format = "json";
    opts_on.strict = true;
    int rc_on = cdccli::runCdcWithCompilation(session.compilation(), opts_on);
    CHECK(rc_on != 0);
}
