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
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Expression.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Statement.h"
#include "slang/ast/types/AllTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
    skipped_sdc_relationship_groups_ = 0;
    // Phase 1a: Identify clock sources
    if (sdc_) {
        importSdcClocks();
    }
    autoDetectClockPorts();

    // Phase 1b: Propagate through hierarchy
    propagateTransparentAliases();
    propagateFromRoot();

    // Phase 1b+: Detect PLL/MMCM outputs, clock dividers, and clock gates
    detectPLLOutputs();
    detectClockDividers();
    detectClockGates();
    propagateGeneratedPeriods();

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
        if (clk.period && std::isfinite(*clk.period) && *clk.period > 0)
            src->period_ns = clk.period;
        src->origin_signal = clk.target;
        clock_db_.addSource(std::move(src));
    }

    std::vector<std::pair<ClockSource*, std::string>> generated;
    for (auto& gen : sdc_->generated_clocks) {
        auto src = std::make_unique<ClockSource>();
        src->id = "sdc_gen_" + gen.name;
        src->name = gen.name;
        src->type = ClockSource::Type::Generated;
        src->origin_signal = gen.target;
        src->divide_by = gen.divide_by;
        src->multiply_by = gen.multiply_by;
        src->invert = gen.invert;

        auto* added = clock_db_.addSource(std::move(src));
        generated.emplace_back(added, gen.source_clock);
    }

    // Resolve after all generated sources exist so a child can precede its
    // master in SDC. Ambiguous names, invalid ratios, and master cycles are
    // left unlinked rather than creating a false related-clock relationship.
    std::unordered_map<ClockSource*, ClockSource*> proposed;
    for (const auto& [child, sourceName] : generated) {
        if (sourceName.empty() || child->divide_by <= 0 || child->multiply_by <= 0)
            continue;
        ClockSource* candidate = nullptr;
        bool ambiguous = false;
        for (const auto& source : clock_db_.sources) {
            if (source->name != sourceName && source->origin_signal != sourceName)
                continue;
            if (candidate && candidate != source.get()) {
                ambiguous = true;
                break;
            }
            candidate = source.get();
        }
        if (!ambiguous && candidate)
            proposed[child] = candidate;
    }
    for (const auto& [child, candidate] : proposed) {
        std::unordered_set<ClockSource*> visited;
        ClockSource* current = child;
        ClockSource* terminal = nullptr;
        while (current && visited.insert(current).second) {
            if (auto next = proposed.find(current); next != proposed.end()) {
                current = next->second;
            } else if (current->master) {
                current = current->master;
            } else {
                terminal = current;
                current = nullptr;
            }
        }
        const bool validTerminal =
            terminal && (terminal->type != ClockSource::Type::Generated ||
                         (terminal->period_ns && std::isfinite(*terminal->period_ns) && *terminal->period_ns > 0));
        if (!current && validTerminal)
            child->master = candidate;
    }
}

void ClockTreeAnalyzer::propagateGeneratedPeriods() {
    // Period is timing context, not proof of a stable capture window. Only a
    // finite positive master period and ratio support a derived value.
    for (size_t iteration = 0; iteration < clock_db_.sources.size(); ++iteration) {
        bool changed = false;
        for (const auto& source : clock_db_.sources) {
            if (source->type != ClockSource::Type::Generated || source->period_ns || !source->master ||
                !source->master->period_ns || source->divide_by <= 0 || source->multiply_by <= 0)
                continue;
            const double period = *source->master->period_ns * static_cast<double>(source->divide_by) /
                                  static_cast<double>(source->multiply_by);
            if (std::isfinite(period) && period > 0) {
                source->period_ns = period;
                changed = true;
            }
        }
        if (!changed)
            break;
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

// Extract a parent-scope signal path from a port connection expression.
static std::string extractSignalNameFromExpr(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        auto& named = expr.as<slang::ast::NamedValueExpression>();
        return std::string(named.symbol.name);
    }
    if (expr.kind == slang::ast::ExpressionKind::MemberAccess) {
        auto& member = expr.as<slang::ast::MemberAccessExpression>();
        auto base = extractSignalNameFromExpr(member.value());
        return base.empty() ? std::string{} : base + "." + std::string(member.member.name);
    }
    if (expr.kind == slang::ast::ExpressionKind::Conversion)
        return extractSignalNameFromExpr(expr.as<slang::ast::ConversionExpression>().operand());
    // Output port connections are modeled as Assignment: wire = port_internal
    if (expr.kind == slang::ast::ExpressionKind::Assignment) {
        auto& assign = expr.as<slang::ast::AssignmentExpression>();
        return extractSignalNameFromExpr(assign.left());
    }
    return "";
}

static ClockSource* rootSource(ClockSource* source) {
    std::unordered_set<ClockSource*> visited;
    while (source && source->master) {
        if (!visited.insert(source).second)
            return nullptr;
        source = source->master;
    }
    return source;
}

// Alias edges must use the signal's declaring scope. A reference inside a
// generate block can name a port declared on the enclosing module, while a
// clock net declared inside the block needs its generated path.
static std::string extractSignalPathFromExpr(const slang::ast::Expression& expr, const std::string& scope_path) {
    using EK = slang::ast::ExpressionKind;
    if (expr.kind == EK::NamedValue) {
        const auto& symbol = expr.as<slang::ast::NamedValueExpression>().symbol;
        auto path = symbol.getHierarchicalPath();
        return path.empty() ? scope_path + "." + std::string(symbol.name) : path;
    }
    if (expr.kind == EK::HierarchicalValue)
        return expr.as<slang::ast::HierarchicalValueExpression>().symbol.getHierarchicalPath();
    if (expr.kind == EK::MemberAccess) {
        const auto& member = expr.as<slang::ast::MemberAccessExpression>();
        const auto base = extractSignalPathFromExpr(member.value(), scope_path);
        return base.empty() ? std::string{} : base + "." + std::string(member.member.name);
    }
    if (expr.kind == EK::Conversion)
        return extractSignalPathFromExpr(expr.as<slang::ast::ConversionExpression>().operand(), scope_path);
    if (expr.kind == EK::Assignment)
        return extractSignalPathFromExpr(expr.as<slang::ast::AssignmentExpression>().left(), scope_path);
    return {};
}

static std::string directAliasPathFromExpr(const slang::ast::Expression& expr, const std::string& scope_path) {
    using EK = slang::ast::ExpressionKind;
    if (expr.kind == EK::Conversion) {
        const auto& conversion = expr.as<slang::ast::ConversionExpression>();
        if (!expr.type || !conversion.operand().type ||
            expr.type->getBitWidth() != conversion.operand().type->getBitWidth() ||
            expr.type->isFourState() != conversion.operand().type->isFourState())
            return {};
        return directAliasPathFromExpr(conversion.operand(), scope_path);
    }
    if (expr.kind == EK::Assignment)
        return directAliasPathFromExpr(expr.as<slang::ast::AssignmentExpression>().left(), scope_path);
    if (expr.kind == EK::MemberAccess) {
        const auto& member = expr.as<slang::ast::MemberAccessExpression>();
        auto base = directAliasPathFromExpr(member.value(), scope_path);
        return base.empty() ? std::string{} : base + "." + std::string(member.member.name);
    }
    if (expr.kind == EK::NamedValue || expr.kind == EK::HierarchicalValue) {
        const auto& symbol = expr.as<slang::ast::ValueExpressionBase>().symbol;
        if (symbol.kind == slang::ast::SymbolKind::Parameter || symbol.kind == slang::ast::SymbolKind::TypeParameter ||
            symbol.kind == slang::ast::SymbolKind::EnumValue)
            return {};
        return extractSignalPathFromExpr(expr, scope_path);
    }
    return {};
}

struct SelectedElementPath {
    std::string base;
    std::string bit;
};

static std::optional<SelectedElementPath> selectedIntegralElementPath(const slang::ast::Expression& expr,
                                                                      const std::string& scope_path) {
    using EK = slang::ast::ExpressionKind;
    const slang::ast::Expression* selected = &expr;
    while (selected->kind == EK::Assignment || selected->kind == EK::Conversion) {
        if (selected->kind == EK::Assignment)
            selected = &selected->as<slang::ast::AssignmentExpression>().left();
        else {
            const auto& conversion = selected->as<slang::ast::ConversionExpression>();
            if (!selected->type || !conversion.operand().type ||
                selected->type->getBitWidth() != conversion.operand().type->getBitWidth() ||
                selected->type->isFourState() != conversion.operand().type->isFourState())
                return std::nullopt;
            selected = &conversion.operand();
        }
    }
    if (selected->kind != EK::ElementSelect || !selected->type || !selected->type->isIntegral())
        return std::nullopt;
    const auto& select = selected->as<slang::ast::ElementSelectExpression>();
    const auto* index = select.selector().getConstant();
    if (!select.value().type || !select.value().type->isIntegral() || !select.value().type->hasFixedRange() || !index ||
        !*index || !index->isInteger())
        return std::nullopt;
    const auto value = index->integer().as<int64_t>();
    const auto range = select.value().type->getFixedRange();
    if (!value || *value < range.lower() || *value > range.upper())
        return std::nullopt;
    auto base = extractSignalPathFromExpr(select.value(), scope_path);
    if (base.empty())
        return std::nullopt;
    auto bit = base + "[" + std::to_string(*value) + "]";
    return SelectedElementPath{std::move(base), std::move(bit)};
}

static std::string oneBitInversionSource(const slang::ast::Expression& expr, const std::string& scope_path) {
    using EK = slang::ast::ExpressionKind;
    const slang::ast::Expression* inverted = &expr;
    while (inverted->kind == EK::Conversion) {
        const auto& conversion = inverted->as<slang::ast::ConversionExpression>();
        if (!inverted->type || !conversion.operand().type || inverted->type->getBitWidth() != 1 ||
            conversion.operand().type->getBitWidth() != 1 ||
            inverted->type->isFourState() != conversion.operand().type->isFourState())
            return {};
        inverted = &conversion.operand();
    }
    if (inverted->kind != EK::UnaryOp)
        return {};
    const auto& unary = inverted->as<slang::ast::UnaryExpression>();
    if ((unary.op != slang::ast::UnaryOperator::BitwiseNot && unary.op != slang::ast::UnaryOperator::LogicalNot) ||
        !unary.operand().type || unary.operand().type->getBitWidth() != 1)
        return {};
    auto source = directAliasPathFromExpr(unary.operand(), scope_path);
    if (!source.empty())
        return source;
    if (const auto selected = selectedIntegralElementPath(unary.operand(), scope_path);
        selected && unary.operand().type->getBitWidth() == 1)
        return selected->bit;
    return {};
}

static bool isCompileTimeValue(const slang::ast::Expression& expr) {
    using EK = slang::ast::ExpressionKind;
    const slang::ast::Expression* current = &expr;
    while (current->kind == EK::Conversion || current->kind == EK::MemberAccess || current->kind == EK::ElementSelect ||
           current->kind == EK::RangeSelect) {
        if (current->kind == EK::Conversion)
            current = &current->as<slang::ast::ConversionExpression>().operand();
        else if (current->kind == EK::MemberAccess)
            current = &current->as<slang::ast::MemberAccessExpression>().value();
        else if (current->kind == EK::ElementSelect)
            current = &current->as<slang::ast::ElementSelectExpression>().value();
        else
            current = &current->as<slang::ast::RangeSelectExpression>().value();
    }
    if (current->kind != EK::NamedValue && current->kind != EK::HierarchicalValue)
        return false;
    const auto& symbol = current->as<slang::ast::ValueExpressionBase>().symbol;
    return symbol.kind == slang::ast::SymbolKind::Parameter || symbol.kind == slang::ast::SymbolKind::TypeParameter ||
           symbol.kind == slang::ast::SymbolKind::EnumValue;
}

static std::vector<std::string> expressionDependencies(const slang::ast::Expression& expr,
                                                       const std::string& scope_path) {
    std::vector<std::string> dependencies;
    auto add = [&](std::string path) {
        if (!path.empty())
            dependencies.push_back(std::move(path));
    };
    auto visitor = slang::ast::makeVisitor(
        [&](auto& self, const slang::ast::ElementSelectExpression& selected) {
            if (const auto path = selectedIntegralElementPath(selected, scope_path);
                path && !isCompileTimeValue(selected))
                add(path->bit);
            else
                self.visitDefault(selected);
        },
        [&](auto& self, const slang::ast::MemberAccessExpression& member) {
            if (!isCompileTimeValue(member)) {
                auto path = extractSignalPathFromExpr(member, scope_path);
                if (!path.empty()) {
                    add(std::move(path));
                    return;
                }
            }
            self.visitDefault(member);
        },
        [&](auto&, const slang::ast::NamedValueExpression& named) {
            if (!isCompileTimeValue(named))
                add(extractSignalPathFromExpr(named, scope_path));
        },
        [&](auto&, const slang::ast::HierarchicalValueExpression& named) {
            if (!isCompileTimeValue(named))
                add(extractSignalPathFromExpr(named, scope_path));
        },
        [&](auto&, const slang::ast::ArbitrarySymbolExpression& symbol) {
            if (symbol.symbol && symbol.symbol->kind != slang::ast::SymbolKind::Parameter &&
                symbol.symbol->kind != slang::ast::SymbolKind::TypeParameter &&
                symbol.symbol->kind != slang::ast::SymbolKind::EnumValue)
                add(symbol.symbol->getHierarchicalPath());
        },
        [&](auto& self, const slang::ast::ConditionalExpression& conditional) {
            if (const auto* known = conditional.knownSide())
                known->visit(self);
            else
                self.visitDefault(conditional);
        });
    expr.visit(visitor);
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    return dependencies;
}

static const slang::ast::AssignmentExpression* soleUnconditionalAssignment(const slang::ast::Statement& stmt) {
    using SK = slang::ast::StatementKind;
    if (stmt.kind == SK::ExpressionStatement) {
        const auto& expr = stmt.as<slang::ast::ExpressionStatement>().expr;
        return expr.kind == slang::ast::ExpressionKind::Assignment ? &expr.as<slang::ast::AssignmentExpression>()
                                                                   : nullptr;
    }
    if (stmt.kind == SK::Block)
        return soleUnconditionalAssignment(stmt.as<slang::ast::BlockStatement>().body);
    if (stmt.kind == SK::List) {
        const auto& list = stmt.as<slang::ast::StatementList>().list;
        return list.size() == 1 && list[0] ? soleUnconditionalAssignment(*list[0]) : nullptr;
    }
    return nullptr;
}

void ClockTreeAnalyzer::propagateTransparentAliases() {
    // Direct assignments and directed port connections form a clock-alias
    // graph. Compute it before the ordinary top-down walk so a clock-manager
    // output can reach a consumer declared earlier in its parent module.
    // Conditional, arithmetic, and gated expressions are deliberately absent.
    std::unordered_map<std::string, std::vector<std::string>> aliases;
    std::unordered_map<std::string, std::vector<std::string>> root_edges;
    std::unordered_map<std::string, std::vector<std::string>> resetInversions;
    std::unordered_map<std::string, std::vector<std::string>> resetSelectedAliases;
    std::unordered_map<std::string, std::vector<std::string>> resetIndexedFieldAliases;
    std::unordered_map<std::string, ResetMuxInputs> resetMuxInputs;
    auto addAlias = [&aliases, &root_edges](std::string from, std::string to) {
        if (!from.empty() && !to.empty()) {
            aliases[from].push_back(to);
            root_edges[std::move(from)].push_back(std::move(to));
        }
    };
    auto recordResetInversion = [&](const slang::ast::AssignmentExpression& expr, const std::string& path) {
        if (expr.isNonBlocking() || expr.isCompound() || expr.timingControl || !expr.left().type ||
            expr.left().type->getBitWidth() != 1)
            return;
        auto lhs = directAliasPathFromExpr(expr.left(), path);
        if (lhs.empty())
            return;
        auto source = oneBitInversionSource(expr.right(), path);
        if (!source.empty())
            resetInversions[std::move(source)].push_back(std::move(lhs));
    };

    auto visitScope = [&](auto&& self, const slang::ast::Scope& scope, const std::string& path,
                          const std::string& parent_path, const slang::ast::InstanceSymbol* inst) -> void {
        if (inst && !parent_path.empty()) {
            const std::string definition(inst->getDefinition().name);
            if (definition == "prim_clock_buf" || definition == "prim_generic_clock_buf") {
                addAlias(path + ".clk_i", path + ".clk_o");
            } else if (definition == "prim_clock_mux2" || definition == "prim_generic_clock_mux2") {
                auto hasPort = [&](const char* name, slang::ast::ArgumentDirection direction) {
                    for (const auto* member : inst->body.getPortList()) {
                        if (!member || member->kind != slang::ast::SymbolKind::Port || member->name != name)
                            continue;
                        const auto& port = member->as<slang::ast::PortSymbol>();
                        return port.direction == direction && port.getType().getBitWidth() == 1;
                    }
                    return false;
                };
                if (hasPort("clk0_i", slang::ast::ArgumentDirection::In) &&
                    hasPort("clk1_i", slang::ast::ArgumentDirection::In) &&
                    hasPort("sel_i", slang::ast::ArgumentDirection::In) &&
                    hasPort("clk_o", slang::ast::ArgumentDirection::Out)) {
                    resetMuxInputs.emplace(path + ".clk_o",
                                           ResetMuxInputs{path + ".clk0_i", path + ".clk1_i", path + ".sel_i"});
                }
            } else if (definition == "prim_clock_gating" || definition == "prim_generic_clock_gating" ||
                       definition == "prim_clock_gating_sync" || definition == "prim_clock_div") {
                // Gate/divider output shares an origin, not a clock domain.
                // This edge is never added to the transparent alias graph.
                root_edges[path + ".clk_i"].push_back(path + ".clk_o");
            }
            for (auto* conn : inst->getPortConnections()) {
                if (!conn || conn->port.kind != slang::ast::SymbolKind::Port)
                    continue;
                auto* expr = conn->getExpression();
                if (!expr)
                    continue;
                const auto& port = conn->port.as<slang::ast::PortSymbol>();
                const std::string inner = path + "." + std::string(port.name);
                auto outer = directAliasPathFromExpr(*expr, parent_path);
                const auto selectedBit = outer.empty() && port.getType().getBitWidth() == 1
                                             ? selectedIntegralElementPath(*expr, parent_path)
                                             : std::nullopt;
                if (selectedBit) {
                    if (port.direction == slang::ast::ArgumentDirection::In) {
                        // Keep the aggregate candidate for a vector FF driving
                        // the bit, plus its separate indexed FF candidate.
                        resetSelectedAliases[selectedBit->base].push_back(inner);
                        resetSelectedAliases[selectedBit->bit].push_back(inner);
                    } else if (port.direction == slang::ast::ArgumentDirection::Out) {
                        resetSelectedAliases[inner].push_back(selectedBit->bit);
                    }
                }
                if (outer.empty() && !selectedBit && port.direction == slang::ast::ArgumentDirection::In &&
                    port.getType().getBitWidth() == 1) {
                    auto invertedSource = oneBitInversionSource(*expr, parent_path);
                    if (!invertedSource.empty())
                        resetInversions[std::move(invertedSource)].push_back(inner);
                }
                if (auto mux = resetMuxInputs.find(path + ".clk_o");
                    mux != resetMuxInputs.end() && port.direction == slang::ast::ArgumentDirection::In) {
                    const auto source = !outer.empty() ? outer : selectedBit ? selectedBit->bit : std::string{};
                    const auto dependencies =
                        source.empty() ? expressionDependencies(*expr, parent_path) : std::vector<std::string>{};
                    if (port.name == "clk0_i") {
                        mux->second.input0_source = source;
                        mux->second.input0_dependencies = dependencies;
                    } else if (port.name == "clk1_i") {
                        mux->second.input1_source = source;
                        mux->second.input1_dependencies = dependencies;
                    } else if (port.name == "sel_i") {
                        mux->second.select_source = source;
                        mux->second.select_dependencies = dependencies;
                        const auto* cached = expr->getConstant();
                        if (cached && *cached && cached->isInteger() && !cached->hasUnknown()) {
                            mux->second.selected_input = cached->isTrue();
                        } else if (!cached) {
                            slang::ast::EvalContext context(*inst);
                            const auto value = expr->eval(context);
                            if (value && value.isInteger() && !value.hasUnknown())
                                mux->second.selected_input = value.isTrue();
                        }
                    }
                }
                if (outer.empty())
                    continue;
                auto link = [&](const std::string& suffix, bool indexedResetField = false) {
                    if (port.direction == slang::ast::ArgumentDirection::In) {
                        addAlias(outer + suffix, inner + suffix);
                        if (indexedResetField)
                            resetIndexedFieldAliases[outer + suffix].push_back(inner + suffix);
                    } else if (port.direction == slang::ast::ArgumentDirection::Out) {
                        addAlias(inner + suffix, outer + suffix);
                        if (indexedResetField)
                            resetIndexedFieldAliases[inner + suffix].push_back(outer + suffix);
                    }
                };

                auto linkField = [&](const slang::ast::Symbol& field) {
                    if (field.kind != slang::ast::SymbolKind::Field)
                        return;
                    const std::string fieldName(field.name);
                    const bool resetField = isResetName(fieldName);
                    if (!resetField && !isClockName(fieldName))
                        return;
                    const auto& fieldType = field.as<slang::ast::FieldSymbol>().getType();
                    const bool indexedResetField = resetField && fieldType.isIntegral() && fieldType.hasFixedRange() &&
                                                   fieldType.getBitWidth() > 1 &&
                                                   fieldType.getFixedRange().fullWidth() == fieldType.getBitWidth();
                    link("." + fieldName, indexedResetField);
                };

                const auto& type = port.getType().getCanonicalType();
                if (type.kind == slang::ast::SymbolKind::PackedStructType) {
                    for (const auto& field : type.as<slang::ast::PackedStructType>().members())
                        linkField(field);
                } else if (type.kind == slang::ast::SymbolKind::UnpackedStructType) {
                    for (const auto& field : type.as<slang::ast::UnpackedStructType>().members())
                        linkField(field);
                } else {
                    link("");
                }
            }
        }

        for (const auto& member : scope.members()) {
            if (member.kind == slang::ast::SymbolKind::ContinuousAssign) {
                const auto& assign = member.as<slang::ast::ContinuousAssignSymbol>().getAssignment();
                if (assign.kind != slang::ast::ExpressionKind::Assignment)
                    continue;
                const auto& expr = assign.as<slang::ast::AssignmentExpression>();
                auto lhs = directAliasPathFromExpr(expr.left(), path);
                auto rhs = directAliasPathFromExpr(expr.right(), path);
                if (!lhs.empty() && !rhs.empty())
                    addAlias(std::move(rhs), std::move(lhs));
                recordResetInversion(expr, path);
            } else if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
                const auto& block = member.as<slang::ast::ProceduralBlockSymbol>();
                if (block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysComb) {
                    if (const auto* assignment = soleUnconditionalAssignment(block.getBody()))
                        recordResetInversion(*assignment, path);
                }
            } else if (member.kind == slang::ast::SymbolKind::Instance) {
                const auto& child = member.as<slang::ast::InstanceSymbol>();
                self(self, child.body, path + "." + std::string(child.name), path, &child);
            } else if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
                const auto& block = member.as<slang::ast::GenerateBlockSymbol>();
                if (block.isUninstantiated)
                    continue;
                const std::string name = block.getExternalName();
                self(self, block, name.empty() ? path : path + "." + name, path, nullptr);
            } else if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
                const auto& array = member.as<slang::ast::GenerateBlockArraySymbol>();
                for (const auto* entry : array.entries) {
                    if (!entry || entry->isUninstantiated)
                        continue;
                    std::string name = entry->getExternalName();
                    if (name.empty())
                        name = std::string(array.name);
                    self(self, *entry, path + "." + name, path, nullptr);
                }
            }
        }
    };

    auto& root = compilation_.getRoot();
    std::unordered_map<std::string, std::unordered_set<ClockSource*>> candidates;
    std::deque<std::pair<std::string, ClockSource*>> pending;
    std::vector<std::pair<std::string, ClockSource*>> seeds;
    for (auto* top : root.topInstances) {
        if (!top)
            continue;
        const std::string top_path(top->name);
        visitScope(visitScope, top->body, top_path, "", top);
        std::unordered_set<std::string> top_ports;
        for (const auto& member : top->body.members()) {
            if (member.kind == slang::ast::SymbolKind::Port)
                top_ports.insert(std::string(member.name));
        }
        for (const auto& source : clock_db_.sources) {
            const auto& origin = source->origin_signal;
            if (origin.empty())
                continue;
            std::string path;
            if (source->type == ClockSource::Type::Generated && origin.find('/') != std::string::npos) {
                // SDC get_pins commonly names a child output as
                // `u_gate/clk_o`. Seed that exact pin; its directed output
                // port connection carries the declared clock to the parent
                // signal without a global same-name guess.
                path = origin;
                std::replace(path.begin(), path.end(), '/', '.');
                if (!path.starts_with(top_path + "."))
                    path = top_path + "." + path;
            } else if (origin.find('.') == std::string::npos) {
                if (!top_ports.contains(origin))
                    continue;
                path = top_path + "." + origin;
            } else {
                if (!origin.starts_with(top_path + "."))
                    continue;
                path = origin;
            }
            seeds.emplace_back(path, source.get());
            if (candidates[path].insert(source.get()).second)
                pending.emplace_back(path, source.get());
        }
    }

    while (!pending.empty()) {
        auto [path, source] = std::move(pending.front());
        pending.pop_front();
        auto it = aliases.find(path);
        if (it == aliases.end())
            continue;
        for (const auto& next : it->second) {
            if (candidates[next].insert(source).second)
                pending.emplace_back(next, source);
        }
    }

    // A node reachable from more than one physical source is ambiguous, such
    // as a mux output. Leave it unresolved rather than silently choosing one.
    for (const auto& [path, sources] : candidates) {
        if (sources.size() != 1 || clock_db_.net_by_path.contains(path))
            continue;
        auto net = std::make_unique<ClockNet>();
        net->hier_path = path;
        net->source = *sources.begin();
        clock_db_.addNet(std::move(net));
    }

    std::unordered_map<std::string, std::unordered_set<ClockSource*>> roots;
    pending.clear();
    for (const auto& [path, source] : seeds) {
        auto* root_source = rootSource(source);
        if (!root_source)
            continue;
        if (roots[path].insert(root_source).second)
            pending.emplace_back(path, root_source);
    }
    while (!pending.empty()) {
        auto [path, source] = std::move(pending.front());
        pending.pop_front();
        auto it = root_edges.find(path);
        if (it == root_edges.end())
            continue;
        for (const auto& next : it->second) {
            if (roots[next].insert(source).second)
                pending.emplace_back(next, source);
        }
    }
    for (const auto& [path, sources] : roots) {
        if (sources.size() == 1)
            clock_db_.root_by_path[path] = *sources.begin();
    }
    clock_db_.directed_aliases = std::move(aliases);
    clock_db_.reset_inversions = std::move(resetInversions);
    clock_db_.reset_selected_aliases = std::move(resetSelectedAliases);
    clock_db_.reset_indexed_field_aliases = std::move(resetIndexedFieldAliases);
    clock_db_.reset_mux_inputs = std::move(resetMuxInputs);
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
        const auto parent_dot = inst_path.rfind('.');
        const std::string parent_scope =
            parent_dot == std::string::npos ? std::string{} : inst_path.substr(0, parent_dot);
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
                const auto declared_path = extractSignalPathFromExpr(*expr, parent_scope);
                if (auto known = clock_db_.net_by_path.find(declared_path); known != clock_db_.net_by_path.end())
                    parent_clock_net = known->second;
                if (!actual_signal.empty()) {
                    if (!parent_clock_net) {
                        auto it = parent_nets.find(actual_signal);
                        if (it != parent_nets.end())
                            parent_clock_net = it->second;
                        else if (!parent_scope.empty()) {
                            auto known = clock_db_.net_by_path.find(parent_scope + "." + actual_signal);
                            if (known != clock_db_.net_by_path.end())
                                parent_clock_net = known->second;
                        }
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
            if (auto known = clock_db_.net_by_path.find(inst_path + "." + sig_name);
                known != clock_db_.net_by_path.end()) {
                local_nets[sig_name] = known->second;
                continue;
            }

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
            std::string reset_name;
            bool reset_active_low = false;
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
                    if (isResetName(sig)) {
                        if (sec.edge == slang::ast::EdgeKind::NegEdge || sec.edge == slang::ast::EdgeKind::PosEdge) {
                            reset_name = sig;
                            reset_active_low = sec.edge == slang::ast::EdgeKind::NegEdge;
                        }
                    } else if (isClockName(sig)) {
                        clock_name = sig;
                    }
                }
            }

            if (clock_name.empty()) continue;

            // Look for toggle pattern: q <= ~q or q <= !q
            // Walk the inner statement for assignments where LHS == ~RHS
            checkTogglePattern(timed.stmt, clock_name, inst_path, reset_name, reset_active_low);
        }

        // Recurse into child instances
        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& child = member.as<slang::ast::InstanceSymbol>();
            std::string child_path = inst_path + "." + std::string(child.name);
            detectClockDividersInInstance(child, child_path);
        }
    }
}

// Accept a reset branch only when it assigns a literal to one signal. A
// reset-looking condition without a matching asynchronous reset event is not
// enough to characterize the following toggle as a fixed divider.
static std::string resetLiteralTarget(const slang::ast::Statement& stmt) {
    using SK = slang::ast::StatementKind;
    using EK = slang::ast::ExpressionKind;
    if (stmt.kind == SK::Block)
        return resetLiteralTarget(stmt.as<slang::ast::BlockStatement>().body);
    if (stmt.kind == SK::List) {
        const auto& list = stmt.as<slang::ast::StatementList>().list;
        return list.size() == 1 && list.front() ? resetLiteralTarget(*list.front()) : std::string{};
    }
    if (stmt.kind != SK::ExpressionStatement)
        return {};
    const auto& expr = stmt.as<slang::ast::ExpressionStatement>().expr;
    if (expr.kind != EK::Assignment)
        return {};
    const auto& assignment = expr.as<slang::ast::AssignmentExpression>();
    auto* rhs = &assignment.right();
    while (rhs->kind == EK::Conversion)
        rhs = &rhs->as<slang::ast::ConversionExpression>().operand();
    if (rhs->kind != EK::IntegerLiteral && rhs->kind != EK::UnbasedUnsizedIntegerLiteral)
        return {};
    return extractSignalNameFromExpr(assignment.left());
}

void ClockTreeAnalyzer::checkTogglePattern(const slang::ast::Statement& stmt, const std::string& clock_name,
                                           const std::string& inst_path, const std::string& reset_name,
                                           bool reset_active_low, const std::string& expected_lhs) {
    using SK = slang::ast::StatementKind;
    using EK = slang::ast::ExpressionKind;

    switch (stmt.kind) {
        case SK::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind != EK::Assignment) break;

            auto& assign = expr.as<slang::ast::AssignmentExpression>();
            std::string lhs = extractSignalNameFromExpr(assign.left());
            if (lhs.empty() || (!expected_lhs.empty() && lhs != expected_lhs))
                break;

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
                        if (auto net = clock_db_.net_by_path.find(inst_path + "." + clock_name);
                            net != clock_db_.net_by_path.end()) {
                            master_src = net->second->source;
                        }
                        if (!master_src) {
                            bool ambiguous = false;
                            for (const auto& src : clock_db_.sources) {
                                if (src->origin_signal != clock_name && src->name != clock_name)
                                    continue;
                                if (master_src && master_src != src.get()) {
                                    ambiguous = true;
                                    break;
                                }
                                master_src = src.get();
                            }
                            if (ambiguous)
                                master_src = nullptr;
                        }

                        const std::string divided_path = inst_path + "." + lhs;
                        ClockSource* divided_src = nullptr;
                        for (const auto& src : clock_db_.sources) {
                            if (src->origin_signal != divided_path)
                                continue;
                            if (!divided_src || src->type == ClockSource::Type::Generated)
                                divided_src = src.get();
                            if (src->type == ClockSource::Type::Generated)
                                break;
                        }
                        if (divided_src && divided_src->type != ClockSource::Type::AutoDetected)
                            break;
                        if (master_src == divided_src)
                            master_src = nullptr;

                        // A consumer may have lazily registered this net as
                        // AutoDetected before the toggle FF was visited.
                        // Promote that exact source so there is no phantom
                        // duplicate domain or global same-name collision.
                        if (divided_src) {
                            divided_src->type = ClockSource::Type::Generated;
                            divided_src->master = master_src;
                            divided_src->divide_by = 2;
                            break;
                        }

                        auto src = std::make_unique<ClockSource>();
                        src->id = "divider_" + divided_path;
                        src->name = lhs + "_div2";
                        src->type = ClockSource::Type::Generated;
                        src->origin_signal = divided_path;
                        src->master = master_src;
                        src->divide_by = 2;
                        clock_db_.addSource(std::move(src));

                        // Create a ClockNet for the divided clock
                        auto net = std::make_unique<ClockNet>();
                        net->hier_path = divided_path;
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
            checkTogglePattern(block.body, clock_name, inst_path, reset_name, reset_active_low, expected_lhs);
            break;
        }
        case SK::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            // Multiple updates can change edge spacing; recognize only one
            // update or one reset branch followed by the toggle below.
            if (list.list.size() == 1 && list.list.front())
                checkTogglePattern(*list.list.front(), clock_name, inst_path, reset_name, reset_active_low,
                                   expected_lhs);
            break;
        }
        case SK::Conditional: {
            const auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            if (reset_name.empty() || cond.conditions.size() != 1 || cond.conditions.front().pattern || !cond.ifFalse)
                break;
            const slang::ast::Expression* condition = cond.conditions.front().expr;
            while (condition->kind == EK::Conversion)
                condition = &condition->as<slang::ast::ConversionExpression>().operand();
            bool negated = false;
            if (condition->kind == EK::UnaryOp) {
                const auto& unary = condition->as<slang::ast::UnaryExpression>();
                if (unary.op == slang::ast::UnaryOperator::LogicalNot) {
                    negated = true;
                    condition = &unary.operand();
                }
            }
            if (extractSignalNameFromExpr(*condition) != reset_name || negated != reset_active_low)
                break;
            const std::string reset_target = resetLiteralTarget(cond.ifTrue);
            if (!reset_target.empty())
                checkTogglePattern(*cond.ifFalse, clock_name, inst_path, {}, false, reset_target);
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

            // Mark the exact output net as gated. A substring search would
            // mark unrelated sibling clocks with the same leaf name.
            if (!clock_out_signal.empty()) {
                const std::string output_path = inst_path + "." + clock_out_signal;
                if (auto existing = clock_db_.net_by_path.find(output_path); existing != clock_db_.net_by_path.end()) {
                    existing->second->is_gated = true;
                    existing->second->gate_enable = enable_signal;
                    detectClockGatesInInstance(child, child_path);
                    continue;
                }
                // No explicit generated-clock net was found for this output.
                auto net = std::make_unique<ClockNet>();
                net->hier_path = output_path;
                net->source = nullptr;
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
                            if (auto known = clock_db_.net_by_path.find(inst_path + "." + in_clk);
                                known != clock_db_.net_by_path.end()) {
                                net->source = known->second->source;
                                break;
                            }
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
    auto uniqueSource = [&](const std::string& name) -> ClockSource* {
        ClockSource* found = nullptr;
        for (const auto& source : clock_db_.sources) {
            if (source->name != name)
                continue;
            if (found)
                return nullptr;
            found = source.get();
        }
        return found;
    };
    auto descendsFrom = [](ClockSource* source, ClockSource* ancestor) {
        std::unordered_set<ClockSource*> seen;
        bool foundAncestor = false;
        for (auto* master = source->master; master; master = master->master) {
            if (!seen.insert(master).second)
                return false;
            if (master == ancestor)
                foundAncestor = true;
        }
        return foundAncestor;
    };

    for (const auto& group : sdc_->clock_groups) {
        DomainRelationship::Type rel_type;
        switch (group.type) {
            case SdcClockGroup::Type::Asynchronous:
                rel_type = DomainRelationship::Type::Asynchronous; break;
            case SdcClockGroup::Type::Exclusive:
                rel_type = DomainRelationship::Type::PhysicallyExclusive; break;
            case SdcClockGroup::Type::LogicallyExclusive:
                rel_type = DomainRelationship::Type::LogicallyExclusive; break;
        }

        // Resolve each declared clock once and expand only proven master
        // chains. Missing or duplicate names and overlapping clock groups
        // must not create a partially guessed relationship.
        std::vector<std::unordered_set<ClockSource*>> sourceGroups;
        std::unordered_set<ClockSource*> assigned;
        bool valid = true;
        for (size_t i = 0; i < group.groups.size(); ++i) {
            std::unordered_set<ClockSource*> members;
            for (const auto& name : group.groups[i]) {
                auto* root = uniqueSource(name);
                if (!root) {
                    valid = false;
                    break;
                }
                members.insert(root);
                if (i < group.include_generated.size() && group.include_generated[i]) {
                    for (const auto& source : clock_db_.sources) {
                        if (source->type == ClockSource::Type::Generated && descendsFrom(source.get(), root))
                            members.insert(source.get());
                    }
                }
            }
            if (!valid || members.empty())
                break;
            for (auto* source : members)
                if (!assigned.insert(source).second)
                    valid = false;
            sourceGroups.push_back(std::move(members));
            if (!valid)
                break;
        }
        if (!valid || sourceGroups.empty()) {
            ++skipped_sdc_relationship_groups_;
            continue;
        }

        // A single explicit group is exclusive/asynchronous to every clock
        // not selected into that group, including generated clocks unless
        // -include_generated_clocks selected them above.
        if (sourceGroups.size() == 1) {
            std::unordered_set<ClockSource*> other;
            for (const auto& source : clock_db_.sources)
                if (!assigned.contains(source.get()))
                    other.insert(source.get());
            if (!other.empty())
                sourceGroups.push_back(std::move(other));
        }

        for (size_t i = 0; i < sourceGroups.size(); ++i) {
            for (size_t j = i + 1; j < sourceGroups.size(); ++j) {
                for (auto* sourceA : sourceGroups[i]) {
                    for (auto* sourceB : sourceGroups[j])
                        clock_db_.relationships.push_back({sourceA, sourceB, rel_type, /*sdc_declared=*/true});
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

            // Check if they share a common root source
            auto* rootA = rootSource(a);
            auto* rootB = rootSource(b);

            if (rootA && rootA == rootB) {
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
