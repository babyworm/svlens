#include "JsonReport.h"
#include <fmt/format.h>
#include <string>

namespace connect {

namespace {

std::string escapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '<':  result += "\\u003c"; break;
            default:
                if (c < 0x20)
                    result += fmt::format("\\u{:04x}", c);
                else
                    result += static_cast<char>(c);
                break;
        }
    }
    return result;
}

std::string portRange(const PortInfo& p) {
    if (p.width <= 1) return p.fullPath();
    return fmt::format("{}[{}:0]", p.fullPath(), p.width - 1);
}

} // anonymous namespace

void JsonReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    int totalConnections = static_cast<int>(data.graph.connections.size());

    out << "{\n";
    out << fmt::format("  \"version\": \"1.0\",\n");
    out << fmt::format("  \"top\": \"{}\",\n", escapeJson(data.topModule));

    // Summary
    out << "  \"summary\": {\n";
    out << fmt::format("    \"connections_analyzed\": {},\n", totalConnections);
    out << fmt::format("    \"errors\": {},\n", data.errorCount());
    out << fmt::format("    \"warnings\": {},\n", data.warnCount());
    out << fmt::format("    \"info\": {},\n", data.infoCount());
    out << fmt::format("    \"waived\": {}\n", static_cast<int>(data.waived.size()));
    out << "  },\n";

    // Issues
    out << "  \"issues\": [\n";
    for (size_t i = 0; i < data.active.size(); ++i) {
        const auto& issue = data.active[i];
        out << "    {\n";
        out << fmt::format("      \"type\": \"{}\",\n", Issue::typeToString(issue.type));
        out << fmt::format("      \"severity\": \"{}\",\n", Issue::severityToString(issue.severity));
        out << fmt::format("      \"port\": \"{}\",\n", escapeJson(issue.port.fullPath()));

        if (issue.connection.has_value()) {
            const auto& conn = issue.connection.value();
            out << "      \"source\": {\n";
            out << fmt::format("        \"instance\": \"{}\",\n", escapeJson(conn.source.instancePath));
            out << fmt::format("        \"port\": \"{}\",\n", escapeJson(conn.source.portName));
            out << fmt::format("        \"width\": {}\n", conn.source.width);
            out << "      },\n";
            out << "      \"dest\": {\n";
            out << fmt::format("        \"instance\": \"{}\",\n", escapeJson(conn.dest.instancePath));
            out << fmt::format("        \"port\": \"{}\",\n", escapeJson(conn.dest.portName));
            out << fmt::format("        \"width\": {}\n", conn.dest.width);
            out << "      },\n";
        }

        out << fmt::format("      \"detail\": \"{}\"\n", escapeJson(issue.detail));
        out << "    }";
        if (i + 1 < data.active.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // Analysis (optional)
    if (data.analysis.has_value()) {
        const auto& analysis = data.analysis.value();
        out << "  \"analysis\": {\n";
        out << fmt::format("    \"overall_score\": {:.2f},\n", analysis.overallScore);
        out << fmt::format("    \"total_ports\": {},\n", analysis.totalPorts);
        out << fmt::format("    \"total_connections\": {},\n", analysis.totalConnections);
        out << fmt::format("    \"total_issues\": {},\n", analysis.totalIssues);

        // Module health
        out << "    \"module_health\": [\n";
        for (size_t i = 0; i < analysis.moduleHealth.size(); ++i) {
            const auto& m = analysis.moduleHealth[i];
            out << "      {\n";
            out << fmt::format("        \"instance\": \"{}\",\n", escapeJson(m.instancePath));
            out << fmt::format("        \"name\": \"{}\",\n", escapeJson(m.shortName));
            out << fmt::format("        \"total_ports\": {},\n", m.totalPorts);
            out << fmt::format("        \"connected\": {},\n", m.connectedPorts);
            out << fmt::format("        \"errors\": {},\n", m.errorCount);
            out << fmt::format("        \"warnings\": {},\n", m.warnCount);
            out << fmt::format("        \"score\": {:.2f}\n", m.score);
            out << "      }";
            if (i + 1 < analysis.moduleHealth.size()) out << ",";
            out << "\n";
        }
        out << "    ],\n";

        // Coupling
        out << "    \"coupling\": [\n";
        for (size_t i = 0; i < analysis.coupling.size(); ++i) {
            const auto& c = analysis.coupling[i];
            out << "      {\n";
            out << fmt::format("        \"source\": \"{}\",\n", escapeJson(c.srcModule));
            out << fmt::format("        \"dest\": \"{}\",\n", escapeJson(c.dstModule));
            out << fmt::format("        \"connections\": {}\n", c.connectionCount);
            out << "      }";
            if (i + 1 < analysis.coupling.size()) out << ",";
            out << "\n";
        }
        out << "    ],\n";

        // Risks
        out << "    \"risks\": [\n";
        for (size_t i = 0; i < analysis.risks.size(); ++i) {
            const auto& r = analysis.risks[i];
            out << "      {\n";
            out << fmt::format("        \"level\": \"{}\",\n", RiskItem::levelToString(r.level));
            out << fmt::format("        \"type\": \"{}\",\n", Issue::typeToString(r.issue.type));
            out << fmt::format("        \"port\": \"{}\",\n", escapeJson(r.issue.port.fullPath()));
            out << fmt::format("        \"reason\": \"{}\"\n", escapeJson(r.reason));
            out << "      }";
            if (i + 1 < analysis.risks.size()) out << ",";
            out << "\n";
        }
        out << "    ]\n";

        out << "  },\n";
    }

    // Connections
    out << "  \"connections\": [\n";
    for (size_t i = 0; i < data.graph.connections.size(); ++i) {
        const auto& conn = data.graph.connections[i];

        // Find status for this connection
        std::string status = "OK";
        for (const auto& issue : data.active) {
            if (issue.connection.has_value()) {
                const auto& ic = issue.connection.value();
                if (ic.source.fullPath() == conn.source.fullPath() &&
                    ic.dest.fullPath() == conn.dest.fullPath()) {
                    status = Issue::typeToString(issue.type);
                    break;
                }
            }
        }

        out << "    {\n";
        out << fmt::format("      \"source\": \"{}\",\n", escapeJson(portRange(conn.source)));
        out << fmt::format("      \"dest\": \"{}\",\n", escapeJson(portRange(conn.dest)));
        out << fmt::format("      \"status\": \"{}\"\n", escapeJson(status));
        out << "    }";
        if (i + 1 < data.graph.connections.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

} // namespace connect
