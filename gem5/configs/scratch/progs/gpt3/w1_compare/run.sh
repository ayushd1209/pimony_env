#!/usr/bin/env bash
set -euo pipefail

mode=${1:-all}
count=${2:-all}
fast=${W1_FAST_INIT:-0}
ordered=${W1_ORDERED_CPU:-0}
default_tag=w1_compare
if [[ "$fast" == 1 ]]; then default_tag=w1_compare_fast; fi
tag=${W1_RUN_TAG:-$default_tag}
container=${W1_CONTAINER:-gem5-container}
container_gem5=${W1_CONTAINER_GEM5:-/home/pimony/gem5}
case "$mode" in cpu|pim|all) ;; *) echo 'Usage: bash run.sh [cpu|pim|all] [1|64|check|all]' >&2; exit 2;; esac
case "$count" in 1|64|check|all) ;; *) echo 'Vector count must be 1, 64, check, or all' >&2; exit 2;; esac
[[ "$tag" =~ ^[A-Za-z0-9_-]+$ ]] || { echo 'W1_RUN_TAG must contain letters, digits, underscores or hyphens' >&2; exit 2; }

modes=("$mode"); counts=("$count")
if [[ "$mode" == all ]]; then modes=(cpu pim); fi
if [[ "$count" == all ]]; then counts=(1 64); fi

for vectors in "${counts[@]}"; do
    for backend in "${modes[@]}"; do
        printf 'Running %s, vectors=%s; log: m5out/%s_%s_%s_o3/run.log\n' \
            "$backend" "$vectors" "$tag" "$backend" "$vectors"
        docker exec -i -w "$container_gem5" "$container" bash -s -- "$backend" "$vectors" "$tag" "$fast" "$ordered" <<'CONTAINER'
set -euo pipefail
backend=$1; vectors=$2; tag=$3; fast=$4; ordered=$5
binary="configs/scratch/progs/gpt3/w1_compare/w1_${backend}_${vectors}"
if [[ "$backend" == cpu && "$ordered" == 1 ]]; then binary=${binary}_ordered; fi
config=configs/pimony/se_bert.py
if [[ "$backend" == pim ]]; then config=configs/pimony/fs_bert.py; fi
config_args=(o3 "$binary")
if [[ "$fast" == 1 ]]; then
    binary=${binary}_fast
    config=configs/scratch/progs/gpt3/w1_compare/fast_init.py
    config_args=("$backend" "$binary")
fi
[[ -x "$binary" ]] || { printf 'Missing binary: %s\n' "$binary" >&2; exit 1; }
directory="m5out/${tag}_${backend}_${vectors}_o3"
mkdir "$directory"
build/RISCV/gem5.opt -d "$directory" "$config" "${config_args[@]}" 2>&1 \
    | grep -a -E 'workload|CPU model|vector FU|mem_config|Beginning|w1|Exiting|Initialization|O3 active|fatal|panic|error|Error|FAULT|FAIL|RuntimeError|Traceback|AttributeError' \
    > "$directory/run.log"
tail -n 4 "$directory/run.log"
if grep -a -E -q 'm5_fail|w1 check: FAIL|fatal:|panic:' "$directory/run.log"; then
    printf 'Failed run; inspect %s/run.log\n' "$directory" >&2
    exit 1
fi
if [[ "$backend" == cpu ]]; then
    grep -a -F -q 'w1 check: PASS' "$directory/run.log"
    grep -a -F -q 'because exiting with last active thread context' "$directory/run.log"
else
    grep -a -F -q 'because m5_exit instruction encountered' "$directory/run.log"
fi
CONTAINER
    done
done
