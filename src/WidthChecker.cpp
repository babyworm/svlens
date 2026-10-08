#include "WidthChecker.h"
#include <fmt/core.h>

namespace connect {
std::vector<Issue> WidthChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;
    for (auto& conn : graph.connections) {
        if (conn.kind == ConnectionKind::Approximate)
            continue;
        const auto sourceWidth = conn.sourceBits ? conn.sourceBits->width() : conn.source.width;
        const auto destWidth = conn.destBits ? conn.destBits->width() : conn.dest.width;
        if (sourceWidth == destWidth)
            continue;
        Issue issue;
        issue.type = Issue::Type::WIDTH_MISMATCH;
        issue.connection = conn;
        issue.port = conn.source;
        if (sourceWidth > destWidth) {
            issue.severity = Issue::Severity::ERROR;
            issue.detail = fmt::format("Truncation: {} bits → {} bits, bits [{}:{}] lost", sourceWidth, destWidth,
                                       sourceWidth - 1, destWidth);
        } else if (conn.source.isSigned && conn.dest.isSigned) {
            issue.severity = Issue::Severity::INFO;
            issue.detail =
                fmt::format("Sign-extension: {} bits → {} bits (likely intentional)", sourceWidth, destWidth);
        } else {
            issue.severity = Issue::Severity::WARN;
            issue.detail = fmt::format("Zero-extension: {} bits → {} bits", sourceWidth, destWidth);
        }
        issues.push_back(std::move(issue));
    }
    return issues;
}
} // namespace connect
