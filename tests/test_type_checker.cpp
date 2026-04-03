#include <catch2/catch_test_macros.hpp>
#include "TypeChecker.h"
#include "TestUtils.h"

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

TEST_CASE("TypeChecker: same signedness produces no issues") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16, true)
    });
    TypeChecker checker;
    REQUIRE(checker.check(graph).empty());
}

TEST_CASE("TypeChecker: signed to unsigned is ERROR") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_coeff", ArgumentDirection::Out, 16, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16, false)
    });
    TypeChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::TYPE_MISMATCH);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
}

TEST_CASE("TypeChecker: unsigned to signed is ERROR") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, false),
        makePort("top.u_b", "i_coeff", ArgumentDirection::In, 16, true)
    });
    TypeChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
}

TEST_CASE("TypeChecker: approximate aggregate edges are skipped") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_hi", ArgumentDirection::Out, 8, true),
        makePort("top.u_b", "i_bus", ArgumentDirection::In, 8, false),
        ConnectionKind::Approximate
    });

    TypeChecker checker;
    CHECK(checker.check(graph).empty());
}
