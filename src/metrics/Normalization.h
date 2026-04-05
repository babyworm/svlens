#pragma once

#include "TransformGraph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace metrics {

struct NormalizationGroup {
    std::string signature;
    uint32_t multiplicity = 0;
    uint32_t representative_width = 0;
    uint32_t collapsed_from = 0; // how many raw nodes collapsed
};

struct NormalizationResult {
    bool enabled = false;
    int lane_min_width = 2;
    std::vector<NormalizationGroup> groups;
    uint32_t normalized_count = 0;
};

// Given a set of node indices from a cone, group them by signature
// and compute normalized counts.
NormalizationResult normalizeCone(const TransformGraph& graph,
                                  const std::vector<uint32_t>& coneNodes,
                                  bool enabled,
                                  int laneMinWidth);

} // namespace metrics
