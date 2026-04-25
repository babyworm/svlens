# Feature roadmap (post-v0.2.x)

This document captures planned features that go beyond the existing connectivity /
CDC / metrics surface area. Each item is sized as an independent PR (or PR
series) and includes the data flow, API surface, and tests required to ship.
Items are ordered by expected impact on real-world adoption.

Status legend: `proposed` (no work started), `scoped` (interface fixed, work
ready to begin), `in-progress`, `landed`.

---

## Track D: Analysis depth

### D1. CDC SVA assertion auto-generation -- `proposed`

**Why**: today CDC analysis emits `cdc_constraints.sdc` (timing helpers).
Adding `cdc_assertions.sva` lets simulation verify the same crossings the
static check identified, closing the loop between structural CDC and
dynamic verification.

**Output schema**: a new artifact `cdc_assertions.sva` written next to
`cdc_report.json`. Each crossing produces:

```systemverilog
// CDC-001: src_clk -> dst_clk via top.u_sync.q1/q2
property p_cdc_001_2ff_sync;
    @(posedge dst_clk) disable iff (!rst_n)
        $stable(top.u_sync.q1) || top.u_sync.q1 == $past(top.u_sync.d);
endproperty
a_cdc_001: assert property (p_cdc_001_2ff_sync);
```

For each synchronizer style svlens recognizes, generate the corresponding
property template. Unrecognized crossings emit `cover property` only.

**API**:

- New CLI flag `--sva` writes `cdc_assertions.sva`.
- `--sva-style {2ff,3ff,handshake,fifo}` overrides per-crossing detection.
- New JSON field `cdc_report.json:crossings[].sva_assertion_id` cross-links
  the report to the generated assertion identifier.

**Files to add / modify**:

- `src/cdc/sva_generator.{h,cpp}` -- new module, takes `CdcReport` and
  emits SVA file content.
- `src/CdcRunner.cpp` -- wire `--sva` into the writer.
- `tests/test_sva_generator.cpp` -- snapshot tests against fixtures in
  `tests/cdc/sva/`.
- `docs/schema/cdc_report.md` -- document the new optional field.

**Risk**: SVA hierarchical references depend on the full instance path; we
already capture this in the report. Risk is in the synchronizer-pattern
detection getting confused by mixed-style code; the `--sva-style` override
is the escape hatch.

---

### D2. Interface / modport semantic deepening -- `proposed`

**Why**: README currently flags interface/modport handling as "partial"
(`README.md:386-389`). Real SoC designs lean heavily on bus interfaces, so
this is the largest gap blocking adoption.

**Approach**: extend `ConnectionExtractor` to walk
`InterfaceInstanceSymbol` and `ModportSymbol` from slang's AST, producing
per-modport-signal edges instead of opaque interface ports.

**Files to modify**:

- `src/ConnectionExtractor.cpp` -- new visitor for interface symbols.
- `src/InterfaceGrouper.cpp` -- already exists; deepen to consume the new
  per-signal edges and group them back at report time.
- `tests/sv/conn/interfaces/*.sv` -- new fixtures: AXI-lite-style modport,
  multi-modport interface, parameterized interface array.

**Risk**: parameterized interfaces (parametric interface arrays) need
careful handling; start with non-parametric, gate parametric behind a
flag.

---

### D3. Metrics extension -- `proposed`

Three sub-features, each independently shippable:

#### D3a. Fanout metric

Add `fanout` field per FF-D-rooted cone in `metrics_report.json`. Already
trivial given `TransformExtractor` tracks reverse edges; needs a forward
edge map per signal node.

#### D3b. Estimated gate-count proxy

Sum the operator complexity (multipliers, comparators, muxes weighted)
into a single `estimated_gate_count` field. Calibration against a small
synthesis run should land alongside.

#### D3c. Video-pipeline-aware metrics

Codec / video designs have specific structural patterns (line buffers,
DPB-style FF arrays, fixed-point arithmetic chains). Add detection
heuristics that classify a cone as `lineBuffer`, `dpbAccess`, or
`fixedPointArith` and surface counts per top-level module.

**Risk**: D3c is heuristic-heavy; gate behind `--metrics-domain video`
to avoid false positives in non-video designs.

---

### D4. Plugin / YAML checker registration -- `proposed`

**Why**: every team wants different convention rules. Today
`ConventionChecker` is hard-coded; allow YAML-defined rules so the
toolchain can be customized without forking.

**Schema sketch** (`checkers.yaml`):

```yaml
checkers:
  - id: USR-001
    description: "Module names must end with _ip"
    target: module
    pattern: ".*_ip$"
    severity: warning
  - id: USR-002
    description: "Signals named tmp_* are forbidden in production"
    target: signal
    forbidden: "tmp_.*"
    severity: error
```

**Files to add**:

- `src/UserCheckerLoader.{h,cpp}` -- parse YAML, build
  `std::vector<CustomChecker>`.
- `src/CheckerRunner.cpp` -- run user checkers in the standard pipeline.
- `tests/test_user_checker.cpp` -- golden YAML + violation fixtures.

**Risk**: pattern matching needs a clear glob vs. regex contract; the
schema explicitly names `pattern` (regex) vs. existing `pattern` (glob)
fields used elsewhere -- pick one and document loudly.

---

## Track F: Distribution / integration

### F1. Python bindings (pybind11) -- `proposed`

**Why**: EDA scripting is overwhelmingly Python. A `pip install svlens`
that exposes the analysis modes as functions is the highest-leverage
adoption move.

**Surface**:

```python
import svlens

result = svlens.conn(["rtl/top.sv"], top="my_top",
                     check_protocol=True, format="json")
# result["edges"], result["issues"], etc.

cdc = svlens.cdc(filelist="rtl/filelist.f", top="soc_top",
                 sdc="syn/clocks.sdc")

metrics = svlens.metrics(filelist="rtl/filelist.f", top="soc_top",
                         topk=5)
```

Each function returns the same JSON dict the CLI emits to disk, so users
can introspect without re-parsing files.

**Files to add**:

- `python/CMakeLists.txt` -- pybind11 module target.
- `python/src/svlens_module.cpp` -- bindings calling into the existing
  `*Runner` classes.
- `python/svlens/__init__.py` -- thin Python surface and type hints.
- `pyproject.toml` -- scikit-build-core driven build.
- `python/tests/test_bindings.py` -- pytest covering each mode.
- `.github/workflows/python.yml` -- build wheels via cibuildwheel and
  publish to PyPI on tag.

**Risk**: ABI compatibility between Python's libstdc++ and the local
slang build. Solve by linking slang statically into the Python module.

---

### F2. VSCode extension scaffolding -- `proposed`

**Why**: surfacing svlens results inline in editor is a strong
onboarding moment. slang already has an LSP, so we can wrap that and
layer svlens diagnostics on top.

**Approach**:

- VSCode extension in `vscode/` that:
  - Registers a new "Run svlens" command.
  - Spawns `svlens conn|cdc|metrics --format json` against the active
    workspace's `filelist.f`.
  - Surfaces JSON issues as VSCode diagnostics.
  - Adds a tree view for the connectivity / CDC reports.

**Files to add**:

- `vscode/package.json`, `vscode/src/extension.ts`,
  `vscode/src/diagnostics.ts`, `vscode/README.md`.
- `.github/workflows/vscode.yml` -- vsce package + marketplace publish on
  tag.

**Risk**: VSCode marketplace publishing requires a publisher account
managed outside this repo; the workflow has to run against a token.

---

### F3. Interactive HTML dashboard upgrade -- `proposed`

**Why**: today `connect_report.html` is a static table. A dynamic D3.js
viewer (collapsible hierarchy, signal trace highlighting) makes large-SoC
results actually navigable.

**Approach**:

- Migrate `src/HtmlReport.cpp` to emit a single self-contained HTML
  file with:
  - D3.js bundle inlined (vendored under `assets/d3.v7.min.js`).
  - Force-directed graph for the connectivity matrix.
  - Click-to-trace: clicking a node opens the trace path on the right
    pane.
  - Filter-by-module dropdown driven by hierarchy data.
- Existing schema reused; the HTML is purely presentation.

**Files to modify / add**:

- `src/HtmlReport.cpp` -- emit new template.
- `assets/d3.v7.min.js`, `assets/dashboard.js`, `assets/dashboard.css`
  -- inlined at build time (CMake `configure_file` or a small generator
  script).
- `tests/test_html_report.cpp` -- snapshot tests covering deterministic
  parts of the output (data sections, not D3 internals).

**Risk**: keeping the HTML self-contained (no CDN) means embedding ~100KB
of D3 in every report. Acceptable for offline / on-prem CI.

---

## Sequencing

A reasonable PR order, each independently mergeable:

1. **D1 (SVA generation)** -- highest impact, smallest surface change.
2. **F1 (Python bindings)** -- biggest adoption multiplier; can land
   alongside D1.
3. **D2 (interface/modport deepening)** -- closes the largest stated gap.
4. **D3a-c (metrics extensions)** -- ship sub-features individually.
5. **D4 (custom YAML checkers)** -- needs schema review with users first.
6. **F3 (HTML dashboard)** -- nice-to-have, low blocker risk.
7. **F2 (VSCode extension)** -- requires marketplace setup; defer.

Each PR should land its design notes inline (this document is intentionally
high-level) and update CONTRIBUTING.md if the build / test workflow shifts.
