#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/clock_yaml_parser.h"
#include "sv-cdccheck/types.h"

#include <cmath>

using namespace sv_cdccheck;

TEST_CASE("CDC ClockYamlParser: parses clock sources and domain groups", "[cdc][clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: sys_clk
        frequency: 200MHz
      - signal: axi_clk
        frequency: 100MHz
domain_groups:
  async:
    - [sys_clk, pixel_clk]
  related:
    - [sys_clk, axi_clk]
)";

    REQUIRE(parser.loadString(yaml));
    const auto& config = parser.getConfig();
    REQUIRE(config.clock_sources.size() == 1);
    REQUIRE(config.clock_sources[0].outputs.size() == 2);
    REQUIRE(config.domain_groups.size() == 2);
}

TEST_CASE("CDC ClockYamlParser: applyTo populates sources and relationships", "[cdc][clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: sys_clk
        frequency: 200MHz
  - name: pll1
    outputs:
      - signal: pixel_clk
        frequency: 100MHz
domain_groups:
  async:
    - [sys_clk, pixel_clk]
)";

    REQUIRE(parser.loadString(yaml));
    ClockDatabase db;
    parser.applyTo(db);

    REQUIRE(db.sources.size() == 2);
    REQUIRE(db.relationships.size() == 1);
    CHECK(db.relationships[0].relationship == DomainRelationship::Type::Asynchronous);

    bool foundSys = false;
    for (const auto& src : db.sources) {
        if (src->name == "sys_clk") {
            foundSys = true;
            REQUIRE(src->period_ns.has_value());
            CHECK(std::abs(src->period_ns.value() - 5.0) < 0.01);
        }
    }
    CHECK(foundSys);
}

TEST_CASE("CDC ClockYamlParser: empty input returns false", "[cdc][clock_yaml]") {
    ClockYamlParser parser;
    CHECK_FALSE(parser.loadString(""));
    CHECK(parser.getConfig().clock_sources.empty());
    CHECK(parser.getConfig().domain_groups.empty());
}
