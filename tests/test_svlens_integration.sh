#!/bin/bash
set -e

SVLENS_BINARY="$1"

if [ ! -x "$SVLENS_BINARY" ]; then
    echo "ERROR: $SVLENS_BINARY not found or not executable"
    exit 1
fi

OUTDIR="/tmp/svlens-integration-test"
OUTDIR="$(mktemp -d "${TMPDIR:-/tmp}/svlens-integration-test.XXXXXX")"
trap 'rm -rf "$OUTDIR"' EXIT

echo "=== svlens Integration Test ==="

# Test 0: svlens help/version work
SVLENS_HELP="$("$SVLENS_BINARY" --help)"
if ! grep -q 'svlens v0.2.7' <<<"$SVLENS_HELP"; then
    echo "FAIL: svlens --help missing banner"
    exit 1
fi
SVLENS_VERSION="$("$SVLENS_BINARY" --version)"
if ! grep -q '^svlens 0\.2\.7$' <<<"$SVLENS_VERSION"; then
    echo "FAIL: svlens --version unexpected: $SVLENS_VERSION"
    exit 1
fi
echo "PASS: svlens help/version"

# Test 1: svlens conn runs and produces expected report
"$SVLENS_BINARY" conn --help >"$OUTDIR/svlens_conn_help.txt"
if ! grep -q 'svlens conn v0.2.7' "$OUTDIR/svlens_conn_help.txt"; then
    echo "FAIL: svlens conn --help missing banner"
    exit 1
fi
"$SVLENS_BINARY" conn --version >"$OUTDIR/svlens_conn_version.txt"
if ! grep -q '^svlens conn 0\.2\.7$' "$OUTDIR/svlens_conn_version.txt"; then
    echo "FAIL: svlens conn --version unexpected"
    exit 1
fi
"$SVLENS_BINARY" conn tests/sv/clean_design.sv --top clean_top --format json -o "$OUTDIR/svlens_conn"
if [ ! -f "$OUTDIR/svlens_conn/connect_report.json" ]; then
    echo "FAIL: svlens conn missing connect_report.json"
    exit 1
fi
if ! grep -q '"top": "clean_top"' "$OUTDIR/svlens_conn/connect_report.json"; then
    echo "FAIL: svlens conn report missing top"
    exit 1
fi
echo "PASS: svlens conn"

# Test 2: svlens cdc forwards to CDC mode
set +e
"$SVLENS_BINARY" cdc --help >"$OUTDIR/svlens_cdc_help.txt"
if ! grep -q 'svlens cdc v0.2.7' "$OUTDIR/svlens_cdc_help.txt"; then
    echo "FAIL: svlens cdc --help missing banner"
    exit 1
fi
"$SVLENS_BINARY" cdc --version >"$OUTDIR/svlens_cdc_version.txt"
if ! grep -q '^svlens cdc 0\.2\.7$' "$OUTDIR/svlens_cdc_version.txt"; then
    echo "FAIL: svlens cdc --version unexpected"
    exit 1
fi
"$SVLENS_BINARY" cdc --top missing_sync tests/cdc/basic/02_missing_sync.sv --format json -o "$OUTDIR/svlens_cdc"
EXIT=$?
set -e
if [ "$EXIT" -ne 1 ]; then
    echo "FAIL: svlens cdc expected exit 1, got $EXIT"
    exit 1
fi
if [ ! -f "$OUTDIR/svlens_cdc/cdc_report.json" ]; then
    echo "FAIL: svlens cdc missing cdc_report.json"
    exit 1
fi
if ! grep -q '"violations": 1' "$OUTDIR/svlens_cdc/cdc_report.json"; then
    echo "FAIL: svlens cdc report unexpected"
    exit 1
fi
echo "PASS: svlens cdc"

# Test 3: svlens both creates partitioned outputs
set +e
"$SVLENS_BINARY" both tests/cdc/basic/02_missing_sync.sv --top missing_sync -o "$OUTDIR/svlens_both" --conn-format json --cdc-format json
EXIT=$?
set -e
if [ "$EXIT" -ne 1 ]; then
    echo "FAIL: svlens both expected exit 1, got $EXIT"
    exit 1
fi
if [ ! -f "$OUTDIR/svlens_both/conn/connect_report.json" ]; then
    echo "FAIL: svlens both missing conn/connect_report.json"
    exit 1
fi
if [ ! -f "$OUTDIR/svlens_both/cdc/cdc_report.json" ]; then
    echo "FAIL: svlens both missing cdc/cdc_report.json"
    exit 1
fi
if [ ! -f "$OUTDIR/svlens_both/svlens_summary.json" ]; then
    echo "FAIL: svlens both missing svlens_summary.json"
    exit 1
fi
if ! grep -q '"top": "missing_sync"' "$OUTDIR/svlens_both/conn/connect_report.json"; then
    echo "FAIL: svlens both conn report missing top"
    exit 1
fi
if ! grep -q '"violations": 1' "$OUTDIR/svlens_both/cdc/cdc_report.json"; then
    echo "FAIL: svlens both cdc report unexpected"
    exit 1
fi
if ! grep -q '"mode": "all"' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing mode"
    exit 1
fi
if ! grep -q '"top": "missing_sync"' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing top"
    exit 1
fi
if ! grep -q '"conn_exit_code": 0' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing conn exit code"
    exit 1
fi
if ! grep -q '"cdc_exit_code": 1' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing cdc exit code"
    exit 1
fi
if ! grep -q '"conn_status": "ok"' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing conn status"
    exit 1
fi
if ! grep -q '"cdc_status": "issues"' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing cdc status"
    exit 1
fi
if ! grep -q '"conn_format": "json"' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing conn format"
    exit 1
fi
if ! grep -q '"cdc_format": "json"' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing cdc format"
    exit 1
fi
if ! grep -q '"explicit_output": true' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing explicit_output flag"
    exit 1
fi
if ! grep -q '"connect_report":' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing connect report path"
    exit 1
fi
if ! grep -q '"cdc_report":' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing cdc report path"
    exit 1
fi
if ! grep -q '"conn_dir":' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing conn_dir"
    exit 1
fi
if ! grep -q '"cdc_dir":' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing cdc_dir"
    exit 1
fi
if ! grep -q '"source_file_count": 1' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing source_file_count"
    exit 1
fi
if ! grep -q '02_missing_sync.sv' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing expanded source file entry"
    exit 1
fi
if ! grep -q '"used_filelist": false' "$OUTDIR/svlens_both/svlens_summary.json"; then
    echo "FAIL: svlens_summary.json missing used_filelist=false"
    exit 1
fi
echo "PASS: svlens both"

# Test 4: svlens both supports shared filelist-driven elaboration
set +e
"$SVLENS_BINARY" both -F tests/cdc/basic/missing_sync.f --top missing_sync -o "$OUTDIR/svlens_both_filelist" --conn-format json --cdc-format json
EXIT=$?
set -e
if [ "$EXIT" -ne 1 ]; then
    echo "FAIL: svlens both -F expected exit 1, got $EXIT"
    exit 1
fi
if [ ! -f "$OUTDIR/svlens_both_filelist/conn/connect_report.json" ]; then
    echo "FAIL: svlens both -F missing conn/connect_report.json"
    exit 1
fi
if [ ! -f "$OUTDIR/svlens_both_filelist/cdc/cdc_report.json" ]; then
    echo "FAIL: svlens both -F missing cdc/cdc_report.json"
    exit 1
fi
if [ ! -f "$OUTDIR/svlens_both_filelist/svlens_summary.json" ]; then
    echo "FAIL: svlens both -F missing svlens_summary.json"
    exit 1
fi
if ! grep -q '"top": "missing_sync"' "$OUTDIR/svlens_both_filelist/svlens_summary.json"; then
    echo "FAIL: svlens both -F summary missing top"
    exit 1
fi
if ! grep -q '"conn_format": "json"' "$OUTDIR/svlens_both_filelist/svlens_summary.json"; then
    echo "FAIL: svlens both -F summary missing conn format"
    exit 1
fi
if ! grep -q '"source_file_count": 1' "$OUTDIR/svlens_both_filelist/svlens_summary.json"; then
    echo "FAIL: svlens both -F summary missing source_file_count"
    exit 1
fi
if ! grep -q '02_missing_sync.sv' "$OUTDIR/svlens_both_filelist/svlens_summary.json"; then
    echo "FAIL: svlens both -F summary missing expanded source file entry"
    exit 1
fi
if ! grep -q '"used_filelist": true' "$OUTDIR/svlens_both_filelist/svlens_summary.json"; then
    echo "FAIL: svlens both -F summary missing used_filelist=true"
    exit 1
fi
if ! grep -q 'missing_sync.f' "$OUTDIR/svlens_both_filelist/svlens_summary.json"; then
    echo "FAIL: svlens both -F summary missing filelist path"
    exit 1
fi
echo "PASS: svlens both filelist"

echo "=== All svlens integration tests passed ==="
