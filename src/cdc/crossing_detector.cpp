#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/clock_tree.h"

namespace sv_cdccheck {

CrossingDetector::CrossingDetector(const std::vector<FFEdge>& edges,
                                   const ClockDatabase& clock_db)
    : edges_(edges), clock_db_(clock_db) {}

void CrossingDetector::analyze() {
    for (auto& edge : edges_) {
        if (!edge.source || !edge.dest) continue;
        if (!edge.source->domain || !edge.dest->domain) continue;

        // Same domain -> no crossing
        if (edge.source->domain->isSameDomain(*edge.dest->domain))
            continue;

        CrossingReport report;
        report.source_domain = edge.source->domain;
        report.dest_domain = edge.dest->domain;
        report.source_signal = edge.source->hier_path;
        report.dest_signal = edge.dest->hier_path;
        report.sync_type = SyncType::None; // Will be updated by SyncVerifier

        // Populate path from the edge's comb_path
        if (!edge.comb_path.empty()) {
            report.path = edge.comb_path;
        }

        // Classify severity based on domain relationship FIRST
        bool is_async = clock_db_.isAsynchronous(
            edge.source->domain, edge.dest->domain);

        // Check if this is a gated-clock crossing
        bool is_gated = false;
        for (auto& net : clock_db_.nets) {
            if (net->is_gated &&
                (net->source == edge.source->domain->source ||
                 net->source == edge.dest->domain->source)) {
                is_gated = true;
                break;
            }
        }

        if (is_async) {
            report.severity = Severity::High;
            report.category = ViolationCategory::Violation;
            report.id = "VIOLATION-" + std::to_string(++violation_counter_);
            report.rule = "Ac_cdc01";
            report.recommendation = "[Ac_cdc01] Insert 2-FF synchronizer at " +
                edge.dest->hier_path;
        } else if (is_gated && !is_async) {
            report.severity = Severity::Low;
            report.category = ViolationCategory::Info;
            report.id = "INFO-" + std::to_string(++info_counter_);
            report.rule = "Ac_cdc01";
            report.recommendation = "[Ac_cdc01] Gated-clock crossing — verify clock gating is safe";
        } else {
            // Related domains (divided, gated) -- lower severity
            report.severity = Severity::Medium;
            report.category = ViolationCategory::Caution;
            report.id = "CAUTION-" + std::to_string(++caution_counter_);
            report.rule = "Ac_cdc01";
            report.recommendation = "[Ac_cdc01] Verify timing constraints for related-clock crossing";
        }

        // Add CONVENTION annotation for non-standard clock naming
        // This is a separate note, NOT a replacement for the real category
        const std::string& src_clk_name = report.source_domain->source->origin_signal.empty()
            ? report.source_domain->source->name
            : report.source_domain->source->origin_signal;
        const std::string& dst_clk_name = report.dest_domain->source->origin_signal.empty()
            ? report.dest_domain->source->name
            : report.dest_domain->source->origin_signal;
        bool src_standard = ClockTreeAnalyzer::isClockName(src_clk_name);
        bool dst_standard = ClockTreeAnalyzer::isClockName(dst_clk_name);

        if (!src_standard || !dst_standard) {
            std::string bad_names;
            if (!src_standard) bad_names += src_clk_name;
            if (!src_standard && !dst_standard) bad_names += ", ";
            if (!dst_standard) bad_names += dst_clk_name;
            report.recommendation += ". Also: non-standard clock naming convention (" +
                bad_names + "). Use *clk*/*clock*/*ck* naming convention";
        }

        crossings_.push_back(std::move(report));
    }
}

std::vector<CrossingReport> CrossingDetector::getCrossings() const {
    return crossings_;
}

} // namespace sv_cdccheck
