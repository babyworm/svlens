#include "sv-cdccheck/ast_utils.h"

#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"

#include <algorithm>

namespace sv_cdccheck {

std::string extractSignalName(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        auto& named = expr.as<slang::ast::NamedValueExpression>();
        return std::string(named.symbol.name);
    }
    return "";
}

void collectReferencedSignals(const slang::ast::Expression& expr,
                              std::vector<std::string>& signals) {
    switch (expr.kind) {
        case slang::ast::ExpressionKind::NamedValue:
        case slang::ast::ExpressionKind::HierarchicalValue: {
            auto& named = expr.as<slang::ast::ValueExpressionBase>();
            std::string name(named.symbol.name);
            if (std::find(signals.begin(), signals.end(), name) == signals.end())
                signals.push_back(name);
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

void collectAssignments(const slang::ast::Statement& stmt,
                        std::vector<AssignInfo>& assignments) {
    switch (stmt.kind) {
        case slang::ast::StatementKind::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind == slang::ast::ExpressionKind::Assignment) {
                auto& assign = expr.as<slang::ast::AssignmentExpression>();
                AssignInfo info;
                if (assign.left().kind == slang::ast::ExpressionKind::NamedValue) {
                    info.lhs_name = std::string(
                        assign.left().as<slang::ast::NamedValueExpression>().symbol.name);
                }
                collectReferencedSignals(assign.right(), info.rhs_signals);
                if (!info.lhs_name.empty())
                    assignments.push_back(std::move(info));
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
