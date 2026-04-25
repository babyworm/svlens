#!/bin/bash
set -euo pipefail

SVLENS_BINARY="$1"

if [ ! -x "$SVLENS_BINARY" ]; then
    echo "FAIL: missing binary: $SVLENS_BINARY" >&2
    exit 1
fi

require_token() {
    local haystack="$1"
    local token="$2"
    local label="$3"
    if ! grep -Fq "$token" <<<"$haystack"; then
        echo "FAIL: missing '$token' in $label" >&2
        exit 1
    fi
}

ROOT_HELP="$($SVLENS_BINARY --help)"
require_token "$ROOT_HELP" "svlens help [conn|cdc|metrics|all]" "root help"
require_token "$ROOT_HELP" "Quick start:" "root help"
require_token "$ROOT_HELP" "Install hint:" "root help"
require_token "$ROOT_HELP" "Docs:" "root help"
require_token "$ROOT_HELP" "Product boundary:" "root help"

CONN_HELP="$($SVLENS_BINARY help conn)"
for token in "Required:" "Common:" "Outputs:" "Examples:" "Exit Codes:" "Limitations:" "Notes:"; do
    require_token "$CONN_HELP" "$token" "conn help"
done
require_token "$CONN_HELP" "docs/schema/connect_report.md" "conn help"

CDC_HELP="$($SVLENS_BINARY help cdc)"
for token in "Required:" "Common:" "Outputs:" "Examples:" "Exit Codes:" "Limitations:" "Notes:"; do
    require_token "$CDC_HELP" "$token" "cdc help"
done
require_token "$CDC_HELP" "docs/schema/cdc_report.md" "cdc help"

METRICS_HELP="$($SVLENS_BINARY help metrics)"
for token in "Required:" "Common:" "Metrics-specific:" "Outputs:" "Examples:" "Exit Codes:" "Limitations:" "Notes:"; do
    require_token "$METRICS_HELP" "$token" "metrics help"
done
require_token "$METRICS_HELP" "docs/schema/metrics_report.md" "metrics help"
require_token "$METRICS_HELP" "svlens metrics v0.3.0" "metrics help"

ALL_HELP="$($SVLENS_BINARY all --help)"
for token in "Required:" "Common:" "Outputs:" "Examples:" "Exit Codes:" "Limitations:" "Notes:"; do
    require_token "$ALL_HELP" "$token" "all help"
done
require_token "$ALL_HELP" "svlens all v0.3.0" "all help"
require_token "$ALL_HELP" "metrics_report.json" "all help"

# Backward compat: 'both' still works as alias
BOTH_ALIAS="$($SVLENS_BINARY help both)"
require_token "$BOTH_ALIAS" "svlens all v0.3.0" "both alias"
require_token "$BOTH_ALIAS" "Exit Codes:" "both alias"

echo "PASS: help contract"
