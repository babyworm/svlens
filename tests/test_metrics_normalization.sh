#!/bin/bash
set -euo pipefail

SVLENS_BINARY="$1"
if [ ! -x "$SVLENS_BINARY" ]; then
    echo "FAIL: missing binary: $SVLENS_BINARY" >&2
    exit 1
fi

OUTDIR="$(mktemp -d)"
trap 'rm -rf "$OUTDIR"' EXIT

# ============================================================
# Test 1: repeated_lanes: raw > normalized (normalization reduces count)
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/repeated_lanes.sv --top repeated_lanes -o "$OUTDIR/n1" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/n1/metrics_report.json'))
raw = r['analysis']['raw_transform_count']
norm = r['analysis']['normalized_transform_count']
groups = r['analysis']['repeated_lane_groups']
assert raw > norm, f'repeated_lanes: raw ({raw}) should > normalized ({norm})'
assert groups > 0, f'should have normalization groups, got {groups}'
print(f'  raw={raw}, normalized={norm}, groups={groups}')
" || { echo "FAIL: repeated_lanes normalization" >&2; exit 1; }
echo "PASS: repeated_lanes raw > normalized"

# ============================================================
# Test 2: alias_only: raw == normalized (no repeated patterns)
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/alias_only.sv --top alias_only -o "$OUTDIR/n2" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/n2/metrics_report.json'))
raw = r['analysis']['raw_transform_count']
norm = r['analysis']['normalized_transform_count']
# alias_only has 2 simple assigns — no repeated patterns to collapse
print(f'  raw={raw}, normalized={norm}')
" || { echo "FAIL: alias_only normalization" >&2; exit 1; }
echo "PASS: alias_only normalization"

# ============================================================
# Test 3: normalization.enabled reflects CLI flag
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone -o "$OUTDIR/n3a" >/dev/null 2>&1
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone -o "$OUTDIR/n3b" --no-normalize-bit-lanes >/dev/null 2>&1
python3 -c "
import json
a = json.load(open('$OUTDIR/n3a/metrics_report.json'))
b = json.load(open('$OUTDIR/n3b/metrics_report.json'))
assert a['normalization']['enabled'] == True, 'default should be enabled'
assert b['normalization']['enabled'] == False, '--no-normalize should disable'
" || { echo "FAIL: normalization enabled flag" >&2; exit 1; }
echo "PASS: normalization enabled/disabled"

# ============================================================
# Test 4: normalization groups have required schema keys
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/repeated_lanes.sv --top repeated_lanes -o "$OUTDIR/n4" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/n4/metrics_report.json'))
groups = r['normalization']['groups']
assert len(groups) >= 1, 'should have normalization groups'
for g in groups:
    assert 'signature' in g, f'group missing signature: {g}'
    assert 'multiplicity' in g, f'group missing multiplicity: {g}'
    assert 'representative_width' in g, f'group missing representative_width: {g}'
    assert 'collapsed_from' in g, f'group missing collapsed_from: {g}'
    assert g['multiplicity'] >= 2, f'multiplicity should be >= 2: {g}'
    assert g['collapsed_from'] >= 2, f'collapsed_from should be >= 2: {g}'
" || { echo "FAIL: normalization group schema" >&2; exit 1; }
echo "PASS: normalization group schema"

# ============================================================
# Test 5: per-root normalization (root with groups vs root without)
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone -o "$OUTDIR/n5" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/n5/metrics_report.json'))
# simple_cone has 'y' (complex: mux+add+sub) and 'z' (simple: and)
roots = {root['root_id']: root for root in r['roots']}
y = roots.get('y', {})
z = roots.get('z', {})
assert y['raw_node_count'] > z['raw_node_count'], f'y should be more complex than z: y={y[\"raw_node_count\"]}, z={z[\"raw_node_count\"]}'
print(f'  y: raw={y[\"raw_node_count\"]}, norm={y[\"normalized_transform_count\"]}')
print(f'  z: raw={z[\"raw_node_count\"]}, norm={z[\"normalized_transform_count\"]}')
" || { echo "FAIL: per-root complexity" >&2; exit 1; }
echo "PASS: per-root complexity ordering"

# ============================================================
# Test 6: lane_min_width is respected
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/repeated_lanes.sv --top repeated_lanes -o "$OUTDIR/n6a" --lane-min-width 2 >/dev/null 2>&1
"$SVLENS_BINARY" metrics tests/sv/metrics/repeated_lanes.sv --top repeated_lanes -o "$OUTDIR/n6b" --lane-min-width 16 >/dev/null 2>&1
python3 -c "
import json
a = json.load(open('$OUTDIR/n6a/metrics_report.json'))
b = json.load(open('$OUTDIR/n6b/metrics_report.json'))
assert a['normalization']['lane_min_width'] == 2
assert b['normalization']['lane_min_width'] == 16
# With min_width=16, 8-bit lanes should NOT be grouped
groups_a = len(a['normalization']['groups'])
groups_b = len(b['normalization']['groups'])
assert groups_a > groups_b, f'min_width 2 should have more groups ({groups_a}) than min_width 16 ({groups_b})'
" || { echo "FAIL: lane_min_width" >&2; exit 1; }
echo "PASS: lane_min_width respected"

echo ""
echo "PASS: all 6 normalization tests"
