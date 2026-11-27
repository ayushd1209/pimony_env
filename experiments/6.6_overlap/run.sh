#!/bin/bash
# skip: plumbing. Thesis 6.6: GPT-3 layer on O3, overlap modes and attention heads on PIM (gpt3_hs2_*).
# All from gpt3/gpt3.c; flags only differ (build lines: gpt3/gpt3_lab_notes.md, head-split rerun 2026-10-20).
# Usage: experiments/6.6_overlap/run.sh [OUT]   OUT, DRY=1 and skipping: see ../common.sh
set -eu
source "$(dirname "$0")/../common.sh"
G=configs/scratch/progs/gpt3

run gpt3_hs2_n0_o3  fs_bert.py o3 $G/gpt3_hs2_n0    # reference: no overlap, all heads on the CPU
run gpt3_hs2_mm_o3  fs_bert.py o3 $G/gpt3_hs2_mm    # 1st overlap: read Q, K results in the next waits
run gpt3_hs2_att_o3 fs_bert.py o3 $G/gpt3_hs2_att   # 2nd overlap: scores and softmax while PIM computes V
run gpt3_hs2_n28_o3 fs_bert.py o3 $G/gpt3_hs2_n28   # 28 heads on PIM
run gpt3_hs2_n32_o3 fs_bert.py o3 $G/gpt3_hs2_n32   # all 32 heads on PIM
