#pragma once

#include "AnalysisResult.h"
#include "ReportGenerator.h"

#include <string>
#include <vector>

namespace connect {

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
