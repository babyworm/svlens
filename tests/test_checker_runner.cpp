#include <catch2/catch_test_macros.hpp>
#include "CheckerRunner.h"
#include "WidthChecker.h"
#include "TypeChecker.h"
#include "TestUtils.h"

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

TEST_CASE("CheckerRunner: no checkers produces no issues") {
    ConnectionGraph graph;
    CheckerRunner runner;
    REQUIRE(runner.runAll(graph).empty());
}

TEST_CASE("CheckerRunner: aggregates issues from multiple checkers") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 32, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16, false)
    });
    CheckerRunner runner;
    runner.addChecker(std::make_unique<WidthChecker>());
    runner.addChecker(std::make_unique<TypeChecker>());
    auto issues = runner.runAll(graph);
    REQUIRE(issues.size() == 2);
    bool hasWidth = false, hasType = false;
    for (auto& issue : issues) {
        if (issue.type == Issue::Type::WIDTH_MISMATCH) hasWidth = true;
        if (issue.type == Issue::Type::TYPE_MISMATCH) hasType = true;
    }
    CHECK(hasWidth);
    CHECK(hasType);
}
