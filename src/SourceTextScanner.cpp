#include "SourceTextScanner.h"
#include "StyleReachability.h"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/text/SourceLocation.h>

#include <fmt/core.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace connect {

namespace {

namespace fs = std::filesystem;

// Collect all ModuleDeclarationSyntax nodes in a syntax tree, returning
// a vector of (module_name, first_token_location) pairs.
static std::vector<std::pair<std::string, slang::SourceLocation>>
collectModuleDeclarations(const slang::syntax::SyntaxNode& root) {
    using namespace slang::syntax;
    std::vector<std::pair<std::string, slang::SourceLocation>> result;
    AllSyntaxVisitor visitor([&](const SyntaxNode& n) {
        if (n.kind == SyntaxKind::ModuleDeclaration) {
            const auto& mod = static_cast<const ModuleDeclarationSyntax&>(n);
            std::string name(mod.header->name.valueText());
            slang::SourceLocation loc = mod.header->name.location();
            result.emplace_back(std::move(name), loc);
        }
    });
    visitor.visit(root);
    return result;
}

// Scan raw source text line-by-line and append style observations.
// @p filePath  human-readable file path for observation scopePath
// @p text      full raw source text (may or may not end with '\n')
// @p rules     only enabled checks are performed
// @p graph     destination for observations
static void scanSourceText(const std::string& filePath,
                           std::string_view text,
                           const ConventionRules& rules,
                           ConnectionGraph& graph) {
    const bool checkLen = (rules.maxLineLength > 0);
    const bool checkTab = rules.prohibitHardTabs;
    const bool checkTrail = rules.prohibitTrailingWhitespace;

    if (!checkLen && !checkTab && !checkTrail)
        return;

    size_t lineNum = 1;
    size_t pos = 0;
    const size_t textLen = text.size();

    while (pos <= textLen) {
        // Find end of current line (LF or end-of-buffer).
        size_t end = text.find('\n', pos);
        bool atEnd = (end == std::string_view::npos);
        size_t lineEnd = atEnd ? textLen : end;

        // lineContent: characters from pos up to (but not including) '\n'
        // Strip '\r' for CR-LF files.
        std::string_view lineContent = text.substr(pos, lineEnd - pos);
        if (!lineContent.empty() && lineContent.back() == '\r')
            lineContent = lineContent.substr(0, lineContent.size() - 1);

        // Build a fake SourceLocation with offset==0 so the observation
        // carries meaningful file:line context via the detail string.
        // (We encode the line number in the detail message directly.)

        auto emitObs = [&](StyleObservation::Kind kind, const std::string& detail,
                           uint32_t column) {
            StyleObservation obs;
            obs.kind = kind;
            obs.scopePath = filePath;
            obs.name = fmt::format("{}:{}", filePath, lineNum);
            obs.detail = detail;
            // location stays default (invalid) because we have no buffer
            // offset; line/column info is published via lineNumber so
            // JSON consumers don't need to parse the detail string.
            obs.lineNumber = static_cast<uint32_t>(lineNum);
            // Fresh review R1A MAJOR: schema advertises optional
            // `column`, but until now source-text scanners shipped
            // observations with column==0 so JsonReport silently
            // suppressed the field.  Each rule provides the offset
            // within the line where the violation begins (1-based).
            obs.columnNumber = column;
            graph.styleObservations.push_back(std::move(obs));
        };

        // US-39E.1: line length.  Round 39 review (R4 M1): compare in
        // size_t to avoid sign-flip when a line exceeds 2^31 chars
        // (e.g. a generated file with no newlines).  Negative or zero
        // configured limits already short-circuit via checkLen.
        if (checkLen) {
            size_t lineLen = lineContent.size();
            size_t maxLen = static_cast<size_t>(std::max(0, rules.maxLineLength));
            if (lineLen > maxLen) {
                emitObs(StyleObservation::Kind::LineTooLong,
                        fmt::format("{}:{}: line length {} exceeds max {} chars",
                                    filePath, lineNum, lineLen, rules.maxLineLength),
                        // Column points at the first byte that violates
                        // the limit (i.e. maxLen + 1 in 1-based terms).
                        static_cast<uint32_t>(maxLen + 1));
            }
        }

        // US-39E.2: hard tab
        bool sawHardTab = false;
        if (checkTab) {
            size_t tabPos = lineContent.find('\t');
            if (tabPos != std::string_view::npos) {
                emitObs(StyleObservation::Kind::HardTab,
                        fmt::format("{}:{}: hard tab character found (use spaces)",
                                    filePath, lineNum),
                        // Column of the first tab character (1-based).
                        static_cast<uint32_t>(tabPos + 1));
                sawHardTab = true;
            }
        }

        // US-39E.3: trailing whitespace (space/tab before the newline).
        // Round 39 review: when a line ends in a tab the more specific
        // HardTab rule already fires; emitting TrailingWhitespace too
        // double-counts the same source character.
        if (checkTrail && !lineContent.empty()) {
            char last = lineContent.back();
            if (last == ' ' || (last == '\t' && !sawHardTab)) {
                // Walk backward from the line end to find the first
                // byte of the trailing-whitespace run; column is 1-based.
                size_t wsStart = lineContent.size();
                while (wsStart > 0 &&
                       (lineContent[wsStart - 1] == ' ' ||
                        lineContent[wsStart - 1] == '\t')) {
                    --wsStart;
                }
                emitObs(StyleObservation::Kind::TrailingWhitespace,
                        fmt::format("{}:{}: trailing whitespace",
                                    filePath, lineNum),
                        static_cast<uint32_t>(wsStart + 1));
            }
        }

        if (atEnd)
            break;
        pos = end + 1;
        ++lineNum;
    }
}

} // namespace

void SourceTextScanner::scan(const slang::ast::Compilation& compilation, const std::string& topModule,
                             const ConventionRules& rules, ConnectionGraph& graph_out) {
    const bool needTextScans =
        (rules.maxLineLength > 0) ||
        rules.prohibitHardTabs ||
        rules.prohibitTrailingWhitespace;
    const bool needFileModuleChecks =
        rules.prohibitMultipleModulesPerFile ||
        rules.enforceFileModuleMatch;

    if (!needTextScans && !needFileModuleChecks)
        return;

    // Build the reachable-module set for the requested top so files
    // with no participating modules (vendor IP, alternate tops in a
    // filelist) are skipped.  An empty `topModule` means the legacy
    // "scan everything" behavior; in that case we
    // pass `nullptr` to all per-file checks below.
    auto reachable = collectReachableModules(compilation, topModule);
    const bool gateByReachability = !topModule.empty();

    // Track already-scanned buffers so we don't double-report when
    // the same file appears in multiple syntax trees (rare but possible
    // in multi-compilation-unit setups).
    std::unordered_set<uint32_t> scannedBuffers;

    // R1 MINOR: drive line-number lookup off the compilation's global
    // SourceManager so include-header buffers (which the per-tree sm
    // may not own) still resolve.  Falls back to the per-tree sm if
    // the global is unavailable -- preserves prior behavior.
    const auto* globalSm = compilation.getSourceManager();

    for (const auto& tree : compilation.getSyntaxTrees()) {
        if (!tree) continue;

        const auto& root = tree->root();
        const auto& sm = globalSm ? *globalSm : tree->sourceManager();

        // Obtain the BufferID for this tree's root token.
        slang::BufferID bufId =
            root.getFirstToken().location().buffer();
        if (!bufId)
            continue;

        uint32_t rawId = bufId.getId();
        if (scannedBuffers.count(rawId))
            continue;
        scannedBuffers.insert(rawId);

        // Collect this file's module declarations once; reused for
        // both reachability gating and file-naming checks.
        auto modules = collectModuleDeclarations(root);

        // Skip the entire file if no module in it participates in the
        // topModule analysis.  Files with zero module declarations
        // (pure header includes) are also skipped when reachability
        // gating is on, because they cannot be a meaningful child of
        // the requested top.
        //
        // This gate is FILE-LEVEL by design.
        // Once any module in the buffer is reachable from `topModule`,
        // the WHOLE buffer is admitted -- including sibling modules
        // that are not themselves reachable.  Physical-line rules
        // (LineTooLong, HardTab, TrailingWhitespace) cannot be
        // attributed cleanly to a single module declaration because a
        // single source line could span module boundaries or sit
        // entirely outside any module.  Per-module slicing of the
        // buffer is high-effort, fragile, and unintuitive (users place
        // unrelated code in unrelated files); the file boundary is the
        // natural unit of these checks.  See SourceTextScanner.h.
        if (gateByReachability) {
            bool anyReachable = false;
            for (const auto& [name, _loc] : modules) {
                if (reachable.count(name)) {
                    anyReachable = true;
                    break;
                }
            }
            if (!anyReachable)
                continue;
        }

        // Resolve human-readable file path.
        std::string filePath(sm.getRawFileName(bufId));
        if (filePath.empty())
            filePath = "<unknown>";

        // US-39E: raw source-text line checks.
        if (needTextScans) {
            std::string_view text = sm.getSourceText(bufId);
            scanSourceText(filePath, text, rules, graph_out);
        }

        // US-39F: file/module naming checks.
        if (needFileModuleChecks) {

            if (rules.prohibitMultipleModulesPerFile && modules.size() > 1) {
                // Only emit when at least one reachable module lives in
                // the file (gate already ensured this above when
                // reachability is on).
                std::string nameList;
                for (size_t i = 0; i < modules.size(); ++i) {
                    if (i) nameList += ", ";
                    nameList += modules[i].first;
                }
                StyleObservation obs;
                obs.kind = StyleObservation::Kind::MultipleModulesPerFile;
                obs.scopePath = filePath;
                obs.name = filePath;
                obs.detail = fmt::format(
                    "{}: {} modules declared in one file ({}) "
                    "(lowRISC requires one module per file)",
                    filePath, modules.size(), nameList);
                // Anchor at the second module's declaration line so
                // downstream tooling can jump to the offending decl.
                if (modules.size() > 1)
                    obs.lineNumber = static_cast<uint32_t>(sm.getLineNumber(modules[1].second));
                // Fresh review R1A MAJOR: file-level rule anchored at
                // line start; column 1 marks the line so JSON consumers
                // get a non-zero structured field.
                obs.columnNumber = 1;
                graph_out.styleObservations.push_back(std::move(obs));
            }

            if (rules.enforceFileModuleMatch && modules.size() == 1) {
                // Derive expected module name from file basename without ".sv".
                std::string base = fs::path(filePath).stem().string();
                const std::string& modName = modules[0].first;
                if (base != modName) {
                    StyleObservation obs;
                    obs.kind = StyleObservation::Kind::FileNameMismatch;
                    obs.scopePath = filePath;
                    obs.name = modName;
                    obs.detail = fmt::format(
                        "{}: file basename '{}' does not match module name '{}' "
                        "(lowRISC requires file name == module name)",
                        filePath, base, modName);
                    obs.lineNumber = static_cast<uint32_t>(sm.getLineNumber(modules[0].second));
                    // Fresh review R1A MAJOR: anchor the line-start
                    // column so JSON consumers see a non-zero column.
                    obs.columnNumber = 1;
                    graph_out.styleObservations.push_back(std::move(obs));
                }
            }
        }
    }
}

} // namespace connect
