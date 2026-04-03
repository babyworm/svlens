#pragma once

#include "sv-cdccheck/types.h"
#include "slang/ast/Compilation.h"

namespace sv_cdccheck {

/// Pass 3: Connectivity graph — build FF-to-FF data paths
///
/// Analyzes assignments and data flow to build edges between FFs.
/// An edge from FF_A to FF_B means FF_A's output is used (directly or
/// through combinational logic) as input to FF_B.
class ConnectivityBuilder {
public:
    ConnectivityBuilder(slang::ast::Compilation& compilation,
                        const std::vector<std::unique_ptr<FFNode>>& ff_nodes);

    void analyze();
    const std::vector<FFEdge>& getEdges() const;

private:
    slang::ast::Compilation& compilation_;
    const std::vector<std::unique_ptr<FFNode>>& ff_nodes_;
    std::vector<FFEdge> edges_;

    /// Build a map: signal name → FFNode* for quick lookup
    std::unordered_map<std::string, FFNode*> buildFFOutputMap() const;

    /// For each FF, check if its fanin signals include another FF's output
    void findFFtoFFEdges(const std::unordered_map<std::string, FFNode*>& output_map);
};

} // namespace sv_cdccheck
