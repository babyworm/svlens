#include "sv-cdccheck/ast_utils.h"

#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/types/Type.h"

#include <algorithm>

namespace sv_cdccheck {

std::string extractSignalName(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        auto& named = expr.as<slang::ast::NamedValueExpression>();
        return std::string(named.symbol.name);
    }
    return "";
}

// Like extractSignalName but resolves ElementSelect / RangeSelect to
// the base register name (e.g. `b[0]` and `b[3:0]` both return "b").
// Used for LHS assignment targets where a bit-select on a multi-bit
// register still drives the FF as a whole, so the connectivity
// tracker must record `b` as the FF target.
static std::string extractLHSBaseName(const slang::ast::Expression& expr) {
    switch (expr.kind) {
        case slang::ast::ExpressionKind::NamedValue:
            return std::string(expr.as<slang::ast::NamedValueExpression>().symbol.name);
        case slang::ast::ExpressionKind::ElementSelect:
            return extractLHSBaseName(
                expr.as<slang::ast::ElementSelectExpression>().value());
        case slang::ast::ExpressionKind::RangeSelect:
            return extractLHSBaseName(
                expr.as<slang::ast::RangeSelectExpression>().value());
        default:
            return "";
    }
}

void collectReferencedSignals(const slang::ast::Expression& expr,
                              std::vector<std::string>& signals) {
    switch (expr.kind) {
        case slang::ast::ExpressionKind::NamedValue:
        case slang::ast::ExpressionKind::HierarchicalValue: {
            auto& named = expr.as<slang::ast::ValueExpressionBase>();
            // Filter out compile-time constants (parameters, enum
            // values, type parameters). They are NOT runtime fanin
            // signals and counting them inflates fanin.size(), which
            // breaks the relaxed sync_verifier::findNextFF check
            // (`fanin.size() <= 1`) for any FF that resets to a
            // parameter -- the OpenTitan prim_flop pattern
            // `q_o <= ResetValue` in particular.
            switch (named.symbol.kind) {
                case slang::ast::SymbolKind::Parameter:
                case slang::ast::SymbolKind::TypeParameter:
                case slang::ast::SymbolKind::EnumValue:
                    return;
                default:
                    break;
            }
            // For hierarchical references (`u_sync.q` from parent),
            // push the FULL hierarchical path so the connectivity
            // tracker can resolve the FFNode directly via
            // output_map. The leaf name "q" alone is ambiguous when
            // multiple submodules share the same internal signal
            // name. NamedValue stays at the leaf.
            std::string name;
            if (expr.kind == slang::ast::ExpressionKind::HierarchicalValue) {
                name = named.symbol.getHierarchicalPath();
            } else {
                name = std::string(named.symbol.name);
            }
            if (std::find(signals.begin(), signals.end(), name) == signals.end())
                signals.push_back(std::move(name));
            return;
        }
        case slang::ast::ExpressionKind::UnaryOp: {
            auto& unary = expr.as<slang::ast::UnaryExpression>();
            collectReferencedSignals(unary.operand(), signals);
            return;
        }
        case slang::ast::ExpressionKind::BinaryOp: {
            auto& binary = expr.as<slang::ast::BinaryExpression>();
            collectReferencedSignals(binary.left(), signals);
            collectReferencedSignals(binary.right(), signals);
            return;
        }
        case slang::ast::ExpressionKind::ConditionalOp: {
            auto& cond = expr.as<slang::ast::ConditionalExpression>();
            for (auto& condition : cond.conditions)
                collectReferencedSignals(*condition.expr, signals);
            collectReferencedSignals(cond.left(), signals);
            collectReferencedSignals(cond.right(), signals);
            return;
        }
        case slang::ast::ExpressionKind::Concatenation: {
            auto& concat = expr.as<slang::ast::ConcatenationExpression>();
            for (auto* op : concat.operands())
                collectReferencedSignals(*op, signals);
            return;
        }
        case slang::ast::ExpressionKind::Replication: {
            // `{N{expr}}` -- the count is a constant in synthesizable
            // RTL, so the only signals come from the replicated body.
            auto& rep = expr.as<slang::ast::ReplicationExpression>();
            collectReferencedSignals(rep.concat(), signals);
            return;
        }
        case slang::ast::ExpressionKind::ElementSelect: {
            auto& sel = expr.as<slang::ast::ElementSelectExpression>();
            collectReferencedSignals(sel.value(), signals);
            return;
        }
        case slang::ast::ExpressionKind::RangeSelect: {
            auto& sel = expr.as<slang::ast::RangeSelectExpression>();
            collectReferencedSignals(sel.value(), signals);
            return;
        }
        case slang::ast::ExpressionKind::Conversion: {
            auto& conv = expr.as<slang::ast::ConversionExpression>();
            collectReferencedSignals(conv.operand(), signals);
            return;
        }
        default:
            return;
    }
}

// Recursive helper: paired LHS/RHS expressions are split positional
// when both are same-arity, width-matching concatenations; otherwise
// the RHS is broadcast to every leaf in the LHS subtree. Recursion
// handles arbitrary nesting depth (`{{{a,b},{c,d}},{e,f}}`).
// Round 13: replaces the previous 2-level hand-inlined version that
// would silently fall through to broadcast on 3+ levels.
//
// Depth guard: synthesizable RTL never nests concats more than a
// handful of levels (3-4 in the wildest cases). At MAX_DEPTH the
// helper falls through to broadcast rather than recursing further,
// preventing stack exhaustion on adversarial input.
static constexpr int kMaxConcatDepth = 64;

static void splitConcatPair(
    const slang::ast::Expression& lhs,
    const slang::ast::Expression& rhs,
    const std::function<void(std::string, std::vector<std::string>)>& emit,
    int depth = 0)
{
    if (depth >= kMaxConcatDepth) {
        // Adversarial input: stop recursing. Broadcast the full RHS
        // fanin to every leaf reachable in the LHS via the same
        // local broadcast lambda used by the normal fallback.
        std::vector<std::string> rhs_signals;
        collectReferencedSignals(rhs, rhs_signals);
        std::function<void(const slang::ast::Expression&)> broadcast =
            [&](const slang::ast::Expression& sub) {
                if (std::string base = extractLHSBaseName(sub); !base.empty()) {
                    emit(std::move(base), rhs_signals);
                    return;
                }
                if (sub.kind == slang::ast::ExpressionKind::Concatenation) {
                    auto& inner = sub.as<slang::ast::ConcatenationExpression>();
                    for (auto* op : inner.operands())
                        if (op) broadcast(*op);
                }
            };
        broadcast(lhs);
        return;
    }
    // Single-target LHS path: NamedValue / ElementSelect /
    // RangeSelect drilled to its base register name.
    if (std::string base = extractLHSBaseName(lhs); !base.empty()) {
        std::vector<std::string> rhs_signals;
        collectReferencedSignals(rhs, rhs_signals);
        emit(std::move(base), std::move(rhs_signals));
        return;
    }

    if (lhs.kind != slang::ast::ExpressionKind::Concatenation) return;
    auto& lhs_cat = lhs.as<slang::ast::ConcatenationExpression>();
    auto lhs_ops = lhs_cat.operands();

    // Positional matching only when RHS is also a same-arity,
    // width-matching concatenation. Defensive null guard on the
    // type pointers covers slang error-recovery cases.
    bool positional = false;
    const slang::ast::ConcatenationExpression* rhs_cat = nullptr;
    if (rhs.kind == slang::ast::ExpressionKind::Concatenation) {
        rhs_cat = &rhs.as<slang::ast::ConcatenationExpression>();
        if (rhs_cat->operands().size() == lhs_ops.size()) {
            positional = true;
            auto rhs_ops = rhs_cat->operands();
            for (size_t i = 0; i < lhs_ops.size(); ++i) {
                if (!lhs_ops[i] || !rhs_ops[i] ||
                    !lhs_ops[i]->type || !rhs_ops[i]->type ||
                    lhs_ops[i]->type->getBitWidth() !=
                    rhs_ops[i]->type->getBitWidth()) {
                    positional = false;
                    break;
                }
            }
        }
    }

    if (positional) {
        auto rhs_ops = rhs_cat->operands();
        for (size_t i = 0; i < lhs_ops.size(); ++i)
            splitConcatPair(*lhs_ops[i], *rhs_ops[i], emit, depth + 1);
        return;
    }

    // Fallback: broadcast the full RHS fanin to every leaf name in
    // the LHS subtree (recursing into nested concats so a 3-level
    // LHS still sees all its leaves named).
    std::vector<std::string> rhs_signals;
    collectReferencedSignals(rhs, rhs_signals);
    std::function<void(const slang::ast::Expression&)> broadcast_lhs =
        [&](const slang::ast::Expression& sub) {
            if (std::string base = extractLHSBaseName(sub); !base.empty()) {
                emit(std::move(base), rhs_signals);
                return;
            }
            if (sub.kind == slang::ast::ExpressionKind::Concatenation) {
                auto& inner = sub.as<slang::ast::ConcatenationExpression>();
                for (auto* op : inner.operands()) {
                    if (op) broadcast_lhs(*op);
                }
            }
        };
    for (auto* op : lhs_ops) {
        if (op) broadcast_lhs(*op);
    }
}

void splitAssignmentByLHS(
    const slang::ast::AssignmentExpression& assign,
    const std::function<void(std::string, std::vector<std::string>)>& emit)
{
    splitConcatPair(assign.left(), assign.right(), emit);
}

void collectAssignments(const slang::ast::Statement& stmt,
                        std::vector<AssignInfo>& assignments) {
    switch (stmt.kind) {
        case slang::ast::StatementKind::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind == slang::ast::ExpressionKind::Assignment) {
                splitAssignmentByLHS(
                    expr.as<slang::ast::AssignmentExpression>(),
                    [&assignments](std::string lhs_name,
                                   std::vector<std::string> rhs_signals) {
                        AssignInfo info;
                        info.lhs_name = std::move(lhs_name);
                        info.rhs_signals = std::move(rhs_signals);
                        assignments.push_back(std::move(info));
                    });
            }
            break;
        }
        case slang::ast::StatementKind::Timed: {
            auto& timed = stmt.as<slang::ast::TimedStatement>();
            collectAssignments(timed.stmt, assignments);
            break;
        }
        case slang::ast::StatementKind::Block: {
            auto& block = stmt.as<slang::ast::BlockStatement>();
            collectAssignments(block.body, assignments);
            break;
        }
        case slang::ast::StatementKind::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            for (auto* child : list.list)
                if (child) collectAssignments(*child, assignments);
            break;
        }
        case slang::ast::StatementKind::Conditional: {
            auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            collectAssignments(cond.ifTrue, assignments);
            if (cond.ifFalse)
                collectAssignments(*cond.ifFalse, assignments);
            break;
        }
        default: break;
    }
}

} // namespace sv_cdccheck
