# svlens

SystemVerilog structural analysis toolkit built on slang v10+.

## Build & Test

```bash
make build    # Release build
make test     # Run full test suite (496+ tests)
make debug    # Debug build
```

## Architecture

Three analysis modes sharing a common slang compilation frontend:

- **conn** -- port/connectivity graph extraction and checking
- **cdc** -- clock-domain crossing analysis
- **metrics** -- RTL transformation complexity analysis

Entry point: `src/svlens_main.cpp`
Frontend: `src/CompilationSession.cpp`, `src/CommonCli.cpp`

### Hot paths

- `src/metrics/TransformExtractor.cpp` -- backward cone extraction (most-modified file)
- `src/MetricsRunner.cpp` -- metrics orchestration and JSON report generation
- `src/ConnectionExtractor.cpp` -- connectivity graph builder
- `src/cdc/crossing_detector.cpp` -- CDC crossing classification

### Conventions

- C++20, slang v10+ API
- Catch2 for unit tests, shell scripts for integration tests
- Test fixtures in `tests/sv/` and `tests/cdc/basic/`
- JSON report schemas in `docs/schema/`
