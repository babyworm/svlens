#pragma once

#include "ConnectionGraph.h"
#include "ConventionChecker.h"
#include <slang/ast/Compilation.h>
#include <string>

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
///
/// Scope semantics (Codex Round 2 cross-review):
///   Source-text rules apply at FILE granularity: when @p topModule is
///   set, files containing AT LEAST ONE reachable module are scanned in
///   full; sibling modules in the same file are NOT exempt.  This is
///   intentional -- physical-line rules like LineTooLong cannot be
///   cleanly attributed to a single module declaration because a single
///   line could span declarations (e.g. an `endmodule` on the same line
///   as the next `module`, or a comment-only line outside any module).
///   To exempt sibling code from these checks, place it in a separate
///   file.  Splitting buffers by per-module syntax extents would be
///   high-effort and fragile, while the natural file boundary already
///   matches user expectation that `topModule` selects a unit-of-build.
class SourceTextScanner {
public:
    /// Codex cross-review: scope source-text scanning to files that
    /// participate in the analysis of @p topModule. Without scoping,
    /// filelist runs (vendor IP, alternate tops) emitted style INFOs
    /// for unrelated files, affecting the issue exit code.
    ///
    /// Walk syntax-tree buffers from @p compilation that contain at
    /// least one module reachable from @p topModule, and append
    /// LineTooLong, HardTab, TrailingWhitespace, MultipleModulesPerFile,
    /// and FileNameMismatch observations into @p graph_out.styleObservations.
    ///
    /// Only checks that are enabled in @p rules are performed.
    ///
    /// File-level scope: see the class doc-comment above.  A file that
    /// contains the requested top (or any of its instantiation children)
    /// is scanned in full, including sibling modules that are not
    /// themselves reachable.
    static void scan(const slang::ast::Compilation& compilation, const std::string& topModule,
                     const ConventionRules& rules, ConnectionGraph& graph_out);

    /// Legacy "scan everything" overload kept for callers that do not
    /// have a top module name; equivalent to passing an empty topModule
    /// which disables reachability gating.  New callers should prefer
    /// the topModule-aware overload above.
    static void scan(const slang::ast::Compilation& compilation,
                     const ConventionRules& rules,
                     ConnectionGraph& graph_out);
};

} // namespace connect
