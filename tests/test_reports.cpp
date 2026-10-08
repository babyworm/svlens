#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "JsonReport.h"
#include "MarkdownReport.h"
#include "CsvReport.h"
#include "TableReport.h"
#include <sstream>

using namespace connect;
using namespace Catch::Matchers;
using slang::ast::ArgumentDirection;

static ReportData makeTestData() {
    ReportData data;
    data.topModule = "soc_top";

    PortInfo src;
    src.instancePath = "top.u_core"; src.portName = "o_data";
    src.direction = ArgumentDirection::Out; src.width = 32; src.isSigned = false;

    PortInfo dst;
    dst.instancePath = "top.u_bus"; dst.portName = "i_data";
    dst.direction = ArgumentDirection::In; dst.width = 16; dst.isSigned = false;

    Connection conn{src, dst};
    data.graph.connections.push_back(conn);

    Issue issue;
    issue.type = Issue::Type::WIDTH_MISMATCH;
    issue.severity = Issue::Severity::ERROR;
    issue.port = src;
    issue.connection = conn;
    issue.detail = "Truncation: 32 bits -> 16 bits, bits [31:16] lost";
    data.active.push_back(issue);
    return data;
}

TEST_CASE("JsonReport: contains required fields") {
    auto data = makeTestData();
    std::ostringstream out;
    JsonReportGenerator gen;
    gen.generate(data, out);
    auto json = out.str();
    CHECK_THAT(json, ContainsSubstring("\"top\""));
    CHECK_THAT(json, ContainsSubstring("soc_top"));
    CHECK_THAT(json, ContainsSubstring("\"errors\""));
    CHECK_THAT(json, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(json, ContainsSubstring("\"issues\""));
    CHECK_THAT(json, ContainsSubstring("\"kind\": \"direct\""));
}

TEST_CASE("JsonReport: direct slice connections expose ordinal bit ranges", "[report][slice]") {
    auto data = makeTestData();
    data.active.clear();
    data.graph.connections[0].sourceBits = BitRange{4, 7};
    data.graph.connections[0].destBits = BitRange{0, 3};
    std::ostringstream out;
    JsonReportGenerator{}.generate(data, out);
    const auto json = out.str();
    CHECK_THAT(json, ContainsSubstring("\"source_bits\": {\"low\": 4, \"high\": 7}"));
    CHECK_THAT(json, ContainsSubstring("\"dest_bits\": {\"low\": 0, \"high\": 3}"));
}

TEST_CASE("JsonReport: approximate connection is labeled explicitly", "[report][approximate]") {
    auto data = makeTestData();
    data.active.clear();
    data.graph.connections[0].kind = ConnectionKind::Approximate;
    std::ostringstream out;
    JsonReportGenerator{}.generate(data, out);
    CHECK_THAT(out.str(), ContainsSubstring("\"kind\": \"approximate\""));
}

TEST_CASE("JsonReport: bit-flow gaps expose count and bounded examples", "[report][gap]") {
    auto data = makeTestData();
    data.graph.bitFlowGapCount = 2;
    data.graph.bitFlowGapReasons["width_limit"] = 2;
    data.graph.bitFlowGaps.push_back({"top", "width_limit", {}, 4097, 4097});
    std::ostringstream out;
    JsonReportGenerator{}.generate(data, out);
    const auto json = out.str();
    CHECK_THAT(json, ContainsSubstring("\"bit_flow_gap_count\": 2"));
    CHECK_THAT(json, ContainsSubstring("\"bit_flow_gap_reasons\": {\"width_limit\": 2}"));
    CHECK_THAT(json, ContainsSubstring("\"bit_flow_gaps\": ["));
    CHECK_THAT(json, ContainsSubstring("\"reason\": \"width_limit\""));
    CHECK_THAT(json, ContainsSubstring("\"scope\": \"top\""));
    CHECK_THAT(json, ContainsSubstring("\"lhs_width\": 4097"));
}

TEST_CASE("MarkdownReport: contains summary and issues") {
    auto data = makeTestData();
    data.graph.bitFlowGapCount = 1;
    std::ostringstream out;
    MarkdownReportGenerator gen;
    gen.generate(data, out);
    auto md = out.str();
    CHECK_THAT(md, ContainsSubstring("soc_top"));
    CHECK_THAT(md, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(md, ContainsSubstring("ERROR"));
    CHECK_THAT(md, ContainsSubstring("| Bit-flow gaps | 1 |"));
}

TEST_CASE("CsvReport: has header and data rows") {
    auto data = makeTestData();
    std::ostringstream out;
    CsvReportGenerator gen;
    gen.generate(data, out);
    auto csv = out.str();
    CHECK_THAT(csv, ContainsSubstring("Source,Dest,Width_Src,Width_Dst"));
    CHECK_THAT(csv, ContainsSubstring("u_core.o_data"));
    CHECK_THAT(csv, ContainsSubstring("WIDTH_MISMATCH"));
}

TEST_CASE("TableReport: formats output for terminal") {
    auto data = makeTestData();
    data.graph.bitFlowGapCount = 1;
    std::ostringstream out;
    TableReportGenerator gen;
    gen.generate(data, out);
    auto table = out.str();
    CHECK_THAT(table, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(table, ContainsSubstring("ERROR"));
    CHECK_THAT(table, ContainsSubstring("Bit-flow gaps: 1"));
}
