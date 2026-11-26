#!/bin/bash
# skip: plumbing. Thesis 6.3 / Table 6.2: starts the 12 runs (BERT, GPT-3 x PIM, OpenBLAS x minor, o3, o3v61).
# Usage: experiments/6.3_speedup/run.sh [OUT]   OUT = output dir relative to gem5/, default m5out
# DRY=1 prints the commands instead of starting them. An existing run folder is never overwritten.
set -eu
GEM5=$(cd "$(dirname "$0")/../../gem5" && pwd)
OUT=${1:-m5out}
P=configs/scratch/progs
FILTER='workload|layer|out\[0|checksum|PIM State|Exiting|panic|fatal|egmentation'

run() {  # run name, config script, cpu, binary, [vector latency, vector units]
  local d=$OUT/$1 cmd
  if [ -e "$GEM5/$d" ]; then echo "skip $d (exists)"; return; fi
  cmd="mkdir -p $d && build/RISCV/gem5.opt -d $d configs/pimony/$2 $3 $4 ${5:-} ${6:-} 2>&1 | grep -a -E '$FILTER' > $d/run.log"
  if [ "${DRY:-0}" = 1 ]; then echo "$cmd"; return; fi
  docker exec -d -w /home/pimony/gem5 gem5-container sh -c "$cmd"
  echo "started $d"
}

[ "${DRY:-0}" = 1 ] || docker start gem5-container >/dev/null
for h in minor o3 o3v61; do
  case $h in
    minor) c=minor; v="" ;;
    o3)    c=o3;    v="" ;;
    o3v61) c=o3;    v="6 1" ;;  # O3 with Minor's vector unit: 6-cycle latency, 1 unit
  esac
  run bert_pim_g7pt_scrub64_$h fs_bert.py $c $P/bert_pim_g7pt_scrub64 $v
  run bert_blas_fair_scrub_$h  se_bert.py $c $P/bert_blas_fair_scrub $v
  run gpt3_scr64_$h            fs_bert.py $c $P/gpt3/gpt3_scr64 $v
  run gpt3_blas_scrub_$h       se_bert.py $c $P/gpt3/gpt3_blas_scrub $v
done
