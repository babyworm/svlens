# svlens release and distribution notes

This document records the Phase 3 distribution / CI polish path for `svlens`.

`svlens` remains a **pre-signoff structural analysis** tool. Distribution work
improves installation ergonomics and repeatability; it does not change the
product boundary.

## 1. Release archive production path

Phase 3 standardizes a release archive flow driven by:

- GitHub Actions workflow: `.github/workflows/release.yml`
- Packaging helper: `scripts/package-release.sh`

### Local packaging

From a built tree:

```bash
./scripts/package-release.sh build-offline svlens-local dist
```

This creates:

- `dist/svlens-local/` staging directory
- `dist/svlens-local.tar.gz`
- `dist/svlens-local.zip` when `zip` is available

### Archive contents

- `svlens` binary
- `LICENSE`
- `README.md`
- `docs/install.md`
- `docs/cli-help.md`
- `docs/schema/*.md`

## 2. CI release archive exercise

The release workflow:

1. stages `slang`
2. configures a release build with `SVLENS_FETCH_DEPS=OFF`
3. builds `svlens`
4. runs `scripts/package-release.sh`
5. uploads the generated archives as workflow artifacts

This is the Phase 3 “documented and exercised” release path.

## 3. Homebrew / tap path

### Current status

**Deferred with rationale.**

Why deferred:

- a stable tagged release URL + SHA256 workflow must exist first
- bottle strategy is premature until macOS/Linux release artifacts settle
- Phase 3 focuses first on a reliable archive path and OS smoke coverage

### Validation path once enabled

1. create a tap formula pointing at a tagged archive
2. validate formula syntax on macOS CI
3. install via `brew install <tap>/svlens`
4. verify `svlens --help`, `svlens conn --help`, and a small contract bundle

## 4. Phase 3 exit interpretation

Phase 3 is considered complete when:

- OS matrix smoke is green
- archive production path is documented and exercised
- Homebrew/tap handling is either exercised or explicitly deferred with rationale
