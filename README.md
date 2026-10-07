# svlens

[![CI](https://github.com/babyworm/svlens/actions/workflows/ci.yml/badge.svg)](https://github.com/babyworm/svlens/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![slang](https://img.shields.io/badge/slang-v10%2B-purple.svg)](https://github.com/MikePopoloski/slang)
[![Latest Release](https://img.shields.io/github/v/release/babyworm/svlens)](https://github.com/babyworm/svlens/releases)
[![GitHub stars](https://img.shields.io/github/stars/babyworm/svlens?style=social)](https://github.com/babyworm/svlens/stargazers)

Unified structural analysis toolkit for SystemVerilog RTL designs. It analyzes and checks

- simple connectivity
- simple structural CDC issues
- code quality (based on complexity estimation)

svlens is built on [slang](https://github.com/MikePopoloski/slang) v10+. Thus, C++20 is required to build the project.

---

## Quick start

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local"
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build -j4
```

Every mode accepts source files directly or via filelist.
For real projects, **always use `-f` or `-F`**:

A typical project layout that svlens expects:

```text
my_soc/
├── rtl/
│   ├── filelist.f          # see example below
│   ├── include/            # `include headers (-I rtl/include)
│   ├── pkg/soc_pkg.sv
│   ├── top/soc_top.sv
│   └── sub/{cpu,bus,uart}.sv
├── syn/
│   └── clocks.sdc          # for CDC clock-period awareness
└── waivers/
    ├── conn_waivers.yaml
    └── cdc_waivers.yaml
```

```text
# rtl/filelist.f
-I rtl/include
-D SYNTHESIS
rtl/pkg/soc_pkg.sv
rtl/top/soc_top.sv
rtl/sub/cpu.sv
rtl/sub/bus.sv
rtl/sub/uart.sv
```

```bash
# connectivity
svlens conn -f rtl/filelist.f --top soc_top

# CDC
svlens cdc -f rtl/filelist.f --top soc_top --sdc syn/clocks.sdc

# metrics (transformation complexity)
svlens metrics -f rtl/filelist.f --top soc_top

# all three under one output root
svlens all -f rtl/filelist.f --top soc_top -o reports
```

Both `-f` and `-F` resolve relative paths from the **filelist location**.
Place the filelist at the project root alongside the RTL tree, or use absolute paths.

Single-file usage also works for quick checks:

```bash
svlens conn design.sv --top my_top
svlens metrics design.sv --top my_top
```

Command reference:

```bash
svlens --help
svlens help conn
svlens help cdc
svlens help metrics
svlens help all
```

## Reference docs

- install / offline builds: [`docs/install.md`](docs/install.md)
- CLI and help contract: [`docs/cli-help.md`](docs/cli-help.md)
- JSON report schemas: [`docs/schema/`](docs/schema/)
- large-SoC waiver / baseline rollout: [`docs/waiver-baselines.md`](docs/waiver-baselines.md)
- release / packaging flow: [`docs/release.md`](docs/release.md)
- current release notes: [`docs/releases/v0.3.6.md`](docs/releases/v0.3.6.md)
- contributing guide: [`CONTRIBUTING.md`](CONTRIBUTING.md)
- feature roadmap: [`docs/design/feature-roadmap.md`](docs/design/feature-roadmap.md)

---

## Source file input

svlens supports three ways to specify SystemVerilog sources:

| Method | Example | When to use |
|--------|---------|-------------|
| `-f <filelist>` | `svlens conn -f rtl/filelist.f --top soc_top` | **Standard for all projects.** Paths resolved from filelist location. |
| `-F <filelist>` | `svlens cdc -F rtl/filelist.f --top soc_top` | Same behavior as `-f` in slang. |
| Positional files | `svlens conn top.sv sub.sv --top my_top` | Quick single-file checks or small testbenches. |

A filelist (`.f` file) contains one source path per line, and can include
`-I`, `-D`, `--std`, and nested `-f` directives:

```text
// rtl/filelist.f
-I rtl/include
-D SYNTHESIS
rtl/pkg/soc_pkg.sv
rtl/top/soc_top.sv
rtl/sub/cpu.sv
rtl/sub/bus.sv
rtl/sub/uart.sv
```

All pass-through flags (`-I`, `-D`, `--std`, `-f`, `-F`, `-y`, `-v`) are forwarded directly to the slang compiler frontend.

---

## What it does

`svlens` exposes three analysis modes plus a combined mode:

- **`conn`** -- Port / connectivity analysis
  - port-to-port connectivity extraction
  - width mismatch, type mismatch, dangling output, undriven input
  - protocol completeness, naming convention checks
  - diff / trace / interface grouping / report generation

- **`cdc`** -- Clock-domain crossing analysis
  - clock source and domain analysis
  - FF classification and FF-to-FF crossing detection
  - synchronizer recognition
  - CDC waiver / SDC / report generation

- **`metrics`** -- RTL transformation complexity analysis
  - output-rooted and FF-D-rooted backward transformation cones
  - repeated bit-lane normalization
  - FF-to-FF combinational complexity with provenance levels
  - case/casez/for always_comb decomposition
  - baseline diff with regression detection

- **`all`** -- Run conn + cdc + metrics under one output root
  - shared elaboration frontend
  - split output trees (`conn/`, `cdc/`, `metrics/`)
  - `both` accepted as backward-compatible alias

---

## Primary CLI

```bash
svlens conn    [OPTIONS] {-f <filelist> | <SV_FILES...>}
svlens cdc     [OPTIONS] {-f <filelist> | <SV_FILES...>}
svlens metrics [OPTIONS] {-f <filelist> | <SV_FILES...>}
svlens all     [COMMON_OPTIONS] [--conn-* ...] [--cdc-* ...] {-f <filelist> | <SV_FILES...>}
svlens help [conn|cdc|metrics|all]
```

---

## Connectivity examples

```bash
# Standard project analysis
svlens conn -f rtl/filelist.f --top soc_top --format all -o reports/

# Selective checks with waivers
svlens conn -f rtl/filelist.f --top soc_top --no-check-dangling --waiver waivers.yaml

# Protocol and convention checking
svlens conn -f rtl/filelist.f --top soc_top --check-protocol --check-convention

# Ignore intentional NC / tie-off ports
svlens conn -f rtl/filelist.f --top soc_top --ignore-nc --ignore-tie-off

# Diff against baseline
svlens conn -f rtl/filelist.f --top soc_top --diff baseline/connect_report.json

# Expected connectivity
svlens conn -f rtl/filelist.f --top soc_top --expect connectivity_spec.yaml

# Clock/reset naming heuristic summary
svlens conn -f rtl/filelist.f --top soc_top --check-clock-reset

# Signal trace
svlens conn -f rtl/filelist.f --top soc_top --trace "*.u_cpu.o_addr"
```

### Connectivity outputs

| Format | Description | File |
|--------|-------------|------|
| `table` | terminal summary | stdout |
| `json` | machine-readable report | `connect_report.json` |
| `md` | markdown report | `connect_report.md` |
| `csv` | connection matrix | `connection_matrix.csv` |
| `dot` | graphviz block diagram | `connectivity.dot` |
| `html` | interactive dashboard | `connect_report.html` |

---

## CDC examples

```bash
# Basic CDC analysis with SDC
svlens cdc -f rtl/filelist.f --top soc_top --sdc syn/clocks.sdc

# With YAML clock specification
svlens cdc -f rtl/filelist.f --top soc_top --clock-yaml clock_domains.yaml

# Apply waivers and require 3-stage synchronizers
svlens cdc -f rtl/filelist.f --top soc_top --waiver cdc_waivers.yaml --sync-stages 3

# Strict CI mode (JSON only, quiet)
svlens cdc -f rtl/filelist.f --top soc_top --format json --strict -q

# Export DOT graph
svlens cdc -f rtl/filelist.f --top soc_top --dump-graph cdc_graph.dot
```

### CDC outputs

| Format | Description | File |
|--------|-------------|------|
| `md` | markdown CDC report | `cdc_report.md` |
| `json` | machine-readable CDC report | `cdc_report.json` |
| `sdc` | review-only timing template (comments; no executable constraints) | `cdc_constraints.sdc` |
| `waiver` | waiver template | `cdc_waiver_template.yaml` |

---

## Metrics examples

```bash
# Standard complexity analysis
svlens metrics -f rtl/filelist.f --top soc_top

# JSON + markdown reports
svlens metrics -f rtl/filelist.f --top soc_top --format both -o reports/

# Show only top-5 most complex roots
svlens metrics -f rtl/filelist.f --top soc_top --topk 5

# Include per-root cone detail and raw transform graph
svlens metrics -f rtl/filelist.f --top soc_top --emit-cones --emit-raw-graph

# CI regression guard: compare against baseline, fail on regression
svlens metrics -f rtl/filelist.f --top soc_top \
  --baseline prev/metrics_report.json --fail-on-regression

# Limit for-loop unrolling (default: 1024)
svlens metrics -f rtl/filelist.f --top soc_top --max-for-unroll 512
```

### Metrics outputs

| Format | Description | File |
|--------|-------------|------|
| `json` | machine-readable metrics report | `metrics_report.json` |
| `md` | markdown summary with tables | `metrics_report.md` |

### Understanding metrics output

The metrics report provides quantitative guardrails for RTL complexity.
Key fields and how to interpret them:

| Field | Meaning | What to look for |
|-------|---------|-----------------|
| `raw_node_count` | Total transform operations in backward cone | High values indicate complex datapath. Compare across roots to find hotspots. |
| `logic_depth_est` | Estimated logic depth (levels of transformation) | Correlates with combinational timing paths. Values > 20 warrant review. |
| `normalized_transform_count` | Node count after collapsing repeated bit-lanes | Compare with `raw_node_count` -- large gap means repetitive structure (bus operations). |
| `source_inputs` | Number of primary inputs feeding the cone | High fan-in suggests complex convergence. |
| `source_ffs` | Number of FF outputs feeding the cone | High values indicate cross-register dependencies. |
| `approximate` | Whether the cone contains unsupported constructs | `true` means some operations could not be fully decomposed -- treat metrics as lower bounds. |
| `provenance_level` | Confidence in FF path analysis | `provenance_backed` = full extraction; `hint_only` = from CDC hints only; `partial_slice` = incomplete. |

**Decision guide:**

- **Simple passthrough** (`raw_node_count` = 1, `logic_depth_est` = 1): Pure wiring, no concern.
- **Bus operations** (`raw` >> `normalized`): Repetitive structure. The `normalized` count reflects true complexity.
- **Deep cone** (`logic_depth_est` > 15): Potential timing risk. Review the transformation chain.
- **High fan-in** (`source_inputs` + `source_ffs` > 20): Complex convergence point. Consider whether this is intentional.
- **Approximate cones**: Unsupported constructs are listed in `unsupported[]`. Expand support or accept as lower-bound estimate.
- **Baseline regression** (`--baseline`): Positive `raw_delta` means added complexity. Use `--fail-on-regression` in CI to catch unintended growth.

---

## `all` mode

Runs all three analyses under one shared compilation, writing results into separate subdirectories.

```bash
svlens all -f rtl/filelist.f --top soc_top -o reports \
  --conn-format json \
  --cdc-format json \
  --cdc-sync-stages 3
```

Output tree:

```text
reports/
  conn/connect_report.json
  cdc/cdc_report.json
  metrics/metrics_report.json
  svlens_summary.json
```

Use `--conn-*` and `--cdc-*` prefixed flags to pass mode-specific options.
`svlens both` is accepted as a backward-compatible alias.

---

## Connectivity configuration

### Expected connectivity

```yaml
expected:
  - from: "*.u_cpu.o_ibus_*"
    to: "*.u_bus.i_cpu_ibus_*"

forbidden:
  - from: "*.u_debug.*"
    to: "*.u_secure_*"
```

```bash
svlens conn -f rtl/filelist.f --top soc --expect connectivity_spec.yaml
```

### Custom convention rules

```yaml
input_prefix: in_
output_prefix: out_
instance_prefix: inst_
```

Nested keys such as `input.prefix`, `output.prefix`, and `instance.prefix` are also accepted.

```bash
svlens conn -f rtl/filelist.f --top soc --convention convention.yaml
```

---

## Build

For the detailed install guide, including offline / preinstalled dependency flows, see
[`docs/install.md`](docs/install.md).

### Prerequisites

| Dependency | Version | Install |
|------------|---------|---------|
| C++ compiler | C++20 support (GCC 13+, Clang 16+) | system package |
| CMake | 3.20+ | system package |
| [slang](https://github.com/MikePopoloski/slang) | v10+ | see below |

By default, CMake can fetch missing `yaml-cpp` and `Catch2` dependencies.
For preinstalled / no-network builds, configure with `-DSVLENS_FETCH_DEPS=OFF`.
`fmt` is provided by `slang` when bundled, otherwise a system `fmt` install is used.

### Quick setup

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local"
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build -j$(nproc)
```

### Manual slang install

```bash
git clone --depth 1 --branch v10.0 https://github.com/MikePopoloski/slang.git
cd slang
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build
```

### Offline / preinstalled build

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local" --offline
cmake -B build-offline -DCMAKE_PREFIX_PATH="$HOME/.local" -DSVLENS_FETCH_DEPS=OFF
cmake --build build-offline -j$(nproc)
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

For a full AddressSanitizer run, build slang in a separate prefix with
`--no-mimalloc`; see the [installation guide](docs/install.md#addresssanitizer-validation).

### Install

```bash
cmake --install build --prefix "$HOME/.local"
```

---

## Current implementation limits

### Connectivity mode

- `ConnectionExtractor` follows named values, conversions, selects, struct-member access, continuous-assign aliases, and concatenation operands as approximate edges.
- Modport members produce per-signal ports and edges, including direct edges when slang exposes the underlying signal; modport width mismatches are checked. Whole-interface ports infer direction for directly used members, including simple generated if/for scopes. Direct constant bit/part selects of one integral member preserve overlapping ordinal ranges, including indexed `+:`/`-:` selects with elaboration-time constant base and width, modport pairings, and two direct slice-rewiring stages between distinct interface instances. Constant member selects inside computed expressions or procedural guards retain their selected input range but remain `approximate`; unsupported expressions retain whole-member may-flow. Small AXI-lite-style modport, nested-forwarding, fixed two-element, and genvar-indexed parameterized-array fixtures pass. Pinned slang v10 rejects nonconstant interface-instance array selections during elaboration; SoC-level accuracy remains unverified.
- A sole unconditional blocking `always_comb` assignment between resolved integral nets or constant selects now carries `direct` ordinal bit ranges. Conditional, overwritten, compound, and legacy `always @*` assignments remain directed `approximate` dependencies, including modport member inputs and generated-scope references to parent nets. Structurally resolved guarded `if/case` copies also expose positional `approximate` ranges; a coarse may-flow edge remains where later casts or aliases need transitive reachability. Compile-time-known `if`, `case`, `casez`, and `casex` select only the active branch, including whole-interface member use; runtime ranges still do not prove branch reachability. Uninstantiated generate branches are skipped. Direct constant and elaborated generated-bank indexed `+:`/`-:` slices, constant-indexed integral unpacked-array elements, nested constant-indexed packed-array elements and bits (also inside constant-indexed unpacked elements and packed-struct fields), whole packed-array ports, and positional concatenations (up to 4,096 bits) carry ordinal bit ranges across simple assignment chains; arithmetic leaves and branch controls remain approximate. Runtime indexed part-select starts retain range-free may-flow, not a chosen lane. Runtime element reads and combinational procedural writes can connect to structurally bound candidate elements as range-free `approximate` edges; they do not identify the chosen lane or prove reachability. Nonconstant interface-instance array selection is rejected during slang v10 elaboration; fixed and genvar-selected lanes are covered by small fixtures. Nonintegral array elements, arbitrary casts, and full branch-sensitive dataflow remain incomplete.
- Equal-width ternary arms with resolved input ranges add positional `approximate` may-flow for both runtime branches; a compile-time-known branch uses only the selected arm. These edges do not prove selector reachability.
- Simple implicit width changes and explicit size casts of a directly resolved integral signal retain low-bit correspondence: unsigned extension adds constant high bits, signed extension repeats the source sign bit, and truncation drops high bits. An unsigned concatenation inside a size cast also preserves its retained operand lanes. Width-changing type casts that also change signedness, casts changing two-state/four-state representation, and computed cast operands remain approximate. Width- or state-changing conversions do not create a contradictory range-free `direct` alias.
- Clock/reset analysis in `conn` mode remains name-based heuristic analysis, not semantic domain analysis.

### CDC mode

- Finite positive SDC periods are recorded as timing context in `domains[*].period_ns` and propagate through uniquely resolved `create_generated_clock` divide/multiply chains, even when a child is declared before its master. Unknown domain periods are `null`. Recognized unconditional or reset-only RTL toggle dividers inherit a scoped master period; enabled toggles need explicit SDC timing. These periods can add an `Ac_cdc08` review hint for fast-to-slow related clocks; they do not establish a safe capture window or change the structural crossing category. Invalid, ambiguous, or cyclic generated masters remain unresolved. A matching SDC `set_false_path` is reported as a timing exception but never waives or downgrades a CDC crossing. `-physically_exclusive` and `-logically_exclusive` clock groups label the declared relationship but leave an unsynchronized crossing at `CAUTION` for mode-transition review (`--strict` gates it). Generated `cdc_constraints.sdc` contains review comments only, because neither a CDC waiver nor a detected synchronizer determines a sign-off timing exception or delay value.
- `set_clock_groups` accepts static brace lists and static `[get_clocks name]` / `[get_clocks {name ...}]` groups. `[get_clocks -include_generated_clocks name]` expands only generated clocks with a uniquely resolved `master` chain from a literal root name. Missing or ambiguous names and overlapping groups skip the entire relationship with a CLI warning. Other Tcl options, wildcards, and dynamic selectors are not evaluated; a command containing one is skipped as a whole with a warning.
- One finite positive clock-to-clock `set_max_delay` for an exact directional pair is exposed as a declared constraint in CDC JSON/Markdown. It never changes CDC category and is not measured path delay. Multiple matching limits or ambiguous clock names are marked ambiguous; malformed, pin-targeted, or path-filtered commands are skipped with a CLI warning. Timing exception precedence and physical delay remain for STA to verify.
- `Ac_cdc08` remains a review hint: fast-to-slow related-clock periods and conditional wide-bus capture controls are surfaced, but data stability, enable synchrony, phase, and path delay are not proven.
- An unsynchronized multi-bit asynchronous crossing remains a `VIOLATION`, but recommends a coherent bus-transfer scheme (such as a bundled-data handshake, async FIFO, or validated Gray-coded protocol) instead of independent per-bit 2FFs. This is guidance, not proof that any existing control/data protocol is safe.
- An unsynchronized single-bit crossing also remains a `VIOLATION`; its recommendation first asks to verify the clock relationship and whether the signal is a stable level or an event. A 2FF chain is a candidate for a level held across destination samples, while a pulse/toggle transfer protocol may be needed for an event. The analyzer does not prove those assumptions or prescribe a synchronizer for a mode-dependent clock path.
- `Ac_cdc09` matches clock-as-data fan-in against nets within the owning module scope and reports the resolved hierarchical source path, including a direct clock-to-renamed-data-port connection. Unsupported/conditional port aliases may be missed rather than matched by a coincidental name in another instance.
- Direct clock aliases traverse simple assignments, module ports, clock-named struct fields, and instantiated generate scopes even when consumers precede producers. Width- or two-state/four-state-changing casts are not treated as transparent aliases. Conditional muxes and non-direct generated-clock logic remain incomplete. Gate/divider provenance does not make their output the same local domain as the input.
- Connection reports count positional bit-flow gaps for concatenations and bit selections that cannot be mapped exactly (including assignments wider than 4,096 bits). JSON gives reason counts and up to four source-located examples per reason; these are elaborated assignment instances, not missing-connection counts or a correctness verdict.
- CDC JSON distinguishes local clock domains from top-clock root provenance. The latter can follow recognized one-input gates/dividers but is metadata only; equal roots do not waive a crossing.
- Generated-block FFs prefer the nearest scoped clock net before any global same-name fallback. Unresolved external/mux clocks remain distinct rather than being merged by a leaf name.
- A declared generated clock can follow an SDC `get_pins` child output to its connected parent signal; it does not override unrelated same-named ports.
- `--format html` writes a self-contained CDC crossing explorer with module/category filters and a signal trace panel.
- `Ac_cdc12` is a quasi-static hint based on names or empty FF fan-in. `Ac_cdc10` requires a unique hierarchical FF driver reached from an undeclared clock origin through direct assignments or directed ports; a same-named FF in another instance is not treated as its driver. Instance-scoped inferred clocks stay separate when siblings reuse a signal name. Top-level data-as-clock inputs remain naming hints because a non-clock name alone does not establish their source or timing intent. Full RDC analysis is absent.
- `Ac_cdc06` traces an asynchronous reset backward through direct assignments, directed module ports (including reset-named packed-struct fields and a fixed-selected bit feeding a scalar reset), exact hierarchical references, and simple one-bit `~`/`!` inversions, including a sole unconditional `always_comb` assignment or scalar mux input. A selected bit can name its unique aggregate FF driver or the separate FF driving that indexed output bit; other bits are not conflated, while duplicate FF paths and dynamic selectors remain unresolved. Clocked packed-struct member assignments produce an FF node at the member path with its declared width, and member reads can feed CDC FF edges. Inversion and selected-bit edges are reset-only and never merge clock domains; wide inversions, conditional or multiply assigned procedural resets, and conflicting driver/polarity paths remain unresolved. `reset_usage` exposes a unique driver and polarity when proven, or structural `conditional_muxes` routes through recognized two-input reset muxes. Immediately connected mux signals are distinguished from computed-expression dependencies; structurally traced input FFs are labeled as branch candidates with input-relative inversion parity, not output drivers. Conflicting candidate parity is marked ambiguous. Width- or state-changing casts are not direct paths. A runtime/unknown mux selector blocks a unique FF driver claim. An elaboration-time 0/1 selector follows only its chosen input, which must independently resolve to a unique FF; another alias cannot supply that proof. The HMAC scan-mode path remains driver-unresolved, and no deassertion timing or RDC safety is proven.
- A connected, two-clock `prim_sync_reqack_data` port signature produces bidirectional CAUTION records for integration review; the primitive's internal synchronization is not verified by this recognition.
- `--emit-sva` emits covers for eligible unsynchronized violations, sampled stage-transfer assertions for unambiguous 2FF/3FF chains, a Gray-pointer transition assertion for verified `prim_fifo_async` crossings, an ACK-requires-REQ contract assertion for verified `prim_sync_reqack` request paths, and a direction-specific data-hold contract for fully connected `prim_sync_reqack_data` instances with checked parameters and `DataReg=0`. JSON links all generated IDs. Generic handshake data-stability/liveness and FIFO protocol assertions are not generated; these properties do not prove metastability or CDC safety.
- Generated SVA uses AST declaration paths for FF/reset references and scoped clock paths. Compile its assertion module alongside the design top; review simulator binding and assumptions before treating any property result as proof.

### Metrics mode

- `always_comb` handles assignments, conditionals, case/casez/casex, and bounded for loops, but not full procedural semantics. Function calls produce approximate transform nodes; unsupported constructs are reported explicitly.
- Roots report maximum extracted-graph fanout and an uncalibrated operator-weighted gate-cost proxy. Calibrated gate-count estimates and video-pipeline classification are not reported.
- `all` mode runs metrics with default options; use `svlens metrics` directly for `--topk`, `--baseline`, `--emit-cones`, etc.

---

## Project layout

```text
include/sv-cdccheck/    Imported CDC public headers
src/                    Connectivity + unified CLI + shared frontend
src/cdc/                CDC implementation
src/metrics/            Metrics engine (TransformExtractor, ConeAnalyzer, Normalization, BaselineDiff)
tests/                  Catch2 tests + shell integration tests
tests/sv/metrics/       Metrics SV fixtures
python/                 CLI-backed Python API package
vscode/                 On-demand VS Code diagnostics extension
docs/schema/            Stable JSON schema contracts
```

## Output schema documentation

- [`docs/schema/connect_report.md`](docs/schema/connect_report.md)
- [`docs/schema/cdc_report.md`](docs/schema/cdc_report.md)
- [`docs/schema/metrics_report.md`](docs/schema/metrics_report.md)
- [`docs/schema/svlens_summary.md`](docs/schema/svlens_summary.md)
- [`docs/report-integration.md`](docs/report-integration.md) for SARIF and PR-summary conversion
- [`docs/user-rules.md`](docs/user-rules.md) for user-defined name checkers

---

## Validation

```bash
ctest --test-dir build --output-on-failure
```
