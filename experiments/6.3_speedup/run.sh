#!/bin/bash
# skip: plumbing. Thesis 6.3: Table 6.2 (BERT, GPT-3 x PIM, OpenBLAS x minor, o3, o3v61) and VLEN 256 vs 512.
# Usage: experiments/6.3_speedup/run.sh [OUT]   OUT, DRY=1 and skipping: see ../common.sh
set -eu
source "$(dirname "$0")/../common.sh"
P=configs/scratch/progs

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
# VLEN 256 vs 512, BERT on O3 (256 = gem5's default, so no flag)
run vlen/bert_blas_fair_scrub_o3_v256  se_bert.py o3 $P/bert_blas_fair_scrub
run vlen/bert_blas_fair_scrub_o3_v512  se_bert.py o3 $P/bert_blas_fair_scrub --vlen=512
run vlen/bert_pim_g7pt_scrub64_o3_v256 fs_bert.py o3 $P/bert_pim_g7pt_scrub64
run vlen/bert_pim_g7pt_scrub64_o3_v512 fs_bert.py o3 $P/bert_pim_g7pt_scrub64 --vlen=512
