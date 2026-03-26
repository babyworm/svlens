# slang-connect Design Spec

> Date: 2026-03-27
> Status: APPROVED
> Language: C++20 | Base: slang v10+

## 1. Overview

Module interconnect verification tool. Analyzes all port connections in an elaborated SystemVerilog hierarchy and reports width mismatches, type mismatches, dangling outputs, and undriven inputs.

## 2. Scope

### MVP (Phase 1)
- **Checks**: Width Mismatch, Type Mismatch, Dangling Output, Undriven Input
- **Output**: JSON, Markdown, CSV, Table (all 4 formats)
- **Waiver**: YAML-based waiver mechanism included

### Phase 2 (deferred)
- Protocol Completeness (AXI/AHB/APB signal group)
- Convention Check (naming rules via YAML)

## 3. Architecture

**Approach: AST Visitor + ConnectionGraph**

```
main.cpp
  1. CLI parsing
  2. slang::Compilation (full elaboration)
  3. ConnectionExtractor::extract() → ConnectionGraph
  4. CheckerRunner::runAll(graph) → vector<Issue>
  5. WaiverFilter::apply(issues) → active + waived
  6. ReportGenerator::generate(data) → files/stdout
  7. return computeExitCode(active)
```

Each stage communicates through a single data type:
- Extraction outputs `ConnectionGraph`
- Checkers output `vector<Issue>`
- Reporters consume `ReportData`

## 4. Data Model

### 4.1 PortInfo

```cpp
struct PortInfo {
    std::string instancePath;   // "top.u_core"
    std::string portName;       // "o_data"
    ArgumentDirection direction; // In, Out, InOut
    uint32_t width;             // bit width
    bool isSigned;              // signed flag
    SourceLocation location;    // source location
};
```

### 4.2 Connection

```cpp
struct Connection {
    PortInfo source;            // driver (output)
    PortInfo dest;              // load (input)
};
```

### 4.3 Issue

```cpp
struct Issue {
    enum Type { WIDTH_MISMATCH, TYPE_MISMATCH, DANGLING_OUTPUT, UNDRIVEN_INPUT };
    enum Severity { ERROR, WARN, INFO };
    Type type;
    Severity severity;
    PortInfo port;                          // single-port issues (dangling, undriven)
    std::optional<Connection> connection;   // connection issues (width, type)
    std::string detail;                     // human-readable description
};
```

### 4.4 ConnectionGraph

```cpp
struct ConnectionGraph {
    std::vector<Connection> connections;    // all port connections
    std::vector<PortInfo> allPorts;         // all ports (for dangling/undriven detection)
    std::string topModule;
};
```

## 5. Connection Extraction

`ConnectionExtractor` traverses the slang AST using `ASTVisitor` to build the `ConnectionGraph`.

### slang API usage

| API | Purpose |
|-----|---------|
| `InstanceSymbol::getPortConnections()` | Get all port connections for an instance |
| `InstanceBodySymbol::getPortList()` | Get complete port list of a module |
| `PortSymbol::getType()` | Extract width and signedness |
| `PortSymbol::direction` | Port direction (In/Out/InOut) |
| `PortConnection::getExpression()` | Analyze connection expression |
| `InstanceSymbol::instanceDepth` | Hierarchy depth for --depth filtering |

### Key logic

1. **Connection tracking**: Analyze expression from `PortConnection::getExpression()` to identify which net/variable a port connects to. Record output→input pairs sharing the same net as `Connection`.

2. **Hierarchy traversal**: Limit depth per `--depth` option using `InstanceSymbol::instanceDepth`.

3. **Port registry**: Register all ports in `allPorts`. Mark ports that have confirmed connections. After traversal, unmarked outputs = dangling, unmarked inputs = undriven.

4. **Assign tracking**: Continuous assignments (`assign narrow = wide;`) at top-level are also treated as connections. Trace net references within expressions to capture indirect connections.

```cpp
class ConnectionExtractor {
public:
    ConnectionExtractor(const slang::ast::Compilation& compilation,
                        const std::string& topModule,
                        int maxDepth = -1);
    ConnectionGraph extract();

private:
    void visitInstance(const InstanceSymbol& instance, const std::string& path);
    void trackAssignment(const Expression& expr, const std::string& scopePath);
};
```

## 6. Checkers

All checkers implement a common interface:

```cpp
class IChecker {
public:
    virtual ~IChecker() = default;
    virtual std::vector<Issue> check(const ConnectionGraph& graph) const = 0;
};
```

### 6.1 WidthChecker

Compares `source.width` vs `dest.width` for each connection.

| Condition | Severity |
|-----------|----------|
| source > dest (truncation) | ERROR |
| source < dest, unsigned (zero-extension) | WARN |
| source < dest, signed (sign-extension) | INFO |
| source == dest | OK |

### 6.2 TypeChecker

Compares `source.isSigned` vs `dest.isSigned`. Mismatch = ERROR.

### 6.3 DanglingChecker

Finds ports where `direction == Out` but port does not appear as `source` in any connection. Severity = WARN.

### 6.4 UndrivenChecker

Finds ports where `direction == In` but port does not appear as `dest` in any connection. Severity = ERROR.

### CheckerRunner

```cpp
class CheckerRunner {
public:
    void addChecker(std::unique_ptr<IChecker> checker);
    std::vector<Issue> runAll(const ConnectionGraph& graph) const;
};
```

CLI options `--no-check-width` etc. control which checkers are registered (all enabled by default).

## 7. Waiver Filter

```cpp
class WaiverFilter {
public:
    WaiverFilter(const std::string& waiverYamlPath);

    struct WaiverResult {
        std::vector<Issue> active;
        std::vector<Issue> waived;
    };

    WaiverResult apply(const std::vector<Issue>& issues) const;
};
```

YAML format:

```yaml
waivers:
  - pattern: "*.o_debug*"
    type: DANGLING_OUTPUT
    reason: "Debug ports intentionally unconnected"

  - source: "u_legacy.data_o"
    type: CONVENTION
    reason: "Legacy IP, cannot rename"

  - pattern: "u_test_*"
    type: "*"
    reason: "Test infrastructure"
```

Matching: glob-style pattern against `instancePath.portName`. Type matching against `Issue::Type` or `"*"` for all types.

## 8. Report Generation

```cpp
class IReportGenerator {
public:
    virtual ~IReportGenerator() = default;
    virtual void generate(const ReportData& data, std::ostream& out) const = 0;
};

struct ReportData {
    std::string topModule;
    ConnectionGraph graph;
    std::vector<Issue> active;
    std::vector<Issue> waived;
};
```

| Generator | Output file | Purpose |
|-----------|------------|---------|
| `JsonReportGenerator` | `connect_report.json` | CI/automation |
| `MarkdownReportGenerator` | `connect_report.md` | Human review |
| `CsvReportGenerator` | `connection_matrix.csv` | Spreadsheet analysis |
| `TableReportGenerator` | stdout | Terminal quick check |

`--format` option: `json`, `md`, `csv`, `table`, `all` (default: `all`).
`--output <dir>`: file output directory (default: `./connect_reports/`).
Table always goes to stdout regardless of `--output`.

## 9. CLI Interface

```
slang-connect [OPTIONS] <SV_FILES...>

Required:
  <SV_FILES...>           SystemVerilog source files or -f <filelist>
  --top <module>          Top-level module (required)

Options:
  -f <filelist>           Source filelist (.f file)
  -o, --output <dir>      Output directory (default: ./connect_reports/)
  --format <fmt>          json|md|csv|table|all (default: all)

Analysis (all enabled by default, use --no-* to disable):
  --no-check-width        Disable width mismatch detection
  --no-check-type         Disable signed/unsigned mismatch
  --no-check-dangling     Disable unconnected output detection
  --no-check-undriven     Disable undriven input detection
  --depth <n>             Hierarchy depth to analyze (default: unlimited)

Filtering:
  --ignore-tie-off        Don't flag tied-off inputs (1'b0, 1'b1)
  --ignore-nc             Don't flag explicitly NC ports
  --waiver <file>         Waiver YAML for known issues

Slang pass-through:
  -I <dir>                Include directory
  -D <macro>=<val>        Define macro
  --std <ver>             SV standard version

Exit codes:
  0                       No issues found (or all waived)
  N                       N issues found (capped at 255)
```

## 10. Project Structure

```
slang-connect/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── ConnectionExtractor.h/cpp
│   ├── ConnectionGraph.h
│   ├── Issue.h
│   ├── Checker.h
│   ├── WidthChecker.h/cpp
│   ├── TypeChecker.h/cpp
│   ├── DanglingChecker.h/cpp
│   ├── UndrivenChecker.h/cpp
│   ├── CheckerRunner.h/cpp
│   ├── WaiverFilter.h/cpp
│   ├── ReportGenerator.h
│   ├── JsonReport.h/cpp
│   ├── MarkdownReport.h/cpp
│   ├── CsvReport.h/cpp
│   └── TableReport.h/cpp
└── tests/
    ├── CMakeLists.txt
    ├── sv/
    │   ├── width_mismatch.sv
    │   ├── type_mismatch.sv
    │   ├── dangling_output.sv
    │   └── undriven_input.sv
    └── test_*.cpp
```

## 11. Build Dependencies

| Library | Purpose | Source |
|---------|---------|--------|
| slang v10 | SV elaboration | Already installed (`~/.local/`) |
| fmt | Formatting | Already present (slang dependency) |
| yaml-cpp | Waiver YAML parsing | CMake FetchContent |
| Catch2 | Unit tests | CMake FetchContent |

## 12. Testing Strategy

| Level | Content |
|-------|---------|
| Unit | Each Checker independently with minimal 2-module SV |
| Width | 32→16 truncation, 16→32 extension, exact match |
| Type | signed→unsigned, unsigned→signed |
| Dangling | connected vs unconnected output |
| Undriven | driven vs undriven input |
| Waiver | pattern match, exact match, wildcard |
| Integration | Multi-module design → expected issue list |
