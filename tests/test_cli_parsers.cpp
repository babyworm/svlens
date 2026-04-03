#include <catch2/catch_test_macros.hpp>
#include "ConnCli.h"
#include "CdcCli.h"

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
