#pragma once
#include "AnalysisResult.h"
#include "ConnectionGraph.h"
#include "Issue.h"
#include <optional>
#include <ostream>
#include <vector>

namespace connect {

struct ReportData {
    std::string topModule;
    ConnectionGraph graph;
    std::vector<Issue> active;
    std::vector<Issue> waived;
    std::optional<AnalysisResult> analysis; // populated when AnalysisEngine is run

    int errorCount() const {
        int n = 0;
        for (auto& i : active) if (i.severity == Issue::Severity::ERROR) ++n;
        return n;
    }
    int warnCount() const {
        int n = 0;
        for (auto& i : active) if (i.severity == Issue::Severity::WARN) ++n;
        return n;
    }
    int infoCount() const {
        int n = 0;
        for (auto& i : active) if (i.severity == Issue::Severity::INFO) ++n;
        return n;
    }
};

class IReportGenerator {
public:
    virtual ~IReportGenerator() = default;
    virtual void generate(const ReportData& data, std::ostream& out) const = 0;
};

} // namespace connect
