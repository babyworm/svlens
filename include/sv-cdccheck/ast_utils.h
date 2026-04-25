#pragma once

#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"
#include "slang/ast/expressions/AssignmentExpressions.h"

#include <functional>
#include <string>
#include <vector>

namespace sv_cdccheck {

/// Extract signal name from a NamedValueExpression
std::string extractSignalName(const slang::ast::Expression& expr);

/// Collect all referenced signal names from an expression (RHS of assignment).
/// Handles NamedValue, HierarchicalValue, UnaryOp, BinaryOp, ConditionalOp,
/// Concatenation, ElementSelect, RangeSelect, and Conversion expressions.
void collectReferencedSignals(const slang::ast::Expression& expr,
                              std::vector<std::string>& signals);

/// Per-variable assignment info: LHS variable name + all RHS signal names
struct AssignInfo {
    std::string lhs_name;
    std::vector<std::string> rhs_signals;
};

/// Collect assignments from a statement tree. For each assignment LHS = RHS,
/// records which signals the RHS references.
void collectAssignments(const slang::ast::Statement& stmt,
                        std::vector<AssignInfo>& assignments);

/// Split an Assignment expression into one (lhs_name, rhs_signals) pair per
/// LHS sub-target and invoke `emit` for each. Handles three LHS kinds:
///   - NamedValue: emits one pair with the full RHS fanin.
///   - Concatenation paired with a same-arity / width-matching RHS
///     concatenation: emits per-position pairs (each LHS slice gets only
///     its matching RHS slice's fanin -- avoids the false direct edge
///     that a full-fanin broadcast would introduce in the ZipCPU
///     `{wq2,wq1} <= {wq1,rgray}` 2-FF idiom).
///   - Concatenation in any other shape (RHS not a concat, or arity /
///     widths mismatch): broadcasts the full RHS fanin to each LHS
///     operand.
/// Other LHS kinds (ElementSelect, RangeSelect, ...) are silently skipped.
void splitAssignmentByLHS(
    const slang::ast::AssignmentExpression& assign,
    const std::function<void(std::string, std::vector<std::string>)>& emit);

} // namespace sv_cdccheck
