#include "ConnectionExtractor.h"
#include "StyleSyntaxScanner.h"

#include <slang/ast/Expression.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/Statement.h>
#include <slang/ast/EvalContext.h>
#include <slang/ast/SemanticFacts.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/statements/MiscStatements.h>
#include <slang/ast/types/Type.h>
#include <slang/ast/TimingControl.h>
#include <slang/ast/ASTVisitor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/parsing/TokenKind.h>
#include <slang/text/SourceManager.h>

#include <fmt/core.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <set>
#include <span>
#include <string_view>
#include <tuple>

namespace connect {

namespace {

void appendUnique(std::vector<std::string>& dest, const std::vector<std::string>& src) {
    for (const auto& value : src) {
        if (std::find(dest.begin(), dest.end(), value) == dest.end())
            dest.push_back(value);
    }
}

bool hasDynamicIndex(std::string_view key) {
    return key.find("[?]") != std::string_view::npos;
}

// Compare a dynamic selection with an existing bound net. A whole-array or
// whole-element key may end before later selectors; different fields and
// different constant indices cannot meet. The '?' marker comes only from
// resolveExpr's nonconstant element selector.
bool dynamicPathMayOverlap(std::string_view pattern, std::string_view bound) {
    size_t i = 0;
    size_t j = 0;
    while (i < pattern.size() && j < bound.size()) {
        if (pattern[i] == '[' && bound[j] == '[') {
            const auto patternEnd = pattern.find(']', i + 1);
            const auto boundEnd = bound.find(']', j + 1);
            if (patternEnd == std::string_view::npos || boundEnd == std::string_view::npos)
                return false;
            const auto patternIndex = pattern.substr(i + 1, patternEnd - i - 1);
            const auto boundIndex = bound.substr(j + 1, boundEnd - j - 1);
            if (patternIndex != "?" && patternIndex != boundIndex)
                return false;
            i = patternEnd + 1;
            j = boundEnd + 1;
        } else if (pattern[i] == bound[j]) {
            ++i;
            ++j;
        } else {
            return false;
        }
    }
    if (i == pattern.size() && j == bound.size())
        return true;
    if (i == pattern.size())
        return bound[j] == '[' || bound[j] == '.';
    return pattern[i] == '[' || pattern[i] == '.';
}

bool isConstantOnly(const slang::ast::Expression* expr) {
    if (!expr)
        return false;

    auto* constant = expr->getConstant();
    return constant && *constant;
}

bool isConstantZero(const slang::ast::Expression* expr) {
    if (!expr)
        return false;

    auto* constant = expr->getConstant();
    if (!constant || !*constant)
        return false;

    std::string text = constant->toString();
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](unsigned char c) { return std::isspace(c) != 0; }),
               text.end());
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (text == "0" || text == "'0")
        return true;

    auto apostrophe = text.find('\'');
    if (apostrophe != std::string::npos) {
        auto base_pos = apostrophe + 1;
        if (base_pos < text.size() && std::isalpha(static_cast<unsigned char>(text[base_pos])))
            ++base_pos;
        if (base_pos >= text.size())
            return false;
        for (size_t i = base_pos; i < text.size(); ++i) {
            if (text[i] != '0')
                return false;
        }
        return true;
    }

    return false;
}

bool hasNonIdentityConversion(const slang::ast::Expression& expr) {
    bool changesRepresentation = false;
    auto visitor = slang::ast::makeVisitor([&](auto& self, const slang::ast::ConversionExpression& conversion) {
        if (!conversion.type || !conversion.operand().type ||
            conversion.type->getBitWidth() != conversion.operand().type->getBitWidth() ||
            conversion.type->isFourState() != conversion.operand().type->isFourState())
            changesRepresentation = true;
        self.visitDefault(conversion);
    });
    expr.visit(visitor);
    return changesRepresentation;
}

bool isSingleUnconditionalAssignment(const slang::ast::Statement& stmt) {
    using SK = slang::ast::StatementKind;
    switch (stmt.kind) {
    case SK::ExpressionStatement: {
        const auto& expr = stmt.as<slang::ast::ExpressionStatement>().expr;
        if (expr.kind != slang::ast::ExpressionKind::Assignment)
            return false;
        const auto& assignment = expr.as<slang::ast::AssignmentExpression>();
        return assignment.isBlocking() && !assignment.isCompound() && !assignment.isLValueArg() &&
               !assignment.timingControl;
    }
    case SK::Block:
        return isSingleUnconditionalAssignment(stmt.as<slang::ast::BlockStatement>().body);
    case SK::List: {
        const slang::ast::Statement* only = nullptr;
        for (const auto* child : stmt.as<slang::ast::StatementList>().list) {
            if (!child)
                continue;
            if (only)
                return false;
            only = child;
        }
        return only && isSingleUnconditionalAssignment(*only);
    }
    default:
        return false;
    }
}

std::optional<bool> knownIfBranch(const slang::ast::ConditionalStatement& statement) {
    if (statement.conditions.size() != 1 || statement.conditions.front().pattern)
        return std::nullopt;
    const auto* value = statement.conditions.front().expr->getConstant();
    if (!value || !*value)
        return std::nullopt;
    return value->isTrue();
}

std::optional<const slang::ast::Statement*> knownCaseBranch(const slang::ast::CaseStatement& statement,
                                                            const slang::ast::Symbol& owner) {
    if (statement.condition == slang::ast::CaseStatementCondition::Inside)
        return std::nullopt;
    if (const auto* selector = statement.expr.getConstant(); selector && !*selector)
        return std::nullopt;
    slang::ast::EvalContext context(owner);
    const auto [branch, known] = statement.getKnownBranch(context);
    if (!known)
        return std::nullopt;
    return branch;
}

// Peel a (possibly multi-dimensional) ElementSelect chain down to its
// root NamedValue symbol. Reports the root array leaf name, whether ANY
// selector along the chain is a non-constant (variable) index, and
// whether the root symbol is an unpacked array (the storage shape DC
// turns into a memory/latch). Returns false when the expression is not
// rooted at a simple named unpacked-array element select.
bool resolveArrayElementSelect(const slang::ast::Expression* expr, std::string& rootName, bool& hasNonConstIndex,
                               std::vector<const slang::ast::Expression*>* nonConstSelectors = nullptr) {
    using slang::ast::ExpressionKind;
    hasNonConstIndex = false;
    const slang::ast::Expression* cur = expr;
    bool sawSelect = false;
    // Unwrap conversions and walk down through nested element selects.
    while (cur) {
        if (cur->kind == ExpressionKind::Conversion) {
            cur = &cur->as<slang::ast::ConversionExpression>().operand();
            continue;
        }
        if (cur->kind == ExpressionKind::ElementSelect) {
            auto& sel = cur->as<slang::ast::ElementSelectExpression>();
            auto* constant = sel.selector().getConstant();
            if (!(constant && *constant)) {
                hasNonConstIndex = true;
                if (nonConstSelectors)
                    nonConstSelectors->push_back(&sel.selector());
            }
            sawSelect = true;
            cur = &sel.value();
            continue;
        }
        break;
    }
    if (!sawSelect || !cur || cur->kind != ExpressionKind::NamedValue)
        return false;
    auto& named = cur->as<slang::ast::NamedValueExpression>();
    // Only unpacked arrays (register-file / memory storage) are at risk;
    // a packed-vector bit/part select is ordinary combinational fanout.
    if (!named.symbol.getType().isUnpackedArray())
        return false;
    rootName = std::string(named.symbol.name);
    return true;
}

// True when every value reference inside the selector expression resolves
// to one of the in-scope for-loop induction variables (so a sweep over
// these indices touches every array element). A selector that references
// any other signal (a register/port like `diag` / `i_waddr`) is a
// data-dependent partial address -> not loop-var-only.
bool selectorUsesOnlyLoopVars(const slang::ast::Expression& sel,
                              const std::unordered_set<const slang::ast::Symbol*>& loopVars) {
    bool onlyLoopVars = true;
    auto visitor = slang::ast::makeVisitor([&](auto& self, const slang::ast::NamedValueExpression& nv) {
        if (loopVars.find(&nv.symbol) == loopVars.end())
            onlyLoopVars = false;
        self.visitDefault(nv);
    });
    sel.visit(visitor);
    return onlyLoopVars;
}

// True when a conditional's controlling expression references a
// reset-named signal (rst / rst_n / *_rst* / reset variants). Used to
// skip the async-reset clear branch of a clocked block: writes there are
// the reset preset, not the functional next-state, so they neither
// establish nor clear the latch-inference verdict.
bool conditionReferencesReset(std::span<const slang::ast::ConditionalStatement::Condition> conds) {
    auto looksLikeReset = [](const std::string& s) {
        return s == "rst" || s.starts_with("rst_") || s.ends_with("_rst") || s.find("_rst_") != std::string::npos ||
               s == "reset" || s.starts_with("reset_") || s.ends_with("_reset") ||
               s.find("_reset_") != std::string::npos;
    };
    bool found = false;
    auto visitor = slang::ast::makeVisitor([&](auto& self, const slang::ast::NamedValueExpression& nv) {
        if (looksLikeReset(std::string(nv.symbol.name)))
            found = true;
        self.visitDefault(nv);
    });
    for (const auto& c : conds)
        c.expr->visit(visitor);
    return found;
}

slang::ast::ArgumentDirection inferInterfaceDirection(const slang::ast::PortConnection& conn) {
    const auto [_, modport] = conn.getIfaceConn();
    if (!modport)
        return slang::ast::ArgumentDirection::InOut;

    bool hasInput = false;
    bool hasOutput = false;
    for (const auto& member : modport->members()) {
        if (member.kind != slang::ast::SymbolKind::ModportPort)
            continue;

        const auto direction = member.as<slang::ast::ModportPortSymbol>().direction;
        if (direction == slang::ast::ArgumentDirection::In)
            hasInput = true;
        else if (direction == slang::ast::ArgumentDirection::Out)
            hasOutput = true;
        else if (direction == slang::ast::ArgumentDirection::InOut)
            return slang::ast::ArgumentDirection::InOut;
    }

    if (hasOutput && !hasInput)
        return slang::ast::ArgumentDirection::Out;
    if (hasInput && !hasOutput)
        return slang::ast::ArgumentDirection::In;
    return slang::ast::ArgumentDirection::InOut;
}

struct InterfaceUsage {
    bool read = false;
    bool written = false;
    bool unknownRead = false;
    bool unknownWrite = false;
    std::vector<std::pair<int64_t, int64_t>> readRanges;
    std::vector<std::pair<int64_t, int64_t>> writeRanges;
    std::vector<std::pair<int64_t, int64_t>> approximateReadRanges;
    std::vector<std::pair<int64_t, int64_t>> approximateWriteRanges;
};

struct InterfaceMemberRange {
    const slang::ast::Symbol* member = nullptr;
    int64_t left = 0;
    int64_t right = 0;
};

// Slang keeps indexed part-select operands as (base, width), not (left, right).
// Resolve their actual bounds only when both operands are elaboration-time
// constants and the entire interval lies within the selected packed range.
std::optional<std::pair<int64_t, int64_t>> constantRangeSelectBounds(const slang::ast::RangeSelectExpression& select) {
    if (!select.type || !select.value().type || !select.value().type->isIntegral() ||
        !select.value().type->hasFixedRange())
        return std::nullopt;
    const auto* first = select.left().getConstant();
    const auto* second = select.right().getConstant();
    if (!first || !second || !*first || !*second || !first->isInteger() || !second->isInteger())
        return std::nullopt;
    const auto firstValue = first->integer().as<int64_t>();
    const auto secondValue = second->integer().as<int64_t>();
    if (!firstValue || !secondValue)
        return std::nullopt;

    const auto declared = select.value().type->getFixedRange();
    int64_t left = *firstValue;
    int64_t right = *secondValue;
    if (select.getSelectionKind() != slang::ast::RangeSelectionKind::Simple) {
        if (right <= 0 || static_cast<uint64_t>(right) != select.type->getBitWidth() || left < declared.lower() ||
            left > declared.upper())
            return std::nullopt;
        const int64_t span = right - 1;
        int64_t low = left;
        int64_t high = left;
        if (select.getSelectionKind() == slang::ast::RangeSelectionKind::IndexedUp) {
            if (span > static_cast<int64_t>(declared.upper()) - left)
                return std::nullopt;
            high += span;
        } else {
            if (span > left - static_cast<int64_t>(declared.lower()))
                return std::nullopt;
            low -= span;
        }
        left = declared.left >= declared.right ? high : low;
        right = declared.left >= declared.right ? low : high;
    }
    if (std::min(left, right) < declared.lower() || std::max(left, right) > declared.upper() ||
        static_cast<uint64_t>(std::max(left, right) - std::min(left, right)) + 1 != select.type->getBitWidth())
        return std::nullopt;
    return std::pair{left, right};
}

// Only a direct constant bit/part select of one integral interface member
// proves which lanes were accessed. Complex expressions stay whole-member
// may-flow; their subexpressions cannot be treated as positional wiring.
std::optional<InterfaceMemberRange> directInterfaceMemberRange(const slang::ast::Expression& expr) {
    using EK = slang::ast::ExpressionKind;
    const slang::ast::Expression* current = &expr;
    while (current->kind == EK::Conversion)
        current = &current->as<slang::ast::ConversionExpression>().operand();

    const slang::ast::Expression* value = nullptr;
    int64_t left = 0;
    int64_t right = 0;
    if (current->kind == EK::RangeSelect) {
        const auto& select = current->as<slang::ast::RangeSelectExpression>();
        const auto bounds = constantRangeSelectBounds(select);
        if (!bounds)
            return std::nullopt;
        std::tie(left, right) = *bounds;
        value = &select.value();
    } else if (current->kind == EK::ElementSelect) {
        const auto& select = current->as<slang::ast::ElementSelectExpression>();
        const auto* index = select.selector().getConstant();
        if (!index || !*index || !index->isInteger())
            return std::nullopt;
        const auto indexValue = index->integer().as<int64_t>();
        if (!indexValue)
            return std::nullopt;
        left = right = *indexValue;
        value = &select.value();
    } else {
        return std::nullopt;
    }

    while (value->kind == EK::Conversion)
        value = &value->as<slang::ast::ConversionExpression>().operand();
    const slang::ast::Symbol* member = nullptr;
    if (value->kind == EK::MemberAccess)
        member = &value->as<slang::ast::MemberAccessExpression>().member;
    else if (value->kind == EK::NamedValue)
        member = &value->as<slang::ast::NamedValueExpression>().symbol;
    else if (value->kind == EK::HierarchicalValue)
        member = &value->as<slang::ast::HierarchicalValueExpression>().symbol;
    if (!member || (member->kind != slang::ast::SymbolKind::Net && member->kind != slang::ast::SymbolKind::Variable))
        return std::nullopt;
    const auto& type = member->as<slang::ast::ValueSymbol>().getType();
    if (!type.isIntegral() || !type.hasFixedRange() || type.getFixedRange().fullWidth() != type.getBitWidth())
        return std::nullopt;
    const auto declared = type.getFixedRange();
    if (std::min(left, right) < declared.lower() || std::max(left, right) > declared.upper())
        return std::nullopt;
    return InterfaceMemberRange{member, left, right};
}

// Whole-interface ports have no declared modport direction. Infer a member's
// local use from the child's elaborated assignments and control expressions.
// Unused members are omitted, avoiding fabricated links across the bundle.
std::unordered_map<const slang::ast::Symbol*, InterfaceUsage>
scanInterfaceUsage(const slang::ast::InstanceSymbol& child, const slang::ast::InstanceSymbol& iface) {
    std::unordered_map<const slang::ast::Symbol*, InterfaceUsage> usage;
    for (const auto& member : iface.body.members()) {
        if (member.kind == slang::ast::SymbolKind::Net || member.kind == slang::ast::SymbolKind::Variable)
            usage.emplace(&member, InterfaceUsage{});
    }

    auto mark = [&](const slang::ast::Symbol& symbol, bool write,
                    std::optional<std::pair<int64_t, int64_t>> range = std::nullopt, bool approximate = false) {
        if (auto it = usage.find(&symbol); it != usage.end()) {
            if (write) {
                it->second.written = true;
                if (range) {
                    if (approximate)
                        it->second.approximateWriteRanges.push_back(*range);
                    else
                        it->second.writeRanges.push_back(*range);
                } else {
                    it->second.unknownWrite = true;
                }
            } else {
                it->second.read = true;
                if (range) {
                    if (approximate)
                        it->second.approximateReadRanges.push_back(*range);
                    else
                        it->second.readRanges.push_back(*range);
                } else {
                    it->second.unknownRead = true;
                }
            }
        }
    };
    auto markSelected = [&](const slang::ast::Expression& selected, bool write) {
        if (const auto range = directInterfaceMemberRange(selected); range && usage.contains(range->member)) {
            mark(*range->member, write, std::pair{range->left, range->right}, true);
            return true;
        }
        return false;
    };
    auto visitRefs = [&](const slang::ast::Expression& expr, bool write) {
        auto refs = slang::ast::makeVisitor(
            [&](auto&, const slang::ast::NamedValueExpression& ref) { mark(ref.symbol, write); },
            [&](auto&, const slang::ast::HierarchicalValueExpression& ref) { mark(ref.symbol, write); },
            [&](auto&, const slang::ast::ArbitrarySymbolExpression& ref) { mark(*ref.symbol, write); },
            [&](auto& self, const slang::ast::ConditionalExpression& ref) {
                if (const auto* known = ref.knownSide())
                    known->visit(self);
                else
                    self.visitDefault(ref);
            },
            [&](auto& self, const slang::ast::RangeSelectExpression& ref) {
                if (!markSelected(ref, write))
                    self.visitDefault(ref);
            },
            [&](auto& self, const slang::ast::ElementSelectExpression& ref) {
                if (!markSelected(ref, write))
                    self.visitDefault(ref);
            },
            [&](auto& self, const slang::ast::MemberAccessExpression& ref) {
                mark(ref.member, write);
                self.visitDefault(ref);
            });
        expr.visit(refs);
    };
    auto scanSide = [&](const slang::ast::Expression& expr, bool write) {
        if (const auto selected = directInterfaceMemberRange(expr); selected && usage.contains(selected->member)) {
            mark(*selected->member, write, std::pair{selected->left, selected->right});
            return;
        }
        visitRefs(expr, write);
    };
    auto scanAssignment = [&](const slang::ast::AssignmentExpression& assignment) {
        scanSide(assignment.left(), true);
        scanSide(assignment.right(), false);
    };

    auto scanScope = [&](auto&& self, const slang::ast::Scope& scope) -> void {
        for (const auto& member : scope.members()) {
            if (member.kind == slang::ast::SymbolKind::ContinuousAssign) {
                const auto& expr = member.as<slang::ast::ContinuousAssignSymbol>().getAssignment();
                if (expr.kind == slang::ast::ExpressionKind::Assignment)
                    scanAssignment(expr.as<slang::ast::AssignmentExpression>());
            } else if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
                const auto& block = member.as<slang::ast::ProceduralBlockSymbol>();
                auto visitor = slang::ast::makeVisitor(
                    [&](auto&, const slang::ast::AssignmentExpression& assignment) { scanAssignment(assignment); },
                    [&](auto& self, const slang::ast::ConditionalStatement& statement) {
                        if (const auto known = knownIfBranch(statement)) {
                            if (*known)
                                statement.ifTrue.visit(self);
                            else if (statement.ifFalse)
                                statement.ifFalse->visit(self);
                        } else {
                            self.visitDefault(statement);
                        }
                    },
                    [&](auto& self, const slang::ast::CaseStatement& statement) {
                        if (const auto known = knownCaseBranch(statement, block)) {
                            if (*known)
                                (*known)->visit(self);
                        } else {
                            self.visitDefault(statement);
                        }
                    },
                    [&](auto& self, const slang::ast::ConditionalExpression& ref) {
                        if (const auto* known = ref.knownSide())
                            known->visit(self);
                        else
                            self.visitDefault(ref);
                    },
                    [&](auto& self, const slang::ast::RangeSelectExpression& ref) {
                        if (!markSelected(ref, false))
                            self.visitDefault(ref);
                    },
                    [&](auto& self, const slang::ast::ElementSelectExpression& ref) {
                        if (!markSelected(ref, false))
                            self.visitDefault(ref);
                    },
                    [&](auto&, const slang::ast::HierarchicalValueExpression& ref) { mark(ref.symbol, false); },
                    [&](auto&, const slang::ast::NamedValueExpression& ref) { mark(ref.symbol, false); },
                    [&](auto& self, const slang::ast::MemberAccessExpression& ref) {
                        mark(ref.member, false);
                        self.visitDefault(ref);
                    });
                block.getBody().visit(visitor);
            } else if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
                const auto& block = member.as<slang::ast::GenerateBlockSymbol>();
                if (!block.isUninstantiated)
                    self(self, block);
            } else if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
                const auto& array = member.as<slang::ast::GenerateBlockArraySymbol>();
                for (const auto* entry : array.entries) {
                    if (entry && !entry->isUninstantiated)
                        self(self, *entry);
                }
            }
        }
    };
    scanScope(scanScope, child.body);
    return usage;
}

const slang::ast::Symbol* namedBaseSymbol(const slang::ast::Expression& expr) {
    using EK = slang::ast::ExpressionKind;
    switch (expr.kind) {
    case EK::NamedValue:
        return &expr.as<slang::ast::NamedValueExpression>().symbol;
    case EK::MemberAccess:
        return namedBaseSymbol(expr.as<slang::ast::MemberAccessExpression>().value());
    case EK::ElementSelect:
        return namedBaseSymbol(expr.as<slang::ast::ElementSelectExpression>().value());
    case EK::RangeSelect:
        return namedBaseSymbol(expr.as<slang::ast::RangeSelectExpression>().value());
    case EK::Conversion:
        return namedBaseSymbol(expr.as<slang::ast::ConversionExpression>().operand());
    case EK::Assignment:
        return namedBaseSymbol(expr.as<slang::ast::AssignmentExpression>().left());
    default:
        return nullptr;
    }
}

std::string netKeyForExpression(const slang::ast::Expression& expr, const std::string& name, bool absolute,
                                const std::string& scopePath) {
    if (absolute)
        return name;
    if (const auto* symbol = namedBaseSymbol(expr)) {
        const auto declaredPath = symbol->getHierarchicalPath();
        if (const auto dot = declaredPath.rfind('.'); dot != std::string::npos)
            return declaredPath.substr(0, dot) + "::" + name;
    }
    return scopePath + "::" + name;
}

} // namespace

ConnectionExtractor::ConnectionExtractor(slang::ast::Compilation& compilation, const std::string& topModule,
                                         int maxDepth, bool captureDeclarations)
    : compilation_(compilation), topModule_(topModule), maxDepth_(maxDepth), captureDeclarations_(captureDeclarations) {
}

// Round 33 deslop: shared helper for the modport-rendezvous logic
// used by HierarchicalValue and ArbitrarySymbol cases. Returns the
// underlying signal's absolute hier path when `sym` is a ModportPort
// with a non-null internalSymbol; empty string otherwise. The empty
// return is the caller's signal to fall back to scope-relative
// keying without claiming is_absolute=true.
static std::string modportInternalAbsPath(const slang::ast::Symbol& sym) {
    if (sym.kind != slang::ast::SymbolKind::ModportPort)
        return {};
    auto& mpp = sym.as<slang::ast::ModportPortSymbol>();
    if (!mpp.internalSymbol)
        return {};
    return mpp.internalSymbol->getHierarchicalPath();
}

ConnectionExtractor::ResolvedExpr ConnectionExtractor::resolveExpr(
    const slang::ast::Expression* expr) {
    ResolvedExpr result;
    if (!expr)
        return result;

    switch (expr->kind) {
        case slang::ast::ExpressionKind::NamedValue: {
            auto& named = expr->as<slang::ast::NamedValueExpression>();
            result.netNames.push_back(std::string(named.symbol.name));
            return result;
        }
        case slang::ast::ExpressionKind::HierarchicalValue: {
            // Round 30 US-R05 / Round 32 WARN-1 / Round 33 deslop:
            // for ModportPort references, follow internalSymbol to the
            // underlying signal's hier path so the consumer-side key
            // rendezvous with the modport-expansion side
            // ("top.inst.data"). Otherwise the bare hier path goes
            // through the modport scope ("top.inst.slave.data") and
            // would silently fail to pair; fall back to
            // scope-relative keying without is_absolute=true.
            auto& hier = expr->as<slang::ast::HierarchicalValueExpression>();
            std::string hp = modportInternalAbsPath(hier.symbol);
            if (!hp.empty()) {
                result.netNames.push_back(std::move(hp));
                result.is_absolute = true;
            } else {
                auto fallback = hier.symbol.getHierarchicalPath();
                result.netNames.push_back(
                    fallback.empty() ? std::string(hier.symbol.name)
                                     : fallback);
            }
            return result;
        }
        case slang::ast::ExpressionKind::ArbitrarySymbol: {
            // Round 30 US-R05 / Round 33 INFO-2: same rendezvous logic
            // as HierarchicalValue. For non-ModportPort symbols,
            // promote to absolute path only when hp differs from the
            // leaf name (i.e. the symbol lives under an instance).
            auto& symbolExpr = expr->as<slang::ast::ArbitrarySymbolExpression>();
            std::string leaf{symbolExpr.symbol->name};
            std::string hp = modportInternalAbsPath(*symbolExpr.symbol);
            if (hp.empty()) {
                auto raw = symbolExpr.symbol->getHierarchicalPath();
                if (!raw.empty() && raw != leaf)
                    hp = std::move(raw);
            }
            if (!hp.empty()) {
                result.netNames.push_back(std::move(hp));
                result.is_absolute = true;
            } else {
                result.netNames.push_back(std::move(leaf));
            }
            return result;
        }
        case slang::ast::ExpressionKind::Conversion: {
            auto& conv = expr->as<slang::ast::ConversionExpression>();
            return resolveExpr(&conv.operand());
        }
        case slang::ast::ExpressionKind::Assignment: {
            auto& assign = expr->as<slang::ast::AssignmentExpression>();
            return resolveExpr(&assign.left());
        }
        case slang::ast::ExpressionKind::RangeSelect: {
            auto& sel = expr->as<slang::ast::RangeSelectExpression>();
            result = resolveExpr(&sel.value());

            std::string left = "?", right = "?";
            if (auto* constant = sel.left().getConstant(); constant && *constant)
                left = constant->toString();
            if (auto* constant = sel.right().getConstant(); constant && *constant)
                right = constant->toString();

            if (left == "?" || right == "?") {
                result.approximate = true;
                for (auto& name : result.netNames)
                    name += "[?]";
                return result;
            }

            for (auto& name : result.netNames)
                name += "[" + left + ":" + right + "]";
            return result;
        }
        case slang::ast::ExpressionKind::ElementSelect: {
            auto& sel = expr->as<slang::ast::ElementSelectExpression>();
            result = resolveExpr(&sel.value());

            std::string index = "?";
            if (auto* constant = sel.selector().getConstant(); constant && *constant)
                index = constant->toString();

            if (index == "?")
                result.approximate = true;

            for (auto& name : result.netNames)
                name += "[" + index + "]";
            return result;
        }
        case slang::ast::ExpressionKind::MemberAccess: {
            auto& access = expr->as<slang::ast::MemberAccessExpression>();
            // Round 30 US-R05: ModportPort access where internalSymbol
            // is known -- emit the underlying signal's absolute hier
            // path as the netName. The modport-expansion side emits a
            // matching abs-path entry into netMap_ so the connection
            // forms (same key on both sides). Keep approximate=false
            // so WidthChecker can compare widths.
            if (access.member.kind == slang::ast::SymbolKind::ModportPort) {
                auto& mpp = access.member.as<slang::ast::ModportPortSymbol>();
                if (mpp.internalSymbol) {
                    ResolvedExpr r;
                    r.netNames.push_back(mpp.internalSymbol->getHierarchicalPath());
                    r.is_absolute = true;
                    return r;
                }
            }
            result = resolveExpr(&access.value());
            if (access.member.kind == slang::ast::SymbolKind::Modport ||
                access.member.kind == slang::ast::SymbolKind::ModportPort) {
                result.approximate = true;
                return result;
            }

            for (auto& name : result.netNames)
                name += "." + std::string(access.member.name);
            return result;
        }
        case slang::ast::ExpressionKind::Concatenation: {
            auto& concat = expr->as<slang::ast::ConcatenationExpression>();
            bool allTieOff = true;

            for (auto* operand : concat.operands()) {
                auto child = resolveExpr(operand);
                appendUnique(result.netNames, child.netNames);
                result.approximate = result.approximate || child.approximate;
                allTieOff &= child.tieOff || (child.netNames.empty() && isConstantOnly(operand));
            }

            if (!result.netNames.empty())
                result.approximate = true;
            result.tieOff = result.netNames.empty() && allTieOff;
            return result;
        }
        case slang::ast::ExpressionKind::Replication: {
            auto& replication = expr->as<slang::ast::ReplicationExpression>();
            result = resolveExpr(&replication.concat());
            if (!result.netNames.empty())
                result.approximate = true;
            return result;
        }
        case slang::ast::ExpressionKind::Streaming: {
            auto& streaming = expr->as<slang::ast::StreamingConcatenationExpression>();
            bool allTieOff = true;

            for (const auto& stream : streaming.streams()) {
                auto child = resolveExpr(stream.operand.get());
                appendUnique(result.netNames, child.netNames);
                result.approximate = result.approximate || child.approximate;
                allTieOff &= child.tieOff || (child.netNames.empty() && isConstantOnly(stream.operand.get()));
            }

            if (!result.netNames.empty())
                result.approximate = true;
            result.tieOff = result.netNames.empty() && allTieOff;
            return result;
        }
        case slang::ast::ExpressionKind::EmptyArgument:
            return result;
        default:
            result.tieOff = isConstantOnly(expr);
            return result;
    }
}

// A member can be rooted in a constant unpacked-array element such as
// `bank[0].payload`. Its bracketed key names one storage object. A packed
// select or variable index in that path cannot be treated the same way.
static bool hasConcreteUnpackedMemberPath(const slang::ast::Expression& expr) {
    using EK = slang::ast::ExpressionKind;
    if (expr.kind == EK::NamedValue)
        return true;
    if (expr.kind == EK::Conversion)
        return hasConcreteUnpackedMemberPath(expr.as<slang::ast::ConversionExpression>().operand());
    if (expr.kind == EK::MemberAccess)
        return hasConcreteUnpackedMemberPath(expr.as<slang::ast::MemberAccessExpression>().value());
    if (expr.kind != EK::ElementSelect)
        return false;
    const auto& select = expr.as<slang::ast::ElementSelectExpression>();
    if (!select.value().type || !select.value().type->isUnpackedArray() || !select.value().type->hasFixedRange())
        return false;
    const auto* constant = select.selector().getConstant();
    if (!constant || !*constant || !constant->isInteger())
        return false;
    const auto index = constant->integer().as<int64_t>();
    if (!index)
        return false;
    const auto range = select.value().type->getFixedRange();
    return *index >= range.lower() && *index <= range.upper() && hasConcreteUnpackedMemberPath(select.value());
}

std::optional<ConnectionExtractor::WireRange> ConnectionExtractor::resolveWireRange(const slang::ast::Expression& expr,
                                                                                    const std::string& scopePath,
                                                                                    uint32_t portWidth) {
    using EK = slang::ast::ExpressionKind;
    const slang::ast::Expression* current = &expr;
    while (current->kind == EK::Conversion || current->kind == EK::Assignment) {
        if (current->kind == EK::Conversion)
            current = &current->as<slang::ast::ConversionExpression>().operand();
        else
            current = &current->as<slang::ast::AssignmentExpression>().left();
    }
    if (portWidth == 0)
        return std::nullopt;

    const slang::ast::Expression* base = current;
    int64_t left = 0;
    int64_t right = 0;
    if (current->kind == EK::RangeSelect) {
        const auto& select = current->as<slang::ast::RangeSelectExpression>();
        const auto bounds = constantRangeSelectBounds(select);
        if (!bounds || current->type->getBitWidth() != portWidth)
            return std::nullopt;
        std::tie(left, right) = *bounds;
        base = &select.value();
    } else if (current->kind == EK::ElementSelect) {
        const auto& select = current->as<slang::ast::ElementSelectExpression>();
        const auto* index = select.selector().getConstant();
        if (!index || !*index || !index->isInteger())
            return std::nullopt;
        const auto value = index->integer().as<int64_t>();
        if (!value)
            return std::nullopt;
        // A constant element of an unpacked array is a distinct packed
        // signal, not one bit of the array's flat storage. Use its indexed
        // key and the selected element's own declared range.
        if (!select.value().type->isIntegral()) {
            if (!current->type->isIntegral() || !current->type->hasFixedRange() ||
                current->type->getBitWidth() != portWidth)
                return std::nullopt;
            const auto resolved = resolveExpr(current);
            if (resolved.approximate || resolved.tieOff || resolved.netNames.size() != 1)
                return std::nullopt;
            const auto range = current->type->getFixedRange();
            const bool flatPackedArray = current->type->isPackedArray() && range.fullWidth() != portWidth;
            if (range.fullWidth() != portWidth && !flatPackedArray)
                return std::nullopt;
            return WireRange{netKeyForExpression(*current, resolved.netNames.front(), resolved.is_absolute, scopePath),
                             flatPackedArray ? static_cast<int64_t>(portWidth) - 1 : range.left,
                             flatPackedArray ? 0 : range.right};
        }
        if (select.value().type && select.value().type->isPackedArray()) {
            // Each packed dimension contributes an ordinal offset from its
            // declared right bound. Walk all constant selects so
            // lanes[outer][inner] maps back to the one root packed wire.
            const slang::ast::Expression* packedBase = current;
            uint64_t offset = 0;
            while (packedBase->kind == EK::ElementSelect) {
                const auto& element = packedBase->as<slang::ast::ElementSelectExpression>();
                const auto* selector = element.selector().getConstant();
                if (!selector || !*selector || !selector->isInteger() || !packedBase->type || !element.value().type ||
                    !element.value().type->hasFixedRange())
                    return std::nullopt;
                // Unpacked dimensions select distinct storage objects, not
                // bit positions in one packed wire. Keep their indexed key
                // as the base for the packed offset accumulated so far.
                if (element.value().type->isUnpackedArray())
                    break;
                if (!element.value().type->isPackedArray())
                    return std::nullopt;
                const auto indexValue = selector->integer().as<int64_t>();
                if (!indexValue)
                    return std::nullopt;
                const auto dimension = element.value().type->getFixedRange();
                const uint64_t elementWidth = packedBase->type->getBitWidth();
                if (*indexValue < dimension.lower() || *indexValue > dimension.upper() ||
                    static_cast<uint64_t>(dimension.fullWidth()) * elementWidth != element.value().type->getBitWidth())
                    return std::nullopt;
                offset += static_cast<uint64_t>(std::abs(*indexValue - dimension.right)) * elementWidth;
                packedBase = &element.value();
            }
            const slang::ast::Expression* owner = packedBase;
            while (owner->kind == EK::ElementSelect) {
                const auto& element = owner->as<slang::ast::ElementSelectExpression>();
                const auto* selector = element.selector().getConstant();
                if (!element.value().type || !element.value().type->isUnpackedArray() ||
                    !element.value().type->hasFixedRange() || !selector || !*selector || !selector->isInteger())
                    return std::nullopt;
                const auto indexValue = selector->integer().as<int64_t>();
                if (!indexValue)
                    return std::nullopt;
                const auto dimension = element.value().type->getFixedRange();
                if (*indexValue < dimension.lower() || *indexValue > dimension.upper())
                    return std::nullopt;
                owner = &element.value();
            }
            if ((owner->kind != EK::NamedValue && owner->kind != EK::MemberAccess) || !packedBase->type ||
                !packedBase->type->isIntegral() || current->type->getBitWidth() != portWidth ||
                offset + portWidth > packedBase->type->getBitWidth())
                return std::nullopt;
            const auto resolved = resolveExpr(packedBase);
            if (resolved.approximate || resolved.tieOff || resolved.netNames.size() != 1 ||
                (resolved.netNames.front().find('[') != std::string::npos &&
                 !hasConcreteUnpackedMemberPath(*packedBase)))
                return std::nullopt;
            return WireRange{
                netKeyForExpression(*packedBase, resolved.netNames.front(), resolved.is_absolute, scopePath),
                static_cast<int64_t>(offset + portWidth - 1), static_cast<int64_t>(offset)};
        }
        if (portWidth != 1)
            return std::nullopt;
        left = right = *value;
        base = &select.value();
    } else {
        if (current->kind != EK::NamedValue && current->kind != EK::MemberAccess &&
            current->kind != EK::HierarchicalValue)
            return std::nullopt;
    }
    if (!base->type || !base->type->isIntegral() || !base->type->hasFixedRange())
        return std::nullopt;
    const auto declared = base->type->getFixedRange();
    const bool flatPackedArray = base->type->isPackedArray() && declared.fullWidth() != base->type->getBitWidth();
    if (declared.fullWidth() != base->type->getBitWidth() && !flatPackedArray)
        return std::nullopt;
    if (flatPackedArray && (current->kind == EK::RangeSelect || current->kind == EK::ElementSelect))
        return std::nullopt;
    if ((current->kind == EK::RangeSelect || current->kind == EK::ElementSelect) &&
        (std::min(left, right) < declared.lower() || std::max(left, right) > declared.upper()))
        return std::nullopt;
    if (current->kind != EK::RangeSelect && current->kind != EK::ElementSelect) {
        if (base->type->getBitWidth() != portWidth)
            return std::nullopt;
        left = flatPackedArray ? static_cast<int64_t>(portWidth) - 1 : declared.left;
        right = flatPackedArray ? 0 : declared.right;
    }
    if (base->kind == EK::HierarchicalValue) {
        const auto& symbol = base->as<slang::ast::HierarchicalValueExpression>().symbol;
        auto path = modportInternalAbsPath(symbol);
        if (path.empty())
            path = symbol.getHierarchicalPath();
        if (path.empty())
            return std::nullopt;
        return WireRange{std::move(path), left, right};
    }
    const auto resolved = resolveExpr(base);
    if (resolved.approximate || resolved.tieOff || resolved.netNames.size() != 1 ||
        (resolved.netNames.front().find('[') != std::string::npos && !hasConcreteUnpackedMemberPath(*base)))
        return std::nullopt;
    return WireRange{netKeyForExpression(*base, resolved.netNames.front(), resolved.is_absolute, scopePath), left,
                     right};
}

bool ConnectionExtractor::recordBitFlow(const slang::ast::Expression& lhs, const slang::ast::Expression& rhs,
                                        const std::string& scopePath, bool approximate, bool forceInteresting) {
    constexpr uint32_t kMaxMappedWidth = 4096;
    constexpr size_t kMaxGapExamples = 32;
    bool relevant = false;
    auto visitor = slang::ast::makeVisitor(
        [&](auto& self, const slang::ast::ConcatenationExpression& expr) {
            relevant = true;
            self.visitDefault(expr);
        },
        [&](auto& self, const slang::ast::RangeSelectExpression& expr) {
            relevant = true;
            self.visitDefault(expr);
        },
        [&](auto& self, const slang::ast::ElementSelectExpression& expr) {
            relevant = true;
            self.visitDefault(expr);
        });
    lhs.visit(visitor);
    rhs.visit(visitor);
    const uint32_t lhsWidth = lhs.type ? lhs.type->getBitWidth() : 0;
    const uint32_t rhsWidth = rhs.type ? rhs.type->getBitWidth() : 0;
    bool gapRecorded = false;
    auto recordGap = [&](std::string_view reason) {
        if (!relevant || gapRecorded || isConstantOnly(&rhs))
            return;
        gapRecorded = true;
        ++graph_.bitFlowGapCount;
        const size_t reasonCount = ++graph_.bitFlowGapReasons[std::string(reason)];
        if (reasonCount <= 4 && graph_.bitFlowGaps.size() < kMaxGapExamples)
            graph_.bitFlowGaps.push_back({scopePath, std::string(reason), lhs.sourceRange.start(), lhsWidth, rhsWidth});
    };
    if (!lhs.type || !rhs.type) {
        recordGap("missing_type");
        return false;
    }
    if (lhsWidth == 0 || lhsWidth != rhsWidth) {
        recordGap("width_mismatch");
        return false;
    }
    if (lhsWidth > kMaxMappedWidth) {
        recordGap("width_limit");
        return false;
    }

    struct Part {
        std::optional<WireRange> wire;
        const slang::ast::Expression* expr = nullptr;
        uint32_t offset = 0;
        uint32_t width = 0;
        bool selected = false;
        bool approximate = false;
    };
    bool widthChangingConversion = false;
    bool stateChangingConversion = false;
    bool mappedWidthConversion = false;
    bool mappedConditional = false;
    auto flatten = [&](auto&& self, const slang::ast::Expression& expr, uint32_t offset, std::vector<Part>& parts,
                       bool sourceSide) -> bool {
        using EK = slang::ast::ExpressionKind;
        const slang::ast::Expression* current = &expr;
        while (current->kind == EK::Conversion) {
            const auto& conversion = current->as<slang::ast::ConversionExpression>();
            const auto& operand = conversion.operand();
            if (!operand.type || !current->type) {
                widthChangingConversion = true;
                return false;
            }
            const auto fromWidth = operand.type->getBitWidth();
            const auto toWidth = current->type->getBitWidth();
            if (operand.type->isFourState() != current->type->isFourState()) {
                stateChangingConversion = true;
                return false;
            }
            if (fromWidth != toWidth) {
                const bool simpleOperand = operand.kind == EK::NamedValue || operand.kind == EK::HierarchicalValue ||
                                           operand.kind == EK::MemberAccess || operand.kind == EK::RangeSelect ||
                                           operand.kind == EK::ElementSelect;
                const bool unsignedConcat = operand.kind == EK::Concatenation && !operand.type->isSigned();
                const bool implicit = conversion.conversionKind == slang::ast::ConversionKind::Implicit ||
                                      conversion.conversionKind == slang::ast::ConversionKind::Propagated;
                // An explicit size cast preserves the operand's signedness;
                // type/sign casts that change it are not positional width mapping.
                const bool explicitSize = conversion.conversionKind == slang::ast::ConversionKind::Explicit &&
                                          operand.type->isSigned() == current->type->isSigned();
                if (!sourceSide || (!implicit && !explicitSize) || (!simpleOperand && !unsignedConcat) ||
                    fromWidth == 0 || toWidth == 0 || fromWidth > kMaxMappedWidth || !operand.type->isIntegral() ||
                    !current->type->isIntegral() || operand.type->isFourState() != current->type->isFourState()) {
                    widthChangingConversion = true;
                    return false;
                }
                const uint32_t copiedWidth = std::min(fromWidth, toWidth);
                if (unsignedConcat) {
                    std::vector<Part> operands;
                    if (!self(self, operand, 0, operands, true)) {
                        widthChangingConversion = true;
                        return false;
                    }
                    for (auto& part : operands) {
                        if (part.offset >= copiedWidth)
                            continue;
                        part.width = std::min(part.width, copiedWidth - part.offset);
                        part.offset += offset;
                        parts.push_back(std::move(part));
                    }
                    mappedWidthConversion = true;
                    return true;
                }
                auto wire = resolveWireRange(operand, scopePath, fromWidth);
                if (!wire) {
                    widthChangingConversion = true;
                    return false;
                }
                parts.push_back({*wire, &operand, offset, copiedWidth, false});
                if (operand.type->isSigned() && toWidth > fromWidth) {
                    WireRange signBit{wire->baseKey, wire->left, wire->left};
                    for (uint32_t bit = fromWidth; bit < toWidth; ++bit)
                        parts.push_back({signBit, &operand, offset + bit, 1, false});
                }
                mappedWidthConversion = true;
                return true;
            }
            current = &operand;
        }
        if (!current->type || current->type->getBitWidth() == 0)
            return false;
        if (current->kind == EK::ConditionalOp) {
            if (!sourceSide)
                return false;
            const auto& conditional = current->as<slang::ast::ConditionalExpression>();
            if (const auto* known = conditional.knownSide())
                return self(self, *known, offset, parts, sourceSide);
            if (!conditional.left().type || !conditional.right().type ||
                conditional.left().type->getBitWidth() != current->type->getBitWidth() ||
                conditional.right().type->getBitWidth() != current->type->getBitWidth())
                return false;
            mappedConditional = true;
            const size_t leftStart = parts.size();
            if (!self(self, conditional.left(), offset, parts, sourceSide))
                return false;
            for (size_t i = leftStart; i < parts.size(); ++i)
                parts[i].approximate = true;
            const size_t rightStart = parts.size();
            if (!self(self, conditional.right(), offset, parts, sourceSide))
                return false;
            for (size_t i = rightStart; i < parts.size(); ++i)
                parts[i].approximate = true;
            return true;
        }
        if (current->kind == EK::Concatenation) {
            const auto operands = current->as<slang::ast::ConcatenationExpression>().operands();
            uint32_t childOffset = offset;
            for (size_t i = operands.size(); i > 0; --i) {
                const auto* operand = operands[i - 1];
                if (!operand || !operand->type || !self(self, *operand, childOffset, parts, sourceSide))
                    return false;
                childOffset += operand->type->getBitWidth();
            }
            return true;
        }
        const auto width = current->type->getBitWidth();
        parts.push_back({resolveWireRange(*current, scopePath, width), current, offset, width,
                         current->kind == EK::RangeSelect || current->kind == EK::ElementSelect});
        return true;
    };

    std::vector<Part> targets;
    std::vector<Part> sources;
    if (!flatten(flatten, lhs, 0, targets, false) || !flatten(flatten, rhs, 0, sources, true)) {
        recordGap(stateChangingConversion   ? "state_changing_conversion"
                  : widthChangingConversion ? "width_changing_conversion"
                                            : "unflattenable_expression");
        return false;
    }
    bool interesting = forceInteresting || mappedWidthConversion || mappedConditional || targets.size() > 1 ||
                       sources.size() > 1 || lhs.kind == slang::ast::ExpressionKind::Concatenation ||
                       rhs.kind == slang::ast::ExpressionKind::Concatenation;
    for (const auto& part : targets)
        interesting |= part.selected;
    for (const auto& part : sources)
        interesting |= part.selected;

    bool emitted = false;
    for (const auto& target : targets) {
        if (!target.wire) {
            recordGap("unresolved_destination_range");
            continue;
        }
        for (const auto& source : sources) {
            const auto low = std::max(target.offset, source.offset);
            const auto high = std::min(target.offset + target.width, source.offset + source.width);
            if (low >= high)
                continue;
            if (!source.wire) {
                std::vector<std::string> refs;
                if (source.expr)
                    collectDependencyKeys(*source.expr, scopePath, refs);
                // An unsized literal can lack a cached constant value even
                // though it has no signal dependency. Do not count tie-offs
                // as missing source connectivity.
                if (!refs.empty()) {
                    recordGap(source.selected ? "unresolved_source_range" : "nonstructural_source");
                }
                if (interesting && !target.selected) {
                    for (const auto& ref : refs) {
                        if (ref != target.wire->baseKey)
                            proceduralDependencies_.emplace_back(ref, target.wire->baseKey);
                    }
                    emitted |= !refs.empty();
                }
                continue;
            }
            bitFlowLinks_.push_back({*source.wire, *target.wire, low - source.offset, low - target.offset, high - low,
                                     approximate || source.approximate, interesting});
            // A whole unpacked-array port binds at the array key, while an
            // exact element link uses its indexed key. Keep the coarse array
            // dependency so an aggregate port driver is not disconnected
            // when one of its elements is copied into a scalar signal.
            if (source.expr && source.expr->kind == slang::ast::ExpressionKind::ElementSelect) {
                const auto& select = source.expr->as<slang::ast::ElementSelectExpression>();
                if (!select.value().type->isIntegral()) {
                    std::vector<std::string> refs;
                    collectDependencyKeys(select.value(), scopePath, refs);
                    for (const auto& ref : refs) {
                        if (ref != target.wire->baseKey)
                            proceduralDependencies_.emplace_back(ref, target.wire->baseKey);
                    }
                }
            }
            emitted = true;
        }
    }
    return emitted;
}

ConnectionGraph ConnectionExtractor::extract() {
    graph_ = ConnectionGraph{};
    graph_.topModule = topModule_;
    netMap_.clear();
    rangeBindings_.clear();
    bitFlowLinks_.clear();
    netAliases_.clear();
    approximateAliases_.clear();
    proceduralDependencies_.clear();

    auto& root = compilation_.getRoot();

    // Find the requested top-level instance
    const slang::ast::InstanceSymbol* topInst = nullptr;
    for (auto inst : root.topInstances) {
        if (inst->name == topModule_) {
            topInst = inst;
            break;
        }
    }

    if (!topInst)
        return graph_;

    // Round 37: emit the TOP module's own ports into allPorts so the
    // ConventionChecker can validate top-level port naming. Child
    // instance ports are added by processChildInstance during the
    // descent. Without this loop, a single-module top is invisible
    // to convention checks.
    std::string topPath(topInst->name);
    for (auto* port_member : topInst->body.getPortList()) {
        if (!port_member) continue;
        if (port_member->kind != slang::ast::SymbolKind::Port &&
            port_member->kind != slang::ast::SymbolKind::InterfacePort)
            continue;
        PortInfo pinfo;
        pinfo.instancePath = topPath;
        pinfo.portName = std::string(port_member->name);
        pinfo.location = port_member->location;
        if (port_member->kind == slang::ast::SymbolKind::Port) {
            auto& port = port_member->as<slang::ast::PortSymbol>();
            auto& portType = port.getType();
            pinfo.direction = port.direction;
            pinfo.width = portType.getBitWidth();
            pinfo.isSigned = portType.isSigned();
        } else {
            pinfo.direction = slang::ast::ArgumentDirection::InOut;
            pinfo.width = 0;
            pinfo.isSigned = false;
        }
        graph_.allPorts.push_back(pinfo);
    }

    visitInstance(*topInst, topPath);
    resolveConnections();

    // Round 39 US-39C/US-39D: syntax-tree scan for patterns that are
    // normalised away during elaboration (wildcard `.*` port connections
    // and bare integer literals without explicit width specifiers).
    // Pass topModule_ so the scanner restricts itself to the module
    // hierarchy rooted at the requested top, ignoring sibling modules.
    StyleSyntaxScanner::scan(compilation_, topModule_, graph_);

    return graph_;
}

void ConnectionExtractor::visitInstance(const slang::ast::InstanceSymbol& instance,
                                        const std::string& parentPath) {
    // Respect maxDepth: instanceDepth is 0-based for top
    if (maxDepth_ >= 0 && static_cast<int>(instance.instanceDepth) > maxDepth_)
        return;

    if (captureDeclarations_)
        graph_.modules.push_back(
            {parentPath, std::string(instance.getDefinition().name), instance.getDefinition().location});

    // Round 39 US-39B: save and reset the per-module _q/_d buckets so
    // each module instance has an isolated view. After visitScope
    // returns we compute the difference and emit MissingDSuffix
    // observations, then restore the caller's buckets.
    auto saved_q = std::move(registered_q_bases_);
    auto saved_d = std::move(combinational_d_bases_);
    bool saved_comb = has_comb_context_;
    auto saved_arrayFacts = std::move(arrayFacts_);
    registered_q_bases_.clear();
    combinational_d_bases_.clear();
    has_comb_context_ = false;
    arrayFacts_.clear();

    visitScope(instance.body, parentPath);

    // Synthesizability latch-inference verdict (primary rule). Flag an
    // unpacked array that is (a) variable-index partial-written in a
    // clocked block, (b) NOT given a full next-state in that block, and
    // (c) read combinationally -> DC infers a latch/memory (ELAB-978).
    // The full-write discriminator keeps a proper RAM (registered read,
    // no comb read) and an explicit default-hold+overwrite register file
    // clean.
    for (const auto& [arrName, fact] : arrayFacts_) {
        if (fact.clockedVarIdxPartialWrite && fact.combRead && !fact.clockedFullWrite) {
            SynthRisk risk;
            risk.kind = SynthRisk::Kind::RegfileLatchInference;
            risk.scopePath = parentPath;
            risk.signal = arrName;
            risk.isError = true;
            risk.location = fact.partialWriteLoc;
            risk.lineNumber = fact.partialWriteLine;
            risk.columnNumber = fact.partialWriteCol;
            if (!risk.location.valid())
                populateLineColumn(risk);
            risk.detail = fmt::format("array '{}' is variable-index partial-written in a clocked block and read "
                                      "combinationally -> DC infers a latch/memory (ELAB-978); drive the full "
                                      "next-state explicitly (default-hold + overwrite) or use a proper RAM macro.",
                                      arrName);
            graph_.synthRisks.push_back(std::move(risk));
        }
    }

    // Emit one INFO per _q base that has no matching _d driver.
    // Conservative skip: only emit when the module has at least one
    // always_comb or continuous assign (has_comb_context_). Purely
    // registered modules (FSM, pipeline with no comb block) are
    // skipped to avoid false positives.
    if (!registered_q_bases_.empty() && has_comb_context_) {
        for (const auto& base : registered_q_bases_) {
            if (combinational_d_bases_.count(base) == 0) {
                StyleObservation obs;
                obs.kind = StyleObservation::Kind::MissingDSuffix;
                obs.scopePath = parentPath;
                obs.name = base + "_q";
                obs.location = instance.location;
                populateLineColumn(obs);
                obs.detail = fmt::format(
                    "always_ff register '{}' has no matching combinational "
                    "input '{}' (lowRISC requires `<base>_d` -> `<base>_q` "
                    "pairing)",
                    base + "_q", base + "_d");
                graph_.styleObservations.push_back(std::move(obs));
            }
        }
    }

    registered_q_bases_ = std::move(saved_q);
    combinational_d_bases_ = std::move(saved_d);
    has_comb_context_ = saved_comb;
    arrayFacts_ = std::move(saved_arrayFacts);
}

void ConnectionExtractor::visitScope(const slang::ast::Scope& scope,
                                      const std::string& scopePath) {
    for (auto& member : scope.members()) {
        if (captureDeclarations_ &&
            (member.kind == slang::ast::SymbolKind::Net || member.kind == slang::ast::SymbolKind::Variable))
            graph_.signals.push_back({scopePath, std::string(member.name), member.location});
        switch (member.kind) {
            case slang::ast::SymbolKind::Instance:
                processChildInstance(member.as<slang::ast::InstanceSymbol>(), scopePath);
                break;
            case slang::ast::SymbolKind::ContinuousAssign:
                processContinuousAssign(member.as<slang::ast::ContinuousAssignSymbol>(), scopePath);
                break;
            case slang::ast::SymbolKind::ProceduralBlock:
                processProceduralBlock(member.as<slang::ast::ProceduralBlockSymbol>(), scopePath);
                break;
            case slang::ast::SymbolKind::Parameter: {
                // Round 38 US-38D: capture name + location for the
                // ConventionChecker's parameter_case_pattern regex.
                auto& p = member.as<slang::ast::ParameterSymbol>();
                DeclarationCapture cap;
                cap.scopePath = scopePath;
                cap.name = std::string(p.name);
                cap.location = p.location;
                graph_.parameters.push_back(std::move(cap));
                break;
            }
            case slang::ast::SymbolKind::TypeAlias: {
                // Round 38 US-38E: capture typedef declarations for
                // the typedef_suffix_pattern regex.
                DeclarationCapture cap;
                cap.scopePath = scopePath;
                cap.name = std::string(member.name);
                cap.location = member.location;
                graph_.typedefs.push_back(std::move(cap));
                break;
            }
            case slang::ast::SymbolKind::Variable:
            case slang::ast::SymbolKind::Net: {
                // Round 38 US-38B: detect anonymous enums.
                // An enum declared without a typedef binds the
                // EnumType directly to the variable; lowRISC requires
                // every enum to be named via typedef so the type can
                // be referenced explicitly.
                auto& vs = member.as<slang::ast::ValueSymbol>();
                auto& t = vs.getType();
                if (t.kind == slang::ast::SymbolKind::EnumType &&
                    t.name.empty()) {
                    StyleObservation obs;
                    obs.kind = StyleObservation::Kind::AnonymousEnum;
                    obs.scopePath = scopePath;
                    obs.name = std::string(vs.name);
                    obs.location = vs.location;
                    populateLineColumn(obs);
                    obs.detail = fmt::format(
                        "anonymous enum bound to '{}' (lowRISC requires "
                        "`typedef enum {{...}} <name>_e;` first)",
                        std::string(vs.name));
                    graph_.styleObservations.push_back(std::move(obs));
                }
                // Round 38 US-38I: lowRISC requires `logic` (4-state)
                // for RTL signals; reject `bit`, `int`, `byte` etc.
                // 2-state scalar/integer types. Walk through packed-
                // array wrappers to reach the element scalar type.
                {
                    static const char* kBanned[] = {
                        "bit", "int", "shortint", "longint", "byte",
                        "integer", "real", "shortreal", "time"
                    };
                    auto check_type =
                        [&](const slang::ast::Type& ty) -> const char* {
                            const slang::ast::Type* p = &ty.getCanonicalType();
                            // Drill through array wrappers to the
                            // scalar element type.
                            for (int depth = 0; depth < 4; ++depth) {
                                if (p->kind == slang::ast::SymbolKind::PackedArrayType ||
                                    p->kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
                                    p = &p->getArrayElementType()
                                            ->getCanonicalType();
                                    continue;
                                }
                                break;
                            }
                            for (const char* b : kBanned) {
                                if (p->name == b) return b;
                            }
                            return nullptr;
                        };
                    if (const char* banned = check_type(t)) {
                        StyleObservation obs;
                        obs.kind = StyleObservation::Kind::BannedStateType;
                        obs.scopePath = scopePath;
                        obs.name = std::string(vs.name);
                        obs.location = vs.location;
                        populateLineColumn(obs);
                        obs.detail = fmt::format(
                            "variable '{}' uses 2-state/non-logic type "
                            "'{}' (lowRISC requires `logic` for RTL)",
                            std::string(vs.name), banned);
                        graph_.styleObservations.push_back(std::move(obs));
                    }
                }
                break;
            }
            case slang::ast::SymbolKind::GenerateBlock: {
                auto& genBlock = member.as<slang::ast::GenerateBlockSymbol>();
                if (genBlock.isUninstantiated)
                    break;
                // Round 38 US-38C: lowRISC requires explicit
                // generate-block names. Slang auto-synthesizes
                // "genblk<N>" when the user omits the `: name`
                // label; flag those as a style observation.
                if (genBlock.name.empty() ||
                    (genBlock.name.starts_with("genblk") &&
                     std::all_of(genBlock.name.begin() +
                                     std::string_view("genblk").size(),
                                 genBlock.name.end(),
                                 [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))) {
                    StyleObservation obs;
                    obs.kind = StyleObservation::Kind::UnnamedGenerateBlock;
                    obs.scopePath = scopePath;
                    obs.name = std::string(genBlock.name);
                    obs.location = genBlock.location;
                    populateLineColumn(obs);
                    obs.detail = fmt::format(
                        "generate block at '{}' lacks an explicit `: name` "
                        "label (lowRISC requires lower_snake_case names)",
                        scopePath);
                    graph_.styleObservations.push_back(std::move(obs));
                }
                // Standalone generate blocks (if/case) — include name only if non-empty
                std::string blockScope = scopePath;
                if (!genBlock.name.empty())
                    blockScope = scopePath + "." + std::string(genBlock.name);
                visitScope(genBlock, blockScope);
                break;
            }
            case slang::ast::SymbolKind::GenerateBlockArray: {
                // Generate-for: each element is a GenerateBlock with an arrayIndex
                auto& genArray = member.as<slang::ast::GenerateBlockArraySymbol>();
                std::string arrayName(genArray.name);
                // Round 38 US-38C: generate-for array also needs an
                // explicit `: name` label. Slang synthesizes
                // "genblk<N>" when omitted.
                if (arrayName.empty() ||
                    (arrayName.starts_with("genblk") &&
                     std::all_of(arrayName.begin() +
                                     std::string_view("genblk").size(),
                                 arrayName.end(),
                                 [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))) {
                    StyleObservation obs;
                    obs.kind = StyleObservation::Kind::UnnamedGenerateBlock;
                    obs.scopePath = scopePath;
                    obs.name = arrayName;
                    obs.location = genArray.location;
                    populateLineColumn(obs);
                    obs.detail = fmt::format(
                        "generate-for array at '{}' lacks an explicit `: name` "
                        "label (lowRISC requires lower_snake_case names)",
                        scopePath);
                    graph_.styleObservations.push_back(std::move(obs));
                }
                for (auto& elem : genArray.members()) {
                    if (elem.kind == slang::ast::SymbolKind::GenerateBlock) {
                        auto& block = elem.as<slang::ast::GenerateBlockSymbol>();
                        if (block.isUninstantiated)
                            continue;
                        // Build indexed scope: parent.genblk[N]
                        std::string idxStr = block.arrayIndex
                            ? block.arrayIndex->toString()
                            : std::to_string(block.constructIndex);
                        std::string blockScope = scopePath + "." + arrayName + "[" + idxStr + "]";
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

void ConnectionExtractor::processChildInstance(const slang::ast::InstanceSymbol& childInst,
                                                const std::string& scopePath) {
    std::string childPath = scopePath + "." + std::string(childInst.name);
    if (captureDeclarations_)
        graph_.instances.push_back({scopePath, std::string(childInst.name), childInst.location});

    // Process port connections for the child instance
    auto portConns = childInst.getPortConnections();
    for (auto* conn : portConns) {
        auto& portSym = conn->port;
        if (portSym.kind != slang::ast::SymbolKind::Port &&
            portSym.kind != slang::ast::SymbolKind::InterfacePort)
            continue;

        PortInfo pinfo;
        pinfo.instancePath = childPath;
        pinfo.portName = std::string(portSym.name);
        pinfo.location = portSym.location;

        if (portSym.kind == slang::ast::SymbolKind::Port) {
            auto& port = portSym.as<slang::ast::PortSymbol>();
            auto& portType = port.getType();
            pinfo.direction = port.direction;
            pinfo.width = portType.getBitWidth();
            pinfo.isSigned = portType.isSigned();
        } else {
            pinfo.direction = inferInterfaceDirection(*conn);
            pinfo.width = 0;
            pinfo.isSigned = false;
        }

        // Always add the bundle-level port to allPorts
        graph_.allPorts.push_back(pinfo);

        // For interface ports with modports, also emit per-signal port entries
        if (portSym.kind == slang::ast::SymbolKind::InterfacePort) {
            const auto [ifaceSym, modport] = conn->getIfaceConn();
            if (modport) {
                // Get the interface instance name from the connection expression
                const slang::ast::Expression* ifaceExpr = conn->getExpression();
                std::string ifaceInstName;
                if (ifaceExpr) {
                    auto ifaceResolved = resolveExpr(ifaceExpr);
                    if (!ifaceResolved.netNames.empty())
                        ifaceInstName = ifaceResolved.netNames.front();
                }

                for (const auto& member : modport->members()) {
                    if (member.kind != slang::ast::SymbolKind::ModportPort)
                        continue;

                    const auto& modportPort = member.as<slang::ast::ModportPortSymbol>();

                    PortInfo signalPort;
                    signalPort.instancePath = childPath;
                    signalPort.portName = std::string(portSym.name) + "." +
                                          std::string(modportPort.name);
                    signalPort.direction = modportPort.direction;
                    signalPort.location = portSym.location;
                    signalPort.width = 0;
                    signalPort.isSigned = false;

                    // Try to get width from the internal symbol's type
                    if (modportPort.internalSymbol) {
                        auto& internalType = modportPort.internalSymbol->as<slang::ast::ValueSymbol>().getType();
                        signalPort.width = internalType.getBitWidth();
                        signalPort.isSigned = internalType.isSigned();
                    }

                    graph_.allPorts.push_back(signalPort);
                    graph_.connectedPorts.insert(signalPort.fullPath());

                    // Map to per-signal net key: scopePath::ifaceInst.signalName
                    // PLUS, when modportPort.internalSymbol is known, also
                    // emit at the underlying signal's absolute hier path so
                    // a consumer-side MemberAccess(ModportPort) resolveExpr
                    // (which returns that abs path under Round 30 US-R05)
                    // pairs into the same netMap entry. Direct kind on the
                    // abs-path entry permits WidthChecker to fire when the
                    // consumer-side port width differs.
                    if (!ifaceInstName.empty()) {
                        auto emit = [&](const std::string& key, ConnectionKind k) {
                            if (signalPort.direction == slang::ast::ArgumentDirection::InOut) {
                                netMap_[key].push_back({signalPort, true, k});
                                netMap_[key].push_back({signalPort, false, k});
                            } else {
                                bool isDriver = (signalPort.direction == slang::ast::ArgumentDirection::Out);
                                netMap_[key].push_back({signalPort, isDriver, k});
                            }
                        };
                        emit(scopePath + "::" + ifaceInstName + "." +
                                 std::string(modportPort.name),
                             ConnectionKind::Approximate);
                        if (modportPort.internalSymbol) {
                            emit(modportPort.internalSymbol->getHierarchicalPath(),
                                 ConnectionKind::Direct);
                            const auto& memberType =
                                modportPort.internalSymbol->as<slang::ast::ValueSymbol>().getType();
                            if (memberType.isIntegral() && memberType.hasFixedRange() &&
                                memberType.getFixedRange().fullWidth() == signalPort.width) {
                                const auto range = memberType.getFixedRange();
                                auto bindRange = [&](bool isDriver) {
                                    rangeBindings_.push_back(
                                        {signalPort, isDriver,
                                         WireRange{modportPort.internalSymbol->getHierarchicalPath(), range.left,
                                                   range.right},
                                         signalPort.fullPath(), range.right, ConnectionKind::Direct});
                                };
                                if (signalPort.direction == slang::ast::ArgumentDirection::InOut) {
                                    bindRange(true);
                                    bindRange(false);
                                } else {
                                    bindRange(signalPort.direction == slang::ast::ArgumentDirection::Out);
                                }
                            }
                        }
                    }
                }
            } else if (ifaceSym && ifaceSym->kind == slang::ast::SymbolKind::Instance) {
                const auto& iface = ifaceSym->as<slang::ast::InstanceSymbol>();
                const auto usage = scanInterfaceUsage(childInst, iface);
                for (const auto& member : iface.body.members()) {
                    const auto it = usage.find(&member);
                    if (it == usage.end() || (!it->second.read && !it->second.written))
                        continue;

                    PortInfo signalPort;
                    signalPort.instancePath = childPath;
                    signalPort.portName = std::string(portSym.name) + "." + std::string(member.name);
                    signalPort.location = member.location;
                    signalPort.direction = it->second.read && it->second.written ? slang::ast::ArgumentDirection::InOut
                                           : it->second.written                  ? slang::ast::ArgumentDirection::Out
                                                                                 : slang::ast::ArgumentDirection::In;
                    const auto& type = member.as<slang::ast::ValueSymbol>().getType();
                    signalPort.width = type.getBitWidth();
                    signalPort.isSigned = type.isSigned();
                    graph_.allPorts.push_back(signalPort);
                    graph_.connectedPorts.insert(signalPort.fullPath());

                    if (type.isIntegral() && type.hasFixedRange() &&
                        type.getFixedRange().fullWidth() == signalPort.width) {
                        const auto declared = type.getFixedRange();
                        auto bindRange = [&](bool isDriver, int64_t left, int64_t right, ConnectionKind kind) {
                            rangeBindings_.push_back({signalPort, isDriver,
                                                      WireRange{member.getHierarchicalPath(), left, right},
                                                      signalPort.fullPath(), declared.right, kind});
                        };
                        if (it->second.unknownWrite)
                            bindRange(true, declared.left, declared.right, ConnectionKind::Approximate);
                        if (it->second.unknownRead)
                            bindRange(false, declared.left, declared.right, ConnectionKind::Approximate);
                        for (const auto& [left, right] : it->second.writeRanges)
                            bindRange(true, left, right, ConnectionKind::Direct);
                        for (const auto& [left, right] : it->second.readRanges)
                            bindRange(false, left, right, ConnectionKind::Direct);
                        for (const auto& [left, right] : it->second.approximateWriteRanges)
                            bindRange(true, left, right, ConnectionKind::Approximate);
                        for (const auto& [left, right] : it->second.approximateReadRanges)
                            bindRange(false, left, right, ConnectionKind::Approximate);
                    } else {
                        auto& bindings = netMap_[member.getHierarchicalPath()];
                        if (it->second.written)
                            bindings.push_back({signalPort, true, ConnectionKind::Approximate});
                        if (it->second.read)
                            bindings.push_back({signalPort, false, ConnectionKind::Approximate});
                    }
                }
            }
        }

        // Get the connection expression
        const slang::ast::Expression* expr = conn->getExpression();

        if (!expr || expr->kind == slang::ast::ExpressionKind::EmptyArgument) {
            // Port is unconnected -- already in allPorts, skip net mapping
            continue;
        }

        // Port has a non-empty expression — mark as connected
        graph_.connectedPorts.insert(pinfo.fullPath());

        auto resolved = resolveExpr(expr);
        if (resolved.tieOff)
            graph_.tieOffPorts.insert(pinfo.fullPath());
        if (resolved.tieOff && isConstantZero(expr))
            graph_.constantZeroTieOffPorts.insert(pinfo.fullPath());

        if (resolved.netNames.empty())
            continue;

        const ConnectionKind kind = (portSym.kind == slang::ast::SymbolKind::InterfacePort ||
                                     resolved.approximate)
            ? ConnectionKind::Approximate
            : ConnectionKind::Direct;

        for (const auto& netName : resolved.netNames) {
            std::string netKey = netKeyForExpression(*expr, netName, resolved.is_absolute, scopePath);

            if (portSym.kind == slang::ast::SymbolKind::Port && kind == ConnectionKind::Direct &&
                resolved.netNames.size() == 1) {
                if (auto wireRange = resolveWireRange(*expr, scopePath, pinfo.width)) {
                    if (pinfo.direction == slang::ast::ArgumentDirection::InOut) {
                        rangeBindings_.push_back({pinfo, true, *wireRange, netKey});
                        rangeBindings_.push_back({pinfo, false, *wireRange, netKey});
                    } else {
                        const bool isDriver = pinfo.direction == slang::ast::ArgumentDirection::Out;
                        rangeBindings_.push_back({pinfo, isDriver, *wireRange, netKey});
                    }
                }
            }

            if (pinfo.direction == slang::ast::ArgumentDirection::InOut) {
                netMap_[netKey].push_back({pinfo, true, kind});   // driver
                netMap_[netKey].push_back({pinfo, false, kind});  // load
            } else {
                bool isDriver = (pinfo.direction == slang::ast::ArgumentDirection::Out);
                netMap_[netKey].push_back({pinfo, isDriver, kind});
            }
        }
    }

    // Recurse into child instance
    visitInstance(childInst, childPath);
}

void ConnectionExtractor::processContinuousAssign(const slang::ast::ContinuousAssignSymbol& assignSym,
                                                    const std::string& scopePath) {
    auto& assignExpr = assignSym.getAssignment();
    if (assignExpr.kind != slang::ast::ExpressionKind::Assignment)
        return;

    auto& assign = assignExpr.as<slang::ast::AssignmentExpression>();
    // Round 39 US-39B: any continuous assign means this module has
    // combinational logic context -- set flag before early returns.
    has_comb_context_ = true;
    // Synthesizability: a continuous assign is combinational; mark any
    // unpacked-array element-select read on its RHS (run before the
    // element-select early-return below).
    collectArrayCombReads(&assign.right());
    const bool hasConcat = assign.left().kind == slang::ast::ExpressionKind::Concatenation ||
                           assign.right().kind == slang::ast::ExpressionKind::Concatenation;
    const bool mappedBitFlow = recordBitFlow(assign.left(), assign.right(), scopePath, false);
    if (hasConcat)
        return;
    auto lhs = resolveExpr(&assign.left());
    auto rhs = resolveExpr(&assign.right());
    if (lhs.approximate || rhs.approximate ||
        lhs.netNames.size() != 1 || rhs.netNames.size() != 1)
        return;

    std::string lhsKey = netKeyForExpression(assign.left(), lhs.netNames.front(), lhs.is_absolute, scopePath);
    std::string rhsKey = netKeyForExpression(assign.right(), rhs.netNames.front(), rhs.is_absolute, scopePath);

    if (lhsKey == rhsKey)
        return;

    // Round 39 US-39B: collect _d-suffixed LHS names from continuous
    // assigns for the registered-output pairing check.  Round 39
    // review: shared with the always_comb path via collectDBaseFromLeaf.
    {
        const std::string& lhs_leaf = lhs.netNames.front();
        size_t br = lhs_leaf.find('[');
        std::string_view leaf =
            (br != std::string::npos) ? std::string_view(lhs_leaf).substr(0, br) : std::string_view(lhs_leaf);
        collectDBaseFromLeaf(leaf);
    }

    if (hasNonIdentityConversion(assign.right())) {
        // Width- or state-changing casts are not whole-net aliases. Exact mapped bits
        // already have bit-flow links; unsupported forms keep only a coarse
        // may-dependency rather than inventing a direct full-width wire.
        if (!mappedBitFlow) {
            std::vector<std::string> refs;
            collectDependencyKeys(assign.right(), scopePath, refs);
            for (const auto& ref : refs) {
                if (ref != lhsKey)
                    proceduralDependencies_.emplace_back(ref, lhsKey);
            }
        }
        return;
    }
    recordAlias(lhsKey, rhsKey, false);
}

void ConnectionExtractor::processProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                                                 const std::string& scopePath) {
    if (block.procedureKind != slang::ast::ProceduralBlockKind::Always &&
        block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysComb &&
        block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF) {
        return;
    }

    // Synthesizability latch-inference scan. Classify this block as
    // clocked (always_ff, or a legacy `always` whose timing has an
    // edge), combinational (always_comb / always @(*) / always @(a or
    // b)), or neither. Clocked blocks contribute array-write facts;
    // combinational blocks contribute array-read facts. The verdict is
    // computed per array symbol at the end of visitInstance.
    bool combinational;
    {
        using slang::ast::ProceduralBlockKind;
        using slang::ast::TimingControlKind;
        bool isClocked = block.procedureKind == ProceduralBlockKind::AlwaysFF;
        bool isComb = block.procedureKind == ProceduralBlockKind::AlwaysComb;
        if (block.procedureKind == ProceduralBlockKind::Always) {
            // Inspect the wrapping timing control to tell a clocked
            // `always @(posedge clk)` from a combinational `always @(*)`.
            const slang::ast::Statement* body = &block.getBody();
            while (body && body->kind == slang::ast::StatementKind::Block)
                body = &body->as<slang::ast::BlockStatement>().body;
            if (body && body->kind == slang::ast::StatementKind::Timed) {
                const auto& tc = body->as<slang::ast::TimedStatement>().timing;
                auto hasEdge = [](const slang::ast::TimingControl& t) {
                    if (t.kind == TimingControlKind::SignalEvent)
                        return t.as<slang::ast::SignalEventControl>().edge != slang::ast::EdgeKind::None;
                    if (t.kind == TimingControlKind::EventList) {
                        for (const auto* e : t.as<slang::ast::EventListControl>().events) {
                            if (e && e->kind == TimingControlKind::SignalEvent &&
                                e->as<slang::ast::SignalEventControl>().edge != slang::ast::EdgeKind::None)
                                return true;
                        }
                    }
                    return false;
                };
                if (hasEdge(tc))
                    isClocked = true;
                else
                    isComb = true; // level-sensitive list / @(*) -> combinational
            }
        }
        if (isClocked) {
            std::unordered_set<const slang::ast::Symbol*> loopVars;
            scanClockedArrayWrites(block.getBody(), loopVars, /*inResetBranch=*/false);
        } else if (isComb) {
            // Mark array element-select reads on assignment RHS sides
            // throughout the combinational body.
            collectArrayCombReadsInStatement(block.getBody());
        }
        combinational = isComb;
    }

    // Round 38 US-38A: lowRISC requires `always_ff` for sequential
    // and `always_comb` for combinational; the legacy `always @*` /
    // `always @(posedge clk)` form is discouraged because synthesis
    // cannot statically verify the intent. Record a style
    // observation so ConventionChecker can emit an INFO entry.
    if (block.procedureKind == slang::ast::ProceduralBlockKind::Always) {
        StyleObservation obs;
        obs.kind = StyleObservation::Kind::LegacyAlwaysBlock;
        obs.scopePath = scopePath;
        obs.location = block.location;
        populateLineColumn(obs);
        obs.detail = "legacy `always` block (use `always_ff` for "
                     "sequential or `always_comb` for combinational)";
        graph_.styleObservations.push_back(std::move(obs));
    }
    // Round 38 US-38F: lowRISC requires registered outputs (LHS of
    // non-blocking assignments inside always_ff) to end with `_q`
    // (single-stage) or `_q<digits>` (pipeline stages, e.g. `_q2`).
    // Walk the body's assignments only when this is an always_ff
    // block; comb / legacy always blocks have their own conventions.
    if (block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
        std::function<void(const slang::ast::Statement&)> walk =
            [&](const slang::ast::Statement& s) {
                using SK = slang::ast::StatementKind;
                switch (s.kind) {
                    case SK::ExpressionStatement: {
                        auto& es = s.as<slang::ast::ExpressionStatement>();
                        if (es.expr.kind != slang::ast::ExpressionKind::Assignment)
                            return;
                        auto& a = es.expr.as<slang::ast::AssignmentExpression>();
                        if (!a.isNonBlocking())
                            return;
                        // Resolve LHS to a leaf name.
                        auto resolved = resolveExpr(&a.left());
                        if (resolved.netNames.empty())
                            return;
                        std::string lhs_name = resolved.netNames.front();
                        // Strip any [bit:select] / [range] for the
                        // suffix check.
                        size_t br = lhs_name.find('[');
                        std::string base = (br != std::string::npos)
                            ? lhs_name.substr(0, br)
                            : lhs_name;
                        // Round 38 architect feedback: skip
                        // hierarchical writes (`state_q.field`,
                        // `inst.signal`, modport member assignments).
                        // The `_q` rule is for internal registered
                        // local variables only; struct fields and
                        // submodule signals each have their own
                        // naming conventions and are NOT subject to
                        // the registered-output suffix.
                        if (base.find('.') != std::string::npos)
                            return;
                        std::string leaf = base;
                        // Anchor `_q` at end of leaf (R1 MAJOR): the
                        // previous rfind("_q") matched mid-string, so a
                        // comb signal like `data_qual_next` was flagged
                        // because `_q` inside `_qual` was found and the
                        // tail digit-check failed.  Now only accept the
                        // exact patterns `_q$` or `_q[0-9]+$`.
                        bool ok = false;
                        if (leaf.ends_with("_q")) {
                            ok = true;
                        } else if (leaf.size() >= 3) {
                            // Walk back from end while digits, then
                            // require the preceding two chars to be `_q`.
                            size_t i = leaf.size();
                            while (i > 0 && std::isdigit(static_cast<unsigned char>(leaf[i - 1])))
                                --i;
                            if (i < leaf.size() && i >= 2 && leaf[i - 2] == '_' && leaf[i - 1] == 'q') {
                                ok = true; // matches `_q[0-9]+$`
                            }
                        }
                        // Suppress noise for compiler-generated temps
                        // (slang prepends some such; treat empty leaf
                        // as already-bad and skip silently).
                        if (!ok && !leaf.empty()) {
                            StyleObservation obs;
                            obs.kind = StyleObservation::Kind::MissingQSuffix;
                            obs.scopePath = scopePath;
                            obs.name = leaf;
                            obs.location = a.left().sourceRange.start();
                            populateLineColumn(obs);
                            obs.detail = fmt::format(
                                "always_ff non-blocking LHS '{}' lacks `_q` "
                                "(or `_q<n>`) suffix (lowRISC registered-output "
                                "convention)",
                                leaf);
                            graph_.styleObservations.push_back(std::move(obs));
                        }
                        // Round 39 US-39B: collect base name of _q-suffixed
                        // registers for the d-suffix pairing check.
                        // Only collect when the name ends exactly with `_q`
                        // (single-stage); pipeline stages like `valid_q2`
                        // don't require a `valid_d` counterpart.
                        if (ok && leaf.size() >= 2 && leaf.ends_with("_q")) {
                            // Ends exactly with `_q` -- base is prefix.
                            registered_q_bases_.insert(leaf.substr(0, leaf.size() - 2));
                        }
                        return;
                    }
                    case SK::Block: {
                        auto& b = s.as<slang::ast::BlockStatement>();
                        walk(b.body);
                        return;
                    }
                    case SK::List: {
                        auto& l = s.as<slang::ast::StatementList>();
                        for (auto* c : l.list) if (c) walk(*c);
                        return;
                    }
                    case SK::Conditional: {
                        auto& c = s.as<slang::ast::ConditionalStatement>();
                        walk(c.ifTrue);
                        if (c.ifFalse) walk(*c.ifFalse);
                        return;
                    }
                    case SK::Timed: {
                        auto& t = s.as<slang::ast::TimedStatement>();
                        walk(t.stmt);
                        return;
                    }
                    case SK::Case: {
                        auto& cs = s.as<slang::ast::CaseStatement>();
                        for (const auto& g : cs.items)
                            if (g.stmt) walk(*g.stmt);
                        if (cs.defaultCase) walk(*cs.defaultCase);
                        return;
                    }
                    default:
                        return;
                }
            };
        walk(block.getBody());

        // Round 39 US-39A: lowRISC reset-polarity check.
        // Detect two violations for always_ff blocks:
        //   1. Comma-syntax sensitivity list: @(posedge clk, negedge rst)
        //      instead of the required @(posedge clk or negedge rst).
        //   2. Active-high reset: posedge on a signal whose name matches
        //      the active-high naming pattern (^rst_p or _rst_p).
        // Walk the syntax tree of the ProceduralBlockSyntax to inspect
        // the original source tokens (the AST loses the comma/or
        // distinction after elaboration).
        if (auto* blockSyn = block.getSyntax()) {
            using namespace slang::syntax;
            using slang::parsing::TokenKind;

            // The procedural-block syntax has a `statement` field which
            // for always_ff is a TimingControlStatement wrapping the @(...).
            auto& pbSyn = blockSyn->as<ProceduralBlockSyntax>();
            if (pbSyn.statement->kind == SyntaxKind::TimingControlStatement) {
                auto& tcStmt =
                    pbSyn.statement->as<TimingControlStatementSyntax>();
                auto& tcRef = *tcStmt.timingControl;

                if (tcRef.kind == SyntaxKind::EventControlWithExpression) {
                    auto& ecSyn =
                        tcRef.as<EventControlWithExpressionSyntax>();

                    // Walk the EventExpressionSyntax tree to:
                    //   a) detect any BinaryEventExpression whose separator
                    //      is a comma (instead of `or`)
                    //   b) collect all SignalEventExpressions so we can
                    //      check for active-high reset signals
                    bool hasCommaSeparator = false;

                    std::function<void(const EventExpressionSyntax&)> walkExpr =
                        [&](const EventExpressionSyntax& e) {
                            if (e.kind == SyntaxKind::ParenthesizedEventExpression) {
                                // @(posedge clk, negedge rst) wraps the
                                // event expression in parens at the syntax
                                // level. Unwrap and recurse into the inner
                                // event expression.
                                walkExpr(
                                    *e.as<ParenthesizedEventExpressionSyntax>()
                                        .expr);
                                return;
                            }
                            if (e.kind == SyntaxKind::BinaryEventExpression) {
                                auto& bin =
                                    e.as<BinaryEventExpressionSyntax>();
                                if (bin.operatorToken.kind == TokenKind::Comma)
                                    hasCommaSeparator = true;
                                walkExpr(*bin.left);
                                walkExpr(*bin.right);
                            } else if (e.kind ==
                                       SyntaxKind::SignalEventExpression) {
                                auto& sig =
                                    e.as<SignalEventExpressionSyntax>();
                                // Active-high reset: posedge on a signal
                                // whose name starts with `rst_p` or contains
                                // `_rst_p` (lowRISC active-high naming
                                // convention). Only flag PosEdge; negedge
                                // rst_n is the correct lowRISC form.
                                if (sig.edge.kind ==
                                    TokenKind::PosEdgeKeyword) {
                                    // Extract the signal name from the
                                    // expression text using the raw syntax
                                    // toString() for name matching.
                                    std::string sigText =
                                        sig.expr->toString();
                                    // Trim whitespace
                                    sigText.erase(
                                        sigText.begin(),
                                        std::find_if(sigText.begin(),
                                            sigText.end(),
                                            [](unsigned char c) {
                                                return !std::isspace(c);
                                            }));
                                    sigText.erase(
                                        std::find_if(sigText.rbegin(),
                                            sigText.rend(),
                                            [](unsigned char c) {
                                                return !std::isspace(c);
                                            }).base(),
                                        sigText.end());
                                    // Round 39 review: drive active-high
                                    // reset detection off the canonical
                                    // lowRISC active-low convention (signal
                                    // ends in `_n` / `_ni`) rather than a
                                    // hardcoded `rst_p` substring.  Any
                                    // posedge on a reset-named signal that
                                    // is NOT marked active-low is suspect.
                                    // This false-positive-fixes
                                    // `posedge rst_pulse` / `rst_pin` and
                                    // false-negative-fixes a bare `rst`.
                                    auto looks_like_reset = [](const std::string& s) {
                                        return s == "rst" || s.starts_with("rst_") || s.ends_with("_rst") ||
                                               s.find("_rst_") != std::string::npos || s == "reset" ||
                                               s.starts_with("reset_") || s.ends_with("_reset") ||
                                               s.find("_reset_") != std::string::npos;
                                    };
                                    auto is_active_low_named = [](const std::string& s) {
                                        return s.ends_with("_n") || s.ends_with("_ni");
                                    };
                                    // R1 MAJOR: strip trailing bracket
                                    // index (`rst_n[i]`, `rst_ni[0]`)
                                    // before suffix tests.  The previous
                                    // ends_with("_n") check failed on the
                                    // bracketed form, so a bracket-indexed
                                    // active-low reset was misclassified
                                    // as active-high.
                                    auto stripIndex = [](std::string s) {
                                        if (auto b = s.find('['); b != std::string::npos)
                                            s.resize(b);
                                        return s;
                                    };
                                    const auto bare = stripIndex(sigText);
                                    bool activeHigh = looks_like_reset(bare) && !is_active_low_named(bare);
                                    if (activeHigh) {
                                        StyleObservation obs;
                                        obs.kind = StyleObservation::Kind::
                                            ResetPolarityBad;
                                        obs.scopePath = scopePath;
                                        obs.name = sigText;
                                        obs.location = block.location;
                                        populateLineColumn(obs);
                                        obs.detail = fmt::format(
                                            "always_ff uses active-high reset "
                                            "'{}' (posedge) -- lowRISC "
                                            "requires active-low negedge "
                                            "rst_n*",
                                            sigText);
                                        graph_.styleObservations.push_back(
                                            std::move(obs));
                                    }
                                }
                            }
                        };

                    walkExpr(*ecSyn.expr);

                    if (hasCommaSeparator) {
                        StyleObservation obs;
                        obs.kind = StyleObservation::Kind::ResetPolarityBad;
                        obs.scopePath = scopePath;
                        obs.location = block.location;
                        populateLineColumn(obs);
                        obs.detail =
                            "always_ff sensitivity list uses comma syntax "
                            "`@(posedge clk, negedge rst)` -- lowRISC "
                            "requires `or` keyword: "
                            "`@(posedge clk or negedge rst)`";
                        graph_.styleObservations.push_back(std::move(obs));
                    }
                }
            }
        }
    }
    // Round 39 US-39B: collect _d-suffixed LHS names from always_comb
    // blocks for the registered-output pairing check. Walk blocking
    // assignments only (always_comb uses blocking); non-blocking in
    // comb context is already a separate style violation.
    if (block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysComb) {
        has_comb_context_ = true;
        std::function<void(const slang::ast::Statement&)> walk_comb =
            [&](const slang::ast::Statement& s) {
                using SK = slang::ast::StatementKind;
                switch (s.kind) {
                    case SK::ExpressionStatement: {
                        auto& es = s.as<slang::ast::ExpressionStatement>();
                        if (es.expr.kind != slang::ast::ExpressionKind::Assignment)
                            return;
                        auto& a = es.expr.as<slang::ast::AssignmentExpression>();
                        if (a.isNonBlocking())
                            return;
                        auto resolved = resolveExpr(&a.left());
                        if (resolved.netNames.empty())
                            return;
                        std::string lhs_name = resolved.netNames.front();
                        size_t br = lhs_name.find('[');
                        std::string_view leaf = (br != std::string::npos) ? std::string_view(lhs_name).substr(0, br)
                                                                          : std::string_view(lhs_name);
                        collectDBaseFromLeaf(leaf);
                        return;
                    }
                    case SK::Block: {
                        auto& b = s.as<slang::ast::BlockStatement>();
                        walk_comb(b.body);
                        return;
                    }
                    case SK::List: {
                        auto& l = s.as<slang::ast::StatementList>();
                        for (auto* c : l.list) if (c) walk_comb(*c);
                        return;
                    }
                    case SK::Conditional: {
                        auto& c = s.as<slang::ast::ConditionalStatement>();
                        walk_comb(c.ifTrue);
                        if (c.ifFalse) walk_comb(*c.ifFalse);
                        return;
                    }
                    case SK::Timed: {
                        auto& t = s.as<slang::ast::TimedStatement>();
                        walk_comb(t.stmt);
                        return;
                    }
                    case SK::Case: {
                        auto& cs = s.as<slang::ast::CaseStatement>();
                        for (const auto& g : cs.items)
                            if (g.stmt) walk_comb(*g.stmt);
                        if (cs.defaultCase) walk_comb(*cs.defaultCase);
                        return;
                    }
                    default:
                        return;
                }
            };
        walk_comb(block.getBody());

        scanIncompleteCombAssignments(block, scopePath);
    }

    processProceduralStatement(block.getBody(), block, scopePath, combinational,
                               block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysComb &&
                                   isSingleUnconditionalAssignment(block.getBody()));
}

void ConnectionExtractor::scanIncompleteCombAssignments(const slang::ast::ProceduralBlockSymbol& block,
                                                        const std::string& scopePath) {
    using SK = slang::ast::StatementKind;
    // Secondary rule: in always_comb, a simple signal assigned only inside
    // an incomplete branch structure (an `if` with no `else`, or a `case`
    // with no `default`) and never given an unconditional default value
    // holds its previous value when no branch is taken -> the synthesizer
    // infers a latch. We track three sets and flag the conservative
    // intersection so the common default-then-override idiom (a signal
    // assigned unconditionally first, then overridden in a branch) stays
    // clean. WARN severity (non-fatal).
    std::unordered_set<std::string> assignedUnconditional;   // covered by a default
    std::unordered_set<std::string> assignedUnderIncomplete; // under if-no-else / case-no-default
    std::unordered_set<std::string> assignedInElseOrDefault; // in a covering branch
    std::unordered_map<std::string, slang::SourceLocation> firstIncompleteLoc;

    auto leafOf = [&](const slang::ast::Expression& lhs) -> std::string {
        auto resolved = resolveExpr(&lhs);
        if (resolved.netNames.empty())
            return {};
        std::string name = resolved.netNames.front();
        // Skip hierarchical / member writes and bit/element selects: only
        // whole simple-signal assignments participate in the latch rule.
        if (name.find('.') != std::string::npos || name.find('[') != std::string::npos)
            return {};
        return name;
    };

    // depth flags: underAnyConditional (inside any if/case), underIncomplete
    // (inside an if-without-else or case-without-default branch).
    std::function<void(const slang::ast::Statement&, bool, bool, bool)> walk =
        [&](const slang::ast::Statement& s, bool underAnyCond, bool underIncomplete, bool underElseOrDefault) {
            switch (s.kind) {
            case SK::ExpressionStatement: {
                auto& es = s.as<slang::ast::ExpressionStatement>();
                if (es.expr.kind != slang::ast::ExpressionKind::Assignment)
                    return;
                auto& a = es.expr.as<slang::ast::AssignmentExpression>();
                if (a.isNonBlocking())
                    return;
                std::string leaf = leafOf(a.left());
                if (leaf.empty())
                    return;
                if (!underAnyCond)
                    assignedUnconditional.insert(leaf);
                if (underIncomplete) {
                    assignedUnderIncomplete.insert(leaf);
                    firstIncompleteLoc.emplace(leaf, a.left().sourceRange.start());
                }
                if (underElseOrDefault)
                    assignedInElseOrDefault.insert(leaf);
                return;
            }
            case SK::Block:
                walk(s.as<slang::ast::BlockStatement>().body, underAnyCond, underIncomplete, underElseOrDefault);
                return;
            case SK::List:
                for (auto* c : s.as<slang::ast::StatementList>().list)
                    if (c)
                        walk(*c, underAnyCond, underIncomplete, underElseOrDefault);
                return;
            case SK::Timed:
                walk(s.as<slang::ast::TimedStatement>().stmt, underAnyCond, underIncomplete, underElseOrDefault);
                return;
            case SK::Conditional: {
                auto& c = s.as<slang::ast::ConditionalStatement>();
                bool hasElse = c.ifFalse != nullptr;
                // The if-true branch is "incomplete" when there is no
                // else; the else branch (if present) is a covering path.
                walk(c.ifTrue, true, underIncomplete || !hasElse, underElseOrDefault);
                if (c.ifFalse)
                    walk(*c.ifFalse, true, underIncomplete, /*underElseOrDefault=*/true);
                return;
            }
            case SK::Case: {
                auto& cs = s.as<slang::ast::CaseStatement>();
                bool hasDefault = cs.defaultCase != nullptr;
                for (const auto& g : cs.items)
                    if (g.stmt)
                        walk(*g.stmt, true, underIncomplete || !hasDefault, underElseOrDefault);
                if (cs.defaultCase)
                    walk(*cs.defaultCase, true, underIncomplete, /*underElseOrDefault=*/true);
                return;
            }
            default:
                return;
            }
        };
    walk(block.getBody(), false, false, false);

    for (const auto& leaf : assignedUnderIncomplete) {
        // Conservative: only flag when there is no unconditional default
        // and no assignment in any covering else/default branch. This
        // keeps default-then-override and fully-branched if/else clean.
        if (assignedUnconditional.count(leaf) || assignedInElseOrDefault.count(leaf))
            continue;
        SynthRisk risk;
        risk.kind = SynthRisk::Kind::IncompleteCombAssignment;
        risk.scopePath = scopePath;
        risk.signal = leaf;
        risk.isError = false; // WARN (non-fatal)
        auto it = firstIncompleteLoc.find(leaf);
        if (it != firstIncompleteLoc.end())
            risk.location = it->second;
        else
            risk.location = block.location;
        populateLineColumn(risk);
        risk.detail =
            fmt::format("signal '{}' is assigned in some but not all branches of an always_comb "
                        "if/case with no else/default and then read -> incomplete assignment may infer a latch.",
                        leaf);
        graph_.synthRisks.push_back(std::move(risk));
    }
}

void ConnectionExtractor::collectDependencyKeys(const slang::ast::Expression& expr, const std::string& scopePath,
                                                std::vector<std::string>& keys) {
    auto add = [&](const slang::ast::Expression& ref) {
        const auto resolved = resolveExpr(&ref);
        for (const auto& name : resolved.netNames) {
            const std::string key = netKeyForExpression(ref, name, resolved.is_absolute, scopePath);
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
        }
    };
    const auto selected = resolveExpr(&expr);
    if (std::any_of(selected.netNames.begin(), selected.netNames.end(),
                    [](const std::string& name) { return hasDynamicIndex(name); }))
        add(expr);
    auto visitor = slang::ast::makeVisitor(
        [&](auto&, const slang::ast::NamedValueExpression& ref) {
            if (ref.symbol.kind != slang::ast::SymbolKind::Parameter &&
                ref.symbol.kind != slang::ast::SymbolKind::TypeParameter &&
                ref.symbol.kind != slang::ast::SymbolKind::EnumValue)
                add(ref);
        },
        [&](auto&, const slang::ast::HierarchicalValueExpression& ref) { add(ref); },
        [&](auto&, const slang::ast::ArbitrarySymbolExpression& ref) { add(ref); },
        [&](auto&, const slang::ast::MemberAccessExpression& ref) { add(ref); });
    expr.visit(visitor);
}

void ConnectionExtractor::processProceduralStatement(const slang::ast::Statement& stmt,
                                                     const slang::ast::ProceduralBlockSymbol& owner,
                                                     const std::string& scopePath, bool combinational,
                                                     bool exactSingleAssignment, std::vector<std::string> guardKeys) {
    using SK = slang::ast::StatementKind;

    switch (stmt.kind) {
        case SK::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind != slang::ast::ExpressionKind::Assignment)
                return;

            auto& assign = expr.as<slang::ast::AssignmentExpression>();
            if (combinational) {
                const bool hasConcat = assign.left().kind == slang::ast::ExpressionKind::Concatenation ||
                                       assign.right().kind == slang::ast::ExpressionKind::Concatenation;
                const auto lhsWidth = assign.left().type ? assign.left().type->getBitWidth() : 0;
                const auto rhsWidth = assign.right().type ? assign.right().type->getBitWidth() : 0;
                const bool positionalCandidate =
                    assign.isBlocking() && !assign.isCompound() && !assign.isLValueArg() && !assign.timingControl &&
                    lhsWidth > 0 && lhsWidth == rhsWidth && !hasNonIdentityConversion(assign.left()) &&
                    !hasNonIdentityConversion(assign.right()) && resolveWireRange(assign.left(), scopePath, lhsWidth) &&
                    resolveWireRange(assign.right(), scopePath, rhsWidth);
                const bool directCandidate = exactSingleAssignment && positionalCandidate;
                bool mappedPositional = false;
                if (positionalCandidate)
                    mappedPositional = recordBitFlow(assign.left(), assign.right(), scopePath, !directCandidate, true);
                else
                    recordBitFlow(assign.left(), assign.right(), scopePath, true);
                if (mappedPositional && directCandidate)
                    return;
                if (hasConcat) {
                    std::vector<std::string> targets;
                    collectDependencyKeys(assign.left(), scopePath, targets);
                    for (const auto& guard : guardKeys) {
                        for (const auto& dest : targets) {
                            if (guard != dest)
                                proceduralDependencies_.emplace_back(guard, dest);
                        }
                    }
                    return;
                }
            }
            auto lhs = resolveExpr(&assign.left());
            if (combinational) {
                if (lhs.tieOff || lhs.netNames.size() != 1)
                    return;
                const std::string dest =
                    netKeyForExpression(assign.left(), lhs.netNames.front(), lhs.is_absolute, scopePath);
                // Keep the coarse dependency for transitive paths that the
                // bounded bit-flow graph cannot follow through later casts.
                collectDependencyKeys(assign.right(), scopePath, guardKeys);
                for (const auto& source : guardKeys) {
                    if (source != dest)
                        proceduralDependencies_.emplace_back(source, dest);
                }
                return;
            }
            auto rhs = resolveExpr(&assign.right());
            if (lhs.tieOff || rhs.tieOff ||
                lhs.netNames.size() != 1 || rhs.netNames.size() != 1)
                return;

            recordAlias(netKeyForExpression(assign.left(), lhs.netNames.front(), lhs.is_absolute, scopePath),
                        netKeyForExpression(assign.right(), rhs.netNames.front(), rhs.is_absolute, scopePath), true);
            return;
        }
        case SK::Timed: {
            auto& timed = stmt.as<slang::ast::TimedStatement>();
            processProceduralStatement(timed.stmt, owner, scopePath, combinational, exactSingleAssignment, guardKeys);
            return;
        }
        case SK::Block: {
            auto& block = stmt.as<slang::ast::BlockStatement>();
            processProceduralStatement(block.body, owner, scopePath, combinational, exactSingleAssignment, guardKeys);
            return;
        }
        case SK::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            for (auto* child : list.list) {
                if (child)
                    processProceduralStatement(*child, owner, scopePath, combinational, exactSingleAssignment,
                                               guardKeys);
            }
            return;
        }
        case SK::Conditional: {
            auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            if (const auto known = knownIfBranch(cond)) {
                if (*known)
                    processProceduralStatement(cond.ifTrue, owner, scopePath, combinational, exactSingleAssignment,
                                               guardKeys);
                else if (cond.ifFalse)
                    processProceduralStatement(*cond.ifFalse, owner, scopePath, combinational, exactSingleAssignment,
                                               guardKeys);
                return;
            }
            if (combinational) {
                for (const auto& condition : cond.conditions)
                    collectDependencyKeys(*condition.expr, scopePath, guardKeys);
            }
            processProceduralStatement(cond.ifTrue, owner, scopePath, combinational, exactSingleAssignment, guardKeys);
            if (cond.ifFalse)
                processProceduralStatement(*cond.ifFalse, owner, scopePath, combinational, exactSingleAssignment,
                                           guardKeys);
            return;
        }
        case SK::Case: {
            auto& cs = stmt.as<slang::ast::CaseStatement>();
            // Round 38 US-38G: lowRISC requires `unique case` (or
            // `priority case`) and a mandatory `default:` branch.
            // Bare `case` without a check is flagged; missing default
            // is flagged separately.
            if (cs.check == slang::ast::UniquePriorityCheck::None) {
                StyleObservation obs;
                obs.kind = StyleObservation::Kind::MissingUniqueCase;
                obs.scopePath = scopePath;
                obs.location = cs.sourceRange.start();
                populateLineColumn(obs);
                obs.detail = "case statement lacks `unique`/`priority` "
                             "qualifier (lowRISC requires `unique case`)";
                graph_.styleObservations.push_back(std::move(obs));
            }
            if (!cs.defaultCase) {
                StyleObservation obs;
                obs.kind = StyleObservation::Kind::MissingCaseDefault;
                obs.scopePath = scopePath;
                obs.location = cs.sourceRange.start();
                populateLineColumn(obs);
                obs.detail = "case statement lacks `default:` branch "
                             "(lowRISC requires it for synthesis safety)";
                graph_.styleObservations.push_back(std::move(obs));
            }
            if (const auto known = knownCaseBranch(cs, owner)) {
                if (*known)
                    processProceduralStatement(**known, owner, scopePath, combinational, exactSingleAssignment,
                                               guardKeys);
                return;
            }
            // Recurse into each branch's body.
            if (combinational)
                collectDependencyKeys(cs.expr, scopePath, guardKeys);
            for (const auto& g : cs.items) {
                if (g.stmt) {
                    auto itemKeys = guardKeys;
                    if (combinational) {
                        for (const auto* itemExpr : g.expressions)
                            collectDependencyKeys(*itemExpr, scopePath, itemKeys);
                    }
                    processProceduralStatement(*g.stmt, owner, scopePath, combinational, exactSingleAssignment,
                                               itemKeys);
                }
            }
            if (cs.defaultCase)
                processProceduralStatement(*cs.defaultCase, owner, scopePath, combinational, exactSingleAssignment,
                                           guardKeys);
            return;
        }
        default:
            return;
    }
}

void ConnectionExtractor::recordAlias(const std::string& lhsKey,
                                      const std::string& rhsKey,
                                      bool approximate) {
    std::string lhsCanon = findCanonical(lhsKey);
    std::string rhsCanon = findCanonical(rhsKey);
    if (lhsCanon == rhsCanon) {
        if (approximate)
            approximateAliases_.insert(lhsCanon);
        return;
    }

    netAliases_[rhsCanon] = lhsCanon;
    if (approximate || approximateAliases_.count(lhsCanon) || approximateAliases_.count(rhsCanon))
        approximateAliases_.insert(lhsCanon);
    approximateAliases_.erase(rhsCanon);
}

std::string ConnectionExtractor::findCanonical(const std::string& key) {
    std::string current = key;
    std::vector<std::string> path;
    size_t maxIter = netAliases_.size() + 1;
    size_t iter = 0;
    while (netAliases_.count(current) && iter++ < maxIter) {
        path.push_back(current);
        current = netAliases_.at(current);
    }
    // Path compression: point all visited nodes directly to root
    for (const auto& p : path)
        netAliases_[p] = current;
    return current;
}

void ConnectionExtractor::resolveConnections() {
    // Group all bindings by their canonical net key
    std::unordered_map<std::string, std::vector<NetBinding*>> canonicalGroups;
    for (auto& [netKey, bindings] : netMap_) {
        std::string canon = findCanonical(netKey);
        for (auto& b : bindings)
            canonicalGroups[canon].push_back(&b);
    }

    std::vector<std::string> boundKeys;
    boundKeys.reserve(netMap_.size());
    for (const auto& [key, _] : netMap_)
        boundKeys.push_back(key);
    std::sort(boundKeys.begin(), boundKeys.end());
    std::unordered_map<std::string, std::vector<std::string>> dynamicMatches;
    auto matchingBoundKeys = [&](const std::string& pattern) -> const std::vector<std::string>& {
        auto [entry, inserted] = dynamicMatches.try_emplace(pattern);
        if (!inserted || !hasDynamicIndex(pattern))
            return entry->second;
        auto& matches = entry->second;
        const std::string prefix = pattern.substr(0, pattern.find("[?]"));
        for (auto it = std::lower_bound(boundKeys.begin(), boundKeys.end(), prefix);
             it != boundKeys.end() && it->starts_with(prefix); ++it) {
            if (!hasDynamicIndex(*it) && dynamicPathMayOverlap(pattern, *it))
                matches.push_back(*it);
        }
        const auto scopeEnd = prefix.rfind("::");
        const size_t signalStart = scopeEnd == std::string::npos ? 0 : scopeEnd + 2;
        for (size_t pos = prefix.size(); pos > signalStart; --pos) {
            if (prefix[pos - 1] != '[' && prefix[pos - 1] != '.')
                continue;
            const std::string ancestor = prefix.substr(0, pos - 1);
            if (std::binary_search(boundKeys.begin(), boundKeys.end(), ancestor) &&
                dynamicPathMayOverlap(pattern, ancestor))
                matches.push_back(ancestor);
        }
        std::sort(matches.begin(), matches.end());
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
        return matches;
    };

    std::unordered_map<std::string, std::vector<std::string>> dependencySources;
    for (const auto& [source, dest] : proceduralDependencies_) {
        std::vector<std::string> sources{source};
        std::vector<std::string> destinations{dest};
        if (hasDynamicIndex(source)) {
            const auto& matches = matchingBoundKeys(source);
            sources.insert(sources.end(), matches.begin(), matches.end());
        }
        if (hasDynamicIndex(dest)) {
            const auto& matches = matchingBoundKeys(dest);
            destinations.insert(destinations.end(), matches.begin(), matches.end());
        }
        for (const auto& expandedDest : destinations) {
            const auto destCanon = findCanonical(expandedDest);
            for (const auto& expandedSource : sources) {
                const auto sourceCanon = findCanonical(expandedSource);
                if (sourceCanon != destCanon)
                    dependencySources[destCanon].push_back(sourceCanon);
            }
        }
    }

    for (auto& [canon, bindings] : canonicalGroups) {
        // Collect drivers and loads
        std::vector<const NetBinding*> drivers;
        std::vector<const NetBinding*> loads;

        for (auto* binding : bindings) {
            if (binding->isDriver) {
                drivers.push_back(binding);
            } else {
                loads.push_back(binding);
            }
        }

        // Create connections: each driver connects to each load
        for (auto* driver : drivers) {
            for (auto* load : loads) {
                Connection conn;
                conn.source = driver->port;
                conn.dest = load->port;
                if (driver->kind == ConnectionKind::Approximate ||
                    load->kind == ConnectionKind::Approximate ||
                    approximateAliases_.count(canon)) {
                    conn.kind = ConnectionKind::Approximate;
                }
                graph_.connections.push_back(conn);
            }
        }

        // Follow combinational data dependencies toward this net's loads.
        // Each source is visited once so reconvergent mux branches and loops
        // do not duplicate edges or turn input nets into aliases.
        std::unordered_set<std::string> visited{canon};
        std::vector<std::string> pending = dependencySources[canon];
        while (!pending.empty()) {
            std::string source = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(source).second)
                continue;
            if (auto group = canonicalGroups.find(source); group != canonicalGroups.end()) {
                for (auto* driver : group->second) {
                    if (!driver->isDriver)
                        continue;
                    for (auto* load : loads)
                        graph_.connections.push_back({driver->port, load->port, ConnectionKind::Approximate});
                }
            }
            if (auto sources = dependencySources.find(source); sources != dependencySources.end())
                pending.insert(pending.end(), sources->second.begin(), sources->second.end());
        }
    }

    // A runtime selector may name any connected element in its declaration
    // scope. Pair only keys that agree on fixed indices and member names;
    // keep the result approximate because the chosen lane is data-dependent.
    std::set<std::pair<std::string, std::string>> approximatePairs;
    for (const auto& conn : graph_.connections) {
        if (conn.kind == ConnectionKind::Approximate)
            approximatePairs.emplace(conn.source.fullPath(), conn.dest.fullPath());
    }
    auto appendDynamic = [&](const NetBinding* driver, const NetBinding* load) {
        if (!driver || !load || driver->port.fullPath() == load->port.fullPath())
            return;
        const auto pair = std::pair{driver->port.fullPath(), load->port.fullPath()};
        if (approximatePairs.insert(pair).second)
            graph_.connections.push_back({driver->port, load->port, ConnectionKind::Approximate});
    };
    for (const auto& dynamicKey : boundKeys) {
        if (!hasDynamicIndex(dynamicKey))
            continue;
        const auto dynamicGroup = canonicalGroups.find(findCanonical(dynamicKey));
        if (dynamicGroup == canonicalGroups.end())
            continue;
        for (const auto& concreteKey : matchingBoundKeys(dynamicKey)) {
            const auto concreteGroup = canonicalGroups.find(findCanonical(concreteKey));
            if (concreteGroup == canonicalGroups.end() || concreteGroup == dynamicGroup)
                continue;
            for (const auto* dynamicBinding : dynamicGroup->second) {
                for (const auto* concreteBinding : concreteGroup->second) {
                    if (dynamicBinding->isDriver && !concreteBinding->isDriver)
                        appendDynamic(dynamicBinding, concreteBinding);
                    if (concreteBinding->isDriver && !dynamicBinding->isDriver)
                        appendDynamic(concreteBinding, dynamicBinding);
                }
            }
        }
    }

    // Exact constant slices of the same declared vector can overlap without
    // sharing an identical textual key (`bus` versus `bus[3:0]`). Preserve
    // the overlapped ordinal bits of each port. Complex expressions and
    // width-changing connections never enter rangeBindings_.
    std::unordered_map<std::string, std::vector<const RangeBinding*>> rangesByNet;
    for (const auto& binding : rangeBindings_)
        rangesByNet[binding.wire.baseKey].push_back(&binding);
    for (const auto& [_, bindings] : rangesByNet) {
        for (const auto* driver : bindings) {
            if (!driver->isDriver)
                continue;
            for (const auto* load : bindings) {
                if (load->isDriver || driver->originalKey == load->originalKey ||
                    driver->port.fullPath() == load->port.fullPath())
                    continue;
                const int64_t low = std::max(std::min(driver->wire.left, driver->wire.right),
                                             std::min(load->wire.left, load->wire.right));
                const int64_t high = std::min(std::max(driver->wire.left, driver->wire.right),
                                              std::max(load->wire.left, load->wire.right));
                if (low > high)
                    continue;
                auto portBits = [low, high](const RangeBinding& binding) {
                    const auto right = binding.portRight.value_or(binding.wire.right);
                    const auto first = std::abs(low - right);
                    const auto last = std::abs(high - right);
                    return BitRange{std::min(first, last), std::max(first, last)};
                };
                Connection conn;
                conn.source = driver->port;
                conn.dest = load->port;
                conn.kind = driver->kind == ConnectionKind::Approximate || load->kind == ConnectionKind::Approximate
                                ? ConnectionKind::Approximate
                                : ConnectionKind::Direct;
                conn.sourceBits = portBits(*driver);
                conn.destBits = portBits(*load);
                graph_.connections.push_back(std::move(conn));
            }
        }
    }

    struct BitNode {
        std::string key;
        int64_t bit = 0;
        bool operator==(const BitNode&) const = default;
    };
    struct BitNodeHash {
        size_t operator()(const BitNode& node) const {
            return std::hash<std::string>{}(node.key) ^ (std::hash<int64_t>{}(node.bit) << 1);
        }
    };
    struct BitEdge {
        BitNode dest;
        bool approximate = false;
        bool interesting = false;
    };
    std::unordered_map<BitNode, std::vector<BitEdge>, BitNodeHash> bitEdges;
    auto wireBit = [](const WireRange& range, uint32_t offset) {
        return range.right + (range.left >= range.right ? static_cast<int64_t>(offset) : -static_cast<int64_t>(offset));
    };
    for (const auto& link : bitFlowLinks_) {
        for (uint32_t i = 0; i < link.width; ++i) {
            BitNode source{link.source.baseKey, wireBit(link.source, link.sourceOffset + i)};
            BitNode dest{link.dest.baseKey, wireBit(link.dest, link.destOffset + i)};
            bitEdges[std::move(source)].push_back({std::move(dest), link.approximate, link.interesting});
        }
    }

    struct BitConnection {
        const RangeBinding* source = nullptr;
        const RangeBinding* dest = nullptr;
        int64_t sourceBit = 0;
        int64_t destBit = 0;
        bool approximate = false;
    };
    std::vector<BitConnection> bitConnections;
    auto covers = [](const RangeBinding& binding, int64_t bit) {
        return bit >= std::min(binding.wire.left, binding.wire.right) &&
               bit <= std::max(binding.wire.left, binding.wire.right);
    };
    for (const auto& [start, edges] : bitEdges) {
        auto drivers = rangesByNet.find(start.key);
        if (drivers == rangesByNet.end())
            continue;
        for (const auto* driver : drivers->second) {
            if (!driver->isDriver || !covers(*driver, start.bit))
                continue;
            const int64_t sourceBit = std::abs(start.bit - driver->portRight.value_or(driver->wire.right));
            struct State {
                BitNode node;
                bool approximate = false;
                bool interesting = false;
            };
            std::vector<State> pending;
            for (const auto& edge : edges)
                pending.push_back({edge.dest, edge.approximate, edge.interesting});
            std::unordered_map<BitNode, uint8_t, BitNodeHash> visited;
            while (!pending.empty()) {
                State state = std::move(pending.back());
                pending.pop_back();
                const uint8_t flag = static_cast<uint8_t>(
                    1u << (static_cast<unsigned>(state.approximate) * 2u + static_cast<unsigned>(state.interesting)));
                if (visited[state.node] & flag)
                    continue;
                visited[state.node] |= flag;
                if (state.interesting) {
                    if (auto loads = rangesByNet.find(state.node.key); loads != rangesByNet.end()) {
                        for (const auto* load : loads->second) {
                            if (load->isDriver || !covers(*load, state.node.bit) ||
                                driver->port.fullPath() == load->port.fullPath())
                                continue;
                            bitConnections.push_back(
                                {driver, load, sourceBit,
                                 std::abs(state.node.bit - load->portRight.value_or(load->wire.right)),
                                 state.approximate || driver->kind == ConnectionKind::Approximate ||
                                     load->kind == ConnectionKind::Approximate});
                        }
                    }
                }
                if (auto next = bitEdges.find(state.node); next != bitEdges.end()) {
                    for (const auto& edge : next->second) {
                        pending.push_back(
                            {edge.dest, state.approximate || edge.approximate, state.interesting || edge.interesting});
                    }
                }
            }
        }
    }

    auto sortKey = [](const BitConnection& connection) {
        return std::tuple{connection.source->port.fullPath(), connection.dest->port.fullPath(), connection.approximate,
                          connection.sourceBit, connection.destBit};
    };
    std::sort(bitConnections.begin(), bitConnections.end(),
              [&](const auto& a, const auto& b) { return sortKey(a) < sortKey(b); });
    bitConnections.erase(std::unique(bitConnections.begin(), bitConnections.end(),
                                     [&](const auto& a, const auto& b) { return sortKey(a) == sortKey(b); }),
                         bitConnections.end());
    for (size_t i = 0; i < bitConnections.size();) {
        size_t end = i + 1;
        while (end < bitConnections.size() &&
               bitConnections[end].source->port.fullPath() == bitConnections[i].source->port.fullPath() &&
               bitConnections[end].dest->port.fullPath() == bitConnections[i].dest->port.fullPath() &&
               bitConnections[end].approximate == bitConnections[i].approximate &&
               bitConnections[end].sourceBit == bitConnections[end - 1].sourceBit + 1 &&
               bitConnections[end].destBit == bitConnections[end - 1].destBit + 1)
            ++end;
        Connection conn;
        conn.source = bitConnections[i].source->port;
        conn.dest = bitConnections[i].dest->port;
        conn.kind = bitConnections[i].approximate ? ConnectionKind::Approximate : ConnectionKind::Direct;
        conn.sourceBits = BitRange{bitConnections[i].sourceBit, bitConnections[end - 1].sourceBit};
        conn.destBits = BitRange{bitConnections[i].destBit, bitConnections[end - 1].destBit};
        graph_.connections.push_back(std::move(conn));
        i = end;
    }

    // A full-width exact row subsumes a coarse direct row for the same
    // endpoints. Keep partial exact rows and approximate rows: they can
    // describe additional flow not covered by the precise mapping.
    std::set<std::pair<std::string, std::string>> fullyMappedPairs;
    for (const auto& conn : graph_.connections) {
        if (conn.kind != ConnectionKind::Direct || !conn.sourceBits || !conn.destBits || conn.source.width == 0 ||
            conn.dest.width == 0)
            continue;
        if (conn.sourceBits->low == 0 && conn.sourceBits->high == static_cast<int64_t>(conn.source.width) - 1 &&
            conn.destBits->low == 0 && conn.destBits->high == static_cast<int64_t>(conn.dest.width) - 1)
            fullyMappedPairs.emplace(conn.source.fullPath(), conn.dest.fullPath());
    }
    std::erase_if(graph_.connections, [&](const Connection& conn) {
        return conn.kind == ConnectionKind::Direct && !conn.sourceBits && !conn.destBits &&
               fullyMappedPairs.contains({conn.source.fullPath(), conn.dest.fullPath()});
    });
}

void ConnectionExtractor::populateLineColumn(StyleObservation& obs) const {
    if (!obs.location.valid())
        return;
    const auto* sm = compilation_.getSourceManager();
    if (!sm)
        return;
    obs.lineNumber = static_cast<uint32_t>(sm->getLineNumber(obs.location));
    obs.columnNumber = static_cast<uint32_t>(sm->getColumnNumber(obs.location));
}

void ConnectionExtractor::collectDBaseFromLeaf(std::string_view leaf) {
    // Leaf must be a simple (non-hierarchical) name that ends in `_d`
    // and has at least one character before the `_d` so the base is
    // non-empty.  `_d` alone, `foo._d`, and `_d`-less names are skipped.
    if (leaf.find('.') != std::string_view::npos)
        return;
    if (leaf.size() <= 2 || !leaf.ends_with("_d"))
        return;
    combinational_d_bases_.insert(std::string(leaf.substr(0, leaf.size() - 2)));
}

void ConnectionExtractor::populateLineColumn(SynthRisk& risk) const {
    if (!risk.location.valid())
        return;
    const auto* sm = compilation_.getSourceManager();
    if (!sm)
        return;
    risk.lineNumber = static_cast<uint32_t>(sm->getLineNumber(risk.location));
    risk.columnNumber = static_cast<uint32_t>(sm->getColumnNumber(risk.location));
}

void ConnectionExtractor::collectArrayCombReads(const slang::ast::Expression* expr) {
    if (!expr)
        return;
    // Any unpacked-array element-select read anywhere in this
    // combinational expression marks the array as combinationally read.
    // makeVisitor walks the full sub-expression tree so reads nested in
    // operators, conversions, concatenations, etc. are all covered.
    auto visitor = slang::ast::makeVisitor([&](auto& self, const slang::ast::ElementSelectExpression& sel) {
        std::string rootName;
        bool hasNonConstIndex = false;
        if (resolveArrayElementSelect(&sel, rootName, hasNonConstIndex))
            arrayFacts_[rootName].combRead = true;
        self.visitDefault(sel);
    });
    expr->visit(visitor);
}

void ConnectionExtractor::collectArrayCombReadsInStatement(const slang::ast::Statement& stmt) {
    using SK = slang::ast::StatementKind;
    switch (stmt.kind) {
    case SK::ExpressionStatement: {
        auto& es = stmt.as<slang::ast::ExpressionStatement>();
        if (es.expr.kind != slang::ast::ExpressionKind::Assignment)
            return;
        // Reads live on the RHS; LHS element-select indices are also
        // read positions but for the latch rule only the RHS array
        // fan-out matters.
        collectArrayCombReads(&es.expr.as<slang::ast::AssignmentExpression>().right());
        return;
    }
    case SK::Block:
        collectArrayCombReadsInStatement(stmt.as<slang::ast::BlockStatement>().body);
        return;
    case SK::List:
        for (auto* c : stmt.as<slang::ast::StatementList>().list)
            if (c)
                collectArrayCombReadsInStatement(*c);
        return;
    case SK::Conditional: {
        auto& c = stmt.as<slang::ast::ConditionalStatement>();
        collectArrayCombReadsInStatement(c.ifTrue);
        if (c.ifFalse)
            collectArrayCombReadsInStatement(*c.ifFalse);
        return;
    }
    case SK::Case: {
        auto& cs = stmt.as<slang::ast::CaseStatement>();
        for (const auto& g : cs.items)
            if (g.stmt)
                collectArrayCombReadsInStatement(*g.stmt);
        if (cs.defaultCase)
            collectArrayCombReadsInStatement(*cs.defaultCase);
        return;
    }
    case SK::Timed:
        collectArrayCombReadsInStatement(stmt.as<slang::ast::TimedStatement>().stmt);
        return;
    case SK::ForLoop:
        collectArrayCombReadsInStatement(stmt.as<slang::ast::ForLoopStatement>().body);
        return;
    default:
        return;
    }
}

void ConnectionExtractor::scanClockedArrayWrites(const slang::ast::Statement& stmt,
                                                 std::unordered_set<const slang::ast::Symbol*>& loopVars,
                                                 bool inResetBranch) {
    using SK = slang::ast::StatementKind;
    switch (stmt.kind) {
    case SK::ExpressionStatement: {
        // Writes inside the async-reset clear branch are the reset
        // preset, not the functional next-state -> ignore entirely.
        if (inResetBranch)
            return;
        auto& es = stmt.as<slang::ast::ExpressionStatement>();
        if (es.expr.kind != slang::ast::ExpressionKind::Assignment)
            return;
        auto& a = es.expr.as<slang::ast::AssignmentExpression>();
        std::string rootName;
        bool hasNonConstIndex = false;
        std::vector<const slang::ast::Expression*> nonConstSelectors;
        if (resolveArrayElementSelect(&a.left(), rootName, hasNonConstIndex, &nonConstSelectors)) {
            auto& fact = arrayFacts_[rootName];
            if (!hasNonConstIndex) {
                // Fully constant element write: a fixed single
                // element, neither establishes nor clears the
                // variable-address memory pattern.
                return;
            }
            // A variable-index write whose every non-constant
            // selector is composed solely of loop induction variables
            // sweeps the whole declared extent -> full next-state.
            // Any selector referencing a data signal (register/port)
            // addresses a single data-dependent element -> partial.
            bool allLoopVarOnly = true;
            for (const auto* sel : nonConstSelectors) {
                if (!selectorUsesOnlyLoopVars(*sel, loopVars)) {
                    allLoopVarOnly = false;
                    break;
                }
            }
            if (allLoopVarOnly) {
                fact.clockedFullWrite = true;
            } else if (!fact.clockedVarIdxPartialWrite) {
                fact.clockedVarIdxPartialWrite = true;
                fact.partialWriteLoc = a.left().sourceRange.start();
                const auto* sm = compilation_.getSourceManager();
                if (sm && fact.partialWriteLoc.valid()) {
                    fact.partialWriteLine = static_cast<uint32_t>(sm->getLineNumber(fact.partialWriteLoc));
                    fact.partialWriteCol = static_cast<uint32_t>(sm->getColumnNumber(fact.partialWriteLoc));
                }
            }
            return;
        }
        // Whole-array assignment `arr <= expr` (no element select):
        // a full next-state, clears the latch risk.
        if (a.left().kind == slang::ast::ExpressionKind::NamedValue) {
            auto& named = a.left().as<slang::ast::NamedValueExpression>();
            if (named.symbol.getType().isUnpackedArray())
                arrayFacts_[std::string(named.symbol.name)].clockedFullWrite = true;
        }
        return;
    }
    case SK::Block:
        scanClockedArrayWrites(stmt.as<slang::ast::BlockStatement>().body, loopVars, inResetBranch);
        return;
    case SK::List:
        for (auto* c : stmt.as<slang::ast::StatementList>().list)
            if (c)
                scanClockedArrayWrites(*c, loopVars, inResetBranch);
        return;
    case SK::Conditional: {
        auto& c = stmt.as<slang::ast::ConditionalStatement>();
        // `if (!rst_n) <reset> else <normal>`: the true branch is the
        // async-reset clear path. Mark it so its writes are ignored
        // while the else branch carries the functional next-state.
        bool resetCond = !inResetBranch && conditionReferencesReset(c.conditions);
        scanClockedArrayWrites(c.ifTrue, loopVars, inResetBranch || resetCond);
        if (c.ifFalse)
            scanClockedArrayWrites(*c.ifFalse, loopVars, inResetBranch);
        return;
    }
    case SK::Case: {
        auto& cs = stmt.as<slang::ast::CaseStatement>();
        for (const auto& g : cs.items)
            if (g.stmt)
                scanClockedArrayWrites(*g.stmt, loopVars, inResetBranch);
        if (cs.defaultCase)
            scanClockedArrayWrites(*cs.defaultCase, loopVars, inResetBranch);
        return;
    }
    case SK::Timed:
        scanClockedArrayWrites(stmt.as<slang::ast::TimedStatement>().stmt, loopVars, inResetBranch);
        return;
    case SK::ForLoop: {
        // Bring the loop's induction variables into scope for the
        // body so arr[y][x] sweeps register-file writes are seen as
        // full-extent. Remove them again on the way out.
        auto& fl = stmt.as<slang::ast::ForLoopStatement>();
        std::vector<const slang::ast::Symbol*> added;
        for (const auto* lv : fl.loopVars) {
            if (lv && loopVars.insert(lv).second)
                added.push_back(lv);
        }
        scanClockedArrayWrites(fl.body, loopVars, inResetBranch);
        for (const auto* lv : added)
            loopVars.erase(lv);
        return;
    }
    default:
        return;
    }
}

} // namespace connect
