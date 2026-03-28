#include "TraceEngine.h"

#include <fmt/core.h>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace connect {

TraceEngine::TraceEngine(const ConnectionGraph& graph) : graph_(graph) {}

bool TraceEngine::globMatch(const std::string& pattern, const std::string& text) {
    // Simple glob: '*' matches any sequence of characters (including dots)
    size_t pi = 0, ti = 0;
    size_t starP = std::string::npos, starT = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
            ++pi;
            ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starP = pi;
            starT = ti;
            ++pi;
        } else if (starP != std::string::npos) {
            pi = starP + 1;
            ++starT;
            ti = starT;
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }

    return pi == pattern.size();
}

std::vector<TraceHop> TraceEngine::traceFanOut(const std::string& portPattern,
                                                int maxDepth) const {
    std::vector<TraceHop> result;
    std::unordered_set<std::string> visited; // connection keys to prevent cycles

    // BFS queue: (instance path to expand from, current depth)
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

    // BFS expansion
    while (!frontier.empty()) {
        auto [instancePath, depth] = frontier.front();
        frontier.pop();

        if (depth > maxDepth)
            continue;

        for (const auto& conn : graph_.connections) {
            if (conn.source.instancePath == instancePath) {
                std::string key = conn.source.fullPath() + "->" + conn.dest.fullPath();
                if (visited.insert(key).second) {
                    result.push_back({conn, depth});
                    frontier.push({conn.dest.instancePath, depth + 1});
                }
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

    // BFS expansion: go backward
    while (!frontier.empty()) {
        auto [instancePath, depth] = frontier.front();
        frontier.pop();

        if (depth > maxDepth)
            continue;

        for (const auto& conn : graph_.connections) {
            if (conn.dest.instancePath == instancePath) {
                std::string key = conn.source.fullPath() + "->" + conn.dest.fullPath();
                if (visited.insert(key).second) {
                    result.push_back({conn, depth});
                    frontier.push({conn.source.instancePath, depth + 1});
                }
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
        if (src.width != dst.width) {
            os << "  \u26a0 " << src.width << "b\u2192" << dst.width << "b";
        }

        os << "\n";
    }

    return os.str();
}

} // namespace connect
