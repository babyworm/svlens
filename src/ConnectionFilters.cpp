#include "ConnectionFilters.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace connect {

namespace {

std::string toLower(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool hasTokenBoundary(const std::string& str, size_t pos, size_t len) {
    auto isBoundary = [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); };

    const bool atStart = (pos == 0) || isBoundary(str[pos - 1]);
    const bool atEnd = (pos + len >= str.size()) || isBoundary(str[pos + len]);
    return atStart && atEnd;
}

bool containsDelimitedToken(const std::string& str, std::string_view token) {
    size_t pos = 0;
    while ((pos = str.find(token, pos)) != std::string::npos) {
        if (hasTokenBoundary(str, pos, token.size()))
            return true;
        ++pos;
    }
    return false;
}

} // namespace

bool isLikelyNoConnectPort(std::string_view portName) {
    const std::string lower = toLower(portName);
    return containsDelimitedToken(lower, "nc") ||
           containsDelimitedToken(lower, "unused") ||
           containsDelimitedToken(lower, "unconn") ||
           containsDelimitedToken(lower, "unconnected") ||
           lower == "no_connect";
}

ConnectionGraph applyGraphFilters(const ConnectionGraph& graph,
                                  const GraphFilterOptions& options) {
    if (!options.ignoreTieOff && !options.ignoreNc)
        return graph;

    ConnectionGraph filtered = graph;
    std::unordered_set<std::string> ignoredPorts;

    if (options.ignoreTieOff) {
        ignoredPorts.insert(graph.tieOffPorts.begin(), graph.tieOffPorts.end());
    }

    if (options.ignoreNc) {
        for (const auto& port : graph.allPorts) {
            if (isLikelyNoConnectPort(port.portName))
                ignoredPorts.insert(port.fullPath());
        }
    }

    if (ignoredPorts.empty())
        return filtered;

    auto keepPort = [&](const PortInfo& port) {
        return !ignoredPorts.contains(port.fullPath());
    };
    auto keepConnection = [&](const Connection& conn) {
        return !ignoredPorts.contains(conn.source.fullPath()) &&
               !ignoredPorts.contains(conn.dest.fullPath());
    };

    filtered.allPorts.erase(std::remove_if(filtered.allPorts.begin(),
                                           filtered.allPorts.end(),
                                           [&](const PortInfo& port) { return !keepPort(port); }),
                            filtered.allPorts.end());

    filtered.connections.erase(std::remove_if(filtered.connections.begin(),
                                              filtered.connections.end(),
                                              [&](const Connection& conn) {
                                                  return !keepConnection(conn);
                                              }),
                               filtered.connections.end());

    for (auto it = filtered.connectedPorts.begin(); it != filtered.connectedPorts.end();) {
        if (ignoredPorts.contains(*it))
            it = filtered.connectedPorts.erase(it);
        else
            ++it;
    }

    for (auto it = filtered.tieOffPorts.begin(); it != filtered.tieOffPorts.end();) {
        if (ignoredPorts.contains(*it))
            it = filtered.tieOffPorts.erase(it);
        else
            ++it;
    }

    return filtered;
}

} // namespace connect
