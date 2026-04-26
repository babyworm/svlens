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

    // Drive each lowRISC-style category through the fixture and
    // assert: GOOD modules emit zero CONVENTION INFO entries; the
    // BAD modules emit the expected violations. This locks the
    // SUFFIX-mode contract (lowRISC `_i`/`_o`/`_io`) plus
    // differential-pair (`_po`/`_no`) acceptance and digit-tail
    // rejection (lowRISC: `foo_1` -> use `foo_q2` instead).
    auto run_top = [&](const std::string& top) {
        const auto out_top = out / top;
        fs::remove_all(out_top);
        connect::ConnCliOptions o;
        o.topModule = top;
        o.format = "json";
        o.outputDir = out_top.string();
        o.checkConvention = true;
        o.conventionFile = yaml_path.string();
        connect::runConnWithCompilation(*result.compilation, o);
        std::ifstream ifs(out_top / "connect_report.json");
        return std::string((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
    };

    auto count = [](const std::string& body, const std::string& needle) {
        size_t n = 0, p = 0;
        while ((p = body.find(needle, p)) != std::string::npos) {
            ++n; p += needle.size();
        }
        return n;
    };

    // GOOD modules: zero CONVENTION INFO each.
    for (const auto& top : {std::string("ports_good"),
                            std::string("ports_simple_good"),
                            std::string("clocks_good"),
                            std::string("resets_good"),
                            std::string("instances_good")}) {
        auto body = run_top(top);
        INFO("top=" << top);
        // No "CONVENTION" type issue should appear in the GOOD list.
        // Note: the JSON renderer wraps Issue::Type::CONVENTION as
        // string "CONVENTION".
        CHECK(count(body, "\"type\": \"CONVENTION\"") == 0);
    }

    // BAD: ports_bad triggers many rule violations.
    auto bad_body = run_top("ports_bad");
    CHECK(bad_body.find("'RstN' is not lowercase") != std::string::npos);
    CHECK(bad_body.find("'dataIn' is not lowercase") != std::string::npos);
    CHECK(bad_body.find("'enable_n' has active-low suffix") !=
          std::string::npos);
    CHECK(bad_body.find("instance 'bad_inst'") != std::string::npos);
    // ports `clk_i`, `i_command`, `o_status` (plus the GOOD-suffix
    // forms) must not be flagged.
    CHECK(bad_body.find("port 'clk_i' does not") == std::string::npos);

    // BAD: digit_suffix_bad triggers the reject_digit_only_suffix rule.
    // The detail message contains `_<digit>` which the JSON renderer
    // escapes the `<` to `\u003c`, so we search on a stable prefix.
    auto digit_body = run_top("digit_suffix_bad");
    CHECK(digit_body.find("'foo_1' has") != std::string::npos);
    CHECK(digit_body.find("'foo_2' has") != std::string::npos);
    CHECK(digit_body.find("lowRISC prohibits this") != std::string::npos);

    // Round 38 US-38A: legacy `always` block detection.
    auto la_body = run_top("legacy_always_bad");
    // Two legacy always blocks; both should surface.
    CHECK(count(la_body, "legacy `always` block") == 2);
    auto modern_body = run_top("modern_always_good");
    CHECK(count(modern_body, "legacy `always` block") == 0);

    // Round 38 US-38D / US-38E: parameter case + typedef suffix.
    auto pt_bad = run_top("param_typedef_bad");
    CHECK(pt_bad.find("parameter 'width'") != std::string::npos);
    CHECK(pt_bad.find("parameter 'MAX_DEPTH'") != std::string::npos);
    CHECK(pt_bad.find("typedef 'my_byte'") != std::string::npos);
    CHECK(pt_bad.find("typedef 'op'") != std::string::npos);
    auto pt_good = run_top("param_typedef_good");
    CHECK(pt_good.find("parameter '") == std::string::npos);
    CHECK(pt_good.find("typedef '") == std::string::npos);

    // Round 38 US-38B: anonymous enum detection.
    auto ae_bad = run_top("anonymous_enum_bad");
    CHECK(ae_bad.find("anonymous enum bound to 'req_access'") !=
          std::string::npos);
    auto pt_good_no_enum = pt_good;
    CHECK(pt_good_no_enum.find("anonymous enum") == std::string::npos);

    // Round 38 US-38C: generate block naming.
    auto ug_bad = run_top("unnamed_generate_bad");
    CHECK(ug_bad.find("generate-for array") != std::string::npos);
    auto ng_good = run_top("named_generate_good");
    CHECK(ng_good.find("generate-for array") == std::string::npos);
    CHECK(ng_good.find("generate block at") == std::string::npos);

    // Round 38 US-38G: case unique + default check.
    auto cs_bad = run_top("case_bad");
    CHECK(cs_bad.find("`unique`/`priority`") != std::string::npos);
    CHECK(cs_bad.find("`default:` branch") != std::string::npos);
    auto cs_good = run_top("case_good");
    CHECK(cs_good.find("case statement") == std::string::npos);

    // Round 38 US-38I: 2-state type rejection.
    auto ts_bad = run_top("two_state_bad");
    CHECK(ts_bad.find("'bit'") != std::string::npos);
    CHECK(ts_bad.find("'int'") != std::string::npos);
    CHECK(ts_bad.find("'byte'") != std::string::npos);
    auto ts_good = run_top("modern_always_good");
    CHECK(ts_good.find("2-state/non-logic type") == std::string::npos);
}
