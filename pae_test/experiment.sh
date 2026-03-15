#!/bin/bash
# experiment.sh - Master script for PAE experiments
# Usage: ./experiment.sh <bin_path> <benchmark_root> <cases_file> <rounds>

set -e

if [[ $# -lt 4 ]]; then
    echo "Usage: $0 <bin_path> <benchmark_root> <cases_file> <rounds>"
    exit 1
fi

BIN_PATH=$(realpath -s "$1")
BENCH_ROOT=$(realpath -s "$2")
CASES_FILE=$(realpath -s "$3")
ROUNDS=$4

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# Parse case lists from config file
TRAIN_CASES=$(grep "^TRAIN:" "$CASES_FILE" | cut -d':' -f2 | tr ',' ' ')
VAL_CASES=$(grep "^VAL:" "$CASES_FILE" | cut -d':' -f2 | tr ',' ' ')

if [[ -z "$TRAIN_CASES" ]]; then
    echo "Error: No training cases found in $CASES_FILE"
    exit 1
fi

# 1. Create a unique root directory for this experiment run
EXP_TIMESTAMP=$(date +%Y%m%d_%H%M%S)
EXP_ROOT_DIR="$(pwd)/pae_run_${EXP_TIMESTAMP}"
mkdir -p "$EXP_ROOT_DIR"

echo "=== PAE Experiment Started ==="
echo "Experiment Root: $EXP_ROOT_DIR"
echo "Train Cases:     $TRAIN_CASES"
echo "Val Cases:       $VAL_CASES"
echo "Rounds:          $ROUNDS"
echo "------------------------------"

# Move into the root directory so all outputs are contained
cd "$EXP_ROOT_DIR"

# 2. Cumulative report path (within the experiment root)
CUMULATIVE_REPORT="${EXP_ROOT_DIR}/cumulative_pae.report"
# Clean start
rm -f "$CUMULATIVE_REPORT"

for (( r=1; r<=ROUNDS; r++ )); do
    echo "[Round $r] Starting Training Phase..."
    
    # Training: Sequential execution to accumulate knowledge
    for case_name in $TRAIN_CASES; do
        echo "[Round $r][Train] Running $case_name..."
        
        export PAE_DO_PAE=1
        export PAE_DO_PAE_ENHANCE=1
        export WAIT_JOB=1  # Wait for LSF job to complete
        
        if [[ -f "$CUMULATIVE_REPORT" ]]; then
            export PAE_REPORT="$CUMULATIVE_REPORT"
        else
            export PAE_REPORT=""
        fi
        
        JOB_ID="R${r}_TR_${case_name}"
        CASE_PATH="${BENCH_ROOT}/${case_name}"
        
        # Submit via the generic submission script
        bash "${SCRIPT_DIR}/submit_job.sh" "$JOB_ID" "$BIN_PATH" "$CASE_PATH" "${SCRIPT_DIR}/pae_dr.tcl"
        
        # Capture the output report from the unique job directory
        LATEST_DIR=$(ls -td ord_pae_${JOB_ID}_* | head -1)
        NEW_REPORT="${LATEST_DIR}/${case_name}/PAE.report"
        
        if [[ -f "$NEW_REPORT" ]]; then
            cp "$NEW_REPORT" "$CUMULATIVE_REPORT"
            echo "[Round $r][Train] Knowledge from $case_name merged into cumulative report."
        else
            echo "Error: Training failed for $case_name. Expected report at $NEW_REPORT"
            exit 1
        fi
    done
    
    echo "[Round $r] Training Phase Completed."
    echo "[Round $r] Starting Validation Phase..."
    
    # Validation: Parallel execution using the round's final cumulative report
    for case_name in $VAL_CASES; do
        echo "[Round $r][Val] Submitting $case_name..."
        
        export PAE_DO_PAE=1
        export PAE_DO_PAE_ENHANCE=1
        export PAE_REPORT="$CUMULATIVE_REPORT"
        unset WAIT_JOB  # Parallel submission
        
        JOB_ID="R${r}_VAL_${case_name}"
        CASE_PATH="${BENCH_ROOT}/${case_name}"
        
        bash "${SCRIPT_DIR}/submit_job.sh" "$JOB_ID" "$BIN_PATH" "$CASE_PATH" "${SCRIPT_DIR}/pae_dr.tcl"
    done
    
    echo "[Round $r] All validation jobs for this round have been submitted."
    echo "------------------------------"
done

echo "=== PAE Experiment Management Finished ==="
echo "Final cumulative report: $CUMULATIVE_REPORT"
