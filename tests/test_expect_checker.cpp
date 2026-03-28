#include <catch2/catch_test_macros.hpp>
#include "ExpectChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 1) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    return p;
}

static Connection makeConn(const std::string& srcInst, const std::string& srcPort,
                           const std::string& dstInst, const std::string& dstPort) {
    Connection c;
    c.source = makePort(srcInst, srcPort, ArgumentDirection::Out);
    c.dest = makePort(dstInst, dstPort, ArgumentDirection::In);
    return c;
}

static ConnectionGraph makeExpectTestGraph() {
    ConnectionGraph graph;
    graph.topModule = "soc_top";

    // cpu.o_ibus_addr -> bus.i_cpu_ibus_addr
    auto c1 = makeConn("soc_top.u_cpu", "o_ibus_addr",
                        "soc_top.u_bus", "i_cpu_ibus_addr");
    graph.connections.push_back(c1);
    graph.allPorts.push_back(c1.source);
    graph.allPorts.push_back(c1.dest);
    graph.connectedPorts.insert(c1.source.fullPath());
    graph.connectedPorts.insert(c1.dest.fullPath());

    // bus.o_mem_addr -> mem.i_addr
    auto c2 = makeConn("soc_top.u_bus", "o_mem_addr",
                        "soc_top.u_mem", "i_addr");
    graph.connections.push_back(c2);
    graph.allPorts.push_back(c2.source);
    graph.allPorts.push_back(c2.dest);
    graph.connectedPorts.insert(c2.source.fullPath());
    graph.connectedPorts.insert(c2.dest.fullPath());

    return graph;
}

TEST_CASE("ExpectChecker: all expected connections satisfied, no forbidden", "[expect]") {
    ExpectChecker checker("expected_conn.yaml");
    auto graph = makeExpectTestGraph();
    auto issues = checker.check(graph);

    size_t expectIssues = 0;
    for (const auto& iss : issues) {
        if (iss.type == Issue::Type::EXPECT_MISSING ||
            iss.type == Issue::Type::EXPECT_FORBIDDEN) {
            expectIssues++;
        }
    }
    CHECK(expectIssues == 0);
}

TEST_CASE("ExpectChecker: missing expected connection detected", "[expect]") {
    ExpectChecker checker("expected_conn.yaml");

    // Empty graph -- both expected rules should fail
    ConnectionGraph graph;
    graph.topModule = "soc_top";

    auto issues = checker.check(graph);

    size_t missingCount = 0;
    for (const auto& iss : issues) {
        if (iss.type == Issue::Type::EXPECT_MISSING) {
            CHECK(iss.severity == Issue::Severity::ERROR);
            missingCount++;
        }
    }
    CHECK(missingCount == 2);
}

TEST_CASE("ExpectChecker: forbidden connection detected", "[expect]") {
    ExpectChecker checker("expected_conn.yaml");

    ConnectionGraph graph;
    graph.topModule = "soc_top";

    // Satisfy expected rules
    auto c1 = makeConn("soc_top.u_cpu", "o_ibus_addr",
                        "soc_top.u_bus", "i_cpu_ibus_addr");
    graph.connections.push_back(c1);
    auto c2 = makeConn("soc_top.u_bus", "o_mem_addr",
                        "soc_top.u_mem", "i_addr");
    graph.connections.push_back(c2);

    // Add a forbidden connection: mem -> cpu
    auto bad = makeConn("soc_top.u_mem", "o_debug",
                        "soc_top.u_cpu", "i_debug");
    graph.connections.push_back(bad);

    auto issues = checker.check(graph);

    size_t forbiddenCount = 0;
    for (const auto& iss : issues) {
        if (iss.type == Issue::Type::EXPECT_FORBIDDEN) {
            CHECK(iss.severity == Issue::Severity::ERROR);
            forbiddenCount++;
        }
    }
    CHECK(forbiddenCount == 1);
}

TEST_CASE("ExpectChecker: glob matching works with wildcards", "[expect]") {
    ExpectChecker checker("expected_conn.yaml");

    // Use a different top-level name -- globs should still match via *
    ConnectionGraph graph;
    graph.topModule = "chip_top";

    auto c1 = makeConn("chip_top.u_cpu", "o_ibus_addr",
                        "chip_top.u_bus", "i_cpu_ibus_addr");
    graph.connections.push_back(c1);
    auto c2 = makeConn("chip_top.u_bus", "o_mem_addr",
                        "chip_top.u_mem", "i_addr");
    graph.connections.push_back(c2);

    auto issues = checker.check(graph);

    size_t expectIssues = 0;
    for (const auto& iss : issues) {
        if (iss.type == Issue::Type::EXPECT_MISSING ||
            iss.type == Issue::Type::EXPECT_FORBIDDEN) {
            expectIssues++;
        }
    }
    CHECK(expectIssues == 0);
}
