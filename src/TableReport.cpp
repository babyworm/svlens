#include "TableReport.h"
#include <fmt/format.h>

namespace connect {

void TableReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    int totalConnections = static_cast<int>(data.graph.connections.size());

    out << fmt::format("=== slang-connect: {} ===\n\n", data.topModule);

    out << fmt::format("Connections: {}  Errors: {}  Warnings: {}  Info: {}  Waived: {}\n\n",
        totalConnections,
        data.errorCount(),
        data.warnCount(),
        data.infoCount(),
        static_cast<int>(data.waived.size()));

    if (data.active.empty()) {
        out << "No issues found.\n";
        return;
    }

    out << fmt::format("{:<8} {:<18} {:<40} {}\n", "SEV", "TYPE", "PORT", "DETAIL");
    out << std::string(100, '-') << "\n";

    for (const auto& issue : data.active) {
        out << fmt::format("{:<8} {:<18} {:<40} {}\n",
            Issue::severityToString(issue.severity),
            Issue::typeToString(issue.type),
            issue.port.fullPath(),
            issue.detail);
    }
}

} // namespace connect
