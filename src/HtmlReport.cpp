#include "HtmlReport.h"
#include "JsonReport.h"
#include "html_template.h"
#include <sstream>
#include <string>

namespace connect {

namespace {

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

} // anonymous namespace

void HtmlReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    // Generate JSON using the existing JsonReportGenerator
    std::ostringstream jsonStream;
    JsonReportGenerator jsonGen;
    jsonGen.generate(data, jsonStream);

    std::string html(HTML_TEMPLATE);
    html = replaceAll(html, "{{TOP_MODULE}}", data.topModule);
    html = replaceAll(html, "{{JSON_DATA}}", jsonStream.str());

    out << html;
}

} // namespace connect
