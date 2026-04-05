#include <catch2/catch_test_macros.hpp>
#include "ConnCli.h"
#include "CdcCli.h"
#include "CommonCli.h"

TEST_CASE("ConnCli parser: parses basic flags and compilation args", "[cli][conn]") {
    const char* argv[] = {
        "sv-conncheck",
        "--version",
        "--help",
        "--top", "soc_top",
        "-o", "outdir",
        "rtl/top.sv"
    };

    std::vector<std::string> compilationArgs;
    auto opts = connect::parseConnArgs(8, argv, compilationArgs);

    CHECK(opts.showVersion);
    CHECK(opts.showHelp);
    CHECK(opts.topModule == "soc_top");
    CHECK(opts.outputDir == "outdir");
    REQUIRE(compilationArgs.size() == 4);
    CHECK(compilationArgs[1] == "--top");
    CHECK(compilationArgs[2] == "soc_top");
    CHECK(compilationArgs[3] == "rtl/top.sv");
}

TEST_CASE("CdcCli parser: parses basic flags and mode-specific options", "[cli][cdc]") {
    const char* argv[] = {
        "sv-cdccheck",
        "--version",
        "--help",
        "--clock-yaml", "clocks.yaml",
        "--sync-stages", "3",
        "-o", "cdc_out",
        "rtl/top.sv"
    };

    std::vector<std::string> compilationArgs;
    auto opts = cdccli::parseCdcArgs(10, argv, compilationArgs);

    CHECK(opts.showVersion);
    CHECK(opts.showHelp);
    CHECK(opts.clockYamlFile == "clocks.yaml");
    CHECK(opts.syncStages == 3);
    CHECK(opts.outputDir == "cdc_out");
    REQUIRE(compilationArgs.size() == 2);
    CHECK(compilationArgs[1] == "rtl/top.sv");
}

TEST_CASE("CdcCli parser: invalid sync-stages is captured as parse error", "[cli][cdc]") {
    const char* argv[] = {
        "sv-cdccheck",
        "--sync-stages", "abc",
        "rtl/top.sv"
    };

    std::vector<std::string> compilationArgs;
    auto opts = cdccli::parseCdcArgs(4, argv, compilationArgs);

    CHECK(opts.syncStages == 2);
    CHECK(opts.parseError == "--sync-stages requires an integer value");
}

TEST_CASE("CommonCli both-mode routing keeps common args and splits prefixed mode args", "[cli][common]") {
    char arg0[] = "svlens";
    char arg1[] = "both";
    char arg2[] = "--top";
    char arg3[] = "soc_top";
    char arg4[] = "-o";
    char arg5[] = "reports";
    char arg6[] = "--conn-format";
    char arg7[] = "json";
    char arg8[] = "--cdc-sync-stages";
    char arg9[] = "3";
    char arg10[] = "rtl/top.sv";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10};

    const std::unordered_set<std::string> connValueOptions = {
        "--output", "-o", "--format", "--waiver", "--expect", "--diff",
        "--trace", "--convention", "--depth", "--top"
    };
    const std::unordered_set<std::string> cdcValueOptions = {
        "--output", "-o", "--format", "--sdc", "--clock-yaml", "--waiver",
        "--dump-graph", "--sync-stages", "--top", "-f", "-F"
    };

    auto dispatch = commoncli::routeBothModeArgs(11, argv, 2, connValueOptions, cdcValueOptions);

    CHECK(dispatch.explicitOutput);
    CHECK(dispatch.outputBase == "reports");
    CHECK(dispatch.connArgs[0] == "sv-conncheck");
    CHECK(dispatch.cdcArgs[0] == "sv-cdccheck");
    CHECK(dispatch.connArgs[1] == "--top");
    CHECK(dispatch.connArgs[2] == "soc_top");
    CHECK(dispatch.cdcArgs[1] == "--top");
    CHECK(dispatch.cdcArgs[2] == "soc_top");
    CHECK(dispatch.connArgs[3] == "--format");
    CHECK(dispatch.connArgs[4] == "json");
    CHECK(dispatch.cdcArgs[3] == "--sync-stages");
    CHECK(dispatch.cdcArgs[4] == "3");
    CHECK(dispatch.connArgs.back() == "rtl/top.sv");
    CHECK(dispatch.cdcArgs.back() == "rtl/top.sv");
}
