#include "TraceEngine.h"
#include "GlobUtil.h"

#include <fmt/core.h>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace connect {

TraceEngine::TraceEngine(const ConnectionGraph& graph) : graph_(graph) {
    // Build adjacency indices for O(degree) BFS
    for (const auto& conn : graph_.connections) {
        fwdIndex_[conn.source.instancePath].push_back(&conn);
        revIndex_[conn.dest.instancePath].push_back(&conn);
    }
}

std::vector<TraceHop> TraceEngine::traceFanOut(const std::string& portPattern,
                                                int maxDepth) const {
    std::vector<TraceHop> result;
    std::unordered_set<std::string> visited;
    std::queue<std::pair<std::string, int>> frontier;

    // Seed: find all connections where source.fullPath() matches the pattern
    for (const auto& conn : graph_.connections) {
        if (globMatch(portPattern, conn.source.fullPath())) {
            std::string key = conn.source.fullPath() + "->" + conn.dest.fullPath();
            if (visited.insert(key).second) {
                result.push_back({conn, 0});
                frontier.push({conn.dest.instancePath, 1});
            }
        }
    }

    // BFS expansion using forward index
    while (!frontier.empty()) {
        auto [instancePath, depth] = frontier.front();
        frontier.pop();
        if (depth > maxDepth) continue;

        auto it = fwdIndex_.find(instancePath);
        if (it == fwdIndex_.end()) continue;
        for (const auto* conn : it->second) {
            std::string key = conn->source.fullPath() + "->" + conn->dest.fullPath();
            if (visited.insert(key).second) {
                result.push_back({*conn, depth});
                frontier.push({conn->dest.instancePath, depth + 1});
            }
        }
    }

    return result;
}

std::vector<TraceHop> TraceEngine::traceFanIn(const std::string& portPattern,
                                               int maxDepth) const {
    std::vector<TraceHop> result;
    std::unordered_set<std::string> visited;
    std::queue<std::pair<std::string, int>> frontier;

    // Seed: find all connections where dest.fullPath() matches the pattern
    for (const auto& conn : graph_.connections) {
        if (globMatch(portPattern, conn.dest.fullPath())) {
            std::string key = conn.source.fullPath() + "->" + conn.dest.fullPath();
            if (visited.insert(key).second) {
                result.push_back({conn, 0});
                frontier.push({conn.source.instancePath, 1});
            }
        }
    }

    // BFS expansion using reverse index
    while (!frontier.empty()) {
        auto [instancePath, depth] = frontier.front();
        frontier.pop();
        if (depth > maxDepth) continue;

        auto it = revIndex_.find(instancePath);
        if (it == revIndex_.end()) continue;
        for (const auto* conn : it->second) {
            std::string key = conn->source.fullPath() + "->" + conn->dest.fullPath();
            if (visited.insert(key).second) {
                result.push_back({*conn, depth});
                frontier.push({conn->source.instancePath, depth + 1});
            }
        }
    }

    return result;
}

std::string TraceEngine::formatTrace(const std::vector<TraceHop>& hops,
                                      const std::string& pattern,
                                      bool fanOut) {
    std::ostringstream os;

    if (fanOut) {
        os << "=== Fan-Out Trace: " << pattern << " ===\n";
    } else {
        os << "=== Fan-In Trace: " << pattern << " ===\n";
    }

    if (hops.empty()) {
        os << "  (no connections found)\n";
        return os.str();
    }

    for (const auto& hop : hops) {
        // Indentation: 2 spaces per depth level, plus base 2 spaces
        std::string indent(2 + static_cast<size_t>(hop.depth) * 2, ' ');

        const auto& src = hop.connection.source;
        const auto& dst = hop.connection.dest;

        if (fanOut) {
            os << indent
               << src.fullPath() << " [" << src.width << "b]"
               << " -> "
               << dst.fullPath() << " [" << dst.width << "b]";
        } else {
            os << indent
               << dst.fullPath() << " [" << dst.width << "b]"
               << " <- "
               << src.fullPath() << " [" << src.width << "b]";
        }

        // Width mismatch warning
        if (hop.connection.kind != ConnectionKind::Approximate &&
            src.width != dst.width) {
            os << "  \u26a0 " << src.width << "b\u2192" << dst.width << "b";
        }

        os << "\n";
    }

    return os.str();
}

} // namespace connect
