#include "ClockResetAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

namespace connect {

static std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool ClockResetAnalyzer::isClockPort(const std::string& portName) {
    std::string lower = toLower(portName);
    // Exact matches
    if (lower == "clk" || lower == "clock")
        return true;
    // Contains _clk, clk_, _clock, clock_ as substring
    if (lower.find("_clk") != std::string::npos ||
        lower.find("clk_") != std::string::npos ||
        lower.find("_clock") != std::string::npos ||
        lower.find("clock_") != std::string::npos)
        return true;
    return false;
}

bool ClockResetAnalyzer::isResetPort(const std::string& portName) {
    std::string lower = toLower(portName);
    // Exact matches
    if (lower == "rst" || lower == "rst_n" || lower == "reset" || lower == "reset_n")
        return true;
    // Contains _rst, rst_, _reset, reset_ as substring
    if (lower.find("_rst") != std::string::npos ||
        lower.find("rst_") != std::string::npos ||
        lower.find("_reset") != std::string::npos ||
        lower.find("reset_") != std::string::npos)
        return true;
    return false;
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

        if (isClockPort(port.portName)) {
            topology.clockGroups[port.portName].push_back(port.instancePath);
            instancesWithClock.insert(port.instancePath);
        } else if (isResetPort(port.portName)) {
            topology.resetGroups[port.portName].push_back(port.instancePath);
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
