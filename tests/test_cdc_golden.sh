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
    local sdc="tests/cdc/basic/${name}.sdc"
    local golden="tests/cdc/golden/${name}.json"
    local out="$OUTDIR/${name}"

    local expected_violations
    local expected_infos
    local expected_crossings
    expected_violations="$(grep -o '"expected_violations":[[:space:]]*[0-9]\+' "$golden" | grep -o '[0-9]\+')"
    expected_infos="$(grep -o '"expected_infos":[[:space:]]*[0-9]\+' "$golden" | grep -o '[0-9]\+')"
    expected_crossings="$(grep -o '"expected_crossings":[[:space:]]*[0-9]\+' "$golden" | grep -o '[0-9]\+')"

    # Optional SDC companion: tests/cdc/basic/<name>.sdc is passed via --sdc
    # when present.
    local -a sdc_args=()
    if [ -f "$sdc" ]; then
        sdc_args=(--sdc "$sdc")
    fi

    # Optional flags sidecar: tests/cdc/basic/<name>.flags is read into
    # an array of additional arguments. One whitespace-separated token
    # per line; lines starting with `#` are comments. Lets fixtures
    # opt into rule-specific runner flags (e.g. --glitch-free-mux-cell)
    # without bloating every fixture invocation.
    local flags_file="tests/cdc/basic/${name}.flags"
    local -a extra_flags=()
    if [ -f "$flags_file" ]; then
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            case "$line" in \#*) continue ;; esac
            for token in $line; do
                extra_flags+=("$token")
            done
        done < "$flags_file"
    fi

    set +e
    "$BINARY" cdc --top "$top" "$sv" "${sdc_args[@]}" "${extra_flags[@]}" \
        --format json -o "$out" >/dev/null 2>&1
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
check_fixture "06_submodule_sync" "submodule_sync"
check_fixture "07_handshake_req_ack" "handshake_req_ack"
check_fixture "08_gray_fifo_ptr" "gray_fifo_ptr"
check_fixture "09_pulse_sync" "pulse_sync"
check_fixture "10_naming_no_false_clock" "naming_no_false_clock"
check_fixture "11_sdc_async_groups" "sdc_async_groups"
check_fixture "12_multi_crossing_mixed" "multi_crossing_mixed"
check_fixture "13_three_domain_chain" "three_domain_chain"
check_fixture "14_single_ff_only" "single_ff_only"
check_fixture "15_bus_cdc_no_gray" "bus_cdc_no_gray"
check_fixture "16_comb_between_domains" "comb_between_domains"
check_fixture "17_reset_synchronizer" "reset_synchronizer"
check_fixture "18_internal_reset_cdc" "internal_reset_cdc"
check_fixture "19_missing_reset_sync" "missing_reset_sync"
check_fixture "20_fanout_mixed_sync" "fanout_mixed_sync"
check_fixture "21_clock_mux" "clock_mux"
check_fixture "22_two_level_submodule_sync" "two_level_submodule_sync"
check_fixture "23_packed_array_indexed" "packed_array_indexed"
check_fixture "24_genfor_module_sync" "genfor_module_sync"
check_fixture "25_nested_sync_clock_inherit" "nested_sync_clock_inherit"
check_fixture "26_genfor_in_wrapper_clock_inherit" "genfor_in_wrapper_clock_inherit"
check_fixture "27_neg_ac_cdc01_proper_sync" "neg_ac_cdc01_proper_sync"
check_fixture "28_neg_ac_cdc02_clean_path" "neg_ac_cdc02_clean_path"
check_fixture "29_neg_ac_cdc03_distinct_pairs" "neg_ac_cdc03_distinct_pairs"
check_fixture "30_neg_ac_cdc06_synced_reset" "neg_ac_cdc06_synced_reset"
check_fixture "31_parameter_type_genfor_sync" "parameter_type_genfor_sync"
check_fixture "32_neg_ac_cdc04_single_bit" "neg_ac_cdc04_single_bit"
check_fixture "33_always_comb_sync_chain" "always_comb_sync_chain"
check_fixture "34_neg_ac_cdc05_safe_mux_cell" "neg_ac_cdc05_safe_mux_cell"
check_fixture "35_neg_ac_cdc05_sdc_clock_mux" "neg_ac_cdc05_sdc_clock_mux"

echo "=== All CDC golden tests passed ==="
