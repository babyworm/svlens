#include <catch2/catch_test_macros.hpp>
#include "UndrivenChecker.h"
#include "TestUtils.h"

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

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

TEST_CASE("UndrivenChecker: optional exists_* inputs are suppressed when exists_req_i is tied low") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_q", "exists_req_i", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_q", "exists_data_i", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_q", "exists_mask_i", ArgumentDirection::In));
    graph.connectedPorts.insert("top.u_q.exists_req_i");
    graph.constantZeroTieOffPorts.insert("top.u_q.exists_req_i");

    UndrivenChecker checker;
    auto issues = checker.check(graph);
    CHECK(issues.empty());
}

TEST_CASE("UndrivenChecker: optional exists_* inputs still error when disable companion is not tied low") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_q", "exists_req_i", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_q", "exists_data_i", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_q", "exists_mask_i", ArgumentDirection::In));
    graph.connectedPorts.insert("top.u_q.exists_req_i");

    UndrivenChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 2);
}

TEST_CASE("UndrivenChecker: optional exists_* suppression is limited to same instance scope") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_a", "exists_req_i", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_b", "exists_data_i", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_b", "exists_mask_i", ArgumentDirection::In));
    graph.connectedPorts.insert("top.u_a.exists_req_i");
    graph.constantZeroTieOffPorts.insert("top.u_a.exists_req_i");

    UndrivenChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].type == Issue::Type::UNDRIVEN_INPUT);
    CHECK(issues[1].type == Issue::Type::UNDRIVEN_INPUT);
}
