#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SVLENS="$REPO_ROOT/build/svlens"
RESULTS_DIR="$SCRIPT_DIR/results"
FILELIST_DIR="$SCRIPT_DIR/filelists"
CONFIG="$SCRIPT_DIR/targets.yaml"

if [ ! -x "$SVLENS" ]; then
    echo "ERROR: svlens not found at $SVLENS. Run 'make build' first." >&2
    exit 1
fi

# Parse targets using Python (needs PyYAML anyway for gen_filelist)
parse_targets() {
    python3 -c "
import yaml, json, sys
with open('$CONFIG') as f:
    data = yaml.safe_load(f)
for t in data['targets']:
    print(json.dumps(t))
"
}

mkdir -p "$RESULTS_DIR"

echo "=== svlens OpenTitan Benchmark ==="
echo "Binary: $SVLENS"
echo ""

parse_targets | while IFS= read -r target_json; do
    NAME=$(echo "$target_json" | python3 -c "import json,sys; print(json.load(sys.stdin)['name'])")
    TOP=$(echo "$target_json" | python3 -c "import json,sys; print(json.load(sys.stdin)['top_module'])")
    TIMEOUT=$(echo "$target_json" | python3 -c "import json,sys; print(json.load(sys.stdin)['timeout_sec'])")
    LEVEL=$(echo "$target_json" | python3 -c "import json,sys; print(json.load(sys.stdin)['level'])")
    FILELIST="$FILELIST_DIR/${NAME}.f"

    echo "--- [$LEVEL] $NAME (top=$TOP, timeout=${TIMEOUT}s) ---"

    if [ ! -f "$FILELIST" ]; then
        echo "  SKIP: filelist not found at $FILELIST"
        continue
    fi

    TARGET_DIR="$RESULTS_DIR/$NAME"
    mkdir -p "$TARGET_DIR/conn" "$TARGET_DIR/cdc"

    METRICS="{\"name\":\"$NAME\",\"level\":\"$LEVEL\",\"top\":\"$TOP\""

    # Run conn mode
    echo "  Running conn..."
    CONN_START=$(date +%s%N)
    set +e
    /usr/bin/time -v timeout "$TIMEOUT" \
        "$SVLENS" conn -F "$FILELIST" --top "$TOP" --format json -o "$TARGET_DIR/conn" \
        > "$TARGET_DIR/conn_stdout.log" 2> "$TARGET_DIR/conn_stderr.log"
    CONN_EXIT=$?
    set -e
    CONN_END=$(date +%s%N)
    CONN_MS=$(( (CONN_END - CONN_START) / 1000000 ))
    CONN_RSS=$(grep "Maximum resident" "$TARGET_DIR/conn_stderr.log" 2>/dev/null | awk '{print $NF}' || echo "0")

    METRICS="$METRICS,\"conn_exit\":$CONN_EXIT,\"conn_ms\":$CONN_MS,\"conn_rss_kb\":$CONN_RSS"
    echo "  conn: exit=$CONN_EXIT time=${CONN_MS}ms rss=${CONN_RSS}KB"

    # Run cdc mode
    echo "  Running cdc..."
    CDC_START=$(date +%s%N)
    set +e
    /usr/bin/time -v timeout "$TIMEOUT" \
        "$SVLENS" cdc -F "$FILELIST" --top "$TOP" --format json -o "$TARGET_DIR/cdc" \
        > "$TARGET_DIR/cdc_stdout.log" 2> "$TARGET_DIR/cdc_stderr.log"
    CDC_EXIT=$?
    set -e
    CDC_END=$(date +%s%N)
    CDC_MS=$(( (CDC_END - CDC_START) / 1000000 ))
    CDC_RSS=$(grep "Maximum resident" "$TARGET_DIR/cdc_stderr.log" 2>/dev/null | awk '{print $NF}' || echo "0")

    METRICS="$METRICS,\"cdc_exit\":$CDC_EXIT,\"cdc_ms\":$CDC_MS,\"cdc_rss_kb\":$CDC_RSS}"
    echo "  cdc:  exit=$CDC_EXIT time=${CDC_MS}ms rss=${CDC_RSS}KB"

    echo "$METRICS" > "$TARGET_DIR/metrics.json"
    echo ""
done

echo "=== Benchmark Complete ==="
echo "Results in: $RESULTS_DIR/"
