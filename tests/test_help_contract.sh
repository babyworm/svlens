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
require_token "$ROOT_HELP" "svlens help [conn|cdc|both]" "root help"
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

BOTH_HELP="$($SVLENS_BINARY both --help)"
for token in "Required:" "Common:" "Outputs:" "Examples:" "Exit Codes:" "Limitations:" "Notes:"; do
    require_token "$BOTH_HELP" "$token" "both help"
done
require_token "$BOTH_HELP" "svlens_summary.json" "both help"

ALIAS_BOTH_HELP="$($SVLENS_BINARY help both)"
require_token "$ALIAS_BOTH_HELP" "svlens both v0.2.3" "help alias both"
require_token "$ALIAS_BOTH_HELP" "Exit Codes:" "help alias both"

echo "PASS: help contract"
