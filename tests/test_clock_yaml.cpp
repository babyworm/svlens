#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/clock_yaml_parser.h"
#include "sv-cdccheck/types.h"

#include <fstream>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

// =============================================================================
// Basic YAML Parsing
// =============================================================================

TEST_CASE("ClockYaml: parse clock_sources section", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: sys_clk
        frequency: 200MHz
        relationship: independent
      - signal: axi_clk
        frequency: 100MHz
        relationship: independent
)";

    REQUIRE(parser.loadString(yaml));
    auto& config = parser.getConfig();
    REQUIRE(config.clock_sources.size() == 1);

    auto& src = config.clock_sources[0];
    CHECK(src.name == "pll0");
    REQUIRE(src.outputs.size() == 2);
    CHECK(src.outputs[0].signal == "sys_clk");
    CHECK(src.outputs[0].frequency == "200MHz");
    CHECK(src.outputs[0].relationship == "independent");
    CHECK(src.outputs[1].signal == "axi_clk");
    CHECK(src.outputs[1].frequency == "100MHz");
}

TEST_CASE("ClockYaml: parse domain_groups section", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: sys_clk
        frequency: 200MHz
domain_groups:
  async:
    - [sys_clk, pixel_clk]
  related:
    - [sys_clk, axi_clk]
)";

    REQUIRE(parser.loadString(yaml));
    auto& config = parser.getConfig();

    REQUIRE(config.domain_groups.size() == 2);

    bool found_async = false, found_related = false;
    for (auto& dg : config.domain_groups) {
        if (dg.type == "async") {
            found_async = true;
            REQUIRE(dg.groups.size() == 1);
            REQUIRE(dg.groups[0].size() == 2);
            CHECK(dg.groups[0][0] == "sys_clk");
            CHECK(dg.groups[0][1] == "pixel_clk");
        }
        if (dg.type == "related") {
            found_related = true;
            REQUIRE(dg.groups.size() == 1);
            REQUIRE(dg.groups[0].size() == 2);
            CHECK(dg.groups[0][0] == "sys_clk");
            CHECK(dg.groups[0][1] == "axi_clk");
        }
    }
    CHECK(found_async);
    CHECK(found_related);
}

TEST_CASE("ClockYaml: multiple clock sources", "[clock_yaml]") {
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
        frequency: 148.5MHz
)";

    REQUIRE(parser.loadString(yaml));
    auto& config = parser.getConfig();
    REQUIRE(config.clock_sources.size() == 2);
    CHECK(config.clock_sources[0].name == "pll0");
    CHECK(config.clock_sources[0].outputs[0].signal == "sys_clk");
    CHECK(config.clock_sources[1].name == "pll1");
    CHECK(config.clock_sources[1].outputs[0].signal == "pixel_clk");
}

TEST_CASE("ClockYaml: multiple async groups", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
domain_groups:
  async:
    - [clk_a, clk_b]
    - [clk_c, clk_d, clk_e]
)";

    REQUIRE(parser.loadString(yaml));
    auto& config = parser.getConfig();
    REQUIRE(config.domain_groups.size() == 1);
    CHECK(config.domain_groups[0].type == "async");
    REQUIRE(config.domain_groups[0].groups.size() == 2);
    CHECK(config.domain_groups[0].groups[0].size() == 2);
    CHECK(config.domain_groups[0].groups[1].size() == 3);
    CHECK(config.domain_groups[0].groups[1][2] == "clk_e");
}

TEST_CASE("ClockYaml: empty input returns false", "[clock_yaml]") {
    ClockYamlParser parser;
    CHECK_FALSE(parser.loadString(""));
    CHECK(parser.getConfig().clock_sources.empty());
    CHECK(parser.getConfig().domain_groups.empty());
}

TEST_CASE("ClockYaml: comments and blank lines ignored", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
# This is a comment
clock_sources:
  # Another comment
  - name: pll0
    outputs:
      - signal: sys_clk
        frequency: 200MHz

# Blank lines above
domain_groups:
  async:
    - [sys_clk, pixel_clk]
)";

    REQUIRE(parser.loadString(yaml));
    CHECK(parser.getConfig().clock_sources.size() == 1);
    CHECK(parser.getConfig().domain_groups.size() == 1);
}

// =============================================================================
// Apply to ClockDatabase
// =============================================================================

TEST_CASE("ClockYaml: applyTo creates clock sources in database", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: sys_clk
        frequency: 200MHz
      - signal: axi_clk
        frequency: 100MHz
)";

    REQUIRE(parser.loadString(yaml));

    ClockDatabase db;
    parser.applyTo(db);

    REQUIRE(db.sources.size() == 2);

    bool found_sys = false, found_axi = false;
    for (auto& s : db.sources) {
        if (s->name == "sys_clk") {
            found_sys = true;
            REQUIRE(s->period_ns.has_value());
            CHECK(std::abs(s->period_ns.value() - 5.0) < 0.01);  // 1000/200 = 5ns
        }
        if (s->name == "axi_clk") {
            found_axi = true;
            REQUIRE(s->period_ns.has_value());
            CHECK(std::abs(s->period_ns.value() - 10.0) < 0.01);  // 1000/100 = 10ns
        }
    }
    CHECK(found_sys);
    CHECK(found_axi);
}

TEST_CASE("ClockYaml: applyTo creates async relationships", "[clock_yaml]") {
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
        frequency: 148MHz
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
    CHECK(db.relationships[0].a->name == "sys_clk");
    CHECK(db.relationships[0].b->name == "pixel_clk");
}

TEST_CASE("ClockYaml: applyTo creates related relationships", "[clock_yaml]") {
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
  related:
    - [sys_clk, axi_clk]
)";

    REQUIRE(parser.loadString(yaml));

    ClockDatabase db;
    parser.applyTo(db);

    REQUIRE(db.relationships.size() == 1);
    CHECK(db.relationships[0].relationship == DomainRelationship::Type::SameSource);
}

TEST_CASE("ClockYaml: frequency parsing GHz", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: fast_clk
        frequency: 1GHz
)";

    REQUIRE(parser.loadString(yaml));

    ClockDatabase db;
    parser.applyTo(db);

    REQUIRE(db.sources.size() == 1);
    REQUIRE(db.sources[0]->period_ns.has_value());
    CHECK(std::abs(db.sources[0]->period_ns.value() - 1.0) < 0.01);  // 1000/1000 = 1ns
}

TEST_CASE("ClockYaml: frequency parsing KHz", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: slow_clk
        frequency: 32768KHz
)";

    REQUIRE(parser.loadString(yaml));

    ClockDatabase db;
    parser.applyTo(db);

    REQUIRE(db.sources.size() == 1);
    REQUIRE(db.sources[0]->period_ns.has_value());
    // 1000 / 32.768 ~= 30.518ns
    CHECK(db.sources[0]->period_ns.value() > 30.0);
    CHECK(db.sources[0]->period_ns.value() < 31.0);
}

// =============================================================================
// File Loading
// =============================================================================

TEST_CASE("ClockYaml: loadFile reads from disk", "[clock_yaml]") {
    static int counter = 0;
    auto path = fs::temp_directory_path() /
        ("test_clock_yaml_" + std::to_string(counter++) + ".yaml");

    {
        std::ofstream f(path);
        f << "clock_sources:\n"
          << "  - name: pll0\n"
          << "    outputs:\n"
          << "      - signal: sys_clk\n"
          << "        frequency: 200MHz\n";
    }

    ClockYamlParser parser;
    REQUIRE(parser.loadFile(path.string()));
    CHECK(parser.getConfig().clock_sources.size() == 1);
    CHECK(parser.getConfig().clock_sources[0].name == "pll0");
}

TEST_CASE("ClockYaml: loadFile returns false for missing file", "[clock_yaml]") {
    ClockYamlParser parser;
    CHECK_FALSE(parser.loadFile("/nonexistent/clock.yaml"));
}

TEST_CASE("ClockYaml: applyTo does not duplicate existing sources", "[clock_yaml]") {
    ClockYamlParser parser;
    std::string yaml = R"(
clock_sources:
  - name: pll0
    outputs:
      - signal: sys_clk
        frequency: 200MHz
)";

    REQUIRE(parser.loadString(yaml));

    ClockDatabase db;
    // Pre-add the same source
    auto existing = std::make_unique<ClockSource>();
    existing->name = "sys_clk";
    existing->type = ClockSource::Type::Primary;
    db.addSource(std::move(existing));

    parser.applyTo(db);

    // Should still be just 1 source, not duplicated
    int count = 0;
    for (auto& s : db.sources) {
        if (s->name == "sys_clk") count++;
    }
    CHECK(count == 1);
}
