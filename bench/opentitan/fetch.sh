#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OT_DIR="$SCRIPT_DIR/.ot-src"
CONFIG="$SCRIPT_DIR/targets.yaml"

# Parse tag and repo from targets.yaml (no external deps)
OT_TAG=$(grep 'opentitan_tag:' "$CONFIG" | sed 's/.*: *"\(.*\)"/\1/')
OT_REPO=$(grep 'opentitan_repo:' "$CONFIG" | sed 's/.*: *"\(.*\)"/\1/')

echo "=== OpenTitan Fetch ==="
echo "Tag:  $OT_TAG"
echo "Repo: $OT_REPO"
echo "Dir:  $OT_DIR"

if [ -d "$OT_DIR/.git" ]; then
    CURRENT_TAG=$(git -C "$OT_DIR" describe --tags --exact-match 2>/dev/null || echo "none")
    if [ "$CURRENT_TAG" = "$OT_TAG" ]; then
        echo "Already at $OT_TAG — skipping clone."
        exit 0
    fi
    echo "Existing clone at '$CURRENT_TAG', switching to $OT_TAG..."
    git -C "$OT_DIR" fetch --depth 1 origin tag "$OT_TAG"
    git -C "$OT_DIR" checkout "$OT_TAG"
    echo "Switched to $OT_TAG."
    exit 0
fi

echo "Cloning OpenTitan (shallow, tag only)..."
git clone --depth 1 --branch "$OT_TAG" "$OT_REPO" "$OT_DIR"
echo "Clone complete."
