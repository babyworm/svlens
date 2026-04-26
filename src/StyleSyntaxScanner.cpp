#include "StyleSyntaxScanner.h"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

#include <fmt/core.h>

#include <string>
#include <string_view>
#include <unordered_set>

namespace connect {

namespace {

// Round 39 review: ancestor walk depth cap.  Realistic SystemVerilog
// expressions can nest many levels deep (chained ternaries, nested
// concat, packed-struct access).  Bumped from 20 to 64 so deeply
// nested but legitimate code does not silently fall off the end of
// the walk and produce a false negative or a false positive.  Still
// bounded to avoid pathological O(n^2) walks in adversarial input.
static constexpr int kAncestorWalkDepth = 64;

// Return true when the ancestor chain of @p node passes through a
// ForVariableDeclaration node (loop-index initialiser) or a
// ParameterDeclaration node (parameter default value).  Bare integer
// literals in those positions are conventional and must NOT be flagged.
static bool isInSkippedContext(const slang::syntax::SyntaxNode& node) {
    using slang::syntax::SyntaxKind;
    const slang::syntax::SyntaxNode* cur = node.parent;
    for (int depth = 0; depth < kAncestorWalkDepth && cur;
         ++depth, cur = cur->parent) {
        switch (cur->kind) {
            case SyntaxKind::ForVariableDeclaration:
            case SyntaxKind::ParameterDeclaration:
            case SyntaxKind::TypeParameterDeclaration:
            case SyntaxKind::ParameterDeclarationStatement:
                return true;
            // Round 39 review: parameter overrides on instance ports
            // (`u_inst #(.WIDTH(8)) ...`) are conventional integer
            // arguments to a typed parameter; the WIDTH parameter
            // already constrains the value.  Same applies to type
            // parameter port lists.
            case SyntaxKind::ParameterValueAssignment:
            case SyntaxKind::ParameterPortList:
                return true;
            // Round 39 review: contents of `{a, b, c}` concatenation
            // and `{N{x}}` replication are width-constrained by the
            // surrounding container; flagging an integer literal
            // inside is noisy and not what the rule targets.
            case SyntaxKind::ConcatenationExpression:
            case SyntaxKind::MultipleConcatenationExpression:
                return true;
            // Bit/range select indices (`data[0]`, `data[7:0]`) are
            // conventional untyped constants; do not flag them.
            case SyntaxKind::BitSelect:
            case SyntaxKind::ElementSelect:
            case SyntaxKind::SimpleRangeSelect:
            case SyntaxKind::AscendingRangeSelect:
            case SyntaxKind::DescendingRangeSelect:
                return true;
            // Stop early at statement/block boundaries.
            case SyntaxKind::ContinuousAssign:
            case SyntaxKind::AlwaysBlock:
            case SyntaxKind::AlwaysCombBlock:
            case SyntaxKind::AlwaysFFBlock:
                return false;
            default:
                break;
        }
    }
    return false;
}

// Return true when the parent chain indicates the literal appears in
// an RTL assignment context (continuous assign, procedural assignment,
// or port connection expression).  Uses a conservative positive-list.
static bool isInRtlContext(const slang::syntax::SyntaxNode& node) {
    using slang::syntax::SyntaxKind;
    const slang::syntax::SyntaxNode* cur = node.parent;
    for (int depth = 0; depth < kAncestorWalkDepth && cur;
         ++depth, cur = cur->parent) {
        switch (cur->kind) {
            case SyntaxKind::ContinuousAssign:
                return true;
            case SyntaxKind::AssignmentExpression:
                return true;
            case SyntaxKind::NamedPortConnection:
            case SyntaxKind::OrderedPortConnection:
                return true;
            // Stop at module/function/task/parameter boundaries.
            case SyntaxKind::ModuleDeclaration:
            case SyntaxKind::FunctionDeclaration:
            case SyntaxKind::TaskDeclaration:
            case SyntaxKind::ParameterDeclaration:
            case SyntaxKind::ParameterDeclarationStatement:
            case SyntaxKind::ParameterValueAssignment:
            case SyntaxKind::ParameterPortList:
            case SyntaxKind::ForVariableDeclaration:
            case SyntaxKind::ModuleHeader:
                return false;
            default:
                break;
        }
    }
    return false;
}

// Visitor that finds WildcardPortConnection (`.*`) and bare
// IntegerLiteralExpression nodes inside the allowed set of module
// declarations.  Modules NOT in `allowedModules` are skipped entirely
// so that sibling modules in the same file do not contribute
// observations to the current top-module analysis run.
struct StyleScanner : public slang::syntax::SyntaxVisitor<StyleScanner> {
    ConnectionGraph& graph;
    std::string filePath;
    // Source manager for the current syntax tree, used to populate
    // StyleObservation::lineNumber / columnNumber so JSON consumers
    // do not have to regex-parse the detail string.
    const slang::SourceManager* sm = nullptr;
    // Set of module definition names to scan; derived from the top
    // module plus all modules that appear in the compilation and are
    // referenced as children.  We populate this lazily using a simpler
    // policy: allow any module name that contains the top module name
    // as an ancestor OR is itself the top module.  Since we cannot
    // know the full instance hierarchy from the syntax tree alone, we
    // use a coarser but correct strategy: allow ALL module declarations
    // whose body is reachable from the top-module body by DFS of
    // instantiation names collected before the visitor runs.
    //
    // Simple implementation: only scan when `activeDepth_ > 0`
    // (we entered an allowed module declaration) and skip
    // module declarations whose header name is NOT in `allowedModules_`.
    const std::unordered_set<std::string>& allowedModules;

    // Track depth inside an allowed module so that nested module
    // declarations (which are uncommon but legal) can be gated.
    int activeDepth_ = 0;

    StyleScanner(ConnectionGraph& g,
                 const std::unordered_set<std::string>& allowed)
        : graph(g), allowedModules(allowed) {}

    void populateLineColumn(StyleObservation& obs) const {
        if (!sm || !obs.location.valid())
            return;
        obs.lineNumber = static_cast<uint32_t>(sm->getLineNumber(obs.location));
        obs.columnNumber = static_cast<uint32_t>(sm->getColumnNumber(obs.location));
    }

    // Gate entry into module declarations: only descend into modules
    // that are in the allowed set.
    void handle(const slang::syntax::ModuleDeclarationSyntax& node) {
        std::string_view modName = node.header->name.valueText();
        if (allowedModules.count(std::string(modName))) {
            ++activeDepth_;
            visitDefault(node);
            --activeDepth_;
        }
        // Else: skip this module declaration entirely.
    }

    // US-39C: wildcard port connections (`.*`).
    // Only emit when inside an active (allowed) module.
    void handle(const slang::syntax::WildcardPortConnectionSyntax& node) {
        if (activeDepth_ <= 0)
            return;
        slang::SourceLocation loc = node.dot.location();
        StyleObservation obs;
        obs.kind = StyleObservation::Kind::WildcardPortConnection;
        obs.scopePath = filePath;
        obs.name = ".*";
        obs.location = loc;
        populateLineColumn(obs);
        obs.detail =
            "wildcard port connection `.*` hides signal-to-port mapping "
            "(lowRISC requires explicit `.port_name(signal)` connections)";
        graph.styleObservations.push_back(std::move(obs));
        // No children of interest; do not recurse.
    }

    // US-39D: bare (width-less) integer literals.
    // LiteralExpressionSyntax with SyntaxKind::IntegerLiteralExpression
    // covers unsized decimal/hex/octal/binary literals like `2`, `32`.
    // Sized literals (`8'd2`) are IntegerVectorExpression and are not
    // visited here.  UnbasedUnsized (`'0`, `'1`, `'x`) are also a
    // different kind and are not flagged.
    void handle(const slang::syntax::LiteralExpressionSyntax& node) {
        using slang::syntax::SyntaxKind;
        if (node.kind != SyntaxKind::IntegerLiteralExpression) {
            visitDefault(node);
            return;
        }
        if (activeDepth_ <= 0)
            return;

        if (!isInSkippedContext(node) && isInRtlContext(node)) {
            std::string_view text = node.literal.rawText();
            // Round 39 review: lowRISC §6.7 explicitly allows the bare
            // literals 0 and 1 (initial values, polarity flips, count
            // increments).  Skip them so this rule does not produce
            // alarm fatigue on every `+ 1` or `= 0`.
            if (text == "0" || text == "1") {
                visitDefault(node);
                return;
            }
            if (text.find('\'') == std::string_view::npos) {
                slang::SourceLocation loc = node.literal.location();
                StyleObservation obs;
                obs.kind = StyleObservation::Kind::BareIntegerLiteral;
                obs.scopePath = filePath;
                obs.name = std::string(text);
                obs.location = loc;
                populateLineColumn(obs);
                obs.detail = fmt::format(
                    "bare integer literal '{}' has no explicit width "
                    "(lowRISC requires `<width>'<base><value>`, e.g. "
                    "`8'd{}`, or an unsized literal `'0`/`'1`)",
                    std::string(text), std::string(text));
                graph.styleObservations.push_back(std::move(obs));
            }
        }
    }
};

// Collect the set of module definition names reachable from `topModule`
// in the syntax trees.  We do a DFS over HierarchyInstantiationSyntax
// nodes to find the type names of all instantiated children, then
// recursively include those as well.
static std::unordered_set<std::string> collectReachableModules(
    const slang::ast::Compilation& compilation,
    const std::string& topModule)
{
    using namespace slang::syntax;

    std::unordered_set<std::string> reachable;
    std::vector<std::string> worklist{topModule};

    // Build a map: module_name -> ModuleDeclarationSyntax* for quick lookup.
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

    // BFS to find all transitively instantiated module names.
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

} // namespace

void StyleSyntaxScanner::scan(const slang::ast::Compilation& compilation,
                               const std::string& topModule,
                               ConnectionGraph& graph_out) {
    auto allowed = collectReachableModules(compilation, topModule);
    StyleScanner scanner(graph_out, allowed);

    for (const auto& tree : compilation.getSyntaxTrees()) {
        if (!tree) continue;
        scanner.filePath =
            std::string(tree->sourceManager().getFileName(
                tree->root().getFirstToken().location()));
        scanner.sm = &tree->sourceManager();
        tree->root().visit(scanner);
    }
}

} // namespace connect
