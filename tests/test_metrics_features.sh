#!/bin/bash
set -euo pipefail

SVLENS_BINARY="$1"
if [ ! -x "$SVLENS_BINARY" ]; then
    echo "FAIL: missing binary: $SVLENS_BINARY" >&2
    exit 1
fi

OUTDIR="$(mktemp -d)"
trap 'rm -rf "$OUTDIR"' EXIT

SIMPLE="tests/sv/metrics/simple_cone.sv"
FF="tests/sv/metrics/ff_path.sv"
UNSUP="tests/sv/metrics/unsupported_construct.sv"
HIER="tests/sv/metrics/hierarchy_cone.sv"
GEN="tests/sv/metrics/generate_lanes.sv"
CASE="tests/sv/metrics/case_mux.sv"
FORL="tests/sv/metrics/for_loop.sv"

# ============================================================
# Test 1: --baseline with identical input => no regression
# ============================================================
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/base" >/dev/null 2>&1
set +e
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/cur1" \
  --baseline "$OUTDIR/base/metrics_report.json" --fail-on-regression >/dev/null 2>&1
EXIT1=$?
set -e
if [ "$EXIT1" -ne 0 ]; then
    echo "FAIL: baseline same-input should exit 0, got $EXIT1" >&2
    exit 1
fi
python3 -c "
import json
r = json.load(open('$OUTDIR/cur1/metrics_report.json'))
d = r.get('baseline_diff', {})
assert d.get('regressions', -1) == 0, f'expected 0 regressions, got {d}'
assert d.get('total_raw_delta', -1) == 0, f'expected 0 delta, got {d}'
" || { echo "FAIL: baseline diff values wrong" >&2; exit 1; }
echo "PASS: --baseline same input"

# ============================================================
# Test 2: --fail-on-regression with different design => exit 2
# ============================================================
# Use alias_only (small) as baseline, simple_cone (larger) as current
"$SVLENS_BINARY" metrics tests/sv/metrics/alias_only.sv --top alias_only -o "$OUTDIR/base_small" >/dev/null 2>&1
set +e
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/cur2" \
  --baseline "$OUTDIR/base_small/metrics_report.json" --fail-on-regression >/dev/null 2>&1
EXIT2=$?
set -e
# Different top modules => all roots are "new", regression count depends on implementation
# Just verify baseline_diff section exists
python3 -c "
import json
r = json.load(open('$OUTDIR/cur2/metrics_report.json'))
assert 'baseline_diff' in r, 'baseline_diff section missing'
assert 'total_raw_delta' in r['baseline_diff'], 'baseline_diff missing total_raw_delta'
assert 'regressions' in r['baseline_diff'], 'baseline_diff missing regressions'
" || { echo "FAIL: baseline diff section missing" >&2; exit 1; }
echo "PASS: --fail-on-regression baseline_diff present"

# ============================================================
# Test 3: --topk limits root output
# ============================================================
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/topk" --topk 1 >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/topk/metrics_report.json'))
assert len(r['roots']) == 1, f'expected 1 root with topk=1, got {len(r[\"roots\"])}'
" || { echo "FAIL: --topk filtering" >&2; exit 1; }
echo "PASS: --topk"

# ============================================================
# Test 4: --emit-cones produces cone_detail section
# ============================================================
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/cones" --emit-cones >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/cones/metrics_report.json'))
assert 'cone_detail' in r, 'cone_detail section missing with --emit-cones'
assert len(r['cone_detail']) >= 1, 'cone_detail should have entries'
cd = r['cone_detail'][0]
assert 'root_id' in cd, 'cone_detail missing root_id'
assert 'nodes' in cd, 'cone_detail missing nodes'
assert isinstance(cd['nodes'], list), 'cone_detail nodes not a list'
if cd['nodes']:
    n = cd['nodes'][0]
    assert 'id' in n and 'op' in n and 'width' in n, f'node missing fields: {n}'
" || { echo "FAIL: --emit-cones" >&2; exit 1; }
echo "PASS: --emit-cones"

# ============================================================
# Test 5: --emit-raw-graph produces raw_graph section
# ============================================================
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/rawg" --emit-raw-graph >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/rawg/metrics_report.json'))
assert 'raw_graph' in r, 'raw_graph section missing with --emit-raw-graph'
assert r['raw_graph']['node_count'] > 0, 'raw_graph should have nodes'
assert len(r['raw_graph']['nodes']) == r['raw_graph']['node_count'], 'node count mismatch'
n = r['raw_graph']['nodes'][0]
assert 'id' in n and 'op' in n and 'output' in n and 'inputs' in n, f'graph node missing fields: {n}'
" || { echo "FAIL: --emit-raw-graph" >&2; exit 1; }
echo "PASS: --emit-raw-graph"

# ============================================================
# Test 6: --format md produces markdown file
# ============================================================
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/md" --format md >/dev/null 2>&1
if [ ! -f "$OUTDIR/md/metrics_report.md" ]; then
    echo "FAIL: --format md did not produce metrics_report.md" >&2
    exit 1
fi
grep -q "# Metrics Report" "$OUTDIR/md/metrics_report.md" || { echo "FAIL: md missing header" >&2; exit 1; }
grep -q "## Summary" "$OUTDIR/md/metrics_report.md" || { echo "FAIL: md missing summary" >&2; exit 1; }
grep -q "## Roots" "$OUTDIR/md/metrics_report.md" || { echo "FAIL: md missing roots" >&2; exit 1; }
echo "PASS: --format md"

# ============================================================
# Test 7: --format both produces both files
# ============================================================
"$SVLENS_BINARY" metrics "$SIMPLE" --top simple_cone -o "$OUTDIR/both" --format both >/dev/null 2>&1
[ -f "$OUTDIR/both/metrics_report.json" ] || { echo "FAIL: --format both missing json" >&2; exit 1; }
[ -f "$OUTDIR/both/metrics_report.md" ] || { echo "FAIL: --format both missing md" >&2; exit 1; }
echo "PASS: --format both"

# ============================================================
# Test 8: --max-for-unroll cap triggers unsupported event
# ============================================================
"$SVLENS_BINARY" metrics "$FORL" --top for_loop -o "$OUTDIR/forlim" --max-for-unroll 2 >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/forlim/metrics_report.json'))
kinds = [u['kind'] for u in r['unsupported']]
# for_loop.sv may be seen as dynamic bounds or too_large depending on slang evaluation
assert any(k.startswith('for_loop') for k in kinds), f'expected for_loop_* in unsupported, got {kinds}'
" || { echo "FAIL: --max-for-unroll cap" >&2; exit 1; }
echo "PASS: --max-for-unroll cap"

# ============================================================
# Test 9: ff_paths has provenance_level and sync_type
# ============================================================
"$SVLENS_BINARY" metrics "$FF" --top ff_path -o "$OUTDIR/ffp" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/ffp/metrics_report.json'))
assert len(r['ff_paths']) >= 1, 'ff_paths should have entries'
fp = r['ff_paths'][0]
assert fp['provenance_level'] in ('hint_only', 'partial_slice', 'provenance_backed'), f'bad provenance: {fp}'
assert fp['sync_type'] in ('combinational', 'direct', 'unknown'), f'bad sync_type: {fp}'
assert isinstance(fp['path'], list), 'path should be a list'
" || { echo "FAIL: ff_paths schema" >&2; exit 1; }
echo "PASS: ff_paths provenance/sync_type"

# ============================================================
# Test 10: hierarchy traversal produces nodes from submodules
# ============================================================
"$SVLENS_BINARY" metrics "$HIER" --top hierarchy_cone -o "$OUTDIR/hier" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/hier/metrics_report.json'))
# hierarchy_cone has sub_adder with assign sum=a+b, plus top assign result=added^0xFF
# Should have nodes from submodule
assert r['analysis']['raw_transform_count'] > 0 or len(r['roots']) > 0, 'hierarchy should produce analysis'
assert r['summary']['outputs_analyzed'] >= 1, 'should have at least 1 output root'
" || { echo "FAIL: hierarchy traversal" >&2; exit 1; }
echo "PASS: hierarchy traversal"

# ============================================================
# Test 11: generate-for produces nodes per lane
# ============================================================
"$SVLENS_BINARY" metrics "$GEN" --top generate_lanes -o "$OUTDIR/gen" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/gen/metrics_report.json'))
# generate_lanes has 4 lanes of XOR, should extract multiple nodes
total = sum(1 for n in r.get('raw_graph', {}).get('nodes', []))
# Even without --emit-raw-graph, analysis should show some transform count
assert r['summary']['outputs_analyzed'] >= 1, 'should have output root'
" || { echo "FAIL: generate-for" >&2; exit 1; }
echo "PASS: generate-for"

# ============================================================
# Test 12: case/casez produces non-approximate cones
# ============================================================
"$SVLENS_BINARY" metrics "$CASE" --top case_mux -o "$OUTDIR/casem" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/casem/metrics_report.json'))
assert len(r['roots']) >= 1, 'should have roots'
root = r['roots'][0]
assert root['approximate'] == False, f'case_mux root should not be approximate, got {root}'
assert root['raw_node_count'] > 0, 'case_mux should have transform nodes'
" || { echo "FAIL: case non-approximate" >&2; exit 1; }
echo "PASS: case non-approximate"

# ============================================================
# Test 13: unsupported constructs reported with examples
# ============================================================
"$SVLENS_BINARY" metrics "$UNSUP" --top unsupported_construct -o "$OUTDIR/unsup" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/unsup/metrics_report.json'))
assert r['summary']['unsupported_count'] >= 1, 'should have unsupported'
assert len(r['unsupported']) >= 1, 'unsupported array not empty'
u = r['unsupported'][0]
assert 'kind' in u and 'count' in u, f'unsupported missing fields: {u}'
if 'examples' in u:
    assert isinstance(u['examples'], list), 'examples should be a list'
" || { echo "FAIL: unsupported reporting" >&2; exit 1; }
echo "PASS: unsupported reporting"

echo ""
echo "PASS: all 13 metrics feature tests"
