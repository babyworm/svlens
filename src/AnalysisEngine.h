#pragma once

#include "Issue.h"
#include "ReportGenerator.h"

#include <algorithm>
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

class AnalysisEngine {
public:
    /// Run full analysis on a completed report data set.
    AnalysisResult analyze(const ReportData& data) const;

private:
    /// Extract the short (leaf) module name from a full instance path.
    static std::string shortName(const std::string& instancePath);

    /// Compute per-module health scores.
    std::vector<ModuleHealth> computeModuleHealth(const ReportData& data) const;

    /// Compute module-to-module coupling matrix.
    std::vector<CouplingEdge> computeCoupling(const ReportData& data) const;

    /// Classify each issue into a risk level.
    std::vector<RiskItem> classifyRisks(const std::vector<Issue>& issues) const;

    /// Scoring penalties per severity.
    static constexpr double kErrorPenalty = 0.15;
    static constexpr double kWarnPenalty  = 0.05;
    static constexpr double kInfoPenalty  = 0.0;
};

} // namespace connect
