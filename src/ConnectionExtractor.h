#pragma once

#include "ConnectionGraph.h"
#include <slang/ast/Compilation.h>
#include <slang/ast/Statement.h>
#include <slang/ast/symbols/MemberSymbols.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace connect {

class ConnectionExtractor {
public:
    ConnectionExtractor(slang::ast::Compilation& compilation,
                        const std::string& topModule,
                        int maxDepth = -1);

    ConnectionGraph extract();

private:
    struct ResolvedExpr {
        std::vector<std::string> netNames;
        bool approximate = false;
        bool tieOff = false;
    };

    void visitInstance(const slang::ast::InstanceSymbol& instance,
                       const std::string& parentPath);

    void visitScope(const slang::ast::Scope& scope,
                    const std::string& scopePath);

    void processChildInstance(const slang::ast::InstanceSymbol& childInst,
                              const std::string& scopePath);

    void processContinuousAssign(const slang::ast::ContinuousAssignSymbol& assignSym,
                                 const std::string& scopePath);

    void processProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                                const std::string& scopePath);

    void processProceduralStatement(const slang::ast::Statement& stmt,
                                    const std::string& scopePath);

    void resolveConnections();

    static ResolvedExpr resolveExpr(const slang::ast::Expression* expr);
    void recordAlias(const std::string& lhsKey, const std::string& rhsKey, bool approximate);

    slang::ast::Compilation& compilation_;
    std::string topModule_;
    int maxDepth_;
    ConnectionGraph graph_;

    std::string findCanonical(const std::string& key);

    // net_key (scope + net_name) -> list of (PortInfo, isDriver)
    struct NetBinding {
        PortInfo port;
        bool isDriver;
        ConnectionKind kind = ConnectionKind::Direct;
    };
    std::unordered_map<std::string, std::vector<NetBinding>> netMap_;

    // net alias map: key -> parent key (union-find without path compression)
    std::unordered_map<std::string, std::string> netAliases_;
    std::unordered_set<std::string> approximateAliases_;
};

} // namespace connect
