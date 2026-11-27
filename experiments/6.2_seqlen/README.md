# 6.2 Sequence length and PIM offload (Figure 6.1)

**CPU-only token sweep:** one GPT-3 2.7B layer on the default O3CPU with OpenBLAS and FP16
weights. Decode processes 1 new token with 256 tokens in the KV cache; prefill processes
a prompt of 4 to 512 tokens with an empty cache (causal attention). Batch size one.

- Cycles per processed token = numCycles over the whole layer step / tokens.
- DRAM read = membus `ReadSharedReq` x 64 B.
- Multiply-adds: decode 78,643,200 + 2 x 257 x 2560; prefill n x 78,643,200 + 2 x n² x 2560
  (the linear projections and both attention products).

**32 MiB L2 check:** the prefill runs from 128 tokens on, repeated with `L2_SIZE=32MiB` and
nothing else changed. The drop in multiply-adds per byte past 128 tokens disappears.

**BERT-sized sweep:** CPU-only, FP32 weights, 128 to 512 tokens; multiply-adds
7,077,888 x n + 2 x n² x 768.

**W1 at 64 tokens:** the first FFN projection (2,560 inputs, 10,240 outputs), 64 input
vectors: OpenBLAS GEMM on the CPU against `pim.gemv` one vector at a time (3 jobs per
vector, partial sums added on the host). Elapsed = the first stats block's simTicks / 333.
Sources, build scripts and the full record: `gem5/configs/scratch/progs/gpt3/w1_compare/`.

Programs, in `gem5/configs/scratch/progs/`:

| | Program | Source |
|---|---|---|
| Decode | `gpt3/gpt3_blas_c` | `gpt3/gpt3_blas.c` |
| Prefill, n tokens | `gpt3/gpt3_prefill_s<n>` | `gpt3/gpt3_prefill.c` with `-DSEQ=<n>` |
| BERT-sized, n tokens | `bert/bert_blas_s128`, `bert/bert_blas_s<n>_oct` | `bert/bert_blas.c` |
| W1, CPU / PIM | `gpt3/w1_compare/w1_cpu_64`, `w1_pim_64` | `gpt3/w1_compare/cpu.c`, `pim.c` |

Build lines: `gpt3/gpt3_lab_notes.md` ("prefill vs decode", 2026-10-10), `pim_gemv_results.md`
section 4 (BERT) and `gpt3/w1_compare/README.md`.

## Steps

Run from the repo root.

1. **Run** the 18 simulations in the background inside `gem5-container`. The 512-token
   prefill takes about 9 hours. Existing run folders are skipped, so give a new output
   folder for a fresh copy (`DRY=1` prints only):
   ```
   experiments/6.2_seqlen/run.sh m5out/rerun
   ```
2. **Numbers**: prints them and writes `sweep.csv`, `fig6_1.csv` and `w1_compare.csv`.
   ```
   python3 experiments/6.2_seqlen/parse.py m5out/rerun
   ```
   Without an argument it reads the thesis runs in `gem5/m5out`.
3. **Figure**: draws both panels, `fig6_1a` and `fig6_1b` (`.svg`, `.pdf`, `.png`), from
   `fig6_1.csv` (`--svg-only` skips the PDF and PNG, which need Microsoft Edge from Windows).
   ```
   python3 experiments/6.2_seqlen/plot.py
   ```

## Expected result

What `parse.py` prints for the thesis runs. The SVGs that `plot.py` draws from these
numbers are byte-identical to the two panels of the thesis Figure 6.1.

```
GPT-3 layer, CPU only, O3, 2 MiB L2
         tokens  cycles/token  DRAM read   GB/s MACs/byte    IPC
decode        1    27,401,792   163.3 MB  17.90      0.49  0.497
prefill       4    44,959,560   200.5 MB   3.35      1.57  0.367
prefill      16    13,563,564   211.8 MB   2.93      5.95  0.694
prefill      64     5,913,266   295.7 MB   2.35     17.09  1.404
prefill     128     4,754,784   420.4 MB   2.07     24.14  1.726
prefill     256     4,605,617  1073.9 MB   2.74     19.06  1.806
prefill     512     4,462,854  2235.9 MB   2.94     18.61  1.947
256 tokens: 39x the decode MACs/byte

Same prefill runs with a 32 MiB L2 (L2_SIZE=32MiB)
prefill  128  MACs/byte  54.89  (2 MiB L2: 24.14)
prefill  256  MACs/byte  93.84  (2 MiB L2: 19.06)
prefill  512  MACs/byte 128.40  (2 MiB L2: 18.61)
W1 at 256 tokens reads 393.6 MB with the 2 MiB L2, 64.9 MB with 32 MiB; W1's FP16 weights 52.4 MB + FP32 output 10.5 MB

BERT-sized layer, CPU only, O3, FP32 weights
 128 tokens  MACs/byte  17.79
 256 tokens  MACs/byte  22.22
 384 tokens  MACs/byte  19.31
 512 tokens  MACs/byte  17.74

W1, 64 vectors: CPU 1.692 M cycles per vector, PIM 0.486 M: PIM 3.48x faster
```
