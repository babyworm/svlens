#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace metrics {

struct RootDiff {
    std::string root_id;
    std::string root_kind;
    int32_t raw_delta = 0;       // positive = regression
    int32_t depth_delta = 0;
    int32_t norm_delta = 0;
    bool is_new = false;         // root didn't exist in baseline
    bool is_removed = false;     // root no longer exists
};

struct BaselineDiffResult {
    bool has_baseline = false;
    int32_t total_raw_delta = 0;
    int32_t total_norm_delta = 0;
    uint32_t regressions = 0;    // roots with increased complexity
    uint32_t improvements = 0;   // roots with decreased complexity
    uint32_t new_roots = 0;
    uint32_t removed_roots = 0;
    std::vector<RootDiff> root_diffs;
};

// Load baseline report and compute diff against current results.
// baselineFile: path to previous metrics_report.json
// currentRoots: [{root_id, root_kind, raw_node_count, logic_depth_est, normalized_count}]
BaselineDiffResult computeBaselineDiff(
    const std::string& baselineFile,
    const std::vector<std::tuple<std::string, std::string, uint32_t, uint32_t, uint32_t>>& currentRoots);

} // namespace metrics
