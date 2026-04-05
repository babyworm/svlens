#pragma once

#include "CompilationSession.h"
#include "MetricsCli.h"

namespace metrics {

int runMetricsWithCompilation(slang::ast::Compilation& compilation,
                              const MetricsCliOptions& opts);

} // namespace metrics
