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
# Test 1: missing --top produces error, exit 1
# ============================================================
set +e
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv -o "$OUTDIR/e1" 2>/dev/null
E=$?
set -e
[ "$E" -eq 1 ] || { echo "FAIL: missing --top should exit 1, got $E" >&2; exit 1; }
echo "PASS: missing --top"

# ============================================================
# Test 2: invalid --top module not found
# ============================================================
set +e
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top nonexistent_module -o "$OUTDIR/e2" 2>/dev/null
E=$?
set -e
[ "$E" -eq 1 ] || { echo "FAIL: invalid --top should exit 1, got $E" >&2; exit 1; }
echo "PASS: invalid --top"

# ============================================================
# Test 3: invalid --format value produces error
# ============================================================
set +e
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone --format xml -o "$OUTDIR/e3" 2>/dev/null
E=$?
set -e
[ "$E" -eq 1 ] || { echo "FAIL: invalid --format should exit 1, got $E" >&2; exit 1; }
echo "PASS: invalid --format"

# ============================================================
# Test 4: invalid --roots value produces error
# ============================================================
set +e
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone --roots invalid -o "$OUTDIR/e4" 2>/dev/null
E=$?
set -e
[ "$E" -eq 1 ] || { echo "FAIL: invalid --roots should exit 1, got $E" >&2; exit 1; }
echo "PASS: invalid --roots"

# ============================================================
# Test 5: non-numeric --max-depth handled gracefully
# ============================================================
set +e
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone --max-depth abc -o "$OUTDIR/e5" 2>/dev/null
E=$?
set -e
[ "$E" -ne 0 ] || { echo "FAIL: non-numeric --max-depth should exit nonzero" >&2; exit 1; }
echo "PASS: non-numeric --max-depth"

# ============================================================
# Test 6: --baseline with nonexistent file => no crash, no baseline_diff
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone -o "$OUTDIR/e6" \
  --baseline /nonexistent/path/report.json >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/e6/metrics_report.json'))
assert 'baseline_diff' not in r, 'baseline_diff should be absent for missing baseline file'
" || { echo "FAIL: missing baseline file should not crash" >&2; exit 1; }
echo "PASS: missing baseline file"

# ============================================================
# Test 7: module with no output ports => empty roots, exit 0
# ============================================================
cat > "$OUTDIR/no_ports.sv" << 'SV'
module no_ports (input logic clk);
endmodule
SV
"$SVLENS_BINARY" metrics "$OUTDIR/no_ports.sv" --top no_ports -o "$OUTDIR/e7" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/e7/metrics_report.json'))
assert r['summary']['outputs_analyzed'] == 0, 'no output ports => 0 outputs'
assert len(r['roots']) == 0 or all(root['root_kind'] != 'output' for root in r['roots']), 'no output roots'
" || { echo "FAIL: no-ports module" >&2; exit 1; }
echo "PASS: module with no output ports"

# ============================================================
# Test 8: empty SV file => compilation error, exit 1
# ============================================================
echo "" > "$OUTDIR/empty.sv"
set +e
"$SVLENS_BINARY" metrics "$OUTDIR/empty.sv" --top empty -o "$OUTDIR/e8" 2>/dev/null
E=$?
set -e
[ "$E" -ne 0 ] || { echo "FAIL: empty file should fail" >&2; exit 1; }
echo "PASS: empty SV file"

# ============================================================
# Test 9: combined --topk + --emit-cones + --baseline
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone -o "$OUTDIR/base9" >/dev/null 2>&1
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone -o "$OUTDIR/e9" \
  --topk 1 --emit-cones --baseline "$OUTDIR/base9/metrics_report.json" >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/e9/metrics_report.json'))
assert len(r['roots']) == 1, 'topk 1 should limit to 1 root'
assert 'cone_detail' in r, 'emit-cones should produce cone_detail'
assert 'baseline_diff' in r, 'baseline should produce baseline_diff'
" || { echo "FAIL: combined options" >&2; exit 1; }
echo "PASS: combined --topk + --emit-cones + --baseline"

# ============================================================
# Test 10: --roots outputs only => no ff_d roots
# ============================================================
"$SVLENS_BINARY" metrics tests/sv/metrics/ff_path.sv --top ff_path -o "$OUTDIR/e10" \
  --roots outputs >/dev/null 2>&1
python3 -c "
import json
r = json.load(open('$OUTDIR/e10/metrics_report.json'))
assert all(root['root_kind'] == 'output' for root in r['roots']), 'roots=outputs should only have output roots'
assert r['summary']['ff_d_roots_analyzed'] == 0, 'no ff_d roots with --roots outputs'
" || { echo "FAIL: --roots outputs" >&2; exit 1; }
echo "PASS: --roots outputs filter"

echo ""
echo "PASS: all 10 error/edge case tests"
