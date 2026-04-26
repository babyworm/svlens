#pragma once

#include <slang/ast/SemanticFacts.h>
#include <slang/text/SourceLocation.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace connect {

struct PortInfo {
    std::string instancePath;
    std::string portName;
    slang::ast::ArgumentDirection direction;
    uint32_t width = 0;
    bool isSigned = false;
    slang::SourceLocation location;

    std::string fullPath() const {
        return instancePath + "." + portName;
    }
};

enum class ConnectionKind {
    Direct,
    Approximate
};

struct Connection {
    PortInfo source;
    PortInfo dest;
    ConnectionKind kind = ConnectionKind::Direct;
};

// Round 38: style-only observations recorded by ConnectionExtractor
// during the walk, consumed by ConventionChecker. Each observation
// captures a non-port style smell so the checker can emit lowRISC-
// style INFO entries without re-walking the slang AST.
struct StyleObservation {
    enum class Kind {
        LegacyAlwaysBlock,         // `always @*` instead of always_ff/always_comb
        AnonymousEnum,             // `enum { A, B } sig;` without typedef
        UnnamedGenerateBlock,      // `if (...) begin ... end` without `: name`
        ParameterNameViolation,    // parameter not in expected case
        TypedefSuffixViolation,    // typedef without `_t` / `_e` suffix
        ResetPolarityBad,          // comma-syntax or active-high reset in always_ff
        MissingDSuffix,            // _q-registered signal has no matching _d combinational driver
        WildcardPortConnection,    // instance uses `.*` instead of named .port(signal) connections
        BareIntegerLiteral,        // integer literal without explicit width (e.g. bare `2` vs `8'd2`)
        // Round 39 review: split rules previously overloaded onto LegacyAlwaysBlock
        MissingQSuffix,            // always_ff non-blocking LHS lacks `_q`/`_q<n>` suffix
        BannedStateType,           // RTL variable declared with bit/int/byte/... (lowRISC requires logic)
        MissingCaseDefault,        // case statement without `default:` branch
        MissingUniqueCase,         // case statement without `unique`/`priority` qualifier
        // US-39E source-text checks
        LineTooLong,               // source line exceeds max_line_length chars
        HardTab,                   // source line contains a hard tab character
        TrailingWhitespace,        // source line has trailing whitespace before newline
        // US-39F file-module naming checks
        MultipleModulesPerFile,    // .sv file declares more than one module
        FileNameMismatch,          // .sv basename does not match the single module name
    };
    Kind kind;
    std::string scopePath;   // hierarchical path of the enclosing module
    std::string name;        // the offending name (parameter/typedef/etc.)
    std::string detail;      // human-readable message body
    slang::SourceLocation location;
    // Round 39 review: explicit line/column so JSON consumers can
    // surface precise locations without regex-parsing `detail`. For
    // raw-text observations (no slang::SourceLocation), the scanner
    // populates these directly from its line counter.
    uint32_t lineNumber = 0;
    uint32_t columnNumber = 0;
};

// Round 38 US-38D / US-38E: raw declaration captures consumed by
// ConventionChecker for regex-based pattern matching (parameter case,
// typedef suffix). Kept as a thin record so the extractor stays
// rule-agnostic.
struct DeclarationCapture {
    std::string scopePath;
    std::string name;
    slang::SourceLocation location;
};

struct ConnectionGraph {
    std::vector<Connection> connections;
    std::vector<PortInfo> allPorts;
    std::unordered_set<std::string> connectedPorts; // ports with non-empty expressions
    std::unordered_set<std::string> tieOffPorts;    // ports connected only to compile-time constants
    std::unordered_set<std::string> constantZeroTieOffPorts; // ports tied to an explicit constant zero
    std::string topModule;
    std::vector<StyleObservation> styleObservations;
    std::vector<DeclarationCapture> parameters;
    std::vector<DeclarationCapture> typedefs;
};

} // namespace connect
