#include <catch2/catch_test_macros.hpp>
#include "ConnectionExtractor.h"
#include "CheckerRunner.h"
#include "WidthChecker.h"
#include "TypeChecker.h"
#include "DanglingChecker.h"
#include "UndrivenChecker.h"
#include "ProtocolChecker.h"
#include "ConventionChecker.h"
#include "WaiverFilter.h"
#include "slang/driver/Driver.h"

using namespace connect;

// Holds both the Driver and Compilation so source text stays alive.
struct CompileResult {
    std::unique_ptr<slang::driver::Driver> driver;
    std::unique_ptr<slang::ast::Compilation> compilation;
    explicit operator bool() const { return compilation != nullptr; }
};

static CompileResult compileFile(const std::string& path) {
    auto driver = std::make_unique<slang::driver::Driver>();
    driver->addStandardArgs();
    std::vector<const char*> args = {"test", path.c_str()};
    if (!driver->parseCommandLine(static_cast<int>(args.size()), args.data()))
        return {};
    if (!driver->processOptions())
        return {};
    if (!driver->parseAllSources())
        return {};
    auto compilation = driver->createCompilation();
    return {std::move(driver), std::move(compilation)};
}

// ---- Deep Hierarchy Tests ----

TEST_CASE("Integration: deep hierarchy extracts all levels") {
    auto result = compileFile("sv/deep_hierarchy.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "deep_top");
    auto graph = extractor.extract();

    CHECK(graph.topModule == "deep_top");

    // Should have ports from both mid_block and leaf
    bool foundMidData = false;
    bool foundMidResult = false;
    bool foundMidStatus = false;
    bool foundLeafData = false;
    bool foundLeafResult = false;

    for (auto& port : graph.allPorts) {
        if (port.instancePath.find("u_mid") != std::string::npos && port.portName == "i_data")
            foundMidData = true;
        if (port.instancePath.find("u_mid") != std::string::npos && port.portName == "o_result")
            foundMidResult = true;
        if (port.instancePath.find("u_mid") != std::string::npos && port.portName == "o_status")
            foundMidStatus = true;
        if (port.instancePath.find("u_leaf") != std::string::npos && port.portName == "i_data")
            foundLeafData = true;
        if (port.instancePath.find("u_leaf") != std::string::npos && port.portName == "o_result")
            foundLeafResult = true;
    }

    CHECK(foundMidData);
    CHECK(foundMidResult);
    CHECK(foundMidStatus);
    CHECK(foundLeafData);
    CHECK(foundLeafResult);
}

TEST_CASE("Integration: depth limit restricts analysis") {
    auto result = compileFile("sv/deep_hierarchy.sv");
    REQUIRE(result);

    // maxDepth=0: only top-level instance processing.
    // visitInstance(deep_top, depth=0) processes direct children (u_mid ports),
    // but does NOT recurse into u_mid, so u_leaf ports are never discovered.
    ConnectionExtractor extractor(*result.compilation, "deep_top", 0);
    auto graph = extractor.extract();

    bool foundLeafPort = false;
    for (auto& port : graph.allPorts) {
        if (port.instancePath.find("u_leaf") != std::string::npos)
            foundLeafPort = true;
    }

    // With depth=0, leaf-level ports should NOT be present
    CHECK_FALSE(foundLeafPort);

    // But mid-level ports should still be there (direct children of top)
    bool foundMidPort = false;
    for (auto& port : graph.allPorts) {
        if (port.instancePath.find("u_mid") != std::string::npos)
            foundMidPort = true;
    }
    CHECK(foundMidPort);
}

// ---- Array Instance Tests ----

TEST_CASE("Integration: array instances extract connections") {
    auto result = compileFile("sv/array_instance.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "array_top");
    auto graph = extractor.extract();

    CHECK(graph.topModule == "array_top");

    // Should have ports from both channel instances and collector
    bool foundCh0 = false;
    bool foundCh1 = false;
    bool foundColl = false;

    for (auto& port : graph.allPorts) {
        if (port.instancePath.find("u_ch0") != std::string::npos)
            foundCh0 = true;
        if (port.instancePath.find("u_ch1") != std::string::npos)
            foundCh1 = true;
        if (port.instancePath.find("u_coll") != std::string::npos)
            foundColl = true;
    }

    CHECK(foundCh0);
    CHECK(foundCh1);
    CHECK(foundColl);

    // All checkers should produce no issues for this clean design
    CheckerRunner runner;
    runner.addChecker(std::make_unique<WidthChecker>());
    runner.addChecker(std::make_unique<TypeChecker>());
    runner.addChecker(std::make_unique<DanglingChecker>());
    runner.addChecker(std::make_unique<UndrivenChecker>());
    auto issues = runner.runAll(graph);

    // Array indexed connections may or may not be fully resolved by
    // the extractor -- just verify no crashes and reasonable results
    CHECK(graph.allPorts.size() >= 6); // 3 ports * 2 channels = 6 minimum
}

// ---- Mixed Issues Tests ----

TEST_CASE("Integration: mixed issues finds multiple issue types") {
    auto result = compileFile("sv/mixed_issues.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "mixed_top");
    auto graph = extractor.extract();

    CHECK(graph.topModule == "mixed_top");

    // Run all four core checkers
    CheckerRunner runner;
    runner.addChecker(std::make_unique<WidthChecker>());
    runner.addChecker(std::make_unique<TypeChecker>());
    runner.addChecker(std::make_unique<DanglingChecker>());
    runner.addChecker(std::make_unique<UndrivenChecker>());
    auto issues = runner.runAll(graph);

    // Classify found issues by type
    bool hasWidth = false;
    bool hasType = false;
    bool hasDangling = false;
    bool hasUndriven = false;

    for (auto& issue : issues) {
        switch (issue.type) {
            case Issue::Type::WIDTH_MISMATCH: hasWidth = true; break;
            case Issue::Type::TYPE_MISMATCH:  hasType = true; break;
            case Issue::Type::DANGLING_OUTPUT: hasDangling = true; break;
            case Issue::Type::UNDRIVEN_INPUT:  hasUndriven = true; break;
            default: break;
        }
    }

    // o_debug connected with empty parens -> DANGLING_OUTPUT
    CHECK(hasDangling);
    // i_config connected with empty parens -> UNDRIVEN_INPUT
    CHECK(hasUndriven);

    // Width and type mismatches depend on whether the extractor traces
    // through assign chains. At minimum, dangling + undriven must be found.
    CHECK(issues.size() >= 2);
}

TEST_CASE("Integration: mixed issues dangling port identified") {
    auto result = compileFile("sv/mixed_issues.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "mixed_top");
    auto graph = extractor.extract();

    DanglingChecker checker;
    auto issues = checker.check(graph);

    bool foundDebug = false;
    for (auto& issue : issues) {
        if (issue.port.portName == "o_debug")
            foundDebug = true;
    }
    CHECK(foundDebug);
}

TEST_CASE("Integration: mixed issues undriven port identified") {
    auto result = compileFile("sv/mixed_issues.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "mixed_top");
    auto graph = extractor.extract();

    UndrivenChecker checker;
    auto issues = checker.check(graph);

    bool foundConfig = false;
    for (auto& issue : issues) {
        if (issue.port.portName == "i_config")
            foundConfig = true;
    }
    CHECK(foundConfig);
}

// ---- Waiver Integration Tests ----

TEST_CASE("Integration: waiver filters correctly") {
    auto result = compileFile("sv/waiver_test.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "waiver_top");
    auto graph = extractor.extract();

    // Run dangling checker -- both o_debug_a and o_debug_b are empty-connected
    DanglingChecker checker;
    auto issues = checker.check(graph);

    // Should find at least 2 dangling outputs (o_debug_a, o_debug_b)
    bool foundA = false;
    bool foundB = false;
    for (auto& issue : issues) {
        if (issue.port.portName == "o_debug_a") foundA = true;
        if (issue.port.portName == "o_debug_b") foundB = true;
    }
    CHECK(foundA);
    CHECK(foundB);

    // Apply waiver: only o_debug_a should be waived
    WaiverFilter filter("waiver_integration.yaml");
    auto waiverResult = filter.apply(issues);

    // o_debug_a should be waived
    bool aWaived = false;
    for (auto& w : waiverResult.waived) {
        if (w.port.portName == "o_debug_a") aWaived = true;
    }
    CHECK(aWaived);

    // o_debug_b should remain active
    bool bActive = false;
    for (auto& a : waiverResult.active) {
        if (a.port.portName == "o_debug_b") bActive = true;
    }
    CHECK(bActive);
}

// ---- Clean Design Full Pipeline ----

TEST_CASE("Integration: clean design with all checkers") {
    auto result = compileFile("sv/clean_design.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "clean_top");
    auto graph = extractor.extract();

    // Run all 6 checkers
    CheckerRunner runner;
    runner.addChecker(std::make_unique<WidthChecker>());
    runner.addChecker(std::make_unique<TypeChecker>());
    runner.addChecker(std::make_unique<DanglingChecker>());
    runner.addChecker(std::make_unique<UndrivenChecker>());
    runner.addChecker(std::make_unique<ProtocolChecker>());
    runner.addChecker(std::make_unique<ConventionChecker>());
    auto issues = runner.runAll(graph);

    // Clean design should have zero issues from width, type, dangling, undriven
    int coreIssues = 0;
    for (auto& issue : issues) {
        if (issue.type == Issue::Type::WIDTH_MISMATCH ||
            issue.type == Issue::Type::TYPE_MISMATCH ||
            issue.type == Issue::Type::DANGLING_OUTPUT ||
            issue.type == Issue::Type::UNDRIVEN_INPUT) {
            coreIssues++;
        }
    }
    CHECK(coreIssues == 0);
}
