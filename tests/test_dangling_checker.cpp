#include <catch2/catch_test_macros.hpp>
#include "DanglingChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 8) {
    PortInfo p;
    p.instancePath = inst; p.portName = name; p.direction = dir; p.width = width;
    return p;
}

TEST_CASE("DanglingChecker: connected output produces no issues") {
    ConnectionGraph graph;
    auto outPort = makePort("top.u_a", "o_valid", ArgumentDirection::Out, 1);
    auto inPort = makePort("top.u_b", "i_valid", ArgumentDirection::In, 1);
    graph.allPorts.push_back(outPort);
    graph.allPorts.push_back(inPort);
    graph.connections.push_back({outPort, inPort});
    DanglingChecker checker;
    REQUIRE(checker.check(graph).empty());
}

TEST_CASE("DanglingChecker: unconnected output is WARN") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_a", "o_debug", ArgumentDirection::Out));
    DanglingChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::DANGLING_OUTPUT);
    CHECK(issues[0].severity == Issue::Severity::WARN);
    CHECK(issues[0].port.portName == "o_debug");
}

TEST_CASE("DanglingChecker: input ports are ignored") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_a", "i_data", ArgumentDirection::In));
    DanglingChecker checker;
    REQUIRE(checker.check(graph).empty());
}
