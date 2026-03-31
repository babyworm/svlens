# sv-conncheck

Module interconnect verification and connectivity analysis tool for SystemVerilog designs.
Analyzes port connections and reports width mismatches, type mismatches,
dangling outputs, undriven inputs, protocol completeness, and naming conventions.
Provides quantitative health scoring, risk assessment, and interactive visualization.

Built on [slang](https://github.com/MikePopoloski/slang) v10+.

## Prerequisites

| Dependency | Version | Install |
|------------|---------|---------|
| C++ compiler | C++20 support (GCC 13+, Clang 16+) | system package |
| CMake | 3.20+ | system package |
| [slang](https://github.com/MikePopoloski/slang) | v10+ | see below |

**Auto-fetched** (no manual install needed): yaml-cpp 0.8.0, Catch2 v3.5.2, fmt (bundled by slang)

### Quick setup (recommended)

The setup script checks prerequisites and installs slang automatically:

```bash
./scripts/setup-deps.sh                          # install to $HOME/.local (default)
./scripts/setup-deps.sh --prefix /opt/sv-deps    # or custom location
```

### Manual slang install

```bash
git clone --depth 1 --branch v7.0 https://github.com/MikePopoloski/slang.git
cd slang
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build
```

## Build

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build -j$(nproc)
```

`CMAKE_PREFIX_PATH` should point to the directory where slang was installed.

If slang is not found, CMake will print the exact setup command to run.

To run tests:

```bash
ctest --test-dir build
```

## Usage

```bash
# Basic analysis
sv-conncheck design.sv --top my_top

# Full analysis with all output formats
sv-conncheck -f filelist.f --top soc_top --format all -o reports/

# Selective checks with waivers
sv-conncheck design.sv --top my_top --no-check-dangling --waiver waivers.yaml

# Protocol and convention checking
sv-conncheck design.sv --top my_top --check-protocol --check-convention

# Generate block diagram (Graphviz DOT)
sv-conncheck design.sv --top my_top --format dot -o reports/
dot -Tsvg reports/connectivity.dot -o block_diagram.svg

# Interactive HTML dashboard
sv-conncheck design.sv --top my_top --format html -o reports/
open reports/connect_report.html

# Compare against baseline (CI integration)
sv-conncheck design.sv --top my_top --diff baseline/connect_report.json

# Verify expected connectivity
sv-conncheck design.sv --top my_top --expect connectivity_spec.yaml

# Clock/reset topology analysis
sv-conncheck design.sv --top my_top --check-clock-reset

# Signal fan-in/fan-out tracing
sv-conncheck design.sv --top my_top --trace "*.u_cpu.o_addr"
```

## Checks

| Check | Detects | Default |
|-------|---------|---------|
| Width Mismatch | Silent truncation or extension | ON |
| Type Mismatch | signed/unsigned mismatch | ON |
| Dangling Output | Unconnected output ports | ON |
| Undriven Input | Input ports with no driver | ON |
| Protocol Completeness | Missing AXI/AHB/APB signals | OFF (`--check-protocol`) |
| Convention | Port/instance naming violations | OFF (`--check-convention`) |
| Expected Connectivity | Missing or forbidden connections | OFF (`--expect <file>`) |
| Clock/Reset Topology | Clock distribution, missing resets | OFF (`--check-clock-reset`) |

## Analysis Features

### Module Health Score
Each module gets a 0-100% health score based on port connectivity and issue density.
Displayed as progress bars in table output and as a gauge in the HTML dashboard.

### Risk Assessment
Issues are classified as HIGH/MEDIUM/LOW risk with human-readable explanations:
- **HIGH**: Width truncation (data loss), undriven inputs, forbidden connections
- **MEDIUM**: Type mismatch, missing expected connections, incomplete protocols
- **LOW**: Dangling outputs, naming conventions, width extension

### Coupling Matrix
Shows connection counts between each module pair, sorted by coupling strength.
The HTML dashboard includes an interactive heatmap with hierarchical drill-down.

### Interface Grouping
Automatically detects AXI4, AXI4-Lite, AXI-Stream, AHB, and APB interfaces
by matching port name suffixes. Determines master/slave role from signal directions.

### Signal Trace
Traces a signal's fan-out (where does it go?) or fan-in (where does it come from?)
across the module hierarchy using BFS traversal.

## Output Formats

| Format | Description | File |
|--------|-------------|------|
| **table** | Terminal output with health scores, risks, coupling | stdout |
| **json** | Machine-readable with full analysis data | `connect_report.json` |
| **md** | Markdown review report | `connect_report.md` |
| **csv** | Connection matrix spreadsheet | `connection_matrix.csv` |
| **dot** | Graphviz block diagram | `connectivity.dot` |
| **html** | Interactive dashboard (4 tabs: Overview, Graph, Heatmap, Details) | `connect_report.html` |
| **all** | All formats (default) | all of the above |

## Connection Diff

Compare current analysis against a baseline JSON report to detect connectivity changes:

```bash
# Generate baseline
sv-conncheck design.sv --top soc --format json -o baseline/

# After modifications, compare
sv-conncheck design_v2.sv --top soc --diff baseline/connect_report.json
```

Output shows added, removed, and status-changed connections.

## Expected Connectivity

Define expected and forbidden connections in YAML:

```yaml
expected:
  - from: "*.u_cpu.o_ibus_*"
    to: "*.u_bus.i_cpu_ibus_*"

forbidden:
  - from: "*.u_debug.*"
    to: "*.u_secure_*"
```

```bash
sv-conncheck design.sv --top soc --expect connectivity_spec.yaml
```

## Tested Against

Validated on real open-source RTL projects:
- **SERV** (serv_top) — 95 connections, 10 modules
- **ibex** (ibex_core) — 130 connections, 26 modules
- **picorv32** (picorv32_axi) — 7 connections, AXI4 interface detected
- **wb2axip** (axixbar) — 8 connections, generate-for blocks
