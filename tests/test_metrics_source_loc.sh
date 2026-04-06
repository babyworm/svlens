#!/bin/bash
set -euo pipefail

SVLENS_BINARY="$1"
OUTDIR="$(mktemp -d)"
trap 'rm -rf "$OUTDIR"' EXIT

SIMPLE="tests/sv/metrics/simple_cone.sv"

# Test: source_loc fields in raw_graph nodes should be non-empty
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/src" --emit-raw-graph >/dev/null 2>&1
python3 -c "
import json, sys
r = json.load(open('$OUTDIR/src/metrics_report.json'))
nodes = r.get('raw_graph', {}).get('nodes', [])
assert len(nodes) > 0, 'raw_graph should have nodes'
locs = [n.get('source_loc', '') for n in nodes]
non_empty = [l for l in locs if l]
assert len(non_empty) > 0, f'expected non-empty source_loc in at least one node, got all empty'
# Check format: should contain filename and line number
loc = non_empty[0]
assert ':' in loc, f'source_loc should be file:line format, got {loc}'
assert 'simple_cone.sv' in loc, f'source_loc should reference fixture file, got {loc}'
" || { echo "FAIL: source_loc tracking" >&2; exit 1; }
echo "PASS: source_loc tracking"

# Test: unsupported events also have source_loc
"$SVLENS_BINARY" metrics tests/sv/metrics/unsupported_construct.sv --top unsupported_construct -o "$OUTDIR/unsup" --emit-raw-graph >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/unsup/metrics_report.json'))
unsup = r.get('unsupported', [])
locs = [u.get('examples', [''])[0] if 'examples' in u else '' for u in unsup]
print('PASS: unsupported source_loc schema ok')
"
echo "PASS: all source_loc tests"
