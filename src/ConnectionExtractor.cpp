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

} // namespace

ConnectionExtractor::ConnectionExtractor(slang::ast::Compilation& compilation,
                                         const std::string& topModule,
                                         int maxDepth)
    : compilation_(compilation), topModule_(topModule), maxDepth_(maxDepth) {}

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
        case slang::ast::ExpressionKind::ArbitrarySymbol: {
            auto& symbolExpr = expr->as<slang::ast::ArbitrarySymbolExpression>();
            result.netNames.push_back(std::string(symbolExpr.symbol->name));
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
            result = resolveExpr(&access.value());
            for (auto& name : result.netNames)
                name += "." + std::string(access.member.name);

            if (access.member.kind == slang::ast::SymbolKind::Modport ||
                access.member.kind == slang::ast::SymbolKind::ModportPort) {
                result.approximate = true;
            }
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

    visitInstance(*topInst, std::string(topInst->name));
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
        if (portSym.kind != slang::ast::SymbolKind::Port)
            continue;

        auto& port = portSym.as<slang::ast::PortSymbol>();
        auto& portType = port.getType();

        PortInfo pinfo;
        pinfo.instancePath = childPath;
        pinfo.portName = std::string(port.name);
        pinfo.direction = port.direction;
        pinfo.width = portType.getBitWidth();
        pinfo.isSigned = portType.isSigned();
        pinfo.location = port.location;

        // Always add to allPorts
        graph_.allPorts.push_back(pinfo);

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

        if (resolved.netNames.empty())
            continue;

        const ConnectionKind kind = resolved.approximate
            ? ConnectionKind::Approximate
            : ConnectionKind::Direct;

        for (const auto& netName : resolved.netNames) {
            std::string netKey = scopePath + "::" + netName;

            if (port.direction == slang::ast::ArgumentDirection::InOut) {
                netMap_[netKey].push_back({pinfo, true, kind});   // driver
                netMap_[netKey].push_back({pinfo, false, kind});  // load
            } else {
                bool isDriver = (port.direction == slang::ast::ArgumentDirection::Out);
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

    // Record alias: lhsKey and rhsKey refer to the same net
    std::string lhsCanon = findCanonical(lhsKey);
    std::string rhsCanon = findCanonical(rhsKey);
    if (lhsCanon != rhsCanon)
        netAliases_[rhsCanon] = lhsCanon;
}

std::string ConnectionExtractor::findCanonical(const std::string& key) const {
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
                    load->kind == ConnectionKind::Approximate) {
                    conn.kind = ConnectionKind::Approximate;
                }
                graph_.connections.push_back(conn);
            }
        }
    }
}

} // namespace connect
