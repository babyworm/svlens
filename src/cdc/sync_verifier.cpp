#include "sv-cdccheck/sync_verifier.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cctype>

namespace sv_cdccheck {

SyncVerifier::SyncVerifier(std::vector<CrossingReport>& crossings,
                           const std::vector<std::unique_ptr<FFNode>>& ff_nodes,
                           const std::vector<FFEdge>& edges,
                           const ClockDatabase* clock_db)
    : crossings_(crossings), ff_nodes_(ff_nodes), edges_(edges), clock_db_(clock_db) {}

bool SyncVerifier::isSingleSourceFF(const FFNode* ff) {
    if (!ff) return false;
    const auto& fanin = ff->fanin_signals;
    // The semantics is "exactly one runtime input known to be
    // tracked." `fanin.size() == 1` is meaningful ONLY when the
    // body was actually walked (`fanin_populated == true`),
    // otherwise a library-cell stub that happens to have one
    // recorded port connection would falsely qualify. The
    // empty-fanin case is the inverse: it's acceptable only when
    // the body was NOT walked (stub), since a walked-but-empty
    // body means no runtime input. (Code-reviewer Round 12 #3.)
    return (fanin.size() == 1 && ff->fanin_populated) ||
           (fanin.empty() && !ff->fanin_populated);
}

const FFNode* SyncVerifier::findNextFF(const FFNode* ff) const {
    // Find an FF in the same domain that is directly fed by this FF
    // with no combinational logic in between (sync chain characteristic).
    auto it = edges_from_.find(ff);
    if (it == edges_from_.end()) return nullptr;

    // Invariant trusted by this loop:
    //   1. `!edge->has_comb_logic` rules out any combinational gate
    //      between source and dest -- direct flop-to-flop only.
    //   2. The FFEdge was built by ConnectivityBuilder via the
    //      port-aware resolveToFFs walker, which threads through
    //      port_map / wire_map / parent_port_chain to verify the
    //      structural path before emitting an edge.
    //   3. `fanin.size() == 1` ensures the dest FF references exactly
    //      one signal in its always_ff body. An EMPTY fanin is only
    //      acceptable when `fanin_populated == false` (library-cell
    //      stub or opaque body where no walk happened) -- if the
    //      walk DID run and still produced empty (e.g., the only
    //      assignment was `q <= ParamReset` and got filtered out),
    //      the FF has no runtime input and must NOT pose as a sync
    //      stage. (Code-reviewer Round 8 Finding #3.)
    // Together (1)+(2)+(3) are sufficient to classify the edge as a
    // single sync stage, even when source_leaf differs from fanin[0]
    // (the cross-instance case where dest reads its local port `d_i`
    // while source's leaf is the upstream instance's `q_o`).
    for (auto* edge : it->second) {
        if (edge->dest &&
            edge->dest->domain == ff->domain &&
            !edge->has_comb_logic &&
            isSingleSourceFF(edge->dest)) {
            return edge->dest;
        }
    }
    return nullptr;
}

SyncType SyncVerifier::detectSyncPattern(const FFNode* dest_ff) const {
    if (!dest_ff) return SyncType::None;

    // Check for 2-FF synchronizer:
    // dest_ff (first sync stage) -> next_ff (second sync stage)
    // Both must be in the same domain with direct FF-to-FF connection
    const FFNode* second = findNextFF(dest_ff);
    if (second) {
        // 2-FF detected! Check for 3-FF
        const FFNode* third = findNextFF(second);
        if (third) return SyncType::ThreeFF;
        return SyncType::TwoFF;
    }

    // Shift-register-style synchronizer (single multi-bit register that
    // performs the chain internally), as used by
    // pulp-platform/common_cells/src/sync.sv:
    //
    //   logic [STAGES-1:0] reg_q;
    //   always_ff @(posedge clk_i, negedge rst_ni)
    //     if (!rst_ni) reg_q <= '0;
    //     else         reg_q <= {reg_q[STAGES-2:0], serial_i};
    //
    // In our connectivity graph this manifests as a single FFNode whose
    // fanin_signals include its own leaf name (the self-shift). Without
    // this branch we miss the sync recognition and over-report VIOLATION
    // on every cdc_fifo_gray-style design that wraps the chain inside
    // sync.sv.
    if (!dest_ff->fanin_signals.empty()) {
        std::string dest_leaf = dest_ff->hier_path;
        auto dot_pos = dest_leaf.rfind('.');
        if (dot_pos != std::string::npos)
            dest_leaf = dest_leaf.substr(dot_pos + 1);

        bool self_shift = false;
        for (auto& sig : dest_ff->fanin_signals) {
            if (sig == dest_leaf) {
                self_shift = true;
                break;
            }
        }
        if (self_shift)
            return SyncType::TwoFF;
    }

    return SyncType::None;
}

const FFEdge* SyncVerifier::findEdge(const std::string& source_signal,
                                      const std::string& dest_signal) const {
    // Use ff_by_path_ to find the source FFNode, then edges_from_ for its edges
    auto ff_it = ff_by_path_.find(source_signal);
    if (ff_it == ff_by_path_.end()) return nullptr;

    auto edge_it = edges_from_.find(ff_it->second);
    if (edge_it == edges_from_.end()) return nullptr;

    for (auto* edge : edge_it->second) {
        if (edge->dest && edge->dest->hier_path == dest_signal) {
            return edge;
        }
    }
    return nullptr;
}

void SyncVerifier::detectReconvergence() {
    // Group crossings by (source_domain, dest_domain) pair
    // Key: source_domain_name + "|" + dest_domain_name
    std::unordered_map<std::string, std::vector<size_t>> domain_pair_crossings;

    for (size_t i = 0; i < crossings_.size(); ++i) {
        auto& c = crossings_[i];
        if (!c.source_domain || !c.dest_domain) continue;

        std::string key = c.source_domain->canonical_name + "|" +
                          c.dest_domain->canonical_name;
        domain_pair_crossings[key].push_back(i);
    }

    // For any pair with 2+ crossings, flag reconvergence
    for (auto& [key, indices] : domain_pair_crossings) {
        if (indices.size() < 2) continue;

        for (auto idx : indices) {
            auto& c = crossings_[idx];
            // Only downgrade synced crossings to CAUTION, don't touch VIOLATIONs
            if (c.sync_type != SyncType::None) {
                // Preserve a more specific rule already set by an earlier
                // pass (e.g. detectCombBeforeSync's Ac_cdc02). Reconvergence
                // is the catch-all for "two synced bits land in the same
                // dest domain" -- if a crossing already has a precise
                // diagnosis, the user should see that, not a generic
                // reconvergence label.
                if (!c.rule.empty() && c.rule != "Ac_cdc01" &&
                    c.rule != "Ac_cdc03") {
                    continue;
                }
                c.category = ViolationCategory::Caution;
                c.severity = Severity::Medium;
                c.id = "CAUTION-" + std::to_string(++caution_counter_);
                c.rule = "Ac_cdc03";
                c.recommendation = "[Ac_cdc03] Reconvergence risk: multiple signals from same "
                    "source domain cross independently. Consider gray code or handshake.";
            }
        }
    }
}

void SyncVerifier::detectCombBeforeSync() {
    for (auto& crossing : crossings_) {
        // Find the edge for this crossing
        const FFEdge* edge = findEdge(crossing.source_signal, crossing.dest_signal);
        if (!edge) continue;

        // If the edge has combinational logic between source and dest FF
        if (edge->has_comb_logic) {
            crossing.category = ViolationCategory::Caution;
            crossing.severity = Severity::Medium;
            if (crossing.id.find("CAUTION") == std::string::npos)
                crossing.id = "CAUTION-" + std::to_string(++caution_counter_);
            crossing.rule = "Ac_cdc02";
            crossing.recommendation = "[Ac_cdc02, Ac_glitch01] Combinational logic before sync FF introduces "
                "glitch risk. Drive synchronizer input directly from a source-domain FF.";
        }
    }
}

void SyncVerifier::detectResetSyncIssues() {
    // For each FF, check if its async reset originates from a different clock domain.
    // If so, check whether that reset signal is properly synchronized (has a 2-FF
    // sync chain in the crossing list).

    // Build a set of source signals that have synced crossings, keyed by
    // "source_signal|dest_domain_name" to account for which destination
    // domain the sync is for.
    std::unordered_set<std::string> synced_signals;
    for (auto& c : crossings_) {
        if (c.sync_type != SyncType::None && c.dest_domain) {
            synced_signals.insert(c.source_signal + "|" +
                                  c.dest_domain->canonical_name);
        }
    }

    // Build map keyed by leaf signal name for O(1) reset-source lookups.
    // Key: leaf name (after last '.'), Value: list of FFNodes with that leaf name.
    std::unordered_map<std::string, std::vector<const FFNode*>> ff_by_leaf;
    for (auto& ff : ff_nodes_) {
        std::string leaf = ff->hier_path;
        auto dot_pos = leaf.rfind('.');
        if (dot_pos != std::string::npos)
            leaf = leaf.substr(dot_pos + 1);
        ff_by_leaf[leaf].push_back(ff.get());
    }

    // Build index for existing crossings: "source|dest" -> index
    std::unordered_map<std::string, size_t> crossing_index;
    for (size_t i = 0; i < crossings_.size(); ++i) {
        crossing_index[crossings_[i].source_signal + "|" + crossings_[i].dest_signal] = i;
    }

    for (auto& ff : ff_nodes_) {
        if (!ff->reset || !ff->reset->is_async || !ff->domain) continue;

        // Find the FF that generates the reset signal using map-based lookups
        const FFNode* reset_source_ff = nullptr;

        // Try exact path match first
        auto exact_it = ff_by_path_.find(ff->reset->hier_path);
        if (exact_it != ff_by_path_.end()) {
            const FFNode* candidate = exact_it->second;
            if (candidate->domain && !candidate->domain->isSameDomain(*ff->domain)) {
                reset_source_ff = candidate;
            }
        }

        // Try leaf-name match if exact match failed
        if (!reset_source_ff) {
            std::string reset_leaf = ff->reset->hier_path;
            auto dot_pos = reset_leaf.rfind('.');
            if (dot_pos != std::string::npos)
                reset_leaf = reset_leaf.substr(dot_pos + 1);

            auto leaf_it = ff_by_leaf.find(reset_leaf);
            if (leaf_it != ff_by_leaf.end()) {
                for (auto* candidate : leaf_it->second) {
                    if (candidate->domain && !candidate->domain->isSameDomain(*ff->domain)) {
                        reset_source_ff = candidate;
                        break;
                    }
                }
            }
        }

        if (!reset_source_ff) continue;

        // Check if there's already a synced crossing for this reset signal
        // to this specific destination domain
        std::string sync_key = reset_source_ff->hier_path + "|" +
            (ff->domain ? ff->domain->canonical_name : "");
        bool is_synced = synced_signals.count(sync_key) > 0;

        if (!is_synced) {
            // Check if we already have a crossing for this pair
            std::string pair_key = reset_source_ff->hier_path + "|" + ff->hier_path;
            auto cx_it = crossing_index.find(pair_key);
            if (cx_it != crossing_index.end()) {
                // Update existing crossing
                auto& c = crossings_[cx_it->second];
                c.category = ViolationCategory::Caution;
                c.severity = Severity::High;
                c.id = "CAUTION-" + std::to_string(++caution_counter_);
                c.rule = "Ac_cdc06";
                c.recommendation = "[Ac_cdc06] Async reset from different clock domain without "
                    "reset synchronizer. Use async-assert, sync-deassert pattern.";
            } else {
                // Add a new crossing report for the reset issue
                CrossingReport report;
                report.source_domain = reset_source_ff->domain;
                report.dest_domain = ff->domain;
                report.source_signal = reset_source_ff->hier_path;
                report.dest_signal = ff->hier_path;
                report.sync_type = SyncType::None;
                report.category = ViolationCategory::Caution;
                report.severity = Severity::High;
                report.id = "CAUTION-" + std::to_string(++caution_counter_);
                report.rule = "Ac_cdc06";
                report.recommendation = "[Ac_cdc06] Async reset from different clock domain without "
                    "reset synchronizer. Use async-assert, sync-deassert pattern.";
                // Update crossing_index for newly added crossing
                crossing_index[pair_key] = crossings_.size();
                crossings_.push_back(std::move(report));
            }
        }
    }
}

void SyncVerifier::analyze() {
    // Build hash indexes for O(1) lookups
    ff_by_path_.clear();
    for (auto& ff : ff_nodes_) {
        ff_by_path_[ff->hier_path] = ff.get();
    }
    edges_from_.clear();
    for (auto& edge : edges_) {
        if (edge.source) {
            edges_from_[edge.source].push_back(&edge);
        }
    }

    // Phase 1: Detect sync patterns (existing)
    for (auto& crossing : crossings_) {
        // Find the dest FF node for this crossing via hash index
        const FFNode* dest_ff = nullptr;
        auto it = ff_by_path_.find(crossing.dest_signal);
        if (it != ff_by_path_.end()) {
            dest_ff = it->second;
        }

        crossing.sync_type = detectSyncPattern(dest_ff);

        // Update category based on sync detection
        if (crossing.sync_type != SyncType::None) {
            // Determine if the sync chain meets the required stages
            int stages = 0;
            if (crossing.sync_type == SyncType::TwoFF) stages = 2;
            else if (crossing.sync_type == SyncType::ThreeFF) stages = 3;
            else stages = required_stages_; // other types always qualify

            if (stages >= required_stages_) {
                crossing.category = ViolationCategory::Info;
                crossing.severity = Severity::Info;
                crossing.recommendation.clear();
                crossing.id = "INFO-" + std::to_string(++info_counter_);
            } else {
                crossing.category = ViolationCategory::Caution;
                crossing.severity = Severity::Medium;
                crossing.id = "CAUTION-" + std::to_string(++caution_counter_);
                crossing.recommendation = "Synchronizer has " +
                    std::to_string(stages) + " stages but " +
                    std::to_string(required_stages_) + " required.";
            }
        }
    }

    // Phase 2: Detect combinational logic before sync FF
    detectCombBeforeSync();

    // Phase 3: Detect reconvergence
    detectReconvergence();

    // Phase 3b: Detect wide-bus crossings without gray code or handshake.
    // A multi-bit register crossing through a plain TwoFF/ThreeFF chain
    // suffers per-bit metastability skew at the sync output -- some bits
    // resolve a cycle before others, so the receiver can momentarily see
    // intermediate values. Cummings' canonical fix is gray coding (only
    // one bit changes at a time) or single-bit handshake. Flag this as
    // Ac_cdc04 so users see the hazard. Skip when the crossing already
    // has a more specific rule (e.g., Ac_cdc02 / Ac_cdc06) and skip
    // signals that show a clear gray-coding hint (name contains "gray"
    // or "gry") so legitimate async-FIFO pointer crossings are not
    // false-flagged. The svlens GrayCode classifier requires an explicit
    // XOR-tree pattern that user code does not always expose; the name
    // heuristic is a conservative fallback.
    // Look at the leaf signal name only (chars after the last `.`); the
    // enclosing module path can legitimately contain words like "gray"
    // (e.g. fixture 15 `bus_cdc_no_gray.data_a`) and we don't want to
    // suppress the bus-CDC rule on those.
    // Round 34 CDC-1/CDC-2 fix: extend the handshake/gray naming
    // heuristic to cover modern industry idioms (AXI valid/ready,
    // generic start/done, FIFO enq/deq, push/pop) in addition to the
    // existing gray/req/ack matches. Substring-based matching is
    // preserved (existing fixtures like fixture 36 rely on "rgray"
    // matching as gray-coded). Word-boundary anchoring would reduce
    // false positives like "request_buffer"/"ack_count" but at the
    // cost of false negatives on legitimate single-letter prefixes
    // (rgray/wgray/r_gray/wq2_rgray); a separate audit is needed.
    auto looks_like_gray_or_handshake = [](const std::string& signal_path) {
        std::string leaf = signal_path;
        auto pos = leaf.rfind('.');
        if (pos != std::string::npos) {
            leaf = leaf.substr(pos + 1);
        }
        std::string lower(leaf);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return lower.find("gray") != std::string::npos ||
               lower.find("gry") != std::string::npos ||
               lower.find("req") != std::string::npos ||
               lower.find("ack") != std::string::npos ||
               lower.find("valid") != std::string::npos ||
               lower.find("ready") != std::string::npos ||
               lower.find("start") != std::string::npos ||
               lower.find("done") != std::string::npos ||
               lower.find("enq") != std::string::npos ||
               lower.find("deq") != std::string::npos ||
               lower.find("push") != std::string::npos ||
               lower.find("pop") != std::string::npos;
    };
    for (auto& c : crossings_) {
        if (c.sync_type != SyncType::TwoFF && c.sync_type != SyncType::ThreeFF)
            continue;
        if (!c.rule.empty() && c.rule != "Ac_cdc01" && c.rule != "Ac_cdc03")
            continue;
        auto src_it = ff_by_path_.find(c.source_signal);
        if (src_it == ff_by_path_.end()) continue;
        if (src_it->second->width < 2) continue;
        if (looks_like_gray_or_handshake(c.source_signal)) continue;
        c.category = ViolationCategory::Caution;
        c.severity = Severity::Medium;
        c.id = "CAUTION-" + std::to_string(++caution_counter_);
        c.rule = "Ac_cdc04";
        c.recommendation = "[Ac_cdc04] Wide-bus crossing without gray code or "
            "handshake. Per-bit skew at the sync output may produce "
            "intermediate values. Consider gray coding the bus or gating "
            "with a single-bit handshake.";
    }

    // Phase 4: Detect reset synchronizer issues
    detectResetSyncIssues();

    // Phase 5: Detect advanced synchronizer patterns (upgrade TwoFF/ThreeFF)
    detectGrayCodePattern();
    detectHandshakePattern();
    detectPulseSyncPattern();
    detectMuxSyncPattern();

    // Phase 6: Detect fan-out before sync completion
    detectFanoutBeforeSync();

    // Phase 7: Detect Johnson counter pattern (before non-pow2 check,
    // since Johnson counters are valid for non-pow2 depths)
    detectJohnsonCounter();

    // Phase 8: Detect non-power-of-2 FIFO depth
    detectNonPow2FIFO();

    // Phase 9: Detect clock used as data [Ac_cdc09]
    detectClockAsData();

    // Phase 10: Detect same signal crossing to multiple domains [Ac_cdc11]
    detectMultiDomainCrossing();

    // Phase 11: Detect quasi-static signals [Ac_cdc12]
    detectQuasiStaticSignals();
}

// ─── Advanced synchronizer pattern detection ───

/// Extract common prefix from a signal name, stripping numeric suffix/index.
/// e.g. "top.gray_ptr[2]" → "top.gray_ptr"
///      "top.gray_ptr_2"  → "top.gray_ptr_"
static std::string extractPrefix(const std::string& sig) {
    // Strip trailing [N]
    auto bracket = sig.rfind('[');
    if (bracket != std::string::npos && sig.back() == ']')
        return sig.substr(0, bracket);

    // Strip trailing digits
    auto pos = sig.size();
    while (pos > 0 && std::isdigit(static_cast<unsigned char>(sig[pos - 1])))
        --pos;
    if (pos < sig.size() && pos > 0)
        return sig.substr(0, pos);

    return sig;
}

void SyncVerifier::detectGrayCodePattern() {
    // Group synced crossings by (source_domain, dest_domain) pair
    // Key: source_domain_name + "|" + dest_domain_name
    std::unordered_map<std::string, std::vector<size_t>> domain_pair_crossings;

    for (size_t i = 0; i < crossings_.size(); ++i) {
        auto& c = crossings_[i];
        if (!c.source_domain || !c.dest_domain) continue;
        if (c.sync_type != SyncType::TwoFF && c.sync_type != SyncType::ThreeFF) continue;

        std::string key = c.source_domain->canonical_name + "|" +
                          c.dest_domain->canonical_name;
        domain_pair_crossings[key].push_back(i);
    }

    for (auto& [key, indices] : domain_pair_crossings) {
        if (indices.size() < 3) continue;

        // Check if source signals share a common prefix with numeric suffix
        std::unordered_map<std::string, std::vector<size_t>> prefix_groups;
        for (auto idx : indices) {
            std::string prefix = extractPrefix(crossings_[idx].source_signal);
            prefix_groups[prefix].push_back(idx);
        }

        for (auto& [prefix, group_indices] : prefix_groups) {
            if (group_indices.size() < 3) continue;

            // Check if the leaf signal name matches FIFO pointer naming.
            // Use only the leaf portion (after last '.') to avoid false matches
            // from module names.
            bool is_fifo = false;
            std::string leaf_prefix = prefix;
            auto last_dot = leaf_prefix.rfind('.');
            if (last_dot != std::string::npos)
                leaf_prefix = leaf_prefix.substr(last_dot + 1);
            std::string lower_leaf = leaf_prefix;
            std::transform(lower_leaf.begin(), lower_leaf.end(),
                           lower_leaf.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (auto& pat : {"ptr", "addr", "wr_ptr", "rd_ptr",
                              "wptr", "rptr", "fifo"}) {
                if (lower_leaf.find(pat) != std::string::npos) {
                    is_fifo = true;
                    break;
                }
            }

            SyncType type = is_fifo ? SyncType::AsyncFIFO : SyncType::GrayCode;
            std::string rec = is_fifo
                ? "Async FIFO gray-coded pointer synchronizer detected."
                : "Gray code synchronizer detected.";

            for (auto idx : group_indices) {
                crossings_[idx].sync_type = type;
                crossings_[idx].recommendation = rec;
            }
        }
    }
}

void SyncVerifier::detectHandshakePattern() {
    // Build map: "A|B" → list of synced crossing indices
    std::unordered_map<std::string, std::vector<size_t>> pair_map;
    for (size_t i = 0; i < crossings_.size(); ++i) {
        auto& c = crossings_[i];
        if (!c.source_domain || !c.dest_domain) continue;
        if (c.sync_type != SyncType::TwoFF && c.sync_type != SyncType::ThreeFF) continue;

        std::string key = c.source_domain->canonical_name + "|" +
                          c.dest_domain->canonical_name;
        pair_map[key].push_back(i);
    }

    // For each pair A→B, check if B→A also has a synced crossing
    std::unordered_set<size_t> handshake_indices;
    for (auto& [key, indices] : pair_map) {
        auto sep = key.find('|');
        std::string dom_a = key.substr(0, sep);
        std::string dom_b = key.substr(sep + 1);
        if (dom_a == dom_b) continue;

        std::string reverse_key = dom_b + "|" + dom_a;
        auto it = pair_map.find(reverse_key);
        if (it == pair_map.end()) continue;

        // Both directions have synced crossings. Check for req/ack naming.
        bool has_req = false, has_ack = false;
        for (auto idx : indices) {
            auto& src = crossings_[idx].source_signal;
            if (src.find("req") != std::string::npos) has_req = true;
        }
        for (auto idx : it->second) {
            auto& src = crossings_[idx].source_signal;
            if (src.find("ack") != std::string::npos) has_ack = true;
        }

        if (has_req && has_ack) {
            // Strong match: req/ack naming
            for (auto idx : indices) handshake_indices.insert(idx);
            for (auto idx : it->second) handshake_indices.insert(idx);
        }
        // Removed: overly aggressive else branch that classified any
        // bidirectional synced crossing as Handshake without req/ack evidence
    }

    for (auto idx : handshake_indices) {
        crossings_[idx].sync_type = SyncType::Handshake;
        crossings_[idx].recommendation = "Handshake synchronizer detected.";
    }
}

void SyncVerifier::detectPulseSyncPattern() {
    // For each crossing with 2-FF sync, check if the sync chain output
    // feeds into a XOR/XNOR with a delayed version (detected via fanin_signals).
    for (auto& crossing : crossings_) {
        if (crossing.sync_type != SyncType::TwoFF &&
            crossing.sync_type != SyncType::ThreeFF)
            continue;

        // Find the dest FF (first sync stage) via hash index
        auto ff_it = ff_by_path_.find(crossing.dest_signal);
        if (ff_it == ff_by_path_.end()) continue;
        const FFNode* dest_ff = ff_it->second;

        // Walk the sync chain to find the last stage
        const FFNode* second = findNextFF(dest_ff);
        if (!second) continue;

        const FFNode* last_sync = second;
        const FFNode* third = findNextFF(second);
        if (third) last_sync = third;

        auto edge_it = edges_from_.find(last_sync);
        if (edge_it == edges_from_.end()) continue;

        // Build the set of chain FFs (the synced source plus its
        // delay copies). A true pulse-sync XOR edge detector reads
        // BOTH the last sync output AND a delayed copy in the same
        // always_ff body, so the candidate dest FF must have edges
        // incoming from at least TWO distinct chain FFs.
        //
        // Why not just `fanin.size() >= 2` like the earlier
        // relaxation? Because a downstream FF may have two fanin
        // signals where only ONE comes from the sync chain (e.g.,
        // `q <= synced & local_en`); that's a data merge, not a
        // pulse synchronizer, and would be silently false-classified
        // as PulseSync if we only counted dest-side fanin.
        // (Code-reviewer Round 8 Finding #1.)
        //
        // Limitation: the forward extension below walks ONE hop
        // from last_sync. Designs that tap the XOR pair through a
        // multi-hop delay register (rare, e.g. `s3 -> s4 -> XOR
        // with s4_d`) will not have the deeper delay register in
        // chain_ffs. The canonical Cummings 2008 / pulp_sync_wedge
        // pattern is one-hop and fully covered.
        std::unordered_set<const FFNode*> chain_ffs;
        chain_ffs.insert(dest_ff);
        chain_ffs.insert(second);
        if (third) chain_ffs.insert(third);
        // Bounded BFS forward extension: any same-domain FF
        // reachable from a chain FF via a no-comb single-source
        // edge is part of the delay path. Walks up to MAX_DEPTH
        // hops (covers `s1->s2->s3->s4->s4_d` plus extra delay taps
        // that real designs add for deeper XOR debouncing).
        //
        // MAX_DEPTH=6 is chosen because Cummings (SNUG 2008,
        // "Clock Domain Crossing (CDC) Design & Verification
        // Techniques Using SystemVerilog") describes pulse
        // synchronizers using at most 3-stage sync chains with
        // 1-2 additional delay taps for the XOR edge detector --
        // 6 hops covers the deepest practical configuration. The
        // bound also prevents pathological explosion on dense
        // graphs. (Round 10 US-803.)
        constexpr size_t MAX_DEPTH = 6;
        std::vector<const FFNode*> frontier{last_sync};
        for (size_t depth = 0; depth < MAX_DEPTH && !frontier.empty(); ++depth) {
            std::vector<const FFNode*> next_frontier;
            for (auto* node : frontier) {
                auto fe_it = edges_from_.find(node);
                if (fe_it == edges_from_.end()) continue;
                for (auto* fwd_edge : fe_it->second) {
                    if (!fwd_edge->dest) continue;
                    if (fwd_edge->dest->domain != last_sync->domain) continue;
                    if (fwd_edge->has_comb_logic) continue;
                    if (!isSingleSourceFF(fwd_edge->dest)) continue;
                    if (chain_ffs.insert(fwd_edge->dest).second)
                        next_frontier.push_back(fwd_edge->dest);
                }
            }
            frontier = std::move(next_frontier);
        }

        // Collect candidate XOR-consumer FFs: any same-domain FF
        // with fanin.size() >= 2 reachable from ANY chain FF (not
        // just last_sync). For deep chains where the XOR taps live
        // multiple hops downstream, the consumer's incoming edge
        // comes from `s4`/`s4_d` -- not from the original last_sync.
        std::unordered_set<const FFNode*> candidates;
        for (auto* chain_ff : chain_ffs) {
            auto ce_it = edges_from_.find(chain_ff);
            if (ce_it == edges_from_.end()) continue;
            for (auto* ce : ce_it->second) {
                if (!ce->dest) continue;
                if (ce->dest->domain != last_sync->domain) continue;
                if (ce->dest->fanin_signals.size() < 2) continue;
                candidates.insert(ce->dest);
            }
        }
        for (auto* candidate : candidates) {
            // Count distinct chain FFs that have a direct edge to
            // this candidate. Require >= 2 for the XOR pair
            // signature.
            size_t chain_inputs = 0;
            for (auto* chain_ff : chain_ffs) {
                auto ce_it = edges_from_.find(chain_ff);
                if (ce_it == edges_from_.end()) continue;
                for (auto* ce : ce_it->second) {
                    if (ce->dest == candidate) { ++chain_inputs; break; }
                }
            }
            if (chain_inputs < 2) continue;
            crossing.sync_type = SyncType::PulseSync;
            crossing.recommendation = "Pulse synchronizer detected.";
            break;
        }
    }
}

void SyncVerifier::detectMuxSyncPattern() {
    // Build a set of synced source signals per dest domain for quick lookup.
    // Key: "source_leaf|dest_domain_name"
    std::unordered_set<std::string> synced_sources;
    for (auto& c : crossings_) {
        if (c.sync_type == SyncType::TwoFF || c.sync_type == SyncType::ThreeFF) {
            if (c.dest_domain) {
                std::string src_leaf = c.source_signal;
                auto dot = src_leaf.rfind('.');
                if (dot != std::string::npos)
                    src_leaf = src_leaf.substr(dot + 1);
                synced_sources.insert(src_leaf + "|" +
                                      c.dest_domain->canonical_name);
            }
        }
    }

    for (size_t i = 0; i < crossings_.size(); ++i) {
        auto& c = crossings_[i];
        if (c.sync_type != SyncType::TwoFF && c.sync_type != SyncType::ThreeFF)
            continue;
        if (!c.dest_domain) continue;

        // Find the dest FF
        auto ff_it = ff_by_path_.find(c.dest_signal);
        if (ff_it == ff_by_path_.end()) continue;
        const FFNode* dest_ff = ff_it->second;

        // MUX sync heuristic: dest FF has 2+ fanin signals AND one of those
        // fanin signals appears as a synced source in another crossing to the
        // same dest domain.
        if (dest_ff->fanin_signals.size() < 2) continue;

        bool has_synced_fanin = false;
        for (auto& fanin : dest_ff->fanin_signals) {
            std::string key = fanin + "|" + c.dest_domain->canonical_name;
            if (synced_sources.count(key)) {
                has_synced_fanin = true;
                break;
            }
        }

        if (has_synced_fanin) {
            c.sync_type = SyncType::MuxSync;
            c.recommendation = "MUX synchronizer detected: select controlled by synced signal.";
        }
    }
}

void SyncVerifier::detectFanoutBeforeSync() {
    for (auto& crossing : crossings_) {
        if (crossing.sync_type != SyncType::TwoFF &&
            crossing.sync_type != SyncType::ThreeFF)
            continue;

        // Find the first sync FF via hash index
        auto ff_it = ff_by_path_.find(crossing.dest_signal);
        if (ff_it == ff_by_path_.end()) continue;
        const FFNode* first_sync = ff_it->second;

        // Check if the first sync FF's output feeds any FF other than
        // the second sync stage
        int fanout_count = 0;
        auto edge_it = edges_from_.find(first_sync);
        if (edge_it != edges_from_.end()) {
            for (auto* edge : edge_it->second) {
                if (edge->dest) {
                    fanout_count++;
                }
            }
        }

        // A proper sync chain has exactly 1 fanout (to second stage)
        if (fanout_count > 1) {
            crossing.category = ViolationCategory::Caution;
            crossing.severity = Severity::Medium;
            if (crossing.id.find("CAUTION") == std::string::npos)
                crossing.id = "CAUTION-" + std::to_string(++caution_counter_);
            crossing.rule = "Ac_cdc05";
            crossing.recommendation = "[Ac_cdc05] Data used before completing sync chain. "
                "First sync FF has multiple fanouts.";
        }
    }
}

void SyncVerifier::detectNonPow2FIFO() {
    // For each crossing classified as AsyncFIFO or GrayCode, check if
    // a non-power-of-2 depth can be inferred. Two approaches:
    //   1. Count how many crossings share the same prefix (= bit width of pointer)
    //      and check if 2^width matches a natural overflow (power-of-2 depth).
    //   2. Heuristic: if the source signal name contains a comparison-based
    //      wrap pattern (ptr == CONST → ptr <= 0), flag non-pow2.
    //
    // Limitation: This does not inspect RTL parameters from parent modules.
    // It relies on the pointer bit width and crossing count.

    // Group AsyncFIFO/GrayCode crossings by source prefix
    std::unordered_map<std::string, std::vector<size_t>> prefix_groups;
    for (size_t i = 0; i < crossings_.size(); ++i) {
        auto& c = crossings_[i];
        if (c.sync_type != SyncType::AsyncFIFO &&
            c.sync_type != SyncType::GrayCode)
            continue;

        std::string prefix = extractPrefix(c.source_signal);
        prefix_groups[prefix].push_back(i);
    }

    for (auto& [prefix, indices] : prefix_groups) {
        if (indices.empty()) continue;

        // bit_width = number of signals sharing this prefix
        uint64_t bit_width = indices.size();

        // Check if the source FF has fanin suggesting a wrap-around comparison
        // (i.e., non-natural overflow → non-power-of-2 depth).
        // Heuristic: look for any source FF whose fanin contains a constant
        // or a signal matching "depth", "size", "entries" patterns.
        bool has_wrap_comparison = false;
        for (auto idx : indices) {
            auto ff_it = ff_by_path_.find(crossings_[idx].source_signal);
            if (ff_it == ff_by_path_.end()) continue;
            const FFNode* src_ff = ff_it->second;
            for (auto& fanin : src_ff->fanin_signals) {
                std::string lower_fanin = fanin;
                std::transform(lower_fanin.begin(), lower_fanin.end(),
                               lower_fanin.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower_fanin.find("depth") != std::string::npos ||
                    lower_fanin.find("size") != std::string::npos ||
                    lower_fanin.find("entries") != std::string::npos) {
                    has_wrap_comparison = true;
                    break;
                }
            }
            if (has_wrap_comparison) break;
        }

        // If bit_width is such that 2^bit_width is NOT the natural depth
        // (i.e., the pointer width could hold more states than needed),
        // and there's evidence of wrap-around, flag it.
        // With pure natural overflow (no wrap), 2^bit_width IS the depth → power of 2.
        // With explicit wrap, the depth is likely non-power-of-2.
        if (has_wrap_comparison) {
            for (auto idx : indices) {
                auto& c = crossings_[idx];
                // Don't downgrade Johnson counters
                if (c.sync_type == SyncType::JohnsonCounter) continue;

                c.category = ViolationCategory::Caution;
                c.severity = Severity::Medium;
                if (c.id.find("CAUTION") == std::string::npos)
                    c.id = "CAUTION-" + std::to_string(++caution_counter_);
                c.rule = "Ac_cdc07";
                c.recommendation = "[Ac_cdc07] Non-power-of-2 FIFO depth suspected (pointer width " +
                    std::to_string(bit_width) + " bits with wrap-around logic). "
                    "Verify Gray code wrap logic or use Johnson counter encoding.";
            }
        }
    }
}

void SyncVerifier::detectJohnsonCounter() {
    // Johnson counter heuristic:
    // A Johnson counter with N states uses 2*N bits. The register shifts with
    // feedback: {~q[MSB], q[MSB:1]}. Only 1 bit changes per clock cycle.
    //
    // Detection approach:
    // 1. For multi-bit crossings (AsyncFIFO/GrayCode), check if bit_width
    //    is unusually large (>= 2x expected for the depth).
    // 2. Check if the source FF's fanin includes its own shifted version
    //    (suggesting a shift register with feedback).
    //
    // Limitation: fanin-based detection is heuristic and may produce
    // false positives on other shift-register patterns.

    // Group AsyncFIFO/GrayCode crossings by source prefix
    std::unordered_map<std::string, std::vector<size_t>> prefix_groups;
    for (size_t i = 0; i < crossings_.size(); ++i) {
        auto& c = crossings_[i];
        if (c.sync_type != SyncType::AsyncFIFO &&
            c.sync_type != SyncType::GrayCode &&
            c.sync_type != SyncType::TwoFF &&
            c.sync_type != SyncType::ThreeFF)
            continue;

        std::string prefix = extractPrefix(c.source_signal);
        prefix_groups[prefix].push_back(i);
    }

    for (auto& [prefix, indices] : prefix_groups) {
        uint64_t bit_width = indices.size();
        if (bit_width < 4) continue;  // Johnson counter needs at least 4 bits (2 states)

        // Check if source FFs show shift-register-with-negated-feedback pattern.
        // In a Johnson counter, each FF's fanin includes the previous FF in the
        // chain, and the first FF's fanin includes a negated version of the last.
        bool has_shift_pattern = false;
        for (auto idx : indices) {
            auto ff_it = ff_by_path_.find(crossings_[idx].source_signal);
            if (ff_it == ff_by_path_.end()) continue;
            const FFNode* src_ff = ff_it->second;

            // Check if fanin contains another signal from the same prefix group
            // (shift register characteristic)
            for (auto& fanin : src_ff->fanin_signals) {
                std::string fanin_prefix = extractPrefix(fanin);
                // Check if fanin is from the same register group (leaf name match)
                std::string leaf_prefix = prefix;
                auto last_dot = leaf_prefix.rfind('.');
                if (last_dot != std::string::npos)
                    leaf_prefix = leaf_prefix.substr(last_dot + 1);

                std::string fanin_leaf = fanin;
                auto fanin_dot = fanin_leaf.rfind('.');
                if (fanin_dot != std::string::npos)
                    fanin_leaf = fanin_leaf.substr(fanin_dot + 1);

                // Strip index from fanin leaf for comparison
                auto fanin_bracket = fanin_leaf.rfind('[');
                if (fanin_bracket != std::string::npos)
                    fanin_leaf = fanin_leaf.substr(0, fanin_bracket);
                // Strip trailing digits
                auto fpos = fanin_leaf.size();
                while (fpos > 0 && std::isdigit(static_cast<unsigned char>(fanin_leaf[fpos - 1])))
                    --fpos;
                if (fpos < fanin_leaf.size() && fpos > 0)
                    fanin_leaf = fanin_leaf.substr(0, fpos);

                if (fanin_leaf == leaf_prefix) {
                    has_shift_pattern = true;
                    break;
                }
            }
            if (has_shift_pattern) break;
        }

        // Johnson counter: bit_width should be even, and if we can determine
        // expected depth, bit_width >= 2 * depth.
        // Without explicit depth parameter, use the even-width + shift pattern heuristic.
        bool is_even = (bit_width % 2 == 0);

        if (has_shift_pattern && is_even && bit_width >= 4) {
            for (auto idx : indices) {
                auto& c = crossings_[idx];
                c.sync_type = SyncType::JohnsonCounter;
                c.category = ViolationCategory::Info;
                c.severity = Severity::Info;
                c.id = "INFO-" + std::to_string(++info_counter_);
                c.recommendation = "Johnson counter synchronizer detected (" +
                    std::to_string(bit_width) + "-bit for " +
                    std::to_string(bit_width / 2) + " states). "
                    "Valid single-bit-change encoding for non-power-of-2 depths.";
            }
        }
    }
}

void SyncVerifier::detectClockAsData() {
    if (!clock_db_) return;

    // Build a set of known clock signal names (source names and origin signals)
    std::unordered_set<std::string> clock_names;
    for (auto& src : clock_db_->sources) {
        if (!src->name.empty()) clock_names.insert(src->name);
        if (!src->origin_signal.empty()) clock_names.insert(src->origin_signal);
    }
    for (auto& net : clock_db_->nets) {
        // Extract leaf name from hier_path
        std::string leaf = net->hier_path;
        auto dot = leaf.rfind('.');
        if (dot != std::string::npos) leaf = leaf.substr(dot + 1);
        clock_names.insert(leaf);
    }

    if (clock_names.empty()) return;

    // Check all FFs: if any fanin signal is a known clock (other than its own), flag it
    for (auto& ff : ff_nodes_) {
        // Determine the FF's own clock name to skip it
        std::string own_clock;
        if (ff->domain && ff->domain->source) {
            own_clock = ff->domain->source->origin_signal;
            if (own_clock.empty()) own_clock = ff->domain->source->name;
        }

        for (auto& fanin : ff->fanin_signals) {
            // Skip the FF's own clock (that's normal, not a data-path issue)
            if (!own_clock.empty() && fanin == own_clock) continue;

            if (clock_names.count(fanin) > 0) {
                // Clock signal used as data input
                CrossingReport report;
                report.source_domain = nullptr;
                report.dest_domain = ff->domain;
                report.source_signal = fanin;
                report.dest_signal = ff->hier_path;
                report.sync_type = SyncType::None;
                report.category = ViolationCategory::Caution;
                report.severity = Severity::Medium;
                report.id = "CAUTION-" + std::to_string(++caution_counter_);
                report.rule = "Ac_cdc09";
                report.recommendation = "[Ac_cdc09] Clock signal used as data input";
                crossings_.push_back(std::move(report));
                break; // one report per FF
            }
        }
    }
}

void SyncVerifier::detectMultiDomainCrossing() {
    // Group crossings by source_signal
    std::unordered_map<std::string, std::vector<size_t>> source_groups;
    for (size_t i = 0; i < crossings_.size(); ++i) {
        auto& c = crossings_[i];
        if (!c.dest_domain) continue;
        // Only consider actual domain crossings (not clock-as-data etc.)
        if (!c.source_domain) continue;
        source_groups[c.source_signal].push_back(i);
    }

    for (auto& [signal, indices] : source_groups) {
        // Collect unique dest domain names
        std::unordered_set<std::string> dest_domains;
        for (auto idx : indices) {
            if (crossings_[idx].dest_domain)
                dest_domains.insert(crossings_[idx].dest_domain->canonical_name);
        }

        if (dest_domains.size() < 2) continue;

        // Same source crosses to 2+ different dest domains
        for (auto idx : indices) {
            auto& c = crossings_[idx];
            if (c.category == ViolationCategory::Waived) continue;
            // For INFO crossings, upgrade to CAUTION
            if (c.category == ViolationCategory::Info) {
                c.category = ViolationCategory::Caution;
                c.severity = Severity::Medium;
                c.id = "CAUTION-" + std::to_string(++caution_counter_);
            }
            // Annotate with Ac_cdc11 (supplement, don't replace existing rule)
            if (c.rule.empty()) c.rule = "Ac_cdc11";
            if (c.recommendation.empty())
                c.recommendation = "[Ac_cdc11] Signal crosses to multiple clock domains independently";
            else
                c.recommendation += ". [Ac_cdc11] Signal crosses to multiple clock domains independently";
        }
    }
}

void SyncVerifier::detectQuasiStaticSignals() {
    // For each crossing, check if the source signal might be quasi-static
    // Heuristic: signal name contains cfg_, config_, mode_, static_
    // or the source FF has no dynamic data input (empty fanin)
    size_t n = crossings_.size(); // snapshot size to avoid iterating new entries
    for (size_t ci = 0; ci < n; ++ci) {
        // Use index-based access throughout — auto& would dangle if push_back reallocates
        std::string src_leaf = crossings_[ci].source_signal;
        auto dot = src_leaf.rfind('.');
        if (dot != std::string::npos) src_leaf = src_leaf.substr(dot + 1);

        std::string lower_leaf = src_leaf;
        std::transform(lower_leaf.begin(), lower_leaf.end(),
                       lower_leaf.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool is_quasi_static = false;
        for (auto& pat : {"cfg_", "config_", "mode_", "static_"}) {
            if (lower_leaf.find(pat) != std::string::npos) {
                is_quasi_static = true;
                break;
            }
        }

        if (!is_quasi_static) {
            // Also check if source FF has no dynamic data input
            auto ff_it = ff_by_path_.find(crossings_[ci].source_signal);
            if (ff_it != ff_by_path_.end()) {
                const FFNode* src_ff = ff_it->second;
                if (src_ff->fanin_signals.empty()) {
                    is_quasi_static = true;
                }
            }
        }

        if (is_quasi_static) {
            // Add a separate INFO report for the quasi-static hint
            CrossingReport report;
            report.source_domain = crossings_[ci].source_domain;
            report.dest_domain = crossings_[ci].dest_domain;
            report.source_signal = crossings_[ci].source_signal;
            report.dest_signal = crossings_[ci].dest_signal;
            report.sync_type = crossings_[ci].sync_type;
            report.category = ViolationCategory::Info;
            report.severity = Severity::Info;
            report.id = "INFO-" + std::to_string(++info_counter_);
            report.rule = "Ac_cdc12";
            report.recommendation = "[Ac_cdc12] Potentially quasi-static signal "
                "-- verify data stability before use";
            crossings_.push_back(std::move(report));
        }
    }
}

} // namespace sv_cdccheck
