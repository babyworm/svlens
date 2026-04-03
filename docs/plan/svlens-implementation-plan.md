# svlens Implementation Plan

Status: Draft  
Depends on: [`svlens-unification-spec.md`](svlens-unification-spec.md)

> Note:
> This implementation plan was written before the final decision to ship only
> the `svlens` binary. Some phases still mention compatibility wrappers /
> legacy binaries as rollout steps; treat those references as historical plan
> context, not the current end-state contract.

---

## 1. Goal

Implement `svlens` as the unified executable for:

- connectivity analysis (`conn`)
- CDC analysis (`cdc`)
- sequential dual execution (`both`)

while preserving current behavior of:

- `sv-conncheck`
- `sv-cdccheck`

This plan is intentionally implementation-oriented. It breaks the work into concrete phases, code moves, test additions, and acceptance criteria.

---

## 2. Ground Rules

This repository is under TDD rules.

For every phase below:

1. add or update tests first,
2. reproduce the missing behavior or regression if practical,
3. implement the minimum code to satisfy the tests,
4. run targeted tests,
5. run the full suite before phase completion.

Do not start by merging engines. Start by freezing behavior and extracting shared infrastructure.

---

## 3. Target End State

At the end of this plan:

- `svlens conn ...` behaves like `sv-conncheck`
- `svlens cdc ...` behaves like `sv-cdccheck`
- `svlens both ...` runs both analyses on one elaborated design
- shared compilation/frontend code is no longer duplicated
- shared AST/path helpers are reused where overlap is safe
- `sv-conncheck` and `sv-cdccheck` remain available as compatibility entrypoints

---

## 4. Work Breakdown

## Phase 0 — Freeze Contracts

### Purpose

Make the desired `svlens` command behavior explicit before code movement begins.

### Tasks

- Keep [`svlens-unification-spec.md`](svlens-unification-spec.md) as the authoritative contract.
- Add this implementation plan as the execution checklist.
- Record unresolved decisions explicitly instead of leaving them implicit.

### Deliverables

- command contract document
- implementation plan document

### Exit Criteria

- no major ambiguity remains about:
  - command names,
  - output layout,
  - compatibility strategy,
  - exit-code behavior.

---

## Phase 1 — Lock Current Behavior with Golden Tests

### Purpose

Prevent refactors from silently changing user-visible behavior.

### Tasks

#### 1.1 `sv-conncheck` golden coverage

Add or expand tests for:

- `--help`
- version output
- representative fixture runs
- exit code on known issue cases
- output file existence
- representative JSON keys / structure

#### 1.2 `sv-cdccheck` golden coverage

In the `sv-cdccheck` repository, add or expand tests for:

- `--help`
- version output
- representative fixture runs
- exit code on known CDC violation cases
- report existence
- representative JSON keys / structure

#### 1.3 Parity fixture selection

Choose a small fixture set for fast parity checks:

- one clean design
- one connectivity-issue design
- one CDC-issue design
- one fixture with filelist usage

### Code Areas

- current `tests/` in this repo
- corresponding `tests/` in `sv-cdccheck`

### New Tests

- `test_cli_help_*`
- `test_cli_version_*`
- shell or Catch2 parity/golden tests as appropriate

### Exit Criteria

- both tools have stable, automated golden coverage for their current public behavior.

---

## Phase 2 — Introduce a Shared Frontend Library

### Purpose

Extract duplicated slang setup and elaboration into one reusable layer.

### Tasks

#### 2.1 Create `CompilationSession`

Implement a common abstraction responsible for:

- owning `slang::driver::Driver`
- parsing common args
- expanding filelists
- forwarding slang args
- creating `slang::ast::Compilation`
- resolving / validating the top module

#### 2.2 Create `CommonCliOptions`

Add a shared common-options model for:

- `--top`
- `-o/--output`
- input source files
- filelist inputs
- shared verbosity/version/help handling where safe

#### 2.3 Refactor `sv-conncheck` to use shared compilation session

Move current compile/elaboration path behind the new abstraction.

#### 2.4 Refactor `sv-cdccheck` to use shared compilation session

Same change, without touching CDC analysis semantics.

### Proposed New Files

```text
src/common/compilation_session.h
src/common/compilation_session.cpp
src/common/cli_common.h
src/common/cli_common.cpp
```

### Tests First

Add tests for:

- top-module resolution
- filelist expansion
- slang pass-through preservation
- error behavior on missing top
- identical elaboration behavior for representative fixtures

### Exit Criteria

- both tools elaborate through one shared path
- existing golden tests remain green

---

## Phase 3 — Shared AST / Hierarchy Utility Layer

### Purpose

Consolidate overlapping signal/path extraction logic without merging the engines.

### Tasks

#### 3.1 Inventory overlap

Compare:

- `sv-conncheck` expression resolution
- `sv-cdccheck` `ast_utils`

#### 3.2 Extract shared helpers

Create common utilities for:

- named value extraction
- arbitrary symbol extraction
- member access
- element/range select
- conversion stripping
- concatenation decomposition
- replication decomposition
- streaming concatenation decomposition
- tie-off detection
- path normalization / hierarchy helpers

#### 3.3 Rewire both tools incrementally

- migrate `sv-conncheck` first if it already has broader coverage
- then migrate `sv-cdccheck`

### Proposed New Files

```text
src/common/ast_signal_utils.h
src/common/ast_signal_utils.cpp
src/common/hierarchy_utils.h
src/common/hierarchy_utils.cpp
```

### Tests First

Add targeted regression tests for:

- struct member access
- array/range select
- concat/replication/streaming
- tie-off handling
- stale fixture resolution / path helpers

### Exit Criteria

- overlapping AST cases are served by shared helpers
- no regression in existing analysis tests

---

## Phase 4 — Create `svlens conn` and `svlens cdc`

### Purpose

Add the unified entrypoint while leaving old binaries intact.

### Tasks

#### 4.1 Add unified CLI dispatcher

Create a new executable:

```bash
svlens conn ...
svlens cdc ...
```

#### 4.2 Implement runner wrappers

Add thin runners:

- `ConnRunner`
- `CdcRunner`

Each runner should accept:

- shared compilation/session state
- mode-specific options
- output root

and should internally delegate to the existing engine logic.

#### 4.3 Keep compatibility entrypoints

Choose one:

- wrapper binaries, or
- argv[0]-based aliasing

Recommended implementation:

- keep `sv-conncheck`
- keep `sv-cdccheck`
- route both through shared runner code

### Proposed New Files

```text
src/cli/svlens_main.cpp
src/cli/dispatch.h
src/cli/dispatch.cpp
src/conn/runner.h
src/conn/runner.cpp
src/cdc/runner.h
src/cdc/runner.cpp
```

### Tests First

Add parity tests:

- `svlens conn` vs `sv-conncheck`
- `svlens cdc` vs `sv-cdccheck`
- help/version tests for all entrypoints

### Exit Criteria

- `svlens conn` and `svlens cdc` both work
- old binaries still behave compatibly

---

## Phase 5 — Implement `svlens both`

### Purpose

Enable sequential execution of both analyses on one elaborated compilation.

### Tasks

#### 5.1 Add `both` dispatch mode

`svlens both` must:

- parse shared options,
- parse `conn`-prefixed options,
- parse `cdc`-prefixed options,
- elaborate once,
- run `ConnRunner`,
- run `CdcRunner`.

#### 5.2 Add output partitioning

For:

```bash
svlens both --output reports ...
```

write to:

```text
reports/conn/...
reports/cdc/...
```

#### 5.3 Add combined summary artifact (optional initial version)

If added in this phase:

- `svlens_summary.json`
- should only summarize high-level counts and output paths

### Tests First

Add tests for:

- one elaboration reused for both runs
- both output trees exist
- conn outputs match standalone conn run
- cdc outputs match standalone cdc run
- exit code policy (`max(conn_exit, cdc_exit)`)

### Exit Criteria

- `svlens both` works end-to-end with shared elaboration

---

## Phase 6 — Wrapper Finalization and Packaging

### Purpose

Make `svlens` the primary binary without breaking existing users.

### Tasks

- finalize compatibility wrappers
- update install/build targets
- ensure CI builds all expected executables
- document preferred invocation

### Packaging Targets

Recommended installed binaries:

- `svlens`
- `sv-conncheck` (compat)
- `sv-cdccheck` (compat)

### Tests

- installation/invocation smoke tests if packaging tests exist
- wrapper behavior tests

### Exit Criteria

- all binaries resolve to the correct internal mode

---

## Phase 7 — Documentation and CI Migration

### Purpose

Align docs and automation with the new structure.

### Tasks

- update README(s)
- add migration notes
- update examples to prefer `svlens`
- update CI to exercise:
  - `svlens conn`
  - `svlens cdc`
  - `svlens both`

### Exit Criteria

- docs and CI no longer describe outdated architecture

---

## 5. File / Module Mapping

## Current `sv-conncheck` areas likely to move or wrap

- CLI / orchestration:
  - [`src/main.cpp`](../../src/main.cpp)
- conn engine:
  - extractor, graph, filters, checkers, reports

## Current `sv-cdccheck` areas likely to move or wrap

- CLI / orchestration:
  - original external source: `sv-cdccheck/src/main.cpp`
- cdc engine:
  - clock tree, FF classifier, connectivity, crossing detector, sync verifier, reports

## Expected ownership after unification

| Area | Owner |
|---|---|
| compilation session | shared/common |
| filelist + source expansion | shared/common |
| AST signal helpers | shared/common |
| connectivity report engine | conn runner |
| CDC report engine | cdc runner |

---

## 6. Testing Plan

## 6.1 Unit Tests

Add or extend tests for:

- argument parsing
- session creation
- top resolution
- filelist expansion
- expression decomposition
- path normalization

## 6.2 Runner Parity Tests

Need explicit parity cases:

- `sv-conncheck` vs `svlens conn`
- `sv-cdccheck` vs `svlens cdc`

Compare:

- exit code
- output file set
- selected JSON keys / counts

## 6.3 `both` Mode Tests

Need tests for:

- sequential execution
- output partitioning
- exit code aggregation
- standalone parity of each sub-result

## 6.4 Full Regression

Before each merge:

### In this repo

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### In `sv-cdccheck`

Run that repository’s full test suite as part of the cross-repo integration effort.

---

## 7. Key Decisions to Keep Stable

- use subcommands, not a flat global option space
- preserve old binaries during rollout
- reuse one elaboration in `both`
- keep conn graph and cdc graph separate
- do not skip golden tests before refactor

If any of these must change, update both:

- the unification spec
- this implementation plan

---

## 8. Acceptance Criteria

Implementation is complete when all of the following are true:

- `svlens conn` matches current connectivity analysis behavior
- `svlens cdc` matches current CDC analysis behavior
- `svlens both` runs both analyses using one elaboration
- shared frontend replaces duplicated elaboration logic
- shared AST utilities are used where overlap exists
- compatibility entrypoints still work
- docs and CI reflect the new preferred workflow
- full regression suites remain green

---

## 9. Immediate Next Step

Start with **Phase 1**:

1. enumerate current public behavior to freeze,
2. add golden tests for both tools,
3. only then begin shared frontend extraction.
