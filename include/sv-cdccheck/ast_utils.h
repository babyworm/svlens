#pragma once

#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"

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

} // namespace sv_cdccheck
