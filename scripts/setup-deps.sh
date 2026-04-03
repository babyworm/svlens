#!/bin/bash
#
# setup-deps.sh — Install external dependencies for svlens
#
# Usage:
#   ./scripts/setup-deps.sh [--prefix <install_dir>] [--slang-tag <version>]
#
# Defaults:
#   --prefix     $HOME/.local
#   --slang-tag  v10.0  (default pinned version)
#
set -euo pipefail

PREFIX="${HOME}/.local"
SLANG_TAG="v10.0"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)  PREFIX="$2"; shift 2 ;;
        --slang-tag) SLANG_TAG="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,10p' "$0"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "=== svlens dependency setup ==="
echo "  Install prefix : $PREFIX"
echo "  slang version  : $SLANG_TAG"
echo "  Parallel jobs  : $JOBS"
echo ""

# --- 1. Check prerequisites ---
check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. Please install it first."
        exit 1
    fi
}

check_cmd cmake
check_cmd g++ || check_cmd clang++
check_cmd git

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
    echo "[OK] slang already installed at ${PREFIX}"
    echo "     Config: ${SLANG_CMAKE}"
    echo ""
    echo "To rebuild, remove ${PREFIX}/lib/cmake/slang/ and re-run this script."
    echo ""
    echo "=== Ready to build ==="
    echo "  cmake -B build -DCMAKE_PREFIX_PATH=\"${PREFIX}\""
    echo "  cmake --build build -j${JOBS}"
    exit 0
fi

# --- 3. Build and install slang ---
echo "[INSTALLING] slang ${SLANG_TAG} ..."
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

cd "$WORK_DIR"
git clone --depth 1 --branch "$SLANG_TAG" https://github.com/MikePopoloski/slang.git
cd slang

cmake -B build \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSLANG_INCLUDE_TESTS=OFF \
    -DSLANG_INCLUDE_TOOLS=OFF

cmake --build build -j"$JOBS"
cmake --install build

echo ""
echo "[OK] slang ${SLANG_TAG} installed to ${PREFIX}"
echo ""
echo "=== Ready to build svlens ==="
echo "  cmake -B build -DCMAKE_PREFIX_PATH=\"${PREFIX}\""
echo "  cmake --build build -j${JOBS}"
