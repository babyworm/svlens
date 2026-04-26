#pragma once

#include "ConnectionGraph.h"
#include <slang/ast/Compilation.h>

namespace connect {

/// Stage-2 syntax-tree scanner for style observations that cannot be
/// detected in the post-elaboration AST because the elaboration pass
/// normalises them away:
///
///   US-39C  Wildcard port connections (`.*`):
///     The elaborated AST expands `.*` to individual PortConnection
///     nodes -- the original wildcard token is gone.
///
///   US-39D  Bare integer literals (width-less):
///     The elaborated AST assigns every integer a default 32-bit int
///     type, so `2` and `32'd2` are indistinguishable at the AST
///     level.  The original syntax token retains the distinction.
///
/// The scanner walks the raw SyntaxTree(s) from the compilation and
/// appends StyleObservation entries into the provided ConnectionGraph.
class StyleSyntaxScanner {
public:
    /// Walk all syntax trees from @p compilation and append
    /// WildcardPortConnection and BareIntegerLiteral observations into
    /// @p graph_out.styleObservations.
    ///
    /// @p topModule restricts scanning to module declarations whose
    /// name matches the requested top.  Observations that originate
    /// in other (sibling) module definitions in the same file are
    /// suppressed so that each per-top analysis run is independent.
    static void scan(const slang::ast::Compilation& compilation,
                     const std::string& topModule,
                     ConnectionGraph& graph_out);
};

} // namespace connect
