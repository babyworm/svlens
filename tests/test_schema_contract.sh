#!/bin/bash
set -euo pipefail

SVLENS_BINARY="$1"
if [ ! -x "$SVLENS_BINARY" ]; then
    echo "FAIL: missing binary: $SVLENS_BINARY" >&2
    exit 1
fi

OUTDIR="$(mktemp -d)"
trap 'rm -rf "$OUTDIR"' EXIT

set +e
"$SVLENS_BINARY" conn tests/sv/dangling_output.sv --top dangling_top --format json -o "$OUTDIR/conn"
CONN_EXIT=$?
"$SVLENS_BINARY" cdc --top missing_sync tests/cdc/basic/02_missing_sync.sv --format json -o "$OUTDIR/cdc"
CDC_EXIT=$?
"$SVLENS_BINARY" both tests/cdc/basic/02_missing_sync.sv --top missing_sync -o "$OUTDIR/both" --conn-format json --cdc-format json
BOTH_EXIT=$?
set -e

if [ "$CONN_EXIT" -eq 0 ]; then
  echo "FAIL: expected conn canary to report at least one issue" >&2
  exit 1
fi
if [ "$CDC_EXIT" -ne 1 ]; then
  echo "FAIL: expected CDC canary exit 1, got $CDC_EXIT" >&2
  exit 1
fi
if [ "$BOTH_EXIT" -ne 1 ]; then
    echo "FAIL: expected both canary exit 1, got $BOTH_EXIT" >&2
    exit 1
fi

for doc in docs/schema/connect_report.md docs/schema/cdc_report.md docs/schema/svlens_summary.md; do
    if [ ! -f "$doc" ]; then
        echo "FAIL: missing schema doc $doc" >&2
        exit 1
    fi
done

python3 - "$OUTDIR" <<'PY'
import json
import pathlib
import sys
outdir = pathlib.Path(sys.argv[1])

conn = json.loads((outdir / 'conn' / 'connect_report.json').read_text())
cdc = json.loads((outdir / 'cdc' / 'cdc_report.json').read_text())
both = json.loads((outdir / 'both' / 'svlens_summary.json').read_text())

conn_top = {'version', 'top', 'summary', 'issues', 'analysis', 'connections'}
conn_summary = {'connections_analyzed', 'errors', 'warnings', 'info', 'waived'}
conn_analysis = {'overall_score', 'total_ports', 'total_connections', 'total_issues', 'module_health', 'coupling', 'risks'}
conn_issue = {'type', 'severity', 'port', 'detail'}
conn_connection = {'source', 'dest', 'status'}

cdc_top = {'summary', 'domains', 'crossings'}
cdc_summary = {'violations', 'cautions', 'conventions', 'info', 'waived'}
cdc_domain = {'name', 'source'}
cdc_crossing = {'id', 'source', 'dest', 'source_domain', 'dest_domain', 'path', 'category', 'severity', 'sync_type', 'rule', 'recommendation'}

both_top = {'mode', 'top', 'conn_format', 'cdc_format', 'explicit_output', 'used_filelist', 'conn_exit_code', 'cdc_exit_code', 'exit_code', 'source_file_count', 'conn_status', 'cdc_status', 'filelists', 'source_files', 'outputs', 'reports'}
both_outputs = {'conn', 'cdc', 'conn_dir', 'cdc_dir'}
both_reports = {'connect_report', 'cdc_report'}

assert conn_top.issubset(conn.keys()), conn.keys()
assert conn_summary.issubset(conn['summary'].keys()), conn['summary'].keys()
assert conn_analysis.issubset(conn['analysis'].keys()), conn['analysis'].keys()
assert conn['issues'], 'connect_report.json should contain at least one issue for dangling-output canary'
assert conn_issue.issubset(conn['issues'][0].keys()), conn['issues'][0].keys()
assert isinstance(conn['connections'], list), type(conn['connections'])
if conn['connections']:
    assert conn_connection.issubset(conn['connections'][0].keys()), conn['connections'][0].keys()
assert conn['top'] == 'dangling_top', conn['top']
assert conn['summary']['warnings'] >= 1 or conn['summary']['errors'] >= 1, conn['summary']

assert cdc_top.issubset(cdc.keys()), cdc.keys()
assert cdc_summary.issubset(cdc['summary'].keys()), cdc['summary'].keys()
assert cdc['domains'], 'cdc_report.json should contain at least one domain'
assert cdc_domain.issubset(cdc['domains'][0].keys()), cdc['domains'][0].keys()
assert cdc['crossings'], 'cdc_report.json should contain at least one crossing'
assert cdc_crossing.issubset(cdc['crossings'][0].keys()), cdc['crossings'][0].keys()
assert cdc['summary']['violations'] == 1, cdc['summary']

assert both_top.issubset(both.keys()), both.keys()
assert both_outputs.issubset(both['outputs'].keys()), both['outputs'].keys()
assert both_reports.issubset(both['reports'].keys()), both['reports'].keys()
assert both['mode'] == 'both', both['mode']
assert both['conn_status'] == 'ok', both['conn_status']
assert both['cdc_status'] == 'issues', both['cdc_status']
assert both['used_filelist'] is False, both['used_filelist']
PY

echo "PASS: schema contract"
