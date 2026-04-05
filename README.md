# svlens

Unified structural analysis toolkit for SystemVerilog RTL designs.

This repository now ships a single user-facing executable:

- `svlens`

Built on [slang](https://github.com/MikePopoloski/slang) v10+.

---

## Quick start

Build `svlens`, then run one of the three primary modes:

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local"
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build -j4

# connectivity
./build/svlens conn tests/sv/clean_design.sv --top clean_top

# CDC
./build/svlens cdc --top missing_sync tests/cdc/basic/02_missing_sync.sv

# both modes under one output root
./build/svlens both tests/cdc/basic/02_missing_sync.sv --top missing_sync \
  -o reports --conn-format json --cdc-format json
```

If you just want the command surface first:

```bash
./build/svlens --help
./build/svlens conn --help
./build/svlens cdc --help
./build/svlens both --help
```

## Reference docs

- install / offline builds: [`docs/install.md`](docs/install.md)
- CLI and help contract: [`docs/cli-help.md`](docs/cli-help.md)
- JSON report schemas: [`docs/schema/`](docs/schema/)
- large-SoC waiver / baseline rollout: [`docs/waiver-baselines.md`](docs/waiver-baselines.md)
- release / packaging flow: [`docs/release.md`](docs/release.md)
- current release notes: [`docs/releases/v0.2.5.md`](docs/releases/v0.2.5.md)

---

## What it does

`svlens` currently exposes two analysis modes:

- **Connectivity / interconnect analysis**
  - port-to-port connectivity extraction
  - width mismatch, type mismatch, dangling output, undriven input
  - protocol completeness, naming convention checks
  - diff / trace / interface grouping / report generation

- **CDC (Clock Domain Crossing) analysis**
  - clock source and domain analysis
  - FF classification
  - FF-to-FF crossing detection
  - synchronizer recognition
  - CDC waiver / SDC / report generation

It also supports:

- **`svlens both`**
  - sequential `conn` + `cdc` execution
  - shared elaboration frontend
  - split output trees under one output root

---

## Build

For the detailed install guide, including offline / preinstalled dependency flows, see
[`docs/install.md`](docs/install.md).
For help output expectations and stable schema contracts, see
[`docs/cli-help.md`](docs/cli-help.md) and [`docs/schema/`](docs/schema/).

### Prerequisites

| Dependency | Version | Install |
|------------|---------|---------|
| C++ compiler | C++20 support (GCC 13+, Clang 16+) | system package |
| CMake | 3.20+ | system package |
| [slang](https://github.com/MikePopoloski/slang) | v10+ | see below |

By default, CMake can fetch missing `yaml-cpp` and `Catch2` dependencies.
For preinstalled / no-network builds, configure with `-DSVLENS_FETCH_DEPS=OFF`
after installing dependencies into your prefix or system package paths.
`fmt` is provided by `slang` when bundled, otherwise a system `fmt` install is used.

### Quick setup

```bash
./scripts/setup-deps.sh
./scripts/setup-deps.sh --prefix /opt/sv-deps
```

Offline / preinstalled dependency check:

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local" --offline
cmake -B build-offline -DCMAKE_PREFIX_PATH="$HOME/.local" -DSVLENS_FETCH_DEPS=OFF
cmake --build build-offline -j$(nproc)
```

### Manual slang install

```bash
git clone --depth 1 --branch v10.0 https://github.com/MikePopoloski/slang.git
cd slang
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build
```

### Build this project

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build -j$(nproc)
```

Offline / preinstalled dependency build:

```bash
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

For additional install paths and planned distribution routes
(prebuilt archives / Homebrew), see [`docs/install.md`](docs/install.md).

Release archive packaging and Homebrew/tap validation guidance live in
[`docs/release.md`](docs/release.md).

---

## Primary CLI

```bash
svlens conn [OPTIONS] <SV_FILES...>
svlens cdc  [OPTIONS] <SV_FILES...>
svlens both [COMMON_OPTIONS] [--conn-* ...] [--cdc-* ...] <SV_FILES...>
svlens help [conn|cdc|both]
```

### Help

```bash
./build/svlens --help
./build/svlens conn --help
./build/svlens cdc --help
./build/svlens both --help
./build/svlens help conn
```

For the detailed help contract and structure, see [`docs/cli-help.md`](docs/cli-help.md).
Stable JSON contracts are documented under [`docs/schema/`](docs/schema/).
For large-SoC waiver / baseline rollout guidance, see
[`docs/waiver-baselines.md`](docs/waiver-baselines.md).

### Version

```bash
./build/svlens --version
```

---

## Connectivity examples

```bash
# Basic connectivity analysis
./build/svlens conn design.sv --top my_top

# All output formats
./build/svlens conn -f filelist.f --top soc_top --format all -o reports/

# Selective checks with waivers
./build/svlens conn design.sv --top my_top --no-check-dangling --waiver waivers.yaml

# Protocol and convention checking
./build/svlens conn design.sv --top my_top --check-protocol --check-convention

# Ignore intentional NC / tie-off ports
./build/svlens conn design.sv --top my_top --ignore-nc --ignore-tie-off

# Diff against baseline
./build/svlens conn design.sv --top my_top --diff baseline/connect_report.json

# Expected connectivity
./build/svlens conn design.sv --top my_top --expect connectivity_spec.yaml

# Clock/reset naming heuristic summary
./build/svlens conn design.sv --top my_top --check-clock-reset

# Signal trace
./build/svlens conn design.sv --top my_top --trace "*.u_cpu.o_addr"
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
# Basic CDC analysis
./build/svlens cdc --top soc_top rtl/soc_top.sv rtl/subsystem.sv

# With SDC clock constraints
./build/svlens cdc --top soc_top rtl/*.sv --sdc syn/clocks.sdc

# With YAML clock specification
./build/svlens cdc --top soc_top rtl/*.sv --clock-yaml clock_domains.yaml

# Apply waivers
./build/svlens cdc --top soc_top rtl/*.sv --waiver cdc_waivers.yaml

# Require 3-stage synchronizers
./build/svlens cdc --top soc_top rtl/*.sv --sync-stages 3

# Strict CI mode
./build/svlens cdc --top soc_top rtl/*.sv --format json --strict -q

# Export DOT graph
./build/svlens cdc --top soc_top rtl/*.sv --dump-graph cdc_graph.dot
```

### CDC outputs

| Format | Description | File |
|--------|-------------|------|
| `md` | markdown CDC report | `cdc_report.md` |
| `json` | machine-readable CDC report | `cdc_report.json` |
| `sdc` | timing / false-path helper constraints | `cdc_constraints.sdc` |
| `waiver` | waiver template | `cdc_waiver_template.yaml` |

---

## `both` mode examples

`both` mode runs connectivity analysis first, then CDC analysis, under one output root.

```bash
./build/svlens both rtl/top.sv --top soc_top -o reports \
  --conn-format json \
  --conn-check-protocol \
  --cdc-format json \
  --cdc-sync-stages 3
```

Typical output tree:

```text
reports/
  conn/
    connect_report.json
    ...
  cdc/
    cdc_report.json
    ...
```

`both` mode also accepts filelists through the shared compilation frontend:

```bash
./build/svlens both -F rtl/filelist.f --top soc_top -o reports \
  --conn-format json \
  --cdc-format json
```

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
./build/svlens conn design.sv --top soc --expect connectivity_spec.yaml
```

### Custom convention rules

```yaml
input_prefix: in_
output_prefix: out_
instance_prefix: inst_
```

Nested keys such as `input.prefix`, `output.prefix`, and `instance.prefix` are also accepted.

```bash
./build/svlens conn design.sv --top soc --convention convention.yaml
```

---

## Current implementation limits

### Connectivity mode

- `ConnectionExtractor` follows named values, conversions, selects, struct-member access, continuous-assign aliases, and concatenation operands as approximate edges.
- Whole-interface / modport ports and procedural glue logic are still only partially modeled.
- Clock/reset analysis in `conn` mode remains name-based heuristic analysis, not semantic domain analysis.

### CDC mode

- SDC period data is used for reporting but not for full timing-aware crossing classification.
- Some advanced CDC test coverage from the original CDC project has not yet been fully ported into this repository’s Catch2 suite, though core utility/unit tests and golden integration tests are now present.

### Unified mode

- `svlens both` shares the compilation frontend and routes outputs correctly, but there is still room to further reduce duplicated mode-specific CLI parsing.

---

## Project layout

```text
include/sv-cdccheck/    Imported CDC public headers
src/                    Connectivity implementation + unified CLI + shared frontend
src/cdc/                Imported CDC implementation
tests/                  Catch2 tests + shell integration tests
tests/cdc/basic/        CDC fixtures
tests/cdc/golden/       CDC golden expectations
docs/plan/              Unification specs and implementation plans
```

---

## Planning documents

- [`docs/plan/svlens-unification-spec.md`](docs/plan/svlens-unification-spec.md)
- [`docs/plan/svlens-implementation-plan.md`](docs/plan/svlens-implementation-plan.md)

## Output schema documentation

- [`docs/schema/connect_report.md`](docs/schema/connect_report.md)
- [`docs/schema/cdc_report.md`](docs/schema/cdc_report.md)
- [`docs/schema/svlens_summary.md`](docs/schema/svlens_summary.md)

---

## Validation status

This repository currently includes:

- legacy `conn` unit/integration coverage
- unified CLI integration coverage
- CDC smoke + golden integration coverage
- CDC utility/unit coverage for:
  - clock database
  - filelist parser
  - waiver manager
  - clock yaml parser
  - report generator

Run all checks with:

```bash
ctest --test-dir build --output-on-failure
```
