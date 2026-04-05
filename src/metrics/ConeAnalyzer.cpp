#include "ConeAnalyzer.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace metrics {

ConeAnalyzer::ConeAnalyzer(const TransformGraph& graph, int maxDepth)
    : graph_(graph), maxDepth_(maxDepth) {
    for (auto& ff : graph_.flip_flops)
        ffQNames_.insert(ff.q_ref.canonical());
}

ConeSummary ConeAnalyzer::analyzeCone(const ValueRef& root) {
    ConeSummary summary;
    summary.root = root;

    std::unordered_map<std::string, bool> visited;
    int maxDepthReached = 0;
    uint32_t sourceInputs = 0;
    uint32_t sourceFfs = 0;
    uint32_t approxCount = 0;

    traverse(root.canonical(), 0, summary.cone_nodes, visited,
             maxDepthReached, sourceInputs, sourceFfs, approxCount,
             summary.signal_chain);

    summary.raw_node_count = static_cast<uint32_t>(summary.cone_nodes.size());
    summary.logic_depth_est = static_cast<uint32_t>(maxDepthReached);
    summary.source_input_count = sourceInputs;
    summary.source_ff_count = sourceFfs;
    summary.approximate_node_count = approxCount;
    summary.approximate = (approxCount > 0);

    // Count unique signatures
    std::unordered_set<std::string> uniqueSigs;
    for (auto nodeId : summary.cone_nodes) {
        auto& node = graph_.nodes[nodeId];
        uniqueSigs.insert(node.signature());
    }
    summary.unique_transform_count = static_cast<uint32_t>(uniqueSigs.size());

    return summary;
}

void ConeAnalyzer::traverse(const std::string& signalKey, int depth,
                            std::vector<uint32_t>& coneNodes,
                            std::unordered_map<std::string, bool>& visited,
                            int& maxDepthReached,
                            uint32_t& sourceInputs, uint32_t& sourceFfs,
                            uint32_t& approxCount,
                            std::vector<std::string>& signalChain) {
    if (depth > maxDepth_)
        return;
    if (visited.count(signalKey))
        return;
    visited[signalKey] = true;

    if (depth > maxDepthReached)
        maxDepthReached = depth;

    // Check if this signal has any drivers in the graph
    auto it = graph_.drivers_by_value.find(signalKey);

    // If no direct driver, look for partial drivers (range-selected sub-signals)
    // e.g., root "y" should find drivers for "y[7:0]", "y[15:8]", etc.
    std::vector<uint32_t> collectedDrivers;
    if (it != graph_.drivers_by_value.end()) {
        collectedDrivers = it->second;
    } else {
        std::string prefix = signalKey + "[";
        for (auto& [key, nodeIds] : graph_.drivers_by_value) {
            if (key.compare(0, prefix.size(), prefix) == 0) {
                collectedDrivers.insert(collectedDrivers.end(),
                                        nodeIds.begin(), nodeIds.end());
            }
        }
    }

    if (collectedDrivers.empty()) {
        // Leaf node: primary input, FF output, constant, or unknown
        if (signalKey.find("__const") != std::string::npos)
            return; // constant — not counted as source
        if (signalKey.find("__unknown") != std::string::npos)
            return;
        // Distinguish FF Q outputs from primary inputs
        if (ffQNames_.count(signalKey)) {
            ++sourceFfs;
        } else {
            ++sourceInputs;
        }
        // Record non-internal signal names in path
        if (signalKey.find("__") != 0)
            signalChain.push_back(signalKey);
        return;
    }

    for (auto nodeId : collectedDrivers) {
        auto& node = graph_.nodes[nodeId];
        coneNodes.push_back(nodeId);

        if (node.approximate)
            ++approxCount;

        // Recurse into inputs
        for (auto& input : node.inputs) {
            traverse(input.canonical(), depth + 1, coneNodes, visited,
                     maxDepthReached, sourceInputs, sourceFfs, approxCount,
                     signalChain);
        }
    }
}

} // namespace metrics
