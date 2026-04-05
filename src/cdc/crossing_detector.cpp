#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/clock_tree.h"

namespace sv_cdccheck {

namespace {

const char* relationshipToString(DomainRelationship::Type rel) {
    switch (rel) {
        case DomainRelationship::Type::Asynchronous: return "asynchronous";
        case DomainRelationship::Type::SameSource: return "same_source";
        case DomainRelationship::Type::Divided: return "divided";
        case DomainRelationship::Type::PhysicallyExclusive: return "physically_exclusive";
        case DomainRelationship::Type::LogicallyExclusive: return "logically_exclusive";
    }
    return "unknown";
}

std::optional<double> timingBasis(const CrossingReport& report) {
    if (report.dest_domain && report.dest_domain->source && report.dest_domain->source->period_ns)
        return report.dest_domain->source->period_ns;
    if (report.source_domain && report.source_domain->source && report.source_domain->source->period_ns)
        return report.source_domain->source->period_ns;
    return std::nullopt;
}

} // namespace

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
        auto relationship = clock_db_.relationshipBetween(
            edge.source->domain, edge.dest->domain);
        bool is_async = clock_db_.isAsynchronous(
            edge.source->domain, edge.dest->domain);
        report.relationship = relationship
            ? relationshipToString(*relationship)
            : "inferred_asynchronous";
        report.timing_basis_ns = timingBasis(report);

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
            report.rationale =
                "Source and destination domains are treated as asynchronous; CDC synchronization is required.";
        } else if (relationship &&
                   (*relationship == DomainRelationship::Type::PhysicallyExclusive ||
                    *relationship == DomainRelationship::Type::LogicallyExclusive)) {
            report.severity = Severity::Info;
            report.category = ViolationCategory::Info;
            report.id = "INFO-" + std::to_string(++info_counter_);
            report.rule = "Ac_cdc01";
            report.recommendation =
                "[Ac_cdc01] Exclusive clock relationship detected — verify mux/select assumptions and SDC intent";
            report.rationale =
                "Clock groups are exclusive per constraints, so the crossing is informational rather than a synchronizer violation.";
        } else if (is_gated && !is_async) {
            report.severity = Severity::Low;
            report.category = ViolationCategory::Info;
            report.id = "INFO-" + std::to_string(++info_counter_);
            report.rule = "Ac_cdc01";
            report.recommendation = "[Ac_cdc01] Gated-clock crossing — verify clock gating is safe";
            report.rationale =
                "A gated clock is involved, so this crossing is downgraded to informational review.";
        } else {
            // Related domains (divided, gated) -- lower severity
            report.severity = Severity::Medium;
            report.category = ViolationCategory::Caution;
            report.id = "CAUTION-" + std::to_string(++caution_counter_);
            report.rule = "Ac_cdc01";
            report.recommendation = "[Ac_cdc01] Verify timing constraints for related-clock crossing";
            if (relationship && *relationship == DomainRelationship::Type::Divided) {
                report.rationale =
                    "Domains share a divided/generated relationship, so the crossing is timing-sensitive but not fully asynchronous.";
            } else if (relationship && *relationship == DomainRelationship::Type::SameSource) {
                report.rationale =
                    "Domains share a common source; verify edge/phase assumptions and timing constraints for the related-clock crossing.";
            } else {
                report.rationale =
                    "Crossing is not fully asynchronous, but timing and synchronization assumptions still need review.";
            }
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

        if (report.timing_basis_ns.has_value() && report.category != ViolationCategory::Violation) {
            report.recommendation += " (timing basis " + std::to_string(*report.timing_basis_ns) + " ns)";
        }

        crossings_.push_back(std::move(report));
    }
}

std::vector<CrossingReport> CrossingDetector::getCrossings() const {
    return crossings_;
}

} // namespace sv_cdccheck
