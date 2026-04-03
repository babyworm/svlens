#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <filesystem>
#include "sv-cdccheck/sdc_parser.h"

namespace fs = std::filesystem;
using namespace sv_cdccheck;

// Helper: write a temp SDC file and return its path
static fs::path writeTempSdc(const std::string& content) {
    static int counter = 0;
    auto path = fs::temp_directory_path() / ("test_sdc_" + std::to_string(counter++) + ".sdc");
    std::ofstream(path) << content;
    return path;
}

TEST_CASE("SDC parser: create_clock basic", "[sdc]") {
    auto path = writeTempSdc(
        "create_clock -name sys_clk -period 10 [get_ports sys_clk]\n"
        "create_clock -name axi_clk -period 8 [get_ports axi_clk]\n"
    );

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.clocks.size() == 2);

    CHECK(sdc.clocks[0].name == "sys_clk");
    CHECK(sdc.clocks[0].period.value() == 10.0);
    CHECK(sdc.clocks[0].target == "sys_clk");

    CHECK(sdc.clocks[1].name == "axi_clk");
    CHECK(sdc.clocks[1].period.value() == 8.0);
    CHECK(sdc.clocks[1].target == "axi_clk");
}

TEST_CASE("SDC parser: create_generated_clock", "[sdc]") {
    auto path = writeTempSdc(
        "create_clock -name sys_clk -period 10 [get_ports sys_clk]\n"
        "create_generated_clock -name div2_clk -source [get_ports sys_clk] "
        "-divide_by 2 [get_pins u_divider/clk_out]\n"
    );

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.generated_clocks.size() == 1);
    CHECK(sdc.generated_clocks[0].name == "div2_clk");
    CHECK(sdc.generated_clocks[0].source_clock == "sys_clk");
    CHECK(sdc.generated_clocks[0].divide_by == 2);
    CHECK(sdc.generated_clocks[0].target == "u_divider/clk_out");
}

TEST_CASE("SDC parser: set_clock_groups asynchronous", "[sdc]") {
    auto path = writeTempSdc(
        "set_clock_groups -asynchronous -group {sys_clk div_clk} -group {ext_clk}\n"
    );

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.clock_groups.size() == 1);
    CHECK(sdc.clock_groups[0].type == SdcClockGroup::Type::Asynchronous);
    REQUIRE(sdc.clock_groups[0].groups.size() == 2);
    CHECK(sdc.clock_groups[0].groups[0] == std::vector<std::string>{"sys_clk", "div_clk"});
    CHECK(sdc.clock_groups[0].groups[1] == std::vector<std::string>{"ext_clk"});
}

TEST_CASE("SDC parser: set_clock_groups physically_exclusive", "[sdc]") {
    auto path = writeTempSdc(
        "set_clock_groups -physically_exclusive -group {mux_clk_a} -group {mux_clk_b}\n"
    );

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.clock_groups.size() == 1);
    CHECK(sdc.clock_groups[0].type == SdcClockGroup::Type::Exclusive);
}

TEST_CASE("SDC parser: comments and blank lines ignored", "[sdc]") {
    auto path = writeTempSdc(
        "# This is a comment\n"
        "\n"
        "create_clock -name sys_clk -period 10 [get_ports sys_clk]\n"
        "# Another comment\n"
        "\n"
    );

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.clocks.size() == 1);
    CHECK(sdc.clocks[0].name == "sys_clk");
}

TEST_CASE("SDC parser: backslash line continuation", "[sdc]") {
    auto path = writeTempSdc(
        "create_clock -name sys_clk \\\n"
        "    -period 10 \\\n"
        "    [get_ports sys_clk]\n"
    );

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.clocks.size() == 1);
    CHECK(sdc.clocks[0].name == "sys_clk");
    CHECK(sdc.clocks[0].period.value() == 10.0);
    CHECK(sdc.clocks[0].target == "sys_clk");
}

TEST_CASE("SDC parser: empty file", "[sdc]") {
    auto path = writeTempSdc("");

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    CHECK(sdc.clocks.empty());
    CHECK(sdc.generated_clocks.empty());
    CHECK(sdc.clock_groups.empty());
}

TEST_CASE("SDC parser: unknown commands ignored", "[sdc]") {
    auto path = writeTempSdc(
        "set_input_delay -clock sys_clk 2.0 [get_ports data_in]\n"
        "create_clock -name sys_clk -period 10 [get_ports sys_clk]\n"
        "set_output_delay -clock sys_clk 1.5 [get_ports data_out]\n"
    );

    auto sdc = SdcParser::parse(path);
    fs::remove(path);

    REQUIRE(sdc.clocks.size() == 1);
    CHECK(sdc.clocks[0].name == "sys_clk");
}
