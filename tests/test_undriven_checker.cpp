#include <catch2/catch_test_macros.hpp>
#include "UndrivenChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 8) {
    PortInfo p;
    p.instancePath = inst; p.portName = name; p.direction = dir; p.width = width;
    return p;
}

TEST_CASE("UndrivenChecker: driven input produces no issues") {
    ConnectionGraph graph;
    auto outPort = makePort("top.u_a", "o_data", ArgumentDirection::Out);
    auto inPort = makePort("top.u_b", "i_data", ArgumentDirection::In);
    graph.allPorts.push_back(outPort);
    graph.allPorts.push_back(inPort);
    graph.connections.push_back({outPort, inPort});
    UndrivenChecker checker;
    REQUIRE(checker.check(graph).empty());
}

TEST_CASE("UndrivenChecker: undriven input is ERROR") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_b", "i_config", ArgumentDirection::In));
    UndrivenChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::UNDRIVEN_INPUT);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
    CHECK(issues[0].port.portName == "i_config");
}

TEST_CASE("UndrivenChecker: output ports are ignored") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_a", "o_data", ArgumentDirection::Out));
    UndrivenChecker checker;
    REQUIRE(checker.check(graph).empty());
}
