#include "ClockResetAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace connect {

static std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Check if keyword appears as a whole word (bounded by _ or string boundaries).
// e.g. "rst" matches "sys_rst_n", "rst", "rst_n" but NOT "first", "burst".
static bool containsWord(const std::string& str, const std::string& keyword) {
    size_t pos = 0;
    while ((pos = str.find(keyword, pos)) != std::string::npos) {
        bool atStart = (pos == 0 || str[pos - 1] == '_');
        bool atEnd = (pos + keyword.size() >= str.size() ||
                      str[pos + keyword.size()] == '_');
        if (atStart && atEnd)
            return true;
        pos++;
    }
    return false;
}

static std::vector<const Connection*> upstreamConnections(const ConnectionGraph& graph,
                                                          const PortInfo& port) {
    std::vector<const Connection*> incoming;
    const auto fullPath = port.fullPath();
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() == fullPath)
            incoming.push_back(&conn);
    }
    return incoming;
}

bool ClockResetAnalyzer::isClockPort(const std::string& portName) {
    std::string lower = toLower(portName);
    return containsWord(lower, "clk") || containsWord(lower, "clock");
}

bool ClockResetAnalyzer::isResetPort(const std::string& portName) {
    std::string lower = toLower(portName);
    return containsWord(lower, "rst") || containsWord(lower, "reset");
}

ClockResetTopology ClockResetAnalyzer::analyze(const ConnectionGraph& graph) const {
    ClockResetTopology topology;

    // Track which instances have clock ports and which have reset ports
    std::set<std::string> instancesWithClock;
    std::set<std::string> instancesWithReset;

    for (const auto& port : graph.allPorts) {
        // Only classify input ports
        if (port.direction != slang::ast::ArgumentDirection::In)
            continue;

        // Skip ports belonging to the top module itself
        // Top module ports have instancePath == topModule
        if (port.instancePath == graph.topModule)
            continue;

        const auto incoming = upstreamConnections(graph, port);
        bool clockLike = isClockPort(port.portName);
        bool resetLike = isResetPort(port.portName);
        std::string semanticName = port.portName;

        if (!clockLike && !resetLike) {
            for (const auto* incomingConn : incoming) {
                if (isClockPort(incomingConn->source.portName)) {
                    clockLike = true;
                    semanticName = incomingConn->source.fullPath();
                    break;
                }
                if (isResetPort(incomingConn->source.portName)) {
                    resetLike = true;
                    semanticName = incomingConn->source.fullPath();
                    break;
                }
            }
        }

        if (clockLike && semanticName == port.portName && incoming.size() == 1 &&
            isClockPort(incoming.front()->source.portName)) {
            semanticName = incoming.front()->source.fullPath();
        }
        if (resetLike && semanticName == port.portName && incoming.size() == 1 &&
            isResetPort(incoming.front()->source.portName)) {
            semanticName = incoming.front()->source.fullPath();
        }

        if (clockLike) {
            topology.clockGroups[semanticName].push_back(port.instancePath);
            instancesWithClock.insert(port.instancePath);
        } else if (resetLike) {
            topology.resetGroups[semanticName].push_back(port.instancePath);
            instancesWithReset.insert(port.instancePath);
        }
    }

    // Warn on instances that have a clock but no reset
    for (const auto& inst : instancesWithClock) {
        if (instancesWithReset.find(inst) == instancesWithReset.end()) {
            topology.warnings.push_back(inst);
        }
    }

    return topology;
}

} // namespace connect
