#!/bin/bash
set -euo pipefail

SVLENS_BINARY="$1"
if [ ! -x "$SVLENS_BINARY" ]; then
    echo "FAIL: missing binary: $SVLENS_BINARY" >&2
    exit 1
fi

OUTDIR1="$(mktemp -d)"
OUTDIR2="$(mktemp -d)"
trap 'rm -rf "$OUTDIR1" "$OUTDIR2"' EXIT

# Run metrics twice on each fixture and compare JSON output
for fixture in tests/sv/metrics/simple_cone.sv tests/sv/metrics/repeated_lanes.sv tests/sv/metrics/alias_only.sv tests/sv/metrics/ff_path.sv tests/sv/metrics/case_mux.sv tests/sv/metrics/mux_heavy.sv tests/sv/metrics/ff_deep_pipeline.sv tests/sv/metrics/slice_concat.sv tests/sv/metrics/hierarchy_cone.sv tests/sv/metrics/generate_lanes.sv tests/sv/metrics/unsupported_construct.sv; do
    BASENAME="$(basename "$fixture" .sv)"
    TOP="$BASENAME"

    "$SVLENS_BINARY" metrics "$fixture" --top "$TOP" -o "$OUTDIR1/$BASENAME" >/dev/null 2>&1
    "$SVLENS_BINARY" metrics "$fixture" --top "$TOP" -o "$OUTDIR2/$BASENAME" >/dev/null 2>&1

    if ! diff -q "$OUTDIR1/$BASENAME/metrics_report.json" "$OUTDIR2/$BASENAME/metrics_report.json" >/dev/null 2>&1; then
        echo "FAIL: determinism check failed for $fixture" >&2
        diff "$OUTDIR1/$BASENAME/metrics_report.json" "$OUTDIR2/$BASENAME/metrics_report.json" >&2
        exit 1
    fi
done

echo "PASS: metrics determinism"
