#pragma once

#include "ConnectionGraph.h"
#include <slang/ast/Compilation.h>
#include <slang/ast/Statement.h>
#include <slang/ast/symbols/MemberSymbols.h>

#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace connect {

class ConnectionExtractor {
public:
    ConnectionExtractor(slang::ast::Compilation& compilation, const std::string& topModule, int maxDepth = -1,
                        bool captureDeclarations = false);

    ConnectionGraph extract();

private:
    struct ResolvedExpr {
        std::vector<std::string> netNames;
        bool approximate = false;
        bool tieOff = false;
        // Round 30 US-R05: when true, netNames hold absolute hier
        // paths (e.g. "top.iface_inst.signal") that should bypass the
        // scopePath prefix when forming netMap_ keys. Used by modport
        // member access where the expansion side keys at the same
        // absolute path so endpoints rendezvous.
        bool is_absolute = false;
    };

    void visitInstance(const slang::ast::InstanceSymbol& instance,
                       const std::string& parentPath);

    void visitScope(const slang::ast::Scope& scope,
                    const std::string& scopePath);

    void processChildInstance(const slang::ast::InstanceSymbol& childInst,
                              const std::string& scopePath);

    void processContinuousAssign(const slang::ast::ContinuousAssignSymbol& assignSym,
                                 const std::string& scopePath);

    void processProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                                const std::string& scopePath);

    void processProceduralStatement(const slang::ast::Statement& stmt, const slang::ast::ProceduralBlockSymbol& owner,
                                    const std::string& scopePath, bool combinational, bool exactSingleAssignment,
                                    std::vector<std::string> guardKeys = {});

    void resolveConnections();

    static ResolvedExpr resolveExpr(const slang::ast::Expression* expr);
    struct WireRange {
        std::string baseKey;
        int64_t left = 0;
        int64_t right = 0;
    };
    static std::optional<WireRange> resolveWireRange(const slang::ast::Expression& expr, const std::string& scopePath,
                                                     uint32_t portWidth);
    struct BitFlowLink {
        WireRange source;
        WireRange dest;
        uint32_t sourceOffset = 0;
        uint32_t destOffset = 0;
        uint32_t width = 0;
        bool approximate = false;
        bool interesting = false; // positional flow to surface, including exact procedural copies
    };
    bool recordBitFlow(const slang::ast::Expression& lhs, const slang::ast::Expression& rhs,
                       const std::string& scopePath, bool approximate, bool forceInteresting = false);
    static void collectDependencyKeys(const slang::ast::Expression& expr, const std::string& scopePath,
                                      std::vector<std::string>& keys);
    void recordAlias(const std::string& lhsKey, const std::string& rhsKey, bool approximate);

    // Round 39 review: fill in StyleObservation::lineNumber/columnNumber
    // from the location's source manager so JSON consumers do not need
    // to regex-parse the detail string.  Safe to call when location is
    // invalid; the fields stay at their default 0.
    void populateLineColumn(StyleObservation& obs) const;

    // Round 39 review: dedupe `_d` LHS collection across the always_comb
    // body walk and the continuous-assign path.  Inserts the base name
    // (leaf with `_d` stripped) into combinational_d_bases_ when the
    // leaf is a non-hierarchical name ending in `_d` and the base is
    // non-empty.
    void collectDBaseFromLeaf(std::string_view leaf);

    // Synthesizability / latch-inference detection. Populated per module
    // instance during the procedural-block walk and drained into
    // graph_.synthRisks at the end of visitInstance (mirrors the _q/_d
    // MissingDSuffix accounting). See ConnectionExtractor.cpp for the
    // discriminator logic.
    struct ArrayFact {
        // A clocked (always_ff / always-with-clock) write to arr[var]
        // that is nested under a data-dependent conditional -> only a
        // subset of elements get a new value each cycle (partial write).
        bool clockedVarIdxPartialWrite = false;
        // A clocked write that drives the whole next-state (variable or
        // loop index, but NOT under a data-dependent conditional, i.e.
        // an unconditional default-hold/overwrite of every element, or a
        // whole-array `arr <= ...`). Presence of this clears the latch
        // risk because DC then sees a plain flop array.
        bool clockedFullWrite = false;
        // The array symbol is read in an always_comb / always @(*) or a
        // continuous assign (any index).
        bool combRead = false;
        // First location of the offending partial write (for reporting).
        slang::SourceLocation partialWriteLoc;
        uint32_t partialWriteLine = 0;
        uint32_t partialWriteCol = 0;
    };
    // keyed by array leaf name within the current module instance.
    std::unordered_map<std::string, ArrayFact> arrayFacts_;

    // Collect array element-select reads (NamedValue base of an
    // ElementSelectExpression) from an expression subtree, marking the
    // ArrayFact.combRead bit. Used for continuous-assign RHS.
    void collectArrayCombReads(const slang::ast::Expression* expr);
    // Walk a combinational block body, marking ArrayFact.combRead for
    // every unpacked-array element-select read on assignment RHS sides.
    void collectArrayCombReadsInStatement(const slang::ast::Statement& stmt);
    // Walk a clocked (always_ff / always) block body, classifying writes
    // to unpacked-array elements as partial (variable data-dependent
    // index) or full (loop-induction-variable sweep / whole-array). The
    // set of in-scope for-loop induction variables is threaded down so a
    // `for(y) for(x) arr[y][x] <= ...` full next-state is told apart from
    // `arr[data_signal] <= ...` partial write.
    void scanClockedArrayWrites(const slang::ast::Statement& stmt,
                                std::unordered_set<const slang::ast::Symbol*>& loopVars, bool inResetBranch);
    // Fill line/column for a SynthRisk from its location (mirrors
    // populateLineColumn for StyleObservation).
    void populateLineColumn(SynthRisk& risk) const;
    // Secondary rule: flag always_comb signals assigned only inside an
    // incomplete branch structure (if-without-else / case-without-default)
    // with no unconditional default -> latch-prone. Emits WARN SynthRisks.
    void scanIncompleteCombAssignments(const slang::ast::ProceduralBlockSymbol& block, const std::string& scopePath);

    slang::ast::Compilation& compilation_;
    std::string topModule_;
    int maxDepth_;
    bool captureDeclarations_;
    ConnectionGraph graph_;

    std::string findCanonical(const std::string& key);

    // net_key (scope + net_name) -> list of (PortInfo, isDriver)
    struct NetBinding {
        PortInfo port;
        bool isDriver;
        ConnectionKind kind = ConnectionKind::Direct;
    };
    std::unordered_map<std::string, std::vector<NetBinding>> netMap_;
    struct RangeBinding {
        PortInfo port;
        bool isDriver = false;
        WireRange wire;
        std::string originalKey;
        std::optional<int64_t> portRight;
        ConnectionKind kind = ConnectionKind::Direct;
    };
    std::vector<RangeBinding> rangeBindings_;
    std::vector<BitFlowLink> bitFlowLinks_;

    // Round 39 US-39B: per-module sets accumulated during visitScope.
    // registered_q_bases_: base names of _q-suffixed always_ff NB-LHS.
    // combinational_d_bases_: base names of _d-suffixed always_comb/assign LHS.
    // has_comb_context_: true when at least one always_comb or assign is present.
    // All are reset at the start of each visitInstance call.
    std::unordered_set<std::string> registered_q_bases_;
    std::unordered_set<std::string> combinational_d_bases_;
    bool has_comb_context_ = false;

    // net alias map: key -> parent key (union-find without path compression)
    std::unordered_map<std::string, std::string> netAliases_;
    std::unordered_set<std::string> approximateAliases_;
    // Directed signal dependencies from combinational procedural assignments.
    // Unlike aliases, multiple mux inputs must not merge into one net.
    std::vector<std::pair<std::string, std::string>> proceduralDependencies_;
};

} // namespace connect
