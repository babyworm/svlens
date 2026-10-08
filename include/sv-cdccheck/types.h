#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <cstdint>
#include <slang/text/SourceLocation.h>

namespace slang {
class SourceManager;
}

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

    // True when the clock signal is driven by a combinational expression
    // inside the design (e.g. `assign clk_mux = sel ? clk_a : clk_b;`)
    // and that expression is NOT the output of a known glitch-free clock
    // mux primitive. Populated by ClockTreeAnalyzer::detectUnsafeCombClocks
    // and consumed by the Ac_cdc05 emitter in CdcRunnerUtils.
    bool is_unsafe_comb_clock = false;
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
        Asynchronous,        // set_clock_groups -asynchronous
        SameSource,          // same PLL, same division
        Divided,             // integer-divided (harmonic)
        PhysicallyExclusive, // set_clock_groups -physically_exclusive timing assumption
        LogicallyExclusive   // set_clock_groups -logically_exclusive timing assumption
    } relationship;
    bool sdc_declared = false; // true when from SDC set_clock_groups, false when inferred
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
    std::string source_domain;           // unique FF driver's clock domain, when traced
    std::string driver_ff_path;          // unique FF driver through direct aliases, if known
    std::optional<bool> driver_inverted; // odd one-bit inversions from driver to this reset pin
    std::string declared_path;           // AST symbol path; may differ from generated analysis scope
    bool is_async = false;       // appears in sensitivity list (not just if-condition)
    enum class Polarity { ActiveLow, ActiveHigh } polarity = Polarity::ActiveLow;
};

struct ResetMuxInputs {
    std::string input0;
    std::string input1;
    std::string select;
    std::optional<bool> selected_input; // elaboration-time 0/1; absent for runtime or unknown selection
    std::string input0_source;          // direct connected net, when structurally resolved
    std::string input1_source;
    std::string select_source;
    std::vector<std::string> input0_dependencies; // possible inputs of a computed expression
    std::vector<std::string> input1_dependencies;
    std::vector<std::string> select_dependencies;
};

// ─── Flip-flop node in connectivity graph ───
struct FFNode {
    std::string hier_path;       // e.g., "top.u_a.q_data"
    ClockDomain* domain;
    ResetSignal* reset = nullptr;
    std::vector<std::string> fanin_signals;
    std::string primitive_name;
    // Bit width of the underlying variable. Populated by ff_classifier
    // when the slang VariableSymbol is available; left at 1 as a safe
    // default. Used by sync_verifier to flag wide-bus crossings without
    // gray code or handshake (Ac_cdc04).
    int width = 1;
    // True when ff_classifier walked the always_ff body and the
    // collected `fanin_signals` reflect the FULL set of runtime
    // input names. False when the FF was created via a fallback
    // path (library-cell stub, opaque always_ff body) where the
    // empty fanin_signals does NOT mean "no inputs".
    // sync_verifier::findNextFF uses this to decide whether an
    // empty fanin is "definitively single source" (populated) or
    // "data is missing" (not populated). Optional fields stay after
    // the legacy positional fields used by test fixtures.
    bool fanin_populated = false;
    slang::SourceLocation location;
    // Owning module instance, excluding generate scopes. Needed to keep
    // leaf-name clock checks from resolving into an unrelated parent module.
    std::string module_path;
    // Signals used in conditions guarding assignments to this FF. These are
    // review clues, not proof that the capture enable is synchronized.
    std::vector<std::string> capture_conditions;
    // Declaring-scope path of the sequential timing event clock, when the
    // AST exposes one. SVA generation uses this instead of a bare domain name.
    std::string clock_path;
    std::string declared_path; // AST variable path for emitted signal references
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
[[nodiscard]] inline bool isPowerOf2(uint64_t val) {
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
struct ReqAckDataContract {
    std::string instance_path;
    bool src_to_dst = true;
    uint32_t width = 0;
};

struct FifoTransferContract {
    std::string instance_path;
    bool write_pointer = true;
};

struct CrossingReport {
    std::string id;             // e.g., "VIOLATION-001"
    ViolationCategory category = ViolationCategory::Info;
    Severity severity = Severity::None;
    std::string source_signal;
    std::string dest_signal;
    ClockDomain* source_domain = nullptr;
    ClockDomain* dest_domain = nullptr;
    std::vector<std::string> path;
    SyncType sync_type = SyncType::None;
    std::string recommendation;
    std::string rule;  // SpyGlass-compatible rule ID, e.g. "Ac_cdc01"
    std::string relationship;   // asynchronous, divided, same_source, logically_exclusive, etc.
    std::string rationale;      // human-readable explanation for the classification
    std::optional<double> timing_basis_ns; // relevant timing period used for reasoning, if available
    bool sdc_false_path = false;           // matching timing exception; never evidence of CDC safety
    std::optional<double> sdc_max_delay_constraint_ns; // declared bound, never measured path delay
    bool sdc_max_delay_datapath_only = false;
    bool sdc_max_delay_ambiguous = false;
    std::string waive_reason;                                   // reserved for explicit waiver provenance
    std::vector<std::string> capture_conditions;                // destination FF control signals, if collected
    std::optional<ReqAckDataContract> reqack_data_contract;     // checked primitive port/parameter signature
    std::optional<FifoTransferContract> fifo_transfer_contract; // checked ready/valid port signature
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
    // Structural root provenance, including known gates/dividers. This is
    // metadata only: matching roots do not imply a safe CDC relationship.
    std::unordered_map<std::string, ClockSource*> root_by_path;
    // Directed direct-assignment and port aliases. The reset checker uses
    // these to find a unique FF driver without a global same-leaf guess.
    // This is not a complete reset tree.
    std::unordered_map<std::string, std::vector<std::string>> directed_aliases;
    // Reset-only one-bit inversion edges. These must not merge clock domains
    // or propagate clock roots as transparent aliases.
    std::unordered_map<std::string, std::vector<std::string>> reset_inversions;
    // A scalar reset input fed by one constant-selected bit of an integral
    // vector. Used only for reset-driver provenance, never clock lineage.
    std::unordered_map<std::string, std::vector<std::string>> reset_selected_aliases;
    // A reset-named integral struct field crosses a same-shaped module port.
    // Project only an observed constant index during reset reporting; never
    // merge these paths into the clock alias graph.
    std::unordered_map<std::string, std::vector<std::string>> reset_indexed_field_aliases;
    // Recognized two-input mux ports are conditional reset-route candidates,
    // not transparent aliases or evidence of a synchronized reset.
    std::unordered_map<std::string, ResetMuxInputs> reset_mux_inputs;
    // Lookup: canonical domain name → ClockDomain
    std::unordered_map<std::string, ClockDomain*> domain_by_name;

    ClockSource* addSource(std::unique_ptr<ClockSource> src);
    ClockNet* addNet(std::unique_ptr<ClockNet> net);
    ClockDomain* findOrCreateDomain(ClockSource* source, Edge edge);
    ClockDomain* domainForSignal(const std::string& hier_path) const;
    bool isAsynchronous(const ClockDomain* a, const ClockDomain* b) const;
    std::optional<DomainRelationship::Type> relationshipBetween(const ClockDomain* a,
                                                                const ClockDomain* b) const;
    /// Check if the relationship between two domains was declared via SDC constraint
    bool isSdcDeclaredRelationship(const ClockDomain* a, const ClockDomain* b) const;
};

/// Overall analysis result
struct AnalysisResult {
    ClockDatabase clock_db;
    std::vector<std::unique_ptr<FFNode>> ff_nodes;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;
    const slang::SourceManager* sourceManager = nullptr;

    [[nodiscard]] int violation_count() const;
    [[nodiscard]] int caution_count() const;
    [[nodiscard]] int info_count() const;
    [[nodiscard]] int waived_count() const;
    [[nodiscard]] int convention_count() const;
};

} // namespace sv_cdccheck
