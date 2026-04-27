#include "StyleReachability.h"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/syntax/SyntaxTree.h>

#include <unordered_map>
#include <vector>

namespace connect {

std::unordered_set<std::string> collectReachableModules(const slang::ast::Compilation& compilation,
                                                        const std::string& topModule) {
    using namespace slang::syntax;

    std::unordered_set<std::string> reachable;
    if (topModule.empty())
        return reachable;

    // Build a map: module_name -> ModuleDeclarationSyntax* for quick lookup.
    std::unordered_map<std::string, const ModuleDeclarationSyntax*> defMap;
    for (const auto& tree : compilation.getSyntaxTrees()) {
        if (!tree)
            continue;
        AllSyntaxVisitor finder([&](const SyntaxNode& n) {
            if (n.kind == SyntaxKind::ModuleDeclaration) {
                const auto& mod = static_cast<const ModuleDeclarationSyntax&>(n);
                std::string name(mod.header->name.valueText());
                defMap.emplace(name, &mod);
            }
        });
        finder.visit(tree->root());
    }

    // BFS to find all transitively instantiated module names.
    std::vector<std::string> worklist{topModule};
    while (!worklist.empty()) {
        std::string cur = worklist.back();
        worklist.pop_back();
        if (reachable.count(cur))
            continue;
        reachable.insert(cur);

        auto it = defMap.find(cur);
        if (it == defMap.end())
            continue;

        // Walk the body of this module declaration looking for
        // HierarchyInstantiationSyntax nodes to collect child types.
        AllSyntaxVisitor childFinder([&](const SyntaxNode& n) {
            if (n.kind == SyntaxKind::HierarchyInstantiation) {
                const auto& hi = static_cast<const HierarchyInstantiationSyntax&>(n);
                std::string childType(hi.type.valueText());
                if (!reachable.count(childType))
                    worklist.push_back(childType);
            }
        });
        childFinder.visit(*it->second);
    }
    return reachable;
}

} // namespace connect
