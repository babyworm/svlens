#include <catch2/catch_test_macros.hpp>
#include "ConnRunner.h"
#include "TestUtils.h"

#include <filesystem>
#include <fstream>
#include <iterator>

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

TEST_CASE("ConnRunner: lowRISC-style YAML produces expected INFO violations",
          "[conn][runner][lowrisc_style]") {
    // Round 36: end-to-end exercise of the lowRISC-style convention
    // YAML against a fixture that intentionally violates several
    // rules. Verifies the new patterns (clock/reset/lowercase/
    // active-low) wire through ConventionChecker correctly and that
    // properly-named ports (clk, i_command, o_status) are NOT
    // flagged. All issues remain at Severity::INFO so exit code is
    // unaffected.
    auto result = testutils::compileFile("sv/lowrisc_style_violations.sv");
    REQUIRE(result);

    const auto out =
        fs::temp_directory_path() / "svlens_conn_lowrisc_style";
    fs::remove_all(out);

    // Use absolute path resolved from TEST_SV_DIR so the test is
    // location-independent (ctest WORKING_DIRECTORY is the build
    // directory; the YAML file lives in the source tree).
    auto yaml_path =
        fs::path(TEST_SV_DIR).parent_path().parent_path() /
        "examples" / "styles" / "lowrisc.yaml";
    REQUIRE(fs::exists(yaml_path));

    connect::ConnCliOptions opts;
    opts.topModule = "lowrisc_violator";
    opts.format = "json";
    opts.outputDir = out.string();
    opts.checkConvention = true;
    opts.conventionFile = yaml_path.string();

    int exitCode = connect::runConnWithCompilation(*result.compilation, opts);
    // ConnRunner counts ALL active issues (including INFO) into the
    // exit code, capped at 255. The fixture violates 8 lowRISC rules.
    CHECK(exitCode > 0);
    REQUIRE(fs::exists(out / "connect_report.json"));

    std::ifstream ifs(out / "connect_report.json");
    std::string body((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());

    // Specific violations the YAML should surface:
    CHECK(body.find("'RstN' is not lowercase") != std::string::npos);
    CHECK(body.find("'dataIn' is not lowercase") != std::string::npos);
    CHECK(body.find("'enable_n' has active-low suffix") !=
          std::string::npos);
    CHECK(body.find("instance 'bad'") != std::string::npos);
    // Properly-named ports must NOT appear:
    CHECK(body.find("port 'clk' does not") == std::string::npos);
    CHECK(body.find("port 'i_command' does not") == std::string::npos);
    CHECK(body.find("port 'o_status' does not") == std::string::npos);
}
