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
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/MiscStatements.h>
#include <slang/ast/types/Type.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/parsing/TokenKind.h>
#include <slang/text/SourceManager.h>

#include <fmt/core.h>

#include <algorithm>
#include <cctype>
#include <functional>

namespace connect {

namespace {

void appendUnique(std::vector<std::string>& dest, const std::vector<std::string>& src) {
    for (const auto& value : src) {
        if (std::find(dest.begin(), dest.end(), value) == dest.end())
            dest.push_back(value);
    }
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

} // namespace

ConnectionExtractor::ConnectionExtractor(slang::ast::Compilation& compilation,
                                         const std::string& topModule,
                                         int maxDepth)
    : compilation_(compilation), topModule_(topModule), maxDepth_(maxDepth) {}

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

ConnectionGraph ConnectionExtractor::extract() {
    graph_ = ConnectionGraph{};
    graph_.topModule = topModule_;
    netMap_.clear();
    netAliases_.clear();
    approximateAliases_.clear();

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

    // Round 39 US-39B: save and reset the per-module _q/_d buckets so
    // each module instance has an isolated view. After visitScope
    // returns we compute the difference and emit MissingDSuffix
    // observations, then restore the caller's buckets.
    auto saved_q = std::move(registered_q_bases_);
    auto saved_d = std::move(combinational_d_bases_);
    bool saved_comb = has_comb_context_;
    registered_q_bases_.clear();
    combinational_d_bases_.clear();
    has_comb_context_ = false;

    visitScope(instance.body, parentPath);

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
}

void ConnectionExtractor::visitScope(const slang::ast::Scope& scope,
                                      const std::string& scopePath) {
    for (auto& member : scope.members()) {
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
                        if (modportPort.internalSymbol)
                            emit(modportPort.internalSymbol->getHierarchicalPath(),
                                 ConnectionKind::Direct);
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
            std::string netKey = resolved.is_absolute
                ? netName
                : (scopePath + "::" + netName);

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
    auto lhs = resolveExpr(&assign.left());
    auto rhs = resolveExpr(&assign.right());
    if (lhs.approximate || rhs.approximate ||
        lhs.netNames.size() != 1 || rhs.netNames.size() != 1)
        return;

    std::string lhsKey = scopePath + "::" + lhs.netNames.front();
    std::string rhsKey = scopePath + "::" + rhs.netNames.front();

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

    recordAlias(lhsKey, rhsKey, false);
}

void ConnectionExtractor::processProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                                                 const std::string& scopePath) {
    if (block.procedureKind != slang::ast::ProceduralBlockKind::Always &&
        block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysComb &&
        block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF) {
        return;
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
                            while (i > 0 &&
                                   std::isdigit(static_cast<unsigned char>(leaf[i - 1])))
                                --i;
                            if (i < leaf.size() && i >= 2 &&
                                leaf[i - 2] == '_' && leaf[i - 1] == 'q') {
                                ok = true;  // matches `_q[0-9]+$`
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
                                    bool activeHigh = looks_like_reset(sigText) && !is_active_low_named(sigText);
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
    }

    processProceduralStatement(block.getBody(), scopePath);
}

void ConnectionExtractor::processProceduralStatement(const slang::ast::Statement& stmt,
                                                     const std::string& scopePath) {
    using SK = slang::ast::StatementKind;

    switch (stmt.kind) {
        case SK::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind != slang::ast::ExpressionKind::Assignment)
                return;

            auto& assign = expr.as<slang::ast::AssignmentExpression>();
            auto lhs = resolveExpr(&assign.left());
            auto rhs = resolveExpr(&assign.right());
            if (lhs.tieOff || rhs.tieOff ||
                lhs.netNames.size() != 1 || rhs.netNames.size() != 1)
                return;

            recordAlias(scopePath + "::" + lhs.netNames.front(),
                        scopePath + "::" + rhs.netNames.front(),
                        true);
            return;
        }
        case SK::Timed: {
            auto& timed = stmt.as<slang::ast::TimedStatement>();
            processProceduralStatement(timed.stmt, scopePath);
            return;
        }
        case SK::Block: {
            auto& block = stmt.as<slang::ast::BlockStatement>();
            processProceduralStatement(block.body, scopePath);
            return;
        }
        case SK::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            for (auto* child : list.list) {
                if (child)
                    processProceduralStatement(*child, scopePath);
            }
            return;
        }
        case SK::Conditional: {
            auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            processProceduralStatement(cond.ifTrue, scopePath);
            if (cond.ifFalse)
                processProceduralStatement(*cond.ifFalse, scopePath);
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
            // Recurse into each branch's body.
            for (const auto& g : cs.items) {
                if (g.stmt)
                    processProceduralStatement(*g.stmt, scopePath);
            }
            if (cs.defaultCase)
                processProceduralStatement(*cs.defaultCase, scopePath);
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
    }
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

} // namespace connect
