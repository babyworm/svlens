#pragma once

#include "CdcRunner.h"
#include "sv-cdccheck/types.h"

namespace cdccli {

sv_cdccheck::AnalysisResult analyzeCdcCompilation(slang::ast::Compilation& compilation,
                                                  const CdcCliOptions& opts);

void emitCdcReports(const CdcCliOptions& opts,
                    const sv_cdccheck::AnalysisResult& result);

void printCdcSummary(const CdcCliOptions& opts,
                     const sv_cdccheck::AnalysisResult& result);

} // namespace cdccli
