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
}

TEST_CASE("MarkdownReport: contains summary and issues") {
    auto data = makeTestData();
    std::ostringstream out;
    MarkdownReportGenerator gen;
    gen.generate(data, out);
    auto md = out.str();
    CHECK_THAT(md, ContainsSubstring("soc_top"));
    CHECK_THAT(md, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(md, ContainsSubstring("ERROR"));
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
    std::ostringstream out;
    TableReportGenerator gen;
    gen.generate(data, out);
    auto table = out.str();
    CHECK_THAT(table, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(table, ContainsSubstring("ERROR"));
}
