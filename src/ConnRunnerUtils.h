#pragma once

#include "ConnRunner.h"
#include "ReportGenerator.h"

namespace connect {

bool buildConnectionGraph(slang::ast::Compilation& compilation,
                          const ConnCliOptions& opts,
                          ConnectionGraph& graph);

std::vector<Issue> runConnCheckers(const ConnCliOptions& opts,
                                   const ConnectionGraph& graph);

void generateConnReports(const ConnCliOptions& opts, const ReportData& data);
void printConnInterfaceSummary(const ConnCliOptions& opts, const ReportData& data);
void runConnDiffMode(const ConnCliOptions& opts, const ReportData& data);
void printConnClockResetTopology(const ReportData& data);
void runConnSignalTrace(const ConnCliOptions& opts, const ReportData& data);

} // namespace connect
