#!/bin/bash
# skip: plumbing. Thesis 6.2: CPU-only GPT-3 token sweep on O3 (decode and prefill), the 32 MiB L2 check,
# the BERT-sized sweep at 128-512 tokens, and W1 at 64 tokens: CPU GEMM vs pim.gemv.
# Usage: experiments/6.2_seqlen/run.sh [OUT]   OUT, DRY=1 and skipping: see ../common.sh
set -eu
source "$(dirname "$0")/../common.sh"
P=configs/scratch/progs
G=$P/gpt3

run gpt3_blas_c_o3_oct se_bert.py o3 $G/gpt3_blas_c            # decode: 1 token, 256 cached
for s in 4 16 64 128 256 512; do
  run gpt3_prefill_s${s}_o3_oct se_bert.py o3 $G/gpt3_prefill_s$s  # prefill: s prompt tokens
done
for s in 128 256 512; do
  ENVS='L2_SIZE=32MiB' run gpt3_prefill_s${s}_l2_32m se_bert.py o3 $G/gpt3_prefill_s$s
done
run o3-s128-l2big  se_bert.py o3 $P/bert_blas_s128               # BERT-sized, FP32 weights
run l2big-s256_oct se_bert.py o3 $P/bert_blas_s256_oct
run l2big-s384_oct se_bert.py o3 $P/bert_blas_s384_oct
run l2big-s512_oct se_bert.py o3 $P/bert_blas_s512_oct
run w1_compare_cpu_64_o3 se_bert.py o3 $G/w1_compare/w1_cpu_64  # W1, 64 vectors: OpenBLAS GEMM
run w1_compare_pim_64_o3 fs_bert.py o3 $G/w1_compare/w1_pim_64  # W1, 64 vectors: pim.gemv, 3 jobs each
