#!/usr/bin/env bash
set -euo pipefail

SVLENS_BINARY="$1"
OUTDIR="$(mktemp -d)"
trap 'rm -rf "$OUTDIR"' EXIT

"$SVLENS_BINARY" conn tests/sv/user_rules.sv --top user_rules_top \
    --user-rules tests/user_rules.yaml --format json -o "$OUTDIR" >/dev/null 2>&1 || true

python3 - "$OUTDIR/connect_report.json" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1]))
issues = {issue.get("rule_id"): issue for issue in report["issues"]}
assert issues["USR-001"]["severity"] == "ERROR", issues
assert issues["USR-002"]["severity"] == "WARN", issues
assert "tmp_data" in issues["USR-002"]["port"], issues
assert sum(issue.get("rule_id") == "USR-001" for issue in report["issues"]) == 2, report["issues"]
PY

"$SVLENS_BINARY" conn tests/sv/user_rules.sv --top user_rules_top \
    --user-rules tests/user_rules.yaml --waiver tests/user_rules_waiver.yaml \
    --format json -o "$OUTDIR/waived" >/dev/null 2>&1 || true

python3 - "$OUTDIR/waived/connect_report.json" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1]))
assert report["summary"]["waived"] == 2, report["summary"]
assert {issue.get("rule_id") for issue in report["issues"]} == {"USR-002"}
PY

if "$SVLENS_BINARY" conn tests/sv/user_rules.sv --top user_rules_top \
    --user-rules tests/user_rules_invalid.yaml --format json \
    -o "$OUTDIR/invalid" >"$OUTDIR/invalid_stdout.log" 2>"$OUTDIR/invalid_stderr.log"; then
    echo "FAIL: invalid user checker regex was accepted" >&2
    exit 1
fi
if ! grep -q 'invalid user checker regex' "$OUTDIR/invalid_stderr.log"; then
    echo "FAIL: invalid regex error was not reported" >&2
    exit 1
fi

"$SVLENS_BINARY" conn tests/sv/user_rules_inline.sv --top user_rules_inline_top \
    --user-rules tests/user_rules.yaml --format json \
    -o "$OUTDIR/inline" >/dev/null 2>&1 || true

python3 - "$OUTDIR/inline/connect_report.json" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1]))
assert report["summary"]["waived"] == 1, report["summary"]
assert {issue.get("rule_id") for issue in report["issues"]} == {"USR-001"}
PY

"$SVLENS_BINARY" conn tests/sv/inline_width_waiver.sv --top inline_width_top \
    --format json -o "$OUTDIR/inline_width" >/dev/null 2>&1 || true

python3 - "$OUTDIR/inline_width/connect_report.json" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1]))
assert report["summary"]["waived"] == 1, report["summary"]
assert not any(issue["type"] == "WIDTH_MISMATCH" for issue in report["issues"])
PY
