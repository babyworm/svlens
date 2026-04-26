#include "SourceTextScanner.h"

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
#include <unordered_map>
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

// Codex cross-review: collect the set of module definition names
// reachable from `topModule` via instantiation. Mirrors the BFS in
// StyleSyntaxScanner so that source-text rules honor the requested
// top and ignore unrelated files in a filelist.
static std::unordered_set<std::string> collectReachableModules(
    const slang::ast::Compilation& compilation,
    const std::string& topModule)
{
    using namespace slang::syntax;
    std::unordered_set<std::string> reachable;
    if (topModule.empty())
        return reachable;

    std::unordered_map<std::string, const ModuleDeclarationSyntax*> defMap;
    for (const auto& tree : compilation.getSyntaxTrees()) {
        if (!tree) continue;
        AllSyntaxVisitor finder([&](const SyntaxNode& n) {
            if (n.kind == SyntaxKind::ModuleDeclaration) {
                const auto& mod = static_cast<const ModuleDeclarationSyntax&>(n);
                std::string name(mod.header->name.valueText());
                defMap.emplace(name, &mod);
            }
        });
        finder.visit(tree->root());
    }

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

        AllSyntaxVisitor childFinder([&](const SyntaxNode& n) {
            if (n.kind == SyntaxKind::HierarchyInstantiation) {
                const auto& hi =
                    static_cast<const HierarchyInstantiationSyntax&>(n);
                std::string childType(hi.type.valueText());
                if (!reachable.count(childType))
                    worklist.push_back(childType);
            }
        });
        childFinder.visit(*it->second);
    }
    return reachable;
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

        auto emitObs = [&](StyleObservation::Kind kind, const std::string& detail) {
            StyleObservation obs;
            obs.kind = kind;
            obs.scopePath = filePath;
            obs.name = fmt::format("{}:{}", filePath, lineNum);
            obs.detail = detail;
            // location stays default (invalid) because we have no buffer
            // offset; line/column info is published via lineNumber so
            // JSON consumers don't need to parse the detail string.
            obs.lineNumber = static_cast<uint32_t>(lineNum);
            graph.styleObservations.push_back(std::move(obs));
        };

        // US-39E.1: line length.  Round 39 review (R4 M1): compare in
        // size_t to avoid sign-flip when a line exceeds 2^31 chars
        // (e.g. a generated file with no newlines).  Negative or zero
        // configured limits already short-circuit via checkLen.
        if (checkLen) {
            size_t lineLen = lineContent.size();
            size_t maxLen = static_cast<size_t>(
                std::max(0, rules.maxLineLength));
            if (lineLen > maxLen) {
                emitObs(StyleObservation::Kind::LineTooLong,
                        fmt::format("{}:{}: line length {} exceeds max {} chars",
                                    filePath, lineNum, lineLen, rules.maxLineLength));
            }
        }

        // US-39E.2: hard tab
        bool sawHardTab = false;
        if (checkTab) {
            if (lineContent.find('\t') != std::string_view::npos) {
                emitObs(StyleObservation::Kind::HardTab,
                        fmt::format("{}:{}: hard tab character found (use spaces)",
                                    filePath, lineNum));
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
                emitObs(StyleObservation::Kind::TrailingWhitespace,
                        fmt::format("{}:{}: trailing whitespace",
                                    filePath, lineNum));
            }
        }

        if (atEnd)
            break;
        pos = end + 1;
        ++lineNum;
    }
}

} // namespace

void SourceTextScanner::scan(const slang::ast::Compilation& compilation,
                              const std::string& topModule,
                              const ConventionRules& rules,
                              ConnectionGraph& graph_out) {
    const bool needTextScans =
        (rules.maxLineLength > 0) ||
        rules.prohibitHardTabs ||
        rules.prohibitTrailingWhitespace;
    const bool needFileModuleChecks =
        rules.prohibitMultipleModulesPerFile ||
        rules.enforceFileModuleMatch;

    if (!needTextScans && !needFileModuleChecks)
        return;

    // Codex cross-review: build the reachable-module set for the
    // requested top so files with no participating modules (vendor IP,
    // alternate tops in a filelist) are skipped.  An empty `topModule`
    // means the legacy "scan everything" behavior; in that case we
    // pass `nullptr` to all per-file checks below.
    auto reachable = collectReachableModules(compilation, topModule);
    const bool gateByReachability = !topModule.empty();

    // Track already-scanned buffers so we don't double-report when
    // the same file appears in multiple syntax trees (rare but possible
    // in multi-compilation-unit setups).
    std::unordered_set<uint32_t> scannedBuffers;

    for (const auto& tree : compilation.getSyntaxTrees()) {
        if (!tree) continue;

        const auto& root = tree->root();
        const auto& sm = tree->sourceManager();

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

        // Codex cross-review: skip the entire file if no module in it
        // participates in the topModule analysis.  Files with zero
        // module declarations (pure header includes) are also skipped
        // when reachability gating is on, because they cannot be a
        // meaningful child of the requested top.
        //
        // Codex Round 2 cross-review: this gate is FILE-LEVEL by design.
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
                // Codex cross-review: only emit when at least one
                // reachable module lives in the file (gate already
                // ensured this above when reachability is on).
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
                    obs.lineNumber =
                        static_cast<uint32_t>(sm.getLineNumber(modules[1].second));
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
                    obs.lineNumber =
                        static_cast<uint32_t>(sm.getLineNumber(modules[0].second));
                    graph_out.styleObservations.push_back(std::move(obs));
                }
            }
        }
    }
}

void SourceTextScanner::scan(const slang::ast::Compilation& compilation,
                              const ConventionRules& rules,
                              ConnectionGraph& graph_out) {
    // Legacy overload: passing an empty topModule disables reachability
    // gating, preserving the original "scan every syntax tree" behavior.
    scan(compilation, /*topModule=*/std::string{}, rules, graph_out);
}

} // namespace connect
