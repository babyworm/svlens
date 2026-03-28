#include "AnalysisEngine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace connect {

std::string AnalysisEngine::shortName(const std::string& instancePath) {
    auto dot = instancePath.rfind('.');
    if (dot == std::string::npos)
        return instancePath;
    return instancePath.substr(dot + 1);
}

std::vector<ModuleHealth> AnalysisEngine::computeModuleHealth(const ReportData& data) const {
    // Collect unique instance paths from allPorts.
    std::map<std::string, ModuleHealth> modules; // ordered for determinism

    for (const auto& port : data.graph.allPorts) {
        auto& m = modules[port.instancePath];
        if (m.instancePath.empty()) {
            m.instancePath = port.instancePath;
            m.shortName = shortName(port.instancePath);
        }
        m.totalPorts++;
        if (data.graph.connectedPorts.count(port.fullPath())) {
            m.connectedPorts++;
        }
    }

    // Accumulate issue counts per module.
    for (const auto& issue : data.active) {
        auto it = modules.find(issue.port.instancePath);
        if (it == modules.end()) {
            // Port's instance may not be in allPorts (shouldn't happen, but be safe).
            auto& m = modules[issue.port.instancePath];
            m.instancePath = issue.port.instancePath;
            m.shortName = shortName(issue.port.instancePath);
        }
        auto& m = modules[issue.port.instancePath];
        switch (issue.severity) {
            case Issue::Severity::ERROR: m.errorCount++; break;
            case Issue::Severity::WARN:  m.warnCount++;  break;
            case Issue::Severity::INFO:  m.infoCount++;  break;
        }
    }

    // Compute score for each module.
    std::vector<ModuleHealth> result;
    result.reserve(modules.size());
    for (auto& [path, m] : modules) {
        double penalty = m.errorCount * kErrorPenalty
                       + m.warnCount  * kWarnPenalty
                       + m.infoCount  * kInfoPenalty;
        m.score = std::max(0.0, 1.0 - penalty);
        result.push_back(std::move(m));
    }

    // Sort by score ascending (worst first).
    std::sort(result.begin(), result.end(), [](const ModuleHealth& a, const ModuleHealth& b) {
        return a.score < b.score;
    });

    return result;
}

std::vector<CouplingEdge> AnalysisEngine::computeCoupling(const ReportData& data) const {
    // Key: (srcFullPath, dstFullPath) -> count (use full path for uniqueness)
    struct PathPair {
        std::string srcPath, dstPath, srcShort, dstShort;
    };
    std::map<std::pair<std::string, std::string>, int> counts;
    std::map<std::pair<std::string, std::string>, PathPair> info;

    for (const auto& conn : data.graph.connections) {
        auto& sp = conn.source.instancePath;
        auto& dp = conn.dest.instancePath;
        if (sp != dp) {
            auto key = std::make_pair(sp, dp);
            counts[key]++;
            if (info.find(key) == info.end()) {
                info[key] = {sp, dp, shortName(sp), shortName(dp)};
            }
        }
    }

    std::vector<CouplingEdge> result;
    result.reserve(counts.size());
    for (auto& [key, count] : counts) {
        auto& p = info[key];
        result.push_back({p.srcShort, p.dstShort, p.srcPath, p.dstPath, count});
    }

    // Sort by connectionCount descending.
    std::sort(result.begin(), result.end(), [](const CouplingEdge& a, const CouplingEdge& b) {
        return a.connectionCount > b.connectionCount;
    });

    return result;
}

std::vector<RiskItem> AnalysisEngine::classifyRisks(const std::vector<Issue>& issues) const {
    std::vector<RiskItem> result;
    result.reserve(issues.size());

    for (const auto& issue : issues) {
        RiskItem item;
        item.issue = issue;

        switch (issue.type) {
            case Issue::Type::WIDTH_MISMATCH:
                // Truncation (source wider than dest) is HIGH risk; extension is LOW.
                if (issue.connection.has_value() &&
                    issue.connection->source.width > issue.connection->dest.width) {
                    item.level = RiskItem::Level::HIGH;
                    item.reason = "width truncation may silently lose data bits";
                } else {
                    item.level = RiskItem::Level::LOW;
                    item.reason = "width extension is typically safe (zero/sign extension)";
                }
                break;

            case Issue::Type::TYPE_MISMATCH:
                item.level = RiskItem::Level::MEDIUM;
                item.reason = "type mismatch may cause unexpected sign extension or interpretation";
                break;

            case Issue::Type::PROTOCOL_INCOMPLETE:
                item.level = RiskItem::Level::HIGH;
                item.reason = "missing protocol signals can cause bus hangs or data corruption";
                break;

            case Issue::Type::UNDRIVEN_INPUT:
                item.level = RiskItem::Level::MEDIUM;
                item.reason = "undriven input will hold an indeterminate value";
                break;

            case Issue::Type::DANGLING_OUTPUT:
                item.level = RiskItem::Level::LOW;
                item.reason = "unused output is benign but may indicate dead logic";
                break;

            case Issue::Type::CONVENTION:
                item.level = RiskItem::Level::LOW;
                item.reason = "naming convention violation affects maintainability";
                break;

            case Issue::Type::EXPECT_MISSING:
                item.level = RiskItem::Level::MEDIUM;
                item.reason = "expected connection is missing from the design";
                break;

            case Issue::Type::EXPECT_FORBIDDEN:
                item.level = RiskItem::Level::HIGH;
                item.reason = "forbidden connection is present in the design";
                break;
        }

        result.push_back(std::move(item));
    }

    // Sort by level: HIGH < MEDIUM < LOW (enum order).
    std::sort(result.begin(), result.end(), [](const RiskItem& a, const RiskItem& b) {
        return static_cast<int>(a.level) < static_cast<int>(b.level);
    });

    return result;
}

AnalysisResult AnalysisEngine::analyze(const ReportData& data) const {
    AnalysisResult result;

    result.totalPorts = static_cast<int>(data.graph.allPorts.size());
    result.totalConnections = static_cast<int>(data.graph.connections.size());
    result.totalIssues = static_cast<int>(data.active.size());

    result.moduleHealth = computeModuleHealth(data);
    result.coupling = computeCoupling(data);
    result.risks = classifyRisks(data.active);

    // Overall score: average of module scores, or 1.0 if no modules.
    if (result.moduleHealth.empty()) {
        result.overallScore = 1.0;
    } else {
        double sum = 0.0;
        for (const auto& m : result.moduleHealth) {
            sum += m.score;
        }
        result.overallScore = sum / static_cast<double>(result.moduleHealth.size());
    }

    return result;
}

} // namespace connect
