#pragma once

#include "sv-cdccheck/types.h"
#include "sv-cdccheck/sdc_parser.h"

namespace sv_cdccheck {

/// Pass 4: Cross-domain detection
///
/// For each edge where source.domain != dest.domain,
/// classify the crossing and create a CrossingReport.
class CrossingDetector {
public:
    CrossingDetector(const std::vector<FFEdge>& edges,
                     const ClockDatabase& clock_db);

    /// Set SDC false_path constraints to auto-waive matching crossings
    void setFalsePaths(const std::vector<SdcFalsePath>& false_paths);

    void analyze();
    std::vector<CrossingReport> getCrossings() const;

private:
    const std::vector<FFEdge>& edges_;
    const ClockDatabase& clock_db_;
    std::vector<CrossingReport> crossings_;
    std::vector<SdcFalsePath> false_paths_;
    int violation_counter_ = 0;
    int caution_counter_ = 0;
    int convention_counter_ = 0;
    int info_counter_ = 0;
};

} // namespace sv_cdccheck
