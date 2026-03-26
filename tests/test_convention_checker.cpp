#include <catch2/catch_test_macros.hpp>
#include "ConventionChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 8) {
    PortInfo p;
    p.instancePath = inst; p.portName = name; p.direction = dir; p.width = width;
    return p;
}

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
