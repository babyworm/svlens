#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "HtmlReport.h"
#include "AnalysisEngine.h"
#include <sstream>

using namespace connect;
using namespace Catch::Matchers;
using slang::ast::ArgumentDirection;

static ReportData makeHtmlTestData() {
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

TEST_CASE("HtmlReport: produces valid HTML document") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("<!DOCTYPE html>"));
    CHECK_THAT(html, ContainsSubstring("</html>"));
}

TEST_CASE("HtmlReport: embeds JSON data with module and issues") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("\"top\":"));
    CHECK_THAT(html, ContainsSubstring("soc_top"));
    CHECK_THAT(html, ContainsSubstring("WIDTH_MISMATCH"));
}

TEST_CASE("HtmlReport: includes vis-network script reference") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("vis-network"));
}

TEST_CASE("HtmlReport: empty graph produces valid HTML") {
    ReportData data;
    data.topModule = "empty_top";
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("<!DOCTYPE html>"));
    CHECK_THAT(html, ContainsSubstring("</html>"));
    CHECK_THAT(html, ContainsSubstring("empty_top"));
}

TEST_CASE("HtmlReport: analysis data is embedded when present") {
    auto data = makeHtmlTestData();

    // Run the AnalysisEngine to populate the analysis field
    AnalysisEngine analysisEngine;
    data.analysis = analysisEngine.analyze(data);

    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    // The JSON should contain analysis data
    CHECK_THAT(html, ContainsSubstring("overall_score"));
    CHECK_THAT(html, ContainsSubstring("module_health"));
}

TEST_CASE("HtmlReport: no unreplaced placeholders") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    // All {{...}} placeholders should have been replaced
    CHECK(html.find("{{") == std::string::npos);
}

TEST_CASE("HtmlReport: special characters in module name are escaped") {
    ReportData data;
    data.topModule = "mod<>&\"test";
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("<!DOCTYPE html>"));
    CHECK_THAT(html, ContainsSubstring("</html>"));

    // The HTML-escaped module name should appear in the title/header
    // Raw < > & " should NOT appear unescaped in the TOP_MODULE placeholder spots
    CHECK_THAT(html, ContainsSubstring("&lt;"));
    CHECK_THAT(html, ContainsSubstring("&gt;"));
    CHECK_THAT(html, ContainsSubstring("&amp;"));
    CHECK_THAT(html, ContainsSubstring("&quot;"));
}

TEST_CASE("HtmlReport: contains severity color styling") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    // Dark theme background
    CHECK_THAT(html, ContainsSubstring("#1a1a2e"));
    // Severity colors for issue sidebar
    CHECK_THAT(html, ContainsSubstring("ERROR"));
}
