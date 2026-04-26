#pragma once

#include "ConnectionGraph.h"
#include "ConventionChecker.h"
#include <slang/ast/Compilation.h>

namespace connect {

/// Stage-3 source-text scanner for style observations that operate on
/// the raw source bytes rather than the post-elaboration AST.
///
///   US-39E  Line-length / hard-tab / trailing-whitespace:
///     These are purely textual conventions; slang's AST provides no
///     representation of them.
///
///   US-39F  File/module naming:
///     Each .sv file should declare exactly one module, and the
///     file's basename (without ".sv") should match the module name.
///
/// The scanner reads each buffer via SourceManager::getSourceText and
/// walks it line-by-line, then walks the syntax trees for module
/// declaration counts.  Observations are appended into the provided
/// ConnectionGraph.
class SourceTextScanner {
public:
    /// Walk all syntax-tree buffers from @p compilation and append
    /// LineTooLong, HardTab, TrailingWhitespace, MultipleModulesPerFile,
    /// and FileNameMismatch observations into @p graph_out.styleObservations.
    ///
    /// Only checks that are enabled in @p rules are performed.
    static void scan(const slang::ast::Compilation& compilation,
                     const ConventionRules& rules,
                     ConnectionGraph& graph_out);
};

} // namespace connect
