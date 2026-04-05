#!/bin/bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <built-svlens-binary> <cmake-build-dir>" >&2
    exit 2
fi

BINARY="$1"
BUILD_DIR="$2"
BUILD_DIR_ABS="$(cd "$BUILD_DIR" && pwd)"
ROOT_DIR="$(pwd)"

if [ ! -x "$BINARY" ]; then
    echo "FAIL: built binary not executable: $BINARY" >&2
    exit 1
fi

if [ ! -f "$BUILD_DIR_ABS/cmake_install.cmake" ]; then
    echo "FAIL: missing cmake_install.cmake in build dir: $BUILD_DIR_ABS" >&2
    exit 1
fi

CACHE_FILE="$BUILD_DIR_ABS/CMakeCache.txt"
if [ ! -f "$CACHE_FILE" ]; then
    echo "FAIL: missing CMakeCache.txt in build dir: $BUILD_DIR_ABS" >&2
    exit 1
fi

TMP_ROOT="$(mktemp -d)"
MAKE_PREFIX="$TMP_ROOT/make-prefix"
CMAKE_PREFIX="$TMP_ROOT/cmake-prefix"
BUILD_LINK_CREATED=0
MAKE_BUILD_DIR="$BUILD_DIR_ABS"
SKIP_MAKE_INSTALL=0

cleanup() {
    if [ "$BUILD_LINK_CREATED" -eq 1 ]; then
        rm -f "$ROOT_DIR/build"
    fi
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

mkdir -p "$MAKE_PREFIX" "$CMAKE_PREFIX"

cmake --install "$BUILD_DIR_ABS" --prefix "$CMAKE_PREFIX"

CMAKE_INSTALLED="$CMAKE_PREFIX/bin/svlens"
if [ ! -x "$CMAKE_INSTALLED" ]; then
    echo "FAIL: installed binary missing after cmake --install: $CMAKE_INSTALLED" >&2
    exit 1
fi

if [ "$(basename "$BUILD_DIR_ABS")" != "build" ]; then
    if [ -e "$ROOT_DIR/build" ]; then
        SKIP_MAKE_INSTALL=1
    else
        ln -s "$BUILD_DIR_ABS" "$ROOT_DIR/build"
        BUILD_LINK_CREATED=1
        MAKE_BUILD_DIR="$ROOT_DIR/build"
    fi
fi

MAKE_INSTALLED=""
if [ "$SKIP_MAKE_INSTALL" -eq 0 ]; then
    MAKE_CACHE_FILE="$MAKE_BUILD_DIR/CMakeCache.txt"
    MAKE_CMAKE_PREFIX_PATH_VALUE="$(sed -n -E 's/^CMAKE_PREFIX_PATH(:[^=]+)?=(.*)$/\2/p' "$MAKE_CACHE_FILE" | head -n 1)"

    env CMAKE_PREFIX_PATH="$MAKE_CMAKE_PREFIX_PATH_VALUE" PREFIX="$MAKE_PREFIX" make install >/dev/null

    MAKE_INSTALLED="$MAKE_PREFIX/bin/svlens"
    if [ ! -x "$MAKE_INSTALLED" ]; then
        echo "FAIL: installed binary missing after make install: $MAKE_INSTALLED" >&2
        exit 1
    fi
else
    echo "INFO: skipping make install check because repository root already has a separate build/ tree"
fi

verify_help() {
    local binary="$1"
    local help_out
    help_out="$("$binary" --help)"
    if ! grep -q 'svlens' <<<"$help_out" || \
       ! grep -q 'conn' <<<"$help_out" || \
       ! grep -q 'cdc' <<<"$help_out" || \
       ! grep -q 'both' <<<"$help_out"; then
        echo "FAIL: installed binary help output missing expected command summary: $binary" >&2
        exit 1
    fi
}

verify_help "$CMAKE_INSTALLED"
if [ -n "$MAKE_INSTALLED" ]; then
    verify_help "$MAKE_INSTALLED"
fi

echo "PASS: install smoke"
