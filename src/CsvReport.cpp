#include "CsvReport.h"
#include <fmt/format.h>

namespace connect {

void CsvReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    out << "Source,Dest,Width_Src,Width_Dst,Type_Src,Type_Dst,Status\n";

    for (const auto& conn : data.graph.connections) {
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

        std::string srcType = conn.source.isSigned ? "signed" : "unsigned";
        std::string dstType = conn.dest.isSigned ? "signed" : "unsigned";

        out << fmt::format("{},{},{},{},{},{},{}\n",
            conn.source.fullPath(),
            conn.dest.fullPath(),
            conn.source.width,
            conn.dest.width,
            srcType,
            dstType,
            status);
    }
}

} // namespace connect
