#!/bin/bash
set -e
BINARY="${1:-./build/sv-conncheck}"
if [ ! -x "$BINARY" ]; then
    echo "ERROR: $BINARY not found or not executable"
    exit 1
fi
OUTDIR="/tmp/sv-conncheck-integration-test"
rm -rf "$OUTDIR"

echo "=== Integration Test ==="

# Test 1: Clean design exit code 0
$BINARY tests/sv/clean_design.sv --top clean_top --format table -o "$OUTDIR/clean"
EXIT=$?
if [ $EXIT -ne 0 ]; then echo "FAIL: clean exit=$EXIT"; exit 1; fi
echo "PASS: clean design"

# Test 2: Integration design finds issues
$BINARY tests/sv/integration.sv --top integration_top --format all -o "$OUTDIR/integration" || true
if [ ! -f "$OUTDIR/integration/connect_report.json" ]; then echo "FAIL: no JSON"; exit 1; fi
echo "PASS: reports generated"

# Test 3: All output files exist
for f in connect_report.json connect_report.md connection_matrix.csv; do
    if [ ! -f "$OUTDIR/integration/$f" ]; then echo "FAIL: $f missing"; exit 1; fi
done
echo "PASS: all output formats"

# Test 4: Checker disable works
$BINARY tests/sv/integration.sv --top integration_top --no-check-width --no-check-type --no-check-dangling --no-check-undriven --format table -o "$OUTDIR/disabled"
EXIT=$?
if [ $EXIT -ne 0 ]; then echo "FAIL: disabled checkers exit=$EXIT"; exit 1; fi
echo "PASS: checker disable"

echo "=== All integration tests passed ==="
