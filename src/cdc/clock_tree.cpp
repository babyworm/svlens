#include "sv-cdccheck/clock_tree.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
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

namespace sv_cdccheck {

ClockTreeAnalyzer::ClockTreeAnalyzer(slang::ast::Compilation& compilation,
                                     ClockDatabase& clock_db)
    : compilation_(compilation), clock_db_(clock_db) {}

void ClockTreeAnalyzer::loadSdc(const SdcConstraints& sdc) {
    sdc_ = sdc;
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

            if (isClockName(port_name)) {
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

            // Resolve the actual signal name from the connection expression
            auto* expr = conn->getExpression();
            if (expr) {
                std::string actual_signal = extractSignalNameFromExpr(*expr);
                if (!actual_signal.empty()) {
                    auto it = parent_nets.find(actual_signal);
                    if (it != parent_nets.end()) {
                        parent_clock_net = it->second;
                    }
                }
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

    // Recurse into child instances
    for (auto& child : inst.body.membersOfType<slang::ast::InstanceSymbol>()) {
        propagateInstance(child, local_nets, inst_path);
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
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
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

void ClockTreeAnalyzer::detectClockDividers() {
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
                        // Toggle pattern found: lhs <= ~lhs
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

static bool isICGName(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    for (auto& pat : {"ICG", "CLKGATE", "CG", "CLOCK_GATE"}) {
        std::string upat(pat);
        if (upper.find(upat) != std::string::npos) return true;
    }
    // Also check lowercase
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("clock_gate") != std::string::npos) return true;
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
                               lower_port.begin(), ::tolower);

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
                                   lower_port.begin(), ::tolower);
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
                                {src_a, src_b, rel_type});
                        }
                    }
                }
            }
        }
    }
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

            // Same master → related/divided
            if (a->master && a->master == b->master) {
                clock_db_.relationships.push_back(
                    {a, b, DomainRelationship::Type::Divided});
            } else if (a->master == b || b->master == a) {
                clock_db_.relationships.push_back(
                    {a, b, DomainRelationship::Type::Divided});
            } else {
                // Different sources, no known relationship → assume async
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

bool ClockTreeAnalyzer::isClockName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& pattern : {"clk", "clock", "ck"}) {
        if (matchWordBoundary(lower, pattern)) return true;
    }
    return false;
}

bool ClockTreeAnalyzer::isResetName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    // Check longer patterns first to match correctly (rst_n before rst)
    for (auto& pattern : {"reset", "rstn", "rst_n", "rst"}) {
        if (matchWordBoundary(lower, pattern)) return true;
    }
    return false;
}

} // namespace sv_cdccheck
