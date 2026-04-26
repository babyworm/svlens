#include "UndrivenChecker.h"
#include <fmt/core.h>
#include <unordered_set>

namespace connect {

namespace {

bool isSuppressedOptionalInput(const ConnectionGraph& graph, const PortInfo& port) {
    if (port.portName != "exists_data_i" && port.portName != "exists_mask_i")
        return false;

    const auto reqPath = port.instancePath + ".exists_req_i";
    return graph.constantZeroTieOffPorts.contains(reqPath);
}

} // namespace

std::vector<Issue> UndrivenChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;
    std::unordered_set<std::string> drivenDests;
    for (auto& conn : graph.connections)
        drivenDests.insert(conn.dest.fullPath());

    for (auto& port : graph.allPorts) {
        if (port.direction != slang::ast::ArgumentDirection::In &&
            port.direction != slang::ast::ArgumentDirection::InOut)
            continue;
        // Round 37: top-module inputs are driven externally
        // (testbench/parent), so we cannot detect undriven here.
        if (port.instancePath == graph.topModule) continue;
        if (drivenDests.contains(port.fullPath())) continue;
        // Port connected to a local wire (not another instance) is not undriven
        if (graph.connectedPorts.contains(port.fullPath())) continue;
        if (isSuppressedOptionalInput(graph, port)) continue;
        Issue issue;
        issue.type = Issue::Type::UNDRIVEN_INPUT;
        issue.severity = Issue::Severity::ERROR;
        issue.port = port;
        if (port.width <= 1)
            issue.detail = fmt::format("{} — no driver (will propagate X)", port.fullPath());
        else
            issue.detail = fmt::format("{}[{}:0] — no driver (will propagate X)", port.fullPath(), port.width - 1);
        issues.push_back(std::move(issue));
    }
    return issues;
}
} // namespace connect
