# slang-connect v0.2–v0.7 Enhancement Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** slang-connect를 "lint 보조 도구"에서 "SoC connectivity analyzer"로 격상시킨다. Verilator가 구조적으로 답할 수 없는 질문들(diff, expect, trace, visualization)을 추가하여 독자적 가치를 확보한다.

**Architecture:** 기존 `IReportGenerator`/`IChecker` 인터페이스를 최대한 재활용한다. 새 기능은 모두 `ConnectionGraph`를 입력으로 받는 순수 분석 레이어로 구현하여 slang에 대한 직접 의존을 피한다. 시각화는 self-contained HTML 파일로 JSON 데이터를 임베딩하는 방식을 사용한다.

**Tech Stack:** C++20, slang v10, fmt, yaml-cpp, Catch2, vis-network.js (HTML 임베딩)

---

## File Map

### New Files

| Milestone | File | Responsibility |
|-----------|------|----------------|
| v0.2 | `src/DotReport.h` / `.cpp` | Graphviz DOT 형식 리포트 생성 |
| v0.2 | `src/HtmlReport.h` / `.cpp` | Interactive HTML 리포트 생성 |
| v0.2 | `src/html_template.h` | HTML+JS 템플릿 (raw string literal) |
| v0.3 | `src/GraphDiff.h` / `.cpp` | 두 ConnectionGraph 간 diff 계산 |
| v0.4 | `src/ExpectChecker.h` / `.cpp` | Expected connectivity YAML 로드 + 검증 |
| v0.5 | `src/ClockResetAnalyzer.h` / `.cpp` | clk/rst 포트 분류 + topology 추출 |
| v0.6 | `src/InterfaceGrouper.h` / `.cpp` | 포트 → 논리 인터페이스 그룹핑 |
| v0.7 | `src/TraceEngine.h` / `.cpp` | 신호 fan-in/fan-out 추적 |
| all | `tests/test_<feature>.cpp` | 각 기능별 유닛 테스트 |
| v0.2 | `tests/sv/multi_module_soc.sv` | 시각화/diff/trace 공용 테스트 디자인 |
| v0.5 | `tests/sv/clock_reset.sv` | 멀티 클럭/리셋 테스트 디자인 |

### Modified Files

| Milestone | File | Changes |
|-----------|------|---------|
| v0.2 | `CMakeLists.txt` | 새 소스 파일 추가 |
| v0.2 | `tests/CMakeLists.txt` | 새 테스트 파일 + SV fixture 복사 |
| v0.2 | `src/main.cpp` | `--format dot,html` 지원 |
| v0.3 | `src/main.cpp` | `--diff <baseline.json>` 옵션 |
| v0.4 | `src/main.cpp` | `--expect <spec.yaml>` 옵션 |
| v0.5 | `src/main.cpp` | `--check-clock-reset` 옵션 |
| v0.6 | `src/ConnectionGraph.h` | `InterfaceGroup` 구조체 추가 |
| v0.7 | `src/main.cpp` | `--trace <signal>` 옵션 |

---

## Milestone 1: Visualization (v0.2)

> 핵심 가치: 텍스트 리포트를 사람이 볼 수 있는 블록 다이어그램과 인터랙티브 뷰로 변환

### Task 1: 공용 테스트 SV 디자인 작성

**Files:**
- Create: `tests/sv/multi_module_soc.sv`

이후 milestone에서도 재사용할 3-level hierarchy SoC 테스트 디자인.

- [ ] **Step 1: 테스트 SV 파일 작성**

```systemverilog
// tests/sv/multi_module_soc.sv
// 3-level hierarchy: soc_top → {u_cpu, u_bus, u_mem}
// u_cpu has sub-instance u_alu

module alu (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] i_op_a,
    input  logic [31:0] i_op_b,
    input  logic [2:0]  i_op_sel,
    output logic [31:0] o_result
);
    assign o_result = i_op_a + i_op_b; // simplified
endmodule

module cpu (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] o_ibus_addr,
    input  logic [31:0] i_ibus_rdata,
    output logic [15:0] o_dbus_addr,   // intentional: 16-bit addr
    output logic [31:0] o_dbus_wdata,
    input  logic [31:0] i_dbus_rdata,
    output logic        o_dbus_we
);
    logic [31:0] alu_a, alu_b, alu_result;
    logic [2:0]  alu_sel;

    alu u_alu (
        .clk(clk), .rst_n(rst_n),
        .i_op_a(alu_a), .i_op_b(alu_b),
        .i_op_sel(alu_sel), .o_result(alu_result)
    );

    assign o_ibus_addr = 32'h0;
    assign o_dbus_addr = 16'h0;
    assign o_dbus_wdata = alu_result;
    assign o_dbus_we = 1'b0;
endmodule

module bus_interconnect (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] i_cpu_ibus_addr,
    output logic [31:0] o_cpu_ibus_rdata,
    input  logic [15:0] i_cpu_dbus_addr,
    input  logic [31:0] i_cpu_dbus_wdata,
    output logic [31:0] o_cpu_dbus_rdata,
    input  logic        i_cpu_dbus_we,
    output logic [31:0] o_mem_addr,
    output logic [31:0] o_mem_wdata,
    input  logic [31:0] i_mem_rdata,
    output logic        o_mem_we
);
    assign o_mem_addr  = {16'h0, i_cpu_dbus_addr};
    assign o_mem_wdata = i_cpu_dbus_wdata;
    assign o_cpu_dbus_rdata = i_mem_rdata;
    assign o_cpu_ibus_rdata = i_mem_rdata;
    assign o_mem_we = i_cpu_dbus_we;
endmodule

module memory (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] i_addr,
    input  logic [31:0] i_wdata,
    output logic [31:0] o_rdata,
    input  logic        i_we
);
    assign o_rdata = 32'h0;
endmodule

module soc_top (
    input  logic clk,
    input  logic rst_n
);
    logic [31:0] cpu_ibus_addr, cpu_ibus_rdata;
    logic [15:0] cpu_dbus_addr;
    logic [31:0] cpu_dbus_wdata, cpu_dbus_rdata;
    logic        cpu_dbus_we;
    logic [31:0] mem_addr, mem_wdata, mem_rdata;
    logic        mem_we;

    cpu u_cpu (
        .clk(clk), .rst_n(rst_n),
        .o_ibus_addr(cpu_ibus_addr),
        .i_ibus_rdata(cpu_ibus_rdata),
        .o_dbus_addr(cpu_dbus_addr),
        .o_dbus_wdata(cpu_dbus_wdata),
        .i_dbus_rdata(cpu_dbus_rdata),
        .o_dbus_we(cpu_dbus_we)
    );

    bus_interconnect u_bus (
        .clk(clk), .rst_n(rst_n),
        .i_cpu_ibus_addr(cpu_ibus_addr),
        .o_cpu_ibus_rdata(cpu_ibus_rdata),
        .i_cpu_dbus_addr(cpu_dbus_addr),
        .i_cpu_dbus_wdata(cpu_dbus_wdata),
        .o_cpu_dbus_rdata(cpu_dbus_rdata),
        .i_cpu_dbus_we(cpu_dbus_we),
        .o_mem_addr(mem_addr),
        .o_mem_wdata(mem_wdata),
        .i_mem_rdata(mem_rdata),
        .o_mem_we(mem_we)
    );

    memory u_mem (
        .clk(clk), .rst_n(rst_n),
        .i_addr(mem_addr),
        .i_wdata(mem_wdata),
        .o_rdata(mem_rdata),
        .i_we(mem_we)
    );
endmodule
```

- [ ] **Step 2: 빌드 확인**

Run: `cd /Users/babyworm/work/eda-tools/slang-connect && cmake --build build 2>&1 | tail -5`
Expected: 빌드 성공 (기존 코드에 영향 없음, 파일 추가만)

- [ ] **Step 3: Commit**

```bash
git add tests/sv/multi_module_soc.sv
git commit -m "test: add multi-module SoC fixture for visualization and diff tests"
```

---

### Task 2: DOT Report Generator

**Files:**
- Create: `src/DotReport.h`
- Create: `src/DotReport.cpp`
- Create: `tests/test_dot_report.cpp`
- Modify: `CMakeLists.txt:30-44` (add source)
- Modify: `tests/CMakeLists.txt:1-2` (add test)

모듈을 노드, 연결을 엣지로 표현하는 Graphviz DOT 출력. `dot -Tsvg` 으로 블록 다이어그램 생성 가능.

- [ ] **Step 1: Failing test 작성**

```cpp
// tests/test_dot_report.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "DotReport.h"
#include <sstream>

using namespace connect;
using namespace Catch::Matchers;
using slang::ast::ArgumentDirection;

static ReportData makeDotTestData() {
    ReportData data;
    data.topModule = "soc_top";

    PortInfo cpu_out;
    cpu_out.instancePath = "soc_top.u_cpu";
    cpu_out.portName = "o_data";
    cpu_out.direction = ArgumentDirection::Out;
    cpu_out.width = 32;

    PortInfo bus_in;
    bus_in.instancePath = "soc_top.u_bus";
    bus_in.portName = "i_data";
    bus_in.direction = ArgumentDirection::In;
    bus_in.width = 16;

    Connection conn{cpu_out, bus_in};
    data.graph.connections.push_back(conn);
    data.graph.allPorts.push_back(cpu_out);
    data.graph.allPorts.push_back(bus_in);

    Issue issue;
    issue.type = Issue::Type::WIDTH_MISMATCH;
    issue.severity = Issue::Severity::ERROR;
    issue.port = cpu_out;
    issue.connection = conn;
    issue.detail = "32 -> 16 truncation";
    data.active.push_back(issue);

    return data;
}

TEST_CASE("DotReport: produces valid DOT with digraph") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("digraph"));
    CHECK_THAT(dot, ContainsSubstring("soc_top"));
}

TEST_CASE("DotReport: modules appear as nodes") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    // Unique instance paths become nodes
    CHECK_THAT(dot, ContainsSubstring("u_cpu"));
    CHECK_THAT(dot, ContainsSubstring("u_bus"));
}

TEST_CASE("DotReport: connections appear as edges with width labels") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    // Edge from cpu to bus
    CHECK_THAT(dot, ContainsSubstring("->"));
    // Width label on edge
    CHECK_THAT(dot, ContainsSubstring("32"));
}

TEST_CASE("DotReport: error connections are colored red") {
    auto data = makeDotTestData();
    std::ostringstream out;
    DotReportGenerator gen;
    gen.generate(data, out);
    auto dot = out.str();

    CHECK_THAT(dot, ContainsSubstring("red"));
}
```

- [ ] **Step 2: 테스트 빌드 실패 확인**

Run: `cd /Users/babyworm/work/eda-tools/slang-connect && cmake --build build 2>&1 | grep -i error | head -5`
Expected: `DotReport.h: No such file` 또는 링크 에러

- [ ] **Step 3: DotReport 헤더 작성**

```cpp
// src/DotReport.h
#pragma once
#include "ReportGenerator.h"

namespace connect {

class DotReportGenerator : public IReportGenerator {
public:
    void generate(const ReportData& data, std::ostream& out) const override;
};

} // namespace connect
```

- [ ] **Step 4: DotReport 구현 작성**

```cpp
// src/DotReport.cpp
#include "DotReport.h"
#include <fmt/format.h>
#include <map>
#include <set>

namespace connect {

namespace {

// "soc_top.u_cpu" → "u_cpu", "soc_top.u_cpu.u_alu" → "u_cpu__u_alu"
std::string nodeId(const std::string& instancePath, const std::string& topModule) {
    auto pos = instancePath.find('.');
    std::string relative = (pos != std::string::npos) ? instancePath.substr(pos + 1) : instancePath;
    // Replace dots with __ for DOT node IDs
    std::string id = relative;
    for (auto& c : id) {
        if (c == '.') c = '_';
    }
    return id;
}

// "soc_top.u_cpu" → "u_cpu"
std::string nodeLabel(const std::string& instancePath) {
    auto pos = instancePath.rfind('.');
    return (pos != std::string::npos) ? instancePath.substr(pos + 1) : instancePath;
}

std::string severityColor(Issue::Severity sev) {
    switch (sev) {
        case Issue::Severity::ERROR: return "red";
        case Issue::Severity::WARN:  return "orange";
        case Issue::Severity::INFO:  return "blue";
    }
    return "black";
}

} // anonymous namespace

void DotReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    // Build issue lookup: "source.fullPath|dest.fullPath" → worst severity
    std::map<std::string, Issue::Severity> issueMap;
    for (const auto& issue : data.active) {
        if (!issue.connection.has_value()) continue;
        auto key = issue.connection->source.fullPath() + "|" + issue.connection->dest.fullPath();
        auto it = issueMap.find(key);
        if (it == issueMap.end() || issue.severity < it->second) {
            issueMap[key] = issue.severity;
        }
    }

    // Collect unique modules (by instancePath)
    std::set<std::string> instances;
    for (const auto& port : data.graph.allPorts) {
        instances.insert(port.instancePath);
    }

    out << fmt::format("digraph \"{}\" {{\n", data.topModule);
    out << "  rankdir=LR;\n";
    out << "  node [shape=record, style=filled, fillcolor=lightyellow, fontname=\"Helvetica\"];\n";
    out << "  edge [fontname=\"Helvetica\", fontsize=10];\n\n";

    // Nodes
    for (const auto& inst : instances) {
        if (inst == data.topModule) continue; // skip top-level itself
        auto id = nodeId(inst, data.topModule);
        auto label = nodeLabel(inst);
        out << fmt::format("  {} [label=\"{}\"];\n", id, label);
    }
    out << "\n";

    // Edges: group connections by (source_instance, dest_instance)
    struct EdgeInfo {
        std::string portName;
        uint32_t srcWidth;
        uint32_t dstWidth;
        std::string color;
    };
    std::map<std::pair<std::string, std::string>, std::vector<EdgeInfo>> edgeGroups;

    for (const auto& conn : data.graph.connections) {
        auto srcInst = conn.source.instancePath;
        auto dstInst = conn.dest.instancePath;
        if (srcInst == dstInst) continue; // skip internal connections

        std::string color = "black";
        auto key = conn.source.fullPath() + "|" + conn.dest.fullPath();
        auto it = issueMap.find(key);
        if (it != issueMap.end()) {
            color = severityColor(it->second);
        }

        edgeGroups[{srcInst, dstInst}].push_back({
            conn.source.portName, conn.source.width, conn.dest.width, color
        });
    }

    for (const auto& [pair, edges] : edgeGroups) {
        auto srcId = nodeId(pair.first, data.topModule);
        auto dstId = nodeId(pair.second, data.topModule);

        // Find worst color among edges in this group
        std::string worstColor = "black";
        for (const auto& e : edges) {
            if (e.color == "red") { worstColor = "red"; break; }
            if (e.color == "orange" && worstColor != "red") worstColor = "orange";
        }

        // Label: list signal names with widths
        std::string label;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (i > 0) label += "\\n";
            label += fmt::format("{} [{}b]", edges[i].portName, edges[i].srcWidth);
        }

        out << fmt::format("  {} -> {} [label=\"{}\", color={}, fontcolor={}];\n",
                           srcId, dstId, label, worstColor, worstColor);
    }

    out << "}\n";
}

} // namespace connect
```

- [ ] **Step 5: CMakeLists.txt에 소스 추가**

`CMakeLists.txt`의 `add_library(slang-connect-lib OBJECT` 블록에 `src/DotReport.cpp` 추가.

`tests/CMakeLists.txt`의 `add_executable(slang-connect-tests` 블록에 `test_dot_report.cpp` 추가.

- [ ] **Step 6: 테스트 통과 확인**

Run: `cd /Users/babyworm/work/eda-tools/slang-connect && cmake --build build && cd build && ctest --test-dir . -R DotReport --output-on-failure`
Expected: 4개 테스트 모두 PASS

- [ ] **Step 7: Commit**

```bash
git add src/DotReport.h src/DotReport.cpp tests/test_dot_report.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add DOT report generator for Graphviz block diagrams"
```

---

### Task 3: Interactive HTML Report

**Files:**
- Create: `src/html_template.h`
- Create: `src/HtmlReport.h`
- Create: `src/HtmlReport.cpp`
- Create: `tests/test_html_report.cpp`
- Modify: `CMakeLists.txt:30-44`
- Modify: `tests/CMakeLists.txt:1-2`

Self-contained HTML 파일 생성. JSON 데이터를 `<script>` 태그에 임베딩하고, vis-network.js로 인터랙티브 그래프를 렌더링한다. 브라우저에서 열면 즉시 사용 가능.

- [ ] **Step 1: HTML 템플릿 작성**

```cpp
// src/html_template.h
#pragma once

namespace connect {

// vis-network CDN URL - loaded at runtime in browser
// Template uses {{JSON_DATA}} as placeholder for embedded report JSON
inline constexpr const char* HTML_TEMPLATE = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>slang-connect: {{TOP_MODULE}}</title>
<script src="https://unpkg.com/vis-network@9.1.6/standalone/umd/vis-network.min.js"></script>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
         background: #1a1a2e; color: #e0e0e0; }
  #header { padding: 16px 24px; background: #16213e; border-bottom: 1px solid #0f3460;
            display: flex; align-items: center; gap: 24px; }
  #header h1 { font-size: 18px; font-weight: 600; }
  .badge { padding: 4px 12px; border-radius: 12px; font-size: 13px; font-weight: 500; }
  .badge-err { background: #e74c3c33; color: #e74c3c; border: 1px solid #e74c3c55; }
  .badge-warn { background: #f39c1233; color: #f39c12; border: 1px solid #f39c1255; }
  .badge-ok { background: #2ecc7133; color: #2ecc71; border: 1px solid #2ecc7155; }
  #container { display: flex; height: calc(100vh - 57px); }
  #graph { flex: 1; }
  #sidebar { width: 360px; background: #16213e; overflow-y: auto; border-left: 1px solid #0f3460; }
  #sidebar h2 { padding: 12px 16px; font-size: 14px; border-bottom: 1px solid #0f3460; }
  .issue { padding: 10px 16px; border-bottom: 1px solid #0f346033; cursor: pointer; font-size: 13px; }
  .issue:hover { background: #0f346066; }
  .issue .sev { font-weight: 600; margin-right: 8px; }
  .sev-ERROR { color: #e74c3c; } .sev-WARN { color: #f39c12; } .sev-INFO { color: #3498db; }
  .issue .detail { color: #999; margin-top: 4px; }
  #filters { padding: 12px 16px; border-bottom: 1px solid #0f3460; display: flex; gap: 8px; flex-wrap: wrap; }
  .filter-btn { padding: 4px 10px; border-radius: 8px; border: 1px solid #555; background: none;
                color: #ccc; cursor: pointer; font-size: 12px; }
  .filter-btn.active { background: #0f3460; border-color: #3498db; color: #fff; }
  #tooltip { position: absolute; background: #16213e; border: 1px solid #0f3460; padding: 12px;
             border-radius: 8px; font-size: 13px; display: none; max-width: 400px; z-index: 10; }
</style>
</head>
<body>
<div id="header">
  <h1>slang-connect: <span id="top-name"></span></h1>
  <span id="conn-count" class="badge badge-ok"></span>
  <span id="err-count" class="badge badge-err"></span>
  <span id="warn-count" class="badge badge-warn"></span>
</div>
<div id="container">
  <div id="graph"></div>
  <div id="sidebar">
    <div id="filters"></div>
    <h2>Issues (<span id="issue-count">0</span>)</h2>
    <div id="issue-list"></div>
  </div>
</div>
<div id="tooltip"></div>

<script>
const DATA = {{JSON_DATA}};

// --- Summary ---
document.getElementById('top-name').textContent = DATA.top;
document.getElementById('conn-count').textContent = DATA.summary.connections_analyzed + ' connections';
document.getElementById('err-count').textContent = DATA.summary.errors + ' errors';
document.getElementById('warn-count').textContent = DATA.summary.warnings + ' warnings';

// --- Build Graph ---
const instanceSet = new Set();
DATA.connections.forEach(c => {
  const srcInst = c.source.split('.').slice(0, -1).join('.');
  const dstInst = c.dest.split('.').slice(0, -1).join('.');
  if (srcInst) instanceSet.add(srcInst);
  if (dstInst) instanceSet.add(dstInst);
});

const nodes = [];
const nodeIds = {};
let nodeIdx = 0;
instanceSet.forEach(inst => {
  const label = inst.split('.').pop();
  nodeIds[inst] = nodeIdx;
  nodes.push({
    id: nodeIdx, label: label, title: inst,
    shape: 'box', color: { background: '#1a3a5c', border: '#3498db', highlight: { background: '#1a5276', border: '#5dade2' } },
    font: { color: '#e0e0e0', size: 14 }, borderWidth: 2, margin: 10
  });
  nodeIdx++;
});

// Build issue lookup
const issueMap = {};
DATA.issues.forEach(iss => {
  if (iss.source && iss.dest) {
    const key = (iss.source.instance + '.' + iss.source.port) + '|' + (iss.dest.instance + '.' + iss.dest.port);
    issueMap[key] = iss;
  }
});

// Aggregate edges by instance pair
const edgeAgg = {};
DATA.connections.forEach(c => {
  const srcParts = c.source.replace(/\[\d+:\d+\]/g, '').split('.');
  const dstParts = c.dest.replace(/\[\d+:\d+\]/g, '').split('.');
  const srcInst = srcParts.slice(0, -1).join('.');
  const dstInst = dstParts.slice(0, -1).join('.');
  if (!srcInst || !dstInst || srcInst === dstInst) return;
  const key = srcInst + '→' + dstInst;
  if (!edgeAgg[key]) edgeAgg[key] = { from: srcInst, to: dstInst, signals: [], hasError: false, hasWarn: false };
  const sigName = srcParts.pop();
  edgeAgg[key].signals.push({ name: sigName, status: c.status });
  if (c.status !== 'OK') {
    if (c.status === 'WIDTH_MISMATCH' || c.status === 'TYPE_MISMATCH') edgeAgg[key].hasError = true;
    else edgeAgg[key].hasWarn = true;
  }
});

const edges = [];
Object.values(edgeAgg).forEach(e => {
  const fromId = nodeIds[e.from];
  const toId = nodeIds[e.to];
  if (fromId === undefined || toId === undefined) return;
  let color = '#3498db';
  if (e.hasError) color = '#e74c3c';
  else if (e.hasWarn) color = '#f39c12';
  const label = e.signals.length + ' sig' + (e.signals.length > 1 ? 's' : '');
  edges.push({
    from: fromId, to: toId, label: label, arrows: 'to',
    color: { color: color, highlight: color }, font: { color: color, size: 11, strokeWidth: 0 },
    width: Math.min(1 + e.signals.length * 0.5, 5),
    title: e.signals.map(s => s.name + (s.status !== 'OK' ? ' ⚠ ' + s.status : '')).join('\n')
  });
});

const network = new vis.Network(
  document.getElementById('graph'),
  { nodes: new vis.DataSet(nodes), edges: new vis.DataSet(edges) },
  { layout: { hierarchical: { direction: 'LR', sortMethod: 'directed', levelSeparation: 200, nodeSpacing: 80 } },
    physics: false,
    interaction: { hover: true, tooltipDelay: 100 } }
);

// --- Issue List ---
const issueTypes = [...new Set(DATA.issues.map(i => i.type))];
const filtersDiv = document.getElementById('filters');
let activeFilters = new Set(issueTypes);

function renderFilters() {
  filtersDiv.innerHTML = '';
  issueTypes.forEach(t => {
    const btn = document.createElement('button');
    btn.className = 'filter-btn' + (activeFilters.has(t) ? ' active' : '');
    btn.textContent = t;
    btn.onclick = () => { activeFilters.has(t) ? activeFilters.delete(t) : activeFilters.add(t); renderFilters(); renderIssues(); };
    filtersDiv.appendChild(btn);
  });
}

function renderIssues() {
  const list = document.getElementById('issue-list');
  const filtered = DATA.issues.filter(i => activeFilters.has(i.type));
  document.getElementById('issue-count').textContent = filtered.length;
  list.innerHTML = '';
  filtered.forEach(iss => {
    const div = document.createElement('div');
    div.className = 'issue';
    div.innerHTML = '<span class="sev sev-' + iss.severity + '">' + iss.severity + '</span>' +
      '<span>' + iss.type + '</span><br><strong>' + iss.port + '</strong>' +
      '<div class="detail">' + iss.detail + '</div>';
    list.appendChild(div);
  });
}

renderFilters();
renderIssues();
</script>
</body>
</html>)html";

} // namespace connect
```

- [ ] **Step 2: Failing test 작성**

```cpp
// tests/test_html_report.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "HtmlReport.h"
#include <sstream>

using namespace connect;
using namespace Catch::Matchers;
using slang::ast::ArgumentDirection;

static ReportData makeHtmlTestData() {
    ReportData data;
    data.topModule = "soc_top";

    PortInfo src;
    src.instancePath = "soc_top.u_cpu"; src.portName = "o_data";
    src.direction = ArgumentDirection::Out; src.width = 32;

    PortInfo dst;
    dst.instancePath = "soc_top.u_bus"; dst.portName = "i_data";
    dst.direction = ArgumentDirection::In; dst.width = 16;

    data.graph.connections.push_back({src, dst});
    data.graph.allPorts.push_back(src);
    data.graph.allPorts.push_back(dst);

    Issue issue;
    issue.type = Issue::Type::WIDTH_MISMATCH;
    issue.severity = Issue::Severity::ERROR;
    issue.port = src;
    issue.connection = Connection{src, dst};
    issue.detail = "32 -> 16 truncation";
    data.active.push_back(issue);
    return data;
}

TEST_CASE("HtmlReport: produces valid HTML document") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("<!DOCTYPE html>"));
    CHECK_THAT(html, ContainsSubstring("</html>"));
}

TEST_CASE("HtmlReport: embeds JSON data") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("\"top\":"));
    CHECK_THAT(html, ContainsSubstring("soc_top"));
    CHECK_THAT(html, ContainsSubstring("WIDTH_MISMATCH"));
}

TEST_CASE("HtmlReport: includes vis-network script") {
    auto data = makeHtmlTestData();
    std::ostringstream out;
    HtmlReportGenerator gen;
    gen.generate(data, out);
    auto html = out.str();

    CHECK_THAT(html, ContainsSubstring("vis-network"));
}
```

- [ ] **Step 3: HtmlReport 헤더 + 구현 작성**

```cpp
// src/HtmlReport.h
#pragma once
#include "ReportGenerator.h"

namespace connect {

class HtmlReportGenerator : public IReportGenerator {
public:
    void generate(const ReportData& data, std::ostream& out) const override;
};

} // namespace connect
```

```cpp
// src/HtmlReport.cpp
#include "HtmlReport.h"
#include "JsonReport.h"
#include "html_template.h"
#include <sstream>

namespace connect {

void HtmlReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    // Generate JSON data using existing JsonReportGenerator
    std::ostringstream jsonStream;
    JsonReportGenerator jsonGen;
    jsonGen.generate(data, jsonStream);
    std::string jsonData = jsonStream.str();

    // Replace placeholders in HTML template
    std::string html(HTML_TEMPLATE);

    // Replace {{TOP_MODULE}}
    const std::string topPlaceholder = "{{TOP_MODULE}}";
    auto topPos = html.find(topPlaceholder);
    if (topPos != std::string::npos) {
        html.replace(topPos, topPlaceholder.size(), data.topModule);
    }

    // Replace {{JSON_DATA}}
    const std::string jsonPlaceholder = "{{JSON_DATA}}";
    auto jsonPos = html.find(jsonPlaceholder);
    if (jsonPos != std::string::npos) {
        html.replace(jsonPos, jsonPlaceholder.size(), jsonData);
    }

    out << html;
}

} // namespace connect
```

- [ ] **Step 4: CMakeLists.txt 업데이트, 빌드 + 테스트**

`CMakeLists.txt`의 OBJECT 라이브러리에 `src/HtmlReport.cpp` 추가.
`tests/CMakeLists.txt`에 `test_html_report.cpp` 추가.

Run: `cd /Users/babyworm/work/eda-tools/slang-connect && cmake --build build && cd build && ctest --test-dir . -R HtmlReport --output-on-failure`
Expected: 3개 테스트 모두 PASS

- [ ] **Step 5: Commit**

```bash
git add src/html_template.h src/HtmlReport.h src/HtmlReport.cpp tests/test_html_report.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add interactive HTML report with vis-network graph visualization"
```

---

### Task 4: CLI에 dot/html 포맷 통합

**Files:**
- Modify: `src/main.cpp:144-149` (format validation)
- Modify: `src/main.cpp:230-273` (report generation section)

- [ ] **Step 1: main.cpp format validation 확장**

`src/main.cpp:144` 의 format validation 조건에 `"dot"`, `"html"` 추가:

```cpp
    if (opts.format != "json" && opts.format != "md" && opts.format != "csv" &&
        opts.format != "table" && opts.format != "dot" && opts.format != "html" &&
        opts.format != "all") {
```

- [ ] **Step 2: main.cpp report generation에 DOT, HTML 추가**

`src/main.cpp`의 CSV report 블록 뒤 (약 line 273)에 추가:

```cpp
    // DOT to file
    if (opts.format == "dot" || opts.format == "all") {
        fs::create_directories(opts.outputDir);
        std::string path = (fs::path(opts.outputDir) / "connectivity.dot").string();
        std::ofstream ofs(path);
        if (ofs) {
            connect::DotReportGenerator dotGen;
            dotGen.generate(reportData, ofs);
        } else {
            fmt::print(stderr, "Error: cannot write to {}\n", path);
        }
    }

    // HTML to file
    if (opts.format == "html" || opts.format == "all") {
        fs::create_directories(opts.outputDir);
        std::string path = (fs::path(opts.outputDir) / "connect_report.html").string();
        std::ofstream ofs(path);
        if (ofs) {
            connect::HtmlReportGenerator htmlGen;
            htmlGen.generate(reportData, ofs);
        } else {
            fmt::print(stderr, "Error: cannot write to {}\n", path);
        }
    }
```

`main.cpp` 상단에 include 추가:
```cpp
#include "DotReport.h"
#include "HtmlReport.h"
```

- [ ] **Step 3: printUsage 업데이트**

`--format` help 텍스트를 `json|md|csv|table|dot|html|all` 로 변경.

- [ ] **Step 4: 빌드 + 통합 테스트**

Run: `cd /Users/babyworm/work/eda-tools/slang-connect && cmake --build build && ./build/slang-connect tests/sv/multi_module_soc.sv --top soc_top --format dot -o /tmp/sc_test/ && cat /tmp/sc_test/connectivity.dot | head -20`
Expected: `digraph "soc_top" {` 로 시작하는 DOT 출력

Run: `./build/slang-connect tests/sv/multi_module_soc.sv --top soc_top --format html -o /tmp/sc_test/ && wc -l /tmp/sc_test/connect_report.html`
Expected: HTML 파일 생성 (100줄 이상)

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate dot and html output formats into CLI"
```

---

## Milestone 2: Connection Diff (v0.3)

> 핵심 가치: "이번 커밋에서 연결이 뭐가 바뀌었나?"에 대한 답

### Task 5: GraphDiff 엔진

**Files:**
- Create: `src/GraphDiff.h`
- Create: `src/GraphDiff.cpp`
- Create: `tests/test_graph_diff.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

두 JSON report를 파싱하여 added/removed/changed 연결을 계산한다. ConnectionGraph 자체가 아니라 JSON을 비교하므로, 이전 버전의 리포트와도 호환된다.

- [ ] **Step 1: Failing test 작성**

```cpp
// tests/test_graph_diff.cpp
#include <catch2/catch_test_macros.hpp>
#include "GraphDiff.h"

using namespace connect;

static DiffInput makeBaseline() {
    DiffInput input;
    // Connection 1: cpu.o_data[32] → bus.i_data[32]  OK
    input.connections.push_back({"soc_top.u_cpu.o_data", "soc_top.u_bus.i_data", "OK"});
    // Connection 2: bus.o_mem_addr[32] → mem.i_addr[32]  OK
    input.connections.push_back({"soc_top.u_bus.o_mem_addr", "soc_top.u_mem.i_addr", "OK"});
    return input;
}

static DiffInput makeCurrent() {
    DiffInput input;
    // Connection 1: CHANGED width (now shows WIDTH_MISMATCH)
    input.connections.push_back({"soc_top.u_cpu.o_data", "soc_top.u_bus.i_data", "WIDTH_MISMATCH"});
    // Connection 2: REMOVED (bus → mem no longer exists)
    // Connection 3: ADDED (new dma connection)
    input.connections.push_back({"soc_top.u_dma.o_addr", "soc_top.u_bus.i_dma_addr", "OK"});
    return input;
}

TEST_CASE("GraphDiff: detects added connections") {
    auto result = computeDiff(makeBaseline(), makeCurrent());
    REQUIRE(result.added.size() == 1);
    CHECK(result.added[0].source == "soc_top.u_dma.o_addr");
}

TEST_CASE("GraphDiff: detects removed connections") {
    auto result = computeDiff(makeBaseline(), makeCurrent());
    REQUIRE(result.removed.size() == 1);
    CHECK(result.removed[0].source == "soc_top.u_bus.o_mem_addr");
}

TEST_CASE("GraphDiff: detects changed status") {
    auto result = computeDiff(makeBaseline(), makeCurrent());
    REQUIRE(result.changed.size() == 1);
    CHECK(result.changed[0].source == "soc_top.u_cpu.o_data");
    CHECK(result.changed[0].oldStatus == "OK");
    CHECK(result.changed[0].newStatus == "WIDTH_MISMATCH");
}

TEST_CASE("GraphDiff: identical inputs produce empty diff") {
    auto baseline = makeBaseline();
    auto result = computeDiff(baseline, baseline);
    CHECK(result.added.empty());
    CHECK(result.removed.empty());
    CHECK(result.changed.empty());
}
```

- [ ] **Step 2: 테스트 빌드 실패 확인**

Run: `cd /Users/babyworm/work/eda-tools/slang-connect && cmake --build build 2>&1 | grep error | head -3`
Expected: `GraphDiff.h: No such file`

- [ ] **Step 3: GraphDiff 헤더 + 구현 작성**

```cpp
// src/GraphDiff.h
#pragma once
#include <string>
#include <vector>

namespace connect {

struct DiffConnection {
    std::string source;
    std::string dest;
    std::string status;
};

struct DiffInput {
    std::vector<DiffConnection> connections;
};

struct ChangedConnection {
    std::string source;
    std::string dest;
    std::string oldStatus;
    std::string newStatus;
};

struct DiffResult {
    std::vector<DiffConnection> added;
    std::vector<DiffConnection> removed;
    std::vector<ChangedConnection> changed;

    bool empty() const { return added.empty() && removed.empty() && changed.empty(); }
};

// Compute diff between baseline and current
DiffResult computeDiff(const DiffInput& baseline, const DiffInput& current);

// Load DiffInput from a JSON report file (reads "connections" array)
DiffInput loadDiffInputFromJson(const std::string& jsonPath);

} // namespace connect
```

```cpp
// src/GraphDiff.cpp
#include "GraphDiff.h"
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace connect {

namespace {
// Simple key for connection identity
std::string connKey(const std::string& src, const std::string& dst) {
    return src + " -> " + dst;
}
} // anonymous namespace

DiffResult computeDiff(const DiffInput& baseline, const DiffInput& current) {
    DiffResult result;

    // Build maps: key → status
    std::map<std::string, const DiffConnection*> baseMap;
    for (const auto& c : baseline.connections) {
        baseMap[connKey(c.source, c.dest)] = &c;
    }

    std::map<std::string, const DiffConnection*> currMap;
    for (const auto& c : current.connections) {
        currMap[connKey(c.source, c.dest)] = &c;
    }

    // Added: in current but not in baseline
    for (const auto& [key, conn] : currMap) {
        if (baseMap.find(key) == baseMap.end()) {
            result.added.push_back(*conn);
        }
    }

    // Removed: in baseline but not in current
    for (const auto& [key, conn] : baseMap) {
        if (currMap.find(key) == currMap.end()) {
            result.removed.push_back(*conn);
        }
    }

    // Changed: in both but status differs
    for (const auto& [key, baseConn] : baseMap) {
        auto it = currMap.find(key);
        if (it != currMap.end() && it->second->status != baseConn->status) {
            result.changed.push_back({
                baseConn->source, baseConn->dest,
                baseConn->status, it->second->status
            });
        }
    }

    return result;
}

// Minimal JSON parser - extracts connections array from report JSON
// Looks for "source": "...", "dest": "...", "status": "..." patterns
DiffInput loadDiffInputFromJson(const std::string& jsonPath) {
    std::ifstream ifs(jsonPath);
    if (!ifs) throw std::runtime_error("Cannot open " + jsonPath);

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    DiffInput input;

    // Find "connections" array and extract each object
    // Simple state machine parser (avoids adding a JSON library dependency)
    auto findValue = [&](const std::string& block, const std::string& key) -> std::string {
        auto kpos = block.find("\"" + key + "\"");
        if (kpos == std::string::npos) return "";
        auto colon = block.find(':', kpos);
        if (colon == std::string::npos) return "";
        auto qstart = block.find('"', colon + 1);
        if (qstart == std::string::npos) return "";
        auto qend = block.find('"', qstart + 1);
        if (qend == std::string::npos) return "";
        return block.substr(qstart + 1, qend - qstart - 1);
    };

    auto connStart = content.find("\"connections\"");
    if (connStart == std::string::npos) return input;

    auto arrStart = content.find('[', connStart);
    if (arrStart == std::string::npos) return input;

    size_t pos = arrStart;
    while (true) {
        auto objStart = content.find('{', pos);
        if (objStart == std::string::npos) break;
        auto objEnd = content.find('}', objStart);
        if (objEnd == std::string::npos) break;

        std::string block = content.substr(objStart, objEnd - objStart + 1);
        std::string source = findValue(block, "source");
        std::string dest = findValue(block, "dest");
        std::string status = findValue(block, "status");

        if (!source.empty() && !dest.empty()) {
            // Strip width annotations like [31:0]
            auto stripWidth = [](std::string& s) {
                auto bracket = s.find('[');
                if (bracket != std::string::npos) s = s.substr(0, bracket);
            };
            stripWidth(source);
            stripWidth(dest);
            input.connections.push_back({source, dest, status});
        }

        pos = objEnd + 1;
        // Stop if we hit the end of the connections array
        if (content[pos] == ']' || (pos < content.size() && content.find(']', pos) < content.find('{', pos))) {
            break;
        }
    }

    return input;
}

} // namespace connect
```

- [ ] **Step 4: CMakeLists 업데이트, 빌드 + 테스트**

`CMakeLists.txt` OBJECT lib에 `src/GraphDiff.cpp` 추가.
`tests/CMakeLists.txt`에 `test_graph_diff.cpp` 추가.

Run: `cmake --build build && cd build && ctest --test-dir . -R GraphDiff --output-on-failure`
Expected: 4개 테스트 모두 PASS

- [ ] **Step 5: Commit**

```bash
git add src/GraphDiff.h src/GraphDiff.cpp tests/test_graph_diff.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add connection graph diff engine for baseline comparison"
```

---

### Task 6: --diff CLI 통합

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: CliOptions에 diffFile 필드 추가**

`src/main.cpp`의 `CliOptions` struct에 추가:
```cpp
    std::string diffFile;
```

- [ ] **Step 2: parseCustomArgs에 --diff 파싱 추가**

```cpp
        } else if (arg == "--diff") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --diff requires a value\n"); return opts; }
            opts.diffFile = argv[++i];
```

- [ ] **Step 3: main()에서 diff 모드 처리**

Phase 6 (report generation) 뒤, Phase 7 (exit code) 앞에 추가:

```cpp
    // --- Phase 6.5: Diff mode ---
    if (!opts.diffFile.empty()) {
        try {
            auto baseline = connect::loadDiffInputFromJson(opts.diffFile);

            // Build current DiffInput from reportData
            connect::DiffInput current;
            for (const auto& conn : reportData.graph.connections) {
                std::string status = "OK";
                for (const auto& issue : reportData.active) {
                    if (issue.connection.has_value() &&
                        issue.connection->source.fullPath() == conn.source.fullPath() &&
                        issue.connection->dest.fullPath() == conn.dest.fullPath()) {
                        status = connect::Issue::typeToString(issue.type);
                        break;
                    }
                }
                current.connections.push_back({conn.source.fullPath(), conn.dest.fullPath(), status});
            }

            auto diff = connect::computeDiff(baseline, current);

            if (diff.empty()) {
                fmt::print("\nDiff: no connectivity changes vs baseline.\n");
            } else {
                fmt::print("\n=== Connectivity Diff vs {} ===\n", opts.diffFile);
                for (const auto& c : diff.added) {
                    fmt::print("  + ADDED:   {} → {}  [{}]\n", c.source, c.dest, c.status);
                }
                for (const auto& c : diff.removed) {
                    fmt::print("  - REMOVED: {} → {}  [was: {}]\n", c.source, c.dest, c.status);
                }
                for (const auto& c : diff.changed) {
                    fmt::print("  ~ CHANGED: {} → {}  status: {} → {}\n",
                               c.source, c.dest, c.oldStatus, c.newStatus);
                }
                fmt::print("  Total: +{} -{} ~{}\n",
                           diff.added.size(), diff.removed.size(), diff.changed.size());
            }
        } catch (const std::exception& e) {
            fmt::print(stderr, "Error loading diff baseline: {}\n", e.what());
        }
    }
```

Include 추가: `#include "GraphDiff.h"`

- [ ] **Step 4: 통합 테스트 (수동)**

```bash
# 1. Generate baseline
./build/slang-connect tests/sv/multi_module_soc.sv --top soc_top --format json -o /tmp/baseline/

# 2. Run diff against itself (should show no changes)
./build/slang-connect tests/sv/multi_module_soc.sv --top soc_top --diff /tmp/baseline/connect_report.json --format table

# 3. Run diff against a different design (should show changes)
./build/slang-connect tests/sv/width_mismatch.sv --top top --diff /tmp/baseline/connect_report.json --format table
```

Expected: Step 2에서 "no connectivity changes", Step 3에서 added/removed 표시

- [ ] **Step 5: printUsage 업데이트 + Commit**

`--diff <file>` 옵션 설명을 help 텍스트에 추가.

```bash
git add src/main.cpp
git commit -m "feat: add --diff option for baseline connectivity comparison"
```

---

## Milestone 3: Expected Connectivity (v0.4)

> 핵심 가치: "RTL이 설계 의도대로 연결되었는지" CI에서 자동 검증

### Task 7: ExpectChecker 구현

**Files:**
- Create: `src/ExpectChecker.h`
- Create: `src/ExpectChecker.cpp`
- Create: `tests/test_expect_checker.cpp`
- Create: `tests/expected_conn.yaml` (테스트 fixture)
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

YAML expect 파일을 파싱하고, ConnectionGraph에 대해 검증한다.

- [ ] **Step 1: YAML expect 형식 정의 및 테스트 fixture 작성**

```yaml
# tests/expected_conn.yaml
expected:
  - from: "*.u_cpu.o_ibus_addr"
    to: "*.u_bus.i_cpu_ibus_addr"
  - from: "*.u_bus.o_mem_addr"
    to: "*.u_mem.i_addr"

forbidden:
  - from: "*.u_mem.*"
    to: "*.u_cpu.*"
```

- [ ] **Step 2: Failing test 작성**

```cpp
// tests/test_expect_checker.cpp
#include <catch2/catch_test_macros.hpp>
#include "ExpectChecker.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static ConnectionGraph makeExpectTestGraph() {
    ConnectionGraph graph;
    graph.topModule = "soc_top";

    PortInfo cpu_out;
    cpu_out.instancePath = "soc_top.u_cpu"; cpu_out.portName = "o_ibus_addr";
    cpu_out.direction = ArgumentDirection::Out; cpu_out.width = 32;

    PortInfo bus_in;
    bus_in.instancePath = "soc_top.u_bus"; bus_in.portName = "i_cpu_ibus_addr";
    bus_in.direction = ArgumentDirection::In; bus_in.width = 32;

    PortInfo bus_out;
    bus_out.instancePath = "soc_top.u_bus"; bus_out.portName = "o_mem_addr";
    bus_out.direction = ArgumentDirection::Out; bus_out.width = 32;

    PortInfo mem_in;
    mem_in.instancePath = "soc_top.u_mem"; mem_in.portName = "i_addr";
    mem_in.direction = ArgumentDirection::In; mem_in.width = 32;

    graph.connections.push_back({cpu_out, bus_in});
    graph.connections.push_back({bus_out, mem_in});
    graph.allPorts = {cpu_out, bus_in, bus_out, mem_in};

    return graph;
}

TEST_CASE("ExpectChecker: all expected connections satisfied") {
    auto graph = makeExpectTestGraph();
    ExpectChecker checker("expected_conn.yaml");
    auto issues = checker.check(graph);

    // All expected connections exist, no forbidden ones
    bool hasExpectFail = false;
    for (const auto& i : issues) {
        if (i.type == Issue::Type::EXPECT_MISSING || i.type == Issue::Type::EXPECT_FORBIDDEN) {
            hasExpectFail = true;
        }
    }
    CHECK_FALSE(hasExpectFail);
}

TEST_CASE("ExpectChecker: detects missing expected connection") {
    ConnectionGraph graph;
    graph.topModule = "soc_top";
    // Empty graph - no connections at all

    ExpectChecker checker("expected_conn.yaml");
    auto issues = checker.check(graph);

    // Should report missing expected connections
    int missingCount = 0;
    for (const auto& i : issues) {
        if (i.type == Issue::Type::EXPECT_MISSING) missingCount++;
    }
    CHECK(missingCount == 2); // two expected connections missing
}

TEST_CASE("ExpectChecker: detects forbidden connection") {
    auto graph = makeExpectTestGraph();

    // Add a forbidden connection: mem → cpu
    PortInfo mem_out;
    mem_out.instancePath = "soc_top.u_mem"; mem_out.portName = "o_rdata";
    mem_out.direction = ArgumentDirection::Out; mem_out.width = 32;

    PortInfo cpu_in;
    cpu_in.instancePath = "soc_top.u_cpu"; cpu_in.portName = "i_secret";
    cpu_in.direction = ArgumentDirection::In; cpu_in.width = 32;

    graph.connections.push_back({mem_out, cpu_in});

    ExpectChecker checker("expected_conn.yaml");
    auto issues = checker.check(graph);

    int forbiddenCount = 0;
    for (const auto& i : issues) {
        if (i.type == Issue::Type::EXPECT_FORBIDDEN) forbiddenCount++;
    }
    CHECK(forbiddenCount == 1);
}
```

- [ ] **Step 3: Issue::Type 확장**

`src/Issue.h`의 `Type` enum에 추가:

```cpp
        EXPECT_MISSING,
        EXPECT_FORBIDDEN
```

`typeToString`에도 case 추가:

```cpp
            case Type::EXPECT_MISSING:  return "EXPECT_MISSING";
            case Type::EXPECT_FORBIDDEN: return "EXPECT_FORBIDDEN";
```

- [ ] **Step 4: ExpectChecker 헤더 + 구현 작성**

```cpp
// src/ExpectChecker.h
#pragma once
#include "Checker.h"
#include <string>
#include <vector>

namespace connect {

struct ExpectRule {
    std::string from;  // glob pattern for source
    std::string to;    // glob pattern for dest
};

class ExpectChecker : public IChecker {
public:
    explicit ExpectChecker(const std::string& yamlPath);
    std::vector<Issue> check(const ConnectionGraph& graph) const override;

private:
    std::vector<ExpectRule> expected_;
    std::vector<ExpectRule> forbidden_;

    static bool globMatch(const std::string& pattern, const std::string& text);
};

} // namespace connect
```

```cpp
// src/ExpectChecker.cpp
#include "ExpectChecker.h"
#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

namespace connect {

ExpectChecker::ExpectChecker(const std::string& yamlPath) {
    YAML::Node root = YAML::LoadFile(yamlPath);

    if (root["expected"]) {
        for (const auto& node : root["expected"]) {
            expected_.push_back({node["from"].as<std::string>(), node["to"].as<std::string>()});
        }
    }
    if (root["forbidden"]) {
        for (const auto& node : root["forbidden"]) {
            forbidden_.push_back({node["from"].as<std::string>(), node["to"].as<std::string>()});
        }
    }
}

bool ExpectChecker::globMatch(const std::string& pattern, const std::string& text) {
    // Simple glob: * matches any sequence (including dots for path matching)
    size_t pi = 0, ti = 0;
    size_t starP = std::string::npos, starT = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
            ++pi; ++ti;
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
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

std::vector<Issue> ExpectChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Check expected connections
    for (const auto& rule : expected_) {
        bool found = false;
        for (const auto& conn : graph.connections) {
            if (globMatch(rule.from, conn.source.fullPath()) &&
                globMatch(rule.to, conn.dest.fullPath())) {
                found = true;
                break;
            }
        }
        if (!found) {
            Issue issue;
            issue.type = Issue::Type::EXPECT_MISSING;
            issue.severity = Issue::Severity::ERROR;
            issue.port = {}; // no specific port
            issue.port.instancePath = rule.from;
            issue.port.portName = "";
            issue.detail = fmt::format("Expected connection not found: {} -> {}", rule.from, rule.to);
            issues.push_back(std::move(issue));
        }
    }

    // Check forbidden connections
    for (const auto& conn : graph.connections) {
        for (const auto& rule : forbidden_) {
            if (globMatch(rule.from, conn.source.fullPath()) &&
                globMatch(rule.to, conn.dest.fullPath())) {
                Issue issue;
                issue.type = Issue::Type::EXPECT_FORBIDDEN;
                issue.severity = Issue::Severity::ERROR;
                issue.port = conn.source;
                issue.connection = conn;
                issue.detail = fmt::format("Forbidden connection: {} -> {} (rule: {} -> {})",
                                          conn.source.fullPath(), conn.dest.fullPath(),
                                          rule.from, rule.to);
                issues.push_back(std::move(issue));
            }
        }
    }

    return issues;
}

} // namespace connect
```

- [ ] **Step 5: CMakeLists 업데이트, 빌드 + 테스트**

`CMakeLists.txt` OBJECT lib에 `src/ExpectChecker.cpp` 추가.
`tests/CMakeLists.txt`에 `test_expect_checker.cpp` 추가, `expected_conn.yaml` 복사 추가.

Run: `cmake --build build && cd build && ctest --test-dir . -R ExpectChecker --output-on-failure`
Expected: 3개 테스트 모두 PASS

- [ ] **Step 6: main.cpp에 --expect 옵션 통합**

`CliOptions`에 `std::string expectFile;` 추가.
파싱에 `--expect` 처리 추가.
checker 등록 부분에:
```cpp
    if (!opts.expectFile.empty()) {
        runner.addChecker(std::make_unique<connect::ExpectChecker>(opts.expectFile));
    }
```

- [ ] **Step 7: Commit**

```bash
git add src/ExpectChecker.h src/ExpectChecker.cpp src/Issue.h tests/test_expect_checker.cpp tests/expected_conn.yaml CMakeLists.txt tests/CMakeLists.txt src/main.cpp
git commit -m "feat: add expected connectivity spec checker (--expect option)"
```

---

## Milestone 4: Clock/Reset Topology (v0.5)

> 핵심 가치: "어떤 클럭이 어디로 가는지" 한눈에 파악, CDC 분석 전 sanity check

### Task 8: ClockResetAnalyzer 구현

**Files:**
- Create: `src/ClockResetAnalyzer.h`
- Create: `src/ClockResetAnalyzer.cpp`
- Create: `tests/test_clock_reset.cpp`
- Create: `tests/sv/clock_reset.sv`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

포트 이름 패턴(`clk`, `clock`, `rst`, `reset`)을 기반으로 클럭/리셋 포트를 분류하고, topology를 추출한다.

- [ ] **Step 1: 테스트 SV 작성**

```systemverilog
// tests/sv/clock_reset.sv
module core (
    input  logic sys_clk,
    input  logic sys_rst_n,
    input  logic [31:0] i_data,
    output logic [31:0] o_data
);
    assign o_data = i_data;
endmodule

module periph (
    input  logic peri_clk,
    input  logic peri_rst_n,
    input  logic [7:0] i_cfg,
    output logic [7:0] o_status
);
    assign o_status = i_cfg;
endmodule

module noreset_block (
    input  logic clk,
    input  logic [3:0] i_val,
    output logic [3:0] o_val
);
    assign o_val = i_val;
endmodule

module clk_rst_top (
    input  logic sys_clk,
    input  logic peri_clk,
    input  logic sys_rst_n,
    input  logic peri_rst_n
);
    logic [31:0] core_data;
    logic [7:0]  periph_cfg, periph_status;
    logic [3:0]  nr_val;

    core u_core (
        .sys_clk(sys_clk), .sys_rst_n(sys_rst_n),
        .i_data(32'h0), .o_data(core_data)
    );

    periph u_periph (
        .peri_clk(peri_clk), .peri_rst_n(peri_rst_n),
        .i_cfg(8'h0), .o_status(periph_status)
    );

    noreset_block u_norst (
        .clk(sys_clk),
        .i_val(4'h0), .o_val(nr_val)
    );
endmodule
```

- [ ] **Step 2: Failing test 작성**

```cpp
// tests/test_clock_reset.cpp
#include <catch2/catch_test_macros.hpp>
#include "ClockResetAnalyzer.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static ConnectionGraph makeClkRstGraph() {
    ConnectionGraph graph;
    graph.topModule = "clk_rst_top";

    auto makePort = [](const std::string& inst, const std::string& name, ArgumentDirection dir) {
        PortInfo p;
        p.instancePath = inst; p.portName = name; p.direction = dir; p.width = 1;
        return p;
    };

    // u_core gets sys_clk, sys_rst_n
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "sys_rst_n", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "i_data", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_core", "o_data", ArgumentDirection::Out));

    // u_periph gets peri_clk, peri_rst_n
    graph.allPorts.push_back(makePort("clk_rst_top.u_periph", "peri_clk", ArgumentDirection::In));
    graph.allPorts.push_back(makePort("clk_rst_top.u_periph", "peri_rst_n", ArgumentDirection::In));

    // u_norst gets clk only (no reset)
    graph.allPorts.push_back(makePort("clk_rst_top.u_norst", "clk", ArgumentDirection::In));

    return graph;
}

TEST_CASE("ClockResetAnalyzer: identifies clock ports") {
    auto graph = makeClkRstGraph();
    ClockResetAnalyzer analyzer;
    auto topology = analyzer.analyze(graph);

    CHECK(topology.clockGroups.size() >= 2); // sys_clk group and peri_clk group
}

TEST_CASE("ClockResetAnalyzer: identifies reset ports") {
    auto graph = makeClkRstGraph();
    ClockResetAnalyzer analyzer;
    auto topology = analyzer.analyze(graph);

    CHECK(topology.resetGroups.size() >= 2); // sys_rst_n and peri_rst_n
}

TEST_CASE("ClockResetAnalyzer: warns on missing reset") {
    auto graph = makeClkRstGraph();
    ClockResetAnalyzer analyzer;
    auto topology = analyzer.analyze(graph);

    // u_norst has clock but no reset
    bool foundWarning = false;
    for (const auto& w : topology.warnings) {
        if (w.find("u_norst") != std::string::npos && w.find("reset") != std::string::npos) {
            foundWarning = true;
        }
    }
    CHECK(foundWarning);
}
```

- [ ] **Step 3: ClockResetAnalyzer 헤더 + 구현 작성**

```cpp
// src/ClockResetAnalyzer.h
#pragma once
#include "ConnectionGraph.h"
#include <map>
#include <string>
#include <vector>

namespace connect {

struct ClockResetTopology {
    // clock_name → list of instance paths
    std::map<std::string, std::vector<std::string>> clockGroups;
    // reset_name → list of instance paths
    std::map<std::string, std::vector<std::string>> resetGroups;
    // Instances with clock but no reset
    std::vector<std::string> warnings;
};

class ClockResetAnalyzer {
public:
    ClockResetTopology analyze(const ConnectionGraph& graph) const;

    static bool isClockPort(const std::string& portName);
    static bool isResetPort(const std::string& portName);
};

} // namespace connect
```

```cpp
// src/ClockResetAnalyzer.cpp
#include "ClockResetAnalyzer.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace connect {

namespace {
std::string toLower(const std::string& s) {
    std::string result = s;
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
} // anonymous namespace

bool ClockResetAnalyzer::isClockPort(const std::string& portName) {
    std::string lower = toLower(portName);
    // Match: clk, clock, *_clk, *_clock, clk_*
    return lower == "clk" || lower == "clock" ||
           lower.find("_clk") != std::string::npos ||
           lower.find("clk_") != std::string::npos ||
           lower.find("_clock") != std::string::npos ||
           lower.find("clock_") != std::string::npos;
}

bool ClockResetAnalyzer::isResetPort(const std::string& portName) {
    std::string lower = toLower(portName);
    return lower == "rst" || lower == "rst_n" || lower == "reset" || lower == "reset_n" ||
           lower.find("_rst") != std::string::npos ||
           lower.find("rst_") != std::string::npos ||
           lower.find("_reset") != std::string::npos ||
           lower.find("reset_") != std::string::npos;
}

ClockResetTopology ClockResetAnalyzer::analyze(const ConnectionGraph& graph) const {
    ClockResetTopology topology;

    // Sets to track which instances have clocks and resets
    std::set<std::string> instancesWithClock;
    std::set<std::string> instancesWithReset;
    std::set<std::string> allInstances;

    for (const auto& port : graph.allPorts) {
        if (port.instancePath == graph.topModule) continue; // skip top-level
        allInstances.insert(port.instancePath);

        if (port.direction != slang::ast::ArgumentDirection::In) continue;

        if (isClockPort(port.portName)) {
            topology.clockGroups[port.portName].push_back(port.instancePath);
            instancesWithClock.insert(port.instancePath);
        } else if (isResetPort(port.portName)) {
            topology.resetGroups[port.portName].push_back(port.instancePath);
            instancesWithReset.insert(port.instancePath);
        }
    }

    // Warn: instances with clock but no reset
    for (const auto& inst : instancesWithClock) {
        if (instancesWithReset.find(inst) == instancesWithReset.end()) {
            auto label = inst.substr(inst.rfind('.') + 1);
            topology.warnings.push_back(
                inst + " (" + label + "): has clock input but no reset port detected");
        }
    }

    return topology;
}

} // namespace connect
```

- [ ] **Step 4: CMakeLists 업데이트, 빌드 + 테스트**

Run: `cmake --build build && cd build && ctest --test-dir . -R ClockReset --output-on-failure`
Expected: 3개 테스트 모두 PASS

- [ ] **Step 5: main.cpp에 --check-clock-reset 옵션 + topology 출력 통합**

report 생성 후, clock/reset topology를 table 형식으로 stdout에 출력:

```cpp
    // --- Clock/Reset Topology ---
    if (opts.checkClockReset) {
        connect::ClockResetAnalyzer crAnalyzer;
        auto topology = crAnalyzer.analyze(reportData.graph);

        fmt::print("\n=== Clock Topology ===\n");
        for (const auto& [clkName, instances] : topology.clockGroups) {
            fmt::print("  {} → ", clkName);
            for (size_t i = 0; i < instances.size(); ++i) {
                if (i > 0) fmt::print(", ");
                auto label = instances[i].substr(instances[i].rfind('.') + 1);
                fmt::print("{}", label);
            }
            fmt::print("\n");
        }

        fmt::print("\n=== Reset Topology ===\n");
        for (const auto& [rstName, instances] : topology.resetGroups) {
            fmt::print("  {} → ", rstName);
            for (size_t i = 0; i < instances.size(); ++i) {
                if (i > 0) fmt::print(", ");
                auto label = instances[i].substr(instances[i].rfind('.') + 1);
                fmt::print("{}", label);
            }
            fmt::print("\n");
        }

        if (!topology.warnings.empty()) {
            fmt::print("\n=== Clock/Reset Warnings ===\n");
            for (const auto& w : topology.warnings) {
                fmt::print("  ⚠ {}\n", w);
            }
        }
    }
```

- [ ] **Step 6: Commit**

```bash
git add src/ClockResetAnalyzer.h src/ClockResetAnalyzer.cpp tests/test_clock_reset.cpp tests/sv/clock_reset.sv CMakeLists.txt tests/CMakeLists.txt src/main.cpp
git commit -m "feat: add clock/reset topology analysis (--check-clock-reset)"
```

---

## Milestone 5: Interface Grouping (v0.6)

> 핵심 가치: 포트 500개 나열 대신 "AXI master 2개, APB slave 3개"로 요약

### Task 9: InterfaceGrouper 구현

**Files:**
- Create: `src/InterfaceGrouper.h`
- Create: `src/InterfaceGrouper.cpp`
- Create: `tests/test_interface_grouper.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

기존 `ProtocolChecker`의 프로토콜 정의를 재활용하여, 포트를 논리적 인터페이스 그룹으로 분류한다. 그룹 정보는 리포트 (특히 HTML, DOT) 에서 활용된다.

- [ ] **Step 1: Failing test 작성**

```cpp
// tests/test_interface_grouper.cpp
#include <catch2/catch_test_macros.hpp>
#include "InterfaceGrouper.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static ConnectionGraph makeIfGroupTestGraph() {
    ConnectionGraph graph;
    graph.topModule = "top";

    auto makePort = [](const std::string& inst, const std::string& name,
                       ArgumentDirection dir, uint32_t w = 1) {
        PortInfo p;
        p.instancePath = inst; p.portName = name; p.direction = dir; p.width = w;
        return p;
    };

    std::string inst = "top.u_master";
    // AXI write address channel signals
    graph.allPorts.push_back(makePort(inst, "m_axi_awvalid", ArgumentDirection::Out));
    graph.allPorts.push_back(makePort(inst, "m_axi_awready", ArgumentDirection::In));
    graph.allPorts.push_back(makePort(inst, "m_axi_awaddr", ArgumentDirection::Out, 32));
    graph.allPorts.push_back(makePort(inst, "m_axi_awlen", ArgumentDirection::Out, 8));
    graph.allPorts.push_back(makePort(inst, "m_axi_awsize", ArgumentDirection::Out, 3));
    graph.allPorts.push_back(makePort(inst, "m_axi_awburst", ArgumentDirection::Out, 2));
    // AXI write data channel
    graph.allPorts.push_back(makePort(inst, "m_axi_wvalid", ArgumentDirection::Out));
    graph.allPorts.push_back(makePort(inst, "m_axi_wready", ArgumentDirection::In));
    graph.allPorts.push_back(makePort(inst, "m_axi_wdata", ArgumentDirection::Out, 64));
    // Non-AXI port
    graph.allPorts.push_back(makePort(inst, "irq", ArgumentDirection::In));

    return graph;
}

TEST_CASE("InterfaceGrouper: detects AXI interface") {
    auto graph = makeIfGroupTestGraph();
    InterfaceGrouper grouper;
    auto groups = grouper.group(graph);

    // Should find at least one AXI group on u_master
    bool foundAxi = false;
    for (const auto& g : groups) {
        if (g.protocol == "AXI4" && g.instancePath == "top.u_master") {
            foundAxi = true;
            CHECK(g.matchedPorts.size() >= 6); // AW + W channel signals
        }
    }
    CHECK(foundAxi);
}

TEST_CASE("InterfaceGrouper: non-protocol ports are ungrouped") {
    auto graph = makeIfGroupTestGraph();
    InterfaceGrouper grouper;
    auto groups = grouper.group(graph);

    // irq should not be in any protocol group
    for (const auto& g : groups) {
        for (const auto& p : g.matchedPorts) {
            CHECK(p.portName != "irq");
        }
    }
}

TEST_CASE("InterfaceGrouper: detects role from direction") {
    auto graph = makeIfGroupTestGraph();
    InterfaceGrouper grouper;
    auto groups = grouper.group(graph);

    for (const auto& g : groups) {
        if (g.protocol == "AXI4" && g.instancePath == "top.u_master") {
            // AWVALID is output → master role
            CHECK(g.role == "master");
        }
    }
}
```

- [ ] **Step 2: InterfaceGrouper 헤더 + 구현 작성**

```cpp
// src/InterfaceGrouper.h
#pragma once
#include "ConnectionGraph.h"
#include <string>
#include <vector>

namespace connect {

struct InterfaceGroup {
    std::string instancePath;
    std::string protocol;     // "AXI4", "AXI4-Lite", "AXI-Stream", "AHB", "APB"
    std::string role;         // "master" or "slave"
    std::string prefix;       // common prefix (e.g., "m_axi_")
    std::vector<PortInfo> matchedPorts;
};

class InterfaceGrouper {
public:
    std::vector<InterfaceGroup> group(const ConnectionGraph& graph) const;
};

} // namespace connect
```

```cpp
// src/InterfaceGrouper.cpp
#include "InterfaceGrouper.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace connect {

namespace {

std::string toUpper(const std::string& s) {
    std::string result = s;
    for (auto& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

bool hasSuffix(const std::string& upper, const std::string& signal) {
    if (upper.size() < signal.size()) return false;
    auto pos = upper.rfind(signal);
    if (pos == std::string::npos || pos + signal.size() != upper.size()) return false;
    if (pos > 0 && upper[pos - 1] != '_') return false;
    return true;
}

struct ProtoSignal {
    std::string name;
    bool isValidIndicator; // true for *VALID signals (used to determine role)
};

struct ProtoSpec {
    std::string name;
    std::vector<ProtoSignal> keySignals; // minimum signals to identify protocol
};

std::vector<ProtoSpec> getProtocolSpecs() {
    return {
        {"AXI4", {
            {"AWVALID", true}, {"AWREADY", false}, {"AWADDR", false},
            {"WVALID", true}, {"WREADY", false}, {"WDATA", false},
            {"BVALID", false}, {"BREADY", false}}},
        {"AXI4-Lite", {
            {"AWVALID", true}, {"AWREADY", false}, {"AWADDR", false},
            {"WVALID", true}, {"WREADY", false}, {"WDATA", false}}},
        {"AXI-Stream", {
            {"TVALID", true}, {"TREADY", false}, {"TDATA", false}}},
        {"AHB", {
            {"HSEL", false}, {"HADDR", false}, {"HTRANS", false},
            {"HWDATA", false}, {"HRDATA", false}, {"HREADY", false}}},
        {"APB", {
            {"PSEL", false}, {"PENABLE", false}, {"PWRITE", false},
            {"PADDR", false}, {"PWDATA", false}, {"PRDATA", false}}}
    };
}

} // anonymous namespace

std::vector<InterfaceGroup> InterfaceGrouper::group(const ConnectionGraph& graph) const {
    std::vector<InterfaceGroup> result;
    auto specs = getProtocolSpecs();

    // Group ports by instance
    std::map<std::string, std::vector<const PortInfo*>> instancePorts;
    for (const auto& p : graph.allPorts) {
        instancePorts[p.instancePath].push_back(&p);
    }

    for (const auto& [inst, ports] : instancePorts) {
        for (const auto& spec : specs) {
            std::vector<PortInfo> matched;
            bool foundValid = false;
            bool validIsOutput = false;

            int matchCount = 0;
            for (const auto& sig : spec.keySignals) {
                for (const auto* port : ports) {
                    if (hasSuffix(toUpper(port->portName), sig.name)) {
                        matched.push_back(*port);
                        matchCount++;
                        if (sig.isValidIndicator) {
                            foundValid = true;
                            validIsOutput = (port->direction == slang::ast::ArgumentDirection::Out);
                        }
                        break;
                    }
                }
            }

            // Need at least 60% of key signals to match
            double ratio = static_cast<double>(matchCount) / static_cast<double>(spec.keySignals.size());
            if (ratio < 0.6) continue;

            // Avoid AXI4-Lite matching when AXI4 already matched (AXI4 has more signals)
            if (spec.name == "AXI4-Lite") {
                bool alreadyAxi4 = false;
                for (const auto& g : result) {
                    if (g.instancePath == inst && g.protocol == "AXI4") {
                        alreadyAxi4 = true; break;
                    }
                }
                if (alreadyAxi4) continue;
            }

            // Determine role: if *VALID is output → master; if input → slave
            std::string role = foundValid ? (validIsOutput ? "master" : "slave") : "unknown";

            // Find common prefix
            std::string prefix;
            if (!matched.empty()) {
                // Heuristic: take port name up to the protocol signal suffix
                const auto& first = matched[0].portName;
                auto upper = toUpper(first);
                for (const auto& sig : spec.keySignals) {
                    auto pos = upper.rfind(sig.name);
                    if (pos != std::string::npos && pos > 0) {
                        prefix = first.substr(0, pos);
                        break;
                    }
                }
            }

            // Also add any other ports with the same prefix that weren't in keySignals
            if (!prefix.empty()) {
                for (const auto* port : ports) {
                    if (port->portName.substr(0, prefix.size()) == prefix) {
                        bool alreadyIn = false;
                        for (const auto& m : matched) {
                            if (m.portName == port->portName) { alreadyIn = true; break; }
                        }
                        if (!alreadyIn) matched.push_back(*port);
                    }
                }
            }

            result.push_back({inst, spec.name, role, prefix, std::move(matched)});
        }
    }

    return result;
}

} // namespace connect
```

- [ ] **Step 3: CMakeLists 업데이트, 빌드 + 테스트**

Run: `cmake --build build && cd build && ctest --test-dir . -R InterfaceGrouper --output-on-failure`
Expected: 3개 테스트 모두 PASS

- [ ] **Step 4: main.cpp에 인터페이스 요약 출력 추가**

`--format table` 또는 `--format all` 일 때 인터페이스 요약을 stdout에 출력:

```cpp
    // --- Interface Summary (always shown with table output) ---
    if (opts.format == "table" || opts.format == "all") {
        connect::InterfaceGrouper grouper;
        auto groups = grouper.group(reportData.graph);
        if (!groups.empty()) {
            fmt::print("\n=== Interface Summary ===\n");
            for (const auto& g : groups) {
                auto label = g.instancePath.substr(g.instancePath.rfind('.') + 1);
                fmt::print("  {} : {} {} ({} signals, prefix: {})\n",
                           label, g.protocol, g.role,
                           g.matchedPorts.size(), g.prefix.empty() ? "-" : g.prefix);
            }
        }
    }
```

- [ ] **Step 5: Commit**

```bash
git add src/InterfaceGrouper.h src/InterfaceGrouper.cpp tests/test_interface_grouper.cpp CMakeLists.txt tests/CMakeLists.txt src/main.cpp
git commit -m "feat: add interface grouper for protocol-level connectivity summary"
```

---

## Milestone 6: Signal Trace (v0.7)

> 핵심 가치: "이 신호가 어디서 시작해서 어디까지 가는지" hierarchy를 따라 추적

### Task 10: TraceEngine 구현

**Files:**
- Create: `src/TraceEngine.h`
- Create: `src/TraceEngine.cpp`
- Create: `tests/test_trace_engine.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/main.cpp`

ConnectionGraph 위에서 BFS/DFS로 fan-out (output port에서 시작) 또는 fan-in (input port에서 시작) 을 추적한다.

- [ ] **Step 1: Failing test 작성**

```cpp
// tests/test_trace_engine.cpp
#include <catch2/catch_test_macros.hpp>
#include "TraceEngine.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static ConnectionGraph makeTraceTestGraph() {
    ConnectionGraph graph;
    graph.topModule = "soc_top";

    auto makePort = [](const std::string& inst, const std::string& name,
                       ArgumentDirection dir, uint32_t w = 32) {
        PortInfo p;
        p.instancePath = inst; p.portName = name; p.direction = dir; p.width = w;
        return p;
    };

    PortInfo cpu_out = makePort("soc_top.u_cpu", "o_addr", ArgumentDirection::Out);
    PortInfo bus_in  = makePort("soc_top.u_bus", "i_addr", ArgumentDirection::In);
    PortInfo bus_out = makePort("soc_top.u_bus", "o_mem_addr", ArgumentDirection::Out, 24);
    PortInfo mem_in  = makePort("soc_top.u_mem", "i_addr", ArgumentDirection::In, 24);

    graph.connections.push_back({cpu_out, bus_in});
    graph.connections.push_back({bus_out, mem_in});
    graph.allPorts = {cpu_out, bus_in, bus_out, mem_in};

    return graph;
}

TEST_CASE("TraceEngine: fan-out from source traces through chain") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);
    auto path = engine.traceFanOut("soc_top.u_cpu.o_addr");

    // cpu.o_addr → bus.i_addr, then bus.o_mem_addr → mem.i_addr
    REQUIRE(path.size() >= 2);
    CHECK(path[0].dest.instancePath == "soc_top.u_bus");
}

TEST_CASE("TraceEngine: fan-in to destination traces back to source") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);
    auto path = engine.traceFanIn("soc_top.u_mem.i_addr");

    REQUIRE(path.size() >= 1);
    CHECK(path[0].source.instancePath == "soc_top.u_bus");
}

TEST_CASE("TraceEngine: trace with glob pattern") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);
    auto path = engine.traceFanOut("*.u_cpu.o_addr");

    CHECK(path.size() >= 2);
}

TEST_CASE("TraceEngine: nonexistent signal returns empty") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);
    auto path = engine.traceFanOut("soc_top.u_cpu.o_nonexistent");

    CHECK(path.empty());
}
```

- [ ] **Step 2: TraceEngine 헤더 + 구현 작성**

```cpp
// src/TraceEngine.h
#pragma once
#include "ConnectionGraph.h"
#include <string>
#include <vector>

namespace connect {

struct TraceHop {
    Connection connection;
    int depth;
};

class TraceEngine {
public:
    explicit TraceEngine(const ConnectionGraph& graph);

    // Trace forward from an output port (fan-out)
    std::vector<TraceHop> traceFanOut(const std::string& portPattern, int maxDepth = 16) const;

    // Trace backward to an input port (fan-in)
    std::vector<TraceHop> traceFanIn(const std::string& portPattern, int maxDepth = 16) const;

private:
    const ConnectionGraph& graph_;

    static bool matchPattern(const std::string& pattern, const std::string& text);

    // Find connections where source matches the given instance
    std::vector<const Connection*> findFromInstance(const std::string& instancePath) const;
    // Find connections where dest matches the given instance
    std::vector<const Connection*> findToInstance(const std::string& instancePath) const;
};

} // namespace connect
```

```cpp
// src/TraceEngine.cpp
#include "TraceEngine.h"
#include <queue>
#include <set>

namespace connect {

TraceEngine::TraceEngine(const ConnectionGraph& graph) : graph_(graph) {}

bool TraceEngine::matchPattern(const std::string& pattern, const std::string& text) {
    size_t pi = 0, ti = 0;
    size_t starP = std::string::npos, starT = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
            ++pi; ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starP = pi++; starT = ti;
        } else if (starP != std::string::npos) {
            pi = starP + 1; ti = ++starT;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

std::vector<const Connection*> TraceEngine::findFromInstance(const std::string& instancePath) const {
    std::vector<const Connection*> result;
    for (const auto& conn : graph_.connections) {
        if (conn.source.instancePath == instancePath) {
            result.push_back(&conn);
        }
    }
    return result;
}

std::vector<const Connection*> TraceEngine::findToInstance(const std::string& instancePath) const {
    std::vector<const Connection*> result;
    for (const auto& conn : graph_.connections) {
        if (conn.dest.instancePath == instancePath) {
            result.push_back(&conn);
        }
    }
    return result;
}

std::vector<TraceHop> TraceEngine::traceFanOut(const std::string& portPattern, int maxDepth) const {
    std::vector<TraceHop> result;
    std::set<std::string> visited;

    // Find starting connections: source port matches pattern
    std::queue<std::pair<const Connection*, int>> workQueue;
    for (const auto& conn : graph_.connections) {
        if (matchPattern(portPattern, conn.source.fullPath())) {
            workQueue.push({&conn, 0});
        }
    }

    while (!workQueue.empty()) {
        auto [conn, depth] = workQueue.front();
        workQueue.pop();

        std::string key = conn->source.fullPath() + "->" + conn->dest.fullPath();
        if (visited.count(key)) continue;
        visited.insert(key);

        result.push_back({*conn, depth});

        if (depth + 1 >= maxDepth) continue;

        // Continue from the destination instance's output ports
        auto nextConns = findFromInstance(conn->dest.instancePath);
        for (const auto* next : nextConns) {
            workQueue.push({next, depth + 1});
        }
    }

    return result;
}

std::vector<TraceHop> TraceEngine::traceFanIn(const std::string& portPattern, int maxDepth) const {
    std::vector<TraceHop> result;
    std::set<std::string> visited;

    // Find starting connections: dest port matches pattern
    std::queue<std::pair<const Connection*, int>> workQueue;
    for (const auto& conn : graph_.connections) {
        if (matchPattern(portPattern, conn.dest.fullPath())) {
            workQueue.push({&conn, 0});
        }
    }

    while (!workQueue.empty()) {
        auto [conn, depth] = workQueue.front();
        workQueue.pop();

        std::string key = conn->source.fullPath() + "->" + conn->dest.fullPath();
        if (visited.count(key)) continue;
        visited.insert(key);

        result.push_back({*conn, depth});

        if (depth + 1 >= maxDepth) continue;

        // Continue backward from the source instance's input ports
        auto prevConns = findToInstance(conn->source.instancePath);
        for (const auto* prev : prevConns) {
            workQueue.push({prev, depth + 1});
        }
    }

    return result;
}

} // namespace connect
```

- [ ] **Step 3: CMakeLists 업데이트, 빌드 + 테스트**

Run: `cmake --build build && cd build && ctest --test-dir . -R TraceEngine --output-on-failure`
Expected: 4개 테스트 모두 PASS

- [ ] **Step 4: main.cpp에 --trace 옵션 통합**

`CliOptions`에 `std::string traceSignal;` 추가.
파싱에 `--trace` 처리 추가.
Report 출력 후:

```cpp
    // --- Signal Trace ---
    if (!opts.traceSignal.empty()) {
        connect::TraceEngine engine(reportData.graph);

        auto fanOut = engine.traceFanOut(opts.traceSignal);
        auto fanIn = engine.traceFanIn(opts.traceSignal);

        if (fanOut.empty() && fanIn.empty()) {
            fmt::print(stderr, "Trace: no connections found matching '{}'\n", opts.traceSignal);
        }

        if (!fanOut.empty()) {
            fmt::print("\n=== Fan-Out Trace: {} ===\n", opts.traceSignal);
            for (const auto& hop : fanOut) {
                std::string indent(hop.depth * 2 + 2, ' ');
                std::string widthNote;
                if (hop.connection.source.width != hop.connection.dest.width) {
                    widthNote = fmt::format(" ⚠ {}b→{}b",
                                           hop.connection.source.width, hop.connection.dest.width);
                }
                fmt::print("{}{} [{}b] → {} [{}b]{}\n",
                           indent,
                           hop.connection.source.fullPath(), hop.connection.source.width,
                           hop.connection.dest.fullPath(), hop.connection.dest.width,
                           widthNote);
            }
        }

        if (!fanIn.empty()) {
            fmt::print("\n=== Fan-In Trace: {} ===\n", opts.traceSignal);
            for (const auto& hop : fanIn) {
                std::string indent(hop.depth * 2 + 2, ' ');
                fmt::print("{}{} [{}b] ← {} [{}b]\n",
                           indent,
                           hop.connection.dest.fullPath(), hop.connection.dest.width,
                           hop.connection.source.fullPath(), hop.connection.source.width);
            }
        }
    }
```

- [ ] **Step 5: Commit**

```bash
git add src/TraceEngine.h src/TraceEngine.cpp tests/test_trace_engine.cpp CMakeLists.txt tests/CMakeLists.txt src/main.cpp
git commit -m "feat: add signal trace engine for fan-in/fan-out analysis (--trace)"
```

---

## Milestone Summary

| Version | Feature | New Files | Key CLI Option |
|---------|---------|-----------|----------------|
| v0.2 | DOT + HTML visualization | 5 files | `--format dot\|html` |
| v0.3 | Connection Diff | 2 files | `--diff <baseline.json>` |
| v0.4 | Expected Connectivity | 2 files | `--expect <spec.yaml>` |
| v0.5 | Clock/Reset Topology | 2 files | `--check-clock-reset` |
| v0.6 | Interface Grouping | 2 files | (auto with table/html) |
| v0.7 | Signal Trace | 2 files | `--trace <signal>` |

### Cross-Milestone Enhancement: HTML 뷰어 확장

v0.3–v0.7 의 새 기능들은 HTML 리포트에도 반영되어야 한다. 각 milestone commit 후, `html_template.h`에 해당 데이터를 표시하는 탭/패널을 추가하는 후속 작업이 필요하다:

- v0.3: Diff 결과를 "Changes" 탭으로 표시 (added=green, removed=red, changed=yellow)
- v0.4: Expected/Forbidden 위반을 그래프 위에 overlay
- v0.5: Clock/Reset topology를 별도 탭으로 표시 (클럭별 색상 구분)
- v0.6: 노드를 interface group 단위로 클러스터링
- v0.7: 클릭한 노드/엣지에서 trace 결과 하이라이트

이 HTML 확장은 각 milestone의 C++ 기능이 안정화된 뒤 별도 task로 진행한다.

---

## Dependencies Between Milestones

```
v0.2 (Visualization) ─── 독립, 먼저 진행
v0.3 (Diff)         ─── 독립 (JSON 리포트만 필요, v0.1에 이미 있음)
v0.4 (Expect)       ─── 독립 (IChecker 인터페이스 사용)
v0.5 (Clock/Reset)  ─── 독립
v0.6 (Interface)    ─── v0.2의 DOT/HTML에 반영 시 의존
v0.7 (Trace)        ─── 독립
```

v0.2–v0.5는 완전히 병렬 진행 가능. v0.6는 v0.2 이후에 진행하면 HTML 통합이 자연스러움. v0.7도 독립적이나, v0.6의 interface 정보를 trace에 활용하면 더 유용.
