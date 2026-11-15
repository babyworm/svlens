#!/bin/bash
set -euo pipefail

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: $0 <build-dir> <artifact-name> [output-dir]" >&2
    exit 2
fi

BUILD_DIR="$1"
ARTIFACT_NAME="$2"
OUTPUT_DIR="${3:-dist}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR_ABS="$(cd "$BUILD_DIR" && pwd)"
OUTPUT_DIR_ABS="$ROOT_DIR/$OUTPUT_DIR"
STAGE_DIR="$OUTPUT_DIR_ABS/$ARTIFACT_NAME"
ARCHIVE_TGZ="$OUTPUT_DIR_ABS/${ARTIFACT_NAME}.tar.gz"
ARCHIVE_ZIP="$OUTPUT_DIR_ABS/${ARTIFACT_NAME}.zip"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/docs/schema" "$STAGE_DIR/docs/releases" "$STAGE_DIR/docs/benchmarks"

if [ ! -x "$BUILD_DIR_ABS/svlens" ]; then
    echo "ERROR: built svlens binary not found in $BUILD_DIR_ABS" >&2
    exit 1
fi

cp "$BUILD_DIR_ABS/svlens" "$STAGE_DIR/"
cp "$ROOT_DIR/LICENSE" "$STAGE_DIR/"
cp "$ROOT_DIR/README.md" "$STAGE_DIR/"
cp "$ROOT_DIR/docs/install.md" "$STAGE_DIR/docs/"
cp "$ROOT_DIR/docs/cli-help.md" "$STAGE_DIR/docs/"
cp "$ROOT_DIR/docs/schema/"*.md "$STAGE_DIR/docs/schema/"
cp "$ROOT_DIR/docs/releases/"*.md "$STAGE_DIR/docs/releases/"
cp "$ROOT_DIR/docs/benchmarks/"*.md "$STAGE_DIR/docs/benchmarks/"

# Ship the exact fmt/yaml-cpp runtimes used by the release build. Homebrew
# minor upgrades can add symbols without changing the dylib major name.
mkdir -p "$STAGE_DIR/lib"
if [ "$(uname -s)" = "Darwin" ]; then
    while IFS= read -r dependency; do
        name="$(basename "$dependency")"
        case "$name" in
            libfmt.*.dylib|libyaml-cpp.*.dylib)
                cp -L "$dependency" "$STAGE_DIR/lib/$name"
                cp "$(dirname "$(dirname "$dependency")")/LICENSE" "$STAGE_DIR/lib/$name.LICENSE"
                install_name_tool -change "$dependency" "@loader_path/lib/$name" "$STAGE_DIR/svlens"
                ;;
        esac
    done < <(otool -L "$BUILD_DIR_ABS/svlens" | sed -E '1d;s/^[[:space:]]*//;s/ \(compatibility version.*$//')
    codesign --force --sign - "$STAGE_DIR/svlens"
else
    while read -r name arrow dependency rest; do
        case "$name" in
            libfmt.so*|libyaml-cpp.so*)
                cp -L "$dependency" "$STAGE_DIR/lib/$name"
                case "$name" in
                    libfmt.so*) notice=/usr/share/doc/libfmt-dev/copyright ;;
                    libyaml-cpp.so*) notice=/usr/share/doc/libyaml-cpp-dev/copyright ;;
                esac
                cp "$notice" "$STAGE_DIR/lib/$name.LICENSE"
                ;;
        esac
    done < <(ldd "$BUILD_DIR_ABS/svlens")
    if ldd "$STAGE_DIR/svlens" | grep -E '(libfmt|libyaml-cpp)' | grep -Fv "$STAGE_DIR/lib/"; then
        echo 'ERROR: configure the Linux release build with -DCMAKE_BUILD_RPATH=$ORIGIN/lib' >&2
        exit 1
    fi
fi

rm -f "$ARCHIVE_TGZ" "$ARCHIVE_ZIP"
tar -C "$OUTPUT_DIR_ABS" -czf "$ARCHIVE_TGZ" "$ARTIFACT_NAME"

if command -v zip >/dev/null 2>&1; then
    (
        cd "$OUTPUT_DIR_ABS"
        zip -qr "$ARCHIVE_ZIP" "$ARTIFACT_NAME"
    )
fi

echo "Created release staging directory: $STAGE_DIR"
echo "Created release archive: $ARCHIVE_TGZ"
if [ -f "$ARCHIVE_ZIP" ]; then
    echo "Created release archive: $ARCHIVE_ZIP"
fi
