#include <catch2/catch_test_macros.hpp>
#include "CdcRunner.h"
#include "CompilationSession.h"

#include <filesystem>

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
