#include "DanglingChecker.h"
#include <fmt/core.h>
#include <unordered_set>

namespace connect {
std::vector<Issue> DanglingChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;
    std::unordered_set<std::string> connectedSources;
    for (auto& conn : graph.connections)
        connectedSources.insert(conn.source.fullPath());

    for (auto& port : graph.allPorts) {
        if (port.direction != slang::ast::ArgumentDirection::Out) continue;
        if (connectedSources.count(port.fullPath())) continue;
        Issue issue;
        issue.type = Issue::Type::DANGLING_OUTPUT;
        issue.severity = Issue::Severity::WARN;
        issue.port = port;
        if (port.width <= 1)
            issue.detail = fmt::format("{} — not connected", port.fullPath());
        else
            issue.detail = fmt::format("{}[{}:0] — not connected", port.fullPath(), port.width - 1);
        issues.push_back(std::move(issue));
    }
    return issues;
}
} // namespace connect
