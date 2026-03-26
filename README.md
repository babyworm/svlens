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

- **table** -- Terminal output
- **json** -- Machine-readable report
- **md** -- Markdown review report
- **csv** -- Connection matrix spreadsheet
- **all** -- All formats (default)
