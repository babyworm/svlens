#include "Normalization.h"

#include <algorithm>
#include <map>

namespace metrics {

NormalizationResult normalizeCone(const TransformGraph& graph,
                                  const std::vector<uint32_t>& coneNodes,
                                  bool enabled,
                                  int laneMinWidth) {
    NormalizationResult result;
    result.enabled = enabled;
    result.lane_min_width = laneMinWidth;

    if (!enabled || coneNodes.empty()) {
        result.normalized_count = static_cast<uint32_t>(coneNodes.size());
        return result;
    }

    // Group nodes by signature
    // Use ordered map for deterministic output ordering
    std::map<std::string, std::vector<uint32_t>> sigGroups;
    for (auto nodeId : coneNodes) {
        auto& node = graph.nodes[nodeId];
        sigGroups[node.signature()].push_back(nodeId);
    }

    // Pass 1: Group by exact signature (repeated lane detection)
    uint32_t normalizedCount = 0;
    std::vector<uint32_t> ungroupedNodes; // nodes not collapsed in pass 1

    for (auto& [sig, nodeIds] : sigGroups) {
        if (nodeIds.size() <= 1) {
            ungroupedNodes.insert(ungroupedNodes.end(), nodeIds.begin(), nodeIds.end());
            continue;
        }

        bool qualifiesAsLane = true;
        uint32_t repWidth = 0;
        for (auto id : nodeIds) {
            auto& node = graph.nodes[id];
            if (node.bit_width < static_cast<uint32_t>(laneMinWidth)) {
                qualifiesAsLane = false;
                break;
            }
            if (repWidth == 0)
                repWidth = node.bit_width;
        }

        if (qualifiesAsLane) {
            NormalizationGroup group;
            group.signature = sig;
            group.multiplicity = static_cast<uint32_t>(nodeIds.size());
            group.representative_width = repWidth;
            group.collapsed_from = static_cast<uint32_t>(nodeIds.size());
            result.groups.push_back(std::move(group));
            normalizedCount += 1;
        } else {
            ungroupedNodes.insert(ungroupedNodes.end(), nodeIds.begin(), nodeIds.end());
        }
    }

    // Pass 2: Stride normalization — group ungrouped Slice nodes by stride key
    // e.g., slice:range[7:0]:8:1 + slice:range[15:8]:8:1 → one stride group
    std::map<std::string, std::vector<uint32_t>> strideGroups;
    std::vector<uint32_t> nonStrideNodes;
    for (auto id : ungroupedNodes) {
        auto& node = graph.nodes[id];
        if (node.op_kind == TransformNode::Slice &&
            node.bit_width >= static_cast<uint32_t>(laneMinWidth)) {
            strideGroups[node.strideKey()].push_back(id);
        } else {
            nonStrideNodes.push_back(id);
        }
    }

    for (auto& [skey, nodeIds] : strideGroups) {
        if (nodeIds.size() >= 2) {
            uint32_t repWidth = graph.nodes[nodeIds[0]].bit_width;
            NormalizationGroup group;
            group.signature = "stride:" + skey;
            group.multiplicity = static_cast<uint32_t>(nodeIds.size());
            group.representative_width = repWidth;
            group.collapsed_from = static_cast<uint32_t>(nodeIds.size());
            result.groups.push_back(std::move(group));
            normalizedCount += 1;
        } else {
            normalizedCount += static_cast<uint32_t>(nodeIds.size());
        }
    }
    normalizedCount += static_cast<uint32_t>(nonStrideNodes.size());

    // Sort groups for deterministic output: signature, representative_width, multiplicity
    std::sort(result.groups.begin(), result.groups.end(),
              [](const NormalizationGroup& a, const NormalizationGroup& b) {
                  if (a.signature != b.signature) return a.signature < b.signature;
                  if (a.representative_width != b.representative_width)
                      return a.representative_width < b.representative_width;
                  return a.multiplicity < b.multiplicity;
              });

    result.normalized_count = normalizedCount;
    return result;
}

} // namespace metrics
