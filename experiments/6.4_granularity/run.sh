#!/bin/bash
# skip: plumbing. Thesis 6.4: pim.gemv vs pim.dispatch on O3 (Table 6.3, Figure 6.3).
# Usage: experiments/6.4_granularity/run.sh [OUT]   OUT, DRY=1 and skipping: see ../common.sh
set -eu
source "$(dirname "$0")/../common.sh"
P=configs/scratch/progs
MAC='[|] MAC [|]'  # dispatch runs keep their MAC lines: commands sent, engine gaps

run bert_pim_g7pt_scrub64_o3 fs_bert.py o3 $P/bert_pim_g7pt_scrub64   # pim.gemv (also in 6.3)
run gpt3_scr64_o3            fs_bert.py o3 $P/gpt3/gpt3_scr64
KEEP=$MAC run bert_disp_w32_scrub64_o3 fs_bert.py o3 $P/bert_disp_w32_scrub64       # 32 commands, then wait
KEEP=$MAC run bert_disp_scrub64_o3     fs_bert.py o3 $P/bert_disp_scrub64           # 1 command, then wait
KEEP=$MAC run gpt3_disp_w32_scr64_o3   fs_bert.py o3 $P/gpt3/gpt3_disp_w32_scr64
KEEP=$MAC run gpt3_disp_w1_scr64_o3    fs_bert.py o3 $P/gpt3/gpt3_disp_w1_scr64
KEEP=$MAC run bert_disp_w192_o3_f2     fs_bert.py o3 $P/bert_disp_w192              # 192 at once: PIMony aborts
