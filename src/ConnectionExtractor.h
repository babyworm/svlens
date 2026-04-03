#pragma once

#include "ConnectionGraph.h"
#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/MemberSymbols.h>

#include <string>
#include <unordered_map>
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

    void resolveConnections();

    static ResolvedExpr resolveExpr(const slang::ast::Expression* expr);

    slang::ast::Compilation& compilation_;
    std::string topModule_;
    int maxDepth_;
    ConnectionGraph graph_;

    std::string findCanonical(const std::string& key) const;

    // net_key (scope + net_name) -> list of (PortInfo, isDriver)
    struct NetBinding {
        PortInfo port;
        bool isDriver;
        ConnectionKind kind = ConnectionKind::Direct;
    };
    std::unordered_map<std::string, std::vector<NetBinding>> netMap_;

    // net alias map: key -> parent key (union-find without path compression)
    mutable std::unordered_map<std::string, std::string> netAliases_;
};

} // namespace connect
