#!/bin/bash
set -e

BINARY="$1"
if [ ! -x "$BINARY" ]; then
    echo "ERROR: $BINARY not found or not executable"
    exit 1
fi

OUTDIR="/tmp/sv-cdc-golden-test"
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"

echo "=== CDC Golden Integration Test ==="

check_fixture() {
    local name="$1"
    local top="$2"
    local sv="tests/cdc/basic/${name}.sv"
    local golden="tests/cdc/golden/${name}.json"
    local out="$OUTDIR/${name}"

    local expected_violations
    local expected_infos
    local expected_crossings
    expected_violations="$(grep -o '"expected_violations":[[:space:]]*[0-9]\+' "$golden" | grep -o '[0-9]\+')"
    expected_infos="$(grep -o '"expected_infos":[[:space:]]*[0-9]\+' "$golden" | grep -o '[0-9]\+')"
    expected_crossings="$(grep -o '"expected_crossings":[[:space:]]*[0-9]\+' "$golden" | grep -o '[0-9]\+')"

    set +e
    "$BINARY" cdc --top "$top" "$sv" --format json -o "$out" >/dev/null 2>&1
    local exit_code=$?
    set -e

    if [ "$exit_code" -ne "$expected_violations" ]; then
        echo "FAIL: ${name} expected exit ${expected_violations}, got ${exit_code}"
        exit 1
    fi

    local actual_violations
    local actual_infos
    local actual_crossings
    actual_violations="$(grep -o '"violations":[[:space:]]*[0-9]\+' "$out/cdc_report.json" | head -1 | grep -o '[0-9]\+')"
    actual_infos="$(grep -o '"info":[[:space:]]*[0-9]\+' "$out/cdc_report.json" | head -1 | grep -o '[0-9]\+')"
    actual_crossings="$(grep -c '"id":' "$out/cdc_report.json" || true)"

    if [ "$actual_violations" -ne "$expected_violations" ]; then
        echo "FAIL: ${name} expected violations ${expected_violations}, got ${actual_violations}"
        exit 1
    fi
    if [ "$actual_infos" -ne "$expected_infos" ]; then
        echo "FAIL: ${name} expected infos ${expected_infos}, got ${actual_infos}"
        exit 1
    fi
    if [ "$actual_crossings" -ne "$expected_crossings" ]; then
        echo "FAIL: ${name} expected crossings ${expected_crossings}, got ${actual_crossings}"
        exit 1
    fi

    echo "PASS: ${name}"
}

check_fixture "01_no_crossing" "single_domain"
check_fixture "02_missing_sync" "missing_sync"
check_fixture "03_two_ff_sync" "two_ff_sync"
check_fixture "04_three_ff_sync" "three_ff_sync"
check_fixture "05_comb_before_sync" "comb_before_sync"

echo "=== All CDC golden tests passed ==="
