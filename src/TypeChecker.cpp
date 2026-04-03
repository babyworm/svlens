#include "TypeChecker.h"
#include <fmt/core.h>

namespace connect {
std::vector<Issue> TypeChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;
    for (auto& conn : graph.connections) {
        if (conn.kind == ConnectionKind::Approximate)
            continue;
        if (conn.source.isSigned == conn.dest.isSigned) continue;
        Issue issue;
        issue.type = Issue::Type::TYPE_MISMATCH;
        issue.severity = Issue::Severity::ERROR;
        issue.connection = conn;
        issue.port = conn.source;
        issue.detail = fmt::format("{} ({}) → {} ({}): negative values interpreted as large positive",
            conn.source.fullPath(), conn.source.isSigned ? "signed" : "unsigned",
            conn.dest.fullPath(), conn.dest.isSigned ? "signed" : "unsigned");
        issues.push_back(std::move(issue));
    }
    return issues;
}
} // namespace connect
