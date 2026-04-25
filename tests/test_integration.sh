#!/bin/bash
set -e
BINARY="${1:-./build/svlens}"
if [ ! -x "$BINARY" ]; then
    echo "ERROR: $BINARY not found or not executable"
    exit 1
fi
OUTDIR="$(mktemp -d "${TMPDIR:-/tmp}/svlens-conn-integration-test.XXXXXX")"
trap 'rm -rf "$OUTDIR"' EXIT

echo "=== Integration Test ==="

# Test 0: --help works and prints tool banner
HELP_OUT="$($BINARY conn --help)"
if ! grep -q 'svlens conn v' <<<"$HELP_OUT"; then
    echo "FAIL: --help missing version banner"
    exit 1
fi
if ! grep -q 'Usage: svlens conn' <<<"$HELP_OUT"; then
    echo "FAIL: --help missing usage"
    exit 1
fi
echo "PASS: help"

# Test 0b: --version works
VERSION_OUT="$($BINARY conn --version)"
if ! grep -q '^svlens conn 0\.2\.8$' <<<"$VERSION_OUT"; then
    echo "FAIL: --version output unexpected: $VERSION_OUT"
    exit 1
fi
echo "PASS: version"

# Test 1: Clean design exit code 0
$BINARY conn tests/sv/clean_design.sv --top clean_top --format table -o "$OUTDIR/clean"
EXIT=$?
if [ $EXIT -ne 0 ]; then echo "FAIL: clean exit=$EXIT"; exit 1; fi
echo "PASS: clean design"

# Test 2: Integration design finds issues
$BINARY conn tests/sv/integration.sv --top integration_top --format all -o "$OUTDIR/integration" || true
if [ ! -f "$OUTDIR/integration/connect_report.json" ]; then echo "FAIL: no JSON"; exit 1; fi
echo "PASS: reports generated"

# Test 3: All output files exist
for f in connect_report.json connect_report.md connection_matrix.csv; do
    if [ ! -f "$OUTDIR/integration/$f" ]; then echo "FAIL: $f missing"; exit 1; fi
done
echo "PASS: all output formats"

# Test 4: Checker disable works
$BINARY conn tests/sv/integration.sv --top integration_top --no-check-width --no-check-type --no-check-dangling --no-check-undriven --format table -o "$OUTDIR/disabled"
EXIT=$?
if [ $EXIT -ne 0 ]; then echo "FAIL: disabled checkers exit=$EXIT"; exit 1; fi
echo "PASS: checker disable"

# Test 5: ignore-nc suppresses intentional NC ports
$BINARY conn tests/sv/filter_flags.sv --top filter_top --format json -o "$OUTDIR/filter_default" || true
if ! grep -q 'top.u_src.o_nc' "$OUTDIR/filter_default/connect_report.json"; then
    echo "FAIL: default run should report o_nc"
    exit 1
fi
if ! grep -q 'top.u_snk.i_nc' "$OUTDIR/filter_default/connect_report.json"; then
    echo "FAIL: default run should report i_nc"
    exit 1
fi

$BINARY conn tests/sv/filter_flags.sv --top filter_top --ignore-nc --format json -o "$OUTDIR/filter_ignore_nc" || true
if grep -q 'top.u_src.o_nc' "$OUTDIR/filter_ignore_nc/connect_report.json"; then
    echo "FAIL: --ignore-nc should suppress o_nc"
    exit 1
fi
if grep -q 'top.u_snk.i_nc' "$OUTDIR/filter_ignore_nc/connect_report.json"; then
    echo "FAIL: --ignore-nc should suppress i_nc"
    exit 1
fi
echo "PASS: ignore-nc"

# Test 6: custom convention file is loaded and implies convention checking
cat > "$OUTDIR/custom_convention.yaml" <<'EOF'
input_prefix: in_
output_prefix: out_
instance_prefix: inst_
EOF

$BINARY conn tests/sv/clean_design.sv --top clean_top --convention "$OUTDIR/custom_convention.yaml" --format json -o "$OUTDIR/custom_convention" || true
if ! grep -q '"type": "CONVENTION"' "$OUTDIR/custom_convention/connect_report.json"; then
    echo "FAIL: custom convention rules were not applied"
    exit 1
fi
echo "PASS: custom convention"

echo "=== All integration tests passed ==="
