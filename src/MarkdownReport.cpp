#include "MarkdownReport.h"
#include <fmt/format.h>

namespace connect {

void MarkdownReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    int totalConnections = static_cast<int>(data.graph.connections.size());

    out << fmt::format("# Connection Report: {}\n\n", data.topModule);

    // Summary table
    out << "## Summary\n\n";
    out << "| Metric | Count |\n";
    out << "|--------|-------|\n";
    out << fmt::format("| Connections analyzed | {} |\n", totalConnections);
    out << fmt::format("| Errors | {} |\n", data.errorCount());
    out << fmt::format("| Warnings | {} |\n", data.warnCount());
    out << fmt::format("| Info | {} |\n", data.infoCount());
    out << fmt::format("| Waived | {} |\n", static_cast<int>(data.waived.size()));
    out << "\n";

    // Issues table
    if (!data.active.empty()) {
        out << "## Issues\n\n";
        out << "| Severity | Type | Port | Detail |\n";
        out << "|----------|------|------|--------|\n";
        for (const auto& issue : data.active) {
            out << fmt::format("| {} | {} | {} | {} |\n",
                Issue::severityToString(issue.severity),
                Issue::typeToString(issue.type),
                issue.port.fullPath(),
                issue.detail);
        }
        out << "\n";
    }

    // Waived issues
    if (!data.waived.empty()) {
        out << fmt::format("## Waived Issues: {}\n\n", static_cast<int>(data.waived.size()));
    }
}

} // namespace connect
