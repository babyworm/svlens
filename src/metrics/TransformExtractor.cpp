#include "TransformExtractor.h"

#include <slang/ast/Expression.h>
#include <slang/ast/Statement.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/MiscStatements.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/symbols/SubroutineSymbols.h>
#include <slang/ast/types/Type.h>
#include <slang/ast/types/AllTypes.h>

namespace metrics {

TransformExtractor::TransformExtractor(slang::ast::Compilation& compilation,
                                       const std::string& topModule,
                                       int maxForUnroll)
    : compilation_(compilation), topModule_(topModule), maxForUnroll_(maxForUnroll) {}

TransformGraph TransformExtractor::extract() {
    graph_ = TransformGraph{};
    tempCounter_ = 0;

    auto& root = compilation_.getRoot();
    const slang::ast::InstanceSymbol* topInst = nullptr;
    for (auto* inst : root.topInstances) {
        if (inst->name == topModule_) {
            topInst = inst;
            break;
        }
    }
    if (!topInst)
        return graph_;

    // Collect output port roots
    for (auto& member : topInst->body.members()) {
        if (member.kind == slang::ast::SymbolKind::Port) {
            auto& port = member.as<slang::ast::PortSymbol>();
            if (port.direction == slang::ast::ArgumentDirection::Out ||
                port.direction == slang::ast::ArgumentDirection::InOut) {
                ValueRef ref;
                ref.hier_path = std::string(port.name);
                ref.base_name = std::string(port.name);
                ref.kind = ValueRef::Port;
                graph_.roots.push_back(ref);
            }
        }
    }

    visitScope(topInst->body, "");

    // Deduplicate FFs: keep non-constant D-side per name
    {
        std::unordered_map<std::string, size_t> bestFF; // name -> index
        for (size_t i = 0; i < graph_.flip_flops.size(); ++i) {
            auto& ff = graph_.flip_flops[i];
            auto it = bestFF.find(ff.name);
            if (it == bestFF.end()) {
                bestFF[ff.name] = i;
            } else {
                // Prefer non-constant D-side
                auto& existing = graph_.flip_flops[it->second];
                if (existing.d_ref.kind == ValueRef::Const &&
                    ff.d_ref.kind != ValueRef::Const) {
                    it->second = i;
                }
            }
        }
        std::vector<FFInfo> deduped;
        deduped.reserve(bestFF.size());
        // Sort by name for determinism
        std::vector<std::string> names;
        names.reserve(bestFF.size());
        for (auto& [name, _] : bestFF) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (auto& name : names)
            deduped.push_back(graph_.flip_flops[bestFF[name]]);
        graph_.flip_flops = std::move(deduped);
    }

    // Add FF D-side roots using actual D-side signal reference
    for (auto& ff : graph_.flip_flops) {
        ValueRef dRoot = ff.d_ref;
        dRoot.kind = ValueRef::FfDSink;
        if (dRoot.base_name.empty())
            dRoot.base_name = ff.name;
        graph_.roots.push_back(dRoot);
    }

    return graph_;
}

void TransformExtractor::visitScope(const slang::ast::Scope& scope,
                                    const std::string& scopePath) {
    for (auto& member : scope.members()) {
        switch (member.kind) {
            case slang::ast::SymbolKind::ContinuousAssign:
                processContinuousAssign(
                    member.as<slang::ast::ContinuousAssignSymbol>(), scopePath);
                break;

            case slang::ast::SymbolKind::ProceduralBlock:
                processProceduralBlock(
                    member.as<slang::ast::ProceduralBlockSymbol>(), scopePath);
                break;

            case slang::ast::SymbolKind::Instance: {
                auto& childInst = member.as<slang::ast::InstanceSymbol>();
                std::string childPath = scopePath.empty()
                    ? std::string(childInst.name)
                    : scopePath + "." + std::string(childInst.name);
                visitScope(childInst.body, childPath);
                break;
            }

            case slang::ast::SymbolKind::GenerateBlock: {
                auto& genBlock = member.as<slang::ast::GenerateBlockSymbol>();
                std::string blockScope = scopePath;
                if (!genBlock.name.empty())
                    blockScope = scopePath.empty()
                        ? std::string(genBlock.name)
                        : scopePath + "." + std::string(genBlock.name);
                visitScope(genBlock, blockScope);
                break;
            }

            case slang::ast::SymbolKind::GenerateBlockArray: {
                auto& genArray = member.as<slang::ast::GenerateBlockArraySymbol>();
                for (auto& elem : genArray.members()) {
                    if (elem.kind == slang::ast::SymbolKind::GenerateBlock) {
                        auto& block = elem.as<slang::ast::GenerateBlockSymbol>();
                        std::string blockScope = scopePath.empty()
                            ? std::string(genArray.name)
                            : scopePath + "." + std::string(genArray.name);
                        if (block.arrayIndex)
                            blockScope += "[" + block.arrayIndex->toString() + "]";
                        visitScope(block, blockScope);
                    }
                }
                break;
            }

            default:
                break;
        }
    }
}

void TransformExtractor::processContinuousAssign(
    const slang::ast::ContinuousAssignSymbol& sym,
    const std::string& scopePath) {
    auto& assign = sym.getAssignment().as<slang::ast::AssignmentExpression>();
    auto& lhs = assign.left();
    auto& rhs = assign.right();

    ValueRef lhsRef = makeNamedRef(lhs, scopePath);
    ValueRef rhsRef = decomposeExpr(rhs, scopePath);

    // Create a node linking RHS result to LHS target
    TransformNode node;
    node.op_kind = TransformNode::Alias;
    node.op_detail = "assign";
    node.output = lhsRef;
    node.inputs.push_back(rhsRef);
    node.bit_width = lhs.type->getBitWidth();
    node.source_loc = sourceLoc(lhs);
    graph_.addNode(std::move(node));
}

void TransformExtractor::processProceduralBlock(
    const slang::ast::ProceduralBlockSymbol& block,
    const std::string& scopePath) {
    if (block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysComb) {
        processStatement(block.getBody(), scopePath);
        return;
    }

    if (block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
        detectFFs(block.getBody(), scopePath);
        return;
    }

    if (block.procedureKind == slang::ast::ProceduralBlockKind::Always) {
        // Legacy always @(*) — treat as combinational (approximate)
        processStatement(block.getBody(), scopePath, true);
        return;
    }

    UnsupportedEvent evt;
    evt.kind = "procedural_block";
    evt.detail = "unsupported procedural block kind";
    graph_.unsupported_events.push_back(std::move(evt));
}

void TransformExtractor::processStatement(const slang::ast::Statement& stmt,
                                          const std::string& scopePath,
                                          bool approximate) {
    switch (stmt.kind) {
        case slang::ast::StatementKind::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind == slang::ast::ExpressionKind::Assignment) {
                auto& assign = expr.as<slang::ast::AssignmentExpression>();
                ValueRef lhsRef = makeNamedRef(assign.left(), scopePath);
                ValueRef rhsRef = decomposeExpr(assign.right(), scopePath);

                TransformNode node;
                node.op_kind = TransformNode::Alias;
                node.op_detail = "proc_assign";
                node.output = lhsRef;
                node.inputs.push_back(rhsRef);
                node.bit_width = assign.left().type->getBitWidth();
                node.source_loc = sourceLoc(assign.left());
                node.approximate = approximate;
                graph_.addNode(std::move(node));
            }
            // Handle non-assignment expression statements (e.g., task calls)
            if (expr.kind == slang::ast::ExpressionKind::Call) {
                decomposeExpr(expr, scopePath);
            }
            break;
        }
        case slang::ast::StatementKind::Block: {
            auto& block = stmt.as<slang::ast::BlockStatement>();
            processStatement(block.body, scopePath, approximate);
            break;
        }
        case slang::ast::StatementKind::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            for (auto* child : list.list) {
                if (child)
                    processStatement(*child, scopePath, approximate);
            }
            break;
        }
        case slang::ast::StatementKind::Conditional: {
            auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            processStatement(cond.ifTrue, scopePath, approximate);
            if (cond.ifFalse)
                processStatement(*cond.ifFalse, scopePath, approximate);
            break;
        }
        case slang::ast::StatementKind::Case: {
            processCase(stmt.as<slang::ast::CaseStatement>(), scopePath);
            break;
        }
        case slang::ast::StatementKind::ForLoop: {
            processForLoop(stmt.as<slang::ast::ForLoopStatement>(), scopePath);
            break;
        }
        default: {
            UnsupportedEvent evt;
            evt.kind = "procedural_statement";
            evt.detail = "unsupported statement in always_comb";
            graph_.unsupported_events.push_back(std::move(evt));
            break;
        }
    }
}

void TransformExtractor::processCase(const slang::ast::CaseStatement& caseStmt,
                                     const std::string& scopePath) {
    // Decompose case into cascaded Mux chain (priority-encoded)
    // case(selector) item[0]: body[0]; item[1]: body[1]; ... default: default_body endcase
    // becomes: mux_1 = Mux(sel==item[0], body[0], Mux(sel==item[1], body[1], ... default))

    ValueRef selectorRef = decomposeExpr(caseStmt.expr, scopePath);

    // Determine mux op_detail based on case condition type
    std::string muxDetail = "case_mux";
    std::string cmpDetail = "eq";
    if (caseStmt.condition == slang::ast::CaseStatementCondition::WildcardJustZ) {
        muxDetail = "casez_mux";
        cmpDetail = "casez_eq";
    } else if (caseStmt.condition == slang::ast::CaseStatementCondition::WildcardXOrZ) {
        muxDetail = "casex_mux";
        cmpDetail = "casex_eq";
    } else if (caseStmt.condition == slang::ast::CaseStatementCondition::Inside) {
        muxDetail = "inside_mux";
        cmpDetail = "inside_eq";
    }

    // Process default case first (it's the deepest in the chain)
    // We process each case body as a statement, but to build the Mux chain
    // we need to handle per-LHS-signal. For MVP, process all bodies as statements
    // and record that we handled the case.
    if (caseStmt.defaultCase)
        processStatement(*caseStmt.defaultCase, scopePath, false);

    for (auto& item : caseStmt.items) {
        // Create compare node(s) for each item expression
        for (auto* itemExpr : item.expressions) {
            ValueRef itemRef = decomposeExpr(*itemExpr, scopePath);

            std::string cmpTemp = nextTemp();
            TransformNode cmpNode;
            cmpNode.op_kind = TransformNode::Compare;
            cmpNode.op_detail = cmpDetail;
            cmpNode.inputs.push_back(selectorRef);
            cmpNode.inputs.push_back(itemRef);
            cmpNode.output = {cmpTemp, cmpTemp, "", ValueRef::Net, false};
            cmpNode.bit_width = 1;
            cmpNode.approximate = false;
            graph_.addNode(std::move(cmpNode));
        }

        // Process the item body
        processStatement(*item.stmt, scopePath, false);
    }
}

void TransformExtractor::processForLoop(const slang::ast::ForLoopStatement& forStmt,
                                        const std::string& scopePath) {
    // Try to evaluate constant bounds
    if (!forStmt.stopExpr) {
        UnsupportedEvent evt;
        evt.kind = "for_loop_no_stop";
        evt.detail = "for loop with no stop expression";
        graph_.unsupported_events.push_back(std::move(evt));
        processStatement(forStmt.body, scopePath, true);
        return;
    }
    auto* stopConst = forStmt.stopExpr->getConstant();
    if (!stopConst || !*stopConst) {
        // Dynamic bounds — cannot unroll
        UnsupportedEvent evt;
        evt.kind = "for_loop_dynamic_bounds";
        evt.detail = "for loop with non-constant bounds";
        graph_.unsupported_events.push_back(std::move(evt));
        // Process body once as approximate
        processStatement(forStmt.body, scopePath, true);
        return;
    }

    // Estimate iteration count from loop structure
    // For simple `for(int i=start; i<stop; i++)` patterns, try to determine count
    // For MVP: just unroll the body maxForUnroll times or fewer, recording nodes
    int iterCount = 0;

    // Check for initializers to find start value
    int64_t startVal = 0;
    for (auto* init : forStmt.initializers) {
        if (init->kind == slang::ast::ExpressionKind::Assignment) {
            auto& assign = init->as<slang::ast::AssignmentExpression>();
            auto* initConst = assign.right().getConstant();
            if (initConst && *initConst) {
                auto optVal = initConst->integer().as<int64_t>();
                if (optVal)
                    startVal = *optVal;
            }
        }
    }

    auto optStop = stopConst->integer().as<int64_t>();
    if (optStop) {
        iterCount = static_cast<int>(std::abs(*optStop - startVal));
    } else {
        iterCount = maxForUnroll_; // fallback
    }

    if (iterCount > maxForUnroll_) {
        UnsupportedEvent evt;
        evt.kind = "for_loop_too_large";
        evt.detail = "iteration count " + std::to_string(iterCount) +
                     " exceeds --max-for-unroll " + std::to_string(maxForUnroll_);
        graph_.unsupported_events.push_back(std::move(evt));
        // Process body once as approximate
        processStatement(forStmt.body, scopePath, true);
        return;
    }

    // Unroll: process body iterCount times
    for (int i = 0; i < iterCount; ++i) {
        processStatement(forStmt.body, scopePath, false);
    }
}

namespace {

std::string normalizeBinaryOp(slang::ast::BinaryOperator op) {
    using BO = slang::ast::BinaryOperator;
    switch (op) {
        case BO::Add: return "add";
        case BO::Subtract: return "sub";
        case BO::Multiply: return "mul";
        case BO::Divide: return "div";
        case BO::Mod: return "mod";
        case BO::Power: return "power";
        case BO::BinaryAnd: return "and";
        case BO::BinaryOr: return "or";
        case BO::BinaryXor: return "xor";
        case BO::BinaryXnor: return "xnor";
        case BO::LogicalAnd: return "logical_and";
        case BO::LogicalOr: return "logical_or";
        case BO::LogicalShiftLeft: return "shift_left";
        case BO::LogicalShiftRight: return "shift_right";
        case BO::ArithmeticShiftLeft: return "arith_shift_left";
        case BO::ArithmeticShiftRight: return "arith_shift_right";
        case BO::Equality: return "eq";
        case BO::Inequality: return "neq";
        case BO::CaseEquality: return "case_eq";
        case BO::CaseInequality: return "case_neq";
        case BO::WildcardEquality: return "wildcard_eq";
        case BO::WildcardInequality: return "wildcard_neq";
        case BO::GreaterThan: return "gt";
        case BO::GreaterThanEqual: return "gte";
        case BO::LessThan: return "lt";
        case BO::LessThanEqual: return "lte";
        default: return std::string(toString(op));
    }
}

std::string normalizeUnaryOp(slang::ast::UnaryOperator op) {
    using UO = slang::ast::UnaryOperator;
    switch (op) {
        case UO::BitwiseNot: return "not";
        case UO::Minus: return "negate";
        case UO::Plus: return "plus";
        case UO::LogicalNot: return "logical_not";
        case UO::BitwiseAnd: return "reduce_and";
        case UO::BitwiseOr: return "reduce_or";
        case UO::BitwiseXor: return "reduce_xor";
        case UO::BitwiseNand: return "reduce_nand";
        case UO::BitwiseNor: return "reduce_nor";
        case UO::BitwiseXnor: return "reduce_xnor";
        default: return std::string(toString(op));
    }
}

// Simple recursive signal name resolver — no graph node creation
std::string resolveSignalName(const slang::ast::Expression& expr) {
    switch (expr.kind) {
        case slang::ast::ExpressionKind::NamedValue: {
            auto& named = expr.as<slang::ast::NamedValueExpression>();
            return std::string(named.symbol.name);
        }
        case slang::ast::ExpressionKind::Conversion: {
            auto& conv = expr.as<slang::ast::ConversionExpression>();
            return resolveSignalName(conv.operand());
        }
        case slang::ast::ExpressionKind::RangeSelect: {
            auto& sel = expr.as<slang::ast::RangeSelectExpression>();
            return resolveSignalName(sel.value());
        }
        case slang::ast::ExpressionKind::ElementSelect: {
            auto& sel = expr.as<slang::ast::ElementSelectExpression>();
            return resolveSignalName(sel.value());
        }
        case slang::ast::ExpressionKind::IntegerLiteral:
        case slang::ast::ExpressionKind::UnbasedUnsizedIntegerLiteral:
        case slang::ast::ExpressionKind::RealLiteral:
            return "__const";
        default:
            return "";
    }
}
} // anonymous namespace

void TransformExtractor::detectFFs(const slang::ast::Statement& stmt,
                                   const std::string& scopePath) {
    switch (stmt.kind) {
        case slang::ast::StatementKind::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            if (exprStmt.expr.kind == slang::ast::ExpressionKind::Assignment) {
                auto& assign = exprStmt.expr.as<slang::ast::AssignmentExpression>();
                std::string lhsName = resolveSignalName(assign.left());
                std::string rhsName = resolveSignalName(assign.right());

                if (lhsName.empty()) break;

                std::string scopedLhs = scopePath.empty() ? lhsName : scopePath + "." + lhsName;
                std::string scopedRhs = (rhsName.empty() || rhsName == "__const" || rhsName == "__unknown")
                    ? rhsName : (scopePath.empty() ? rhsName : scopePath + "." + rhsName);

                ValueRef qRef;
                qRef.hier_path = scopedLhs;
                qRef.base_name = lhsName;
                qRef.kind = ValueRef::FfQ;

                ValueRef dRef;
                dRef.hier_path = scopedRhs.empty() ? "__unknown" : scopedRhs;
                dRef.base_name = rhsName.empty() ? "__unknown" : rhsName;
                dRef.kind = (rhsName == "__const") ? ValueRef::Const : ValueRef::FfDSink;
                dRef.approximate = rhsName.empty();

                FFInfo ff;
                ff.name = scopedLhs;
                ff.q_ref = qRef;
                ff.d_ref = dRef;
                graph_.flip_flops.push_back(std::move(ff));
            }
            break;
        }
        case slang::ast::StatementKind::Block: {
            auto& block = stmt.as<slang::ast::BlockStatement>();
            detectFFs(block.body, scopePath);
            break;
        }
        case slang::ast::StatementKind::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            for (auto* child : list.list) {
                if (child)
                    detectFFs(*child, scopePath);
            }
            break;
        }
        case slang::ast::StatementKind::Conditional: {
            // In always_ff: the reset branch has constant, the else branch has D-input
            auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            detectFFs(cond.ifTrue, scopePath);
            if (cond.ifFalse)
                detectFFs(*cond.ifFalse, scopePath);
            break;
        }
        case slang::ast::StatementKind::Timed: {
            auto& timed = stmt.as<slang::ast::TimedStatement>();
            detectFFs(timed.stmt, scopePath);
            break;
        }
        default:
            break;
    }
}

ValueRef TransformExtractor::decomposeExpr(const slang::ast::Expression& expr,
                                           const std::string& scopePath) {
    switch (expr.kind) {
        case slang::ast::ExpressionKind::NamedValue: {
            return makeNamedRef(expr, scopePath);
        }

        case slang::ast::ExpressionKind::ArbitrarySymbol: {
            auto& sym = expr.as<slang::ast::ArbitrarySymbolExpression>();
            ValueRef ref;
            std::string name(sym.symbol->name);
            ref.hier_path = scopePath.empty() ? name : scopePath + "." + name;
            ref.base_name = name;
            ref.kind = ValueRef::Net;
            return ref;
        }

        case slang::ast::ExpressionKind::IntegerLiteral:
        case slang::ast::ExpressionKind::RealLiteral:
        case slang::ast::ExpressionKind::UnbasedUnsizedIntegerLiteral: {
            ValueRef ref;
            ref.hier_path = "__const";
            ref.base_name = "__const";
            ref.kind = ValueRef::Const;
            return ref;
        }

        case slang::ast::ExpressionKind::Conversion: {
            auto& conv = expr.as<slang::ast::ConversionExpression>();
            ValueRef inputRef = decomposeExpr(conv.operand(), scopePath);

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Cast;
            node.op_detail = "conversion";
            node.inputs.push_back(inputRef);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::BinaryOp: {
            auto& binOp = expr.as<slang::ast::BinaryExpression>();
            ValueRef leftRef = decomposeExpr(binOp.left(), scopePath);
            ValueRef rightRef = decomposeExpr(binOp.right(), scopePath);

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Binary;
            node.op_detail = normalizeBinaryOp(binOp.op);
            node.inputs.push_back(leftRef);
            node.inputs.push_back(rightRef);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::UnaryOp: {
            auto& unOp = expr.as<slang::ast::UnaryExpression>();
            ValueRef operandRef = decomposeExpr(unOp.operand(), scopePath);

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Unary;
            node.op_detail = normalizeUnaryOp(unOp.op);
            node.inputs.push_back(operandRef);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::ConditionalOp: {
            auto& cond = expr.as<slang::ast::ConditionalExpression>();
            ValueRef condRef = decomposeExpr(*cond.conditions[0].expr, scopePath);
            ValueRef trueRef = decomposeExpr(cond.left(), scopePath);
            ValueRef falseRef = decomposeExpr(cond.right(), scopePath);

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Mux;
            node.op_detail = "ternary";
            node.inputs.push_back(condRef);
            node.inputs.push_back(trueRef);
            node.inputs.push_back(falseRef);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::Concatenation: {
            auto& concat = expr.as<slang::ast::ConcatenationExpression>();
            std::vector<ValueRef> inputs;
            for (auto* operand : concat.operands())
                inputs.push_back(decomposeExpr(*operand, scopePath));

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Concat;
            node.op_detail = "concat";
            node.inputs = std::move(inputs);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::Replication: {
            auto& rep = expr.as<slang::ast::ReplicationExpression>();
            ValueRef inputRef = decomposeExpr(rep.concat(), scopePath);

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Replicate;
            node.op_detail = "replicate";
            node.inputs.push_back(inputRef);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::RangeSelect: {
            auto& sel = expr.as<slang::ast::RangeSelectExpression>();
            ValueRef baseRef = decomposeExpr(sel.value(), scopePath);

            std::string left = "?", right = "?";
            if (auto* c = sel.left().getConstant(); c && *c)
                left = c->toString();
            if (auto* c = sel.right().getConstant(); c && *c)
                right = c->toString();

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Slice;
            node.op_detail = "range[" + left + ":" + right + "]";
            node.inputs.push_back(baseRef);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::ElementSelect: {
            auto& sel = expr.as<slang::ast::ElementSelectExpression>();
            ValueRef baseRef = decomposeExpr(sel.value(), scopePath);

            std::string idx = "?";
            if (auto* c = sel.selector().getConstant(); c && *c)
                idx = c->toString();

            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Slice;
            node.op_detail = "elem[" + idx + "]";
            node.inputs.push_back(baseRef);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        case slang::ast::ExpressionKind::MemberAccess: {
            auto& access = expr.as<slang::ast::MemberAccessExpression>();
            ValueRef baseRef = decomposeExpr(access.value(), scopePath);

            // Try to get precise bit offset for packed struct fields
            auto* field = access.member.as_if<slang::ast::FieldSymbol>();
            if (field) {
                auto& parentType = access.value().type->getCanonicalType();
                if (parentType.kind == slang::ast::SymbolKind::PackedStructType) {
                    uint64_t offset = field->bitOffset;
                    uint32_t width = field->getType().getBitWidth();
                    std::string detail = "field." + std::string(field->name) +
                        "[" + std::to_string(offset + width - 1) + ":" +
                        std::to_string(offset) + "]";

                    std::string tempName = nextTemp();
                    TransformNode node;
                    node.op_kind = TransformNode::Slice;
                    node.op_detail = detail;
                    node.inputs.push_back(baseRef);
                    node.output = {tempName, tempName, "", ValueRef::Net, false};
                    node.bit_width = width;
                    node.source_loc = sourceLoc(expr);
                    node.approximate = false;
                    graph_.addNode(std::move(node));
                    return {tempName, tempName, "", ValueRef::Net, false};
                }
            }

            // Fallback: unpacked struct or non-field member — approximate
            baseRef.hier_path += "." + std::string(access.member.name);
            baseRef.approximate = true;
            return baseRef;
        }

        case slang::ast::ExpressionKind::Call: {
            auto& call = expr.as<slang::ast::CallExpression>();

            // Decompose all call arguments as inputs
            std::vector<ValueRef> argRefs;
            for (auto* arg : call.arguments()) {
                argRefs.push_back(decomposeExpr(*arg, scopePath));
            }

            if (!call.isSystemCall()) {
                // User-defined function: process body once as approximate
                auto* sub = std::get_if<const slang::ast::SubroutineSymbol*>(&call.subroutine);
                if (sub && *sub) {
                    processStatement((*sub)->getBody(), scopePath, true);
                }
            }

            // Create a node representing the call result
            std::string tempName = nextTemp();
            TransformNode node;
            node.op_kind = TransformNode::Unary;
            node.op_detail = "call:" + std::string(call.getSubroutineName());
            node.inputs = std::move(argRefs);
            node.output = {tempName, tempName, "", ValueRef::Net, false};
            node.bit_width = expr.type->getBitWidth();
            node.source_loc = sourceLoc(expr);
            node.approximate = true;
            graph_.addNode(std::move(node));

            return {tempName, tempName, "", ValueRef::Net, false};
        }

        default: {
            // Record unsupported expression
            UnsupportedEvent evt;
            evt.kind = "expression";
            evt.detail = "unsupported expression kind";
            evt.source_loc = sourceLoc(expr);
            graph_.unsupported_events.push_back(std::move(evt));

            ValueRef ref;
            ref.hier_path = "__unknown";
            ref.base_name = "__unknown";
            ref.kind = ValueRef::Unknown;
            ref.approximate = true;
            return ref;
        }
    }
}

ValueRef TransformExtractor::makeNamedRef(const slang::ast::Expression& expr,
                                          const std::string& scopePath) const {
    ValueRef ref;
    ref.kind = ValueRef::Net;

    switch (expr.kind) {
        case slang::ast::ExpressionKind::NamedValue: {
            auto& named = expr.as<slang::ast::NamedValueExpression>();
            std::string name(named.symbol.name);
            ref.base_name = name;
            ref.hier_path = scopePath.empty() ? name : scopePath + "." + name;
            break;
        }
        case slang::ast::ExpressionKind::RangeSelect: {
            auto& sel = expr.as<slang::ast::RangeSelectExpression>();
            ref = makeNamedRef(sel.value(), scopePath);
            std::string left = "?", right = "?";
            if (auto* c = sel.left().getConstant(); c && *c)
                left = c->toString();
            if (auto* c = sel.right().getConstant(); c && *c)
                right = c->toString();
            ref.selector += "[" + left + ":" + right + "]";
            break;
        }
        case slang::ast::ExpressionKind::ElementSelect: {
            auto& sel = expr.as<slang::ast::ElementSelectExpression>();
            ref = makeNamedRef(sel.value(), scopePath);
            std::string idx = "?";
            if (auto* c = sel.selector().getConstant(); c && *c)
                idx = c->toString();
            ref.selector += "[" + idx + "]";
            break;
        }
        case slang::ast::ExpressionKind::Conversion: {
            auto& conv = expr.as<slang::ast::ConversionExpression>();
            return makeNamedRef(conv.operand(), scopePath);
        }
        case slang::ast::ExpressionKind::Concatenation: {
            // LHS concatenation: treat as approximate
            ref.hier_path = "__lhs_concat";
            ref.base_name = "__lhs_concat";
            ref.approximate = true;
            break;
        }
        default: {
            ref.hier_path = "__expr";
            ref.base_name = "__expr";
            ref.approximate = true;
            break;
        }
    }
    return ref;
}

std::string TransformExtractor::sourceLoc(const slang::ast::Expression& expr) const {
    const auto* srcMgr = compilation_.getSourceManager();
    if (!srcMgr)
        return "";

    auto loc = expr.sourceRange.start();
    if (!loc.valid())
        return "";

    auto fileName = srcMgr->getFileName(loc);
    auto lineNum = srcMgr->getLineNumber(loc);
    if (fileName.empty())
        return "";

    return std::string(fileName) + ":" + std::to_string(lineNum);
}

} // namespace metrics
