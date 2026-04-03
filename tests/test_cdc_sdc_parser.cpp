#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/sdc_parser.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static fs::path writeTempSdc(const std::string& content) {
    static int counter = 0;
    auto path = fs::temp_directory_path() / ("svlens_sdc_" + std::to_string(counter++) + ".sdc");
    std::ofstream(path) << content;
    return path;
}

TEST_CASE("CDC SdcParser: parses create_clock entries", "[cdc][sdc]") {
    auto path = writeTempSdc(
        "create_clock -name sys_clk -period 10 [get_ports sys_clk]\n"
        "create_clock -name axi_clk -period 8 [get_ports axi_clk]\n");

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.clocks.size() == 2);
    CHECK(sdc.clocks[0].name == "sys_clk");
    CHECK(sdc.clocks[0].period.value() == 10.0);
    CHECK(sdc.clocks[1].name == "axi_clk");
}

TEST_CASE("CDC SdcParser: parses generated clocks and clock groups", "[cdc][sdc]") {
    auto path = writeTempSdc(
        "create_clock -name sys_clk -period 10 [get_ports sys_clk]\n"
        "create_generated_clock -name div2_clk -source [get_ports sys_clk] -divide_by 2 [get_pins u_div/clk_out]\n"
        "set_clock_groups -asynchronous -group {sys_clk div2_clk} -group {ext_clk}\n");

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.generated_clocks.size() == 1);
    CHECK(sdc.generated_clocks[0].name == "div2_clk");
    CHECK(sdc.generated_clocks[0].source_clock == "sys_clk");
    REQUIRE(sdc.clock_groups.size() == 1);
    CHECK(sdc.clock_groups[0].type == SdcClockGroup::Type::Asynchronous);
}

TEST_CASE("CDC SdcParser: comments and empty file are tolerated", "[cdc][sdc]") {
    auto path = writeTempSdc("# comment only\n\n");
    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    CHECK(sdc.clocks.empty());
    CHECK(sdc.generated_clocks.empty());
    CHECK(sdc.clock_groups.empty());
}
