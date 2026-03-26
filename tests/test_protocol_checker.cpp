#include <catch2/catch_test_macros.hpp>
#include "ProtocolChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 1) {
    PortInfo p;
    p.instancePath = inst; p.portName = name; p.direction = dir; p.width = width;
    return p;
}

TEST_CASE("ProtocolChecker: complete AXI4 write address channel produces no issues") {
    ConnectionGraph graph;
    // All required AW signals present
    graph.allPorts.push_back(makePort("top.u_axi", "axi_awvalid", ArgumentDirection::Out));
    graph.allPorts.push_back(makePort("top.u_axi", "axi_awready", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_axi", "axi_awaddr", ArgumentDirection::Out, 32));
    graph.allPorts.push_back(makePort("top.u_axi", "axi_awlen", ArgumentDirection::Out, 8));
    graph.allPorts.push_back(makePort("top.u_axi", "axi_awsize", ArgumentDirection::Out, 3));
    graph.allPorts.push_back(makePort("top.u_axi", "axi_awburst", ArgumentDirection::Out, 2));

    ProtocolChecker checker;
    auto issues = checker.check(graph);

    // Filter for AXI4 AW channel issues only
    size_t axi4AwIssues = 0;
    for (auto& iss : issues) {
        if (iss.detail.find("AXI4 ") != std::string::npos &&
            iss.detail.find("channel 'AW'") != std::string::npos) {
            axi4AwIssues++;
        }
    }
    CHECK(axi4AwIssues == 0);
}

TEST_CASE("ProtocolChecker: partial AXI4 write address channel reports PROTOCOL_INCOMPLETE") {
    ConnectionGraph graph;
    // Only AWVALID present, missing AWREADY, AWADDR, etc.
    graph.allPorts.push_back(makePort("top.u_axi", "m_awvalid", ArgumentDirection::Out));

    ProtocolChecker checker;
    auto issues = checker.check(graph);

    bool foundProtocolIssue = false;
    for (auto& iss : issues) {
        if (iss.type == Issue::Type::PROTOCOL_INCOMPLETE &&
            iss.detail.find("AWREADY") != std::string::npos) {
            foundProtocolIssue = true;
            CHECK(iss.severity == Issue::Severity::WARN);
        }
    }
    CHECK(foundProtocolIssue);
}

TEST_CASE("ProtocolChecker: no protocol signals produces no issues") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_core", "clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_core", "reset_n", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("top.u_core", "data_out", ArgumentDirection::Out, 32));

    ProtocolChecker checker;
    REQUIRE(checker.check(graph).empty());
}

TEST_CASE("ProtocolChecker: complete APB produces no issues") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_apb", "apb_psel", ArgumentDirection::Out));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_penable", ArgumentDirection::Out));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_pwrite", ArgumentDirection::Out));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_paddr", ArgumentDirection::Out, 32));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_pwdata", ArgumentDirection::Out, 32));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_prdata", ArgumentDirection::In, 32));
    graph.allPorts.push_back(makePort("top.u_apb", "apb_pready", ArgumentDirection::In));

    ProtocolChecker checker;
    auto issues = checker.check(graph);

    // No APB issues
    size_t apbIssues = 0;
    for (auto& iss : issues) {
        if (iss.detail.find("APB") != std::string::npos) {
            apbIssues++;
        }
    }
    CHECK(apbIssues == 0);
}
