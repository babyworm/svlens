#!/bin/bash
# Phase B regression guard for the metrics LHS-concat + for-loop
# fixture. Codifies current TransformExtractor behavior so any
# accidental drop in node/depth/normalization counts is surfaced.
set -euo pipefail

SVLENS_BINARY="$1"
if [ ! -x "$SVLENS_BINARY" ]; then
    echo "FAIL: missing binary: $SVLENS_BINARY" >&2
    exit 1
fi

OUTDIR="$(mktemp -d)"
trap 'rm -rf "$OUTDIR"' EXIT

FIXTURE="tests/sv/metrics/lhs_concat_for_loop.sv"

"$SVLENS_BINARY" metrics "$FIXTURE" --top lhs_concat_for_loop \
    -o "$OUTDIR/r" >/dev/null 2>&1

python3 - <<PY
import json, sys
r = json.load(open("$OUTDIR/r/metrics_report.json"))
roots = r.get("roots", [])
unsupported = r.get("summary", {}).get("unsupported_events", 0)

# Codified expectations:
#   - 3 roots (data_out, hi, lo)
#   - 12 raw transform nodes total
#   - depth >= 3 on the for-loop root
#   - no unsupported events
assert len(roots) == 3, f"FAIL: expected 3 roots, got {len(roots)}"
# Per-root raw_node_count sums double-counts shared fanin nodes (the
# {mask_in, ~mask_in} concat RHS is shared by the hi and lo cones),
# whereas the global "transform nodes extracted" counter de-duplicates.
# We assert on the per-root sum because the report is the canonical
# artifact downstream tooling reads.
total_raw = sum(rt.get("raw_node_count", 0) for rt in roots)
assert total_raw == 14, f"FAIL: expected 14 total raw nodes, got {total_raw}"

depths = [rt.get("logic_depth_est", 0) for rt in roots]
assert max(depths) >= 3, f"FAIL: expected max depth >= 3, got {depths}"

assert unsupported == 0, f"FAIL: unsupported_events={unsupported}, expected 0"

print("PASS: lhs_concat_for_loop metrics within Phase B baseline")
PY
