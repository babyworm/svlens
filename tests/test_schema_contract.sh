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
"$SVLENS_BINARY" metrics tests/sv/metrics/simple_cone.sv --top simple_cone -o "$OUTDIR/metrics"
METRICS_EXIT=$?
"$SVLENS_BINARY" metrics tests/sv/metrics/unsupported_construct.sv --top unsupported_construct -o "$OUTDIR/metrics_unsup"
METRICS_UNSUP_EXIT=$?
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
if [ "$METRICS_EXIT" -ne 0 ]; then
    echo "FAIL: expected metrics canary exit 0, got $METRICS_EXIT" >&2
    exit 1
fi

for doc in docs/schema/connect_report.md docs/schema/cdc_report.md docs/schema/svlens_summary.md docs/schema/metrics_report.md; do
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

# --- metrics schema ---
metrics = json.loads((outdir / 'metrics' / 'metrics_report.json').read_text())
metrics_top = {'version', 'top', 'summary', 'analysis', 'roots', 'ff_paths', 'normalization', 'unsupported'}
metrics_summary = {'outputs_analyzed', 'ff_d_roots_analyzed', 'cones_analyzed', 'approximate_cones', 'unsupported_count'}
metrics_analysis = {'raw_transform_count', 'normalized_transform_count', 'repeated_lane_groups', 'ff_to_ff_paths', 'max_output_cone', 'max_ffd_cone'}
metrics_root = {'root_id', 'root_kind', 'raw_node_count', 'logic_depth_est', 'normalized_transform_count', 'repeated_lane_group_count', 'source_inputs', 'source_ffs', 'approximate'}
metrics_norm = {'enabled', 'lane_min_width', 'groups'}

assert metrics_top.issubset(metrics.keys()), f"metrics top keys: {metrics.keys()}"
assert metrics_summary.issubset(metrics['summary'].keys()), f"metrics summary: {metrics['summary'].keys()}"
assert metrics_analysis.issubset(metrics['analysis'].keys()), f"metrics analysis: {metrics['analysis'].keys()}"
assert metrics['version'] == '1.1', f"schema version: {metrics['version']}"
assert 'tool_version' in metrics, 'missing tool_version field'
assert metrics['top'] == 'simple_cone', metrics['top']
assert isinstance(metrics['roots'], list), type(metrics['roots'])
assert len(metrics['roots']) >= 1, 'metrics should have at least one root'
assert metrics_root.issubset(metrics['roots'][0].keys()), f"root keys: {metrics['roots'][0].keys()}"
assert isinstance(metrics['ff_paths'], list), type(metrics['ff_paths'])
assert metrics_norm.issubset(metrics['normalization'].keys()), f"norm keys: {metrics['normalization'].keys()}"
assert isinstance(metrics['unsupported'], list), type(metrics['unsupported'])
assert metrics['summary']['outputs_analyzed'] >= 1, metrics['summary']

# --- metrics unsupported schema ---
m_unsup = json.loads((outdir / 'metrics_unsup' / 'metrics_report.json').read_text())
assert m_unsup['summary']['unsupported_count'] >= 1, f"unsupported_count should be >= 1, got {m_unsup['summary']['unsupported_count']}"
assert len(m_unsup['unsupported']) >= 1, 'unsupported[] should not be empty for unsupported_construct fixture'
unsup_item = m_unsup['unsupported'][0]
assert 'kind' in unsup_item, f"unsupported item missing 'kind': {unsup_item}"
assert 'count' in unsup_item, f"unsupported item missing 'count': {unsup_item}"
assert unsup_item['count'] >= 1, f"unsupported count should be >= 1: {unsup_item}"
PY

echo "PASS: schema contract"
