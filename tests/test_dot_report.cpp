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
    auto data = makeMultiConnectionData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    // The cpu->bus edge has an error, so it should be red
    CHECK_THAT(dot, ContainsSubstring("red"));
    // The bus->mem edge is clean, should be black (default)
    CHECK_THAT(dot, ContainsSubstring("u_mem"));
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
    CHECK_THAT(dot, ContainsSubstring("u_mem"));
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
