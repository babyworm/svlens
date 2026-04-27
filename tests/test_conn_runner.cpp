#include <catch2/catch_test_macros.hpp>
#include "CompilationSession.h"
#include "ConnRunner.h"
#include "TestUtils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

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

    // GOOD modules: zero AST-side CONVENTION INFO each.  The shipped
    // example yaml now wires source-text + file-naming rules (Fresh
    // review R1A MAJOR #2), so the file-level violations of
    // lowrisc_style_violations.sv (long lines, multiple modules,
    // basename mismatch) DO surface even on GOOD-module tops because
    // those rules apply at file granularity.  Assert no AST-side rule
    // fires on a GOOD top by checking the rule-specific detail markers
    // are absent.
    for (const auto& top : {std::string("ports_good"),
                            std::string("ports_simple_good"),
                            std::string("clocks_good"),
                            std::string("resets_good"),
                            std::string("instances_good")}) {
        auto body = run_top(top);
        INFO("top=" << top);
        // AST-side rule detail substrings; none must appear for GOOD tops.
        CHECK(body.find("does not follow naming convention") == std::string::npos);
        CHECK(body.find("is not lowercase") == std::string::npos);
        CHECK(body.find("active-low suffix") == std::string::npos);
        CHECK(body.find("legacy `always` block") == std::string::npos);
        CHECK(body.find("anonymous enum") == std::string::npos);
        CHECK(body.find("bare integer literal") == std::string::npos);
        CHECK(body.find("wildcard port connection") == std::string::npos);
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

    // Round 38 US-38F: always_ff registered-output `_q` suffix.
    auto fq_bad = run_top("ff_q_suffix_bad");
    CHECK(fq_bad.find("'data_value' lacks `_q`") != std::string::npos);
    CHECK(fq_bad.find("'state' lacks `_q`") != std::string::npos);
    auto fq_good = run_top("ff_q_suffix_good");
    CHECK(fq_good.find("lacks `_q`") == std::string::npos);

    // R1 MAJOR: anchor `_q` suffix at end of leaf.  The previous
    // rfind("_q") form matched mid-string, so `data_qual_next` was
    // flagged because `_q` inside `_qual` was found and the trailing
    // `ual_next` failed the digit check.  After anchoring, the BAD
    // names `data_qual_next` and `data_q_next` MUST surface as
    // MissingQSuffix (they are still always_ff NB-LHS without
    // end-anchored `_q`), while `data_q` and `data_q2` MUST NOT.
    auto qa_body = run_top("q_suffix_anchor_check");
    CHECK(qa_body.find("'data_qual_next' lacks `_q`") != std::string::npos);
    CHECK(qa_body.find("'data_q_next' lacks `_q`") != std::string::npos);
    CHECK(qa_body.find("'data_q' lacks `_q`") == std::string::npos);
    CHECK(qa_body.find("'data_q2' lacks `_q`") == std::string::npos);

    // Round 39 US-39A: reset-polarity check.
    // reset_polarity_bad uses comma syntax @(posedge clk_i, negedge rst_ni)
    // which must be flagged. reset_polarity_good uses `or` and must be clean.
    auto rp_bad = run_top("reset_polarity_bad");
    CHECK(rp_bad.find("comma syntax") != std::string::npos);
    auto rp_good = run_top("reset_polarity_good");
    CHECK(rp_good.find("comma syntax") == std::string::npos);
    CHECK(rp_good.find("reset-polarity") == std::string::npos);

    // R1 MAJOR: bracket-indexed active-low reset must NOT be flagged
    // as active-high.  Before the stripIndex fix, `rst_n_arr[0]`
    // failed the ends_with("_n") test on the literal bracket form
    // and was misclassified.
    auto rpb_good = run_top("reset_polarity_bracketed_good");
    CHECK(rpb_good.find("active-high reset") == std::string::npos);

    // Round 39 US-39B: FF d-suffix check.
    // ff_d_suffix_bad has valid_q in always_ff but comb input is
    // named valid_next (not valid_d) -- should emit MissingDSuffix.
    // ff_d_suffix_good uses canonical valid_d -> valid_q -- clean.
    auto fd_bad = run_top("ff_d_suffix_bad");
    CHECK(fd_bad.find("'valid_q'") != std::string::npos);
    CHECK(fd_bad.find("valid_d") != std::string::npos);
    auto fd_good = run_top("ff_d_suffix_good");
    CHECK(fd_good.find("has no matching combinational input") == std::string::npos);

    // Round 39 US-39C: wildcard port connection (`.*`) detection.
    // dotstar_bad uses `.*` -- must emit at least one WildcardPortConnection INFO.
    // dotstar_good uses explicit named connections -- must be clean for this kind.
    auto ds_bad = run_top("dotstar_bad");
    CHECK(ds_bad.find("wildcard port connection") != std::string::npos);
    CHECK(ds_bad.find("hides signal-to-port mapping") != std::string::npos);
    auto ds_good = run_top("dotstar_good");
    CHECK(ds_good.find("wildcard port connection") == std::string::npos);

    // Round 39 US-39D: bare integer literal (width-less) detection.
    // width_lit_bad has `assign data_o = count_q + 2` -- bare `2` must be flagged.
    // width_lit_good uses `8'd2` and `'0` -- must be clean for this kind.
    auto wl_bad = run_top("width_lit_bad");
    CHECK(wl_bad.find("bare integer literal") != std::string::npos);
    CHECK(wl_bad.find("no explicit width") != std::string::npos);
    auto wl_good = run_top("width_lit_good");
    CHECK(wl_good.find("bare integer literal") == std::string::npos);
}

TEST_CASE("ConnRunner: source-text + file-name rules emit when YAML enables them",
          "[conn][runner][source_text]") {
    // Round 39 US-39E + US-39F: source-text rules (line length,
    // hard tabs, trailing whitespace) + file-naming rules (one
    // module per file, file basename matches module name) are
    // OFF by default; YAML must opt-in with explicit values.
    auto result =
        testutils::compileFile("sv/lowrisc_source_text_violations.sv");
    REQUIRE(result);

    const auto out =
        fs::temp_directory_path() / "svlens_conn_source_text";
    fs::remove_all(out);
    fs::create_directories(out);

    // Synthesize a strict YAML inline so the existing
    // examples/styles/lowrisc.yaml defaults stay loose.
    auto yaml_path = out / "strict.yaml";
    {
        std::ofstream y(yaml_path);
        y << "input_suffix: \"_i,_pi,_ni\"\n"
          << "output_suffix: \"_o,_po,_no\"\n"
          << "input_prefix: \"\"\n"
          << "output_prefix: \"\"\n"
          << "instance_prefix: \"u_\"\n"
          << "max_line_length: 80\n"
          << "prohibit_hard_tabs: true\n"
          << "prohibit_trailing_whitespace: true\n"
          << "prohibit_multiple_modules_per_file: true\n"
          << "enforce_file_module_match: true\n";
    }

    connect::ConnCliOptions opts;
    opts.topModule = "source_text_primary";
    opts.format = "json";
    opts.outputDir = (out / "report").string();
    opts.checkConvention = true;
    opts.conventionFile = yaml_path.string();

    int exitCode =
        connect::runConnWithCompilation(*result.compilation, opts);
    CHECK(exitCode > 0);
    REQUIRE(fs::exists(fs::path(opts.outputDir) / "connect_report.json"));
    std::ifstream ifs(fs::path(opts.outputDir) / "connect_report.json");
    std::string body((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());

    CHECK(body.find("line length") != std::string::npos);
    CHECK(body.find("exceeds max") != std::string::npos);
    CHECK(body.find("hard tab character") != std::string::npos);
    CHECK(body.find("trailing whitespace") != std::string::npos);
    CHECK(body.find("modules declared in one file") != std::string::npos);

    // Round 39 review (R2 #3): bind line-number assertions to the
    // actual line offsets in lowrisc_source_text_violations.sv so a
    // future regression in line-number tracking surfaces as a test
    // failure rather than a silent swap.  Fixture layout:
    //   line 13: hard tab indent + line >100 chars
    //   line 14: trailing whitespace before EOL
    //   line 22: second module declaration
    CHECK(body.find(":13:") != std::string::npos);
    CHECK(body.find(":14:") != std::string::npos);

    // Verify the structured `line` JSON field is emitted by
    // JsonReport, not just the line marker inside detail.
    // SourceTextScanner populates obs.lineNumber, ConventionChecker
    // copies it onto Issue, and JsonReport emits `"line": <n>` when
    // nonzero.
    CHECK(body.find("\"line\": 13") != std::string::npos);
    CHECK(body.find("\"line\": 14") != std::string::npos);

    // Fresh review R1A MAJOR #1: schema advertises optional `column`
    // for source-text observations, but until the scanner started
    // populating columnNumber the field was suppressed by JsonReport.
    // Lock in the contract by asserting that at least one column shows
    // up in the JSON.  Fixture layout:
    //   line 13: hard tab is the first character of the line
    //           -> HardTab column == 1
    //   line 13: line length 100+ exceeds max=80
    //           -> LineTooLong column == 81 (maxLineLength + 1)
    CHECK(body.find("\"column\": 1") != std::string::npos);
    CHECK(body.find("\"column\": 81") != std::string::npos);
}

TEST_CASE("ConnRunner: shipped lowrisc.yaml example wires source-text rules end-to-end",
          "[conn][runner][source_text][lowrisc_example]") {
    // Fresh review R1A MAJOR #2: ConventionChecker has read
    // max_line_length / prohibit_hard_tabs /
    // prohibit_trailing_whitespace / prohibit_multiple_modules_per_file
    // / enforce_file_module_match since v0.3.2, but the canonical
    // example yaml shipped without any of these keys.  Users copying
    // examples/styles/lowrisc.yaml got silent under-enforcement.
    // This regression test loads the example yaml file directly (not a
    // synthesized strict.yaml) and verifies the source-text rules
    // actually fire on the canonical violation fixture.
    auto result =
        testutils::compileFile("sv/lowrisc_source_text_violations.sv");
    REQUIRE(result);

    const auto out =
        fs::temp_directory_path() / "svlens_conn_lowrisc_example_source_text";
    fs::remove_all(out);
    fs::create_directories(out);

    auto yaml_path = fs::path(TEST_SV_DIR).parent_path().parent_path() /
                     "examples" / "styles" / "lowrisc.yaml";
    REQUIRE(fs::exists(yaml_path));

    connect::ConnCliOptions opts;
    opts.topModule = "source_text_primary";
    opts.format = "json";
    opts.outputDir = (out / "report").string();
    opts.checkConvention = true;
    opts.conventionFile = yaml_path.string();

    connect::runConnWithCompilation(*result.compilation, opts);
    REQUIRE(fs::exists(fs::path(opts.outputDir) / "connect_report.json"));
    std::ifstream ifs(fs::path(opts.outputDir) / "connect_report.json");
    std::string body((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());

    // Each opt-in source-text / file-naming rule from the shipped
    // example MUST fire on the canonical violation fixture.
    CHECK(body.find("hard tab character") != std::string::npos);
    CHECK(body.find("trailing whitespace") != std::string::npos);
    CHECK(body.find("exceeds max") != std::string::npos);
    CHECK(body.find("modules declared in one file") != std::string::npos);
}

TEST_CASE("ConnRunner: source-text emits FileNameMismatch for mismatched basename",
          "[conn][runner][source_text][file_name_mismatch]") {
    // Round 39 review (R5 Gap 1): the FileNameMismatch rule was being
    // emitted but never asserted on by tests, so any regression that
    // silently disabled it would have gone unnoticed.  Fixture has
    // exactly one module whose name differs from the file basename.
    auto result = testutils::compileFile("sv/lowrisc_filename_mismatch.sv");
    REQUIRE(result);

    const auto out = fs::temp_directory_path() / "svlens_conn_filename_mismatch";
    fs::remove_all(out);
    fs::create_directories(out);

    auto yaml_path = out / "strict.yaml";
    {
        std::ofstream y(yaml_path);
        y << "input_suffix: \"_i,_pi,_ni\"\n"
          << "output_suffix: \"_o,_po,_no\"\n"
          << "input_prefix: \"\"\n"
          << "output_prefix: \"\"\n"
          << "instance_prefix: \"u_\"\n"
          << "prohibit_multiple_modules_per_file: true\n"
          << "enforce_file_module_match: true\n";
    }

    connect::ConnCliOptions opts;
    opts.topModule = "not_matching_module_name";
    opts.format = "json";
    opts.outputDir = (out / "report").string();
    opts.checkConvention = true;
    opts.conventionFile = yaml_path.string();

    connect::runConnWithCompilation(*result.compilation, opts);
    REQUIRE(fs::exists(fs::path(opts.outputDir) / "connect_report.json"));
    std::ifstream ifs(fs::path(opts.outputDir) / "connect_report.json");
    std::string body((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    // The detail message format from SourceTextScanner is:
    //   "<path>: file basename '<base>' does not match module name '<mod>' ..."
    CHECK(body.find("file basename") != std::string::npos);
    CHECK(body.find("does not match module name") != std::string::npos);
    CHECK(body.find("not_matching_module_name") != std::string::npos);
    // MultipleModulesPerFile must NOT fire (single module).
    CHECK(body.find("modules declared in one file") == std::string::npos);
}

TEST_CASE("ConnRunner: source-text reachability suppresses unrelated file",
          "[conn][runner][source_text][reachability][unrelated]") {
    // More direct test for SourceTextScanner reachability gating.
    // Compile two files together (the clean top fixture and the
    // unrelated sibling fixture) and request the clean top.  The
    // sibling file's violations must NOT appear in the report because
    // its only module is not reachable from the requested top.
    namespace fs = std::filesystem;
    auto session = std::make_unique<connect::CompilationSession>();
    auto top_path = fs::path(TEST_SV_DIR) / "clean_source_text.sv";
    auto sib_path = fs::path(TEST_SV_DIR) / "lowrisc_unreachable_sibling.sv";
    REQUIRE(fs::exists(top_path));
    REQUIRE(fs::exists(sib_path));
    std::vector<std::string> args = {"test", top_path.string(), sib_path.string()};
    REQUIRE(session->compile(args));

    const auto out = fs::temp_directory_path() / "svlens_conn_source_text_unrelated";
    fs::remove_all(out);
    fs::create_directories(out);

    auto yaml_path = out / "strict.yaml";
    {
        std::ofstream y(yaml_path);
        y << "input_suffix: \"_i,_pi,_ni\"\n"
          << "output_suffix: \"_o,_po,_no\"\n"
          << "input_prefix: \"\"\n"
          << "output_prefix: \"\"\n"
          << "instance_prefix: \"u_\"\n"
          << "max_line_length: 80\n"
          << "prohibit_hard_tabs: true\n"
          << "prohibit_trailing_whitespace: true\n";
    }

    connect::ConnCliOptions opts;
    opts.topModule = "clean_source_text"; // clean fixture is the top
    opts.format = "json";
    opts.outputDir = (out / "report").string();
    opts.checkConvention = true;
    opts.conventionFile = yaml_path.string();

    connect::runConnWithCompilation(session->compilation(), opts);
    REQUIRE(fs::exists(fs::path(opts.outputDir) / "connect_report.json"));
    std::ifstream ifs(fs::path(opts.outputDir) / "connect_report.json");
    std::string body((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    // The unrelated_sibling fixture has hard tab, long line, and
    // trailing whitespace.  None of these must appear because the
    // sibling file contains no module reachable from
    // `clean_source_text`.  The path basename "lowrisc_unreachable_sibling"
    // is stable enough to use as a positive substring check.
    CHECK(body.find("lowrisc_unreachable_sibling") == std::string::npos);
    CHECK(body.find("hard tab character") == std::string::npos);
}

TEST_CASE("ConnRunner: source-text scope is file-level (sibling violations included)",
          "[conn][runner][source_text][reachability][sibling_lockin]") {
    // SourceTextScanner reachability is FILE-LEVEL: any reachable
    // module in a buffer admits the whole buffer.  This is
    // intentional -- physical-line rules like
    // LineTooLong cannot be attributed to a single module declaration
    // when a line could span declarations.  Sibling code in the same
    // file is NOT exempt; users must put unrelated code in a separate
    // file to escape these checks.
    //
    // This test LOCKS IN that contract: with `unreachable_top` (clean)
    // selected as the requested top, the same file's `unrelated_sibling`
    // (intentionally dirty) violations MUST appear in the report.  If a
    // future change tightens the scope to module-level, this test fails
    // and the contract change is visible.
    namespace fs = std::filesystem;
    auto result = testutils::compileFile("sv/lowrisc_unreachable_sibling.sv");
    REQUIRE(result);

    const auto out = fs::temp_directory_path() / "svlens_conn_source_text_sibling";
    fs::remove_all(out);
    fs::create_directories(out);

    auto yaml_path = out / "strict.yaml";
    {
        std::ofstream y(yaml_path);
        y << "input_suffix: \"_i,_pi,_ni\"\n"
          << "output_suffix: \"_o,_po,_no\"\n"
          << "input_prefix: \"\"\n"
          << "output_prefix: \"\"\n"
          << "instance_prefix: \"u_\"\n"
          << "max_line_length: 80\n"
          << "prohibit_hard_tabs: true\n"
          << "prohibit_trailing_whitespace: true\n";
    }

    connect::ConnCliOptions opts;
    opts.topModule = "unreachable_top"; // clean module; sibling is dirty
    opts.format = "json";
    opts.outputDir = (out / "report").string();
    opts.checkConvention = true;
    opts.conventionFile = yaml_path.string();

    connect::runConnWithCompilation(*result.compilation, opts);
    REQUIRE(fs::exists(fs::path(opts.outputDir) / "connect_report.json"));
    std::ifstream ifs(fs::path(opts.outputDir) / "connect_report.json");
    std::string body((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    // The sibling's violations MUST appear because the file containing
    // `unreachable_top` is admitted in full (file-level scope).  If
    // these checks ever change to be module-level, this test fails --
    // surfacing the contract change at test time.
    CHECK(body.find("hard tab character") != std::string::npos);
    CHECK(body.find("line length") != std::string::npos);
    CHECK(body.find("trailing whitespace") != std::string::npos);
}

TEST_CASE("ConnRunner: clean source-text fixture emits zero text observations", "[conn][runner][source_text][clean]") {
    // Round 39 review (R2 #3): pair the violation fixture with a clean
    // counterpart so a regression that turns checks into false-positive
    // generators surfaces immediately.  clean_source_text.sv is fully
    // compliant (no long lines, no tabs, no trailing whitespace, one
    // module, basename matches module name).
    auto result = testutils::compileFile("sv/clean_source_text.sv");
    REQUIRE(result);

    const auto out = fs::temp_directory_path() / "svlens_conn_source_text_clean";
    fs::remove_all(out);
    fs::create_directories(out);

    auto yaml_path = out / "strict.yaml";
    {
        std::ofstream y(yaml_path);
        y << "input_suffix: \"_i,_pi,_ni\"\n"
          << "output_suffix: \"_o,_po,_no\"\n"
          << "input_prefix: \"\"\n"
          << "output_prefix: \"\"\n"
          << "instance_prefix: \"u_\"\n"
          << "max_line_length: 100\n"
          << "prohibit_hard_tabs: true\n"
          << "prohibit_trailing_whitespace: true\n"
          << "prohibit_multiple_modules_per_file: true\n"
          << "enforce_file_module_match: true\n";
    }

    connect::ConnCliOptions opts;
    opts.topModule = "clean_source_text";
    opts.format = "json";
    opts.outputDir = (out / "report").string();
    opts.checkConvention = true;
    opts.conventionFile = yaml_path.string();

    connect::runConnWithCompilation(*result.compilation, opts);
    REQUIRE(fs::exists(fs::path(opts.outputDir) / "connect_report.json"));
    std::ifstream ifs(fs::path(opts.outputDir) / "connect_report.json");
    std::string body((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    // None of the source-text rules should fire.
    CHECK(body.find("line length") == std::string::npos);
    CHECK(body.find("exceeds max") == std::string::npos);
    CHECK(body.find("hard tab character") == std::string::npos);
    CHECK(body.find("trailing whitespace") == std::string::npos);
    CHECK(body.find("modules declared in one file") == std::string::npos);
    CHECK(body.find("does not match module name") == std::string::npos);
}
