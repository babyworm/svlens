# svlens installation guide

`svlens` supports the following installation paths:

1. **Source build with dependency fetching**
2. **Offline / preinstalled dependency build**
3. **Installed binary from a local build prefix**
4. **Prebuilt release archive** (Linux x86_64, macOS arm64)
5. **Docker image** from GHCR
6. **Homebrew tap**

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

## 5. Prebuilt release archive

Each tagged release publishes per-platform tarballs and zip archives via the
[`release-artifacts`](../.github/workflows/release.yml) workflow. Available
artifacts:

| Platform | Archive |
|----------|---------|
| Linux x86_64 | `svlens-linux-x86_64.tar.gz` / `.zip` |
| macOS arm64 (M-series) | `svlens-macos-arm64.tar.gz` / `.zip` |

Download from the [Releases page](https://github.com/babyworm/svlens/releases),
verify against the `*.sha256` companion file, then extract and install:

```bash
TAG=v0.2.7  # replace with the desired tag
ARCH=linux-x86_64  # or macos-arm64

curl -LO "https://github.com/babyworm/svlens/releases/download/${TAG}/svlens-${ARCH}.tar.gz"
curl -LO "https://github.com/babyworm/svlens/releases/download/${TAG}/svlens-${ARCH}.sha256"
shasum -a 256 -c "svlens-${ARCH}.sha256"

tar -xzf "svlens-${ARCH}.tar.gz"
install -m 0755 "svlens-${ARCH}/svlens" "$HOME/.local/bin/svlens"
```

---

## 6. Docker image

Each tagged release also publishes a multi-arch image to GHCR via the
[`docker`](../.github/workflows/docker.yml) workflow:

```bash
docker pull ghcr.io/babyworm/svlens:latest
# or pin a version
docker pull ghcr.io/babyworm/svlens:0.2.7

docker run --rm -v "$PWD:/work" -w /work ghcr.io/babyworm/svlens:latest \
  conn -f rtl/filelist.f --top soc_top
```

The image entrypoint is `/usr/local/bin/svlens`, so all svlens subcommands
work directly. Mount your project at `/work` (or any path) and pass relative
paths.

---

## 7. Homebrew tap

A Homebrew formula template lives in
[`packaging/homebrew/svlens.rb`](../packaging/homebrew/svlens.rb). Once a tap
repository (`babyworm/homebrew-svlens`) is published, install with:

```bash
brew tap babyworm/svlens
brew install svlens
```

The formula builds from source against the tagged tarball; see
[`packaging/homebrew/README.md`](../packaging/homebrew/README.md) for the
release-time update workflow.

---

## 8. Packaging scope

The approved install direction is:

- **source build**
- **prebuilt release archives** (Linux x86_64, macOS arm64)
- **Docker image** (GHCR)
- **Homebrew tap**

The following remain out of scope for the current milestone:

- deb/rpm packaging
- nix flake support
- Windows installers
- static universal binaries

For release archive production and Homebrew/tap validation guidance, see
[`docs/release.md`](release.md).

---

## 9. Troubleshooting

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
