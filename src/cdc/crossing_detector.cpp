#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/clock_tree.h"

#include <algorithm>
#include <cctype>

namespace sv_cdccheck {

namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string leafSignalName(const std::string& hier_path) {
    auto pos = hier_path.rfind('.');
    if (pos == std::string::npos)
        return hier_path;
    return hier_path.substr(pos + 1);
}

std::string instancePath(const std::string& hier_path) {
    auto pos = hier_path.rfind('.');
    if (pos == std::string::npos)
        return {};
    return hier_path.substr(0, pos);
}

bool samePrimitiveInstance(const FFEdge& edge) {
    return edge.source && edge.dest &&
           instancePath(edge.source->hier_path) == instancePath(edge.dest->hier_path);
}

bool isIsochronousHandshakePair(const FFEdge& edge) {
    if (!edge.source || !edge.dest || !samePrimitiveInstance(edge))
        return false;

    auto src_primitive = toLowerCopy(edge.source->primitive_name);
    auto dst_primitive = toLowerCopy(edge.dest->primitive_name);
    if (src_primitive != "isochronous_4phase_handshake" ||
        dst_primitive != "isochronous_4phase_handshake") {
        return false;
    }

    const auto src_leaf = leafSignalName(edge.source->hier_path);
    const auto dst_leaf = leafSignalName(edge.dest->hier_path);
    return (src_leaf == "src_req_q" && dst_leaf == "dst_req_q") ||
           (src_leaf == "dst_ack_q" && dst_leaf == "src_ack_q");
}

bool isSyncregPair(const FFEdge& edge) {
    if (!edge.source || !edge.dest || !samePrimitiveInstance(edge))
        return false;

    auto src_primitive = toLowerCopy(edge.source->primitive_name);
    auto dst_primitive = toLowerCopy(edge.dest->primitive_name);
    if (src_primitive != "syncreg" || dst_primitive != "syncreg")
        return false;

    return leafSignalName(edge.source->hier_path) == "regA" &&
           leafSignalName(edge.dest->hier_path) == "regB";
}

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

void CrossingDetector::setFalsePaths(const std::vector<SdcFalsePath>& false_paths) {
    false_paths_ = false_paths;
}

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
        report.sync_type = SyncType::None; // May be updated by SyncVerifier or intent recognizers

        if (!edge.comb_path.empty())
            report.path = edge.comb_path;

        auto relationship = clock_db_.relationshipBetween(
            edge.source->domain, edge.dest->domain);
        bool is_async = clock_db_.isAsynchronous(
            edge.source->domain, edge.dest->domain);
        report.relationship = relationship
            ? relationshipToString(*relationship)
            : "inferred_asynchronous";
        report.timing_basis_ns = timingBasis(report);

        // Check SDC false_path auto-waive
        bool false_path_matched = false;
        for (const auto& fp : false_paths_) {
            if (report.source_domain && report.dest_domain &&
                report.source_domain->source && report.dest_domain->source &&
                fp.from == report.source_domain->source->name &&
                fp.to == report.dest_domain->source->name) {
                false_path_matched = true;
                break;
            }
        }
        if (false_path_matched) {
            report.severity = Severity::Info;
            report.category = ViolationCategory::Info;
            report.id = "INFO-" + std::to_string(++info_counter_);
            report.rule = "Ac_cdc01";
            report.waive_reason = "sdc_false_path";
            report.recommendation =
                "[Ac_cdc01] Crossing waived by SDC set_false_path constraint";
            report.rationale =
                "SDC set_false_path from " + report.source_domain->source->name +
                " to " + report.dest_domain->source->name +
                " — crossing is intentionally excluded from timing analysis.";
            crossings_.push_back(std::move(report));
            continue;
        }

        if (isIsochronousHandshakePair(edge)) {
            report.severity = Severity::Info;
            report.category = ViolationCategory::Info;
            report.id = "INFO-" + std::to_string(++info_counter_);
            report.rule = "Ac_cdc01";
            report.sync_type = SyncType::Handshake;
            report.recommendation =
                "[Ac_cdc01] Intentional isochronous handshake primitive detected — verify primitive assumptions and integration constraints";
            report.rationale =
                "Recognized isochronous_4phase_handshake by primitive name plus src_req_q/dst_req_q or dst_ack_q/src_ack_q structural signature in the same instance; downgrade to informational review.";
            crossings_.push_back(std::move(report));
            continue;
        }

        if (isSyncregPair(edge)) {
            report.severity = Severity::Info;
            report.category = ViolationCategory::Info;
            report.id = "INFO-" + std::to_string(++info_counter_);
            report.rule = "Ac_cdc01";
            report.sync_type = SyncType::TwoFF;
            report.recommendation =
                "[Ac_cdc01] Intentional syncreg primitive detected — verify the primitive is used as the designed CLKA/CLKB synchronizer";
            report.rationale =
                "Recognized syncreg by primitive name plus regA/regB structural signature in the same instance; downgrade to informational review.";
            crossings_.push_back(std::move(report));
            continue;
        }

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
