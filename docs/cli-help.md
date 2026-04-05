# svlens CLI help guide

`svlens` ships layered help:

- `svlens --help` — root overview, quick-start, install hint, docs pointer, product boundary
- `svlens help conn` / `svlens conn --help` — connectivity-specific contract
- `svlens help cdc` / `svlens cdc --help` — CDC-specific contract
- `svlens help both` / `svlens both --help` — combined-run contract

## Help architecture

Phase 1B uses a **hybrid help architecture**:

- **Generated / structured sections**
  - command usage
  - option tables
  - required/common/output sections
- **Hand-authored narrative sections**
  - quick-start examples
  - exit code semantics
  - limitations / heuristic notes
  - install hints
  - product boundary messaging

This keeps help aligned with the CLI surface while still giving users practical guidance.

## Product boundary

`svlens` is a **pre-signoff structural analysis** tool.
Use it for CI gates, review support, and early structural/CDC checks.
Do not treat the help text as a claim of sign-off equivalence.

## Stable expectations in Phase 1B

### Root help
Must include:
- usage for `conn`, `cdc`, `both`, and `help`
- quick-start examples
- install hint for offline / preinstalled builds
- docs pointer
- product boundary note

### Subcommand help
Must include:
- `Required`
- `Common`
- `Outputs`
- `Examples`
- `Exit Codes`
- `Limitations`
- `Notes`

## Related docs

- [`docs/install.md`](install.md)
- [`docs/schema/connect_report.md`](schema/connect_report.md)
- [`docs/schema/cdc_report.md`](schema/cdc_report.md)
- [`docs/schema/svlens_summary.md`](schema/svlens_summary.md)
