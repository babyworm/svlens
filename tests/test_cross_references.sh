#!/usr/bin/env bash
set -euo pipefail

SVLENS_BINARY="$1"
OUTDIR="$(mktemp -d)"
trap 'rm -rf "$OUTDIR"' EXIT

"$SVLENS_BINARY" all tests/sv/cross_mode_link.sv --top cross_mode_link_top \
    --conn-format json --cdc-format json -o "$OUTDIR" >/dev/null 2>&1 || true

python3 - "$OUTDIR" <<'PY'
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
summary = json.loads((root / "svlens_summary.json").read_text())
conn = json.loads((root / "conn/connect_report.json").read_text())
cdc = json.loads((root / "cdc/cdc_report.json").read_text())
assert conn["issues"], "expected a width issue"
assert cdc["crossings"], "expected a CDC crossing"
refs = summary["cross_references"]
assert summary["cross_reference_status"] == "matched", summary
assert len(refs) == 1, refs
assert refs[0]["conn_issue_index"] == 0, refs
assert refs[0]["matched_signal"] == "cross_mode_link_top.u_prod.q_o", refs
PY

# Re-running into the same directory with non-JSON formats must not correlate
# the JSON reports left behind by the previous run.
"$SVLENS_BINARY" all tests/sv/cross_mode_link.sv --top cross_mode_link_top \
    --conn-format md --cdc-format md -o "$OUTDIR" >/dev/null 2>&1 || true

python3 - "$OUTDIR" <<'PY'
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
assert (root / "conn/connect_report.json").exists(), "stale JSON fixture missing"
summary = json.loads((root / "svlens_summary.json").read_text())
assert summary["cross_reference_status"] == "unavailable", summary
assert summary["cross_references"] == [], summary
PY
