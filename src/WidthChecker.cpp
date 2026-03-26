#include "WidthChecker.h"
#include <fmt/core.h>

namespace connect {
std::vector<Issue> WidthChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;
    for (auto& conn : graph.connections) {
        if (conn.source.width == conn.dest.width) continue;
        Issue issue;
        issue.type = Issue::Type::WIDTH_MISMATCH;
        issue.connection = conn;
        issue.port = conn.source;
        if (conn.source.width > conn.dest.width) {
            issue.severity = Issue::Severity::ERROR;
            issue.detail = fmt::format("Truncation: {} bits → {} bits, bits [{}:{}] lost",
                conn.source.width, conn.dest.width, conn.source.width - 1, conn.dest.width);
        } else if (conn.source.isSigned && conn.dest.isSigned) {
            issue.severity = Issue::Severity::INFO;
            issue.detail = fmt::format("Sign-extension: {} bits → {} bits (likely intentional)",
                conn.source.width, conn.dest.width);
        } else {
            issue.severity = Issue::Severity::WARN;
            issue.detail = fmt::format("Zero-extension: {} bits → {} bits",
                conn.source.width, conn.dest.width);
        }
        issues.push_back(std::move(issue));
    }
    return issues;
}
} // namespace connect
