#include "ConnectionExtractor.h"

#include <slang/ast/Expression.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
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

#include <algorithm>

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
    return graph_;
}

void ConnectionExtractor::visitInstance(const slang::ast::InstanceSymbol& instance,
                                        const std::string& parentPath) {
    // Respect maxDepth: instanceDepth is 0-based for top
    if (maxDepth_ >= 0 && static_cast<int>(instance.instanceDepth) > maxDepth_)
        return;

    visitScope(instance.body, parentPath);
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
            case slang::ast::SymbolKind::GenerateBlock: {
                auto& genBlock = member.as<slang::ast::GenerateBlockSymbol>();
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
    auto lhs = resolveExpr(&assign.left());
    auto rhs = resolveExpr(&assign.right());
    if (lhs.approximate || rhs.approximate ||
        lhs.netNames.size() != 1 || rhs.netNames.size() != 1)
        return;

    std::string lhsKey = scopePath + "::" + lhs.netNames.front();
    std::string rhsKey = scopePath + "::" + rhs.netNames.front();

    if (lhsKey == rhsKey)
        return;

    recordAlias(lhsKey, rhsKey, false);
}

void ConnectionExtractor::processProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                                                 const std::string& scopePath) {
    if (block.procedureKind != slang::ast::ProceduralBlockKind::Always &&
        block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysComb &&
        block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF) {
        return;
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

} // namespace connect
