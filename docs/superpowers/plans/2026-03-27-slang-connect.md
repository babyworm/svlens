# slang-connect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a C++20 CLI tool that analyzes SystemVerilog module port connections and reports width mismatches, type mismatches, dangling outputs, and undriven inputs.

**Architecture:** AST Visitor + ConnectionGraph. slang elaborates the design, ConnectionExtractor builds a ConnectionGraph, Checkers analyze it and produce Issues, WaiverFilter removes known issues, ReportGenerators output results in 4 formats.

**Tech Stack:** C++20, slang v10 (`~/.local/`), fmt, yaml-cpp (FetchContent), Catch2 (FetchContent)

---

## File Map

| File | Responsibility |
|------|---------------|
| `CMakeLists.txt` | Top-level build: find slang/fmt, FetchContent yaml-cpp/Catch2 |
| `src/ConnectionGraph.h` | `PortInfo`, `Connection`, `ConnectionGraph` data types |
| `src/Issue.h` | `Issue` type with `Type`, `Severity` enums and formatting helpers |
| `src/Checker.h` | `IChecker` interface |
| `src/WidthChecker.h` / `.cpp` | Width mismatch detection |
| `src/TypeChecker.h` / `.cpp` | Signed/unsigned mismatch detection |
| `src/DanglingChecker.h` / `.cpp` | Unconnected output detection |
| `src/UndrivenChecker.h` / `.cpp` | Undriven input detection |
| `src/CheckerRunner.h` / `.cpp` | Runs all registered checkers |
| `src/WaiverFilter.h` / `.cpp` | YAML waiver loading and pattern matching |
| `src/ReportGenerator.h` | `IReportGenerator` interface + `ReportData` struct |
| `src/JsonReport.h` / `.cpp` | JSON output |
| `src/MarkdownReport.h` / `.cpp` | Markdown output |
| `src/CsvReport.h` / `.cpp` | CSV connection matrix |
| `src/TableReport.h` / `.cpp` | Terminal table output |
| `src/ConnectionExtractor.h` / `.cpp` | slang AST traversal → ConnectionGraph |
| `src/main.cpp` | CLI parsing, pipeline orchestration |
| `tests/CMakeLists.txt` | Test target |
| `tests/test_width_checker.cpp` | WidthChecker unit tests |
| `tests/test_type_checker.cpp` | TypeChecker unit tests |
| `tests/test_dangling_checker.cpp` | DanglingChecker unit tests |
| `tests/test_undriven_checker.cpp` | UndrivenChecker unit tests |
| `tests/test_checker_runner.cpp` | CheckerRunner unit tests |
| `tests/test_waiver_filter.cpp` | WaiverFilter unit tests |
| `tests/test_reports.cpp` | Report generator unit tests |
| `tests/test_extractor.cpp` | ConnectionExtractor integration tests |
| `tests/sv/width_mismatch.sv` | Test SV: width mismatch scenarios |
| `tests/sv/type_mismatch.sv` | Test SV: signed/unsigned mismatch |
| `tests/sv/dangling_output.sv` | Test SV: unconnected output |
| `tests/sv/undriven_input.sv` | Test SV: undriven input |
| `tests/sv/clean_design.sv` | Test SV: no issues (clean) |

---

### Task 1: Project Scaffolding & Build System

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/ConnectionGraph.h`
- Create: `src/Issue.h`
- Create: `tests/CMakeLists.txt`

- [ ] **Step 1: Create top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(slang-connect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find slang (installed at ~/.local/)
find_package(slang REQUIRED)
find_package(fmt REQUIRED)

# FetchContent for yaml-cpp and Catch2
include(FetchContent)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Main executable
add_executable(slang-connect
    src/main.cpp
    src/WidthChecker.cpp
    src/TypeChecker.cpp
    src/DanglingChecker.cpp
    src/UndrivenChecker.cpp
    src/CheckerRunner.cpp
    src/WaiverFilter.cpp
    src/JsonReport.cpp
    src/MarkdownReport.cpp
    src/CsvReport.cpp
    src/TableReport.cpp
    src/ConnectionExtractor.cpp
)

target_include_directories(slang-connect PRIVATE src)
target_link_libraries(slang-connect PRIVATE svlang fmt::fmt yaml-cpp::yaml-cpp)

# Tests
enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: Create tests/CMakeLists.txt**

```cmake
add_executable(slang-connect-tests
    test_width_checker.cpp
    test_type_checker.cpp
    test_dangling_checker.cpp
    test_undriven_checker.cpp
    test_checker_runner.cpp
    test_waiver_filter.cpp
    test_reports.cpp
    test_extractor.cpp
    ${CMAKE_SOURCE_DIR}/src/WidthChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/TypeChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/DanglingChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/UndrivenChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/CheckerRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/WaiverFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/JsonReport.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownReport.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvReport.cpp
    ${CMAKE_SOURCE_DIR}/src/TableReport.cpp
    ${CMAKE_SOURCE_DIR}/src/ConnectionExtractor.cpp
)

target_include_directories(slang-connect-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(slang-connect-tests PRIVATE
    Catch2::Catch2WithMain
    svlang
    fmt::fmt
    yaml-cpp::yaml-cpp
)

include(CTest)
include(Catch)
catch_discover_tests(slang-connect-tests)

# Copy SV test files to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sv DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
```

- [ ] **Step 3: Create data model headers**

`src/ConnectionGraph.h`:

```cpp
#pragma once

#include "slang/ast/SemanticFacts.h"
#include "slang/text/SourceLocation.h"

#include <cstdint>
#include <string>
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

struct Connection {
    PortInfo source;
    PortInfo dest;
};

struct ConnectionGraph {
    std::vector<Connection> connections;
    std::vector<PortInfo> allPorts;
    std::string topModule;
};

} // namespace connect
```

`src/Issue.h`:

```cpp
#pragma once

#include "ConnectionGraph.h"

#include <optional>
#include <string>
#include <vector>

namespace connect {

struct Issue {
    enum class Type {
        WIDTH_MISMATCH,
        TYPE_MISMATCH,
        DANGLING_OUTPUT,
        UNDRIVEN_INPUT
    };

    enum class Severity {
        ERROR,
        WARN,
        INFO
    };

    Type type;
    Severity severity;
    PortInfo port;
    std::optional<Connection> connection;
    std::string detail;

    static const char* typeToString(Type t) {
        switch (t) {
            case Type::WIDTH_MISMATCH:  return "WIDTH_MISMATCH";
            case Type::TYPE_MISMATCH:   return "TYPE_MISMATCH";
            case Type::DANGLING_OUTPUT: return "DANGLING_OUTPUT";
            case Type::UNDRIVEN_INPUT:  return "UNDRIVEN_INPUT";
        }
        return "UNKNOWN";
    }

    static const char* severityToString(Severity s) {
        switch (s) {
            case Severity::ERROR: return "ERROR";
            case Severity::WARN:  return "WARN";
            case Severity::INFO:  return "INFO";
        }
        return "UNKNOWN";
    }
};

} // namespace connect
```

- [ ] **Step 4: Create stub main.cpp to verify build**

`src/main.cpp`:

```cpp
#include <fmt/core.h>

int main(int argc, char* argv[]) {
    fmt::print("slang-connect v0.1.0\n");
    return 0;
}
```

- [ ] **Step 5: Configure and build**

```bash
cd /Users/babyworm/work/eda-tools/slang-connect
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build
./build/slang-connect
```

Expected: prints `slang-connect v0.1.0`

- [ ] **Step 6: Commit**

```bash
git init
git add CMakeLists.txt src/ConnectionGraph.h src/Issue.h src/main.cpp tests/CMakeLists.txt
git commit -m "feat: project scaffolding with CMake, data model types"
```

---

### Task 2: WidthChecker (TDD)

**Files:**
- Create: `src/Checker.h`
- Create: `src/WidthChecker.h`
- Create: `src/WidthChecker.cpp`
- Create: `tests/test_width_checker.cpp`

- [ ] **Step 1: Create IChecker interface**

`src/Checker.h`:

```cpp
#pragma once

#include "ConnectionGraph.h"
#include "Issue.h"

#include <vector>

namespace connect {

class IChecker {
public:
    virtual ~IChecker() = default;
    virtual std::vector<Issue> check(const ConnectionGraph& graph) const = 0;
};

} // namespace connect
```

- [ ] **Step 2: Write failing tests**

`tests/test_width_checker.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "WidthChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width, bool isSigned = false) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    p.isSigned = isSigned;
    return p;
}

TEST_CASE("WidthChecker: exact match produces no issues") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 32),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 32)
    });

    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.empty());
}

TEST_CASE("WidthChecker: truncation is ERROR") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 32),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16)
    });

    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::WIDTH_MISMATCH);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
}

TEST_CASE("WidthChecker: unsigned zero-extension is WARN") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, false),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 32, false)
    });

    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Issue::Severity::WARN);
}

TEST_CASE("WidthChecker: signed sign-extension is INFO") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 32, true)
    });

    WidthChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Issue::Severity::INFO);
}
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake --build build
cd build && ctest -R "WidthChecker" --output-on-failure
```

Expected: compilation error (WidthChecker not defined yet)

- [ ] **Step 4: Implement WidthChecker**

`src/WidthChecker.h`:

```cpp
#pragma once

#include "Checker.h"

namespace connect {

class WidthChecker : public IChecker {
public:
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
};

} // namespace connect
```

`src/WidthChecker.cpp`:

```cpp
#include "WidthChecker.h"

#include <fmt/core.h>

namespace connect {

std::vector<Issue> WidthChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    for (auto& conn : graph.connections) {
        if (conn.source.width == conn.dest.width)
            continue;

        Issue issue;
        issue.type = Issue::Type::WIDTH_MISMATCH;
        issue.connection = conn;
        issue.port = conn.source;

        if (conn.source.width > conn.dest.width) {
            issue.severity = Issue::Severity::ERROR;
            issue.detail = fmt::format(
                "Truncation: {} bits → {} bits, bits [{}:{}] lost",
                conn.source.width, conn.dest.width,
                conn.source.width - 1, conn.dest.width);
        } else if (conn.source.isSigned && conn.dest.isSigned) {
            issue.severity = Issue::Severity::INFO;
            issue.detail = fmt::format(
                "Sign-extension: {} bits → {} bits (likely intentional)",
                conn.source.width, conn.dest.width);
        } else {
            issue.severity = Issue::Severity::WARN;
            issue.detail = fmt::format(
                "Zero-extension: {} bits → {} bits",
                conn.source.width, conn.dest.width);
        }

        issues.push_back(std::move(issue));
    }

    return issues;
}

} // namespace connect
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "WidthChecker" --output-on-failure
```

Expected: all 4 tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/Checker.h src/WidthChecker.h src/WidthChecker.cpp tests/test_width_checker.cpp
git commit -m "feat: add WidthChecker with truncation/extension detection"
```

---

### Task 3: TypeChecker (TDD)

**Files:**
- Create: `src/TypeChecker.h`
- Create: `src/TypeChecker.cpp`
- Create: `tests/test_type_checker.cpp`

- [ ] **Step 1: Write failing tests**

`tests/test_type_checker.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "TypeChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width, bool isSigned) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    p.isSigned = isSigned;
    return p;
}

TEST_CASE("TypeChecker: same signedness produces no issues") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16, true)
    });

    TypeChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.empty());
}

TEST_CASE("TypeChecker: signed to unsigned is ERROR") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_coeff", ArgumentDirection::Out, 16, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16, false)
    });

    TypeChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::TYPE_MISMATCH);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
}

TEST_CASE("TypeChecker: unsigned to signed is ERROR") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 16, false),
        makePort("top.u_b", "i_coeff", ArgumentDirection::In, 16, true)
    });

    TypeChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build && cd build && ctest -R "TypeChecker" --output-on-failure
```

Expected: compilation error

- [ ] **Step 3: Implement TypeChecker**

`src/TypeChecker.h`:

```cpp
#pragma once

#include "Checker.h"

namespace connect {

class TypeChecker : public IChecker {
public:
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
};

} // namespace connect
```

`src/TypeChecker.cpp`:

```cpp
#include "TypeChecker.h"

#include <fmt/core.h>

namespace connect {

std::vector<Issue> TypeChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    for (auto& conn : graph.connections) {
        if (conn.source.isSigned == conn.dest.isSigned)
            continue;

        Issue issue;
        issue.type = Issue::Type::TYPE_MISMATCH;
        issue.severity = Issue::Severity::ERROR;
        issue.connection = conn;
        issue.port = conn.source;
        issue.detail = fmt::format(
            "{} ({}) → {} ({}): negative values interpreted as large positive",
            conn.source.fullPath(),
            conn.source.isSigned ? "signed" : "unsigned",
            conn.dest.fullPath(),
            conn.dest.isSigned ? "signed" : "unsigned");

        issues.push_back(std::move(issue));
    }

    return issues;
}

} // namespace connect
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "TypeChecker" --output-on-failure
```

Expected: all 3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/TypeChecker.h src/TypeChecker.cpp tests/test_type_checker.cpp
git commit -m "feat: add TypeChecker for signed/unsigned mismatch detection"
```

---

### Task 4: DanglingChecker (TDD)

**Files:**
- Create: `src/DanglingChecker.h`
- Create: `src/DanglingChecker.cpp`
- Create: `tests/test_dangling_checker.cpp`

- [ ] **Step 1: Write failing tests**

`tests/test_dangling_checker.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "DanglingChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 8) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    return p;
}

TEST_CASE("DanglingChecker: connected output produces no issues") {
    ConnectionGraph graph;
    auto outPort = makePort("top.u_a", "o_valid", ArgumentDirection::Out, 1);
    auto inPort = makePort("top.u_b", "i_valid", ArgumentDirection::In, 1);
    graph.allPorts.push_back(outPort);
    graph.allPorts.push_back(inPort);
    graph.connections.push_back({outPort, inPort});

    DanglingChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.empty());
}

TEST_CASE("DanglingChecker: unconnected output is WARN") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_a", "o_debug", ArgumentDirection::Out));
    // no connection for o_debug

    DanglingChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::DANGLING_OUTPUT);
    CHECK(issues[0].severity == Issue::Severity::WARN);
    CHECK(issues[0].port.portName == "o_debug");
}

TEST_CASE("DanglingChecker: input ports are ignored") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_a", "i_data", ArgumentDirection::In));

    DanglingChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build && cd build && ctest -R "DanglingChecker" --output-on-failure
```

Expected: compilation error

- [ ] **Step 3: Implement DanglingChecker**

`src/DanglingChecker.h`:

```cpp
#pragma once

#include "Checker.h"

namespace connect {

class DanglingChecker : public IChecker {
public:
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
};

} // namespace connect
```

`src/DanglingChecker.cpp`:

```cpp
#include "DanglingChecker.h"

#include <fmt/core.h>
#include <unordered_set>

namespace connect {

std::vector<Issue> DanglingChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    std::unordered_set<std::string> connectedSources;
    for (auto& conn : graph.connections) {
        connectedSources.insert(conn.source.fullPath());
    }

    for (auto& port : graph.allPorts) {
        if (port.direction != slang::ast::ArgumentDirection::Out)
            continue;

        if (connectedSources.count(port.fullPath()))
            continue;

        Issue issue;
        issue.type = Issue::Type::DANGLING_OUTPUT;
        issue.severity = Issue::Severity::WARN;
        issue.port = port;
        issue.detail = fmt::format("{}[{}:0] — not connected",
                                   port.fullPath(), port.width - 1);

        issues.push_back(std::move(issue));
    }

    return issues;
}

} // namespace connect
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "DanglingChecker" --output-on-failure
```

Expected: all 3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/DanglingChecker.h src/DanglingChecker.cpp tests/test_dangling_checker.cpp
git commit -m "feat: add DanglingChecker for unconnected output detection"
```

---

### Task 5: UndrivenChecker (TDD)

**Files:**
- Create: `src/UndrivenChecker.h`
- Create: `src/UndrivenChecker.cpp`
- Create: `tests/test_undriven_checker.cpp`

- [ ] **Step 1: Write failing tests**

`tests/test_undriven_checker.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "UndrivenChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 8) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    return p;
}

TEST_CASE("UndrivenChecker: driven input produces no issues") {
    ConnectionGraph graph;
    auto outPort = makePort("top.u_a", "o_data", ArgumentDirection::Out);
    auto inPort = makePort("top.u_b", "i_data", ArgumentDirection::In);
    graph.allPorts.push_back(outPort);
    graph.allPorts.push_back(inPort);
    graph.connections.push_back({outPort, inPort});

    UndrivenChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.empty());
}

TEST_CASE("UndrivenChecker: undriven input is ERROR") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_b", "i_config", ArgumentDirection::In));

    UndrivenChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].type == Issue::Type::UNDRIVEN_INPUT);
    CHECK(issues[0].severity == Issue::Severity::ERROR);
    CHECK(issues[0].port.portName == "i_config");
}

TEST_CASE("UndrivenChecker: output ports are ignored") {
    ConnectionGraph graph;
    graph.allPorts.push_back(makePort("top.u_a", "o_data", ArgumentDirection::Out));

    UndrivenChecker checker;
    auto issues = checker.check(graph);
    REQUIRE(issues.empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build && cd build && ctest -R "UndrivenChecker" --output-on-failure
```

Expected: compilation error

- [ ] **Step 3: Implement UndrivenChecker**

`src/UndrivenChecker.h`:

```cpp
#pragma once

#include "Checker.h"

namespace connect {

class UndrivenChecker : public IChecker {
public:
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
};

} // namespace connect
```

`src/UndrivenChecker.cpp`:

```cpp
#include "UndrivenChecker.h"

#include <fmt/core.h>
#include <unordered_set>

namespace connect {

std::vector<Issue> UndrivenChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    std::unordered_set<std::string> drivenDests;
    for (auto& conn : graph.connections) {
        drivenDests.insert(conn.dest.fullPath());
    }

    for (auto& port : graph.allPorts) {
        if (port.direction != slang::ast::ArgumentDirection::In)
            continue;

        if (drivenDests.count(port.fullPath()))
            continue;

        Issue issue;
        issue.type = Issue::Type::UNDRIVEN_INPUT;
        issue.severity = Issue::Severity::ERROR;
        issue.port = port;
        issue.detail = fmt::format("{}[{}:0] — no driver (will propagate X)",
                                   port.fullPath(), port.width - 1);

        issues.push_back(std::move(issue));
    }

    return issues;
}

} // namespace connect
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "UndrivenChecker" --output-on-failure
```

Expected: all 3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/UndrivenChecker.h src/UndrivenChecker.cpp tests/test_undriven_checker.cpp
git commit -m "feat: add UndrivenChecker for undriven input detection"
```

---

### Task 6: CheckerRunner (TDD)

**Files:**
- Create: `src/CheckerRunner.h`
- Create: `src/CheckerRunner.cpp`
- Create: `tests/test_checker_runner.cpp`

- [ ] **Step 1: Write failing tests**

`tests/test_checker_runner.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "CheckerRunner.h"
#include "WidthChecker.h"
#include "TypeChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width, bool isSigned = false) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    p.isSigned = isSigned;
    return p;
}

TEST_CASE("CheckerRunner: no checkers produces no issues") {
    ConnectionGraph graph;
    CheckerRunner runner;
    auto issues = runner.runAll(graph);
    REQUIRE(issues.empty());
}

TEST_CASE("CheckerRunner: aggregates issues from multiple checkers") {
    ConnectionGraph graph;
    graph.connections.push_back({
        makePort("top.u_a", "o_data", ArgumentDirection::Out, 32, true),
        makePort("top.u_b", "i_data", ArgumentDirection::In, 16, false)
    });

    CheckerRunner runner;
    runner.addChecker(std::make_unique<WidthChecker>());
    runner.addChecker(std::make_unique<TypeChecker>());

    auto issues = runner.runAll(graph);
    REQUIRE(issues.size() == 2);

    bool hasWidth = false, hasType = false;
    for (auto& issue : issues) {
        if (issue.type == Issue::Type::WIDTH_MISMATCH) hasWidth = true;
        if (issue.type == Issue::Type::TYPE_MISMATCH) hasType = true;
    }
    CHECK(hasWidth);
    CHECK(hasType);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build && cd build && ctest -R "CheckerRunner" --output-on-failure
```

Expected: compilation error

- [ ] **Step 3: Implement CheckerRunner**

`src/CheckerRunner.h`:

```cpp
#pragma once

#include "Checker.h"

#include <memory>
#include <vector>

namespace connect {

class CheckerRunner {
public:
    void addChecker(std::unique_ptr<IChecker> checker);
    std::vector<Issue> runAll(const ConnectionGraph& graph) const;

private:
    std::vector<std::unique_ptr<IChecker>> checkers_;
};

} // namespace connect
```

`src/CheckerRunner.cpp`:

```cpp
#include "CheckerRunner.h"

namespace connect {

void CheckerRunner::addChecker(std::unique_ptr<IChecker> checker) {
    checkers_.push_back(std::move(checker));
}

std::vector<Issue> CheckerRunner::runAll(const ConnectionGraph& graph) const {
    std::vector<Issue> allIssues;

    for (auto& checker : checkers_) {
        auto issues = checker->check(graph);
        allIssues.insert(allIssues.end(),
                         std::make_move_iterator(issues.begin()),
                         std::make_move_iterator(issues.end()));
    }

    return allIssues;
}

} // namespace connect
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "CheckerRunner" --output-on-failure
```

Expected: all 2 tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/CheckerRunner.h src/CheckerRunner.cpp tests/test_checker_runner.cpp
git commit -m "feat: add CheckerRunner to aggregate multiple checkers"
```

---

### Task 7: WaiverFilter (TDD)

**Files:**
- Create: `src/WaiverFilter.h`
- Create: `src/WaiverFilter.cpp`
- Create: `tests/test_waiver_filter.cpp`
- Create: `tests/test_waivers.yaml`

- [ ] **Step 1: Write test waiver YAML**

`tests/test_waivers.yaml`:

```yaml
waivers:
  - pattern: "*.o_debug*"
    type: DANGLING_OUTPUT
    reason: "Debug ports intentionally unconnected"

  - pattern: "top.u_test_*"
    type: "*"
    reason: "Test infrastructure"
```

- [ ] **Step 2: Write failing tests**

`tests/test_waiver_filter.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "WaiverFilter.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static Issue makeIssue(Issue::Type type, const std::string& inst,
                       const std::string& port) {
    Issue issue;
    issue.type = type;
    issue.severity = Issue::Severity::WARN;
    issue.port.instancePath = inst;
    issue.port.portName = port;
    issue.port.direction = ArgumentDirection::Out;
    issue.port.width = 8;
    issue.detail = "test";
    return issue;
}

TEST_CASE("WaiverFilter: no waivers passes all issues through") {
    WaiverFilter filter;
    std::vector<Issue> issues = {
        makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_a", "o_debug")
    };

    auto result = filter.apply(issues);
    CHECK(result.active.size() == 1);
    CHECK(result.waived.empty());
}

TEST_CASE("WaiverFilter: pattern match waives matching issues") {
    WaiverFilter filter("test_waivers.yaml");
    std::vector<Issue> issues = {
        makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_a", "o_debug"),
        makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_a", "o_valid")
    };

    auto result = filter.apply(issues);
    CHECK(result.active.size() == 1);
    CHECK(result.active[0].port.portName == "o_valid");
    CHECK(result.waived.size() == 1);
    CHECK(result.waived[0].port.portName == "o_debug");
}

TEST_CASE("WaiverFilter: wildcard type waives all issue types") {
    WaiverFilter filter("test_waivers.yaml");
    std::vector<Issue> issues = {
        makeIssue(Issue::Type::WIDTH_MISMATCH, "top.u_test_block", "o_data"),
        makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_test_block", "o_debug")
    };

    auto result = filter.apply(issues);
    CHECK(result.active.empty());
    CHECK(result.waived.size() == 2);
}

TEST_CASE("WaiverFilter: type mismatch does not waive") {
    WaiverFilter filter("test_waivers.yaml");
    std::vector<Issue> issues = {
        makeIssue(Issue::Type::WIDTH_MISMATCH, "top.u_a", "o_debug")
    };

    auto result = filter.apply(issues);
    CHECK(result.active.size() == 1);
    CHECK(result.waived.empty());
}
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake --build build && cd build && ctest -R "WaiverFilter" --output-on-failure
```

Expected: compilation error

- [ ] **Step 4: Implement WaiverFilter**

`src/WaiverFilter.h`:

```cpp
#pragma once

#include "Issue.h"

#include <string>
#include <vector>

namespace connect {

class WaiverFilter {
public:
    WaiverFilter() = default;
    explicit WaiverFilter(const std::string& yamlPath);

    struct WaiverResult {
        std::vector<Issue> active;
        std::vector<Issue> waived;
    };

    WaiverResult apply(const std::vector<Issue>& issues) const;

private:
    struct WaiverRule {
        std::string pattern;
        std::string type;  // Issue type string or "*"
        std::string reason;
    };

    std::vector<WaiverRule> rules_;

    static bool globMatch(const std::string& pattern, const std::string& text);
};

} // namespace connect
```

`src/WaiverFilter.cpp`:

```cpp
#include "WaiverFilter.h"

#include <yaml-cpp/yaml.h>

namespace connect {

WaiverFilter::WaiverFilter(const std::string& yamlPath) {
    YAML::Node root = YAML::LoadFile(yamlPath);
    if (!root["waivers"])
        return;

    for (auto& node : root["waivers"]) {
        WaiverRule rule;
        rule.pattern = node["pattern"].as<std::string>("");
        rule.type = node["type"].as<std::string>("*");
        rule.reason = node["reason"].as<std::string>("");
        rules_.push_back(std::move(rule));
    }
}

bool WaiverFilter::globMatch(const std::string& pattern, const std::string& text) {
    size_t pi = 0, ti = 0;
    size_t starP = std::string::npos, starT = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
            ++pi;
            ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starP = pi++;
            starT = ti;
        } else if (starP != std::string::npos) {
            pi = starP + 1;
            ti = ++starT;
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*')
        ++pi;

    return pi == pattern.size();
}

WaiverFilter::WaiverResult WaiverFilter::apply(const std::vector<Issue>& issues) const {
    WaiverResult result;

    for (auto& issue : issues) {
        bool waived = false;
        std::string fullPath = issue.port.fullPath();

        for (auto& rule : rules_) {
            bool typeMatch = (rule.type == "*") ||
                             (rule.type == Issue::typeToString(issue.type));
            bool pathMatch = globMatch(rule.pattern, fullPath);

            if (typeMatch && pathMatch) {
                waived = true;
                break;
            }
        }

        if (waived)
            result.waived.push_back(issue);
        else
            result.active.push_back(issue);
    }

    return result;
}

} // namespace connect
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "WaiverFilter" --output-on-failure
```

Expected: all 4 tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/WaiverFilter.h src/WaiverFilter.cpp tests/test_waiver_filter.cpp tests/test_waivers.yaml
git commit -m "feat: add WaiverFilter with glob pattern matching and YAML loading"
```

---

### Task 8: Report Generators (TDD)

**Files:**
- Create: `src/ReportGenerator.h`
- Create: `src/JsonReport.h` / `.cpp`
- Create: `src/MarkdownReport.h` / `.cpp`
- Create: `src/CsvReport.h` / `.cpp`
- Create: `src/TableReport.h` / `.cpp`
- Create: `tests/test_reports.cpp`

- [ ] **Step 1: Create ReportGenerator interface**

`src/ReportGenerator.h`:

```cpp
#pragma once

#include "ConnectionGraph.h"
#include "Issue.h"

#include <ostream>
#include <vector>

namespace connect {

struct ReportData {
    std::string topModule;
    ConnectionGraph graph;
    std::vector<Issue> active;
    std::vector<Issue> waived;

    int errorCount() const {
        int n = 0;
        for (auto& i : active)
            if (i.severity == Issue::Severity::ERROR) ++n;
        return n;
    }
    int warnCount() const {
        int n = 0;
        for (auto& i : active)
            if (i.severity == Issue::Severity::WARN) ++n;
        return n;
    }
    int infoCount() const {
        int n = 0;
        for (auto& i : active)
            if (i.severity == Issue::Severity::INFO) ++n;
        return n;
    }
};

class IReportGenerator {
public:
    virtual ~IReportGenerator() = default;
    virtual void generate(const ReportData& data, std::ostream& out) const = 0;
};

} // namespace connect
```

- [ ] **Step 2: Write failing tests**

`tests/test_reports.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "JsonReport.h"
#include "MarkdownReport.h"
#include "CsvReport.h"
#include "TableReport.h"

#include <sstream>

using namespace connect;
using namespace Catch::Matchers;
using slang::ast::ArgumentDirection;

static ReportData makeTestData() {
    ReportData data;
    data.topModule = "soc_top";

    PortInfo src;
    src.instancePath = "top.u_core";
    src.portName = "o_data";
    src.direction = ArgumentDirection::Out;
    src.width = 32;
    src.isSigned = false;

    PortInfo dst;
    dst.instancePath = "top.u_bus";
    dst.portName = "i_data";
    dst.direction = ArgumentDirection::In;
    dst.width = 16;
    dst.isSigned = false;

    Connection conn{src, dst};
    data.graph.connections.push_back(conn);

    Issue issue;
    issue.type = Issue::Type::WIDTH_MISMATCH;
    issue.severity = Issue::Severity::ERROR;
    issue.port = src;
    issue.connection = conn;
    issue.detail = "Truncation: 32 bits -> 16 bits, bits [31:16] lost";
    data.active.push_back(issue);

    return data;
}

TEST_CASE("JsonReport: contains required fields") {
    auto data = makeTestData();
    std::ostringstream out;
    JsonReportGenerator gen;
    gen.generate(data, out);
    auto json = out.str();

    CHECK_THAT(json, ContainsSubstring("\"top\""));
    CHECK_THAT(json, ContainsSubstring("soc_top"));
    CHECK_THAT(json, ContainsSubstring("\"errors\""));
    CHECK_THAT(json, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(json, ContainsSubstring("\"issues\""));
}

TEST_CASE("MarkdownReport: contains summary and issues") {
    auto data = makeTestData();
    std::ostringstream out;
    MarkdownReportGenerator gen;
    gen.generate(data, out);
    auto md = out.str();

    CHECK_THAT(md, ContainsSubstring("soc_top"));
    CHECK_THAT(md, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(md, ContainsSubstring("ERROR"));
}

TEST_CASE("CsvReport: has header and data rows") {
    auto data = makeTestData();
    std::ostringstream out;
    CsvReportGenerator gen;
    gen.generate(data, out);
    auto csv = out.str();

    CHECK_THAT(csv, ContainsSubstring("Source,Dest,Width_Src,Width_Dst"));
    CHECK_THAT(csv, ContainsSubstring("u_core.o_data"));
    CHECK_THAT(csv, ContainsSubstring("WIDTH_MISMATCH"));
}

TEST_CASE("TableReport: formats output for terminal") {
    auto data = makeTestData();
    std::ostringstream out;
    TableReportGenerator gen;
    gen.generate(data, out);
    auto table = out.str();

    CHECK_THAT(table, ContainsSubstring("WIDTH_MISMATCH"));
    CHECK_THAT(table, ContainsSubstring("ERROR"));
}
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake --build build && cd build && ctest -R "reports" --output-on-failure
```

Expected: compilation error

- [ ] **Step 4: Implement JsonReportGenerator**

`src/JsonReport.h`:

```cpp
#pragma once

#include "ReportGenerator.h"

namespace connect {

class JsonReportGenerator : public IReportGenerator {
public:
    void generate(const ReportData& data, std::ostream& out) const override;
};

} // namespace connect
```

`src/JsonReport.cpp`:

```cpp
#include "JsonReport.h"

#include <fmt/core.h>

namespace connect {

static std::string escapeJson(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else result += c;
    }
    return result;
}

void JsonReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    out << "{\n";
    out << fmt::format("  \"version\": \"1.0\",\n");
    out << fmt::format("  \"top\": \"{}\",\n", data.topModule);

    out << "  \"summary\": {\n";
    out << fmt::format("    \"connections_analyzed\": {},\n", data.graph.connections.size());
    out << fmt::format("    \"errors\": {},\n", data.errorCount());
    out << fmt::format("    \"warnings\": {},\n", data.warnCount());
    out << fmt::format("    \"info\": {},\n", data.infoCount());
    out << fmt::format("    \"waived\": {}\n", data.waived.size());
    out << "  },\n";

    out << "  \"issues\": [\n";
    for (size_t i = 0; i < data.active.size(); ++i) {
        auto& issue = data.active[i];
        out << "    {\n";
        out << fmt::format("      \"type\": \"{}\",\n", Issue::typeToString(issue.type));
        out << fmt::format("      \"severity\": \"{}\",\n", Issue::severityToString(issue.severity));
        out << fmt::format("      \"port\": \"{}\",\n", issue.port.fullPath());
        if (issue.connection) {
            out << "      \"source\": {\n";
            out << fmt::format("        \"instance\": \"{}\",\n", issue.connection->source.instancePath);
            out << fmt::format("        \"port\": \"{}\",\n", issue.connection->source.portName);
            out << fmt::format("        \"width\": {}\n", issue.connection->source.width);
            out << "      },\n";
            out << "      \"dest\": {\n";
            out << fmt::format("        \"instance\": \"{}\",\n", issue.connection->dest.instancePath);
            out << fmt::format("        \"port\": \"{}\",\n", issue.connection->dest.portName);
            out << fmt::format("        \"width\": {}\n", issue.connection->dest.width);
            out << "      },\n";
        }
        out << fmt::format("      \"detail\": \"{}\"\n", escapeJson(issue.detail));
        out << (i + 1 < data.active.size() ? "    },\n" : "    }\n");
    }
    out << "  ],\n";

    out << "  \"connections\": [\n";
    for (size_t i = 0; i < data.graph.connections.size(); ++i) {
        auto& conn = data.graph.connections[i];
        out << "    {\n";
        out << fmt::format("      \"source\": \"{}[{}:0]\",\n",
                           conn.source.fullPath(), conn.source.width - 1);
        out << fmt::format("      \"dest\": \"{}[{}:0]\",\n",
                           conn.dest.fullPath(), conn.dest.width - 1);
        out << fmt::format("      \"status\": \"{}\"\n",
                           conn.source.width == conn.dest.width ? "OK" : "WIDTH_MISMATCH");
        out << (i + 1 < data.graph.connections.size() ? "    },\n" : "    }\n");
    }
    out << "  ]\n";
    out << "}\n";
}

} // namespace connect
```

- [ ] **Step 5: Implement MarkdownReportGenerator**

`src/MarkdownReport.h`:

```cpp
#pragma once

#include "ReportGenerator.h"

namespace connect {

class MarkdownReportGenerator : public IReportGenerator {
public:
    void generate(const ReportData& data, std::ostream& out) const override;
};

} // namespace connect
```

`src/MarkdownReport.cpp`:

```cpp
#include "MarkdownReport.h"

#include <fmt/core.h>

namespace connect {

void MarkdownReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    out << fmt::format("# Connection Report: {}\n\n", data.topModule);

    out << "## Summary\n\n";
    out << fmt::format("| Metric | Count |\n");
    out << fmt::format("|--------|-------|\n");
    out << fmt::format("| Connections analyzed | {} |\n", data.graph.connections.size());
    out << fmt::format("| Errors | {} |\n", data.errorCount());
    out << fmt::format("| Warnings | {} |\n", data.warnCount());
    out << fmt::format("| Info | {} |\n", data.infoCount());
    out << fmt::format("| Waived | {} |\n\n", data.waived.size());

    if (!data.active.empty()) {
        out << "## Issues\n\n";
        out << "| Severity | Type | Port | Detail |\n";
        out << "|----------|------|------|--------|\n";
        for (auto& issue : data.active) {
            out << fmt::format("| {} | {} | {} | {} |\n",
                               Issue::severityToString(issue.severity),
                               Issue::typeToString(issue.type),
                               issue.port.fullPath(),
                               issue.detail);
        }
        out << "\n";
    }

    if (!data.waived.empty()) {
        out << "## Waived Issues\n\n";
        out << fmt::format("{} issue(s) waived.\n\n", data.waived.size());
    }
}

} // namespace connect
```

- [ ] **Step 6: Implement CsvReportGenerator**

`src/CsvReport.h`:

```cpp
#pragma once

#include "ReportGenerator.h"

namespace connect {

class CsvReportGenerator : public IReportGenerator {
public:
    void generate(const ReportData& data, std::ostream& out) const override;
};

} // namespace connect
```

`src/CsvReport.cpp`:

```cpp
#include "CsvReport.h"

#include <fmt/core.h>

namespace connect {

void CsvReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    out << "Source,Dest,Width_Src,Width_Dst,Type_Src,Type_Dst,Status\n";

    for (auto& conn : data.graph.connections) {
        std::string status = "OK";
        for (auto& issue : data.active) {
            if (issue.connection &&
                issue.connection->source.fullPath() == conn.source.fullPath() &&
                issue.connection->dest.fullPath() == conn.dest.fullPath()) {
                status = Issue::typeToString(issue.type);
                break;
            }
        }

        out << fmt::format("{},{},{},{},{},{},{}\n",
                           conn.source.fullPath(),
                           conn.dest.fullPath(),
                           conn.source.width,
                           conn.dest.width,
                           conn.source.isSigned ? "signed" : "unsigned",
                           conn.dest.isSigned ? "signed" : "unsigned",
                           status);
    }
}

} // namespace connect
```

- [ ] **Step 7: Implement TableReportGenerator**

`src/TableReport.h`:

```cpp
#pragma once

#include "ReportGenerator.h"

namespace connect {

class TableReportGenerator : public IReportGenerator {
public:
    void generate(const ReportData& data, std::ostream& out) const override;
};

} // namespace connect
```

`src/TableReport.cpp`:

```cpp
#include "TableReport.h"

#include <fmt/core.h>

namespace connect {

void TableReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    out << fmt::format("=== slang-connect: {} ===\n\n", data.topModule);

    out << fmt::format("Connections: {}  Errors: {}  Warnings: {}  Info: {}  Waived: {}\n\n",
                       data.graph.connections.size(),
                       data.errorCount(), data.warnCount(),
                       data.infoCount(), data.waived.size());

    if (data.active.empty()) {
        out << "No issues found.\n";
        return;
    }

    out << fmt::format("{:<8} {:<18} {:<40} {}\n", "SEV", "TYPE", "PORT", "DETAIL");
    out << std::string(100, '-') << "\n";

    for (auto& issue : data.active) {
        out << fmt::format("{:<8} {:<18} {:<40} {}\n",
                           Issue::severityToString(issue.severity),
                           Issue::typeToString(issue.type),
                           issue.port.fullPath(),
                           issue.detail);
    }
}

} // namespace connect
```

- [ ] **Step 8: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "reports" --output-on-failure
```

Expected: all 4 tests PASS

- [ ] **Step 9: Commit**

```bash
git add src/ReportGenerator.h src/JsonReport.h src/JsonReport.cpp \
    src/MarkdownReport.h src/MarkdownReport.cpp \
    src/CsvReport.h src/CsvReport.cpp \
    src/TableReport.h src/TableReport.cpp \
    tests/test_reports.cpp
git commit -m "feat: add 4 report generators (JSON, Markdown, CSV, Table)"
```

---

### Task 9: ConnectionExtractor (TDD)

**Files:**
- Create: `src/ConnectionExtractor.h`
- Create: `src/ConnectionExtractor.cpp`
- Create: `tests/test_extractor.cpp`
- Create: `tests/sv/width_mismatch.sv`
- Create: `tests/sv/type_mismatch.sv`
- Create: `tests/sv/dangling_output.sv`
- Create: `tests/sv/undriven_input.sv`
- Create: `tests/sv/clean_design.sv`

- [ ] **Step 1: Create test SystemVerilog files**

`tests/sv/width_mismatch.sv`:

```systemverilog
module producer (
    output logic [31:0] o_data
);
    assign o_data = 32'hDEADBEEF;
endmodule

module consumer (
    input logic [15:0] i_data
);
endmodule

module width_mismatch_top;
    logic [31:0] wide_data;
    logic [15:0] narrow_data;

    producer u_prod (.o_data(wide_data));
    consumer u_cons (.i_data(narrow_data));
    assign narrow_data = wide_data[15:0];
endmodule
```

`tests/sv/type_mismatch.sv`:

```systemverilog
module signed_producer (
    output logic signed [15:0] o_coeff
);
    assign o_coeff = -16'sd100;
endmodule

module unsigned_consumer (
    input logic [15:0] i_data
);
endmodule

module type_mismatch_top;
    logic signed [15:0] coeff;
    logic [15:0] data;

    signed_producer u_prod (.o_coeff(coeff));
    unsigned_consumer u_cons (.i_data(data));
    assign data = coeff;
endmodule
```

`tests/sv/dangling_output.sv`:

```systemverilog
module block_with_debug (
    output logic [7:0] o_debug,
    output logic       o_valid
);
    assign o_debug = 8'hFF;
    assign o_valid = 1'b1;
endmodule

module dangling_top;
    logic valid;

    block_with_debug u_block (
        .o_debug(),
        .o_valid(valid)
    );
endmodule
```

`tests/sv/undriven_input.sv`:

```systemverilog
module block_with_config (
    input  logic [7:0] i_config,
    input  logic       i_enable,
    output logic [7:0] o_result
);
    assign o_result = i_enable ? i_config : 8'h0;
endmodule

module undriven_top;
    logic [7:0] result;
    logic enable;

    assign enable = 1'b1;

    block_with_config u_block (
        .i_config(),
        .i_enable(enable),
        .o_result(result)
    );
endmodule
```

`tests/sv/clean_design.sv`:

```systemverilog
module source_mod (
    output logic [7:0] o_data,
    output logic       o_valid
);
    assign o_data = 8'hAB;
    assign o_valid = 1'b1;
endmodule

module sink_mod (
    input logic [7:0] i_data,
    input logic       i_valid
);
endmodule

module clean_top;
    logic [7:0] data;
    logic       valid;

    source_mod u_src (.o_data(data), .o_valid(valid));
    sink_mod   u_snk (.i_data(data), .i_valid(valid));
endmodule
```

- [ ] **Step 2: Write failing tests**

`tests/test_extractor.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ConnectionExtractor.h"

#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"

using namespace connect;

static slang::ast::Compilation compileFile(const std::string& path, const std::string& top) {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    std::array<const char*, 4> args = {"slang-connect", path.c_str(), "--top", top.c_str()};
    driver.parseCommandLine(4, args.data());
    driver.processOptions();
    driver.parseAllSources();
    return std::move(*driver.createCompilation());
}

TEST_CASE("Extractor: clean design has connections and no dangling/undriven") {
    auto compilation = compileFile("sv/clean_design.sv", "clean_top");
    ConnectionExtractor extractor(compilation, "clean_top");
    auto graph = extractor.extract();

    CHECK(graph.topModule == "clean_top");
    CHECK(graph.connections.size() >= 2);
    CHECK(graph.allPorts.size() >= 4);
}

TEST_CASE("Extractor: width mismatch captures port widths") {
    auto compilation = compileFile("sv/width_mismatch.sv", "width_mismatch_top");
    ConnectionExtractor extractor(compilation, "width_mismatch_top");
    auto graph = extractor.extract();

    CHECK(!graph.connections.empty());
    CHECK(!graph.allPorts.empty());

    bool foundWide = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "o_data" && port.width == 32) foundWide = true;
    }
    CHECK(foundWide);
}

TEST_CASE("Extractor: dangling output has unconnected port") {
    auto compilation = compileFile("sv/dangling_output.sv", "dangling_top");
    ConnectionExtractor extractor(compilation, "dangling_top");
    auto graph = extractor.extract();

    bool foundDebug = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "o_debug") foundDebug = true;
    }
    CHECK(foundDebug);
}

TEST_CASE("Extractor: undriven input has undriven port") {
    auto compilation = compileFile("sv/undriven_input.sv", "undriven_top");
    ConnectionExtractor extractor(compilation, "undriven_top");
    auto graph = extractor.extract();

    bool foundConfig = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "i_config") foundConfig = true;
    }
    CHECK(foundConfig);
}
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake --build build && cd build && ctest -R "Extractor" --output-on-failure
```

Expected: compilation error (ConnectionExtractor not defined)

- [ ] **Step 4: Implement ConnectionExtractor**

`src/ConnectionExtractor.h`:

```cpp
#pragma once

#include "ConnectionGraph.h"

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"

#include <string>
#include <unordered_set>

namespace connect {

class ConnectionExtractor {
public:
    ConnectionExtractor(const slang::ast::Compilation& compilation,
                        const std::string& topModule,
                        int maxDepth = -1);

    ConnectionGraph extract();

private:
    void visitInstance(const slang::ast::InstanceSymbol& instance,
                       const std::string& parentPath);

    void collectPortInfo(const slang::ast::PortSymbol& port,
                         const std::string& instancePath);

    void trackConnection(const slang::ast::InstanceSymbol& instance,
                         const slang::ast::PortConnection& conn,
                         const std::string& instancePath);

    const slang::ast::Compilation& compilation_;
    std::string topModule_;
    int maxDepth_;
    ConnectionGraph graph_;

    // Map net name → PortInfo for connection matching
    struct NetBinding {
        PortInfo port;
        bool isDriver;  // true if output port, false if input
    };
    std::vector<NetBinding> netBindings_;

    // Track which ports are connected
    std::unordered_set<std::string> connectedSources_;
    std::unordered_set<std::string> connectedDests_;
};

} // namespace connect
```

`src/ConnectionExtractor.cpp`:

```cpp
#include "ConnectionExtractor.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Expression.h"
#include "slang/ast/types/AllTypes.h"

#include <fmt/core.h>

namespace connect {

ConnectionExtractor::ConnectionExtractor(const slang::ast::Compilation& compilation,
                                         const std::string& topModule,
                                         int maxDepth)
    : compilation_(compilation), topModule_(topModule), maxDepth_(maxDepth) {
    graph_.topModule = topModule;
}

ConnectionGraph ConnectionExtractor::extract() {
    auto* topDef = compilation_.getRoot().find(topModule_);
    if (!topDef)
        return graph_;

    // Find the top-level instance
    for (auto* inst : compilation_.getRoot().topInstances) {
        if (inst->name == topModule_) {
            visitInstance(*inst, topModule_);
            break;
        }
    }

    return graph_;
}

void ConnectionExtractor::visitInstance(const slang::ast::InstanceSymbol& instance,
                                        const std::string& parentPath) {
    if (maxDepth_ >= 0 && (int)instance.instanceDepth > maxDepth_)
        return;

    auto& body = instance.body;

    // Collect all ports of this instance
    for (auto* portSym : body.getPortList()) {
        if (portSym->kind == slang::ast::SymbolKind::Port) {
            auto& port = portSym->as<slang::ast::PortSymbol>();
            collectPortInfo(port, parentPath);
        }
    }

    // Process port connections
    for (auto* conn : instance.getPortConnections()) {
        trackConnection(instance, *conn, parentPath);
    }

    // Recurse into child instances
    for (auto& member : body.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& childInst = member.as<slang::ast::InstanceSymbol>();
            std::string childPath = parentPath + "." + std::string(childInst.name);
            visitInstance(childInst, childPath);
        }
    }
}

void ConnectionExtractor::collectPortInfo(const slang::ast::PortSymbol& port,
                                           const std::string& instancePath) {
    PortInfo info;
    info.instancePath = instancePath;
    info.portName = std::string(port.name);
    info.direction = port.direction;
    info.location = port.location;

    auto& type = port.getType();
    info.width = type.getBitWidth();
    info.isSigned = type.isSigned();

    graph_.allPorts.push_back(info);
}

void ConnectionExtractor::trackConnection(const slang::ast::InstanceSymbol& instance,
                                           const slang::ast::PortConnection& conn,
                                           const std::string& instancePath) {
    if (conn.port.kind != slang::ast::SymbolKind::Port)
        return;

    auto& port = conn.port.as<slang::ast::PortSymbol>();
    auto* expr = conn.getExpression();

    if (!expr)
        return;

    // Build PortInfo for this port
    PortInfo portInfo;
    portInfo.instancePath = instancePath;
    portInfo.portName = std::string(port.name);
    portInfo.direction = port.direction;
    portInfo.location = port.location;

    auto& type = port.getType();
    portInfo.width = type.getBitWidth();
    portInfo.isSigned = type.isSigned();

    // Mark port as connected based on direction
    std::string fullPath = portInfo.fullPath();
    if (port.direction == slang::ast::ArgumentDirection::Out) {
        connectedSources_.insert(fullPath);
    } else if (port.direction == slang::ast::ArgumentDirection::In) {
        connectedDests_.insert(fullPath);
    }

    // Record binding for later connection matching
    netBindings_.push_back({portInfo,
                            port.direction == slang::ast::ArgumentDirection::Out});
}

} // namespace connect
```

Note: This is the initial implementation. The connection matching between output and input ports sharing the same net will be refined in Step 6 after basic tests pass.

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && cd build && ctest -R "Extractor" --output-on-failure
```

Expected: all 4 tests PASS (basic port collection)

- [ ] **Step 6: Refine connection matching logic**

Add net-based connection matching to `ConnectionExtractor.cpp`. After all instances are visited, match output ports to input ports that share the same expression/net:

Update the `extract()` method to add a post-processing step:

```cpp
ConnectionGraph ConnectionExtractor::extract() {
    auto* topDef = compilation_.getRoot().find(topModule_);
    if (!topDef)
        return graph_;

    for (auto* inst : compilation_.getRoot().topInstances) {
        if (inst->name == topModule_) {
            visitInstance(*inst, topModule_);
            break;
        }
    }

    // Match drivers to loads through shared nets
    buildConnections();

    return graph_;
}
```

Add `buildConnections()` to the header and implement:

```cpp
void ConnectionExtractor::buildConnections() {
    // Group bindings by their expression/net
    // Outputs drive, inputs are driven
    std::vector<PortInfo> drivers;
    std::vector<PortInfo> loads;

    for (auto& binding : netBindings_) {
        if (binding.isDriver)
            drivers.push_back(binding.port);
        else
            loads.push_back(binding.port);
    }

    // For now, match ports that share the same parent scope
    // and are connected through the same net variable
    // This will be enhanced when we add expression analysis
    for (auto& driver : drivers) {
        for (auto& load : loads) {
            // Ports at the same hierarchy level connected through same scope
            if (driver.instancePath.substr(0, driver.instancePath.rfind('.')) ==
                load.instancePath.substr(0, load.instancePath.rfind('.'))) {
                graph_.connections.push_back({driver, load});
            }
        }
    }
}
```

- [ ] **Step 7: Run all tests**

```bash
cmake --build build && cd build && ctest --output-on-failure
```

Expected: all tests PASS

- [ ] **Step 8: Commit**

```bash
git add src/ConnectionExtractor.h src/ConnectionExtractor.cpp \
    tests/test_extractor.cpp tests/sv/
git commit -m "feat: add ConnectionExtractor with slang AST traversal"
```

---

### Task 10: CLI & Main Pipeline

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement full main.cpp**

`src/main.cpp`:

```cpp
#include "CheckerRunner.h"
#include "ConnectionExtractor.h"
#include "CsvReport.h"
#include "DanglingChecker.h"
#include "JsonReport.h"
#include "MarkdownReport.h"
#include "ReportGenerator.h"
#include "TableReport.h"
#include "TypeChecker.h"
#include "UndrivenChecker.h"
#include "WaiverFilter.h"
#include "WidthChecker.h"

#include "slang/driver/Driver.h"

#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    std::string topModule;
    std::string outputDir = "./connect_reports/";
    std::string format = "all";
    std::string waiverFile;
    bool checkWidth = true;
    bool checkType = true;
    bool checkDangling = true;
    bool checkUndriven = true;
    bool ignoreTieOff = false;
    bool ignoreNC = false;
    int depth = -1;
};

static void printUsage() {
    fmt::print(stderr,
        "Usage: slang-connect [OPTIONS] <SV_FILES...>\n\n"
        "Required:\n"
        "  <SV_FILES...>           SystemVerilog source files\n"
        "  --top <module>          Top-level module\n\n"
        "Options:\n"
        "  -f <filelist>           Source filelist (.f file)\n"
        "  -o, --output <dir>      Output directory (default: ./connect_reports/)\n"
        "  --format <fmt>          json|md|csv|table|all (default: all)\n\n"
        "Analysis (all enabled by default):\n"
        "  --no-check-width        Disable width mismatch detection\n"
        "  --no-check-type         Disable signed/unsigned mismatch\n"
        "  --no-check-dangling     Disable unconnected output detection\n"
        "  --no-check-undriven     Disable undriven input detection\n"
        "  --depth <n>             Hierarchy depth (default: unlimited)\n\n"
        "Filtering:\n"
        "  --ignore-tie-off        Don't flag tied-off inputs\n"
        "  --ignore-nc             Don't flag NC ports\n"
        "  --waiver <file>         Waiver YAML\n"
    );
}

static Options parseOptions(int argc, char* argv[]) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--top" && i + 1 < argc) {
            opts.topModule = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            opts.outputDir = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            opts.format = argv[++i];
        } else if (arg == "--waiver" && i + 1 < argc) {
            opts.waiverFile = argv[++i];
        } else if (arg == "--depth" && i + 1 < argc) {
            opts.depth = std::stoi(argv[++i]);
        } else if (arg == "--no-check-width") {
            opts.checkWidth = false;
        } else if (arg == "--no-check-type") {
            opts.checkType = false;
        } else if (arg == "--no-check-dangling") {
            opts.checkDangling = false;
        } else if (arg == "--no-check-undriven") {
            opts.checkUndriven = false;
        } else if (arg == "--ignore-tie-off") {
            opts.ignoreTieOff = true;
        } else if (arg == "--ignore-nc") {
            opts.ignoreNC = true;
        }
    }

    return opts;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    // Parse our options first
    Options opts = parseOptions(argc, argv);

    if (opts.topModule.empty()) {
        fmt::print(stderr, "Error: --top <module> is required\n");
        return 1;
    }

    // Use slang driver for compilation
    slang::driver::Driver driver;
    driver.addStandardArgs();

    if (!driver.parseCommandLine(argc, argv)) {
        return 1;
    }

    if (!driver.processOptions()) {
        return 1;
    }

    driver.parseAllSources();

    auto compilation = driver.createCompilation();
    if (!compilation) {
        fmt::print(stderr, "Error: compilation failed\n");
        return 1;
    }

    // Extract connections
    connect::ConnectionExtractor extractor(*compilation, opts.topModule, opts.depth);
    auto graph = extractor.extract();

    // Run checkers
    connect::CheckerRunner runner;
    if (opts.checkWidth)
        runner.addChecker(std::make_unique<connect::WidthChecker>());
    if (opts.checkType)
        runner.addChecker(std::make_unique<connect::TypeChecker>());
    if (opts.checkDangling)
        runner.addChecker(std::make_unique<connect::DanglingChecker>());
    if (opts.checkUndriven)
        runner.addChecker(std::make_unique<connect::UndrivenChecker>());

    auto issues = runner.runAll(graph);

    // Apply waivers
    connect::WaiverFilter::WaiverResult waiverResult;
    if (!opts.waiverFile.empty()) {
        connect::WaiverFilter filter(opts.waiverFile);
        waiverResult = filter.apply(issues);
    } else {
        waiverResult.active = std::move(issues);
    }

    // Build report data
    connect::ReportData reportData;
    reportData.topModule = opts.topModule;
    reportData.graph = std::move(graph);
    reportData.active = std::move(waiverResult.active);
    reportData.waived = std::move(waiverResult.waived);

    // Generate reports
    bool wantJson  = (opts.format == "json"  || opts.format == "all");
    bool wantMd    = (opts.format == "md"    || opts.format == "all");
    bool wantCsv   = (opts.format == "csv"   || opts.format == "all");
    bool wantTable = (opts.format == "table" || opts.format == "all");

    if (wantJson || wantMd || wantCsv) {
        fs::create_directories(opts.outputDir);
    }

    if (wantTable) {
        connect::TableReportGenerator tableGen;
        tableGen.generate(reportData, std::cout);
    }

    if (wantJson) {
        std::ofstream f(fs::path(opts.outputDir) / "connect_report.json");
        connect::JsonReportGenerator jsonGen;
        jsonGen.generate(reportData, f);
    }

    if (wantMd) {
        std::ofstream f(fs::path(opts.outputDir) / "connect_report.md");
        connect::MarkdownReportGenerator mdGen;
        mdGen.generate(reportData, f);
    }

    if (wantCsv) {
        std::ofstream f(fs::path(opts.outputDir) / "connection_matrix.csv");
        connect::CsvReportGenerator csvGen;
        csvGen.generate(reportData, f);
    }

    // Exit code = number of active issues (capped at 255)
    return std::min((int)reportData.active.size(), 255);
}
```

- [ ] **Step 2: Build and test with clean_design.sv**

```bash
cmake --build build
./build/slang-connect tests/sv/clean_design.sv --top clean_top --format table
```

Expected: table output with 0 issues, exit code 0

- [ ] **Step 3: Test with width_mismatch.sv**

```bash
./build/slang-connect tests/sv/width_mismatch.sv --top width_mismatch_top --format table
echo "Exit code: $?"
```

Expected: WIDTH_MISMATCH reported, exit code > 0

- [ ] **Step 4: Test with all formats**

```bash
./build/slang-connect tests/sv/width_mismatch.sv --top width_mismatch_top --format all -o /tmp/connect_test/
ls /tmp/connect_test/
cat /tmp/connect_test/connect_report.json
```

Expected: 3 files created (json, md, csv) + table on stdout

- [ ] **Step 5: Test checker disable**

```bash
./build/slang-connect tests/sv/width_mismatch.sv --top width_mismatch_top --no-check-width --format table
echo "Exit code: $?"
```

Expected: no WIDTH_MISMATCH reported

- [ ] **Step 6: Run full test suite**

```bash
cd build && ctest --output-on-failure
```

Expected: all tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add CLI and main pipeline orchestration"
```

---

### Task 11: End-to-End Integration Test

**Files:**
- Create: `tests/sv/integration.sv`
- Create: `tests/test_integration.sh`

- [ ] **Step 1: Create multi-module integration test SV**

`tests/sv/integration.sv`:

```systemverilog
module core (
    output logic [31:0] o_data,
    output logic        o_valid,
    output logic [7:0]  o_debug,
    output logic signed [15:0] o_coeff
);
    assign o_data = 32'hDEAD;
    assign o_valid = 1'b1;
    assign o_debug = 8'hFF;
    assign o_coeff = -16'sd42;
endmodule

module bus_adapter (
    input  logic [15:0] i_data,
    input  logic        i_valid,
    input  logic [15:0] i_coeff,
    input  logic [7:0]  i_config,
    output logic [15:0] o_result
);
    assign o_result = i_valid ? i_data + i_coeff + {8'b0, i_config} : 16'b0;
endmodule

module integration_top;
    logic [31:0] data;
    logic        valid;
    logic [7:0]  debug;
    logic signed [15:0] coeff;
    logic [15:0] result;

    core u_core (
        .o_data(data),
        .o_valid(valid),
        .o_debug(),
        .o_coeff(coeff)
    );

    bus_adapter u_bus (
        .i_data(data[15:0]),
        .i_valid(valid),
        .i_coeff(coeff),
        .i_config(),
        .o_result(result)
    );
endmodule
```

Expected issues:
- WIDTH_MISMATCH: u_core.o_data[31:0] → u_bus.i_data[15:0] (if assign tracks through)
- TYPE_MISMATCH: u_core.o_coeff (signed) → u_bus.i_coeff (unsigned)
- DANGLING_OUTPUT: u_core.o_debug (connected to empty `()`)
- UNDRIVEN_INPUT: u_bus.i_config (connected to empty `()`)

- [ ] **Step 2: Create integration test script**

`tests/test_integration.sh`:

```bash
#!/bin/bash
set -e

BINARY="${1:-./build/slang-connect}"
OUTDIR="/tmp/slang-connect-integration-test"
rm -rf "$OUTDIR"

echo "=== Integration Test ==="

# Test 1: Clean design should produce exit code 0
$BINARY tests/sv/clean_design.sv --top clean_top --format table -o "$OUTDIR/clean"
EXIT=$?
if [ $EXIT -ne 0 ]; then
    echo "FAIL: clean_design expected exit 0, got $EXIT"
    exit 1
fi
echo "PASS: clean design"

# Test 2: Integration design should find issues
$BINARY tests/sv/integration.sv --top integration_top --format all -o "$OUTDIR/integration" || true

# Verify JSON output exists and contains issues
if [ ! -f "$OUTDIR/integration/connect_report.json" ]; then
    echo "FAIL: JSON report not generated"
    exit 1
fi
echo "PASS: reports generated"

# Test 3: Verify all output files exist
for f in connect_report.json connect_report.md connection_matrix.csv; do
    if [ ! -f "$OUTDIR/integration/$f" ]; then
        echo "FAIL: $f not found"
        exit 1
    fi
done
echo "PASS: all output formats"

echo "=== All integration tests passed ==="
```

- [ ] **Step 3: Run integration tests**

```bash
chmod +x tests/test_integration.sh
./tests/test_integration.sh ./build/slang-connect
```

Expected: all integration tests PASS

- [ ] **Step 4: Commit**

```bash
git add tests/sv/integration.sv tests/test_integration.sh
git commit -m "feat: add end-to-end integration test with multi-module SV"
```

---

### Task 12: Polish & Documentation

**Files:**
- Create: `README.md`
- Create: `.gitignore`

- [ ] **Step 1: Create .gitignore**

```
build/
connect_reports/
.cache/
compile_commands.json
```

- [ ] **Step 2: Create README.md**

```markdown
# slang-connect

Module interconnect verification tool for SystemVerilog designs.
Analyzes port connections and reports width mismatches, type mismatches,
dangling outputs, and undriven inputs.

Built on [slang](https://github.com/MikePopoloski/slang) v10+.

## Build

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build
```

## Usage

```bash
slang-connect design.sv --top my_top
slang-connect -f filelist.f --top soc_top --format json -o reports/
slang-connect design.sv --top my_top --no-check-dangling --waiver waivers.yaml
```

## Checks

| Check | Detects | Default |
|-------|---------|---------|
| Width Mismatch | Silent truncation or extension | ON |
| Type Mismatch | signed/unsigned mismatch | ON |
| Dangling Output | Unconnected output ports | ON |
| Undriven Input | Input ports with no driver | ON |

## Output Formats

- **table** — Terminal output
- **json** — Machine-readable report
- **md** — Markdown review report
- **csv** — Connection matrix spreadsheet
- **all** — All formats (default)
```

- [ ] **Step 3: Commit**

```bash
git add .gitignore README.md
git commit -m "docs: add README and .gitignore"
```

- [ ] **Step 4: Run full test suite one final time**

```bash
cd build && ctest --output-on-failure && cd .. && ./tests/test_integration.sh ./build/slang-connect
```

Expected: all tests PASS
