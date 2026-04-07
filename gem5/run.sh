#!/usr/bin/env bash

export PYTHONUTF8=1
export PYTHONIOENCODING=UTF-8
export LANG=C.UTF-8
export LC_ALL=C.UTF-8

set -u

########################################
# User-configurable defaults
########################################

GEM5_BIN="build/X86/gem5.opt"

# Base paths (device-specific scripts are resolved as <BASE>_<device>.py)
CHECKPOINT_PY_BASE="configs/example/gem5_library/x86-spec-cpu2017-checkpoint"
RUN_PY_BASE="configs/example/gem5_library/x86-spec-cpu2017-benchmarks"   

IMAGE="../disk-image/spec-2017/spec-2017-image/spec-2017"
PARTITION=1
LOG_LEVEL="off" # debug|info|off

# Root directories for outputs
RESULTS_DIR="./results"

# Models to iterate (filenames under model_configs/)
MODELS=(
  "gpt3-2.7B_single_layer.json" # "GEMV.json" "gpt3-6.7B_single_layer.json"
)

# Devices & memory modes
DEVICES=(laptop) # mobile|laptop

MEM_MODES=(pimony pim_first mem_first balanced async dpsa)

# Benchmark sizes
SIZES=(ref) # test|train|ref

# SPEC CPU 2017 benchmark list (trimmed)
BENCHMARKS=(
# ------------------------------
# SPEC CPU2017 Rate Benchmarks
# ------------------------------
  500.perlbench_r
  # 502.gcc_r
  # 503.bwaves_r
  # 505.mcf_r
  # 507.cactuBSSN_r
  # 508.namd_r
  # 510.parest_r
  # 511.povray_r
  # 519.lbm_r
  # 520.omnetpp_r
  # 521.wrf_r
  # 523.xalancbmk_r
  # 525.x264_r
  # 526.blender_r
  # 527.cam4_r
  # 531.deepsjeng_r
  # 538.imagick_r
  # 541.leela_r
  # 544.nab_r
  # 548.exchange2_r
  # 549.fotonik3d_r
  # 554.roms_r
  # 557.xz_r
# ------------------------------
# SPEC CPU2017 Speed Benchmarks
# ------------------------------
  # 600.perlbench_s
  # 602.gcc_s
  # 603.bwaves_s
  # 605.mcf_s
  # 607.cactuBSSN_s
  # 619.lbm_s
  # 620.omnetpp_s
  # 621.wrf_s
  # 623.xalancbmk_s
  # 625.x264_s
  # 627.cam4_s
  # 628.pop2_s
  # 631.deepsjeng_s
  # 638.imagick_s
  # 641.leela_s
  # 644.nab_s
  # 648.exchange2_s
  # 649.fotonik3d_s
  # 654.roms_s
  # 657.xz_s
)

########################################
# Helpers
########################################

sanitize() {
  local s="${1}"
  s="${s// /_}"
  s="${s//\//_}"
  s="${s//[^a-zA-Z0-9._-]/_}"
  printf "%s" "$s"
}

checkpoint_script_for() { echo "${CHECKPOINT_PY_BASE}_$1.py"; }
run_script_for()        { echo "${RUN_PY_BASE}_$1.py"; }

########################################
# Public checkpoint cache (shared)
########################################
CKPT_ROOT="./_ckpts"
mkdir -p "$CKPT_ROOT"

# Flags
REGEN_CKPT=0
DRY_RUN=0
INTERLEAVED=1     # default: round-robin
MAX_ROUNDS=0      # 0 = run up to each case's max
START_ROUND=0
JOBS=1            # max parallel gem5 runs (can be overridden via --jobs/-j)

########################################
# Checkpoints (create/reuse)
########################################

ckpt_dir_for() {
  local device="$1" bench="$2" size="$3"
  echo "${CKPT_ROOT}/${device}/${bench}/${size}"
}

checkpoint_for_case() {
  local device="$1" bench="$2" size="$3" result_dir="$4"
  local ckpt_py; ckpt_py="$(checkpoint_script_for "$device")"

  [[ -x "$GEM5_BIN" ]] || { echo "ERROR: GEM5_BIN not executable: $GEM5_BIN"; exit 1; }
  [[ -f "$ckpt_py"  ]] || { echo "ERROR: Checkpoint script not found: $ckpt_py"; exit 1; }

  echo "------------------------------------------------------------" >&2
  echo "[CKPT] device=${device} bench=${bench} size=${size}" >&2
  echo "       script=${ckpt_py}" >&2
  echo "------------------------------------------------------------" >&2

  # Use a unique outdir for checkpoints to avoid collisions in parallel runs
  local ckpt_out="${result_dir}/ckpt_m5out"
  mkdir -p "$ckpt_out"

  # IMPORTANT: -d/--outdir must be passed to the gem5 binary (before the script path)
  local cmd=( env PYTHONUTF8=1 PYTHONIOENCODING=UTF-8 LANG=C.UTF-8 LC_ALL=C.UTF-8
              "$GEM5_BIN" -d "$ckpt_out"
              "$ckpt_py"
              --image "$IMAGE"
              --partition "$PARTITION"
              --benchmark "$bench"
              --size "$size" )

  ( "${cmd[@]}" 2>&1 \
      | sed -r "s/\x1B\[[0-9;]*[mK]//g" \
      | tee "${result_dir}/checkpoint.log" )
  local ckpt_status=${PIPESTATUS[0]}

  if [[ $ckpt_status -ne 0 ]]; then
    echo "[CKPT][FAIL] gem5 exited with code ${ckpt_status}" >&2
  fi

  local n_found
  n_found=$(find "${ckpt_out}" -type f -name "m5.cpt" -printf '%h\n' 2>/dev/null | sort -u -V | wc -l || true)
  if [[ "$n_found" -eq 0 ]]; then
    echo "[CKPT][WARN] No checkpoints found under ${ckpt_out}" >&2
  else
    echo "[CKPT] Found ${n_found} checkpoints." >&2
  fi
}

# Get or make shared checkpoints for (device, bench, size)
get_or_make_checkpoints() {
  local device="$1" bench="$2" size="$3"
  local ckpt_case_root; ckpt_case_root="$(ckpt_dir_for "$device" "$bench" "$size")"
  local ckpt_m5="${ckpt_case_root}/ckpt_m5out"

  if (( REGEN_CKPT )) && [[ -d "$ckpt_case_root" ]]; then
    echo "[CKPT] --regen-ckpt: removing ${ckpt_case_root}" >&2
    rm -rf "$ckpt_case_root"
  fi

  if find "$ckpt_m5" -type f -name "m5.cpt" -print -quit | grep -q .; then
    echo "[CKPT] Reusing checkpoints under ${ckpt_m5}" >&2
    return 0
  fi

  mkdir -p "$ckpt_case_root"
  checkpoint_for_case "$device" "$bench" "$size" "$ckpt_case_root"
  return 0
}

# Count how many checkpoints exist for (device, bench, size)
count_ckpts() {
  local device="$1" bench="$2" size="$3"
  local ckpt_m5="$(ckpt_dir_for "$device" "$bench" "$size")/ckpt_m5out"

  get_or_make_checkpoints "$device" "$bench" "$size" >/dev/null 2>&1

  LC_ALL=C
  local n
  n=$(find "$ckpt_m5" -type f -name "m5.cpt" -printf '%h\n' 2>/dev/null \
        | sort -u -V | wc -l | tr -d '[:space:]')
  [[ "$n" =~ ^[0-9]+$ ]] || n=0
  echo "$n"
}

# Get the N-th checkpoint dir (0-based) for (device, bench, size)
nth_ckpt() {
  local device="$1" bench="$2" size="$3" idx="$4"
  local ckpt_m5="$(ckpt_dir_for "$device" "$bench" "$size")/ckpt_m5out"
  get_or_make_checkpoints "$device" "$bench" "$size"
  # print the (idx+1)-th line
  local n=$((idx + 1))
  find "$ckpt_m5" -type f -name "m5.cpt" -printf '%h\n' 2>/dev/null | sort -u -V | sed -n "${n}p" || true
}

########################################
# Parallel job launcher helper
########################################

run_bg () {
  # Submit a command in background while capping concurrency to $JOBS.
  while (( $(jobs -rp | wc -l) >= JOBS )); do
    wait -n || true
  done
  "$@" &
}

########################################
# Run one checkpoint (N-th) for a case
########################################
run_one_case_at_index() {
  local device="$1" mem_mode="$2" model_json="$3" bench="$4" size="$5" idx="$6"

  local run_py; run_py="$(run_script_for "$device")"
  [[ -f "$run_py" ]] || { echo "ERROR: Run script not found: $run_py"; exit 1; }

  local mem_cfg="./ext/dramsim3/PIMony/configs/memory_configs/${device}_${mem_mode}.json"
  [[ -f "$mem_cfg" ]] || { echo "ERROR: mem_config not found: $mem_cfg"; exit 1; }

  local model_cfg="./ext/dramsim3/PIMony/configs/model_configs/${model_json}"
  [[ -f "$model_cfg" ]] || { echo "ERROR: model_config not found: $model_cfg"; exit 1; }

  # Resolve the N-th checkpoint for this (device, bench, size)
  local ckpt
  ckpt="$(nth_ckpt "$device" "$bench" "$size" "$idx")"
  if [[ -z "$ckpt" ]]; then
    # This case doesn't have that many checkpoints; skip silently.
    return 0
  fi

  local model_base; model_base="$(basename "$model_json" .json)"
  model_base="$(sanitize "$model_base")"

  local result_dir="${RESULTS_DIR}/${device}/${mem_mode}/${model_base}/${bench}/${size}"
  mkdir -p "$result_dir"

  local ckpt_name; ckpt_name="$(basename "$ckpt")"        # ff_000123
  local run_dir="${result_dir}/run_${ckpt_name}"
  mkdir -p "$run_dir"

  echo "------------------------------------------------------------"
  echo "[RUN] idx=${idx}  ckpt=${ckpt}"
  echo "      dev=${device} mem=${mem_mode} model=$(basename "$model_json") bench=${bench} size=${size}"
  echo "      out=${run_dir}"
  echo "------------------------------------------------------------"

  if (( DRY_RUN )); then
    echo "[DRY-RUN] would run: $run_py @ $ckpt → $run_dir"
    return 0
  fi

  # IMPORTANT: -d/--outdir must be passed to the gem5 binary (before the script path)
  local cmd=( env PYTHONUTF8=1 PYTHONIOENCODING=UTF-8 LANG=C.UTF-8 LC_ALL=C.UTF-8
              "$GEM5_BIN" -d "$run_dir"
              "$run_py"
              --mem_config "$mem_cfg"
              --model_config "$model_cfg"
              --log_level "$LOG_LEVEL"
              --image "$IMAGE"
              --partition "$PARTITION"
              --benchmark "$bench"
              --size "$size"
              --checkpoint "$ckpt" )

  ( "${cmd[@]}" 2>&1 \
      | sed -r "s/\x1B\[[0-9;]*[mK]//g" \
      | tee "${run_dir}/simulator.log" )
  local gem5_status=${PIPESTATUS[0]}

  {
    echo "device          : ${device}"
    echo "mem_mode        : ${mem_mode}"
    echo "model_json      : ${model_json}"
    echo "benchmark       : ${bench}"
    echo "size            : ${size}"
    echo "mem_config_path : ${mem_cfg}"
    echo "model_config    : ${model_cfg}"
    echo "gem5_bin        : ${GEM5_BIN}"
    echo "run_py          : ${run_py}"
    echo "image           : ${IMAGE}"
    echo "partition       : ${PARTITION}"
    echo "log_level       : ${LOG_LEVEL}"
    echo "checkpoint_path : ${ckpt}"
    echo "outdir          : ${run_dir}"
    echo "run_cmdline     : ${cmd[*]}"
  } > "${run_dir}/meta.txt"

  if [[ $gem5_status -ne 0 ]]; then
    echo "[RUN][FAIL] gem5 exited with code ${gem5_status}" | tee -a "${run_dir}/simulator.log"
  else
    echo "[RUN][OK] ${run_dir}"
  fi
}

########################################
# CLI filters
########################################

usage() {
  cat <<EOF
Usage: $0 [--device <d>] [--mem <m>] [--model <json>] [--bench <b>] [--size <s>]
          [--regen-ckpt] [--dry-run]
          [--interleaved | --sequential]
          [--max-rounds N] [--start-round N]
          [--jobs N | -j N]

This script:
  1) Creates/reuses shared checkpoints per (device, bench, size) under:
       ${CKPT_ROOT}/<device>/<bench>/<size>/ckpt_m5out
  2) Runs timing simulations. Each run gets a unique --outdir to avoid m5out collisions.
  3) Supports parallel execution up to --jobs N.

Modes:
  --interleaved  : Round-robin by checkpoint index across ALL cases (default)
                   e.g., run all ff_000000, then all ff_000001, ...
  --sequential   : Old behavior (run all ckpts per case before next case)
  --max-rounds N : Only run first N checkpoints (e.g., N=20 → ...000019)
  --start-round N: Start from checkpoint index N (default=0)
EOF
}

FILTER_DEVICE="" FILTER_MEM="" FILTER_MODEL="" FILTER_BENCH="" FILTER_SIZE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device) FILTER_DEVICE="${2:-}"; shift 2 ;;
    --mem)    FILTER_MEM="${2:-}";    shift 2 ;;
    --model)  FILTER_MODEL="${2:-}";  shift 2 ;;
    --bench)  FILTER_BENCH="${2:-}";  shift 2 ;;
    --size)   FILTER_SIZE="${2:-}";   shift 2 ;;
    --regen-ckpt) REGEN_CKPT=1; shift ;;
    --dry-run)    DRY_RUN=1; shift ;;
    --interleaved) INTERLEAVED=1; shift ;;
    --sequential)  INTERLEAVED=0; shift ;;
    --max-rounds)  MAX_ROUNDS="${2:-0}"; shift 2 ;;
    --start-round) START_ROUND="${2:-0}"; shift 2 ;;
    --jobs|-j)     JOBS="${2:-1}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1"; usage; exit 1 ;;
  esac
done

mkdir -p "$RESULTS_DIR"
[[ -x "$GEM5_BIN" ]] || { echo "ERROR: GEM5_BIN not executable: $GEM5_BIN"; exit 1; }
[[ -f "$IMAGE"   ]]  || { echo "WARN : IMAGE not found: $IMAGE"; }

########################################
# Build the list of CASES that pass filters
########################################

declare -a CASES=()   # each item: "device|mem|model|bench|size"

for device in "${DEVICES[@]}"; do
  if [[ -n "$FILTER_DEVICE" && "$FILTER_DEVICE" != "$device" ]]; then continue; fi

  ckpt_py="$(checkpoint_script_for "$device")"
  run_py="$(run_script_for "$device")"
  [[ -f "$ckpt_py" ]] || { echo "ERROR: Missing checkpoint script for ${device}: ${ckpt_py}"; exit 1; }
  [[ -f "$run_py"  ]] || { echo "ERROR: Missing run script for ${device}: ${run_py}"; exit 1; }

  for mem in "${MEM_MODES[@]}"; do
    if [[ -n "$FILTER_MEM" && "$FILTER_MEM" != "$mem" ]]; then continue; fi
    for model in "${MODELS[@]}"; do
      if [[ -n "$FILTER_MODEL" && "$FILTER_MODEL" != "$model" ]]; then continue; fi
      for bench in "${BENCHMARKS[@]}"; do
        if [[ -n "$FILTER_BENCH" && "$FILTER_BENCH" != "$bench" ]]; then continue; fi
        for size in "${SIZES[@]}"; do
          if [[ -n "$FILTER_SIZE" && "$FILTER_SIZE" != "$size" ]]; then continue; fi
          CASES+=("${device}|${mem}|${model}|${bench}|${size}")
        done
      done
    done
  done
done

if ((${#CASES[@]}==0)); then
  echo "No cases to run (filters too restrictive?)."
  exit 0
fi

########################################
# Ensure checkpoints exist per (device,bench,size), compute rounds
########################################

# Prewarm & compute per (device,bench,size) counts
declare -A KEY_COUNTS=() # key="device|bench|size" -> count
max_rounds_seen=0

for item in "${CASES[@]}"; do
  IFS='|' read -r device mem model bench size <<<"$item"
  key="${device}|${bench}|${size}"
  if [[ -z "${KEY_COUNTS[$key]:-}" ]]; then
    cnt="$(count_ckpts "$device" "$bench" "$size")"
    KEY_COUNTS[$key]="$cnt"
    if (( cnt > max_rounds_seen )); then max_rounds_seen="$cnt"; fi
  fi
done

if (( MAX_ROUNDS > 0 )) && (( MAX_ROUNDS < max_rounds_seen )); then
  max_rounds_seen="$MAX_ROUNDS"
fi

echo "[PLAN] CASES=${#CASES[@]}  ROUNDS=${max_rounds_seen} (per index ff_000000 ...)"
echo "[PLAN] Mode: $([[ $INTERLEAVED -eq 1 ]] && echo interleaved || echo sequential)"
echo "[PLAN] Jobs: ${JOBS}"

########################################
# Execute
########################################

# Make sure we kill children on Ctrl-C / termination
trap 'echo; echo "[ABORT] stopping children..."; jobs -rp | xargs -r -n1 kill; wait; exit 130' INT TERM

if (( INTERLEAVED )); then
  # Round-robin by checkpoint index across ALL cases (parallelized)
  for ((idx=START_ROUND; idx<max_rounds_seen; ++idx)); do
    echo "===================== ROUND idx=${idx} ====================="
    for item in "${CASES[@]}"; do
      IFS='|' read -r device mem model bench size <<<"$item"

      # Skip if this (device,bench,size) doesn't have enough ckpts
      key="${device}|${bench}|${size}"
      total="${KEY_COUNTS[$key]}"
      if (( idx >= total )); then
        continue
      fi

      if (( DRY_RUN )); then
        echo "[DRY-RUN] would run: $device $mem $model $bench $size @ idx=$idx"
      else
        run_bg run_one_case_at_index "$device" "$mem" "$model" "$bench" "$size" "$idx"
      fi
    done
    # Optionally, wait here to synchronize rounds:
    # wait
  done
else
  # Sequential over cases, but checkpoints within a case can run in parallel up to $JOBS
  for item in "${CASES[@]}"; do
    IFS='|' read -r device mem model bench size <<<"$item"
    total="$(count_ckpts "$device" "$bench" "$size")"
    limit="$total"
    if (( MAX_ROUNDS > 0 )) && (( MAX_ROUNDS < limit )); then
      limit="$MAX_ROUNDS"
    fi
    for ((i=START_ROUND; i<limit; ++i)); do 
      if (( DRY_RUN )); then
        echo "[DRY-RUN] would run: $device $mem $model $bench $size @ idx=$i"
      else
        run_bg run_one_case_at_index "$device" "$mem" "$model" "$bench" "$size" "$i"
      fi
    done
    # Finish this case before moving to the next
    wait
  done
fi

# Final wait for all background jobs
wait
echo "All done."
