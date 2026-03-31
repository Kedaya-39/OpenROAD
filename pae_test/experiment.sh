#!/bin/bash
# experiment.sh - Master script for PAE experiments
# Usage: ./experiment.sh <bin_path> <benchmark_root> <cases_file> <rounds> <val_interval> [init_report_path]
#
# Example cases_file format:
# TEST:case_A,case_B,case_C
# VAL:case_D,case_E

set -e

# Check arguments
if [[ $# -lt 5 ]]; then
    echo "Usage: $0 <bin_path> <benchmark_root> <cases_file> <rounds> <val_interval> [init_report_path]"
    exit 1
fi

BIN_PATH=$(realpath -s "$1")
BENCH_ROOT=$(realpath -s "$2")
CASES_FILE=$(realpath -s "$3")
ROUNDS=$4
VAL_INTERVAL=$5

if [[ "$VAL_INTERVAL" -le 0 ]]; then
    echo "Warning: VAL_INTERVAL is set to ${VAL_INTERVAL}. Validation will be SKIPPED during Test rounds."
fi

# Parse optional initial report path
INIT_REPORT=""
if [[ -n "$6" ]]; then
    INIT_REPORT=$(realpath -s "$6")
    # Fail fast if provided initial report does not exist
    if [[ ! -f "$INIT_REPORT" ]]; then
        echo "Error: Initial report file not found at $INIT_REPORT"
        exit 1
    fi
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
COLLECT_SCRIPT="${SCRIPT_DIR}/collect_results.py"

# Parse case lists from config file
TEST_CASES=$(grep "^TEST:" "$CASES_FILE" | cut -d':' -f2 | tr ',' ' ' || echo "")
VAL_CASES=$(grep "^VAL:" "$CASES_FILE" | cut -d':' -f2 | tr ',' ' ' || echo "")

if [[ -z "$TEST_CASES" && -z "$VAL_CASES" ]]; then
    echo "Error: Both TEST and VAL cases are empty in $CASES_FILE"
    exit 1
fi

# 1. Create a unique root directory for this experiment run
EXP_TIMESTAMP=$(date +%Y%m%d_%H%M%S)
EXP_ROOT_DIR="$(pwd)/pae_run_${EXP_TIMESTAMP}"
mkdir -p "$EXP_ROOT_DIR"
SUMMARY_CSV="${EXP_ROOT_DIR}/summary_results.csv"

echo "=== PAE Experiment Started ==="
echo "Experiment Root: $EXP_ROOT_DIR"
echo "Test Cases:      ${TEST_CASES:-None}"
echo "Val Cases:       ${VAL_CASES:-None}"
echo "Rounds:          $ROUNDS"
echo "Val Interval:    $VAL_INTERVAL"
echo "------------------------------"

# Move into the root directory so all outputs are contained
cd "$EXP_ROOT_DIR"

# 2. Initialize cumulative report
CUMULATIVE_REPORT="${EXP_ROOT_DIR}/cumulative_pae.report"
if [[ -n "$INIT_REPORT" ]]; then
    # Copy initial report to start accumulating from previous knowledge
    cp "$INIT_REPORT" "$CUMULATIVE_REPORT"
    echo "Initialized cumulative report from provided initial report."
fi
echo "------------------------------"

# --- Baseline Phase (Round 0) ---
echo "=== Baseline Phase ==="
if [[ -n "$TEST_CASES" ]]; then
    echo "[Baseline] Starting Test Cases..."
    for case_name in $TEST_CASES; do
        echo "[Baseline][Test] Running $case_name..."
        export PAE_DO_PAE=1
        export PAE_DO_PAE_ENHANCE=0
        export WAIT_JOB=1  # Sequential
        export PAE_REPORT=$([[ -f "$CUMULATIVE_REPORT" ]] && echo "$CUMULATIVE_REPORT" || echo "")

        JOB_ID="Baseline_Test_${case_name}"
        bash "${SCRIPT_DIR}/submit_job.sh" "$JOB_ID" "$BIN_PATH" "${BENCH_ROOT}/${case_name}" "${SCRIPT_DIR}/pae_dr.tcl"

        # Capture output report
        LATEST_DIR=$(ls -td ord_pae_${JOB_ID}_* 2>/dev/null | head -1)
        if [[ -n "$LATEST_DIR" ]]; then
            NEW_REPORT="${LATEST_DIR}/${case_name}/PAE.report"
            [[ -f "$NEW_REPORT" ]] && cp "$NEW_REPORT" "$CUMULATIVE_REPORT"
            python3 "$COLLECT_SCRIPT" "0" "Baseline_Test" "$case_name" "${LATEST_DIR}/${case_name}" "$SUMMARY_CSV"
        fi
    done
fi

if [[ -n "$VAL_CASES" ]]; then
    echo "[Baseline] Starting Validation Cases..."
    for case_name in $VAL_CASES; do
        export PAE_DO_PAE=1
        export PAE_DO_PAE_ENHANCE=0
        export PAE_REPORT="$CUMULATIVE_REPORT"
        unset WAIT_JOB  # Parallel

        JOB_ID="Baseline_Val_${case_name}"
        bash "${SCRIPT_DIR}/submit_job.sh" "$JOB_ID" "$BIN_PATH" "${BENCH_ROOT}/${case_name}" "${SCRIPT_DIR}/pae_dr.tcl"
    done

    echo "[Baseline] Waiting for validation jobs to complete..."
    bwait -w "done(*_Baseline_Val_*)" 2>/dev/null || sleep 60

    # Collect baseline validation results
    for case_name in $VAL_CASES; do
        JOB_ID="Baseline_Val_${case_name}"
        LATEST_DIR=$(ls -td ord_pae_${JOB_ID}_* 2>/dev/null | head -1)
        if [[ -n "$LATEST_DIR" ]]; then
            python3 "$COLLECT_SCRIPT" "0" "Baseline_Val" "$case_name" "${LATEST_DIR}/${case_name}" "$SUMMARY_CSV"
        fi
    done
fi
echo "------------------------------"

# --- Test Phase ---
for (( r=1; r<=ROUNDS; r++ )); do
    if [[ -n "$TEST_CASES" ]]; then
        echo "[Round $r] Starting Test Phase..."
        for case_name in $TEST_CASES; do
            echo "[Round $r][Test] Running $case_name..."

            export PAE_DO_PAE=1
            export PAE_DO_PAE_ENHANCE=1
            export WAIT_JOB=1  # Sequential
            export PAE_REPORT=$([[ -f "$CUMULATIVE_REPORT" ]] && echo "$CUMULATIVE_REPORT" || echo "")

            JOB_ID="R${r}_Test_${case_name}"
            bash "${SCRIPT_DIR}/submit_job.sh" "$JOB_ID" "$BIN_PATH" "${BENCH_ROOT}/${case_name}" "${SCRIPT_DIR}/pae_dr.tcl"

            LATEST_DIR=$(ls -td ord_pae_${JOB_ID}_* 2>/dev/null | head -1)
            if [[ -n "$LATEST_DIR" ]]; then
                # Update cumulative report
                NEW_REPORT="${LATEST_DIR}/${case_name}/PAE.report"
                [[ -f "$NEW_REPORT" ]] && cp "$NEW_REPORT" "$CUMULATIVE_REPORT"

                # Collect test metrics
                python3 "$COLLECT_SCRIPT" "$r" "Test" "$case_name" "${LATEST_DIR}/${case_name}" "$SUMMARY_CSV"
            fi
        done
    fi

    # --- Validation Phase ---
    if [[ -n "$VAL_CASES" && "$VAL_INTERVAL" -gt 0 && $(( r % VAL_INTERVAL )) -eq 0 ]]; then
        echo "[Round $r] Starting Validation Phase..."
        for case_name in $VAL_CASES; do
            export PAE_DO_PAE=1
            export PAE_DO_PAE_ENHANCE=1
            export PAE_REPORT="$CUMULATIVE_REPORT"
            unset WAIT_JOB  # Parallel

            JOB_ID="R${r}_Val_${case_name}"
            bash "${SCRIPT_DIR}/submit_job.sh" "$JOB_ID" "$BIN_PATH" "${BENCH_ROOT}/${case_name}" "${SCRIPT_DIR}/pae_dr.tcl"
        done

        # Wait for parallel validation jobs
        echo "[Round $r] Waiting for validation jobs to complete..."
        bwait -w "done(*_R${r}_Val_*)" 2>/dev/null || sleep 60

        # Collect validation results
        for case_name in $VAL_CASES; do
            JOB_ID="R${r}_Val_${case_name}"
            LATEST_DIR=$(ls -td ord_pae_${JOB_ID}_* 2>/dev/null | head -1)
            if [[ -n "$LATEST_DIR" ]]; then
                python3 "$COLLECT_SCRIPT" "$r" "Val" "$case_name" "${LATEST_DIR}/${case_name}" "$SUMMARY_CSV"
            fi
        done
    fi
    echo "------------------------------"
done

echo "=== PAE Experiment Finished ==="
echo "Final cumulative report: $CUMULATIVE_REPORT"
echo "Final results: $SUMMARY_CSV"