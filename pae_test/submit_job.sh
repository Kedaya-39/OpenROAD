#!/bin/bash
# submit_job.sh - Generic LSF submission script for OpenROAD
# office env location(in openroad repo location): $ORD_ROOT/pae_test/submit_job.sh
# dev env location(executing location): /path/to/proj_orfs/test/submit_job.sh

set -eo pipefail
# 1. Output redirection setup
exec 3>&1 4>&2
trap 'exec 2>&4 1>&3' EXIT

# 2. Argument validation
if [[ $# -lt 3 ]]; then
    echo "Usage: $0 job_id bin_path case_path [run_script]"
    exit 1
fi

job_id="$1"
bin_path="$(realpath -s "$2")"
case_path="$(realpath -s "$3")"
run_script="${4:-$(realpath -s ./pae_dr.tcl)}"

# 3. Environment configuration
case_name=$(basename "${case_path}")
lsf_queue="emu_normal"
lsf_threads=8
lsf_prefix="ord_pae"
timestamp=$(date +%Y%m%d_%H%M%S)
job_dir="${lsf_prefix}_${job_id}_${timestamp}"

# 4. Workspace Creation
mkdir -p "${job_dir}/${case_name}"
work_dir="$(realpath "${job_dir}")"

# 5. Execution Logic (Log to file and console)
exec > >(tee -a "${work_dir}/submit.log") 2>&1

echo "--- Job Configuration ---"
echo "ID: $job_id"
echo "Binary: $bin_path"
echo "Case: $case_name"
echo "-------------------------"

# 6. LSF Submission (Using PAE_ prefix)
ord_root_val="$(cd "$(dirname "$bin_path")/../../" && pwd)"

bsub_cmd="bsub \
    -n ${lsf_threads} \
    -q ${lsf_queue} \
    -J ${lsf_prefix}_${case_name}_${job_id} \
    -o ${work_dir}/${case_name}/lsf_stdout.log \
    -e ${work_dir}/${case_name}/lsf_stderr.log \
    \"export PAE_ORD_ROOT=${ord_root_val}; \
      export PAE_BM_CASE=${case_path}; \
      export PAE_DO_PAE=${PAE_DO_PAE:-1}; \
      export PAE_DO_PAE_ENHANCE=${PAE_DO_PAE_ENHANCE:-1}; \
      export PAE_REPORT=${PAE_REPORT:-}; \
      cd ${work_dir}/${case_name} && \
      ${bin_path} -threads ${lsf_threads} -log openroad.log -metrics openroad.metrics ${run_script}\""

# 7. Check if wait is requested
if [[ -n "$WAIT_JOB" ]]; then
    bsub_cmd="bsub -K $(echo $bsub_cmd | sed 's/bsub//')"
fi

echo "Executing: $bsub_cmd"
eval "${bsub_cmd}"