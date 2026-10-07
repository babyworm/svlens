#include <catch2/catch_test_macros.hpp>
#include "CdcRunner.h"
#include "CompilationSession.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string cdcFixture(const std::string& name) {
    return (fs::path(TEST_CDC_DIR) / name).string();
}

static bool cdcSvaElaborates(const std::string& fixture, const std::string& top, const std::string& svaPath,
                             std::string& error) {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", top, "--top", "svlens_cdc_assertions", cdcFixture(fixture),
                                     svaPath};
    return session.compile(args, &error);
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
    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("Review the clock relationship") != std::string::npos);
    CHECK(json.find("pulse/toggle") != std::string::npos);
    CHECK(json.find("Insert 2-FF synchronizer") == std::string::npos);
}

TEST_CASE("CdcRunner: unsynchronized wide bus does not recommend independent 2FFs", "[cdc][runner][wide]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("wide_missing_sync.sv")};
    REQUIRE(session.compile(args));
    const auto out = fs::temp_directory_path() / "svlens_cdc_wide_missing_sync";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "wide_missing_sync";
    opts.outputDir = out.string();
    opts.format = "json";
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 1);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"category\": \"VIOLATION\"") != std::string::npos);
    CHECK(json.find("multi-bit") != std::string::npos);
    CHECK(json.find("handshake") != std::string::npos);
    CHECK(json.find("Insert 2-FF synchronizer") == std::string::npos);
}

TEST_CASE("CdcRunner: clock-as-data JSON preserves the connected signal path", "[cdc][runner][cdc09]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("clock_as_data_alias.sv")};
    REQUIRE(session.compile(args));
    const auto out = fs::temp_directory_path() / "svlens_cdc_clock_as_data_alias";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "clock_as_data_alias";
    opts.outputDir = out.string();
    opts.format = "json";
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"rule\": \"Ac_cdc09\"") != std::string::npos);
    CHECK(json.find("\"source\": \"clock_as_data_alias.u_child.data_i\"") != std::string::npos);
    CHECK(json.find("\"dest\": \"clock_as_data_alias.u_child.q_o\"") != std::string::npos);
}

TEST_CASE("CdcRunner: SDC false path does not waive a missing synchronizer", "[cdc][runner][sdc][false_path]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_false_path_missing_sync";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "missing_sync";
    opts.outputDir = out.string();
    opts.format = "all";
    opts.sdcFile = cdcFixture("false_path_missing_sync.sdc");
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 1);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"violations\": 1") != std::string::npos);
    CHECK(json.find("\"category\": \"VIOLATION\"") != std::string::npos);
    CHECK(json.find("\"sdc_false_path\": true") != std::string::npos);
    CHECK(json.find("\"waive_reason\": \"sdc_false_path\"") == std::string::npos);
    std::ifstream sdcFile(out / "cdc_constraints.sdc");
    REQUIRE(sdcFile.good());
    const std::string sdc((std::istreambuf_iterator<char>(sdcFile)), std::istreambuf_iterator<char>());
    CHECK(sdc.find("No executable timing constraints") != std::string::npos);
    CHECK(sdc.find("set_false_path") == std::string::npos);
}

TEST_CASE("CdcRunner: SDC false path preserves a structurally detected 2FF", "[cdc][runner][sdc][false_path]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("03_two_ff_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_false_path_two_ff";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "two_ff_sync";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.sdcFile = cdcFixture("false_path_missing_sync.sdc");
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"sync_type\": \"two_ff\"") != std::string::npos);
    CHECK(json.find("\"category\": \"INFO\"") != std::string::npos);
    CHECK(json.find("\"sdc_false_path\": true") != std::string::npos);
}

TEST_CASE("CdcRunner: max-delay constraint is reported without downgrading CDC", "[cdc][runner][sdc][max_delay]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_max_delay_missing_sync";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "missing_sync";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.sdcFile = cdcFixture("max_delay_missing_sync.sdc");
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 1);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"violations\": 1") != std::string::npos);
    CHECK(json.find("\"sdc_max_delay_constraint_ns\": 5") != std::string::npos);
    CHECK(json.find("\"sdc_max_delay_datapath_only\": true") != std::string::npos);
    CHECK(json.find("\"sdc_max_delay_ambiguous\": false") != std::string::npos);
    CHECK(json.find("declared SDC max delay") != std::string::npos);
}

TEST_CASE("CdcRunner: invalid max-delay constraint warns and leaves CDC classification intact",
          "[cdc][runner][sdc][max_delay]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));
    const auto out = fs::temp_directory_path() / "svlens_cdc_invalid_max_delay";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "missing_sync";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.sdcFile = cdcFixture("invalid_max_delay.sdc");
    std::ostringstream warnings;
    auto* previous = std::cerr.rdbuf(warnings.rdbuf());
    const int exitCode = cdccli::runCdcWithCompilation(session.compilation(), opts);
    std::cerr.rdbuf(previous);
    CHECK(exitCode == 1);
    CHECK(warnings.str().find("skipped 1 unsupported or invalid set_max_delay") != std::string::npos);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"violations\": 1") != std::string::npos);
    CHECK(json.find("\"sdc_max_delay_constraint_ns\": null") != std::string::npos);
}

TEST_CASE("CdcRunner: exclusive timing groups do not auto-waive an unsynchronized crossing",
          "[cdc][runner][sdc][exclusive]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));

    for (const auto& [fileName, relationship] :
         {std::pair{"physically_exclusive_missing_sync.sdc", "physically_exclusive"},
          std::pair{"logically_exclusive_missing_sync.sdc", "logically_exclusive"}}) {
        const auto out = fs::temp_directory_path() / (std::string("svlens_cdc_") + relationship + "_review");
        fs::remove_all(out);
        cdccli::CdcCliOptions opts;
        opts.topModule = "missing_sync";
        opts.outputDir = out.string();
        opts.format = "json";
        opts.sdcFile = cdcFixture(fileName);
        opts.strict = true;
        REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 1);

        std::ifstream file(out / "cdc_report.json");
        REQUIRE(file.good());
        const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(json.find("\"cautions\": 1") != std::string::npos);
        CHECK(json.find("\"category\": \"CAUTION\"") != std::string::npos);
        CHECK(json.find(std::string("\"relationship\": \"") + relationship + "\"") != std::string::npos);
    }
}

TEST_CASE("CdcRunner: unsupported clock group warns and preserves conservative classification",
          "[cdc][runner][sdc][clock_groups]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_unsupported_group";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "missing_sync";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.sdcFile = cdcFixture("unsupported_group_selector.sdc");
    std::ostringstream warnings;
    auto* previous = std::cerr.rdbuf(warnings.rdbuf());
    const int exitCode = cdccli::runCdcWithCompilation(session.compilation(), opts);
    std::cerr.rdbuf(previous);
    CHECK(exitCode == 1);
    CHECK(warnings.str().find("skipped 1 unsupported set_clock_groups") != std::string::npos);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"violations\": 1") != std::string::npos);
    CHECK(json.find("\"relationship\": \"asynchronous\"") != std::string::npos);
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

TEST_CASE("CdcRunner: generated-clock period reaches CDC review timing basis", "[cdc][runner][timing]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("generated_period_hint.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_generated_period_hint";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "generated_period_hint";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.sdcFile = cdcFixture("generated_period_hint.sdc");
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"dest\": \"generated_period_hint.q_o\"") != std::string::npos);
    CHECK(json.find("\"timing_basis_ns\": 20") != std::string::npos);
    CHECK(json.find("Ac_cdc08") != std::string::npos);
    CHECK(json.find("\"category\": \"CAUTION\"") != std::string::npos);
    CHECK(json.find("\"rule\": \"Ac_cdc09\"") == std::string::npos);
}

TEST_CASE("CdcRunner: reset usage links a unique cross-instance FF driver", "[cdc][runner][reset]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("registered_reset_alias.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_registered_reset_alias";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "registered_reset_alias";
    opts.outputDir = out.string();
    opts.format = "json";
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string driver = "\"driver_ff\": \"registered_reset_alias.u_source.rst_sync_n\"";
    const auto driverPos = json.find(driver);
    REQUIRE(driverPos != std::string::npos);
    const std::string domainKey = "\"source_domain\": \"";
    const auto domainPos = json.find(domainKey, driverPos);
    REQUIRE(domainPos != std::string::npos);
    CHECK(json[domainPos + domainKey.size()] != '"');
    CHECK(json.find("\"driver_inverted\": false", driverPos) != std::string::npos);
    CHECK(json.find("\"rule\": \"Ac_cdc06\"") != std::string::npos);
}

TEST_CASE("CdcRunner: one-bit inverted reset retains registered-driver provenance", "[cdc][runner][reset]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("inverted_reset_alias.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_cdc_inverted_reset_alias";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "inverted_reset_alias";
    opts.outputDir = out.string();
    opts.format = "json";
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"rule\": \"Ac_cdc06\"") != std::string::npos);
    CHECK(json.find("\"driver_ff\": \"inverted_reset_alias.reset_q\"") != std::string::npos);
    CHECK(json.find("\"driver_inverted\": true") != std::string::npos);
    CHECK(json.find("\"source_domain\": \"clk_a\"") != std::string::npos);
}

TEST_CASE("CdcRunner: conditional reset mux exposes candidates without claiming a unique driver",
          "[cdc][runner][reset][mux]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "conditional_reset_mux_top",
                                     cdcFixture("conditional_reset_mux.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_cdc_conditional_reset_mux_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "conditional_reset_mux_top";
    opts.outputDir = out.string();
    opts.format = "json";
    cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto signalPos = json.find("\"signal\": \"conditional_reset_mux_top.u_sink.rst_ni\"");
    REQUIRE(signalPos != std::string::npos);
    const auto lineEnd = json.find('\n', signalPos);
    const auto record = json.substr(signalPos, lineEnd - signalPos);
    CHECK(record.find("\"conditional_muxes\"") != std::string::npos);
    CHECK(record.find("conditional_reset_mux_top.u_source.u_mux.clk_o") != std::string::npos);
    CHECK(record.find("conditional_reset_mux_top.u_source.u_mux.clk0_i") != std::string::npos);
    CHECK(record.find("conditional_reset_mux_top.u_source.u_mux.clk1_i") != std::string::npos);
    CHECK(record.find("conditional_reset_mux_top.u_source.u_mux.sel_i") != std::string::npos);
    CHECK(record.find("\"input0_source\": \"conditional_reset_mux_top.u_source.rst_pair[0]\"") != std::string::npos);
    CHECK(record.find("\"input0_dependencies\"") == std::string::npos);
    CHECK(record.find("\"input0_candidate_ffs\": [{\"path\": \"conditional_reset_mux_top.u_source.u_bit.q_o\", "
                      "\"domain\": \"clk_a\", \"inverted\": false}]") != std::string::npos);
    CHECK(record.find("\"input1_candidate_ffs\"") == std::string::npos);
    CHECK(record.find("\"input1_source\": \"conditional_reset_mux_top.u_source.scan_rst_ni\"") != std::string::npos);
    CHECK(record.find("\"select_source\": \"conditional_reset_mux_top.u_source.scanmode_i\"") != std::string::npos);
    CHECK(record.find("u_other") == std::string::npos);
    CHECK(record.find("\"driver_ff\"") == std::string::npos);
    CHECK(json.find("\"rule\": \"Ac_cdc06\"") == std::string::npos);
}

TEST_CASE("CdcRunner: computed mux input does not invent an upstream reset path", "[cdc][runner][reset][mux]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "conditional_reset_mux_computed_top",
                                     cdcFixture("conditional_reset_mux.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_cdc_computed_reset_mux_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "conditional_reset_mux_computed_top";
    opts.outputDir = out.string();
    opts.format = "json";
    cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto signalPos = json.find("\"signal\": \"conditional_reset_mux_computed_top.u_sink.rst_ni\"");
    REQUIRE(signalPos != std::string::npos);
    const auto lineEnd = json.find('\n', signalPos);
    const auto record = json.substr(signalPos, lineEnd - signalPos);
    CHECK(record.find("conditional_reset_mux_computed_top.u_source.u_mux.clk_o") != std::string::npos);
    CHECK(record.find("\"input0_source\"") == std::string::npos);
    CHECK(record.find("\"input0_dependencies\": [\"conditional_reset_mux_computed_top.u_source.rst_sync_n\"]") !=
          std::string::npos);
    CHECK(
        record.find("\"input0_candidate_ffs\": [{\"path\": \"conditional_reset_mux_computed_top.u_source.rst_sync_n\", "
                    "\"domain\": \"clk_a\", \"inverted\": true}]") != std::string::npos);
    CHECK(record.find("\"input1_source\": \"conditional_reset_mux_computed_top.u_source.scan_rst_ni\"") !=
          std::string::npos);
    CHECK(record.find("\"select_source\": \"conditional_reset_mux_computed_top.u_source.scanmode_i\"") !=
          std::string::npos);
    CHECK(record.find("\"driver_ff\"") == std::string::npos);
}

TEST_CASE("CdcRunner: fixed mux follows one-bit inverted registered reset", "[cdc][runner][reset][mux]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "conditional_reset_mux_inverted_static_top",
                                     cdcFixture("conditional_reset_mux.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_cdc_inverted_static_mux_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "conditional_reset_mux_inverted_static_top";
    opts.outputDir = out.string();
    opts.format = "json";
    cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto signalPos = json.find("\"signal\": \"conditional_reset_mux_inverted_static_top.u_sink.rst_ni\"");
    REQUIRE(signalPos != std::string::npos);
    const auto lineEnd = json.find('\n', signalPos);
    const auto record = json.substr(signalPos, lineEnd - signalPos);
    CHECK(record.find("\"selected_input\": 0") != std::string::npos);
    CHECK(record.find("\"driver_ff\": \"conditional_reset_mux_inverted_static_top.u_source.rst_sync_n\"") !=
          std::string::npos);
    CHECK(record.find("\"driver_inverted\": true") != std::string::npos);
    CHECK(record.find("\"input0_candidate_ffs\": [{\"path\": "
                      "\"conditional_reset_mux_inverted_static_top.u_source.rst_sync_n\", "
                      "\"domain\": \"clk_a\", \"inverted\": true}]") != std::string::npos);
    CHECK(json.find("\"rule\": \"Ac_cdc06\"") != std::string::npos);
}

TEST_CASE("CdcRunner: wide inversion at mux input stays unresolved", "[cdc][runner][reset][mux]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "conditional_reset_mux_wide_inversion_top",
                                     cdcFixture("conditional_reset_mux.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_cdc_wide_inv_mux_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "conditional_reset_mux_wide_inversion_top";
    opts.outputDir = out.string();
    opts.format = "json";
    cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto signalPos = json.find("\"signal\": \"conditional_reset_mux_wide_inversion_top.u_sink.rst_ni\"");
    REQUIRE(signalPos != std::string::npos);
    const auto lineEnd = json.find('\n', signalPos);
    const auto record = json.substr(signalPos, lineEnd - signalPos);
    CHECK(record.find("\"input0_source\"") == std::string::npos);
    CHECK(record.find("\"input0_dependencies\": [\"conditional_reset_mux_wide_inversion_top.u_source.rst_pair\"]") !=
          std::string::npos);
    CHECK(record.find("\"input0_candidate_ffs\"") == std::string::npos);
    CHECK(record.find("\"driver_ff\"") == std::string::npos);
}

TEST_CASE("CdcRunner: width-changing mux input remains a dependency", "[cdc][runner][reset][mux]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "conditional_reset_mux_cast_top",
                                     cdcFixture("conditional_reset_mux.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_cdc_cast_reset_mux_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "conditional_reset_mux_cast_top";
    opts.outputDir = out.string();
    opts.format = "json";
    cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto signalPos = json.find("\"signal\": \"conditional_reset_mux_cast_top.u_sink.rst_ni\"");
    REQUIRE(signalPos != std::string::npos);
    const auto lineEnd = json.find('\n', signalPos);
    const auto record = json.substr(signalPos, lineEnd - signalPos);
    CHECK(record.find("\"input0_source\"") == std::string::npos);
    CHECK(record.find("\"input0_dependencies\": [\"conditional_reset_mux_cast_top.u_source.rst_pair\"]") !=
          std::string::npos);
    CHECK(record.find("\"driver_ff\"") == std::string::npos);
}

TEST_CASE("CdcRunner: fixed mux select follows only its elaborated reset input", "[cdc][runner][reset][mux]") {
    for (const auto& [top, selected, registered] : {std::tuple{"conditional_reset_mux_static0_top", 0, true},
                                                    std::tuple{"conditional_reset_mux_static1_top", 1, false},
                                                    std::tuple{"conditional_reset_mux_staticx_top", -1, false}}) {
        connect::CompilationSession session;
        std::vector<std::string> args = {"test", "--top", top, cdcFixture("conditional_reset_mux.sv")};
        REQUIRE(session.compile(args));
        const auto out =
            fs::temp_directory_path() / (std::string("svlens_cdc_") + top + "_" + std::to_string(::getpid()));
        fs::remove_all(out);
        cdccli::CdcCliOptions opts;
        opts.topModule = top;
        opts.outputDir = out.string();
        opts.format = "json";
        cdccli::runCdcWithCompilation(session.compilation(), opts);

        std::ifstream file(out / "cdc_report.json");
        REQUIRE(file.good());
        const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto signalPos = json.find(std::string("\"signal\": \"") + top + ".u_sink.rst_ni\"");
        REQUIRE(signalPos != std::string::npos);
        const auto lineEnd = json.find('\n', signalPos);
        const auto record = json.substr(signalPos, lineEnd - signalPos);
        if (selected >= 0)
            CHECK(record.find("\"selected_input\": " + std::to_string(selected)) != std::string::npos);
        else
            CHECK(record.find("\"selected_input\"") == std::string::npos);
        CHECK(record.find("\"select_source\"") == std::string::npos);
        CHECK(record.find("\"select_dependencies\"") == std::string::npos);
        const auto candidateFF = std::string("\"path\": \"") + top + ".u_source.rst_sync_n\"";
        CHECK((record.find(candidateFF) != std::string::npos) == (selected != 1));
        if (registered) {
            CHECK(record.find(std::string("\"driver_ff\": \"") + top + ".u_source.rst_sync_n\"") != std::string::npos);
            CHECK(json.find("\"rule\": \"Ac_cdc06\"") != std::string::npos);
        } else {
            CHECK(record.find("\"driver_ff\"") == std::string::npos);
            CHECK(json.find("\"rule\": \"Ac_cdc06\"") == std::string::npos);
        }
    }
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
    CHECK(sva.find("@(posedge missing_sync.clk_b)") != std::string::npos);
    std::string error;
    const bool elaborated = cdcSvaElaborates("02_missing_sync.sv", "missing_sync", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
}

TEST_CASE("CdcRunner: 2FF assertion ID links SVA and JSON", "[cdc][runner][emit_sva][assert]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("03_two_ff_sync.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_emit_sync_assertion";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "two_ff_sync";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);

    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    std::ifstream jsonFile(out / "cdc_report.json");
    REQUIRE(jsonFile.good());
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());

    CHECK(sva.find("cdc_INFO_1_2ff: assert property") != std::string::npos);
    CHECK(sva.find("@(posedge two_ff_sync.clk_b)") != std::string::npos);
    CHECK(json.find("\"sva_assertion_id\": \"cdc_INFO_1_2ff\"") != std::string::npos);
    CHECK(json.find("\"dest_file\":") != std::string::npos);
    CHECK(json.find("\"dest_line\":") != std::string::npos);
    std::string error;
    const bool elaborated = cdcSvaElaborates("03_two_ff_sync.sv", "two_ff_sync", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
}

TEST_CASE("CdcRunner: FIFO Gray assertion ID links SVA and JSON", "[cdc][runner][emit_sva][fifo]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("registered_fifo_gray.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_fifo_gray_sva";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "fifo_gray_top";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    (void)cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    std::ifstream jsonFile(out / "cdc_report.json");
    REQUIRE(jsonFile.good());
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    CHECK(sva.find("_fifo_gray: assert property") != std::string::npos);
    CHECK(sva.find("_fifo_no_step: assert property") == std::string::npos);
    CHECK(json.find("_fifo_gray\"") != std::string::npos);
    CHECK(json.find("fifo_wptr_gray_q") != std::string::npos);
    std::string error;
    const bool elaborated = cdcSvaElaborates("registered_fifo_gray.sv", "fifo_gray_top", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
}

TEST_CASE("CdcRunner: qualified FIFO transfer gates link SVA and JSON", "[cdc][runner][emit_sva][fifo]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "fifo_transfer_sva_top", cdcFixture("fifo_transfer_sva.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_fifo_transfer_sva_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "fifo_transfer_sva_top";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    (void)cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    std::ifstream jsonFile(out / "cdc_report.json");
    REQUIRE(jsonFile.good());
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    CHECK(sva.find("!(fifo_transfer_sva_top.u_fifo.wvalid_i && fifo_transfer_sva_top.u_fifo.wready_o) |=> "
                   "$stable(fifo_transfer_sva_top.u_fifo.fifo_wptr_gray_q)") != std::string::npos);
    CHECK(sva.find("!(fifo_transfer_sva_top.u_fifo.rvalid_o && fifo_transfer_sva_top.u_fifo.rready_i) |=> "
                   "$stable(fifo_transfer_sva_top.u_fifo.fifo_rptr_gray_q)") != std::string::npos);
    CHECK(json.find("_fifo_no_step\"") != std::string::npos);
    std::string error;
    const bool elaborated =
        cdcSvaElaborates("fifo_transfer_sva.sv", "fifo_transfer_sva_top", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
}

TEST_CASE("CdcRunner: generated FIFO transfer SVA uses declaration scope", "[cdc][runner][emit_sva][fifo]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "fifo_transfer_generated_top",
                                     cdcFixture("fifo_transfer_sva.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_fifo_generated_sva_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "fifo_transfer_generated_top";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    (void)cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    CHECK(sva.find("fifo_transfer_generated_top.gen_lanes[0].u_fifo.wvalid_i") != std::string::npos);
    CHECK(sva.find("fifo_transfer_generated_top.gen_lanes[0].u_fifo.rvalid_o") != std::string::npos);
    CHECK(sva.find("genblk0.u_fifo.wvalid_i") == std::string::npos);
    CHECK(sva.find("genblk0.u_fifo.rvalid_o") == std::string::npos);
    std::string error;
    const bool elaborated =
        cdcSvaElaborates("fifo_transfer_sva.sv", "fifo_transfer_generated_top", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
}

TEST_CASE("CdcRunner: unconnected FIFO ready port suppresses only write transfer SVA",
          "[cdc][runner][emit_sva][fifo]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "fifo_transfer_partial_top", cdcFixture("fifo_transfer_sva.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_fifo_partial_sva_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "fifo_transfer_partial_top";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    (void)cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    CHECK(sva.find("u_fifo.wvalid_i &&") == std::string::npos);
    CHECK(sva.find("u_fifo.rvalid_o &&") != std::string::npos);
    std::string error;
    const bool elaborated =
        cdcSvaElaborates("fifo_transfer_sva.sv", "fifo_transfer_partial_top", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
}

TEST_CASE("CdcRunner: req/ack contract assertion ID links SVA and JSON", "[cdc][runner][emit_sva][handshake]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("registered_reqack.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / "svlens_reqack_sva";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "reqack_top";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    (void)cdccli::runCdcWithCompilation(session.compilation(), opts);

    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    std::ifstream jsonFile(out / "cdc_report.json");
    REQUIRE(jsonFile.good());
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    CHECK(sva.find("_ack_requires_req: assert property") != std::string::npos);
    CHECK(json.find("_ack_requires_req\"") != std::string::npos);
    CHECK(json.find("reqack_top.u_reqack.src_req_q") != std::string::npos);
    std::string error;
    const bool elaborated = cdcSvaElaborates("registered_reqack.sv", "reqack_top", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
}

TEST_CASE("CdcRunner: html format writes crossing dashboard", "[cdc][runner][html]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("02_missing_sync.sv")};
    REQUIRE(session.compile(args));
    const auto out = fs::temp_directory_path() / "svlens_cdc_html";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "missing_sync";
    opts.outputDir = out.string();
    opts.format = "html";
    CHECK(cdccli::runCdcWithCompilation(session.compilation(), opts) == 1);
    CHECK(fs::exists(out / "cdc_report.html"));
}

TEST_CASE("CdcRunner: source comment waives one exact CDC path", "[cdc][runner][inline_waiver]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("inline_waive_missing_sync.sv")};
    REQUIRE(session.compile(args));
    const auto out = fs::temp_directory_path() / "svlens_cdc_inline_waiver";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "missing_sync_inline";
    opts.outputDir = out.string();
    opts.format = "json";
    CHECK(cdccli::runCdcWithCompilation(session.compilation(), opts) == 1);
    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"category\": \"WAIVED\"") != std::string::npos);
    CHECK(json.find("\"category\": \"VIOLATION\"") != std::string::npos);
}

TEST_CASE("CdcRunner: named reqack primitive reports both clock boundaries for review", "[cdc][runner][reqack]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", cdcFixture("named_reqack_primitive.sv")};
    REQUIRE(session.compile(args));
    const auto out = fs::temp_directory_path() / "svlens_named_reqack";
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "named_reqack_top";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);
    std::ifstream file(out / "cdc_report.json");
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"cautions\": 2") != std::string::npos);
    CHECK(json.find("\"sync_type\": \"handshake\"") != std::string::npos);
    CHECK(json.find("\"source_domain\": \"clk_a\", \"dest_domain\": \"clk_b\"") != std::string::npos);
    CHECK(json.find("\"source_domain\": \"clk_b\", \"dest_domain\": \"clk_a\"") != std::string::npos);
    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    CHECK(sva.find("_data_hold") == std::string::npos);
    CHECK(json.find("\"sva_assertion_id\"") == std::string::npos);
}

TEST_CASE("CdcRunner: qualified reqack-data contracts link SVA and JSON", "[cdc][runner][reqack][emit_sva]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test", "--top", "reqack_data_sva_top", cdcFixture("reqack_data_sva.sv")};
    REQUIRE(session.compile(args));

    const auto out = fs::temp_directory_path() / ("svlens_reqack_data_sva_" + std::to_string(::getpid()));
    fs::remove_all(out);
    cdccli::CdcCliOptions opts;
    opts.topModule = "reqack_data_sva_top";
    opts.outputDir = out.string();
    opts.format = "json";
    opts.svaOutputFile = (out / "cdc_assertions.sva").string();
    REQUIRE(cdccli::runCdcWithCompilation(session.compilation(), opts) == 0);

    std::ifstream svaFile(opts.svaOutputFile);
    REQUIRE(svaFile.good());
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    std::ifstream jsonFile(out / "cdc_report.json");
    REQUIRE(jsonFile.good());
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());

    CHECK(sva.find("_data_hold_src2dst: assert property") != std::string::npos);
    CHECK(sva.find("_data_hold_dst2src: assert property") != std::string::npos);
    CHECK(sva.find("!$stable(reqack_data_sva_top.u_forward.data_i)") != std::string::npos);
    CHECK(sva.find("$past(reqack_data_sva_top.u_reverse.data_o, 2)") != std::string::npos);
    CHECK(sva.find("u_buffered.data_i") == std::string::npos);
    CHECK(sva.find("u_partial.data_i") == std::string::npos);

    auto crossingRecord = [&](const std::string& source) {
        const auto pos = json.find("\"source\": \"" + source + "\"");
        if (pos == std::string::npos)
            return std::string{};
        const auto end = json.find('\n', pos);
        return json.substr(pos, end - pos);
    };
    CHECK(crossingRecord("reqack_data_sva_top.u_forward.src_req_i").find("_data_hold_src2dst\"") != std::string::npos);
    CHECK(crossingRecord("reqack_data_sva_top.u_reverse.dst_ack_i").find("_data_hold_dst2src\"") != std::string::npos);
    CHECK(crossingRecord("reqack_data_sva_top.u_buffered.dst_ack_i").find("\"sva_assertion_id\"") == std::string::npos);
    CHECK(crossingRecord("reqack_data_sva_top.u_partial.src_req_i").find("\"sva_assertion_id\"") == std::string::npos);
    std::string error;
    const bool elaborated = cdcSvaElaborates("reqack_data_sva.sv", "reqack_data_sva_top", opts.svaOutputFile, error);
    INFO(error);
    CHECK(elaborated);
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
