#pragma once

#include "sv-cdccheck/types.h"
#include "slang/ast/Compilation.h"

namespace sv_cdccheck {

/// Latch warning: always_latch detected (not a proper FF for CDC)
struct LatchWarning {
    std::string hier_path;  // instance path where latch was found
    std::string message;
};

/// FF classification error (e.g., multi-clock sensitivity list)
struct FFClassificationError {
    std::string hier_path;
    std::string message;
};

/// Pass 2: FF classification — map every FF to its clock domain
///
/// Detects always_ff and legacy always @(posedge) blocks.
/// Flags always_latch as warnings (not classified as FFs).
class FFClassifier {
public:
    FFClassifier(slang::ast::Compilation& compilation,
                 ClockDatabase& clock_db);

    void analyze();
    const std::vector<std::unique_ptr<FFNode>>& getFFNodes() const;
    std::vector<std::unique_ptr<FFNode>> releaseFFNodes();
    const std::vector<LatchWarning>& getLatchWarnings() const;
    const std::vector<FFClassificationError>& getErrors() const;

private:
    slang::ast::Compilation& compilation_;
    ClockDatabase& clock_db_;
    std::vector<std::unique_ptr<FFNode>> ff_nodes_;
    std::vector<LatchWarning> latch_warnings_;
    std::vector<FFClassificationError> errors_;
};

} // namespace sv_cdccheck
