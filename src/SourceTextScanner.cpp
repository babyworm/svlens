#include "SourceTextScanner.h"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/text/SourceLocation.h>

#include <fmt/core.h>

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

        // US-39E.1: line length
        if (checkLen) {
            size_t lineLen = lineContent.size();
            if (static_cast<int>(lineLen) > rules.maxLineLength) {
                emitObs(StyleObservation::Kind::LineTooLong,
                        fmt::format("{}:{}: line length {} exceeds max {} chars",
                                    filePath, lineNum, lineLen, rules.maxLineLength));
            }
        }

        // US-39E.2: hard tab
        if (checkTab) {
            if (lineContent.find('\t') != std::string_view::npos) {
                emitObs(StyleObservation::Kind::HardTab,
                        fmt::format("{}:{}: hard tab character found (use spaces)",
                                    filePath, lineNum));
            }
        }

        // US-39E.3: trailing whitespace (space/tab before the newline)
        if (checkTrail && !lineContent.empty()) {
            char last = lineContent.back();
            if (last == ' ' || last == '\t') {
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
            auto modules = collectModuleDeclarations(root);

            if (rules.prohibitMultipleModulesPerFile && modules.size() > 1) {
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

} // namespace connect
