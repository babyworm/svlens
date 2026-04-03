# sv-conncheck Agent Guide

This `AGENTS.md` applies to the repository root and all files under it.

## Project Goal

`sv-conncheck` is a SystemVerilog interconnect verification and connectivity analysis tool built on top of `slang`.

Primary goals:

- Extract module-to-module connectivity from elaborated SystemVerilog designs.
- Detect interconnect issues such as width mismatches, type mismatches, dangling outputs, undriven inputs, protocol incompleteness, and naming convention violations.
- Provide both machine-readable and human-readable outputs for review, regression tracking, and CI.
- Keep CLI behavior and library behavior aligned. If an option exists in the CLI, it should have real implementation and tests.

## Repository Layout

- [`src/`](/home/babyworm/work/sv-conncheck/src): core implementation
  Contains the extractor, checkers, analyzers, reports, CLI entrypoint, and graph/filter logic.
- [`tests/`](/home/babyworm/work/sv-conncheck/tests): Catch2 test suite
  Contains unit tests, integration-style tests, shell integration checks, and shared test utilities.
- [`tests/sv/`](/home/babyworm/work/sv-conncheck/tests/sv): SystemVerilog fixtures used by tests
  Add new RTL test cases here.
- [`tests/TestUtils.h`](/home/babyworm/work/sv-conncheck/tests/TestUtils.h), [`tests/TestUtils.cpp`](/home/babyworm/work/sv-conncheck/tests/TestUtils.cpp): shared test helpers
  Use these instead of duplicating per-test compile helpers.
- [`docs/`](/home/babyworm/work/sv-conncheck/docs): project documentation and design notes.
- [`scripts/`](/home/babyworm/work/sv-conncheck/scripts): setup and developer utility scripts.
- [`build/`](/home/babyworm/work/sv-conncheck/build): generated build artifacts
  Do not treat this as source of truth.

## Development Rules

- Prefer fixing root causes over papering over symptoms.
- Keep changes narrow and local when possible, but do not leave CLI flags, reports, or tests in inconsistent states.
- When adding or changing connectivity semantics, check downstream consumers too:
  reports, checkers, trace, analysis, and CLI behavior.
- Treat `build/` contents as disposable generated outputs. Source files under `tests/sv/` are the authoritative test fixtures.

## TDD Is Required

This repository should be changed using test-driven development.

Expected workflow:

1. Write or update a test that captures the intended behavior before the implementation change.
2. Confirm the new test fails for the current code when practical.
3. Implement the code change.
4. Run targeted tests for the touched area.
5. Run the full test suite before considering the work complete.

For bug fixes:

- Reproduce the bug with a regression test first.
- Do not remove or weaken the regression test after the fix.

For new CLI options or semantics:

- Add at least one unit or integration test for the implementation.
- Add CLI-level coverage when the behavior is user-visible.

## Test Fixture Rules

- Put new SystemVerilog fixtures in [`tests/sv/`](/home/babyworm/work/sv-conncheck/tests/sv).
- When a C++ test needs to compile an SV fixture, use `testutils::compileFile(...)` from [`tests/TestUtils.h`](/home/babyworm/work/sv-conncheck/tests/TestUtils.h).
- When a test needs fixture path resolution, use `testutils::resolveSvFixturePath(...)`.
- Do not reintroduce ad-hoc duplicated `compileFile` helpers in individual test files.
- Do not rely on stale copied fixtures under `build/tests/sv`; tests must resolve source fixtures from `tests/sv`.

## Verification Expectations

Before concluding non-trivial work:

- Build with:
  `cmake --build build -j$(nproc)`
- Run targeted tests first.
- Run the full suite with:
  `ctest --test-dir build --output-on-failure`

If CLI behavior changed, ensure shell-level coverage still passes:

- `integration_shell`

## When Updating This Repository

- If you change repository structure, test workflow, or fixture-handling conventions, update this file in the same change.
- If you discover a recurring failure mode that can be prevented with project-level guidance, add that guidance here.
