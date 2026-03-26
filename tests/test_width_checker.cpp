#include <catch2/catch_test_macros.hpp>
#include "WidthChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width, bool isSigned = false) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    p.isSigned = isSigned;
    return p;
}

TEST_CASE("WidthChecker: exact match produces no issues") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 32),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 32)
    });
    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.empty());
}

TEST_CASE("WidthChecker: truncation is ERROR") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 32),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16)
    });
    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::WIDTH_MISMATCH);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
}

TEST_CASE("WidthChecker: unsigned zero-extension is WARN") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, false),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 32, false)
    });
    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Issue::Severity::WARN);
}

TEST_CASE("WidthChecker: signed sign-extension is INFO") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 32, true)
    });
    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Issue::Severity::INFO);
}
