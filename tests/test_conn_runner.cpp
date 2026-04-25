#include <catch2/catch_test_macros.hpp>
#include "ConnRunner.h"
#include "TestUtils.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("ConnRunner: clean design exits clean and writes report", "[conn][runner]") {
    auto result = testutils::compileFile("sv/clean_design.sv");
    REQUIRE(result);

    const auto out = fs::temp_directory_path() / "svlens_conn_runner_clean";
    fs::remove_all(out);

    connect::ConnCliOptions opts;
    opts.topModule = "clean_top";
    opts.format = "json";
    opts.outputDir = out.string();

    int exitCode = connect::runConnWithCompilation(*result.compilation, opts);
    CHECK(exitCode == 0);
    CHECK(fs::exists(out / "connect_report.json"));
}

TEST_CASE("ConnRunner: issue design returns non-zero", "[conn][runner]") {
    auto result = testutils::compileFile("sv/mixed_issues.sv");
    REQUIRE(result);

    const auto out = fs::temp_directory_path() / "svlens_conn_runner_issues";
    fs::remove_all(out);

    connect::ConnCliOptions opts;
    opts.topModule = "mixed_top";
    opts.format = "json";
    opts.outputDir = out.string();

    int exitCode = connect::runConnWithCompilation(*result.compilation, opts);
    CHECK(exitCode > 0);
    CHECK(fs::exists(out / "connect_report.json"));
}

// Phase B paired fixtures: interface modport width across hierarchy.
// Pos drives an 8-bit modport.data into a 16-bit consumer port;
// neg keeps everything 8-bit. Both fixtures currently report 0
// errors -- the paired pair codifies the modport-width-inference
// gap (Phase B prio #1) so any future hardening that closes the gap
// will surface as a test diff.
TEST_CASE("ConnRunner: modport-width paired fixtures stay deterministic",
          "[conn][runner][modport_width]") {
    auto pos = testutils::compileFile("sv/conn_modport_width_pos.sv");
    REQUIRE(pos);
    const auto out_pos =
        fs::temp_directory_path() / "svlens_conn_modport_width_pos";
    fs::remove_all(out_pos);
    connect::ConnCliOptions opts_pos;
    opts_pos.topModule = "conn_modport_width_pos";
    opts_pos.format = "json";
    opts_pos.outputDir = out_pos.string();
    int exitPos =
        connect::runConnWithCompilation(*pos.compilation, opts_pos);
    // Round 30 US-R05: gap closed via absolute-hier-path netKey on
    // both endpoints. resolveExpr(MemberAccess into ModportPort) now
    // returns the underlying signal's getHierarchicalPath() when
    // internalSymbol is non-null; the modport-expansion side emits a
    // matching abs-path entry into netMap_ so the connection forms.
    CHECK(exitPos > 0);
    CHECK(fs::exists(out_pos / "connect_report.json"));

    auto neg = testutils::compileFile("sv/conn_modport_width_neg.sv");
    REQUIRE(neg);
    const auto out_neg =
        fs::temp_directory_path() / "svlens_conn_modport_width_neg";
    fs::remove_all(out_neg);
    connect::ConnCliOptions opts_neg;
    opts_neg.topModule = "conn_modport_width_neg";
    opts_neg.format = "json";
    opts_neg.outputDir = out_neg.string();
    int exitNeg =
        connect::runConnWithCompilation(*neg.compilation, opts_neg);
    CHECK(exitNeg == 0);
    CHECK(fs::exists(out_neg / "connect_report.json"));
}
