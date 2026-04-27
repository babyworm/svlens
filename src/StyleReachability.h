#pragma once

#include <slang/ast/Compilation.h>

#include <string>
#include <unordered_set>

namespace connect {

// Fresh review R2 MAJOR: shared reachable-module BFS used by both
// SourceTextScanner and StyleSyntaxScanner.  Previously each kept its
// own near-identical copy; the two had already drifted on minor
// details and would silently diverge on the next refactor.
//
// Walks all syntax trees in @p compilation, builds a map of
// ModuleDeclarationSyntax by name, then BFS-expands the set of names
// reachable from @p topModule via HierarchyInstantiationSyntax child
// types.  Returns the closed set including @p topModule itself.
//
// An empty @p topModule yields an empty set; callers should treat
// that as "no gating" (scan everything).
std::unordered_set<std::string> collectReachableModules(const slang::ast::Compilation& compilation,
                                                        const std::string& topModule);

} // namespace connect
