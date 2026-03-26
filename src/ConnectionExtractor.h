#pragma once

#include "ConnectionGraph.h"
#include "slang/ast/Compilation.h"

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
    void visitInstance(const slang::ast::InstanceSymbol& instance,
                       const std::string& parentPath);

    void resolveConnections();

    static std::string extractNetName(const slang::ast::Expression* expr);

    slang::ast::Compilation& compilation_;
    std::string topModule_;
    int maxDepth_;
    ConnectionGraph graph_;

    std::string findCanonical(const std::string& key) const;

    // net_key (scope + net_name) -> list of (PortInfo, isDriver)
    struct NetBinding {
        PortInfo port;
        bool isDriver;
    };
    std::unordered_map<std::string, std::vector<NetBinding>> netMap_;

    // net alias map: key -> parent key (union-find without path compression)
    mutable std::unordered_map<std::string, std::string> netAliases_;
};

} // namespace connect
