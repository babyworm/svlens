# svlens installation guide

`svlens` supports three installation paths in Phase 1A:

1. **Source build with dependency fetching**
2. **Offline / preinstalled dependency build**
3. **Installed binary from a local build prefix**

`svlens` remains a **pre-signoff structural analysis** tool; the goal of these flows is
reproducible installation and predictable first-run behavior, not universal packaging parity.

---

## 1. Prerequisites

Required for all source-based flows:

- C++20 compiler
- CMake 3.20+
- [`slang`](https://github.com/MikePopoloski/slang) v10+

Additional dependencies:

- `yaml-cpp`
- `Catch2` (only when `BUILD_TESTING=ON`)
- `fmt` (usually supplied by `slang`; otherwise install system `fmt`)

---

## 2. Quick source build

Bootstrap `slang` into your prefix:

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local"
```

Then configure and build `svlens`:

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

This path allows CMake to fetch missing `yaml-cpp` / `Catch2` if they are not already
available.

---

## 3. Offline / preinstalled dependency build

Use this flow when:

- your environment is offline or restricted,
- you want reproducible builds without `FetchContent`, or
- you preinstall dependencies through your package manager or dependency prefix.

First validate the prefix has `slang`:

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local" --offline
```

Then configure with dependency fetching disabled:

```bash
cmake -B build-offline \
  -DCMAKE_PREFIX_PATH="$HOME/.local" \
  -DSVLENS_FETCH_DEPS=OFF
cmake --build build-offline -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
ctest --test-dir build-offline --output-on-failure
cmake --install build-offline --prefix "$HOME/.local"
```

When `SVLENS_FETCH_DEPS=OFF` is set, `svlens` will not download `yaml-cpp` / `Catch2`
during configure. Missing dependencies are reported as actionable configuration errors.

---

## 4. Installed binary smoke check

After installation:

```bash
"$HOME/.local/bin/svlens" --help
"$HOME/.local/bin/svlens" conn --help
"$HOME/.local/bin/svlens" cdc --help
```

Expected outcomes:

- the installed binary exists at `<prefix>/bin/svlens`
- root help lists `conn`, `cdc`, and `both`
- subcommand help remains available after install

---

## 5. Packaging direction (current)

The approved Phase 1A/1B install direction is:

- **source build**
- **prebuilt release archives**
- **Homebrew tap**

The following remain out of scope for the current milestone:

- deb/rpm packaging
- nix flake support
- Windows installers
- static universal binaries

For release archive production and Homebrew/tap validation guidance, see
[`docs/release.md`](release.md).

---

## 6. Troubleshooting

### `yaml-cpp not found` or `Catch2 not found`

- install them in your prefix or system package paths, then rerun with:

```bash
cmake -B build-offline -DCMAKE_PREFIX_PATH="$HOME/.local" -DSVLENS_FETCH_DEPS=OFF
```

### `slang not found`

- bootstrap it with:

```bash
./scripts/setup-deps.sh --prefix "$HOME/.local"
```

### Offline mode fails immediately

- `./scripts/setup-deps.sh --offline` only validates an existing `slang` install
- it does **not** download missing dependencies
