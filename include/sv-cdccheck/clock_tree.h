#pragma once

#include "sv-cdccheck/types.h"
#include "sv-cdccheck/sdc_parser.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Statement.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"

#include <optional>
#include <string>
#include <unordered_set>

namespace sv_cdccheck {

/// Pass 1: Clock tree analysis
///
/// Builds the ClockDatabase by:
/// 1. Importing SDC constraints (if provided) → primary/generated ClockSources
/// 2. Auto-detecting clock ports from naming patterns
/// 3. Walking the elaborated hierarchy to propagate clock nets through port connections
/// 4. Registering domain relationships (async, divided, exclusive)
///
/// After analyze(), every clock-carrying signal in the design has a ClockNet
/// pointing to its ultimate ClockSource, regardless of hierarchical name changes.
class ClockTreeAnalyzer {
public:
    ClockTreeAnalyzer(slang::ast::Compilation& compilation,
                      ClockDatabase& clock_db);

    /// Optionally load SDC constraints before analysis
    void loadSdc(const SdcConstraints& sdc);

    /// Run full clock tree analysis
    void analyze();

    // ── Helpers (public for testability) ──

    /// Check if a port name looks like a clock
    static bool isClockName(const std::string& name);

    /// Check if a port name looks like a reset
    static bool isResetName(const std::string& name);

private:
    slang::ast::Compilation& compilation_;
    ClockDatabase& clock_db_;
    std::optional<SdcConstraints> sdc_;

    // ── Phase 1a: Source identification ──

    /// Import create_clock / create_generated_clock from SDC
    void importSdcClocks();

    /// Auto-detect clock ports by name pattern (*clk*, *clock*, *ck*)
    void autoDetectClockPorts();

    // ── Phase 1b: Hierarchical propagation ──

    /// DFS walk from root: propagate known clock nets through port connections
    void propagateFromRoot();

    /// Recursive: propagate clock nets into an instance via its port connections
    void propagateInstance(
        const slang::ast::InstanceSymbol& inst,
        const std::unordered_map<std::string, ClockNet*>& parent_nets,
        const std::string& hier_prefix = "");

    /// Walk a Scope (InstanceBody, GenerateBlockSymbol, ...) and recurse
    /// into module instances + nested generate blocks, sharing the
    /// surrounding instance's local clock-net map. Without this, clock
    /// signals do not inherit through `for (genvar i = 0; i < N; i++)
    /// begin : g sync u_sync (.clk_i(parent_clk)) end` -- which is the
    /// canonical CDC FIFO synchronizer pattern.
    void propagateChildren(
        const slang::ast::Scope& scope,
        const std::unordered_map<std::string, ClockNet*>& local_nets,
        const std::string& inst_path);

    /// Extract clock signal from always_ff sensitivity list
    void collectSensitivityClocks(
        const slang::ast::InstanceSymbol& inst,
        std::unordered_map<std::string, ClockNet*>& local_nets,
        const std::string& inst_path);

    // ── Phase 1b+: Clock divider detection ──

    /// Detect clock dividers: always_ff with q <= ~q toggle pattern
    void detectClockDividers();

    /// Recursive helper for clock divider detection
    void detectClockDividersInInstance(const slang::ast::InstanceSymbol& inst,
                                       const std::string& inst_path);

    /// Check a statement for toggle pattern (q <= ~q)
    void checkTogglePattern(const slang::ast::Statement& stmt,
                            const std::string& clock_name,
                            const std::string& inst_path);

    /// Collect every signal name that appears on the clock side of an
    /// always_ff sensitivity list, across the full elaborated design.
    /// Used as the structural-evidence filter before promoting toggle
    /// registers to generated clocks (Finding 1 guard).
    void collectUsedClockNames();

    /// Recursive helper for collectUsedClockNames.
    void collectUsedClockNamesInInstance(const slang::ast::InstanceSymbol& inst);

    /// Populated by collectUsedClockNames(); consumed by checkTogglePattern.
    std::unordered_set<std::string> used_clock_names_;

    /// Detect PLL/MMCM module outputs as primary clock sources
    void detectPLLOutputs();

    /// Recursive helper for PLL/MMCM detection
    void detectPLLOutputsInInstance(const slang::ast::InstanceSymbol& inst,
                                    const std::string& inst_path);

    /// Detect ICG (integrated clock gating) cells by module name pattern
    void detectClockGates();

    /// Recursive helper for clock gate detection
    void detectClockGatesInInstance(const slang::ast::InstanceSymbol& inst,
                                    const std::string& inst_path);

    // ── Phase 1c: Relationship registration ──

    /// Import set_clock_groups from SDC
    void importSdcRelationships();

    /// Infer relationships for auto-detected clocks (conservative: assume async)
    void inferRelationships();

};

} // namespace sv_cdccheck
