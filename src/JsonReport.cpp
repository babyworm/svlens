#include "JsonReport.h"
#include <fmt/format.h>
#include <string>

namespace connect {

namespace {

std::string escapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
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
