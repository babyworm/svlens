#pragma once

#include "sv-cdccheck/types.h"

namespace sv_cdccheck {

/// Pass 5: Synchronizer verification — pattern matching on crossing paths
///
/// For each crossing, examines the destination-domain FFs to detect
/// synchronizer patterns (2-FF, 3-FF, etc.).
/// Updates crossing reports with sync_type and adjusts category accordingly.
/// Also detects: reconvergence, combinational-before-sync, reset sync issues.
class SyncVerifier {
public:
    SyncVerifier(std::vector<CrossingReport>& crossings,
                 const std::vector<std::unique_ptr<FFNode>>& ff_nodes,
                 const std::vector<FFEdge>& edges,
                 const ClockDatabase* clock_db = nullptr);

    void analyze();

    /// Set minimum required synchronizer stages (default: 2).
    /// A crossing with fewer stages than required is not downgraded to INFO.
    void setRequiredStages(int n) { required_stages_ = n; }

private:
    std::vector<CrossingReport>& crossings_;
    const std::vector<std::unique_ptr<FFNode>>& ff_nodes_;
    const std::vector<FFEdge>& edges_;
    const ClockDatabase* clock_db_ = nullptr;

    /// Check if dest FF is the start of a 2-FF or 3-FF sync chain
    SyncType detectSyncPattern(const FFNode* dest_ff) const;

    /// Find downstream FF connected to given FF in the same domain
    const FFNode* findNextFF(const FFNode* ff) const;

    /// Find the FFEdge that connects source to dest (for comb logic check)
    const FFEdge* findEdge(const std::string& source_signal,
                           const std::string& dest_signal) const;

    /// Post-processing: flag reconvergence when multiple signals from the
    /// same source domain cross to the same dest domain independently
    void detectReconvergence();

    /// Post-processing: flag combinational logic before first sync FF
    void detectCombBeforeSync();

    /// Post-processing: check async resets crossing domains without reset sync
    void detectResetSyncIssues();

    /// Post-processing: detect gray-code synchronizer pattern
    void detectGrayCodePattern();

    /// Post-processing: detect handshake (req/ack) synchronizer pattern
    void detectHandshakePattern();

    /// Post-processing: detect pulse synchronizer pattern
    void detectPulseSyncPattern();

    /// Post-processing: detect MUX synchronizer pattern
    void detectMuxSyncPattern();

    /// Post-processing: detect fan-out before sync completion
    void detectFanoutBeforeSync();

    /// Post-processing: detect non-power-of-2 FIFO depth and flag caution
    /// Looks for DEPTH/SIZE/ENTRIES parameters in module scope and checks
    /// if the value is a power of 2. Also uses heuristic wrap-around detection.
    /// Limitation: parameters in parent modules are not detected.
    void detectNonPow2FIFO();

    /// Post-processing: detect Johnson counter synchronizer pattern
    /// Johnson counters use 2*N bits for N states and maintain single-bit-change.
    /// Heuristic: if bit_width >= 2 * depth_parameter, classify as JohnsonCounter.
    /// Limitation: fanin-based shift/negate detection is heuristic and may have
    /// false positives/negatives.
    void detectJohnsonCounter();

    /// Post-processing: detect clock signal used as data input [Ac_cdc09]
    void detectClockAsData();

    /// Post-processing: detect same signal crossing to multiple domains [Ac_cdc11]
    void detectMultiDomainCrossing();

    /// Post-processing: detect quasi-static signals [Ac_cdc12]
    void detectQuasiStaticSignals();

    int info_counter_ = 0;
    int caution_counter_ = 0;
    int required_stages_ = 2;

    /// Precomputed indexes built at the start of analyze()
    std::unordered_map<std::string, const FFNode*> ff_by_path_;
    std::unordered_map<const FFNode*, std::vector<const FFEdge*>> edges_from_;
};

} // namespace sv_cdccheck
