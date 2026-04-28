#include <catch2/catch_test_macros.hpp>
#include "ConventionChecker.h"
#include "TestUtils.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

namespace {
namespace fs = std::filesystem;
fs::path uniqueTempDir(const std::string& tag) {
    static std::atomic<unsigned> counter{0};
    const auto pid = static_cast<unsigned>(::getpid());
    const auto seq = ++counter;
    auto dir = fs::temp_directory_path() / ("svlens_" + tag + "_" + std::to_string(pid) + "_" + std::to_string(seq));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}
} // namespace

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
    auto yamlPath = uniqueTempDir("conv_rules") / "test_convention_rules.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input_prefix: in_\n";
        ofs << "output_prefix: out_\n";
        ofs << "instance:\n";
        ofs << "  prefix: inst_\n";
    }

    auto rules = loadConventionRules(yamlPath.string());
    CHECK(rules.inputPrefix == "in_");
    CHECK(rules.outputPrefix == "out_");
    CHECK(rules.instancePrefix == "inst_");

}

TEST_CASE("ConventionChecker: malformed scalar values do not crash") {
    // Previously only YAML::LoadFile was guarded by try/catch;
    // subsequent `.as<int>()` / `.as<bool>()` conversions
    // could throw YAML::BadConversion on malformed scalars (e.g.
    // `max_line_length: nope`) and abort the process. The extractor
    // now wraps the entire scalar-conversion phase, logs a warning,
    // and returns whatever has been successfully parsed so far.
    auto yamlPath = uniqueTempDir("conv_bad_scalar") / "test_convention_bad_scalar.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input_prefix: in_\n";
        ofs << "max_line_length: not_an_integer\n"; // bad int scalar
        ofs << "prohibit_hard_tabs: maybe\n";       // bad bool scalar
    }

    // Must not throw / crash.
    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath.string()));

    // Whatever parsed before the bad scalar should survive; the bad
    // scalars themselves keep their default values.
    CHECK(rules.inputPrefix == "in_");
    CHECK(rules.maxLineLength == 0);        // default
    CHECK(rules.prohibitHardTabs == false); // default

}

TEST_CASE("ConventionChecker: malformed string field keeps default and does not throw") {
    // getString() previously called .as<std::string>() directly; a
    // YAML sequence like `clock_pattern: [bad]` would throw
    // YAML::BadConversion out of
    // loadConventionRules, skipping all subsequent fields.  Now
    // getString() catches conversion errors and returns nullopt so
    // one bad string field only nulls that field.
    auto yamlPath = uniqueTempDir("conv_bad_string") / "test_convention_bad_string.yaml";
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
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath.string()));

    CHECK(rules.inputPrefix == "in_");
    CHECK(rules.clockPattern.empty());   // default — bad conversion kept default
    CHECK(rules.outputPrefix == "out_"); // valid string after bad one must apply

}

TEST_CASE("ConventionChecker: malformed nested prefix field keeps default and does not throw") {
    // Nested paths like `instance: { prefix: [bad] }` used raw
    // .as<std::string>() and could throw out of loadConventionRules.
    // Now each nested path
    // is guarded with try/catch so one bad nested scalar only nulls
    // that one field.
    // Note: ConventionRules::instancePrefix defaults to "u_"; a bad
    // conversion leaves that default in place rather than throwing.
    auto yamlPath = uniqueTempDir("conv_bad_nested") / "test_convention_bad_nested.yaml";
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
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath.string()));

    CHECK(rules.inputPrefix == "in_");
    // Bad conversion: instancePrefix keeps its struct default ("u_"), not overwritten.
    ConventionRules defaultRules;
    CHECK(rules.instancePrefix == defaultRules.instancePrefix);
    CHECK(rules.outputPrefix == "out_"); // field after bad nested must apply

}

TEST_CASE("ConventionChecker: malformed parent-shape YAML keeps default and does not throw") {
    // If a parent node like `input` is a scalar rather than a map
    // (e.g. `input: scalar_not_a_map`), then `root["input"]["prefix"]`
    // throws YAML::BadSubscript before the inner try/catch fires.  The
    // outer try must absorb this so load completes and the field keeps
    // its struct default.
    auto yamlPath = uniqueTempDir("conv_parent_shape") / "test_convention_parent_shape.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "input: scalar_not_a_map\n";    // parent is scalar, not map
        ofs << "output: scalar_not_a_map\n";   // same for output
        ofs << "instance: scalar_not_a_map\n"; // same for instance
        ofs << "output_prefix: out_\n";        // valid flat key after bad parents
    }

    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath.string()));

    // All nested-prefix fields keep struct defaults (no throw, no crash).
    ConventionRules defaultRules;
    CHECK(rules.inputPrefix == defaultRules.inputPrefix);
    CHECK(rules.instancePrefix == defaultRules.instancePrefix);
    // The valid flat key after the bad parents must still apply.
    CHECK(rules.outputPrefix == "out_");

}

TEST_CASE("ConventionChecker: bad scalar does not skip later valid keys") {
    // The previous whole-block try/catch aborted on the first
    // YAML::BadConversion, silently dropping any subsequent valid
    // keys depending on key order in the user's YAML.
    // After per-field tryAssign, a typo in `max_line_length` no longer
    // disables `prohibit_hard_tabs: true` further down in the file.
    auto yamlPath = uniqueTempDir("conv_per_field") / "test_convention_per_field.yaml";
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
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath.string()));

    // The bad int keeps its default.
    CHECK(rules.maxLineLength == 0);
    // The valid bools after the bad scalar MUST be applied.
    CHECK(rules.prohibitHardTabs == true);
    CHECK(rules.prohibitTrailingWhitespace == true);
    CHECK(rules.enforceFileModuleMatch == true);

}

TEST_CASE("ConventionChecker: cyclic anchor does not crash loader") {
    // Fresh review R1B WEAK #1 (HIGH risk): a malformed YAML with a
    // self-referencing alias used to bypass the LoadFile try/catch in
    // pre-CVE-2024-35325 yaml-cpp builds.  The loader now wraps
    // YAML::LoadFile in try/catch and surfaces a runtime_error if
    // yaml-cpp throws.  Either outcome (default rules OR a runtime
    // exception) is acceptable; what is NOT acceptable is a SIGSEGV
    // / stack overflow.
    auto yamlPath = uniqueTempDir("conv_cyclic") / "test_convention_cyclic_anchor.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        // Self-referencing alias.  yaml-cpp typically throws here;
        // the loader catches and rethrows as runtime_error.
        ofs << "key: &a\n";
        ofs << "  - *a\n";
    }

    // Either the loader returns a (possibly default) ConventionRules
    // OR it throws std::runtime_error.  Both outcomes are safe; the
    // failure mode this test guards against is a process crash.
    bool threw = false;
    ConventionRules rules;
    try {
        rules = loadConventionRules(yamlPath.string());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK((threw || rules.maxLineLength == 0));

}

TEST_CASE("ConventionChecker: deeply nested aliases do not stack-overflow") {
    // Fresh review R1B WEAK #1 (HIGH risk): adversarial YAML with
    // ~50 levels of nested anchors / sequences could provoke
    // unbounded recursion in older yaml-cpp builds.  Verify the
    // loader returns cleanly (either with defaults or a
    // runtime_error) without crashing.
    auto yamlPath = uniqueTempDir("conv_deep_nesting") / "test_convention_deep_nesting.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        // 50 levels of nested sequences as a flow-style scalar.
        ofs << "max_line_length: ";
        for (int i = 0; i < 50; ++i)
            ofs << "[";
        ofs << "100";
        for (int i = 0; i < 50; ++i)
            ofs << "]";
        ofs << "\n";
        // A valid key alongside, so we can verify per-field recovery.
        ofs << "prohibit_hard_tabs: true\n";
    }

    ConventionRules rules;
    bool threw = false;
    try {
        rules = loadConventionRules(yamlPath.string());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    // Either path: must not crash.  When the file parses, the deeply
    // nested sequence cannot be coerced to int so max_line_length
    // keeps its default (0); the unrelated valid bool either stays
    // default (load aborted) or applies cleanly (per-field recovery).
    CHECK((threw || rules.maxLineLength == 0));

}

TEST_CASE("ConventionChecker: empty YAML body produces default-initialized rules") {
    // Fresh review R1B WEAK #1: degenerate input.  An empty `{}`
    // body must NOT throw; the loader returns ConventionRules with
    // all defaults.
    auto yamlPath = uniqueTempDir("conv_empty") / "test_convention_empty.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "{}\n";
    }

    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath.string()));

    // Cross-check against a default-constructed instance.
    ConventionRules defaults;
    CHECK(rules.inputPrefix == defaults.inputPrefix);
    CHECK(rules.outputPrefix == defaults.outputPrefix);
    CHECK(rules.instancePrefix == defaults.instancePrefix);
    CHECK(rules.maxLineLength == defaults.maxLineLength);
    CHECK(rules.prohibitHardTabs == defaults.prohibitHardTabs);
    CHECK(rules.enforceFileModuleMatch == defaults.enforceFileModuleMatch);

}

TEST_CASE("ConventionChecker: unknown keys are silently ignored") {
    // Fresh review R1B WEAK #1: forward-compatibility check.  An
    // unknown top-level key (e.g. a future feature added by another
    // tooling layer) must NOT abort load nor invalidate the valid
    // keys that follow in the same file.
    auto yamlPath = uniqueTempDir("conv_unknown_key") / "test_convention_unknown_key.yaml";
    {
        std::ofstream ofs(yamlPath);
        REQUIRE(ofs.good());
        ofs << "unknown_future_key: \"ignored\"\n";
        ofs << "max_line_length: 100\n";
        ofs << "another_unknown: 42\n";
        ofs << "prohibit_hard_tabs: true\n";
    }

    ConventionRules rules;
    REQUIRE_NOTHROW(rules = loadConventionRules(yamlPath.string()));

    // Valid keys must be applied.
    CHECK(rules.maxLineLength == 100);
    CHECK(rules.prohibitHardTabs == true);

}
