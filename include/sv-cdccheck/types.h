#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <cstdint>

namespace sv_cdccheck {

// ─── Clock edge ───
enum class Edge { Posedge, Negedge };

// ─── Clock Source: the physical origin of a clock ───
struct ClockSource {
    std::string id;              // unique id, e.g., "pll0_sys"
    std::string name;            // SDC name or auto-detected name, e.g., "sys_clk"
    enum class Type {
        Primary,       // SDC create_clock or auto-detected top port
        Generated,     // SDC create_generated_clock (divided, gated)
        Virtual,       // SDC create_clock with no physical port
        AutoDetected   // inferred from port name pattern
    } type = Type::AutoDetected;

    std::optional<double> period_ns;
    std::string origin_signal;   // SDC [get_ports ...] or auto-detected port path

    // For Generated clocks: link to master source
    ClockSource* master = nullptr;
    int divide_by = 1;
    int multiply_by = 1;
    bool invert = false;
};

// ─── Clock Net: a hierarchical net carrying a clock ───
// Same physical clock may have different names at each hierarchy level.
// All ClockNets from the same source share the same ClockSource*.
struct ClockNet {
    std::string hier_path;       // e.g., "top.u_subsys.sys_clk"
    ClockSource* source;         // ultimate source (pointer equality = same domain)
    Edge edge = Edge::Posedge;
    bool is_gated = false;
    std::string gate_enable;     // if gated, the enable signal path
};

// ─── Domain Relationship: explicit relationship between two sources ───
struct DomainRelationship {
    ClockSource* a;
    ClockSource* b;
    enum class Type {
        Asynchronous,         // set_clock_groups -asynchronous
        SameSource,           // same PLL, same division
        Divided,              // integer-divided (harmonic)
        PhysicallyExclusive,  // set_clock_groups -physically_exclusive (mux)
        LogicallyExclusive    // set_clock_groups -logically_exclusive
    } relationship;
};

// ─── Clock Domain: a logical grouping of nets from the same physical clock ───
struct ClockDomain {
    std::string canonical_name;  // representative name (SDC name or top port name)
    ClockSource* source;
    Edge edge = Edge::Posedge;
    std::vector<ClockNet*> nets; // all hierarchical nets in this domain

    bool isSameDomain(const ClockDomain& other) const {
        return source == other.source && edge == other.edge;
    }
};

// ─── Reset Signal tracking ───
struct ResetSignal {
    std::string hier_path;
    std::string source_domain;   // clock domain that generates this reset
    bool is_async = false;       // appears in sensitivity list (not just if-condition)
    enum class Polarity { ActiveLow, ActiveHigh } polarity = Polarity::ActiveLow;
};

// ─── Flip-flop node in connectivity graph ───
struct FFNode {
    std::string hier_path;       // e.g., "top.u_a.q_data"
    ClockDomain* domain;
    ResetSignal* reset = nullptr;
    std::vector<std::string> fanin_signals;
};

/// Synchronizer type
enum class SyncType {
    None,
    TwoFF,
    ThreeFF,
    GrayCode,
    Handshake,
    AsyncFIFO,
    MuxSync,
    PulseSync,
    JohnsonCounter
};

/// Check if a value is a power of 2 (and non-zero)
inline bool isPowerOf2(uint64_t val) {
    return val != 0 && (val & (val - 1)) == 0;
}

/// Edge between two FFs in the connectivity graph
struct FFEdge {
    const FFNode* source = nullptr;
    const FFNode* dest = nullptr;
    std::vector<std::string> comb_path;
    SyncType sync_type = SyncType::None;
    bool has_comb_logic = false; // combinational logic between source and dest FF
};

/// Crossing severity
enum class Severity {
    None,       // same domain
    Info,       // properly synchronized
    Low,        // gated clock crossing
    Medium,     // harmonic crossing
    High        // async crossing, no sync
};

/// Violation category
enum class ViolationCategory {
    Violation,    // no synchronizer on async crossing
    Caution,      // synchronizer with quality issue
    Convention,   // naming issue
    Info,         // properly synchronized
    Waived        // user-waived
};

/// A single CDC crossing report entry
struct CrossingReport {
    std::string id;             // e.g., "VIOLATION-001"
    ViolationCategory category;
    Severity severity;
    std::string source_signal;
    std::string dest_signal;
    ClockDomain* source_domain;
    ClockDomain* dest_domain;
    std::vector<std::string> path;
    SyncType sync_type;
    std::string recommendation;
    std::string rule;  // SpyGlass-compatible rule ID, e.g. "Ac_cdc01"
};

// ─── Clock Database: owns all clock-related objects ───
struct ClockDatabase {
    std::vector<std::unique_ptr<ClockSource>> sources;
    std::vector<std::unique_ptr<ClockNet>> nets;
    std::vector<std::unique_ptr<ClockDomain>> domains;
    std::vector<DomainRelationship> relationships;
    std::vector<std::unique_ptr<ResetSignal>> resets;

    // Lookup: hierarchical signal path → ClockNet
    std::unordered_map<std::string, ClockNet*> net_by_path;
    // Lookup: canonical domain name → ClockDomain
    std::unordered_map<std::string, ClockDomain*> domain_by_name;

    ClockSource* addSource(std::unique_ptr<ClockSource> src);
    ClockNet* addNet(std::unique_ptr<ClockNet> net);
    ClockDomain* findOrCreateDomain(ClockSource* source, Edge edge);
    ClockDomain* domainForSignal(const std::string& hier_path) const;
    bool isAsynchronous(const ClockDomain* a, const ClockDomain* b) const;
};

/// Overall analysis result
struct AnalysisResult {
    ClockDatabase clock_db;
    std::vector<std::unique_ptr<FFNode>> ff_nodes;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;

    int violation_count() const;
    int caution_count() const;
    int info_count() const;
    int waived_count() const;
    int convention_count() const;
};

} // namespace sv_cdccheck
