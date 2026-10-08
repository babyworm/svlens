# Feature roadmap (post-v0.2.x)

This document captures planned features that go beyond the existing connectivity /
CDC / metrics surface area. Each item is sized as an independent PR (or PR
series) and includes the data flow, API surface, and tests required to ship.
Items are ordered by expected impact on real-world adoption.

Status legend: `proposed` (no work started), `partial` (a narrower capability
exists), `scoped` (interface fixed, work ready to begin), `in-progress`, `landed`.

---

## Track A: CDC detection quality -- `landed`

Cumulative improvements to the structural CDC analyzer that close
false-negative classes observed in production OSS RTL (OpenTitan,
CVA6/pulp, ZipCPU, BlackParrot). Each item is sized as a discrete
finding with paired pos/neg golden fixtures.

### A1. ZipCPU concat-LHS sync chain idiom -- `landed`

`{wq2,wq1} <= {wq1,rgray};` was previously over-broadcast, hiding the
real 2-FF synchronizer. Bit-aware positional matching in
`src/cdc/ast_utils.cpp::splitAssignmentByLHS` recognises the
positional pairing when LHS and RHS concats have matching arity and
operand widths. Fixtures 36-37, validates upstream ZipCPU `afifo.v`
(0V/0C/2 INFO `two_ff`).

### A2. Cross-instance findNextFF + pulse-sync detector relaxation -- `landed`

`sync_verifier.cpp::findNextFF` and `detectPulseSyncPattern` no longer
require `source_leaf == fanin[0]` strict name match (would fail on
port-renamed cross-instance chains). Replaced with FFEdge-trust +
single-source predicate `isSingleSourceFF`. Fixture 33 transitions
from VIOLATION to INFO, paired neg fixture 38 keeps single-stage
case as VIOLATION.

### A3. Macro-driven RTL workflow -- `landed`

`-D` defines and `-I` include paths flow through the slang
preprocessor (`CompilationSession.cpp::expandFilelists`). Fixtures
39-40 with `.flags` sidecars exercise `\`SYNC_2FF` / `\`SYNC_DIRECT`
macro patterns from VeeR-EH1 / BlackParrot.

### A4. Parameter-as-fanin filter -- `landed`

OpenTitan `prim_flop` uses `q_o <= ResetValue` (parameter) which was
inflating `fanin_signals.size()` to 2 and silently disabling 2-FF
sync detection. `collectReferencedSignals` now filters
`SymbolKind::Parameter / TypeParameter / EnumValue`. OpenTitan
`prim_fifo_async`: was 2 VIOLATIONS, now 2 INFO `two_ff`.

### A5. Port-driven clock unification (no-SDC) -- `landed`

When parent ports use unconventional names (`ca`, `cb`, `tck`,
`phy_clock`) that don't match the `*clk*` / `*clock*` heuristic, the
auto-detector previously produced phantom domains for submodule's
clk_i. `clock_tree.cpp::isPortUsedAsClock` walks generate blocks and
lazily seeds parent ClockSources; `autoDetectClockPorts` rejects
reset-named ports. Fixture 43-44 paired pos/neg.

### A6. Hierarchical reference connectivity -- `landed`

`u_sub.q` reads from a parent module across clock domains were
silently dropped because `extractExprSignalName` returned only the
leaf name. Now returns `getHierarchicalPath()`; `findFFByName` does a
direct dotted-path lookup. Fixtures 45-46.

### A7. Generate-array name normalization -- `landed`

`u_sub.gen_blk[1].q_inner` (`getHierarchicalPath` syntax) vs
`genblk1.q_inner` (`getExternalName` flattened form) format
mismatch. `findFFByName` now tries dual-variant lookup
(no-brackets + flattened) plus a parent-prefix + idx + leaf-suffix
scan with dot-count guard for digit-medial-underscore labels.
Fixtures 47-49 (positive coverage) plus 52 (paired negative for
the digit-medial-underscore label corner).

### A8. Multi-pattern integration + multi-hop pulse-sync -- `landed`

`splitConcatPair` recursive helper handles arbitrary-depth nested
concat (was 2-level inline). `detectPulseSyncPattern` walks bounded
BFS (MAX_DEPTH=6) for delay taps multiple hops downstream of
last_sync. Fixture 50 combines 5 patterns in one design.

### A9. CLI flag integration tests -- `landed`

`--strict`, `--sync-stages`, `--ignore-gated` all locked with
Catch2 integration tests (`test_cdc_runner.cpp`). Fixture 51 with
SDC `create_generated_clock` produces the Severity::Low gated entry
needed for `--ignore-gated` verification.

### A10. Performance baseline -- `landed`

After Round 12's `O(D*N) → O(D)` hash lookup in
`ff_classifier.cpp` (clock net resolution) and the suffix-scan
length pre-check in `connectivity.cpp::findFFByName`, analysis
times: ZipCPU `afifo` <50ms, OpenTitan `prim_fifo_async` ~11ms,
CVA6 `cdc_fifo_gray` ~22ms. See `tests/cdc/README.md` for full
external coverage table.

---

## Track D: Analysis depth

### D1. CDC SVA assertion auto-generation -- `partial`

**Current**: `--emit-sva <path>` writes a cover property for an unsynchronized
violation when its source path is safe to use as an SVA expression. An
unambiguous 2FF/3FF chain emits a sampled stage-transfer `assert property`
only if adjacent receiving FF widths agree and no non-reset capture guard is
observed. Enabled receiving stages remain documentation-only. A generated
assertion has an optional `sva_assertion_id` link in JSON when the SVA file is written.
A `prim_fifo_async` Gray pointer with verified source FF, width, clock, reset,
and destination shape emits the primitive's one-bit transition property.
When its relevant one-bit ready/valid and clock/reset ports are also connected,
it emits a source-clock no-step-without-transfer pointer assertion. When
multiple properties exist, `sva_assertion_ids` links their labels. Verified
`prim_sync_reqack` request paths can emit the primitive's destination
ACK-requires-REQ contract after the destination reset alias is checked. A
fully connected `prim_sync_reqack_data` instance with checked port widths,
directions, and parameters (`DataReg=0`) can emit its direction-specific
source-clock data-hold contract. Other handshake/FIFO protocol patterns
produce comments. SVA signal/reset paths use
AST declaration scopes and stage clocks use the FF timing event, so emitted
properties can be elaborated alongside the design top (including generated
instances) instead of relying on unqualified domain names.
Pinned OpenTitan AES and full-SoC probes compare classifications before/after
SVA emission, link every emitted assert label to JSON, and elaborate the SVA
with the RTL. They do not simulate or prove those properties.
An optional local Verilator fixture run checks pass/fail execution of 2FF,
FIFO no-step, and SRC-to-DST data-hold properties; it does not exercise the
full SoC or the DST-to-SRC `[*2]` hold property.

**Why**: generic handshake and FIFO protocol properties need temporal
assumptions before they can be emitted safely. The narrow Gray assertion
checks only pointer encoding; the no-step assertion checks pointer gating,
not FIFO full/empty logic. Stage transfer does not prove metastability
resolution. The ACK and data-hold properties check caller contracts, not
liveness or protocol safety.

**Next output scope**: extend the current optional SVA artifact beyond these
primitive-specific contracts with validated handshake/FIFO protocol assertions
and link each generated property to the crossing.

**API**:

- Extend the existing `--emit-sva` output with validated handshake/FIFO
  protocol templates beyond the primitive Gray-pointer and ACK contract
  properties.
- A style selector may be added if its assumptions can be checked explicitly.
- Preserve `cdc_report.json:crossings[].sva_assertion_id` as the primary label
  and `sva_assertion_ids` when a crossing emits multiple properties.

**Files to add / modify**:

- `src/cdc/report_generator.cpp` -- extend the current SVA writer.
- `tests/test_cdc_report_generator.cpp` and `tests/test_cdc_runner.cpp` --
  fixture-backed property and JSON-link tests.
- `docs/schema/cdc_report.md` -- define any additional assertion IDs.

**Risk**: a structurally recognized synchronizer does not itself prove that a
particular temporal assertion is sound. Validate reset, clock, and sampling
assumptions for each style; a selector must not bypass these checks.

---

### D2. Interface / modport semantic deepening -- `partial`

**Current**: `ConnectionExtractor` emits per-modport-signal ports and edges.
When slang exposes the underlying signal, it also emits a direct edge used by
width checking; the approximate edge is retained. Simple `always_comb` and
legacy `always @*` if/case/ternary glue create directed approximate
dependencies, including modport member inputs. For whole-interface ports,
direct member reads and writes create direction-aware approximate edges.
Simple direct constant bit/part selects of integral members also produce
overlap-only ordinal bit ranges across whole-interface and modport ports;
two direct slice-rewiring stages between distinct interface instances preserve
source/destination bit correspondence. Constant member selects nested inside
computed expressions and procedural guards retain their selected input ranges
as approximate evidence; unsupported expressions stay whole-member may-flow.
Equal-width ternary arms with resolved source ranges now emit positional
approximate may-flow through direct or multi-stage assignments; a known
constant condition selects one arm, including whole-interface member-use
inference. Other computed expressions remain limited.
Generated if/for scopes are visited for directly used whole-interface members;
procedural and continuous aliases use the referenced signal's declaring scope
and skip uninstantiated branches. Fixtures now include AXI-lite-style
multi-channel modports, nested whole-interface forwarding, two-element
parameterized interface arrays (including genvar-indexed lanes), and isolated
generated lanes. These are small RTL probes, not an accuracy measurement on
an AXI SoC.
A pinned 20-row OpenTitan SoC connection sample has 20 source-backed
confirmations, including an independently checked buffer bit index. It is a narrow audit with
correlated generated-register rows, not a whole-design precision/recall value
or a substitute for an AXI/modport SoC corpus.
An independent AST probe enumerates 358 scalar or whole-width one-dimensional
vector sibling-port paths across seven pinned SoC IP scopes and checks all
358 in the conn report (vector width capped at 4,096 bits). This includes 355
shared-net paths and three uniquely driven one-stage continuous aliases. It
bounds recall for these direct path shapes only; bit-lane correspondence,
interfaces, sliced/converted buses, procedural glue, and other scopes still
need their own source-derived frames.

**Why**: branch-sensitive mux/decoder glue, legal interface-array composition,
deep forwarding, and nontrivial bit-range dataflow still need precise signal-level
validation on representative SoC patterns.

Direct port connections to overlapping constant ranges or elements of the
same flat vector, plus constant-indexed integral unpacked-array elements and
nested constant-indexed packed-array elements/bits and whole packed-array ports
(including packed fields inside constant-indexed unpacked elements),
now emit the overlapping ordinal source/destination bit
intervals. Indexed `+:` / `-:` part-selects use their elaborated base and
width, including generated banks and interface members; runtime starts retain
range-free approximate may-flow. A bounded (up to 4,096 bits)
bit-flow graph also follows nested/unequal-width positional concatenations,
constant holes, simple aliases, and guarded `always_comb` assignments across
multiple direct stages. A sole unconditional blocking `always_comb` copy or
constant select now retains direct ordinal lanes; conditional, overwritten,
compound, and legacy `always @*` assignments remain approximate. Guarded
`if/case` copies with resolved integral source and destination also carry
positional approximate lanes; coarse dependencies remain for paths through
unsupported later casts. Compile-time-known `if`, `case`, `casez`, and `casex`
visit only the selected branch, including whole-interface member use. Runtime
selectors remain conservative. Simple implicit integral extension/truncation and
explicit size casts of one resolved source map retained low bits; signed
extension maps the source sign bit to each high bit. Unsigned concatenations
inside size casts also map their retained operand lanes. Arithmetic or conditional RHS leaves become
target-lane-specific approximate dependencies. Runtime element reads and
combinational procedural writes now add range-free approximate edges to structurally bound candidate
elements with compatible fixed indices and member names. This is a may-flow
relation, not proof of the selected lane or branch reachability. Width-changing
type casts that also change signedness, casts changing two-state/four-state
representation, computed width conversions, nonintegral array elements, and
arbitrary computed-output or branch-sensitive whole-interface bit-range forwarding
remain incomplete.
The pinned slang v10 rejects nonconstant interface-instance array selection
(`buses[select_i].data`) during elaboration, before connection extraction.
That form is an elaboration boundary in this toolchain, not a missing extracted edge; static
and genvar-selected interface lanes remain the supported benchmarked forms.
When a whole unpacked-array port feeds one constant-indexed element, the
aggregate port keeps a conservative dependency alongside the exact element
bit-flow link; the HMAC socket-to-register path is a benchmark reference for
this case.

**Approach**: deepen the existing member-edge extraction and model
procedural dependencies with source and destination bit ranges and branch
conditions. Validate against paired positive/negative fixtures before
claiming complete interface coverage.

**Files to modify**:

- `src/ConnectionExtractor.cpp` -- extend existing member and alias handling.
- `src/InterfaceGrouper.cpp` -- preserve grouping while consuming more precise
  per-signal edges.
- `tests/sv/*.sv` -- extend paired AXI/modport, forwarding, and interface-array
  fixtures beyond the small fixed-index cases already present.

**Risk**: fixed two-element and genvar-indexed arrays are covered, but
dynamic-selector precision in integral arrays and deeper generated
forwarding still need careful matching.
Keep unsupported forms explicit until paired fixtures validate them.

---

### D3. Metrics extension -- `partial`

Three sub-features, each independently shippable:

#### D3a. Fanout metric -- `landed`

`max_fanout` now reports the largest number of distinct transform consumers
of any signal touched by a cone, across the extracted graph. It is available
for output and FF-D roots; it is not physical electrical fanout.

#### D3b. Estimated gate-count proxy -- `partial`

`gate_cost_proxy` now sums width-aware weights for extracted operators.
Wiring costs zero; arithmetic, comparisons, muxes, and shifts have increasing
weights. It is an uncalibrated complexity proxy, not a synthesized gate count.
Calibration against a representative synthesis flow remains open.

#### D3c. Video-pipeline-aware metrics

Codec / video designs have specific structural patterns (line buffers,
DPB-style FF arrays, fixed-point arithmetic chains). Add detection
heuristics that classify a cone as `lineBuffer`, `dpbAccess`, or
`fixedPointArith` and surface counts per top-level module.

**Risk**: D3c is heuristic-heavy; gate behind `--metrics-domain video`
to avoid false positives in non-video designs.

---

### D4. Plugin / YAML checker registration -- `partial`

**Current**: `--user-rules` registers YAML name-pattern checkers for modules,
instances, internal signals, and ports. Rules carry IDs and severity, appear
in JSON, and can be waived by ID. Invalid regexes fail visibly. Arbitrary
AST/plugin checkers are not supported.

**Why**: teams may need custom checks beyond name patterns without forking.

**Current schema** (`checkers.yaml`):

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

**Files for future plugin expansion**:

- `src/UserChecker.{h,cpp}` -- extend the existing loader/checker surface
  only after defining a safe AST query contract.
- `tests/test_user_rules.sh` -- add paired checks for new targets.

**Risk**: name rules use whole-name ECMAScript regex matching while conn
waivers use glob paths. Keep these contracts distinct in documentation.

---

## Track F: Distribution / integration

### F1. Python bindings (pybind11) -- `partial`

**Current**: `python/svlens` provides installable CLI-backed `conn`, `cdc`,
`metrics`, and `all_modes` functions returning parsed JSON. It does not embed
the C++ analyzer or expose in-process pybind11 objects.

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

### F2. VSCode extension scaffolding -- `partial`

**Current**: a dependency-free JavaScript extension runs conn and CDC on
demand. Source-backed conn issues and CDC crossings become editor diagnostics,
and an Explorer tree view lists every CDC crossing by category, including
location-free ones; located entries open their source. Marketplace publishing
and a connectivity tree view remain open.

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

### F3. Interactive HTML dashboard upgrade -- `partial`

**Current**: `connect_report.html` embeds its JSON and an interactive graph
with search, module focus, and port expansion. `cdc_report.html` provides
module/category filters and a selected crossing trace. The conn graph does
not present a full signal trace path or coordinated cross-mode navigation.

**Why**: large-SoC reports still need explicit trace paths and coordinated
navigation across connectivity and CDC results.

**Approach**:

- Extend the existing self-contained conn HTML template with:
  - Click-to-trace: clicking a node opens the trace path on the right
    pane.
  - Filter-by-module dropdown driven by hierarchy data.
- Link conn and CDC HTML findings when they share an exact signal path.
- Existing schema reused; the HTML is purely presentation.

**Files to modify / add**:

- `src/html_template.h` and `src/HtmlReport.cpp` -- extend the current conn
  template and its embedded data.
- `src/cdc/cdc_html_template.h` -- extend the CDC presentation as needed.
- `tests/test_html_report.cpp` -- snapshot tests covering deterministic
  parts of the output (data sections, not graph renderer internals).

**Risk**: trace navigation needs bounded rendering on large graphs and stable
links between related report records.

---

## Sequencing

A reasonable PR order, each independently mergeable:

1. **D2 (interface/modport and procedural dataflow)** -- validate and close
   the most visible SoC connectivity gap.
2. **CDC classification and OpenTitan benchmark evidence** -- add timing and
   data-stability checks, then publish reproducible accuracy numbers.
3. **D1 (SVA assertions)** -- define sound templates and report links.
4. **D3a-c (metrics extensions)** -- ship sub-features individually.
5. **F1 (Python bindings)** -- add a scripting surface after report semantics
   are stable.
6. **D4 (custom YAML checkers)** -- needs schema review with users first.
7. **F3 (HTML trace views)** -- build on the existing conn graph.
8. **F2 (VSCode extension)** -- requires marketplace setup; defer.

Each PR should land its design notes inline (this document is intentionally
high-level) and update CONTRIBUTING.md if the build / test workflow shifts.
