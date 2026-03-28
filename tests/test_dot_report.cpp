#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "DotReport.h"
#include <sstream>

using namespace connect;
using namespace Catch::Matchers;
using slang::ast::ArgumentDirection;

static ReportData makeDotTestData() {
    ReportData data;
    data.topModule = "soc_top";

    PortInfo src;
    src.instancePath = "soc_top.u_cpu"; src.portName = "o_data";
    src.direction = ArgumentDirection::Out; src.width = 32; src.isSigned = false;

    PortInfo dst;
    dst.instancePath = "soc_top.u_bus"; dst.portName = "i_data";
    dst.direction = ArgumentDirection::In; dst.width = 16; dst.isSigned = false;

    Connection conn{src, dst};
    data.graph.connections.push_back(conn);

    Issue issue;
    issue.type = Issue::Type::WIDTH_MISMATCH;
    issue.severity = Issue::Severity::ERROR;
    issue.port = src;
    issue.connection = conn;
    issue.detail = "Truncation: 32 bits -> 16 bits";
    data.active.push_back(issue);

    return data;
}

static ReportData makeMultiConnectionData() {
    ReportData data;
    data.topModule = "soc_top";

    // Connection 1: cpu -> bus (with error)
    PortInfo src1;
    src1.instancePath = "soc_top.u_cpu"; src1.portName = "o_data";
    src1.direction = ArgumentDirection::Out; src1.width = 32; src1.isSigned = false;

    PortInfo dst1;
    dst1.instancePath = "soc_top.u_bus"; dst1.portName = "i_data";
    dst1.direction = ArgumentDirection::In; dst1.width = 16; dst1.isSigned = false;

    Connection conn1{src1, dst1};
    data.graph.connections.push_back(conn1);

    // Connection 2: bus -> mem (clean)
    PortInfo src2;
    src2.instancePath = "soc_top.u_bus"; src2.portName = "o_mem_req";
    src2.direction = ArgumentDirection::Out; src2.width = 8; src2.isSigned = false;

    PortInfo dst2;
    dst2.instancePath = "soc_top.u_mem"; dst2.portName = "i_req";
    dst2.direction = ArgumentDirection::In; dst2.width = 8; dst2.isSigned = false;

    Connection conn2{src2, dst2};
    data.graph.connections.push_back(conn2);

    // Connection 3: cpu -> bus again (with warning)
    PortInfo src3;
    src3.instancePath = "soc_top.u_cpu"; src3.portName = "o_addr";
    src3.direction = ArgumentDirection::Out; src3.width = 32; src3.isSigned = false;

    PortInfo dst3;
    dst3.instancePath = "soc_top.u_bus"; dst3.portName = "i_addr";
    dst3.direction = ArgumentDirection::In; dst3.width = 32; dst3.isSigned = false;

    Connection conn3{src3, dst3};
    data.graph.connections.push_back(conn3);

    // Error on conn1
    Issue err;
    err.type = Issue::Type::WIDTH_MISMATCH;
    err.severity = Issue::Severity::ERROR;
    err.port = src1;
    err.connection = conn1;
    err.detail = "Truncation: 32 bits -> 16 bits";
    data.active.push_back(err);

    // Warning on conn3
    Issue warn;
    warn.type = Issue::Type::CONVENTION;
    warn.severity = Issue::Severity::WARN;
    warn.port = src3;
    warn.connection = conn3;
    warn.detail = "Naming convention mismatch";
    data.active.push_back(warn);

    return data;
}

TEST_CASE("DotReport: produces valid DOT with digraph keyword") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("digraph"));
    CHECK_THAT(dot, ContainsSubstring("soc_top"));
    CHECK_THAT(dot, ContainsSubstring("rankdir=LR"));
    // Must have opening and closing braces
    CHECK(dot.find('{') != std::string::npos);
    CHECK(dot.find('}') != std::string::npos);
}

TEST_CASE("DotReport: modules appear as nodes") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("u_cpu"));
    CHECK_THAT(dot, ContainsSubstring("u_bus"));
    CHECK_THAT(dot, ContainsSubstring("record"));
}

TEST_CASE("DotReport: connections appear as edges with width labels") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("->"));
    CHECK_THAT(dot, ContainsSubstring("o_data"));
    // Width should appear in the label
    CHECK_THAT(dot, ContainsSubstring("32"));
}

TEST_CASE("DotReport: error connections are colored red") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("red"));
}

TEST_CASE("DotReport: warning connections are colored orange") {
    // Isolated WARN connection (no ERROR on same edge) to verify orange
    ReportData data;
    data.topModule = "soc_top";

    PortInfo src;
    src.instancePath = "soc_top.u_bus"; src.portName = "o_status";
    src.direction = ArgumentDirection::Out; src.width = 8; src.isSigned = false;

    PortInfo dst;
    dst.instancePath = "soc_top.u_mon"; dst.portName = "i_status";
    dst.direction = ArgumentDirection::In; dst.width = 8; dst.isSigned = true;

    Connection conn{src, dst};
    data.graph.connections.push_back(conn);

    Issue warn;
    warn.type = Issue::Type::TYPE_MISMATCH;
    warn.severity = Issue::Severity::WARN;
    warn.port = src;
    warn.connection = conn;
    warn.detail = "sign mismatch";
    data.active.push_back(warn);

    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("orange"));
}

TEST_CASE("DotReport: multiple connections between same instances are grouped") {
    auto data = makeMultiConnectionData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    // Both o_data and o_addr should appear in the cpu->bus edge label
    CHECK_THAT(dot, ContainsSubstring("o_data"));
    CHECK_THAT(dot, ContainsSubstring("o_addr"));

    // Verify grouping: count edges from u_cpu -> u_bus (should be exactly 1)
    size_t edgeCount = 0;
    size_t pos = 0;
    std::string edgePattern = "u_cpu -> u_bus";
    while ((pos = dot.find(edgePattern, pos)) != std::string::npos) {
        edgeCount++;
        pos += edgePattern.size();
    }
    CHECK(edgeCount == 1);
}

TEST_CASE("DotReport: special characters in module name are escaped") {
    ReportData data;
    data.topModule = "my\"mod\\ule";

    PortInfo src;
    src.instancePath = "my\"mod\\ule.u_a"; src.portName = "o_x";
    src.direction = ArgumentDirection::Out; src.width = 1; src.isSigned = false;

    PortInfo dst;
    dst.instancePath = "my\"mod\\ule.u_b"; dst.portName = "i_x";
    dst.direction = ArgumentDirection::In; dst.width = 1; dst.isSigned = false;

    data.graph.connections.push_back({src, dst});

    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    // The raw unescaped quote should not appear in the digraph name
    // DOT escaping: \" must appear, but bare " must not break the syntax
    // Count unescaped quotes: after the "digraph" keyword, all quotes in the
    // label should be preceded by backslash
    CHECK_THAT(dot, ContainsSubstring("digraph"));
    // The escaped version should be present
    CHECK_THAT(dot, ContainsSubstring("\\\""));
    CHECK_THAT(dot, ContainsSubstring("\\\\"));
}

TEST_CASE("DotReport: self-connection same instance") {
    ReportData data;
    data.topModule = "top";

    PortInfo src;
    src.instancePath = "top.u_loop"; src.portName = "o_fb";
    src.direction = ArgumentDirection::Out; src.width = 4; src.isSigned = false;

    PortInfo dst;
    dst.instancePath = "top.u_loop"; dst.portName = "i_fb";
    dst.direction = ArgumentDirection::In; dst.width = 4; dst.isSigned = false;

    data.graph.connections.push_back({src, dst});

    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    // Should still produce valid DOT (self-edge)
    CHECK_THAT(dot, ContainsSubstring("digraph"));
    CHECK_THAT(dot, ContainsSubstring("u_loop"));
    // Self-edge: u_loop -> u_loop
    CHECK_THAT(dot, ContainsSubstring("u_loop -> u_loop"));
}

TEST_CASE("DotReport: single module with ports but no connections") {
    ReportData data;
    data.topModule = "top";

    // allPorts has entries but connections is empty
    PortInfo p;
    p.instancePath = "top.u_lonely"; p.portName = "i_data";
    p.direction = ArgumentDirection::In; p.width = 8; p.isSigned = false;
    data.graph.allPorts.push_back(p);
    // No connections added

    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("digraph"));
    // No edges should be present (no "->")
    CHECK(dot.find("->") == std::string::npos);
}

TEST_CASE("DotReport: empty graph produces minimal valid DOT") {
    ReportData data;
    data.topModule = "empty_top";
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("digraph"));
    CHECK_THAT(dot, ContainsSubstring("empty_top"));
}
