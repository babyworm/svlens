#pragma once

#include "TransformGraph.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace metrics {

struct ConeSummary {
    ValueRef root;
    uint32_t raw_node_count = 0;
    uint32_t logic_depth_est = 0;
    uint32_t unique_transform_count = 0;
    uint32_t repeated_lane_group_count = 0;
    uint32_t normalized_transform_count = 0;
    uint32_t approximate_node_count = 0;
    uint32_t source_ff_count = 0;
    uint32_t source_input_count = 0;
    bool approximate = false;

    // Nodes belonging to this cone (indices into TransformGraph::nodes)
    std::vector<uint32_t> cone_nodes;

    // Intermediate signal names encountered during traversal (ordered)
    std::vector<std::string> signal_chain;
};

class ConeAnalyzer {
public:
    ConeAnalyzer(const TransformGraph& graph, int maxDepth);

    ConeSummary analyzeCone(const ValueRef& root);

private:
    void traverse(const std::string& signalKey, int depth,
                  std::vector<uint32_t>& coneNodes,
                  std::unordered_set<uint32_t>& coneNodeSet,
                  std::unordered_map<std::string, bool>& visited,
                  int& maxDepthReached,
                  uint32_t& sourceInputs, uint32_t& sourceFfs,
                  uint32_t& approxCount,
                  std::vector<std::string>& signalChain);

    const TransformGraph& graph_;
    int maxDepth_;
    std::unordered_set<std::string> ffQNames_;
};

} // namespace metrics
