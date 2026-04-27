#include <catch2/catch_test_macros.hpp>
#include "ConventionChecker.h"
#include "TestUtils.h"

#include <cstdio>
#include <fstream>

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

TEST_CASE("ConventionChecker: correctly named ports produce no issues") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_core", "i_data", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_core", "o_result", ArgumentDirection::Out));

    ConventionChecker checker;
    auto issues = checker.check(graph);

    // No port naming issues (instance u_core follows convention too)
    REQUIRE(issues.empty());
}

TEST_CASE("ConventionChecker: output without o_ prefix reports CONVENTION") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_core", "data_o", ArgumentDirection::Out));

    ConventionChecker checker;
    auto issues = checker.check(graph);

    bool foundOutputIssue = false;
    for (auto& iss : issues) {
        if (iss.type == Issue::Type::CONVENTION &&
            iss.detail.find("output port") != std::string::npos &&
            iss.detail.find("data_o") != std::string::npos) {
            foundOutputIssue = true;
            CHECK(iss.severity == Issue::Severity::INFO);
        }
    }
    CHECK(foundOutputIssue);
}

TEST_CASE("ConventionChecker: instance without u_ prefix reports CONVENTION") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.block_inst", "i_data", ArgumentDirection::In));

    ConventionChecker checker;
    auto issues = checker.check(graph);

    bool foundInstanceIssue = false;
    for (auto& iss : issues) {
        if (iss.type == Issue::Type::CONVENTION &&
            iss.detail.find("instance") != std::string::npos &&
            iss.detail.find("block_inst") != std::string::npos) {
            foundInstanceIssue = true;
            CHECK(iss.severity == Issue::Severity::INFO);
        }
    }
    CHECK(foundInstanceIssue);
}

TEST_CASE("ConventionChecker: input without i_ prefix reports CONVENTION") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_core", "data_in", ArgumentDirection::In));

    ConventionChecker checker;
    auto issues = checker.check(graph);

    bool foundInputIssue = false;
    for (auto& iss : issues) {
        if (iss.type == Issue::Type::CONVENTION &&
            iss.detail.find("input port") != std::string::npos &&
            iss.detail.find("data_in") != std::string::npos) {
            foundInputIssue = true;
            CHECK(iss.severity == Issue::Severity::INFO);
        }
    }
    CHECK(foundInputIssue);
}

TEST_CASE("ConventionChecker: custom rules work") {
    ConventionRules rules;
    rules.inputPrefix = "in_";
    rules.outputPrefix = "out_";
    rules.instancePrefix = "inst_";

    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.inst_core", "in_data", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.inst_core", "out_result", ArgumentDirection::Out));

    ConventionChecker checker(rules);
    REQUIRE(checker.check(graph).empty());
}

TEST_CASE("ConventionChecker: loads convention rules from YAML") {
    const char* yamlPath = "test_convention_rules.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input_prefix: in_\n";
        ofs << "output_prefix: out_\n";
        ofs << "instance:\n";
        ofs << "  prefix: inst_\n";
    }

    auto rules = loadConventionRules(yamlPath);
    CHECK(rules.inputPrefix == "in_");
    CHECK(rules.outputPrefix == "out_");
    CHECK(rules.instancePrefix == "inst_");

    std::remove(yamlPath);
}

TEST_CASE("ConventionChecker: malformed scalar values do not crash") {
    // Previously only YAML::LoadFile was guarded by try/catch;
    // subsequent `.as<int>()` / `.as<bool>()` conversions
    // could throw YAML::BadConversion on malformed scalars (e.g.
    // `max_line_length: nope`) and abort the process. The extractor
    // now wraps the entire scalar-conversion phase, logs a warning,
    // and returns whatever has been successfully parsed so far.
    const char* yamlPath = "test_convention_bad_scalar.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input_prefix: in_\n";
        ofs << "max_line_length: not_an_integer\n"; // bad int scalar
        ofs << "prohibit_hard_tabs: maybe\n";       // bad bool scalar
    }

    // Must not throw / crash.
    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath));

    // Whatever parsed before the bad scalar should survive; the bad
    // scalars themselves keep their default values.
    CHECK(rules.inputPrefix == "in_");
    CHECK(rules.maxLineLength == 0);        // default
    CHECK(rules.prohibitHardTabs == false); // default

    std::remove(yamlPath);
}

TEST_CASE("ConventionChecker: malformed string field keeps default and does not throw") {
    // getString() previously called .as<std::string>() directly; a
    // YAML sequence like `clock_pattern: [bad]` would throw
    // YAML::BadConversion out of
    // loadConventionRules, skipping all subsequent fields.  Now
    // getString() catches conversion errors and returns nullopt so
    // one bad string field only nulls that field.
    const char* yamlPath = "test_convention_bad_string.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input_prefix: in_\n";
        // A mapping node causes YAML::BadConversion on .as<std::string>().
        // A sequence like [bad] is silently stringified by yaml-cpp so we
        // use a nested map to reliably trigger a conversion error.
        ofs << "clock_pattern:\n";
        ofs << "  nested_key: nested_val\n"; // map where scalar expected
        ofs << "output_prefix: out_\n";      // valid string after bad one
    }

    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath));

    CHECK(rules.inputPrefix == "in_");
    CHECK(rules.clockPattern.empty());   // default — bad conversion kept default
    CHECK(rules.outputPrefix == "out_"); // valid string after bad one must apply

    std::remove(yamlPath);
}

TEST_CASE("ConventionChecker: malformed nested prefix field keeps default and does not throw") {
    // Nested paths like `instance: { prefix: [bad] }` used raw
    // .as<std::string>() and could throw out of loadConventionRules.
    // Now each nested path
    // is guarded with try/catch so one bad nested scalar only nulls
    // that one field.
    // Note: ConventionRules::instancePrefix defaults to "u_"; a bad
    // conversion leaves that default in place rather than throwing.
    const char* yamlPath = "test_convention_bad_nested.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input_prefix: in_\n";
        // A mapping node causes YAML::BadConversion on .as<std::string>().
        ofs << "instance:\n";
        ofs << "  prefix:\n";
        ofs << "    nested_key: nested_val\n"; // map where scalar expected
        ofs << "output_prefix: out_\n";
    }

    ConventionRules rules;
    // Must not throw despite bad nested scalar.
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath));

    CHECK(rules.inputPrefix == "in_");
    // Bad conversion: instancePrefix keeps its struct default ("u_"), not overwritten.
    ConventionRules defaultRules;
    CHECK(rules.instancePrefix == defaultRules.instancePrefix);
    CHECK(rules.outputPrefix == "out_"); // field after bad nested must apply

    std::remove(yamlPath);
}

TEST_CASE("ConventionChecker: malformed parent-shape YAML keeps default and does not throw") {
    // If a parent node like `input` is a scalar rather than a map
    // (e.g. `input: scalar_not_a_map`), then `root["input"]["prefix"]`
    // throws YAML::BadSubscript before the inner try/catch fires.  The
    // outer try must absorb this so load completes and the field keeps
    // its struct default.
    const char* yamlPath = "test_convention_parent_shape.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input: scalar_not_a_map\n";    // parent is scalar, not map
        ofs << "output: scalar_not_a_map\n";   // same for output
        ofs << "instance: scalar_not_a_map\n"; // same for instance
        ofs << "output_prefix: out_\n";        // valid flat key after bad parents
    }

    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath));

    // All nested-prefix fields keep struct defaults (no throw, no crash).
    ConventionRules defaultRules;
    CHECK(rules.inputPrefix == defaultRules.inputPrefix);
    CHECK(rules.instancePrefix == defaultRules.instancePrefix);
    // The valid flat key after the bad parents must still apply.
    CHECK(rules.outputPrefix == "out_");

    std::remove(yamlPath);
}

TEST_CASE("ConventionChecker: bad scalar does not skip later valid keys") {
    // The previous whole-block try/catch aborted on the first
    // YAML::BadConversion, silently dropping any subsequent valid
    // keys depending on key order in the user's YAML.
    // After per-field tryAssign, a typo in `max_line_length` no longer
    // disables `prohibit_hard_tabs: true` further down in the file.
    const char* yamlPath = "test_convention_per_field.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        // Bad scalar comes FIRST, valid scalars come AFTER.  Under the
        // old behavior the entire block would abort on the first bad
        // value and rules.prohibitHardTabs would remain false.  With
        // per-field extraction the valid keys still apply.
        ofs << "max_line_length: nope\n";              // bad int scalar
        ofs << "prohibit_hard_tabs: true\n";           // valid bool
        ofs << "prohibit_trailing_whitespace: true\n"; // valid bool
        ofs << "enforce_file_module_match: true\n";    // valid bool
    }

    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath));

    // The bad int keeps its default.
    CHECK(rules.maxLineLength == 0);
    // The valid bools after the bad scalar MUST be applied.
    CHECK(rules.prohibitHardTabs == true);
    CHECK(rules.prohibitTrailingWhitespace == true);
    CHECK(rules.enforceFileModuleMatch == true);

    std::remove(yamlPath);
}
