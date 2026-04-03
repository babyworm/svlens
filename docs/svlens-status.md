# svlens Unification Status

Status: implemented  
Updated: 2026-04-03

## Summary

This repository has been consolidated around a single user-facing executable:

- `svlens`

Supported modes:

- `svlens conn`
- `svlens cdc`
- `svlens both`

The previous standalone user-facing binaries are no longer the primary interface.

## Implemented Architecture

### Common layer

- `svlens-common-lib`
  - common CLI helper logic
  - shared compilation frontend
  - shared filelist expansion

Key files:

- [`src/CommonCli.h`](../src/CommonCli.h)
- [`src/CommonCli.cpp`](../src/CommonCli.cpp)
- [`src/CompilationSession.h`](../src/CompilationSession.h)
- [`src/CompilationSession.cpp`](../src/CompilationSession.cpp)

### Connectivity layer

- `svlens-conn-lib`

Key files:

- [`src/ConnRunner.h`](../src/ConnRunner.h)
- [`src/ConnRunner.cpp`](../src/ConnRunner.cpp)
- [`src/ConnRunnerUtils.h`](../src/ConnRunnerUtils.h)
- [`src/ConnRunnerUtils.cpp`](../src/ConnRunnerUtils.cpp)

### CDC layer

- `svlens-cdc-lib`

Key files:

- [`src/CdcRunner.h`](../src/CdcRunner.h)
- [`src/CdcRunner.cpp`](../src/CdcRunner.cpp)
- [`src/CdcRunnerUtils.h`](../src/CdcRunnerUtils.h)
- [`src/CdcRunnerUtils.cpp`](../src/CdcRunnerUtils.cpp)
- vendored CDC implementation under [`src/cdc/`](../src/cdc/)
- vendored public headers under [`include/sv-cdccheck/`](../include/sv-cdccheck/)

### Unified orchestration

- [`src/svlens_main.cpp`](../src/svlens_main.cpp)

`svlens both` runs `conn` and `cdc` over one shared `CompilationSession`, writes split output directories, and emits `svlens_summary.json`.

## Testing Status

The repository now includes:

- legacy connectivity unit tests
- unified CLI integration tests
- CDC utility/unit tests
- CDC pipeline tests
- CDC runner tests
- CDC golden integration tests
- imported CDC upstream tests

Representative scripts:

- [`tests/test_integration.sh`](../tests/test_integration.sh)
- [`tests/test_svlens_integration.sh`](../tests/test_svlens_integration.sh)
- [`tests/test_cdc_golden.sh`](../tests/test_cdc_golden.sh)

## Current Validation Result

Latest full regression result:

- `ctest --test-dir build --output-on-failure`
- **464 / 464 tests passing**

## Packaging / CI

Primary build/install flow:

- [`Makefile`](../Makefile)
- [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)

Dependency bootstrap:

- [`scripts/setup-deps.sh`](../scripts/setup-deps.sh)

## Known Follow-up Opportunities

These are improvements, not blockers:

1. Further split parser/help/version code from runner files.
2. Reduce duplicated local helper logic across imported CDC tests if desired.
3. Further optimize CI cache scope / build artifact reuse.
4. Add release notes / changelog policy if this will be versioned more formally.
