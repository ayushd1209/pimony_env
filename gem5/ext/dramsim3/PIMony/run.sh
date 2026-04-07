#!/usr/bin/env bash
# Robust mode
# set -euo pipefail

########################################
# User-configurable paths
########################################
# Memory configs (all will be run)
MEM_CONFIGS=(
  "./configs/memory_configs/mem_first.json"
  "./configs/memory_configs/pim_first.json"
  "./configs/memory_configs/balanced.json"
  "./configs/memory_configs/dpsa.json"
  "./configs/memory_configs/async.json"
  "./configs/memory_configs/pimony.json"
)

# Model config (default for mobile/tablet)
MODEL_CONFIG="./configs/model_configs/GEMV.json"

# Trace generator (creates a shared normal trace)
TRACE_DIR="./trace"
TRACE_GEN_SCRIPT="RandomTraceGenerator.py"
TRACE_INI="../PIMSim/configs/LPDDR5X_12Gb_x16_8533_pimony.ini"
NORMAL_TRACE="${TRACE_DIR}/normal.trace"

# Binary
BIN="./build/pimony"

# Logging
LOG_LEVEL="info"                    # off | info | debug
LOG_ROOT="results"                  # store all results under ./results
DATE_TAG="$(date "+%F_%H-%M-%S")"  # e.g., 2025-08-25_21-40-02

########################################
# Functions
########################################
gen_trace() {
  echo "[Trace] Generating trace at ${NORMAL_TRACE}"
  mkdir -p "${TRACE_DIR}"
  pushd "${TRACE_DIR}" >/dev/null

  # Always (re)generate to match the provided behavior
  python3 "${TRACE_GEN_SCRIPT}" \
    -t 0.05 -r 0.8 -f normal -n 300000 \
    -c "${TRACE_INI}"

  popd >/dev/null
  echo "[Trace] Done."
}

run_one() {
  local mem_cfg="$1"
  local cfg_name
  cfg_name="$(basename "${mem_cfg}" .json)"         # e.g., mobile_pimony


  local MODEL_CONFIG_LOCAL="${MODEL_CONFIG}"

  # Per-config output directory: results/<config_name>/
  local out_dir="${LOG_ROOT}/${cfg_name}/${DATE_TAG}"
  local log_path="${out_dir}/simulator.log"
  local cfg_log="${out_dir}/config.log"

  mkdir -p "${out_dir}"

  echo
  echo "============================================================"
  echo "[Run] mem_config=${mem_cfg}"
  echo "[Run] model_config=${MODEL_CONFIG_LOCAL}"
  echo "[Run] out_dir=${out_dir}"
  echo "============================================================"

  # Execute simulator; strip ANSI colors; tee to simulator.log
  "${BIN}" \
    --mem_config "${mem_cfg}" \
    --model_config "${MODEL_CONFIG_LOCAL}" \
    --normal_trace "${NORMAL_TRACE}" \
    --log_level "${LOG_LEVEL}" \
    --log_dir "${out_dir}" \
    | sed -r "s/\x1B\[[0-9;]*[mK]//g" | tee "${log_path}"

  # Record run configuration
  {
    echo "date:         ${DATE_TAG}"
    echo "mem_config:   ${mem_cfg}"
    echo "model_config: ${MODEL_CONFIG_LOCAL}"
    echo "normal_trace: ${NORMAL_TRACE}"
    echo "log_level:    ${LOG_LEVEL}"
    echo "bin:          ${BIN}"
    echo "out_dir:      ${out_dir}"
  } > "${cfg_log}"

  # (Optional but handy) copy configs for provenance
  cp "${mem_cfg}" "${out_dir}/$(basename "${mem_cfg}")"
  cp "${MODEL_CONFIG_LOCAL}" "${out_dir}/$(basename "${MODEL_CONFIG_LOCAL}")"

  echo "[Run] Completed: ${cfg_name} → ${log_path}"
}

########################################
# Main
########################################
echo "[Setup] Log root: ${LOG_ROOT}"
mkdir -p "${LOG_ROOT}"

# 1) Generate/update trace once
gen_trace

# 2) Run all memory configs
for mc in "${MEM_CONFIGS[@]}"; do
  run_one "${mc}"
done

echo
echo "[All Done] Results stored under: ${LOG_ROOT}/<config_name>/${DATE_TAG}/"
