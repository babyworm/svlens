#pragma once

#include "ConnectionGraph.h"

#include <map>
#include <string>
#include <vector>

namespace connect {

struct ClockResetTopology {
    // clock port name -> list of instance paths that receive it
    std::map<std::string, std::vector<std::string>> clockGroups;
    // reset port name -> list of instance paths that receive it
    std::map<std::string, std::vector<std::string>> resetGroups;
    // Warnings: instances with clock but no reset
    std::vector<std::string> warnings;
};

class ClockResetAnalyzer {
public:
    /// Analyze the connection graph and produce clock/reset topology.
    /// Skips ports belonging to the top module itself.
    ClockResetTopology analyze(const ConnectionGraph& graph) const;

    /// Classify whether a port name looks like a clock (case-insensitive).
    static bool isClockPort(const std::string& portName);

    /// Classify whether a port name looks like a reset (case-insensitive).
    static bool isResetPort(const std::string& portName);
};

} // namespace connect
