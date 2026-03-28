#pragma once

#include "Issue.h"

#include <string>
#include <vector>

namespace connect {

struct ModuleHealth {
    std::string instancePath; // full path, e.g. "soc_top.u_cpu"
    std::string shortName;    // leaf name, e.g. "u_cpu"
    double score = 1.0;       // 0.0 .. 1.0, lower is worse
    int errorCount = 0;
    int warnCount = 0;
    int infoCount = 0;
    int totalPorts = 0;
    int connectedPorts = 0;
};

struct CouplingEdge {
    std::string srcModule; // short name
    std::string dstModule; // short name
    int connectionCount = 0;
};

struct RiskItem {
    enum class Level { HIGH, MEDIUM, LOW };

    Issue issue;
    Level level = Level::LOW;
    std::string reason;

    static const char* levelToString(Level l) {
        switch (l) {
            case Level::HIGH:   return "HIGH";
            case Level::MEDIUM: return "MEDIUM";
            case Level::LOW:    return "LOW";
        }
        return "UNKNOWN";
    }
};

struct AnalysisResult {
    double overallScore = 1.0;
    std::vector<ModuleHealth> moduleHealth;  // sorted by score ascending (worst first)
    std::vector<CouplingEdge> coupling;      // sorted by connectionCount descending
    std::vector<RiskItem> risks;             // sorted by level: HIGH, MEDIUM, LOW
    int totalPorts = 0;
    int totalConnections = 0;
    int totalIssues = 0;
};

} // namespace connect
