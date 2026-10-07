#!/bin/bash
#
# setup-deps.sh — Install external dependencies for svlens
#
# Usage:
#   ./scripts/setup-deps.sh [--prefix <install_dir>] [--slang-tag <version>] [--offline] [--no-mimalloc]
#
# Defaults:
#   --prefix     $HOME/.local
#   --slang-tag  v10.0  (default pinned version)
#   --offline    Do not download slang; validate existing install and print offline build guidance
#   --no-mimalloc  Build / require slang without mimalloc (for ASan); use a dedicated prefix
#
set -euo pipefail

PREFIX="${HOME}/.local"
SLANG_TAG="v10.0"
OFFLINE=0
NO_MIMALLOC=0
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)  PREFIX="$2"; shift 2 ;;
        --slang-tag) SLANG_TAG="$2"; shift 2 ;;
        --offline) OFFLINE=1; shift ;;
        --no-mimalloc) NO_MIMALLOC=1; shift ;;
        -h|--help)
            sed -n '3,12p' "$0"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "=== svlens dependency setup ==="
echo "  Install prefix : $PREFIX"
echo "  slang version  : $SLANG_TAG"
echo "  Offline mode   : $OFFLINE"
echo "  No mimalloc    : $NO_MIMALLOC"
echo "  Parallel jobs  : $JOBS"
echo ""

print_ready_to_build() {
    echo "=== Ready to build ==="
    echo "  Online / fallback-fetch build:"
    echo "    cmake -B build -DCMAKE_PREFIX_PATH=\"${PREFIX}\""
    echo "    cmake --build build -j${JOBS}"
    echo ""
    echo "  Offline / preinstalled dependency build:"
    echo "    cmake -B build-offline -DCMAKE_PREFIX_PATH=\"${PREFIX}\" -DSVLENS_FETCH_DEPS=OFF"
    echo "    cmake --build build-offline -j${JOBS}"
}

# --- 1. Check prerequisites ---
check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. Please install it first."
        exit 1
    fi
}

check_cmd cmake
if command -v g++ &>/dev/null; then
    :
elif command -v clang++ &>/dev/null; then
    :
else
    echo "ERROR: neither 'g++' nor 'clang++' was found. Please install a C++20 compiler first."
    exit 1
fi
# Check C++20 support
CXX="${CXX:-$(command -v g++ || command -v clang++)}"
CXX_VER=$("$CXX" -dumpversion 2>/dev/null || echo "0")
echo "  C++ compiler   : $CXX (version $CXX_VER)"

# Check CMake version
CMAKE_VER=$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')
echo "  CMake version  : $CMAKE_VER"
echo ""

# --- 2. Check if slang is already installed ---
SLANG_CMAKE="${PREFIX}/lib/cmake/slang/slangConfig.cmake"
if [ -f "$SLANG_CMAKE" ]; then
    if [ "$NO_MIMALLOC" -eq 1 ]; then
        SLANG_TARGETS="${PREFIX}/lib/cmake/slang/slangTargets.cmake"
        if [ ! -f "$SLANG_TARGETS" ] || grep -q 'SLANG_USE_MIMALLOC' "$SLANG_TARGETS"; then
            echo "ERROR: existing slang at ${PREFIX} cannot be verified as mimalloc-free."
            echo "Use a fresh dedicated --prefix for --no-mimalloc; the existing install was not modified."
            exit 1
        fi
    fi
    echo "[OK] slang already installed at ${PREFIX}"
    echo "     Config: ${SLANG_CMAKE}"
    echo ""
    echo "To build another allocator configuration, use a separate prefix."
    echo ""
    print_ready_to_build
    exit 0
fi

if [ "$OFFLINE" -eq 1 ]; then
    echo "ERROR: slang is not installed at ${PREFIX}."
    echo "Offline mode cannot download missing dependencies."
    echo ""
    echo "Either:"
    echo "  1) preinstall slang into ${PREFIX}, or"
    echo "  2) rerun without --offline to bootstrap slang,"
    echo "  3) see docs/install.md for preinstalled dependency guidance."
    exit 1
fi

# --- 3. Build and install slang ---
echo "[INSTALLING] slang ${SLANG_TAG} ..."
check_cmd git
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

cd "$WORK_DIR"
git clone --depth 1 --branch "$SLANG_TAG" https://github.com/MikePopoloski/slang.git
cd slang

SLANG_MIMALLOC=ON
if [ "$NO_MIMALLOC" -eq 1 ]; then
    SLANG_MIMALLOC=OFF
fi

cmake -B build \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSLANG_INCLUDE_TESTS=OFF \
    -DSLANG_INCLUDE_TOOLS=OFF \
    -DSLANG_USE_MIMALLOC="$SLANG_MIMALLOC"

cmake --build build -j"$JOBS"
cmake --install build

echo ""
echo "[OK] slang ${SLANG_TAG} installed to ${PREFIX}"
echo ""
print_ready_to_build
