# Contributing to svlens

Thanks for your interest in contributing! This document covers the development
workflow, coding conventions, and testing expectations. For repo-internal AI
agent guidance see [`CLAUDE.md`](CLAUDE.md).

## Development setup

```bash
# 1. Install dependencies (slang v10+, fmt, yaml-cpp, Catch2)
./scripts/setup-deps.sh --prefix "$HOME/.local"

# 2. Configure and build (debug for local development)
make debug    # or: cmake -B build -DCMAKE_BUILD_TYPE=Debug ...

# 3. Run the test suite (496+ tests)
make test
```

For offline / preinstalled-dependency builds, see [`docs/install.md`](docs/install.md).

## Project layout

| Path | Contents |
|------|----------|
| `src/` | Connectivity + unified CLI + shared frontend |
| `src/cdc/` | CDC implementation |
| `src/metrics/` | Metrics engine (TransformExtractor, ConeAnalyzer, BaselineDiff) |
| `include/sv-cdccheck/` | Imported CDC public headers |
| `tests/` | Catch2 unit tests + shell integration tests |
| `tests/sv/` | SystemVerilog test fixtures |
| `tests/cdc/basic/` | CDC golden fixtures |
| `docs/schema/` | Stable JSON report schemas |

## Coding conventions

- **Language**: C++20, slang v10+ API.
- **Formatting**: enforced via `clang-format` (see `.clang-format`). Run
  `clang-format -i <file>` before committing, or rely on the pre-commit hook.
- **Headers**: prefer forward declarations in headers; include implementation
  details only in `.cpp`. Keep slang-specific types out of public headers when
  feasible.
- **Naming**: `CamelCase` for types, `snake_case` for functions and locals,
  `kCamelCase` for constants, `m_` prefix for non-public class members.
- **Error reporting**: prefer structured `Diagnostic` / JSON output over ad-hoc
  `fmt::print`. CLI surfaces should emit machine-parseable JSON when
  `--format json` is set.

## Adding a feature or check

1. **Open an issue first** for non-trivial changes so we can align on scope.
2. **Branch off `main`**: `git checkout -b feat/<short-name>`.
3. **Write tests first**:
   - Unit tests in `tests/<area>_tests.cpp` (Catch2).
   - Integration tests under `tests/integration/` (shell).
   - SV fixtures in `tests/sv/<area>/` if exercising new RTL constructs.
4. **Update schemas** in `docs/schema/` if you change JSON report fields.
   Schemas are stable contracts -- additive changes only without a major bump.
5. **Document** new CLI flags in `docs/cli-help.md` and the relevant README
   section.

## Commit / PR conventions

- **Commit style**: imperative subject ≤ 72 chars, then a blank line and a
  rationale paragraph. Examples:
  - `Harden SDC parser tests with edge cases`
  - `Add clock_groups auto-waive and single-group implicit other in CDC`
- **Atomic commits**: each commit should build and pass tests independently
  whenever practical. Avoid mixing refactors with feature work.
- **PR description**: include
  - what changed and why,
  - test plan / `make test` output summary,
  - any schema or CLI surface changes,
  - linked issue if applicable.
- **CI must pass** before merge. The `ci`, `portable-smoke`, and offline
  smokes all gate the PR.

## Testing expectations

| Change | Required tests |
|--------|----------------|
| New CLI flag | help-contract test + integration shell test |
| New JSON field | schema test + at least one fixture exercising it |
| Connectivity check | SV fixture under `tests/sv/conn/` + golden report |
| CDC check | fixture under `tests/cdc/basic/` + golden report |
| Metrics check | fixture under `tests/sv/metrics/` + baseline-diff test |
| Bugfix | regression test that fails without the fix |

Run the full suite locally before pushing:

```bash
make test
ctest --test-dir build --output-on-failure
```

## Reporting bugs

Please file issues at <https://github.com/babyworm/svlens/issues> with:

- svlens version (`svlens --version`) and slang version,
- minimal SV reproduction (a small filelist + sources is ideal),
- exact command line and observed vs. expected output,
- platform (OS, compiler version).

## License

By contributing, you agree that your contributions will be licensed under the
[MIT License](LICENSE).
