#!/bin/bash
# skip: plumbing. Thesis 5.6: the instruction tests of Table A.1 on all three CPUs (run_validation.sh),
# and the BERT layer with PIMony's command lines kept, to count what reaches the PIM.
# Usage: experiments/5.6_validation/run.sh [OUT]   OUT, DRY=1 and skipping: see ../common.sh
set -eu
source "$(dirname "$0")/../common.sh"

V=$OUT/validation_1110   # the folder name of the thesis run (2026-11-10)
if [ -e "$GEM5/$V" ]; then echo "skip $V (exists)"
elif [ "${DRY:-0}" = 1 ]; then echo "OUT=$V bash configs/pimony/run_validation.sh"
else docker exec -d -w /home/pimony/gem5 -e OUT="$V" gem5-container bash configs/pimony/run_validation.sh; echo "started $V"
fi
KEEP='AddTransaction\(PIM\)' run bert_pim_g7pt_o3_wlog fs_bert.py o3 configs/scratch/progs/bert/bert_pim_g7pt
