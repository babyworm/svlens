#include "sv-cdccheck/clock_tree.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace sv_cdccheck {

ClockTreeAnalyzer::ClockTreeAnalyzer(slang::ast::Compilation& compilation,
                                     ClockDatabase& clock_db)
    : compilation_(compilation), clock_db_(clock_db) {}

void ClockTreeAnalyzer::loadSdc(const SdcConstraints& sdc) {
    sdc_ = sdc;
}

// ── Ac_cdc05: comb-driven clock detector ──

namespace {

bool isCombDriverExpr(const slang::ast::Expression& expr) {
    using EK = slang::ast::ExpressionKind;
    const auto* current = &expr;
    while (current->kind == EK::Conversion)
        current = &current->as<slang::ast::ConversionExpression>().operand();
    return current->kind == EK::ConditionalOp ||
           current->kind == EK::BinaryOp ||
           current->kind == EK::UnaryOp;
}

void scanInstanceForUnsafeClockDrivers(
    const slang::ast::InstanceSymbol& inst,
    ClockDatabase& clock_db,
    const std::unordered_set<std::string>& safe_mux_cells)
{
    // Build a set of "clock signal names" we are watching for.
    std::unordered_set<std::string> clock_signals;
    for (auto& src : clock_db.sources) {
        if (!src->origin_signal.empty()) {
            std::string leaf = src->origin_signal;
            auto dot = leaf.rfind('.');
            if (dot != std::string::npos)
                leaf = leaf.substr(dot + 1);
            clock_signals.insert(leaf);
            clock_signals.insert(src->origin_signal);
            clock_signals.insert(src->name);
        }
    }

    // Track wires driven by safe-cell instance outputs so we do NOT flag
    // them even if a continuous assign cosmetically looks like a comb.
    // Catches `BUFGCTRL u_mux (.O(clk_mux), ...);` where clk_mux later
    // drives flops.
    std::unordered_set<std::string> safe_driven_wires;
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& child = member.as<slang::ast::InstanceSymbol>();
        std::string def_name(child.getDefinition().name);
        if (!safe_mux_cells.count(def_name)) continue;
        for (auto* conn : child.getPortConnections()) {
            if (!conn) continue;
            if (conn->port.kind != slang::ast::SymbolKind::Port) continue;
            auto& port_sym = conn->port.as<slang::ast::PortSymbol>();
            if (port_sym.direction != slang::ast::ArgumentDirection::Out) continue;
            auto* expr = conn->getExpression();
            if (!expr) continue;
            std::string wire;
            if (expr->kind == slang::ast::ExpressionKind::NamedValue) {
                wire = std::string(
                    expr->as<slang::ast::NamedValueExpression>().symbol.name);
            } else if (expr->kind == slang::ast::ExpressionKind::Assignment) {
                auto& assign = expr->as<slang::ast::AssignmentExpression>();
                if (assign.left().kind == slang::ast::ExpressionKind::NamedValue)
                    wire = std::string(
                        assign.left().as<slang::ast::NamedValueExpression>()
                            .symbol.name);
            }
            if (!wire.empty())
                safe_driven_wires.insert(wire);
        }
    }

    // Walk continuous assigns: `assign <clock_lhs> = <comb_rhs>;`.
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::ContinuousAssign) continue;
        auto& ca = member.as<slang::ast::ContinuousAssignSymbol>();
        auto& assignRaw = ca.getAssignment();
        if (assignRaw.kind != slang::ast::ExpressionKind::Assignment) continue;
        auto& assign = assignRaw.as<slang::ast::AssignmentExpression>();
        if (assign.left().kind != slang::ast::ExpressionKind::NamedValue) continue;
        std::string lhs = std::string(
            assign.left().as<slang::ast::NamedValueExpression>().symbol.name);
        if (!clock_signals.count(lhs)) continue;
        if (safe_driven_wires.count(lhs)) continue;
        if (!isCombDriverExpr(assign.right())) continue;
        for (auto& src : clock_db.sources) {
            if (src->origin_signal != lhs && src->name != lhs) continue;
            // Skip SDC-declared clocks: a user that wrote
            // create_generated_clock for this signal asserts it is a
            // properly modelled clock and the synthesis flow will
            // honour it. Same for Primary (top-port) clocks -- if a
            // continuous assign drives a top port that's a degenerate
            // case outside this rule's scope.
            if (src->type == ClockSource::Type::Generated ||
                src->type == ClockSource::Type::Primary)
                continue;
            src->is_unsafe_comb_clock = true;
        }
    }

    // Recurse into child instances; generate blocks rarely host clock
    // muxes but include them for completeness.
    for (auto& member : inst.body.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            scanInstanceForUnsafeClockDrivers(
                member.as<slang::ast::InstanceSymbol>(), clock_db, safe_mux_cells);
        }
    }
}

} // namespace

void ClockTreeAnalyzer::detectUnsafeCombClocks() {
    auto& root = compilation_.getRoot();
    for (auto* inst : root.topInstances) {
        if (!inst) continue;
        scanInstanceForUnsafeClockDrivers(*inst, clock_db_, safe_mux_cells_);
    }
}

void ClockTreeAnalyzer::analyze() {
    // Phase 1a: Identify clock sources
    if (sdc_) {
        importSdcClocks();
    }
    autoDetectClockPorts();

    // Phase 1b: Propagate through hierarchy
    propagateFromRoot();

    // Phase 1b+: Detect PLL/MMCM outputs, clock dividers, and clock gates
    detectPLLOutputs();
    detectClockDividers();
    detectClockGates();

    // Phase 1c: Register relationships
    if (sdc_) {
        importSdcRelationships();
    }
    inferRelationships();
}

// ── Phase 1a: Source identification ──

void ClockTreeAnalyzer::importSdcClocks() {
    for (auto& clk : sdc_->clocks) {
        auto src = std::make_unique<ClockSource>();
        src->id = "sdc_" + clk.name;
        src->name = clk.name;
        src->type = ClockSource::Type::Primary;
        src->period_ns = clk.period;
        src->origin_signal = clk.target;
        clock_db_.addSource(std::move(src));
    }

    for (auto& gen : sdc_->generated_clocks) {
        auto src = std::make_unique<ClockSource>();
        src->id = "sdc_gen_" + gen.name;
        src->name = gen.name;
        src->type = ClockSource::Type::Generated;
        src->origin_signal = gen.target;
        src->divide_by = gen.divide_by;
        src->multiply_by = gen.multiply_by;
        src->invert = gen.invert;

        // Link to master source
        for (auto& existing : clock_db_.sources) {
            if (existing->origin_signal == gen.source_clock ||
                existing->name == gen.source_clock) {
                src->master = existing.get();
                break;
            }
        }
        clock_db_.addSource(std::move(src));
    }
}

void ClockTreeAnalyzer::autoDetectClockPorts() {
    auto& root = compilation_.getRoot();
    for (auto& member : root.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance)
            continue;

        auto& inst = member.as<slang::ast::InstanceSymbol>();
        for (auto& port_member : inst.body.members()) {
            if (port_member.kind != slang::ast::SymbolKind::Port)
                continue;

            auto& port = port_member.as<slang::ast::PortSymbol>();
            std::string port_name(port.name);

            // Reject reset-shaped names even when they technically
            // match isClockName ("clk_rst_n", "ck_reset_n", etc.).
            // The reset-pattern is the dominant semantic -- treating
            // such a port as a clock source produces phantom domains
            // that suppress real CDC crossings.
            if (isClockName(port_name) && !isResetName(port_name)) {
                // Check if SDC already defined this clock
                bool already_defined = false;
                for (auto& src : clock_db_.sources) {
                    if (src->origin_signal == port_name) {
                        already_defined = true;
                        break;
                    }
                }
                if (!already_defined) {
                    auto src = std::make_unique<ClockSource>();
                    src->id = "auto_" + port_name;
                    src->name = port_name;
                    src->type = ClockSource::Type::AutoDetected;
                    src->origin_signal = port_name;
                    clock_db_.addSource(std::move(src));
                }
            }
        }
    }
}

// ── Phase 1b: Hierarchical propagation ──

// Extract signal name from an expression (NamedValueExpression or Assignment for output ports)
static std::string extractSignalNameFromExpr(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        auto& named = expr.as<slang::ast::NamedValueExpression>();
        return std::string(named.symbol.name);
    }
    // Output port connections are modeled as Assignment: wire = port_internal
    if (expr.kind == slang::ast::ExpressionKind::Assignment) {
        auto& assign = expr.as<slang::ast::AssignmentExpression>();
        if (assign.left().kind == slang::ast::ExpressionKind::NamedValue) {
            return std::string(
                assign.left().as<slang::ast::NamedValueExpression>().symbol.name);
        }
    }
    return "";
}

void ClockTreeAnalyzer::propagateFromRoot() {
    // Build initial net map from known sources at top level
    std::unordered_map<std::string, ClockNet*> top_nets;
    for (auto& src : clock_db_.sources) {
        auto net = std::make_unique<ClockNet>();
        net->hier_path = src->origin_signal;
        net->source = src.get();
        auto* net_ptr = clock_db_.addNet(std::move(net));
        top_nets[src->origin_signal] = net_ptr;
    }

    auto& root = compilation_.getRoot();
    for (auto& member : root.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            propagateInstance(member.as<slang::ast::InstanceSymbol>(), top_nets);
        }
    }
}

void ClockTreeAnalyzer::propagateInstance(
    const slang::ast::InstanceSymbol& inst,
    const std::unordered_map<std::string, ClockNet*>& parent_nets,
    const std::string& hier_prefix)
{
    std::string inst_path = hier_prefix.empty()
        ? std::string(inst.name)
        : hier_prefix + "." + std::string(inst.name);
    std::unordered_map<std::string, ClockNet*> local_nets;

    auto port_connections = inst.getPortConnections();

    if (port_connections.empty()) {
        // Root-level instance: no port connections from parent.
        // Seed local_nets directly from parent_nets — the port names at the
        // module boundary correspond to top-level clock source names.
        for (auto& [name, net] : parent_nets) {
            local_nets[name] = net;
        }
    } else {
        // Map port connections: resolve the actual expression to find parent clock net
        for (auto* conn : port_connections) {
            if (!conn) continue;

            auto& port = conn->port;
            std::string port_name(port.name);

            ClockNet* parent_clock_net = nullptr;
            std::string actual_signal;

            // Resolve the actual signal name from the connection expression
            auto* expr = conn->getExpression();
            if (expr) {
                actual_signal = extractSignalNameFromExpr(*expr);
                if (!actual_signal.empty()) {
                    auto it = parent_nets.find(actual_signal);
                    if (it != parent_nets.end()) {
                        parent_clock_net = it->second;
                    }
                }
            }

            // Lazy port-driven clock unification: if the parent
            // expression doesn't have a registered ClockNet but the
            // submodule uses this port in an always_ff sensitivity
            // list, the parent expression IS a clock signal. Auto-
            // register it so the child's FFs share the parent's
            // clock domain. Without this step, parent ports named
            // `cb`, `tck`, `phy_clock`, etc. (which fail the
            // isClockName heuristic) end up as separate domains
            // from the submodule's `clk_i`, creating spurious
            // cross-domain crossings inside what is actually one
            // physical clock.
            if (!parent_clock_net && !actual_signal.empty() &&
                isPortUsedAsClock(inst, port_name)) {
                // Build a scope-qualified key so two physically
                // distinct clocks that happen to share a leaf name
                // in different parent scopes do NOT merge into one
                // ClockSource. parent_scope is `inst_path` with its
                // last segment chopped (the scope in which
                // actual_signal lives, since actual_signal is the
                // parent's expression for this port connection).
                std::string parent_scope;
                auto last_dot = inst_path.rfind('.');
                if (last_dot != std::string::npos) {
                    parent_scope = inst_path.substr(0, last_dot);
                }
                std::string qualified = parent_scope.empty()
                    ? actual_signal
                    : parent_scope + "." + actual_signal;

                ClockSource* src_ptr = nullptr;
                for (auto& s : clock_db_.sources) {
                    if (s->origin_signal == qualified) {
                        src_ptr = s.get();
                        break;
                    }
                }
                if (!src_ptr) {
                    auto src = std::make_unique<ClockSource>();
                    src->id = "auto_port_" + qualified;
                    src->name = actual_signal;       // human-readable leaf
                    src->type = ClockSource::Type::AutoDetected;
                    src->origin_signal = qualified;  // structural identity
                    src_ptr = clock_db_.addSource(std::move(src));
                }
                auto net = std::make_unique<ClockNet>();
                net->hier_path = qualified;
                net->source = src_ptr;
                parent_clock_net = clock_db_.addNet(std::move(net));
            }

            if (parent_clock_net) {
                auto net = std::make_unique<ClockNet>();
                net->hier_path = inst_path + "." + port_name;
                net->source = parent_clock_net->source; // Same source!
                net->edge = parent_clock_net->edge;
                auto* net_ptr = clock_db_.addNet(std::move(net));
                local_nets[port_name] = net_ptr;
            }
        }
    }

    // Collect clocks from always_ff sensitivity lists in this instance
    collectSensitivityClocks(inst, local_nets, inst_path);

    // Recurse into children: regular module instances AND generate
    // blocks. Generate blocks are part of the enclosing module's scope,
    // so they share `local_nets` and any module instance inside them
    // must inherit the parent's clock connections (Finding 4 fix).
    propagateChildren(inst.body, local_nets, inst_path);
}

void ClockTreeAnalyzer::propagateChildren(
    const slang::ast::Scope& scope,
    const std::unordered_map<std::string, ClockNet*>& local_nets,
    const std::string& inst_path)
{
    for (auto& member : scope.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& child = member.as<slang::ast::InstanceSymbol>();
            propagateInstance(child, local_nets, inst_path);
            continue;
        }
        if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
            auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
            if (gen.isUninstantiated) continue;
            std::string gen_name = gen.getExternalName();
            std::string gen_path = inst_path;
            if (!gen_name.empty())
                gen_path = inst_path + "." + gen_name;
            propagateChildren(gen, local_nets, gen_path);
            continue;
        }
        if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
            auto& arr = member.as<slang::ast::GenerateBlockArraySymbol>();
            for (auto* entry : arr.entries) {
                if (!entry || entry->isUninstantiated) continue;
                std::string entry_name = entry->getExternalName();
                if (entry_name.empty())
                    entry_name = std::string(arr.name);
                std::string entry_path = inst_path + "." + entry_name;
                propagateChildren(*entry, local_nets, entry_path);
            }
        }
    }
}

void ClockTreeAnalyzer::collectSensitivityClocks(
    const slang::ast::InstanceSymbol& inst,
    std::unordered_map<std::string, ClockNet*>& local_nets,
    const std::string& inst_path)
{
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::ProceduralBlock)
            continue;

        auto& block = member.as<slang::ast::ProceduralBlockSymbol>();
        if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF &&
            block.procedureKind != slang::ast::ProceduralBlockKind::Always)
            continue;

        auto& body = block.getBody();
        if (body.kind != slang::ast::StatementKind::Timed)
            continue;

        auto& timed = body.as<slang::ast::TimedStatement>();
        auto& timing = timed.timing;

        // Extract events from sensitivity list
        std::vector<std::pair<std::string, Edge>> events;

        auto processEvent = [&](const slang::ast::TimingControl& tc) {
            if (tc.kind != slang::ast::TimingControlKind::SignalEvent)
                return;
            auto& sec = tc.as<slang::ast::SignalEventControl>();
            std::string sig_name = extractSignalNameFromExpr(sec.expr);
            if (sig_name.empty()) return;
            Edge edge = (sec.edge == slang::ast::EdgeKind::PosEdge) ?
                Edge::Posedge : Edge::Negedge;
            events.push_back({sig_name, edge});
        };

        if (timing.kind == slang::ast::TimingControlKind::SignalEvent) {
            processEvent(timing);
        } else if (timing.kind == slang::ast::TimingControlKind::EventList) {
            auto& list = timing.as<slang::ast::EventListControl>();
            for (auto* ev : list.events) {
                if (ev) processEvent(*ev);
            }
        }

        // Classify: find the clock signal (not a reset)
        for (auto& [sig_name, edge] : events) {
            bool looks_clock = isClockName(sig_name);
            bool looks_reset = isResetName(sig_name);

            // Skip resets
            if (looks_reset && !looks_clock)
                continue;

            // If this signal is already a known local net, skip
            if (local_nets.count(sig_name))
                continue;

            // Check if there's already a source for this clock
            ClockSource* found_source = nullptr;
            for (auto& src : clock_db_.sources) {
                if (src->origin_signal == sig_name || src->name == sig_name) {
                    found_source = src.get();
                    break;
                }
            }

            // If no source found and it looks like a clock, create auto-detected
            if (!found_source && looks_clock) {
                auto src = std::make_unique<ClockSource>();
                src->id = "auto_sens_" + sig_name;
                src->name = sig_name;
                src->type = ClockSource::Type::AutoDetected;
                src->origin_signal = sig_name;
                found_source = clock_db_.addSource(std::move(src));
            }

            if (found_source) {
                auto net = std::make_unique<ClockNet>();
                net->hier_path = inst_path + "." + sig_name;
                net->source = found_source;
                net->edge = edge;
                auto* net_ptr = clock_db_.addNet(std::move(net));
                local_nets[sig_name] = net_ptr;
            }
        }
    }
}

// ── Phase 1b+: PLL/MMCM detection ──

static bool isPLLName(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (auto& pat : {"PLL", "MMCM", "DCM", "CLKGEN"}) {
        if (upper.find(pat) != std::string::npos) return true;
    }
    return false;
}

void ClockTreeAnalyzer::detectPLLOutputs() {
    auto& root = compilation_.getRoot();
    for (auto& member : root.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& inst = member.as<slang::ast::InstanceSymbol>();
        detectPLLOutputsInInstance(inst, std::string(inst.name));
    }
}

void ClockTreeAnalyzer::detectPLLOutputsInInstance(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path)
{
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& child = member.as<slang::ast::InstanceSymbol>();
        std::string child_path = inst_path + "." + std::string(child.name);

        auto& def = child.getDefinition();
        std::string def_name(def.name);

        if (isPLLName(def_name)) {
            // Treat output ports of PLL/MMCM instances as primary clock sources
            for (auto& port_member : child.body.members()) {
                if (port_member.kind != slang::ast::SymbolKind::Port) continue;
                auto& port = port_member.as<slang::ast::PortSymbol>();
                if (port.direction != slang::ast::ArgumentDirection::Out) continue;

                std::string port_name(port.name);
                std::string origin = child_path + "." + port_name;

                // Check if already defined
                bool already_exists = false;
                for (auto& src : clock_db_.sources) {
                    if (src->origin_signal == origin || src->name == port_name) {
                        already_exists = true;
                        break;
                    }
                }
                if (already_exists) continue;

                auto src = std::make_unique<ClockSource>();
                src->id = "pll_" + child_path + "_" + port_name;
                src->name = port_name;
                src->type = ClockSource::Type::Primary;
                src->origin_signal = origin;
                clock_db_.addSource(std::move(src));

                // Create a ClockNet for this output
                auto net = std::make_unique<ClockNet>();
                net->hier_path = origin;
                net->source = clock_db_.sources.back().get();
                clock_db_.addNet(std::move(net));
            }
        }

        // Recurse into children
        detectPLLOutputsInInstance(child, child_path);
    }
}

// ── Phase 1b+: Clock divider detection ──

void ClockTreeAnalyzer::collectUsedClockNames() {
    used_clock_names_.clear();
    auto& root = compilation_.getRoot();
    for (auto* inst : root.topInstances) {
        if (!inst) continue;
        collectUsedClockNamesInInstance(*inst);
    }
}

void ClockTreeAnalyzer::collectUsedClockNamesInInstance(
    const slang::ast::InstanceSymbol& inst)
{
    for (auto& member : inst.body.members()) {
        if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = member.as<slang::ast::ProceduralBlockSymbol>();
            if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF &&
                block.procedureKind != slang::ast::ProceduralBlockKind::Always)
                continue;

            auto& body = block.getBody();
            if (body.kind != slang::ast::StatementKind::Timed) continue;
            auto& timed = body.as<slang::ast::TimedStatement>();
            auto& timing = timed.timing;

            auto addFromSignalEvent = [&](const slang::ast::TimingControl& tc) {
                if (tc.kind != slang::ast::TimingControlKind::SignalEvent) return;
                auto& sec = tc.as<slang::ast::SignalEventControl>();
                std::string sig = extractSignalNameFromExpr(sec.expr);
                if (!sig.empty()) used_clock_names_.insert(sig);
            };

            if (timing.kind == slang::ast::TimingControlKind::SignalEvent) {
                addFromSignalEvent(timing);
            } else if (timing.kind == slang::ast::TimingControlKind::EventList) {
                auto& list = timing.as<slang::ast::EventListControl>();
                for (auto* ev : list.events) {
                    if (ev) addFromSignalEvent(*ev);
                }
            }
        }

        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& child = member.as<slang::ast::InstanceSymbol>();
            collectUsedClockNamesInInstance(child);
        }
    }
}

void ClockTreeAnalyzer::detectClockDividers() {
    // Gather structural evidence first: every name that appears on the
    // clock side of an always_ff sensitivity list in the design. A toggle
    // register is only promoted to a generated clock if the register's
    // name is in this set (Finding 1 guard -- otherwise every pulse-sync
    // toggle register is spuriously reclassified as a "div2" clock, which
    // then trips Ac_cdc09 "clock as data" cautions).
    collectUsedClockNames();

    auto& root = compilation_.getRoot();
    for (auto& member : root.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& inst = member.as<slang::ast::InstanceSymbol>();
        detectClockDividersInInstance(inst, std::string(inst.name));
    }
}

void ClockTreeAnalyzer::detectClockDividersInInstance(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path)
{
    for (auto& member : inst.body.members()) {
        if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = member.as<slang::ast::ProceduralBlockSymbol>();
            if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF &&
                block.procedureKind != slang::ast::ProceduralBlockKind::Always)
                continue;

            auto& body = block.getBody();
            if (body.kind != slang::ast::StatementKind::Timed) continue;
            auto& timed = body.as<slang::ast::TimedStatement>();

            // Extract the clock signal from sensitivity list
            std::string clock_name;
            auto& timing = timed.timing;
            if (timing.kind == slang::ast::TimingControlKind::SignalEvent) {
                auto& sec = timing.as<slang::ast::SignalEventControl>();
                clock_name = extractSignalNameFromExpr(sec.expr);
            } else if (timing.kind == slang::ast::TimingControlKind::EventList) {
                auto& list = timing.as<slang::ast::EventListControl>();
                for (auto* ev : list.events) {
                    if (!ev || ev->kind != slang::ast::TimingControlKind::SignalEvent)
                        continue;
                    auto& sec = ev->as<slang::ast::SignalEventControl>();
                    std::string sig = extractSignalNameFromExpr(sec.expr);
                    if (isClockName(sig)) {
                        clock_name = sig;
                        break;
                    }
                }
            }

            if (clock_name.empty()) continue;

            // Look for toggle pattern: q <= ~q or q <= !q
            // Walk the inner statement for assignments where LHS == ~RHS
            checkTogglePattern(timed.stmt, clock_name, inst_path);
        }

        // Recurse into child instances
        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& child = member.as<slang::ast::InstanceSymbol>();
            std::string child_path = inst_path + "." + std::string(child.name);
            detectClockDividersInInstance(child, child_path);
        }
    }
}

void ClockTreeAnalyzer::checkTogglePattern(
    const slang::ast::Statement& stmt,
    const std::string& clock_name,
    const std::string& inst_path)
{
    using SK = slang::ast::StatementKind;
    using EK = slang::ast::ExpressionKind;

    switch (stmt.kind) {
        case SK::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind != EK::Assignment) break;

            auto& assign = expr.as<slang::ast::AssignmentExpression>();
            std::string lhs = extractSignalNameFromExpr(assign.left());
            if (lhs.empty()) break;

            // Check RHS is ~lhs (unary not/bitwise not)
            auto* rhs = &assign.right();
            // Skip conversions
            while (rhs->kind == EK::Conversion)
                rhs = &rhs->as<slang::ast::ConversionExpression>().operand();

            if (rhs->kind == EK::UnaryOp) {
                auto& unary = rhs->as<slang::ast::UnaryExpression>();
                if (unary.op == slang::ast::UnaryOperator::BitwiseNot ||
                    unary.op == slang::ast::UnaryOperator::LogicalNot) {
                    std::string rhs_name = extractSignalNameFromExpr(unary.operand());
                    if (rhs_name == lhs) {
                        // Toggle pattern found: lhs <= ~lhs.
                        // Promote to generated clock only when at least
                        // one of these holds:
                        //   (a) `lhs` actually drives a clock port
                        //       somewhere in the design (structural
                        //       evidence), or
                        //   (b) `lhs` looks like a clock by naming
                        //       convention (e.g. "clk_div2").
                        // Without this guard, every pulse-sync toggle
                        // register -- which names don't start with clk --
                        // ended up registered as a phantom "_div2" clock
                        // and then drove spurious Ac_cdc09 cautions.
                        const bool used_as_clock = used_clock_names_.count(lhs) > 0;
                        const bool looks_like_clock = isClockName(lhs);
                        if (!used_as_clock && !looks_like_clock) {
                            break;
                        }
                        // Create a generated clock source with divide_by 2
                        ClockSource* master_src = nullptr;
                        for (auto& src : clock_db_.sources) {
                            if (src->origin_signal == clock_name ||
                                src->name == clock_name) {
                                master_src = src.get();
                                break;
                            }
                        }

                        // Check if already created
                        std::string div_name = lhs + "_div2";
                        bool already_exists = false;
                        for (auto& src : clock_db_.sources) {
                            if (src->name == div_name) {
                                already_exists = true;
                                break;
                            }
                        }
                        if (already_exists) break;

                        auto src = std::make_unique<ClockSource>();
                        src->id = "divider_" + lhs;
                        src->name = div_name;
                        src->type = ClockSource::Type::Generated;
                        src->origin_signal = inst_path + "." + lhs;
                        src->master = master_src;
                        src->divide_by = 2;
                        clock_db_.addSource(std::move(src));

                        // Create a ClockNet for the divided clock
                        auto net = std::make_unique<ClockNet>();
                        net->hier_path = inst_path + "." + lhs;
                        net->source = clock_db_.sources.back().get();
                        net->is_gated = false;
                        clock_db_.addNet(std::move(net));
                    }
                }
            }
            break;
        }
        case SK::Block: {
            auto& block = stmt.as<slang::ast::BlockStatement>();
            checkTogglePattern(block.body, clock_name, inst_path);
            break;
        }
        case SK::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            for (auto* child : list.list)
                if (child) checkTogglePattern(*child, clock_name, inst_path);
            break;
        }
        case SK::Conditional: {
            auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            checkTogglePattern(cond.ifTrue, clock_name, inst_path);
            if (cond.ifFalse)
                checkTogglePattern(*cond.ifFalse, clock_name, inst_path);
            break;
        }
        default: break;
    }
}

// ── Phase 1b+: Clock gate (ICG) detection ──

// Forward declaration — defined in Helpers section below
static bool matchWordBoundary(const std::string& lower, const char* pattern);

static bool isICGName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Specific patterns — word-boundary matching to avoid false positives
    for (auto& pat : {"icg", "clkgate", "clock_gate", "clk_gate"}) {
        if (matchWordBoundary(lower, pat)) return true;
    }
    // "cg" only as standalone word boundary (not substring of codec_gen, cfg, etc.)
    if (matchWordBoundary(lower, "cg")) return true;
    return false;
}

void ClockTreeAnalyzer::detectClockGates() {
    auto& root = compilation_.getRoot();
    for (auto& member : root.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& inst = member.as<slang::ast::InstanceSymbol>();
        detectClockGatesInInstance(inst, std::string(inst.name));
    }
}

void ClockTreeAnalyzer::detectClockGatesInInstance(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path)
{
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& child = member.as<slang::ast::InstanceSymbol>();
        std::string child_path = inst_path + "." + std::string(child.name);

        // Check definition name for ICG patterns
        auto& def = child.getDefinition();
        std::string def_name(def.name);

        if (isICGName(def_name)) {
            // Find the clock output port and mark its net as gated
            std::string enable_signal;
            std::string clock_out_signal;

            for (auto* conn : child.getPortConnections()) {
                if (!conn) continue;
                std::string port_name(conn->port.name);
                std::string lower_port = port_name;
                std::transform(lower_port.begin(), lower_port.end(),
                               lower_port.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                auto* expr = conn->getExpression();
                std::string actual;
                if (expr) actual = extractSignalNameFromExpr(*expr);

                if (lower_port.find("en") != std::string::npos && !actual.empty())
                    enable_signal = actual;
                if ((lower_port.find("clk") != std::string::npos ||
                     lower_port.find("ck") != std::string::npos) &&
                    lower_port.find("out") != std::string::npos &&
                    !actual.empty())
                    clock_out_signal = actual;
                // Also match Q or GCLK output patterns
                if ((lower_port == "q" || lower_port == "gclk" ||
                     lower_port == "clk_out" || lower_port == "eclk") &&
                    !actual.empty())
                    clock_out_signal = actual;
            }

            // Mark clock nets as gated
            if (!clock_out_signal.empty()) {
                for (auto& net : clock_db_.nets) {
                    if (net->hier_path.find(clock_out_signal) != std::string::npos) {
                        net->is_gated = true;
                        net->gate_enable = enable_signal;
                    }
                }
                // Also create a gated clock net if not found
                auto net = std::make_unique<ClockNet>();
                net->hier_path = inst_path + "." + clock_out_signal;
                net->is_gated = true;
                net->gate_enable = enable_signal;
                // Try to find the source from input clock port
                for (auto* conn : child.getPortConnections()) {
                    if (!conn) continue;
                    std::string port_name(conn->port.name);
                    std::string lower_port = port_name;
                    std::transform(lower_port.begin(), lower_port.end(),
                                   lower_port.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if ((lower_port.find("clk") != std::string::npos ||
                         lower_port.find("ck") != std::string::npos) &&
                        lower_port.find("out") == std::string::npos &&
                        lower_port != "q" && lower_port != "gclk" &&
                        lower_port != "eclk") {
                        auto* expr = conn->getExpression();
                        if (expr) {
                            std::string in_clk = extractSignalNameFromExpr(*expr);
                            for (auto& src : clock_db_.sources) {
                                if (src->origin_signal == in_clk ||
                                    src->name == in_clk) {
                                    net->source = src.get();
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
                clock_db_.addNet(std::move(net));
            }
        }

        // Recurse
        detectClockGatesInInstance(child, child_path);
    }
}

// ── Phase 1c: Relationship registration ──

void ClockTreeAnalyzer::importSdcRelationships() {
    for (auto& group : sdc_->clock_groups) {
        DomainRelationship::Type rel_type;
        switch (group.type) {
            case SdcClockGroup::Type::Asynchronous:
                rel_type = DomainRelationship::Type::Asynchronous; break;
            case SdcClockGroup::Type::Exclusive:
                rel_type = DomainRelationship::Type::PhysicallyExclusive; break;
            case SdcClockGroup::Type::LogicallyExclusive:
                rel_type = DomainRelationship::Type::LogicallyExclusive; break;
        }

        // Handle single-group case: all unlisted clocks form implicit "other" group
        if (group.groups.size() == 1) {
            // Collect all clock names that are in the explicit group
            std::unordered_set<std::string> listed_names(
                group.groups[0].begin(), group.groups[0].end());

            // Build implicit "other" group from all clock sources not listed
            std::vector<std::string> other_group;
            for (auto& src : clock_db_.sources) {
                if (listed_names.find(src->name) == listed_names.end()) {
                    other_group.push_back(src->name);
                }
            }

            // If there are unlisted clocks, add as implicit group and proceed
            if (!other_group.empty()) {
                group.groups.push_back(other_group);
            }
        }

        // Register pairwise relationships between groups
        for (size_t i = 0; i < group.groups.size(); i++) {
            for (size_t j = i + 1; j < group.groups.size(); j++) {
                for (auto& name_a : group.groups[i]) {
                    for (auto& name_b : group.groups[j]) {
                        ClockSource* src_a = nullptr;
                        ClockSource* src_b = nullptr;
                        for (auto& s : clock_db_.sources) {
                            if (s->name == name_a) src_a = s.get();
                            if (s->name == name_b) src_b = s.get();
                        }
                        if (src_a && src_b) {
                            clock_db_.relationships.push_back(
                                {src_a, src_b, rel_type, /*sdc_declared=*/true});
                        }
                    }
                }
            }
        }
    }
}

// Walk master chain to find ultimate root clock source
static ClockSource* rootSource(ClockSource* s) {
    while (s && s->master) s = s->master;
    return s;
}

void ClockTreeAnalyzer::inferRelationships() {
    // For sources sharing a master: mark as Divided
    // For unrelated auto-detected sources: conservatively mark as Asynchronous
    for (size_t i = 0; i < clock_db_.sources.size(); i++) {
        for (size_t j = i + 1; j < clock_db_.sources.size(); j++) {
            auto* a = clock_db_.sources[i].get();
            auto* b = clock_db_.sources[j].get();

            // Skip if relationship already defined (e.g., from SDC)
            bool already_defined = false;
            for (auto& rel : clock_db_.relationships) {
                if ((rel.a == a && rel.b == b) || (rel.a == b && rel.b == a)) {
                    already_defined = true;
                    break;
                }
            }
            if (already_defined) continue;

            // Check if they share a common root source
            auto* rootA = rootSource(a);
            auto* rootB = rootSource(b);

            if (rootA == rootB) {
                // Same root → divided/related
                clock_db_.relationships.push_back(
                    {a, b, DomainRelationship::Type::Divided});
            } else {
                // Different root sources → assume async
                clock_db_.relationships.push_back(
                    {a, b, DomainRelationship::Type::Asynchronous});
            }
        }
    }
}

// ── Helpers ──

// Check for pattern at word boundaries (start-of-string or '_', end-of-string or '_')
static bool matchWordBoundary(const std::string& lower, const char* pattern) {
    size_t plen = std::strlen(pattern);
    auto pos = lower.find(pattern);
    while (pos != std::string::npos) {
        size_t end = pos + plen;
        bool start_ok = (pos == 0 || lower[pos - 1] == '_');
        bool end_ok = (end == lower.size() || lower[end] == '_');
        if (start_ok && end_ok) return true;
        pos = lower.find(pattern, pos + 1);
    }
    return false;
}

// Recursively scan a scope (instance body, generate block, generate
// block array entry) for any AlwaysFF block whose sensitivity list
// references `port_name` with a clock edge. Used by
// isPortUsedAsClock so that genvar-wrapped synchronizers like
// `for (genvar i...) begin sync_shift u_sync (.clk_i(...)); end`
// still surface their inner clock edge to the parent's port-driven
// unification logic.
static bool scopeUsesPortAsClock(const slang::ast::Scope& scope,
                                 const std::string& port_name) {
    for (auto& member : scope.members()) {
        if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = member.as<slang::ast::ProceduralBlockSymbol>();
            if (block.procedureKind !=
                slang::ast::ProceduralBlockKind::AlwaysFF)
                continue;
            auto& body = block.getBody();
            if (body.kind != slang::ast::StatementKind::Timed) continue;
            auto& timed = body.as<slang::ast::TimedStatement>();
            auto& timing = timed.timing;
            auto check_event = [&](const slang::ast::TimingControl& tc) -> bool {
                if (tc.kind != slang::ast::TimingControlKind::SignalEvent)
                    return false;
                auto& sec = tc.as<slang::ast::SignalEventControl>();
                if (extractSignalNameFromExpr(sec.expr) != port_name)
                    return false;
                return sec.edge == slang::ast::EdgeKind::PosEdge ||
                       sec.edge == slang::ast::EdgeKind::NegEdge;
            };
            if (timing.kind == slang::ast::TimingControlKind::SignalEvent) {
                if (check_event(timing)) return true;
            } else if (timing.kind ==
                       slang::ast::TimingControlKind::EventList) {
                auto& list = timing.as<slang::ast::EventListControl>();
                for (auto* ev : list.events) {
                    if (ev && check_event(*ev)) return true;
                }
            }
            continue;
        }
        // Recurse into generate blocks (single + array). The
        // canonical pulp/cdc_fifo_gray pattern wraps sync.sv inside
        // a `for (genvar i...) begin : g_sync ... end`.
        if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
            auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
            if (gen.isUninstantiated) continue;
            if (scopeUsesPortAsClock(gen, port_name)) return true;
        }
        if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
            auto& arr = member.as<slang::ast::GenerateBlockArraySymbol>();
            for (auto* entry : arr.entries) {
                if (!entry || entry->isUninstantiated) continue;
                if (scopeUsesPortAsClock(*entry, port_name)) return true;
            }
        }
    }
    return false;
}

bool ClockTreeAnalyzer::isPortUsedAsClock(
    const slang::ast::InstanceSymbol& inst,
    const std::string& port_name)
{
    // Reject reset-named ports outright. async resets (`negedge
    // rst_n` / `negedge rst_ni`) appear in the sensitivity list
    // exactly the same way clocks do, but they are NOT clocks --
    // treating them as such would auto-register the reset signal
    // as a ClockSource and leak phantom domains.
    if (isResetName(port_name)) return false;
    return scopeUsesPortAsClock(inst.body, port_name);
}

bool ClockTreeAnalyzer::isClockName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto& pattern : {"clk", "clock", "ck"}) {
        if (matchWordBoundary(lower, pattern)) return true;
    }
    return false;
}

bool ClockTreeAnalyzer::isResetName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Check longer patterns first to match correctly (rst_n before rst)
    for (auto& pattern : {"reset", "rstn", "rst_n", "rst"}) {
        if (matchWordBoundary(lower, pattern)) return true;
    }
    return false;
}

} // namespace sv_cdccheck
