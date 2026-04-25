#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/ast_utils.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"

#include <algorithm>
#include <cassert>

namespace sv_cdccheck {

ConnectivityBuilder::ConnectivityBuilder(slang::ast::Compilation& compilation,
                                         const std::vector<std::unique_ptr<FFNode>>& ff_nodes)
    : compilation_(compilation), ff_nodes_(ff_nodes) {}

std::unordered_map<std::string, FFNode*> ConnectivityBuilder::buildFFOutputMap() const {
    std::unordered_map<std::string, FFNode*> map;
    for (auto& ff : ff_nodes_) {
        // Primary key: full hierarchical path (unique, no collisions)
        map[ff->hier_path] = ff.get();
    }
    return map;
}

// Extract signal name from a NamedValueExpression. Also unwraps a single
// bit-select / range-select / conversion so a port connection written as
// `.d_src_i(wptr_q[i])` or `.d_src_i(wptr_q[3:0])` still maps the port to
// the underlying register name `wptr_q`.
static std::string extractExprSignalName(const slang::ast::Expression& expr) {
    using EK = slang::ast::ExpressionKind;
    const slang::ast::Expression* current = &expr;
    while (true) {
        if (current->kind == EK::NamedValue) {
            return std::string(
                current->as<slang::ast::NamedValueExpression>().symbol.name);
        }
        if (current->kind == EK::HierarchicalValue) {
            // For hierarchical port connections like
            // `.d_i(u_a.q)`, return the FULL hierarchical path so
            // findFFByName can resolve the FFNode directly via
            // output_map. Returning just the leaf name "q" would
            // be ambiguous when multiple submodules share the
            // same internal signal name.
            return current->as<slang::ast::ValueExpressionBase>()
                .symbol.getHierarchicalPath();
        }
        if (current->kind == EK::ElementSelect) {
            current = &current->as<slang::ast::ElementSelectExpression>().value();
            continue;
        }
        if (current->kind == EK::RangeSelect) {
            current = &current->as<slang::ast::RangeSelectExpression>().value();
            continue;
        }
        if (current->kind == EK::Conversion) {
            current = &current->as<slang::ast::ConversionExpression>().operand();
            continue;
        }
        return "";
    }
}

// Build port-to-actual-signal map for an instance: port_name -> actual_signal_name
static std::unordered_map<std::string, std::string> buildPortMap(
    const slang::ast::InstanceSymbol& inst)
{
    std::unordered_map<std::string, std::string> port_map;
    for (auto* conn : inst.getPortConnections()) {
        if (!conn) continue;
        auto* expr = conn->getExpression();
        if (!expr) continue;
        std::string actual = extractExprSignalName(*expr);
        if (!actual.empty()) {
            port_map[std::string(conn->port.name)] = actual;
        }
    }
    return port_map;
}

// Extract signal name from an expression, handling Assignment expressions
// (slang models output port connections as assignments: wire = port_internal)
static std::string extractWireNameFromExpr(const slang::ast::Expression& expr) {
    // Simple named value
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        return std::string(expr.as<slang::ast::NamedValueExpression>().symbol.name);
    }
    // Output port: Assignment expression where LHS is the wire
    if (expr.kind == slang::ast::ExpressionKind::Assignment) {
        auto& assign = expr.as<slang::ast::AssignmentExpression>();
        if (assign.left().kind == slang::ast::ExpressionKind::NamedValue) {
            return std::string(
                assign.left().as<slang::ast::NamedValueExpression>().symbol.name);
        }
    }
    return "";
}

// Build a map: wire/signal name -> FFNode* for output ports of child instances
// E.g., if child u_a has output q connected to wire_ab, map wire_ab -> u_a.q FF
static void buildWireToFFMap(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    std::unordered_map<std::string, FFNode*>& wire_map)
{
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& child = member.as<slang::ast::InstanceSymbol>();
        std::string child_path = inst_path + "." + std::string(child.name);

        for (auto* conn : child.getPortConnections()) {
            if (!conn) continue;
            // Only care about output ports
            if (conn->port.kind != slang::ast::SymbolKind::Port) continue;
            auto& port_sym = conn->port.as<slang::ast::PortSymbol>();
            if (port_sym.direction != slang::ast::ArgumentDirection::Out) continue;
            auto* expr = conn->getExpression();
            if (!expr) continue;
            std::string wire_name = extractWireNameFromExpr(*expr);
            if (wire_name.empty()) continue;

            // Check if the port corresponds to an FF output directly
            std::string ff_path = child_path + "." + std::string(conn->port.name);
            auto it = output_map.find(ff_path);
            if (it != output_map.end()) {
                wire_map[wire_name] = it->second;
                continue;
            }

            // Otherwise, the output port may be driven by a continuous
            // assign inside the child: `assign port_name = local_ff;`.
            // Chase one level of cont-assign indirection so that the
            // wire still resolves back to the underlying FF.
            std::string port_name(conn->port.name);
            for (auto& child_member : child.body.members()) {
                if (child_member.kind != slang::ast::SymbolKind::ContinuousAssign)
                    continue;
                auto& ca = child_member.as<slang::ast::ContinuousAssignSymbol>();
                auto& ca_expr = ca.getAssignment();
                if (ca_expr.kind != slang::ast::ExpressionKind::Assignment) continue;
                auto& assign_expr = ca_expr.as<slang::ast::AssignmentExpression>();
                if (assign_expr.left().kind != slang::ast::ExpressionKind::NamedValue)
                    continue;
                std::string lhs_name(
                    assign_expr.left().as<slang::ast::NamedValueExpression>().symbol.name);
                if (lhs_name != port_name) continue;
                // RHS should be a NamedValue or simple expression naming the source FF
                std::string rhs_name = extractExprSignalName(assign_expr.right());
                if (rhs_name.empty()) continue;
                auto src_it = output_map.find(child_path + "." + rhs_name);
                if (src_it != output_map.end()) {
                    wire_map[wire_name] = src_it->second;
                    break;
                }
            }
        }
    }
}

// Stack of ancestor cont_assigns maps, parallel to parent_port_chain.
// Maintained by processInstanceEdges via push/pop around the recursive
// descent so findFFByName can chase `assign d_o = d_i;` /
// `always_comb d_o = d_i;` renames at any ancestor level. File-scope
// rather than threaded through every function signature because the
// chain is conceptually 1:1 with parent_port_chain and refactoring
// every helper signature would hurt readability for a localized
// detail.
namespace {
thread_local std::vector<std::unordered_map<std::string,
                                              std::vector<std::string>>>
    g_parent_cont_chain;
}

// Find an FFNode by signal name within a given instance scope.
// Uses port_map to resolve port names to actual parent signals,
// and wire_map to resolve parent wires to FF outputs.
// `parent_port_chain` is a stack of (port_map, parent_path) for ancestors
// that lets the resolver walk port chains across multiple submodule
// boundaries when a port maps to another port (Finding 2 fix).
static FFNode* findFFByName(
    const std::string& sig_name,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, std::string>& port_map,
    const std::unordered_map<std::string, FFNode*>& wire_map,
    const std::vector<std::pair<std::unordered_map<std::string, std::string>,
                                std::string>>& parent_port_chain = {})
{
    // Try full scoped path first (same instance)
    std::string full_name = inst_path + "." + sig_name;
    auto it = output_map.find(full_name);
    if (it != output_map.end())
        return it->second;

    // Try just the signal name as a full path
    it = output_map.find(sig_name);
    if (it != output_map.end())
        return it->second;

    // Try normalized forms for generate-array hier-refs:
    // slang's `getHierarchicalPath()` uses `gen_blk[1]` syntax, but
    // ff_classifier's `getExternalName()` flattens labeled genvar
    // entries to `genblk1` (drop label-internal underscores AND
    // brackets). Without this normalization, hierarchical reads
    // into labeled generate-array entries fall through to the slow
    // suffix scan or are lost entirely.
    if (sig_name.find('[') != std::string::npos) {
        // Variant A: drop brackets only -> "gen_blk1"
        std::string no_brackets;
        no_brackets.reserve(sig_name.size());
        for (char c : sig_name) {
            if (c != '[' && c != ']')
                no_brackets.push_back(c);
        }
        // Variant B: ALSO drop underscores inside any alphanumeric
        // segment that ends in digits -> "gen_blk1" -> "genblk1".
        // We process per-segment (split by '.') so module-level
        // underscores like "gar_top" or "u_sub" are preserved.
        std::string flattened;
        flattened.reserve(no_brackets.size());
        size_t seg_start = 0;
        for (size_t i = 0; i <= no_brackets.size(); ++i) {
            if (i == no_brackets.size() || no_brackets[i] == '.') {
                std::string seg = no_brackets.substr(seg_start, i - seg_start);
                bool ends_in_digit = !seg.empty() &&
                    std::isdigit(static_cast<unsigned char>(seg.back()));
                if (ends_in_digit) {
                    // Strip underscores between letters in this segment
                    std::string compact;
                    compact.reserve(seg.size());
                    for (size_t j = 0; j < seg.size(); ++j) {
                        char c = seg[j];
                        if (c == '_' && j > 0 && j + 1 < seg.size() &&
                            std::isalpha(static_cast<unsigned char>(seg[j - 1])) &&
                            std::isalpha(static_cast<unsigned char>(seg[j + 1]))) {
                            continue;
                        }
                        compact.push_back(c);
                    }
                    flattened += compact;
                } else {
                    flattened += seg;
                }
                if (i < no_brackets.size()) flattened.push_back('.');
                seg_start = i + 1;
            }
        }
        it = output_map.find(no_brackets);
        if (it != output_map.end()) return it->second;
        it = output_map.find(flattened);
        if (it != output_map.end()) return it->second;

        // Last resort: parent-prefix + index-N + leaf suffix scan.
        // Slang's `getExternalName()` may reuse `genblk<N>` even when
        // the source label is `: gen_2stage` (digit-medial label
        // names trigger this fallback). Walk output_map for entries
        // whose path = `<parent>.<anything><N>.<leaf>`.
        size_t bracket_open = sig_name.find('[');
        size_t bracket_close = sig_name.find(']', bracket_open);
        if (bracket_open != std::string::npos &&
            bracket_close != std::string::npos &&
            bracket_close > bracket_open + 1) {
            // Parent prefix: everything up to the dot before the
            // generate label that owns the index.
            size_t label_dot = sig_name.rfind('.', bracket_open);
            if (label_dot != std::string::npos) {
                std::string parent_prefix = sig_name.substr(0, label_dot + 1);
                std::string idx = sig_name.substr(
                    bracket_open + 1, bracket_close - bracket_open - 1);
                std::string suffix = sig_name.substr(bracket_close + 1);
                // Pattern: starts with parent_prefix, ends with
                // `<idx>` + suffix (e.g., "1.q_inner"), with any
                // alphanumeric label in between.
                std::string idx_suffix = idx + suffix;
                // Round 31 US-31A: collect all matching paths and
                // return the lexicographically-smallest one. The
                // prior `return ff` inside the loop returned the
                // first hit in unordered_map iteration order, which
                // is implementation-defined when two sibling labels
                // (e.g. "top.a1.x" and "top.b1.x") share the same
                // parent_prefix + idx_suffix. Lexicographic tie-
                // break gives a deterministic, debuggable result.
                std::vector<std::pair<const std::string*, FFNode*>> matches;
                matches.reserve(4);
                for (auto& [path, ff] : output_map) {
                    if (!path.starts_with(parent_prefix)) continue;
                    if (!path.ends_with(idx_suffix)) continue;
                    // Architect Round 11 hardening: ensure the
                    // matched path has exactly ONE label segment
                    // between parent_prefix and idx_suffix. Without
                    // this guard, a nested generate at a deeper
                    // scope could collide via the same suffix
                    // pattern.
                    size_t middle_start = parent_prefix.size();
                    size_t middle_end = path.size() - idx_suffix.size();
                    if (middle_end <= middle_start) continue;
                    auto middle = std::string_view(path).substr(
                        middle_start, middle_end - middle_start);
                    if (middle.find('.') != std::string_view::npos) continue;
                    matches.emplace_back(&path, ff);
                }
                if (!matches.empty()) {
                    auto best = std::min_element(
                        matches.begin(), matches.end(),
                        [](const auto& a, const auto& b) {
                            return *a.first < *b.first;
                        });
                    return best->second;
                }
            }
        }
    }

    // Try direct wire_map lookup: sig_name is a local wire driven by a child FF output
    // (e.g., top-level always_ff reads wire_ab which is connected to u_a.q's output)
    auto wit = wire_map.find(sig_name);
    if (wit != wire_map.end())
        return wit->second;

    // Resolve through port connection: sig_name is a port, trace to actual signal.
    // Walk up the parent_port_chain so that a port wired to another port at
    // the next level is also resolved (multi-level submodule traversal).
    std::string current = sig_name;
    auto pit = port_map.find(current);
    if (pit != port_map.end()) {
        current = pit->second;
        // If port_map produced a fully hierarchical path (e.g., a
        // hierarchical port connection like `.d_i(u_a.q)` whose
        // extractExprSignalName resolved to `top.u_a.q`), try a
        // direct output_map lookup before any further qualification.
        if (current.find('.') != std::string::npos) {
            it = output_map.find(current);
            if (it != output_map.end())
                return it->second;
        }
        // Try wire_map / output_map at each ancestor level.
        auto wit_local = wire_map.find(current);
        if (wit_local != wire_map.end())
            return wit_local->second;

        std::string parent_path;
        auto last_dot = inst_path.rfind('.');
        if (last_dot != std::string::npos) {
            parent_path = inst_path.substr(0, last_dot);
            std::string parent_full = parent_path + "." + current;
            it = output_map.find(parent_full);
            if (it != output_map.end())
                return it->second;
        }

        // Walk up through the ancestor port_maps -- each iteration takes
        // the current resolved name and tries to translate it through the
        // next parent's port_map. Bounded by chain length so this is O(D).
        // Before attempting port_map at each level, try the ancestor's
        // cont_assigns (continuous assigns and `always_comb wire = signal`
        // collected at that level) to chase internal-wire renames such
        // as the OpenTitan prim_flop_2sync `always_comb d_o = d_i;`
        // pattern.
        // Invariant guard: g_parent_cont_chain and parent_port_chain
        // are two parallel stacks pushed/popped at separate call
        // sites in processInstanceEdges and processScopeForEdges.
        // The cidx walk below assumes one entry of cont-assigns per
        // entry of port-chain. If the parallel push/pop ever
        // diverges, the index arithmetic silently reads a wrong
        // ancestor's cont-assigns. Assert the invariant here so any
        // future refactor that breaks it surfaces in tests instead
        // of producing silent misclassification. The "or empty"
        // branch lets the no-ancestor case (root-level call) pass.
        // (Code-reviewer Round 12 #4.)
        assert((g_parent_cont_chain.size() == parent_port_chain.size()) ||
               g_parent_cont_chain.empty());
        size_t cidx = g_parent_cont_chain.size();
        for (auto rit = parent_port_chain.rbegin();
             rit != parent_port_chain.rend(); ++rit) {
            const auto& [ancestor_port_map, ancestor_path] = *rit;
            // Try ancestor's cont_assigns first: if `current` is the LHS
            // of an `assign x = y;` style binding, follow to the RHS and
            // continue the resolution from there.
            if (cidx > 0) {
                --cidx;
                const auto& ancestor_cont = g_parent_cont_chain[cidx];
                auto cit = ancestor_cont.find(current);
                if (cit != ancestor_cont.end() && cit->second.size() == 1) {
                    current = cit->second.front();
                }
            }
            auto ait = ancestor_port_map.find(current);
            if (ait == ancestor_port_map.end()) break;
            current = ait->second;
            auto wit_anc = wire_map.find(current);
            if (wit_anc != wire_map.end())
                return wit_anc->second;
            std::string anc_parent;
            auto adot = ancestor_path.rfind('.');
            if (adot != std::string::npos) {
                anc_parent = ancestor_path.substr(0, adot);
                std::string anc_full = anc_parent + "." + current;
                it = output_map.find(anc_full);
                if (it != output_map.end())
                    return it->second;
            }
        }
    }

    // Try matching by suffix in the same parent scope. Hoist the
    // concatenated prefix / suffix strings out of the loop and add
    // a length pre-check so paths that cannot possibly match are
    // skipped without any allocation. (Code-reviewer Round 12 #2.)
    std::string parent_path;
    auto last_dot = inst_path.rfind('.');
    if (last_dot != std::string::npos) {
        parent_path = inst_path.substr(0, last_dot);
        std::string prefix = parent_path + ".";
        std::string suffix = "." + sig_name;
        size_t min_len = prefix.size() + sig_name.size() + 1;
        for (auto& [path, ff] : output_map) {
            if (path.size() < min_len) continue;
            if (path.starts_with(prefix) && path.ends_with(suffix))
                return ff;
        }
    }

    // Last-resort fallback: walk every ancestor scope (current path with
    // the last segment chopped off, repeatedly) and try `<ancestor>.<sig>`
    // against the output_map. This catches cases where a port maps to a
    // bare signal name that lives several levels up the hierarchy --
    // notably the genfor + sub-module instance combination from
    // cdc_fifo_gray, where `i_sync` is in `gen_sync[i]` but the source
    // flop `wptr_q` lives at the enclosing module body's level.
    std::string ancestor = inst_path;
    while (true) {
        auto pos = ancestor.rfind('.');
        if (pos == std::string::npos) break;
        ancestor = ancestor.substr(0, pos);
        auto cand_it = output_map.find(ancestor + "." + current);
        if (cand_it != output_map.end())
            return cand_it->second;
        // Also try the original sig_name (in case port resolution
        // already advanced `current` and we want to fall back).
        if (current != sig_name) {
            cand_it = output_map.find(ancestor + "." + sig_name);
            if (cand_it != output_map.end())
                return cand_it->second;
        }
    }

    return nullptr;
}

// Collect continuous assign statements: wire_name -> RHS signal names
static void collectContinuousAssigns(
    const slang::ast::InstanceSymbol& inst,
    std::unordered_map<std::string, std::vector<std::string>>& cont_assigns)
{
    for (auto& member : inst.body.members()) {
        if (member.kind == slang::ast::SymbolKind::ContinuousAssign) {
            // ContinuousAssignSymbol has getAssignment() that returns an Assignment expression
            auto& ca = member.as<slang::ast::ContinuousAssignSymbol>();
            auto& assignRaw = ca.getAssignment();
            if (assignRaw.kind != slang::ast::ExpressionKind::Assignment) continue;
            auto& assign_expr = assignRaw.as<slang::ast::AssignmentExpression>();
            std::string lhs_name;
            if (assign_expr.left().kind == slang::ast::ExpressionKind::NamedValue) {
                lhs_name = std::string(
                    assign_expr.left().as<slang::ast::NamedValueExpression>().symbol.name);
            }
            if (lhs_name.empty()) continue;

            std::vector<std::string> rhs_signals;
            collectReferencedSignals(assign_expr.right(), rhs_signals);
            cont_assigns[lhs_name] = std::move(rhs_signals);
            continue;
        }

        // always_comb / always_latch blocks with single-statement
        // assignments behave like continuous assigns for connectivity
        // purposes. Treat `always_comb wire = source;` (or
        // `always_comb begin wire = source; end`) the same as
        // `assign wire = source;` so propagation through the OpenTitan
        // prim_flop_2sync `always_comb d_o = d_i;` pattern works.
        if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = member.as<slang::ast::ProceduralBlockSymbol>();
            if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysComb &&
                block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysLatch)
                continue;
            std::vector<AssignInfo> infos;
            collectAssignments(block.getBody(), infos);
            for (auto& info : infos) {
                if (info.lhs_name.empty()) continue;
                // Only register if not already a continuous assign with the
                // same LHS, to avoid clobbering.
                if (cont_assigns.find(info.lhs_name) != cont_assigns.end())
                    continue;
                cont_assigns[info.lhs_name] = info.rhs_signals;
            }
        }
    }
}

// Resolve a signal name through continuous assigns to find underlying FF sources.
// Returns all FF sources reachable through combinational logic.
// Sets has_comb to true if resolution went through a continuous assign.
static void resolveToFFs(
    const std::string& sig_name,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, std::string>& port_map,
    const std::unordered_map<std::string, FFNode*>& wire_map,
    const std::unordered_map<std::string, std::vector<std::string>>& cont_assigns,
    std::vector<FFNode*>& result,
    bool& has_comb,
    int depth = 0,
    const std::vector<std::pair<std::unordered_map<std::string, std::string>,
                                std::string>>& parent_port_chain = {})
{
    if (depth > 10) return; // prevent infinite recursion

    // Try direct FF lookup first
    FFNode* ff = findFFByName(sig_name, inst_path, output_map, port_map, wire_map,
                              parent_port_chain);
    if (ff) {
        if (std::find(result.begin(), result.end(), ff) == result.end())
            result.push_back(ff);
        return;
    }

    // Try resolving through continuous assigns
    auto it = cont_assigns.find(sig_name);
    if (it != cont_assigns.end()) {
        has_comb = true;
        for (auto& rhs : it->second) {
            resolveToFFs(rhs, inst_path, output_map, port_map, wire_map,
                        cont_assigns, result, has_comb, depth + 1);
        }
    }
}

// Recursive: process an instance and its children for FF-to-FF edges
// Forward declaration: walk a Scope (InstanceBody, GenerateBlockSymbol, ...)
// looking for procedural blocks and recursing into nested generate blocks.
// Reuses the surrounding instance's port_map / wire_map / cont_assigns since
// generate blocks share scope with their enclosing module body.
static void processScopeForEdges(
    const slang::ast::Scope& scope,
    const slang::ast::InstanceSymbol& enclosing_inst,
    const std::string& path_prefix,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, FFNode*>& combined_wire_map,
    std::vector<FFEdge>& edges,
    const std::vector<std::pair<std::unordered_map<std::string, std::string>,
                                std::string>>& parent_port_chain,
    const std::unordered_map<std::string, std::string>& enclosing_port_map,
    const std::unordered_map<std::string, std::vector<std::string>>& cont_assigns);

static void processGenerateBlocks(
    const slang::ast::Scope& scope,
    const slang::ast::InstanceSymbol& enclosing_inst,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, FFNode*>& combined_wire_map,
    std::vector<FFEdge>& edges,
    const std::vector<std::pair<std::unordered_map<std::string, std::string>,
                                std::string>>& parent_port_chain,
    const std::unordered_map<std::string, std::string>& enclosing_port_map,
    const std::unordered_map<std::string, std::vector<std::string>>& cont_assigns)
{
    for (auto& member : scope.members()) {
        if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
            auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
            if (gen.isUninstantiated) continue;
            std::string gen_name = gen.getExternalName();
            std::string gen_path = inst_path;
            if (!gen_name.empty())
                gen_path = inst_path + "." + gen_name;
            processScopeForEdges(gen, enclosing_inst, gen_path, output_map,
                                 combined_wire_map, edges, parent_port_chain,
                                 enclosing_port_map, cont_assigns);
        }
        if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
            auto& arr = member.as<slang::ast::GenerateBlockArraySymbol>();
            for (auto* entry : arr.entries) {
                if (!entry || entry->isUninstantiated) continue;
                std::string entry_name = entry->getExternalName();
                if (entry_name.empty())
                    entry_name = std::string(arr.name);
                std::string entry_path = inst_path + "." + entry_name;
                processScopeForEdges(*entry, enclosing_inst, entry_path,
                                     output_map, combined_wire_map, edges,
                                     parent_port_chain, enclosing_port_map,
                                     cont_assigns);
            }
        }
    }
}

static void processInstanceEdges(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, FFNode*>& parent_wire_map,
    std::vector<FFEdge>& edges,
    const std::vector<std::pair<std::unordered_map<std::string, std::string>,
                                std::string>>& parent_port_chain);

static void processScopeForEdges(
    const slang::ast::Scope& scope,
    const slang::ast::InstanceSymbol& enclosing_inst,
    const std::string& path_prefix,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, FFNode*>& combined_wire_map,
    std::vector<FFEdge>& edges,
    const std::vector<std::pair<std::unordered_map<std::string, std::string>,
                                std::string>>& parent_port_chain,
    const std::unordered_map<std::string, std::string>& enclosing_port_map,
    const std::unordered_map<std::string, std::vector<std::string>>& cont_assigns)
{
    for (auto& body_member : scope.members()) {
        // Module instances inside a generate block must still be walked
        // so their cross-clock crossings are reported. The `i_sync` inside
        // `for (genvar i = 0; i < N; i++) begin : g sync u_sync (...); end`
        // is the canonical example.
        if (body_member.kind == slang::ast::SymbolKind::Instance) {
            auto& child = body_member.as<slang::ast::InstanceSymbol>();
            std::string child_path = path_prefix + "." + std::string(child.name);
            auto child_chain = parent_port_chain;
            child_chain.emplace_back(enclosing_port_map,
                                     enclosing_inst.body.name.empty()
                                         ? path_prefix
                                         : path_prefix);
            // Push the enclosing scope's cont_assigns so the child can
            // chase ancestor always_comb / assign renames during port
            // resolution.
            g_parent_cont_chain.push_back(cont_assigns);
            processInstanceEdges(child, child_path, output_map,
                                 combined_wire_map, edges, child_chain);
            g_parent_cont_chain.pop_back();
            continue;
        }

        if (body_member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = body_member.as<slang::ast::ProceduralBlockSymbol>();
            if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF &&
                block.procedureKind != slang::ast::ProceduralBlockKind::Always)
                continue;
            auto& body = block.getBody();
            std::vector<AssignInfo> assignments;
            collectAssignments(body, assignments);
            for (auto& assign : assignments) {
                FFNode* dest = findFFByName(assign.lhs_name, path_prefix,
                                            output_map, enclosing_port_map,
                                            combined_wire_map, parent_port_chain);
                if (!dest) continue;
                for (auto& rhs_sig : assign.rhs_signals) {
                    FFNode* source = findFFByName(rhs_sig, path_prefix,
                                                  output_map, enclosing_port_map,
                                                  combined_wire_map,
                                                  parent_port_chain);
                    if (source && source != dest) {
                        FFEdge edge;
                        edge.source = source;
                        edge.dest = dest;
                        edge.comb_path = assign.rhs_signals;
                        edges.push_back(edge);
                    } else if (!source) {
                        std::vector<FFNode*> resolved;
                        bool has_comb = false;
                        resolveToFFs(rhs_sig, path_prefix, output_map,
                                     enclosing_port_map, combined_wire_map,
                                     cont_assigns, resolved, has_comb, 0,
                                     parent_port_chain);
                        for (auto* src : resolved) {
                            if (src && src != dest) {
                                FFEdge edge;
                                edge.source = src;
                                edge.dest = dest;
                                edge.comb_path = assign.rhs_signals;
                                edge.has_comb_logic = has_comb;
                                edges.push_back(edge);
                            }
                        }
                    }
                }
            }
        }
    }
    // Recurse into nested generate blocks.
    processGenerateBlocks(scope, enclosing_inst, path_prefix, output_map,
                          combined_wire_map, edges, parent_port_chain,
                          enclosing_port_map, cont_assigns);
}

static void processInstanceEdges(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, FFNode*>& parent_wire_map,
    std::vector<FFEdge>& edges,
    const std::vector<std::pair<std::unordered_map<std::string, std::string>,
                                std::string>>& parent_port_chain = {})
{
    // Build port map for this instance (port_name -> actual_signal in parent)
    auto port_map = buildPortMap(inst);

    // Build wire-to-FF map for wires in this instance's scope
    std::unordered_map<std::string, FFNode*> wire_map;
    buildWireToFFMap(inst, inst_path, output_map, wire_map);

    // Merge parent wire map for port resolution
    // (parent_wire_map is used when resolving port signals to wires in grandparent)
    std::unordered_map<std::string, FFNode*> combined_wire_map = parent_wire_map;
    for (auto& [k, v] : wire_map)
        combined_wire_map[k] = v;

    // Collect continuous assigns for combinational path resolution
    std::unordered_map<std::string, std::vector<std::string>> cont_assigns;
    collectContinuousAssigns(inst, cont_assigns);

    for (auto& body_member : inst.body.members()) {
        if (body_member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = body_member.as<slang::ast::ProceduralBlockSymbol>();
            if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF &&
                block.procedureKind != slang::ast::ProceduralBlockKind::Always)
                continue;

            auto& body = block.getBody();
            std::vector<AssignInfo> assignments;
            collectAssignments(body, assignments);

            for (auto& assign : assignments) {
                FFNode* dest = findFFByName(assign.lhs_name, inst_path,
                                            output_map, port_map, combined_wire_map,
                                            parent_port_chain);
                if (!dest) continue;

                for (auto& rhs_sig : assign.rhs_signals) {
                    // First try direct FF lookup
                    FFNode* source = findFFByName(rhs_sig, inst_path,
                                                  output_map, port_map, combined_wire_map,
                                                  parent_port_chain);

                    if (source && source != dest) {
                        FFEdge edge;
                        edge.source = source;
                        edge.dest = dest;
                        edge.comb_path = assign.rhs_signals;
                        edges.push_back(edge);
                    } else if (!source) {
                        // Try resolving through continuous assigns
                        std::vector<FFNode*> resolved;
                        bool has_comb = false;
                        resolveToFFs(rhs_sig, inst_path, output_map, port_map,
                                    combined_wire_map, cont_assigns, resolved,
                                    has_comb, 0, parent_port_chain);
                        for (auto* src : resolved) {
                            if (src && src != dest) {
                                FFEdge edge;
                                edge.source = src;
                                edge.dest = dest;
                                edge.comb_path = assign.rhs_signals;
                                edge.has_comb_logic = has_comb;
                                edges.push_back(edge);
                            }
                        }
                    }
                }
            }
        }

        // Recurse into child instances. Append the current port_map to the
        // chain so deeper descendants can chase a port-to-port connection
        // back through this level. Pass combined_wire_map so wires from
        // any ancestor remain visible. Also push the current instance's
        // cont_assigns onto the parallel ancestor stack so descendants
        // can chase ancestor `assign d_o = d_i;` /
        // `always_comb d_o = d_i;` renames.
        if (body_member.kind == slang::ast::SymbolKind::Instance) {
            auto& child = body_member.as<slang::ast::InstanceSymbol>();
            std::string child_path = inst_path + "." + std::string(child.name);
            auto child_chain = parent_port_chain;
            child_chain.emplace_back(port_map, inst_path);
            g_parent_cont_chain.push_back(cont_assigns);
            processInstanceEdges(child, child_path, output_map, combined_wire_map,
                                 edges, child_chain);
            g_parent_cont_chain.pop_back();
        }
    }

    // Walk generate blocks at this instance level. slang elaborates a
    // `for (genvar i = 0; i < N; i++) begin : gen_x ... end` block into a
    // GenerateBlockArray containing N GenerateBlockSymbol entries; each
    // entry is itself a Scope holding its own procedural blocks and any
    // child module instances. The CDC connectivity tracker needs to walk
    // those blocks too -- otherwise per-bit synchronizers built with a
    // generate-for (the canonical cdc_fifo_gray pattern) are invisible.
    processGenerateBlocks(inst.body, inst, inst_path, output_map,
                          combined_wire_map, edges, parent_port_chain,
                          port_map, cont_assigns);
}

void ConnectivityBuilder::findFFtoFFEdges(
    const std::unordered_map<std::string, FFNode*>& output_map)
{
    auto& root = compilation_.getRoot();
    std::unordered_map<std::string, FFNode*> empty_wire_map;

    for (auto& member : root.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& inst = member.as<slang::ast::InstanceSymbol>();
        std::string inst_path(inst.name);
        processInstanceEdges(inst, inst_path, output_map, empty_wire_map, edges_);
    }
}

void ConnectivityBuilder::analyze() {
    auto output_map = buildFFOutputMap();
    findFFtoFFEdges(output_map);
}

const std::vector<FFEdge>& ConnectivityBuilder::getEdges() const {
    return edges_;
}

} // namespace sv_cdccheck
