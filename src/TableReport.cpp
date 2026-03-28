#include "TableReport.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace connect {

namespace {

/// Build a 12-character Unicode progress bar for a 0.0-1.0 score.
std::string healthBar(double score) {
    constexpr int barWidth = 12;
    int filled = static_cast<int>(std::round(score * barWidth));
    filled = std::max(0, std::min(barWidth, filled));
    std::string bar;
    for (int i = 0; i < filled; ++i) bar += "\xe2\x96\x88";       // U+2588 FULL BLOCK
    for (int i = filled; i < barWidth; ++i) bar += "\xe2\x96\x91"; // U+2591 LIGHT SHADE
    return bar;
}

/// Risk level symbol.
const char* riskSymbol(RiskItem::Level level) {
    switch (level) {
        case RiskItem::Level::HIGH:   return "\xe2\x96\xb2"; // U+25B2
        case RiskItem::Level::MEDIUM: return "\xe2\x97\x8f"; // U+25CF
        case RiskItem::Level::LOW:    return "\xe2\x97\x8b"; // U+25CB
    }
    return "?";
}

} // anonymous namespace

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
    } else {
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

    // --- Analysis sections (only when analysis has been run) ---
    if (!data.analysis.has_value())
        return;

    const auto& analysis = data.analysis.value();

    // --- Module Health ---
    if (!analysis.moduleHealth.empty()) {
        out << "\n=== Module Health ===\n";

        // Display sorted by score descending (best first) for readability
        auto sorted = analysis.moduleHealth;
        std::sort(sorted.begin(), sorted.end(), [](const ModuleHealth& a, const ModuleHealth& b) {
            return a.score > b.score;
        });

        for (const auto& m : sorted) {
            int pct = static_cast<int>(std::round(m.score * 100.0));
            std::string detail;
            if (m.errorCount > 0 && m.warnCount > 0)
                detail = fmt::format("{} ports, {} errors, {} warn", m.totalPorts, m.errorCount, m.warnCount);
            else if (m.errorCount > 0)
                detail = fmt::format("{} ports, {} errors", m.totalPorts, m.errorCount);
            else if (m.warnCount > 0)
                detail = fmt::format("{} ports, {} warn", m.totalPorts, m.warnCount);
            else
                detail = fmt::format("{} ports, 0 issues", m.totalPorts);

            out << fmt::format("  {:<16s} {}  {:>3d}%  ({})\n",
                               m.shortName, healthBar(m.score), pct, detail);
        }

        int overallPct = static_cast<int>(std::round(analysis.overallScore * 100.0));
        out << "  " << std::string(37, '\xe2' == '\xe2' ? '-' : '-') << "\n";
        out << fmt::format("  Overall: {}%\n", overallPct);
    }

    // --- Risk Assessment ---
    if (!analysis.risks.empty()) {
        out << "\n=== Risk Assessment ===\n";

        for (const auto& risk : analysis.risks) {
            out << fmt::format("  {} {:<8s} {}: {}\n",
                               riskSymbol(risk.level),
                               RiskItem::levelToString(risk.level),
                               risk.issue.port.fullPath(),
                               risk.reason);
        }
    }

    // --- Coupling Summary ---
    if (!analysis.coupling.empty()) {
        out << "\n=== Coupling Summary ===\n";

        constexpr size_t maxShow = 5;
        for (size_t i = 0; i < std::min(maxShow, analysis.coupling.size()); ++i) {
            const auto& edge = analysis.coupling[i];
            std::string label = (i == 0) ? "  (highest)" : "";
            out << fmt::format("  {} \xe2\x86\x94 {}    {} signals{}\n",
                               edge.srcModule, edge.dstModule,
                               edge.connectionCount, label);
        }
        if (analysis.coupling.size() > maxShow) {
            out << fmt::format("  ... and {} more pairs\n",
                               analysis.coupling.size() - maxShow);
        }
    }
}

} // namespace connect
