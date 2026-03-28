#include "ConnectionExtractor.h"

#include "slang/ast/Expression.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/types/Type.h"

namespace connect {

ConnectionExtractor::ConnectionExtractor(slang::ast::Compilation& compilation,
                                         const std::string& topModule,
                                         int maxDepth)
    : compilation_(compilation), topModule_(topModule), maxDepth_(maxDepth) {}

std::string ConnectionExtractor::extractNetName(const slang::ast::Expression* expr) {
    if (!expr)
        return {};

    // Walk through conversions/assignments to find the underlying named value.
    // Output port connections appear as Assignment expressions where left() is the net.
    // Input port connections appear as direct NamedValue expressions.
    const slang::ast::Expression* current = expr;
    while (current) {
        switch (current->kind) {
            case slang::ast::ExpressionKind::NamedValue: {
                auto& named = current->as<slang::ast::NamedValueExpression>();
                return std::string(named.symbol.name);
            }
            case slang::ast::ExpressionKind::Conversion: {
                auto& conv = current->as<slang::ast::ConversionExpression>();
                current = &conv.operand();
                continue;
            }
            case slang::ast::ExpressionKind::Assignment: {
                // For output port connections, the left side is the external net
                auto& assign = current->as<slang::ast::AssignmentExpression>();
                current = &assign.left();
                continue;
            }
            case slang::ast::ExpressionKind::RangeSelect: {
                auto& sel = current->as<slang::ast::RangeSelectExpression>();
                current = &sel.value();
                continue;
            }
            case slang::ast::ExpressionKind::ElementSelect: {
                auto& sel = current->as<slang::ast::ElementSelectExpression>();
                current = &sel.value();
                continue;
            }
            case slang::ast::ExpressionKind::EmptyArgument:
                return {};
            default:
                return {};
        }
    }
    return {};
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

        std::string netName = extractNetName(expr);
        if (netName.empty())
            continue;

        // Build net key: scope path + net name
        std::string netKey = scopePath + "::" + netName;

        if (port.direction == slang::ast::ArgumentDirection::InOut) {
            netMap_[netKey].push_back({pinfo, true});   // driver
            netMap_[netKey].push_back({pinfo, false});  // load
        } else {
            bool isDriver = (port.direction == slang::ast::ArgumentDirection::Out);
            netMap_[netKey].push_back({pinfo, isDriver});
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
    std::string lhsName = extractNetName(&assign.left());
    std::string rhsName = extractNetName(&assign.right());
    if (lhsName.empty() || rhsName.empty())
        return;

    std::string lhsKey = scopePath + "::" + lhsName;
    std::string rhsKey = scopePath + "::" + rhsName;

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
    size_t maxIter = netAliases_.size() + 1;
    size_t iter = 0;
    while (netAliases_.count(current) && iter++ < maxIter)
        current = netAliases_.at(current);
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
        std::vector<const PortInfo*> drivers;
        std::vector<const PortInfo*> loads;

        for (auto* binding : bindings) {
            if (binding->isDriver) {
                drivers.push_back(&binding->port);
            } else {
                loads.push_back(&binding->port);
            }
        }

        // Create connections: each driver connects to each load
        for (auto* driver : drivers) {
            for (auto* load : loads) {
                Connection conn;
                conn.source = *driver;
                conn.dest = *load;
                graph_.connections.push_back(conn);
            }
        }
    }
}

} // namespace connect
