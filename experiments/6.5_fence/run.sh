#!/bin/bash
# skip: plumbing. Thesis 6.5: cost of pim.fence = each PIM run minus a control built without fences.
# _nf = -DNO_PIM_FENCE (no fences), _nfd = -DNO_PIM_FENCE_DATA (keeps the descriptor cleans).
# Table 6.4 is a separate benchmark: see README.md.
# Usage: experiments/6.5_fence/run.sh [OUT]   OUT, DRY=1 and skipping: see ../common.sh
set -eu
source "$(dirname "$0")/../common.sh"
P=configs/scratch/progs

run gpt3_scr64_minor     fs_bert.py minor $P/gpt3/gpt3_scr64       # fences on (also in 6.3)
run gpt3_scr64_o3        fs_bert.py o3    $P/gpt3/gpt3_scr64
run gpt3_scr64_nfd_minor fs_bert.py minor $P/gpt3/gpt3_scr64_nfd   # Minor control: no descriptor clean aborts
run gpt3_scr64_nf_o3     fs_bert.py o3    $P/gpt3/gpt3_scr64_nf
run gpt3_scr64_nfd_o3    fs_bert.py o3    $P/gpt3/gpt3_scr64_nfd   # O3 control keeping the descriptor cleans
run bert_pim_g7pt_scrub64_minor     fs_bert.py minor $P/bert/bert_pim_g7pt_scrub64
run bert_pim_g7pt_scrub64_o3        fs_bert.py o3    $P/bert/bert_pim_g7pt_scrub64
run bert_pim_g7pt_scrub64_nfd_minor fs_bert.py minor $P/bert/bert_pim_g7pt_scrub64_nfd
run bert_pim_g7pt_scrub64_nf_o3     fs_bert.py o3    $P/bert/bert_pim_g7pt_scrub64_nf
run bert_pim_g7pt_minor             fs_bert.py minor $P/bert/bert_pim_g7pt       # without the cache scrub
run bert_pim_g7pt_nfd_minor         fs_bert.py minor $P/bert/bert_pim_g7pt_nfd
