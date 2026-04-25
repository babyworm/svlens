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
- current release notes: [`docs/releases/v0.2.5.md`](docs/releases/v0.2.5.md)
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
| `sdc` | timing / false-path helper constraints | `cdc_constraints.sdc` |
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

### Install

```bash
cmake --install build --prefix "$HOME/.local"
```

---

## Current implementation limits

### Connectivity mode

- `ConnectionExtractor` follows named values, conversions, selects, struct-member access, continuous-assign aliases, and concatenation operands as approximate edges.
- Whole-interface / modport ports and procedural glue logic are still only partially modeled.
- Clock/reset analysis in `conn` mode remains name-based heuristic analysis, not semantic domain analysis.

### CDC mode

- SDC period data is used for reporting but not for full timing-aware crossing classification.

### Metrics mode

- Full `always_comb` support covers case/casez/casex/for. General function calls and complex procedural blocks report as `unsupported`/`approximate`.
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
docs/schema/            Stable JSON schema contracts
```

## Output schema documentation

- [`docs/schema/connect_report.md`](docs/schema/connect_report.md)
- [`docs/schema/cdc_report.md`](docs/schema/cdc_report.md)
- [`docs/schema/metrics_report.md`](docs/schema/metrics_report.md)
- [`docs/schema/svlens_summary.md`](docs/schema/svlens_summary.md)

---

## Validation

```bash
ctest --test-dir build --output-on-failure
```
