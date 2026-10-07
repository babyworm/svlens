#include "sv-cdccheck/report_generator.h"
#include "cdc_html_template.h"
#include <slang/text/SourceManager.h>
#include <fstream>
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace sv_cdccheck {

struct SvaSyncAssertion {
    std::string label;
    std::string clock;
    std::string disable_condition;
    // first stage sampled on the prior clock, then the receiving stage
    std::vector<std::pair<std::string, std::string>> stage_pairs;
};

struct SvaFifoGrayAssertion {
    std::string label;
    std::string clock;
    std::string reset;
    std::string signal;
};

struct SvaReqAckAssertion {
    std::string label;
    std::string clock;
    std::string reset;
    std::string request;
    std::string acknowledgment;
};

struct SvaReqAckDataAssertion {
    std::string label;
    std::string clock;
    std::string reset;
    std::string request;
    std::string acknowledgment;
    std::string data;
    bool src_to_dst = true;
};

struct ResetUsageRecord {
    size_t ff_count = 0;
    bool asynchronous = false;
    bool has_polarity = false;
    bool mixed_polarity = false;
    ResetSignal::Polarity polarity = ResetSignal::Polarity::ActiveLow;
    std::set<std::string> dest_domains;
    std::string driver_ff;
    std::string source_domain;
    std::optional<bool> driver_inverted;
    bool driver_polarity_known_for_all = true;
    bool driver_unresolved_or_mixed = false;
    std::set<std::string> conditional_mux_outputs;
    struct BranchFFs {
        std::map<std::pair<std::string, std::string>, std::optional<bool>> candidates;
        bool truncated = false;
    };
    std::map<std::string, std::array<BranchFFs, 2>> mux_branch_ffs;
    bool mux_trace_truncated = false;
};

static std::map<std::string, ResetUsageRecord> collectResetUsage(const AnalysisResult& result) {
    std::map<std::string, ResetUsageRecord> usage;
    for (const auto& ff : result.ff_nodes) {
        if (!ff->reset)
            continue;
        auto& record = usage[ff->reset->hier_path];
        ++record.ff_count;
        record.asynchronous |= ff->reset->is_async;
        if (record.has_polarity && record.polarity != ff->reset->polarity)
            record.mixed_polarity = true;
        record.polarity = ff->reset->polarity;
        record.has_polarity = true;
        if (ff->reset->driver_ff_path.empty() || ff->reset->source_domain.empty()) {
            record.driver_unresolved_or_mixed = true;
        } else if (record.driver_ff.empty()) {
            record.driver_ff = ff->reset->driver_ff_path;
            record.source_domain = ff->reset->source_domain;
        } else if (record.driver_ff != ff->reset->driver_ff_path || record.source_domain != ff->reset->source_domain) {
            record.driver_unresolved_or_mixed = true;
        }
        if (!ff->reset->driver_inverted) {
            record.driver_polarity_known_for_all = false;
        } else if (!record.driver_inverted) {
            record.driver_inverted = ff->reset->driver_inverted;
        } else if (record.driver_inverted != ff->reset->driver_inverted) {
            record.driver_unresolved_or_mixed = true;
        }
        if (ff->domain)
            record.dest_domains.insert(ff->domain->canonical_name);
    }

    if (result.clock_db.reset_mux_inputs.empty())
        return usage;

    std::unordered_map<std::string, std::vector<std::pair<std::string, bool>>> predecessors;
    auto addPredecessors = [&](const auto& edges, bool inverted) {
        for (const auto& [source, destinations] : edges) {
            for (const auto& dest : destinations)
                predecessors[dest].emplace_back(source, inverted);
        }
    };
    addPredecessors(result.clock_db.directed_aliases, false);
    addPredecessors(result.clock_db.reset_inversions, true);
    addPredecessors(result.clock_db.reset_selected_aliases, false);

    std::unordered_map<std::string, std::vector<std::string>> indexedFieldPredecessors;
    for (const auto& [source, destinations] : result.clock_db.reset_indexed_field_aliases) {
        for (const auto& dest : destinations)
            indexedFieldPredecessors[dest].push_back(source);
    }

    constexpr size_t kMaxResetRouteNodes = 4096;
    std::unordered_map<std::string, std::vector<const FFNode*>> ffByPath;
    for (const auto& ff : result.ff_nodes)
        ffByPath[ff->hier_path].push_back(ff.get());
    std::unordered_map<std::string, ResetUsageRecord::BranchFFs> branchCache;
    auto traceBranchFFs = [&](const std::string& start) -> const ResetUsageRecord::BranchFFs& {
        auto [cached, inserted] = branchCache.try_emplace(start);
        if (!inserted)
            return cached->second;
        std::vector<std::pair<std::string, bool>> pending{{start, false}};
        std::array<std::unordered_set<std::string>, 2> visited;
        while (!pending.empty() && visited[0].size() + visited[1].size() < kMaxResetRouteNodes) {
            auto [current, inverted] = std::move(pending.back());
            pending.pop_back();
            if (!visited[static_cast<size_t>(inverted)].insert(current).second)
                continue;
            if (auto drivers = ffByPath.find(current); drivers != ffByPath.end()) {
                for (const auto* ff : drivers->second) {
                    const auto key = std::pair{ff->hier_path, ff->domain ? ff->domain->canonical_name : std::string{}};
                    auto [candidate, firstPath] = cached->second.candidates.emplace(key, inverted);
                    if (!firstPath && candidate->second && *candidate->second != inverted)
                        candidate->second.reset();
                }
                continue;
            }
            if (auto mux = result.clock_db.reset_mux_inputs.find(current);
                mux != result.clock_db.reset_mux_inputs.end()) {
                if (mux->second.selected_input)
                    pending.emplace_back(*mux->second.selected_input ? mux->second.input1 : mux->second.input0,
                                         inverted);
                else {
                    pending.emplace_back(mux->second.input0, inverted);
                    pending.emplace_back(mux->second.input1, inverted);
                }
            }
            if (auto aliases = predecessors.find(current); aliases != predecessors.end()) {
                for (const auto& [source, edgeInverted] : aliases->second)
                    pending.emplace_back(source, inverted != edgeInverted);
            }
            if (current.ends_with(']')) {
                const auto bracket = current.rfind('[');
                if (bracket != std::string::npos) {
                    const auto base = current.substr(0, bracket);
                    if (auto fields = indexedFieldPredecessors.find(base); fields != indexedFieldPredecessors.end()) {
                        const auto suffix = current.substr(bracket);
                        for (const auto& source : fields->second)
                            pending.emplace_back(source + suffix, inverted);
                    }
                }
            }
        }
        cached->second.truncated = !pending.empty();
        return cached->second;
    };
    for (auto& [signal, record] : usage) {
        std::vector<std::string> pending{signal};
        std::unordered_set<std::string> visited;
        while (!pending.empty() && visited.size() < kMaxResetRouteNodes) {
            std::string current = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(current).second)
                continue;
            if (auto mux = result.clock_db.reset_mux_inputs.find(current);
                mux != result.clock_db.reset_mux_inputs.end()) {
                record.conditional_mux_outputs.insert(current);
                if (mux->second.selected_input)
                    pending.push_back(*mux->second.selected_input ? mux->second.input1 : mux->second.input0);
                else {
                    pending.push_back(mux->second.input0);
                    pending.push_back(mux->second.input1);
                }
            }
            if (auto aliases = predecessors.find(current); aliases != predecessors.end()) {
                for (const auto& [source, _] : aliases->second)
                    pending.push_back(source);
            }
            if (current.ends_with(']')) {
                const auto bracket = current.rfind('[');
                if (bracket != std::string::npos) {
                    const auto base = current.substr(0, bracket);
                    if (auto fields = indexedFieldPredecessors.find(base); fields != indexedFieldPredecessors.end()) {
                        const auto suffix = current.substr(bracket);
                        for (const auto& source : fields->second)
                            pending.push_back(source + suffix);
                    }
                }
            }
        }
        record.mux_trace_truncated = !pending.empty();
        for (const auto& output : record.conditional_mux_outputs) {
            const auto& mux = result.clock_db.reset_mux_inputs.at(output);
            auto& branches = record.mux_branch_ffs[output];
            if (!mux.selected_input || !*mux.selected_input)
                branches[0] = traceBranchFFs(mux.input0);
            if (!mux.selected_input || *mux.selected_input)
                branches[1] = traceBranchFFs(mux.input1);
            record.mux_trace_truncated |= branches[0].truncated || branches[1].truncated;
        }
    }
    return usage;
}

static std::vector<std::string> svaUniqueIds(const std::vector<CrossingReport>& crossings);
static std::optional<SvaSyncAssertion> svaSyncAssertion(const AnalysisResult& result, const CrossingReport& crossing,
                                                        const std::string& safe_id);
static std::optional<SvaFifoGrayAssertion>
svaFifoGrayAssertion(const AnalysisResult& result, const CrossingReport& crossing, const std::string& safe_id);
static std::optional<SvaReqAckAssertion> svaReqAckAssertion(const AnalysisResult& result,
                                                            const CrossingReport& crossing, const std::string& safe_id);
static std::optional<SvaReqAckDataAssertion> svaReqAckDataAssertion(const CrossingReport& crossing,
                                                                    const std::string& safe_id);

static const char* categoryToString(ViolationCategory cat) {
    switch (cat) {
        case ViolationCategory::Violation:  return "VIOLATION";
        case ViolationCategory::Caution:    return "CAUTION";
        case ViolationCategory::Convention: return "CONVENTION";
        case ViolationCategory::Info:       return "INFO";
        case ViolationCategory::Waived:     return "WAIVED";
    }
    return "UNKNOWN";
}

static const char* severityToString(Severity sev) {
    switch (sev) {
        case Severity::None:   return "none";
        case Severity::Info:   return "info";
        case Severity::Low:    return "low";
        case Severity::Medium: return "medium";
        case Severity::High:   return "high";
    }
    return "unknown";
}

static const char* syncTypeToString(SyncType st) {
    switch (st) {
        case SyncType::None:      return "none";
        case SyncType::TwoFF:     return "two_ff";
        case SyncType::ThreeFF:   return "three_ff";
        case SyncType::GrayCode:  return "gray_code";
        case SyncType::Handshake: return "handshake";
        case SyncType::AsyncFIFO: return "async_fifo";
        case SyncType::MuxSync:   return "mux_sync";
        case SyncType::PulseSync: return "pulse_sync";
        case SyncType::JohnsonCounter: return "johnson_counter";
    }
    return "unknown";
}

static const char* relationshipToString(DomainRelationship::Type rel) {
    switch (rel) {
        case DomainRelationship::Type::Asynchronous: return "asynchronous";
        case DomainRelationship::Type::SameSource: return "same_source";
        case DomainRelationship::Type::Divided: return "divided";
        case DomainRelationship::Type::PhysicallyExclusive: return "physically_exclusive";
        case DomainRelationship::Type::LogicallyExclusive: return "logically_exclusive";
    }
    return "unknown";
}

int AnalysisResult::violation_count() const {
    int count = 0;
    for (auto& c : crossings)
        if (c.category == ViolationCategory::Violation) count++;
    return count;
}

int AnalysisResult::caution_count() const {
    int count = 0;
    for (auto& c : crossings)
        if (c.category == ViolationCategory::Caution) count++;
    return count;
}

int AnalysisResult::info_count() const {
    int count = 0;
    for (auto& c : crossings)
        if (c.category == ViolationCategory::Info) count++;
    return count;
}

int AnalysisResult::waived_count() const {
    int count = 0;
    for (auto& c : crossings)
        if (c.category == ViolationCategory::Waived) count++;
    return count;
}

int AnalysisResult::convention_count() const {
    int count = 0;
    for (auto& c : crossings)
        if (c.category == ViolationCategory::Convention) count++;
    return count;
}

ClockSource* ClockDatabase::addSource(std::unique_ptr<ClockSource> src) {
    auto* ptr = src.get();
    sources.push_back(std::move(src));
    return ptr;
}

ClockNet* ClockDatabase::addNet(std::unique_ptr<ClockNet> net) {
    auto* ptr = net.get();
    net_by_path[net->hier_path] = ptr;
    // Link net to its domain if one exists
    if (net->source) {
        auto* dom = findOrCreateDomain(net->source, net->edge);
        dom->nets.push_back(ptr);
    }
    nets.push_back(std::move(net));
    return ptr;
}

ClockDomain* ClockDatabase::findOrCreateDomain(ClockSource* source, Edge edge) {
    for (auto& d : domains) {
        if (d->source == source && d->edge == edge)
            return d.get();
    }
    auto dom = std::make_unique<ClockDomain>();
    dom->source = source;
    dom->edge = edge;

    // Build a unique canonical name: prefer source->id if name collides
    std::string cname = source->name;
    if (domain_by_name.count(cname) > 0) {
        cname = source->id.empty() ? (source->name + "_" + std::to_string(domains.size())) : source->id;
    }
    dom->canonical_name = cname;

    auto* ptr = dom.get();
    domain_by_name[cname] = ptr;
    domains.push_back(std::move(dom));
    return ptr;
}

ClockDomain* ClockDatabase::domainForSignal(const std::string& hier_path) const {
    auto it = net_by_path.find(hier_path);
    if (it == net_by_path.end()) return nullptr;
    auto* net = it->second;
    // Find matching domain
    for (auto& d : domains) {
        if (d->source == net->source && d->edge == net->edge)
            return d.get();
    }
    return nullptr;
}

bool ClockDatabase::isAsynchronous(const ClockDomain* a, const ClockDomain* b) const {
    if (!a || !b) return true; // unknown -> conservative
    if (a->source == b->source) return false; // same source

    if (auto rel = relationshipBetween(a, b))
        return *rel == DomainRelationship::Type::Asynchronous;
    return true; // no relationship found -> assume async
}

std::optional<DomainRelationship::Type> ClockDatabase::relationshipBetween(const ClockDomain* a,
                                                                           const ClockDomain* b) const {
    if (!a || !b)
        return std::nullopt;
    if (a->source == b->source)
        return DomainRelationship::Type::SameSource;

    for (auto& rel : relationships) {
        if ((rel.a == a->source && rel.b == b->source) ||
            (rel.a == b->source && rel.b == a->source)) {
            return rel.relationship;
        }
    }
    return std::nullopt;
}

bool ClockDatabase::isSdcDeclaredRelationship(const ClockDomain* a, const ClockDomain* b) const {
    if (!a || !b) return false;
    for (auto& rel : relationships) {
        if ((rel.a == a->source && rel.b == b->source) ||
            (rel.a == b->source && rel.b == a->source)) {
            return rel.sdc_declared;
        }
    }
    return false;
}

ReportGenerator::ReportGenerator(const AnalysisResult& result, bool includeSvaIds)
    : result_(result), includeSvaIds_(includeSvaIds) {}

std::string ReportGenerator::jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (ch < 0x20) {
                    // Control character: \uXXXX
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += static_cast<char>(ch);
                }
                break;
        }
    }
    return out;
}

void ReportGenerator::generateMarkdown(const std::filesystem::path& output_path) const {
    // Escape pipe characters that would break markdown table cells
    auto mdEscape = [](const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            if (c == '|') out += "\\|";
            else out += c;
        }
        return out;
    };

    std::ofstream out(output_path);
    out << "# CDC Analysis Report\n\n";
    out << "## Summary\n\n";
    out << "| Category | Count |\n";
    out << "|----------|-------|\n";
    out << "| VIOLATION | " << result_.violation_count() << " |\n";
    out << "| CAUTION | " << result_.caution_count() << " |\n";
    out << "| CONVENTION | " << result_.convention_count() << " |\n";
    out << "| INFO | " << result_.info_count() << " |\n";
    out << "| WAIVED | " << result_.waived_count() << " |\n\n";

    // Count FFs per domain
    std::unordered_map<std::string, int> ff_per_domain;
    for (auto& ff : result_.ff_nodes) {
        if (ff->domain)
            ff_per_domain[ff->domain->canonical_name]++;
    }

    out << "## Clock Domains\n\n";
    out << "| Domain | Source | Type | Edge | FFs |\n";
    out << "|--------|--------|------|------|-----|\n";
    for (auto& d : result_.clock_db.domains) {
        out << "| " << mdEscape(d->canonical_name)
            << " | " << mdEscape(d->source->name)
            << " | ";
        switch (d->source->type) {
            case ClockSource::Type::Primary: out << "primary"; break;
            case ClockSource::Type::Generated: out << "generated"; break;
            case ClockSource::Type::Virtual: out << "virtual"; break;
            case ClockSource::Type::AutoDetected: out << "auto"; break;
        }
        int ff_count = 0;
        auto it = ff_per_domain.find(d->canonical_name);
        if (it != ff_per_domain.end()) ff_count = it->second;
        out << " | " << (d->edge == Edge::Posedge ? "posedge" : "negedge")
            << " | " << ff_count
            << " |\n";
    }

    out << "\n## Crossings\n\n";
    for (auto& c : result_.crossings) {
        out << "### " << c.id << ": "
            << (c.source_domain ? c.source_domain->canonical_name : "?")
            << " -> "
            << (c.dest_domain ? c.dest_domain->canonical_name : "?")
            << "\n";
        out << "- Source: " << mdEscape(c.source_signal) << "\n";
        out << "- Dest: " << mdEscape(c.dest_signal) << "\n";
        if (!c.path.empty()) {
            out << "- Path: ";
            for (size_t i = 0; i < c.path.size(); i++) {
                out << mdEscape(c.path[i]);
                if (i + 1 < c.path.size()) out << " -> ";
            }
            out << "\n";
        }
        if (!c.recommendation.empty())
            out << "- Fix: " << mdEscape(c.recommendation) << "\n";
        if (!c.relationship.empty())
            out << "- Relationship: " << mdEscape(c.relationship) << "\n";
        if (!c.rationale.empty())
            out << "- Rationale: " << mdEscape(c.rationale) << "\n";
        if (c.timing_basis_ns.has_value())
            out << "- Timing Basis: " << c.timing_basis_ns.value() << " ns\n";
        if (c.sdc_false_path)
            out << "- SDC False Path: timing excluded; CDC classification unchanged\n";
        if (c.sdc_max_delay_constraint_ns)
            out << "- Declared SDC Max Delay: " << *c.sdc_max_delay_constraint_ns << " ns"
                << (c.sdc_max_delay_datapath_only ? " (datapath only)" : "") << "; not measured path delay\n";
        if (c.sdc_max_delay_ambiguous)
            out << "- SDC Max Delay: multiple matching constraints; resolve in STA\n";
        out << "\n";
    }
}

void ReportGenerator::generateJSON(const std::filesystem::path& output_path) const {
    std::ofstream out(output_path);
    writeJSON(out);
}

void ReportGenerator::writeJSON(std::ostream& out) const {
    std::unordered_set<const ClockSource*> knownRoots;
    for (const auto& [path, root] : result_.clock_db.root_by_path)
        knownRoots.insert(root);
    auto rootName = [&](const ClockDomain* domain) -> std::string {
        if (!domain || !domain->source)
            return {};
        auto* source = domain->source;
        const ClockSource* root = nullptr;
        auto it = result_.clock_db.root_by_path.find(source->origin_signal);
        if (it != result_.clock_db.root_by_path.end())
            root = it->second;
        for (const auto* net : domain->nets) {
            auto netRoot = result_.clock_db.root_by_path.find(net->hier_path);
            if (netRoot == result_.clock_db.root_by_path.end())
                continue;
            if (root && root != netRoot->second)
                return {};
            root = netRoot->second;
        }
        if (root)
            return root->name;
        if (knownRoots.contains(source))
            return source->name;
        return {};
    };
    out << "{\n";
    out << "  \"summary\": {\n";
    out << "    \"violations\": " << result_.violation_count() << ",\n";
    out << "    \"cautions\": " << result_.caution_count() << ",\n";
    out << "    \"conventions\": " << result_.convention_count() << ",\n";
    out << "    \"info\": " << result_.info_count() << ",\n";
    out << "    \"waived\": " << result_.waived_count() << "\n";
    out << "  },\n";

    out << "  \"domains\": [\n";
    for (size_t i = 0; i < result_.clock_db.domains.size(); i++) {
        auto& d = result_.clock_db.domains[i];
        out << "    {\"name\": \"" << jsonEscape(d->canonical_name) << "\", \"source\": \""
            << jsonEscape(d->source->name) << "\", \"root_source\": \"" << jsonEscape(rootName(d.get()))
            << "\", \"period_ns\": ";
        if (d->source->period_ns && std::isfinite(*d->source->period_ns) && *d->source->period_ns > 0)
            out << *d->source->period_ns;
        else
            out << "null";
        out << "}";
        if (i + 1 < result_.clock_db.domains.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // Reset usage is a sink-side inventory, not a proven RDC tree.
    const auto resetUsage = collectResetUsage(result_);
    out << "  \"reset_usage\": [\n";
    size_t resetIndex = 0;
    for (const auto& [signal, usage] : resetUsage) {
        const char* polarity = usage.mixed_polarity                                 ? "mixed"
                               : usage.polarity == ResetSignal::Polarity::ActiveLow ? "active_low"
                                                                                    : "active_high";
        out << "    {\"signal\": \"" << jsonEscape(signal)
            << "\", \"asynchronous\": " << (usage.asynchronous ? "true" : "false") << ", \"polarity\": \"" << polarity
            << "\", \"ff_count\": " << usage.ff_count;
        if (!usage.driver_unresolved_or_mixed && !usage.driver_ff.empty()) {
            out << ", \"driver_ff\": \"" << jsonEscape(usage.driver_ff) << "\", \"source_domain\": \""
                << jsonEscape(usage.source_domain) << "\"";
            if (usage.driver_polarity_known_for_all && usage.driver_inverted)
                out << ", \"driver_inverted\": " << (*usage.driver_inverted ? "true" : "false");
        }
        out << ", \"dest_domains\": [";
        size_t domainIndex = 0;
        for (const auto& domain : usage.dest_domains) {
            if (domainIndex++)
                out << ", ";
            out << "\"" << jsonEscape(domain) << "\"";
        }
        out << "]";
        if (!usage.conditional_mux_outputs.empty()) {
            out << ", \"conditional_muxes\": [";
            size_t muxIndex = 0;
            for (const auto& output : usage.conditional_mux_outputs) {
                const auto& mux = result_.clock_db.reset_mux_inputs.at(output);
                if (muxIndex++)
                    out << ", ";
                out << "{\"output\": \"" << jsonEscape(output) << "\", \"input0\": \"" << jsonEscape(mux.input0)
                    << "\", \"input1\": \"" << jsonEscape(mux.input1) << "\", \"select\": \"" << jsonEscape(mux.select)
                    << "\"";
                if (mux.selected_input)
                    out << ", \"selected_input\": " << (*mux.selected_input ? 1 : 0);
                if (!mux.input0_source.empty())
                    out << ", \"input0_source\": \"" << jsonEscape(mux.input0_source) << "\"";
                if (!mux.input1_source.empty())
                    out << ", \"input1_source\": \"" << jsonEscape(mux.input1_source) << "\"";
                if (!mux.select_source.empty())
                    out << ", \"select_source\": \"" << jsonEscape(mux.select_source) << "\"";
                auto writeDependencies = [&](const char* name, const std::vector<std::string>& dependencies) {
                    if (dependencies.empty())
                        return;
                    out << ", \"" << name << "\": [";
                    for (size_t i = 0; i < dependencies.size(); ++i) {
                        if (i)
                            out << ", ";
                        out << "\"" << jsonEscape(dependencies[i]) << "\"";
                    }
                    out << "]";
                };
                writeDependencies("input0_dependencies", mux.input0_dependencies);
                writeDependencies("input1_dependencies", mux.input1_dependencies);
                writeDependencies("select_dependencies", mux.select_dependencies);
                auto writeCandidateFFs = [&](const char* name, const ResetUsageRecord::BranchFFs& branch) {
                    if (branch.candidates.empty())
                        return;
                    out << ", \"" << name << "\": [";
                    size_t candidateIndex = 0;
                    for (const auto& [candidate, inverted] : branch.candidates) {
                        const auto& [path, domain] = candidate;
                        if (candidateIndex++)
                            out << ", ";
                        out << "{\"path\": \"" << jsonEscape(path) << "\"";
                        if (!domain.empty())
                            out << ", \"domain\": \"" << jsonEscape(domain) << "\"";
                        if (inverted)
                            out << ", \"inverted\": " << (*inverted ? "true" : "false");
                        else
                            out << ", \"inversion_ambiguous\": true";
                        out << "}";
                    }
                    out << "]";
                };
                if (auto candidates = usage.mux_branch_ffs.find(output); candidates != usage.mux_branch_ffs.end()) {
                    writeCandidateFFs("input0_candidate_ffs", candidates->second[0]);
                    writeCandidateFFs("input1_candidate_ffs", candidates->second[1]);
                }
                out << "}";
            }
            out << "]";
        }
        if (usage.mux_trace_truncated)
            out << ", \"conditional_mux_trace_truncated\": true";
        out << "}";
        if (++resetIndex < resetUsage.size())
            out << ",";
        out << "\n";
    }
    out << "  ],\n";

    const auto svaIds = includeSvaIds_ ? svaUniqueIds(result_.crossings) : std::vector<std::string>{};
    std::unordered_map<std::string, const FFNode*> ffByPath;
    for (const auto& ff : result_.ff_nodes)
        ffByPath.emplace(ff->hier_path, ff.get());
    auto writeSourceLocation = [&](const char* prefix, const std::string& signal) {
        if (!result_.sourceManager)
            return;
        const auto it = ffByPath.find(signal);
        if (it == ffByPath.end() || !it->second->location.valid())
            return;
        const auto location = it->second->location;
        const std::string filename(result_.sourceManager->getFileName(location));
        if (filename.empty())
            return;
        std::error_code ec;
        const auto relative = std::filesystem::relative(filename, std::filesystem::current_path(), ec);
        const auto path = !ec && !relative.empty() ? relative.generic_string() : filename;
        out << ", \"" << prefix << "_file\": \"" << jsonEscape(path) << "\"";
        out << ", \"" << prefix << "_line\": " << result_.sourceManager->getLineNumber(location);
        out << ", \"" << prefix << "_column\": " << result_.sourceManager->getColumnNumber(location);
    };
    out << "  \"crossings\": [\n";
    for (size_t i = 0; i < result_.crossings.size(); i++) {
        auto& c = result_.crossings[i];
        out << "    {\"id\": \"" << jsonEscape(c.id) << "\", \"source\": \"" << jsonEscape(c.source_signal)
            << "\", \"dest\": \"" << jsonEscape(c.dest_signal) << "\", \"source_domain\": \""
            << jsonEscape(c.source_domain ? c.source_domain->canonical_name : "") << "\", \"dest_domain\": \""
            << jsonEscape(c.dest_domain ? c.dest_domain->canonical_name : "") << "\", \"source_root_domain\": \""
            << jsonEscape(rootName(c.source_domain)) << "\", \"dest_root_domain\": \""
            << jsonEscape(rootName(c.dest_domain)) << "\"";

        out << ", \"capture_conditions\": [";
        for (size_t j = 0; j < c.capture_conditions.size(); ++j) {
            if (j)
                out << ", ";
            out << "\"" << jsonEscape(c.capture_conditions[j]) << "\"";
        }
        out << "]";

        // Path field
        out << ", \"path\": [";
        for (size_t j = 0; j < c.path.size(); j++) {
            out << "\"" << jsonEscape(c.path[j]) << "\"";
            if (j + 1 < c.path.size()) out << ", ";
        }
        out << "]";

        // Category, severity, sync_type
        out << ", \"category\": \"" << categoryToString(c.category) << "\"";
        out << ", \"severity\": \"" << severityToString(c.severity) << "\"";
        out << ", \"sync_type\": \"" << syncTypeToString(c.sync_type) << "\"";
        writeSourceLocation("source", c.source_signal);
        writeSourceLocation("dest", c.dest_signal);

        // Rule
        out << ", \"rule\": \"" << jsonEscape(c.rule) << "\"";
        if (includeSvaIds_) {
            std::vector<std::string> assertionIds;
            if (auto assertion = svaSyncAssertion(result_, c, svaIds[i]))
                assertionIds.push_back(assertion->label);
            if (auto assertion = svaFifoGrayAssertion(result_, c, svaIds[i]))
                assertionIds.push_back(assertion->label);
            if (auto assertion = svaReqAckAssertion(result_, c, svaIds[i]))
                assertionIds.push_back(assertion->label);
            if (auto assertion = svaReqAckDataAssertion(c, svaIds[i]))
                assertionIds.push_back(assertion->label);
            if (!assertionIds.empty())
                out << ", \"sva_assertion_id\": \"" << assertionIds.front() << "\"";
            if (assertionIds.size() > 1) {
                out << ", \"sva_assertion_ids\": [";
                for (size_t assertionIndex = 0; assertionIndex < assertionIds.size(); ++assertionIndex) {
                    if (assertionIndex)
                        out << ", ";
                    out << "\"" << assertionIds[assertionIndex] << "\"";
                }
                out << "]";
            }
        }

        // Recommendation
        out << ", \"recommendation\": \"" << jsonEscape(c.recommendation) << "\"";
        out << ", \"relationship\": \"" << jsonEscape(c.relationship) << "\"";
        out << ", \"rationale\": \"" << jsonEscape(c.rationale) << "\"";
        if (c.timing_basis_ns.has_value())
            out << ", \"timing_basis_ns\": " << c.timing_basis_ns.value();
        else
            out << ", \"timing_basis_ns\": null";
        out << ", \"sdc_false_path\": " << (c.sdc_false_path ? "true" : "false");
        if (c.sdc_max_delay_constraint_ns)
            out << ", \"sdc_max_delay_constraint_ns\": " << *c.sdc_max_delay_constraint_ns;
        else
            out << ", \"sdc_max_delay_constraint_ns\": null";
        out << ", \"sdc_max_delay_datapath_only\": " << (c.sdc_max_delay_datapath_only ? "true" : "false");
        out << ", \"sdc_max_delay_ambiguous\": " << (c.sdc_max_delay_ambiguous ? "true" : "false");

        out << "}";
        if (i + 1 < result_.crossings.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

void ReportGenerator::generateHTML(const std::filesystem::path& output_path, const std::string& top_module) const {
    std::ostringstream json;
    writeJSON(json);
    std::string embedded = json.str();
    for (size_t pos = embedded.find("</"); pos != std::string::npos; pos = embedded.find("</", pos + 3))
        embedded.replace(pos, 2, "<\\/");

    std::string title;
    for (char c : top_module.empty() ? std::string("CDC analysis") : top_module) {
        switch (c) {
        case '&':
            title += "&amp;";
            break;
        case '<':
            title += "&lt;";
            break;
        case '>':
            title += "&gt;";
            break;
        case '"':
            title += "&quot;";
            break;
        case '\'':
            title += "&#39;";
            break;
        default:
            title += c;
            break;
        }
    }

    std::string html(CDC_HTML_TEMPLATE);
    auto replaceAll = [&](const std::string& marker, const std::string& value) {
        size_t pos = 0;
        while ((pos = html.find(marker, pos)) != std::string::npos) {
            html.replace(pos, marker.size(), value);
            pos += value.size();
        }
    };
    replaceAll("{{TOP_MODULE}}", title);
    replaceAll("{{JSON_DATA}}", embedded);
    std::ofstream out(output_path);
    out << html;
}

void ReportGenerator::generateSDC(const std::filesystem::path& output_path) const {
    std::ofstream out(output_path);
    out << "# Auto-generated CDC constraints by sv-cdccheck\n";
    out << "# " << result_.crossings.size() << " crossing(s)\n";
    out << "# No executable timing constraints are emitted. Review CDC structure,\n";
    out << "# cell/pin selectors, clock periods, and physical timing before using suggestions.\n\n";

    for (auto& c : result_.crossings) {
        if (c.category == ViolationCategory::Waived) {
            out << "# WAIVED: " << c.id << " " << c.source_signal << " -> " << c.dest_signal
                << "; a CDC waiver is not a timing exception.\n";
        } else if (c.category == ViolationCategory::Violation) {
            out << "# WARNING: unsynchronized crossing " << c.id << " " << c.source_signal << " -> " << c.dest_signal
                << "; add synchronization before considering timing exceptions.\n";
        } else if (c.sync_type != SyncType::None) {
            const auto period =
                c.dest_domain && c.dest_domain->source ? c.dest_domain->source->period_ns : std::nullopt;
            if (period && std::isfinite(*period) && *period > 0) {
                out << "# REVIEW: set_max_delay " << *period << " -from [get_cells {" << c.source_signal << "}]"
                    << " -to [get_cells {" << c.dest_signal << "}]"
                    << "  ;# SYNCED: " << c.id << "\n";
            } else {
                out << "# REVIEW: SYNCED " << c.id << " " << c.source_signal << " -> " << c.dest_signal
                    << "; destination period unavailable, so no delay is guessed.\n";
            }
        } else {
            out << "# REVIEW: " << c.id << " " << c.source_signal << " -> " << c.dest_signal
                << "; no timing exception is inferred.\n";
        }
    }
}

// Sanitize a hierarchical name into a SystemVerilog identifier. Used for
// generated property names (e.g. "VIOLATION-1" -> "VIOLATION_1") where
// every char that is not alphanumeric or underscore is replaced. SV
// identifiers cannot begin with a digit, so a leading-digit result is
// prefixed with '_'.
static std::string svaSanitize(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 1);
    for (char c : s) {
        const bool isAlnum = (c >= 'a' && c <= 'z') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') ||
                             c == '_';
        r.push_back(isAlnum ? c : '_');
    }
    if (!r.empty() && r.front() >= '0' && r.front() <= '9')
        r.insert(r.begin(), '_');
    return r;
}

// Sanitize a hierarchical signal path so it can be embedded inside an SVA
// expression like `$stable(<expr>)`. Strips leading dots that the analyzer
// may emit when a top-module prefix is missing -- a leading-dot expression
// is rejected by SystemVerilog parsers. Returns true via `out` only when
// the resulting path uses characters that are valid in a hierarchical
// reference (alnum, '_', '$', '.', '[', ']'). Anything else (whitespace,
// operators) means we cannot recover a safe reference and the caller
// should skip the runtime property.
static bool svaExpressionSafe(const std::string& s, std::string& out) {
    out.clear();
    size_t i = 0;
    while (i < s.size() && s[i] == '.') ++i; // strip leading dots
    if (i == s.size())
        return false;
    out.assign(s, i, std::string::npos);
    for (char c : out) {
        const bool isAlnum = (c >= 'a' && c <= 'z') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') ||
                             c == '_';
        const bool isPathChar = c == '.' || c == '[' || c == ']' || c == '$';
        if (!isAlnum && !isPathChar)
            return false;
    }
    return true;
}

static const std::string& svaSignalPath(const FFNode& ff) {
    return ff.declared_path.empty() ? ff.hier_path : ff.declared_path;
}

static const std::string& svaResetPath(const ResetSignal& reset) {
    return reset.declared_path.empty() ? reset.hier_path : reset.declared_path;
}

static std::vector<std::string> svaUniqueIds(const std::vector<CrossingReport>& crossings) {
    std::vector<std::string> names;
    names.reserve(crossings.size());
    std::unordered_set<std::string> used;
    for (const auto& crossing : crossings) {
        const auto base = svaSanitize(crossing.id);
        std::string name = base;
        for (size_t suffix = 2; used.count(name) > 0; ++suffix)
            name = base + "_dup" + std::to_string(suffix);
        used.insert(name);
        names.push_back(std::move(name));
    }
    return names;
}

static std::optional<SvaSyncAssertion> svaSyncAssertion(const AnalysisResult& result, const CrossingReport& crossing,
                                                        const std::string& safe_id) {
    if ((crossing.category != ViolationCategory::Info && crossing.category != ViolationCategory::Caution) ||
        (crossing.sync_type != SyncType::TwoFF && crossing.sync_type != SyncType::ThreeFF) || !crossing.dest_domain ||
        !crossing.dest_domain->source)
        return std::nullopt;

    SvaSyncAssertion assertion;
    assertion.label = "cdc_" + safe_id + (crossing.sync_type == SyncType::TwoFF ? "_2ff" : "_3ff");

    const FFNode* first = nullptr;
    for (const auto& ff : result.ff_nodes) {
        if (ff->hier_path != crossing.dest_signal)
            continue;
        if (first)
            return std::nullopt;
        first = ff.get();
    }
    if (!first || first->domain != crossing.dest_domain || first->declared_path.empty() ||
        !svaExpressionSafe(first->clock_path, assertion.clock))
        return std::nullopt;

    auto nextStage = [&](const FFNode* stage) -> const FFNode* {
        const FFNode* next = nullptr;
        for (const auto& edge : result.edges) {
            if (edge.source != stage || !edge.dest || edge.has_comb_logic || edge.dest->domain != crossing.dest_domain)
                continue;
            if (next && next != edge.dest)
                return nullptr;
            next = edge.dest;
        }
        return next;
    };

    const FFNode* second = nextStage(first);
    if (!second)
        return std::nullopt;
    std::vector<const FFNode*> stages{first, second};
    if (crossing.sync_type == SyncType::ThreeFF) {
        const FFNode* third = nextStage(second);
        if (!third || third == first)
            return std::nullopt;
        stages.push_back(third);
    }

    std::vector<std::string> resetConditions;
    for (size_t i = 1; i < stages.size(); ++i) {
        if (stages[i - 1]->declared_path.empty() || stages[i]->declared_path.empty())
            return std::nullopt;
        std::string prior;
        std::string current;
        if (!svaExpressionSafe(svaSignalPath(*stages[i - 1]), prior) ||
            !svaExpressionSafe(svaSignalPath(*stages[i]), current))
            return std::nullopt;
        assertion.stage_pairs.emplace_back(std::move(prior), std::move(current));

        if (stages[i]->reset) {
            std::string reset;
            if (!svaExpressionSafe(svaResetPath(*stages[i]->reset), reset))
                return std::nullopt;
            const std::string active =
                stages[i]->reset->polarity == ResetSignal::Polarity::ActiveLow ? "!" + reset : reset;
            if (std::find(resetConditions.begin(), resetConditions.end(), active) == resetConditions.end())
                resetConditions.push_back(active);
        }
    }
    for (size_t i = 0; i < resetConditions.size(); ++i) {
        if (i)
            assertion.disable_condition += " || ";
        assertion.disable_condition += resetConditions[i];
    }
    return assertion;
}

static std::optional<SvaFifoGrayAssertion>
svaFifoGrayAssertion(const AnalysisResult& result, const CrossingReport& crossing, const std::string& safe_id) {
    if ((crossing.category != ViolationCategory::Info && crossing.category != ViolationCategory::Caution) ||
        crossing.sync_type == SyncType::None || !crossing.source_domain || !crossing.source_domain->source)
        return std::nullopt;

    const FFNode* source = nullptr;
    for (const auto& ff : result.ff_nodes) {
        if (ff->hier_path != crossing.source_signal)
            continue;
        if (source)
            return std::nullopt;
        source = ff.get();
    }
    if (!source || source->domain != crossing.source_domain || source->primitive_name != "prim_fifo_async" ||
        source->width < 2 || !source->reset || !source->reset->is_async ||
        source->reset->polarity != ResetSignal::Polarity::ActiveLow)
        return std::nullopt;

    const auto dot = source->hier_path.rfind('.');
    if (dot == std::string::npos)
        return std::nullopt;
    const auto leaf = source->hier_path.substr(dot + 1);
    const auto fifoPrefix = source->hier_path.substr(0, dot);
    const auto& emittedSource = svaSignalPath(*source);
    const auto emittedDot = emittedSource.rfind('.');
    if (emittedDot == std::string::npos)
        return std::nullopt;
    const auto emittedPrefix = emittedSource.substr(0, emittedDot);
    std::string syncPrefix;
    std::string expectedReset;
    std::string expectedClock;
    if (leaf == "fifo_wptr_gray_q") {
        syncPrefix = fifoPrefix + ".sync_wptr.";
        expectedReset = ".rst_wr_ni";
        expectedClock = emittedPrefix + ".clk_wr_i";
    } else if (leaf == "fifo_rptr_gray_q") {
        syncPrefix = fifoPrefix + ".sync_rptr.";
        expectedReset = ".rst_rd_ni";
        expectedClock = emittedPrefix + ".clk_rd_i";
    } else {
        return std::nullopt;
    }
    if (!crossing.dest_signal.starts_with(syncPrefix) || !source->reset->hier_path.ends_with(expectedReset))
        return std::nullopt;

    SvaFifoGrayAssertion assertion;
    assertion.label = "cdc_" + safe_id + "_fifo_gray";
    if (!svaExpressionSafe(expectedClock, assertion.clock) ||
        !svaExpressionSafe(svaResetPath(*source->reset), assertion.reset) ||
        !svaExpressionSafe(emittedSource, assertion.signal))
        return std::nullopt;
    return assertion;
}

static std::optional<SvaReqAckAssertion>
svaReqAckAssertion(const AnalysisResult& result, const CrossingReport& crossing, const std::string& safe_id) {
    if ((crossing.category != ViolationCategory::Info && crossing.category != ViolationCategory::Caution) ||
        crossing.sync_type != SyncType::Handshake || !crossing.source_domain || !crossing.dest_domain ||
        !crossing.dest_domain->source)
        return std::nullopt;

    const FFNode* source = nullptr;
    const FFNode* dest = nullptr;
    for (const auto& ff : result.ff_nodes) {
        if (ff->hier_path == crossing.source_signal) {
            if (source)
                return std::nullopt;
            source = ff.get();
        }
        if (ff->hier_path == crossing.dest_signal) {
            if (dest)
                return std::nullopt;
            dest = ff.get();
        }
    }
    if (!source || !dest || source->domain != crossing.source_domain || dest->domain != crossing.dest_domain ||
        source->primitive_name != "prim_sync_reqack" || source->width != 1 || dest->width != 1 || !source->reset ||
        !dest->reset || !source->reset->is_async || !dest->reset->is_async ||
        source->reset->polarity != ResetSignal::Polarity::ActiveLow ||
        dest->reset->polarity != ResetSignal::Polarity::ActiveLow)
        return std::nullopt;

    constexpr std::string_view sourceLeaf = ".src_req_q";
    if (!source->hier_path.ends_with(sourceLeaf))
        return std::nullopt;
    const auto prefix = source->hier_path.substr(0, source->hier_path.size() - sourceLeaf.size());
    const auto& emittedSource = svaSignalPath(*source);
    if (!emittedSource.ends_with(sourceLeaf))
        return std::nullopt;
    const auto emittedPrefix = emittedSource.substr(0, emittedSource.size() - sourceLeaf.size());
    if (source->reset->hier_path != prefix + ".rst_src_ni" ||
        !dest->hier_path.starts_with(prefix + ".req_sync.u_sync_1.") || !dest->reset->hier_path.ends_with(".rst_ni"))
        return std::nullopt;

    auto hasAlias = [&](const std::string& from, const std::string& to) {
        const auto it = result.clock_db.directed_aliases.find(from);
        return it != result.clock_db.directed_aliases.end() &&
               std::find(it->second.begin(), it->second.end(), to) != it->second.end();
    };
    const auto firstReset = prefix + ".req_sync.rst_ni";
    if (!hasAlias(prefix + ".rst_dst_ni", firstReset) || !hasAlias(firstReset, dest->reset->hier_path))
        return std::nullopt;

    SvaReqAckAssertion assertion;
    assertion.label = "cdc_" + safe_id + "_ack_requires_req";
    if (!svaExpressionSafe(emittedPrefix + ".clk_dst_i", assertion.clock) ||
        !svaExpressionSafe(emittedPrefix + ".rst_dst_ni", assertion.reset) ||
        !svaExpressionSafe(emittedPrefix + ".dst_req_o", assertion.request) ||
        !svaExpressionSafe(emittedPrefix + ".dst_ack_i", assertion.acknowledgment))
        return std::nullopt;
    return assertion;
}

static std::optional<SvaReqAckDataAssertion> svaReqAckDataAssertion(const CrossingReport& crossing,
                                                                    const std::string& safe_id) {
    if (crossing.category != ViolationCategory::Caution || crossing.sync_type != SyncType::Handshake ||
        crossing.rule != "Ac_cdc01" || !crossing.reqack_data_contract || !crossing.source_domain ||
        !crossing.dest_domain || crossing.source_domain->isSameDomain(*crossing.dest_domain))
        return std::nullopt;
    const auto& contract = *crossing.reqack_data_contract;
    if (contract.instance_path.empty() || contract.width == 0)
        return std::nullopt;
    const auto& prefix = contract.instance_path;
    if (contract.src_to_dst) {
        if (crossing.source_signal != prefix + ".src_req_i" || crossing.dest_signal != prefix + ".dst_req_o")
            return std::nullopt;
    } else if (crossing.source_signal != prefix + ".dst_ack_i" || crossing.dest_signal != prefix + ".src_ack_o") {
        return std::nullopt;
    }

    SvaReqAckDataAssertion assertion;
    assertion.src_to_dst = contract.src_to_dst;
    assertion.label = "cdc_" + safe_id + (contract.src_to_dst ? "_data_hold_src2dst" : "_data_hold_dst2src");
    if (!svaExpressionSafe(prefix + ".clk_src_i", assertion.clock) ||
        !svaExpressionSafe(prefix + ".rst_src_ni", assertion.reset) ||
        !svaExpressionSafe(prefix + ".src_req_i", assertion.request) ||
        !svaExpressionSafe(prefix + ".src_ack_o", assertion.acknowledgment) ||
        !svaExpressionSafe(prefix + (contract.src_to_dst ? ".data_i" : ".data_o"), assertion.data))
        return std::nullopt;
    return assertion;
}

bool ReportGenerator::generateSVA(const std::filesystem::path& output_path,
                                  const std::string& top_module) const {
    std::ofstream out(output_path);
    if (!out.is_open() || out.fail())
        return false;
    out << "// =====================================================================\n";
    out << "// svlens CDC analysis -- auto-generated SVA file\n";
    if (!top_module.empty())
        out << "// Top:        " << top_module << "\n";
    out << "// Crossings:  " << result_.crossings.size() << "\n";
    out << "// Note: 2FF/3FF assertions check sampled stage transfer only.\n";
    out << "//       prim_fifo_async Gray assertions check pointer encoding only.\n";
    out << "//       prim_sync_reqack ACK assertions check an integration contract.\n";
    out << "//       prim_sync_reqack_data hold assertions check caller data stability.\n";
    out << "//       They do not prove metastability resolution or CDC safety.\n";
    out << "//       Bind scope and hierarchical references must be reviewed.\n";
    out << "// =====================================================================\n\n";
    out << "module svlens_cdc_assertions;\n\n";

    const auto ids = svaUniqueIds(result_.crossings);
    for (size_t i = 0; i < result_.crossings.size(); ++i) {
        const auto& c = result_.crossings[i];
        const auto& id_safe = ids[i];
        const char* category = categoryToString(c.category);
        const char* severity = severityToString(c.severity);
        const char* sync_type = syncTypeToString(c.sync_type);
        const std::string& src_clk = (c.source_domain && c.source_domain->source)
            ? c.source_domain->source->name : std::string("<unknown>");
        const std::string& dst_clk = (c.dest_domain && c.dest_domain->source)
            ? c.dest_domain->source->name : std::string("<unknown>");

        out << "// ----------------------------------------------------------------\n";
        out << "// Crossing  : " << c.id << "\n";
        out << "// Category  : " << category << "\n";
        out << "// Severity  : " << severity << "\n";
        out << "// Rule      : " << (c.rule.empty() ? std::string("-") : c.rule) << "\n";
        out << "// Source    : " << c.source_signal << "  (" << src_clk << ")\n";
        out << "// Dest      : " << c.dest_signal   << "  (" << dst_clk << ")\n";
        out << "// Sync type : " << sync_type << "\n";
        if (!c.waive_reason.empty())
            out << "// Waiver    : " << c.waive_reason << "\n";
        if (!c.rationale.empty())
            out << "// Rationale : " << c.rationale << "\n";
        out << "// ----------------------------------------------------------------\n";

        if (c.category == ViolationCategory::Violation && c.sync_type == SyncType::None) {
            // Unsynchronized crossing: emit a cover property documenting the
            // glitch surface. Cover (not assert) so simulation does not
            // false-fail on legitimate async toggling -- the cover hits when
            // the source signal toggles within a single dst_clk window.
            //
            // The expression body must be a valid SV hierarchical reference;
            // svaExpressionSafe strips leading dots and rejects paths with
            // operator-class characters. When unsafe, fall through to a
            // doc-only block so the report stays parseable.
            std::string src_expr;
            std::string dst_clk_expr;
            const FFNode* destFF = nullptr;
            const FFNode* sourceFF = nullptr;
            bool ambiguousDest = false;
            for (const auto& ff : result_.ff_nodes) {
                if (ff->hier_path == c.source_signal)
                    sourceFF = ff.get();
                if (ff->hier_path != c.dest_signal)
                    continue;
                if (destFF)
                    ambiguousDest = true;
                destFF = ff.get();
            }
            const auto& sourcePath = sourceFF ? svaSignalPath(*sourceFF) : c.source_signal;
            if (!ambiguousDest && destFF && destFF->domain == c.dest_domain && !destFF->declared_path.empty() &&
                (!sourceFF || !sourceFF->declared_path.empty()) && svaExpressionSafe(sourcePath, src_expr) &&
                svaExpressionSafe(destFF->clock_path, dst_clk_expr)) {
                out << "property cdc_" << id_safe << "_src_toggle;\n";
                out << "    @(posedge " << dst_clk_expr << ") "
                    << "!$stable(" << src_expr << ");\n";
                out << "endproperty\n";
                out << "cdc_" << id_safe << "_cover_toggle: cover property (cdc_"
                    << id_safe << "_src_toggle);\n";
            } else {
                out << "// Source path or destination FF clock is unresolved for SVA;\n";
                out << "// cover property skipped. Triage via the JSON report.\n";
            }
        } else {
            const auto stageAssertion = svaSyncAssertion(result_, c, id_safe);
            const auto fifoAssertion = svaFifoGrayAssertion(result_, c, id_safe);
            const auto reqAckAssertion = svaReqAckAssertion(result_, c, id_safe);
            const auto dataHoldAssertion = svaReqAckDataAssertion(c, id_safe);
            if (stageAssertion) {
                out << "property p_" << stageAssertion->label << ";\n";
                out << "    @(posedge " << stageAssertion->clock << ")";
                if (!stageAssertion->disable_condition.empty())
                    out << " disable iff (" << stageAssertion->disable_condition << ")";
                out << "\n        1'b1 |=> (";
                for (size_t stage = 0; stage < stageAssertion->stage_pairs.size(); ++stage) {
                    if (stage)
                        out << " && ";
                    const auto& [prior, current] = stageAssertion->stage_pairs[stage];
                    out << current << " == $past(" << prior << ")";
                }
                out << ");\nendproperty\n";
                out << stageAssertion->label << ": assert property (p_" << stageAssertion->label << ");\n";
            }
            if (fifoAssertion) {
                out << "// Mirrors prim_fifo_async GrayWptr_A / GrayRptr_A; not a CDC safety proof.\n";
                out << "property p_" << fifoAssertion->label << ";\n";
                out << "    @(posedge " << fifoAssertion->clock << ") disable iff ((!" << fifoAssertion->reset
                    << ") !== '0)\n";
                out << "        $countones(" << fifoAssertion->signal << " ^ $past(" << fifoAssertion->signal
                    << ")) <= 1;\nendproperty\n";
                out << fifoAssertion->label << ": assert property (p_" << fifoAssertion->label << ");\n";
            }
            if (reqAckAssertion) {
                out << "// Mirrors prim_sync_reqack SyncReqAckAckNeedsReq; not a liveness or CDC safety proof.\n";
                out << "property p_" << reqAckAssertion->label << ";\n";
                out << "    @(posedge " << reqAckAssertion->clock << ") disable iff ((!" << reqAckAssertion->reset
                    << ") !== '0)\n";
                out << "        " << reqAckAssertion->acknowledgment << " |-> " << reqAckAssertion->request
                    << ";\nendproperty\n";
                out << reqAckAssertion->label << ": assert property (p_" << reqAckAssertion->label << ");\n";
            }
            if (dataHoldAssertion) {
                out << "// Mirrors prim_sync_reqack_data's caller hold contract; not a CDC safety proof.\n";
                out << "property p_" << dataHoldAssertion->label << ";\n";
                out << "    @(posedge " << dataHoldAssertion->clock << ") disable iff ((!" << dataHoldAssertion->reset
                    << ") !== '0)\n";
                if (dataHoldAssertion->src_to_dst) {
                    out << "        !$stable(" << dataHoldAssertion->data << ") |-> (!" << dataHoldAssertion->request
                        << " || (" << dataHoldAssertion->request << " && " << dataHoldAssertion->acknowledgment
                        << "));\n";
                } else {
                    out << "        " << dataHoldAssertion->request << " && " << dataHoldAssertion->acknowledgment
                        << " |-> ($past(" << dataHoldAssertion->data << ", 2) == " << dataHoldAssertion->data
                        << " && $stable(" << dataHoldAssertion->data << ")[*2]);\n";
                }
                out << "endproperty\n";
                out << dataHoldAssertion->label << ": assert property (p_" << dataHoldAssertion->label << ");\n";
            }
            if (!stageAssertion && !fifoAssertion && !reqAckAssertion && !dataHoldAssertion) {
                if (c.sync_type != SyncType::None) {
                    if (c.category == ViolationCategory::Info)
                        out << "// Verified " << sync_type
                            << " synchronizer; no safe generic assertion for this record.\n";
                    else
                        out << "// Review " << sync_type
                            << " pattern; synchronization is not verified for this record.\n";
                } else {
                    out << "// CAUTION/CONVENTION class — review manually.\n";
                }
            }
        }
        out << "\n";
    }
    out << "endmodule\n";
    return true;
}

void ReportGenerator::generateDOT(const std::filesystem::path& output_path) const {
    std::ofstream out(output_path);
    out << "digraph CDC {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box, style=filled];\n\n";

    // Assign colors to domains
    static const char* palette[] = {
        "\"#A3CEF1\"", "\"#E8D5B7\"", "\"#B5EAD7\"", "\"#FFD6E0\"",
        "\"#C3B1E1\"", "\"#FFEAA7\"", "\"#DFE6E9\"", "\"#FAB1A0\""
    };
    constexpr int palette_size = 8;

    std::unordered_map<std::string, int> domain_color_idx;
    int color_counter = 0;
    for (auto& d : result_.clock_db.domains) {
        domain_color_idx[d->canonical_name] = color_counter % palette_size;
        color_counter++;
    }

    // Sanitize node name for DOT (replace dots with underscores)
    auto sanitize = [](const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            out += (c == '.' || c == '[' || c == ']') ? '_' : c;
        }
        return out;
    };

    // Escape strings for DOT label="..." values
    auto dotEscape = [](const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    };

    // Emit FF nodes
    for (auto& ff : result_.ff_nodes) {
        std::string node_id = sanitize(ff->hier_path);
        std::string color = "\"#DFE6E9\""; // default grey
        if (ff->domain) {
            auto it = domain_color_idx.find(ff->domain->canonical_name);
            if (it != domain_color_idx.end()) {
                color = palette[it->second];
            }
        }
        out << "  " << node_id << " [label=\"" << dotEscape(ff->hier_path) << "\"";
        out << ", fillcolor=" << color;
        if (ff->domain)
            out << ", tooltip=\"domain: " << dotEscape(ff->domain->canonical_name) << "\"";
        out << "];\n";
    }

    out << "\n";

    // Emit edges
    for (auto& edge : result_.edges) {
        if (!edge.source || !edge.dest) continue;
        std::string src_id = sanitize(edge.source->hier_path);
        std::string dst_id = sanitize(edge.dest->hier_path);

        bool is_crossing = false;
        if (edge.source->domain && edge.dest->domain) {
            is_crossing = !edge.source->domain->isSameDomain(*edge.dest->domain);
        }

        out << "  " << src_id << " -> " << dst_id;
        if (is_crossing) {
            out << " [color=red, penwidth=2.0, label=\"" << dotEscape("CDC") << "\"]";
        }
        out << ";\n";
    }

    out << "}\n";
}

void ReportGenerator::generateWaiverTemplate(const std::filesystem::path& output_path) const {
    std::ofstream out(output_path);
    out << "waivers:\n";
    int waiver_num = 0;
    for (auto& c : result_.crossings) {
        if (c.category != ViolationCategory::Violation)
            continue;
        waiver_num++;
        char id_buf[24];
        snprintf(id_buf, sizeof(id_buf), "WAIVE-%03d", waiver_num);
        out << "  - id: " << id_buf << "\n";
        out << "    crossing: \"" << c.source_signal << " -> " << c.dest_signal << "\"\n";
        out << "    reason: \"\"\n";
        out << "    owner: \"\"\n";
        out << "    date: \"\"\n";
    }
}

} // namespace sv_cdccheck
