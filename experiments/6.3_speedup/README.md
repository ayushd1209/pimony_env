# 6.3 Speedup on a single-token transformer layer (Table 6.2, Figure 6.2, VLEN 512)

Matrix-multiplication time (sections 1, 3, 5 and 7: QKV, output projection, W1, W2)
of one BERT-sized layer and one GPT-3 2.7B decode layer: OpenBLAS on the CPU against
`pim.gemv`, on three hosts. Speedup = baseline cycles / PIM cycles, with
cycles = simTicks / 333. Figure 6.2 splits the GPT-3 PIM time into CPU awake and CPU
asleep in `pim.wait` (asleep = cycles - numCycles), next to PIMony running alone.
The VLEN runs repeat the BERT O3 pair with 512-bit vector registers (`--vlen=512`).

| Host | CPU model | Vector unit |
|---|---|---|
| `minor` | MinorCPU | one unit, 6-cycle latency |
| `o3` | O3CPU (default) | four units, 1-cycle latency |
| `o3v61` | O3CPU | Minor's: one unit, 6-cycle latency |

Programs, in `gem5/configs/scratch/progs/`:

| | PIM run (`fs_bert.py`) | CPU baseline (`se_bert.py`) |
|---|---|---|
| BERT | `bert_pim_g7pt_scrub64` (from `bert_scrub64.c`) | `bert_blas_fair_scrub` (from `bert_blas_fair_scrub.c`) |
| GPT-3 | `gpt3/gpt3_scr64` (from `gpt3/gpt3_scrub64.c`) | `gpt3/gpt3_blas_scrub` (from `gpt3/gpt3_blas_scrub.c`) |

PIMony alone (Figure 6.2) comes from the logged standalone runs in
`gem5/configs/scratch/progs/pimony_baseline/`, which also explains them.

## Steps

Run from the repo root.

1. **Run** the 16 simulations (12 for the table, 4 for VLEN). They start in the background inside `gem5-container`.
   BERT runs take minutes, GPT-3 runs 1 to 2.5 hours each. A run folder that already
   exists is skipped, never overwritten, so give a new output folder for a fresh copy:
   ```
   experiments/6.3_speedup/run.sh m5out/rerun
   ```
   `DRY=1` in front prints the commands without starting anything.

2. **Numbers**: prints them and writes `table6_2.csv`, `fig6_2.csv` and `vlen.csv` next to this file.
   ```
   python3 experiments/6.3_speedup/parse.py m5out/rerun
   ```
   Without an argument it reads the thesis runs in `gem5/m5out`. If that folder is
   empty, restore it first (see the top-level README).

3. **Figure**: draws `fig6_2.svg`, `.pdf` and `.png` from `fig6_2.csv`.
   ```
   python3 experiments/6.3_speedup/plot.py
   ```
   The PDF and PNG are printed by Microsoft Edge from Windows; `--svg-only` skips them.

## Expected result

What `parse.py` prints for the thesis runs. The table is Table 6.2; the SVG that
`plot.py` draws from these numbers is byte-identical to the thesis Figure 6.2.

```
Table 6.2
workload host       baseline        PIM  speedup
BERT     minor     5,235,273    375,816   13.93x
BERT     o3        2,291,189    236,107    9.70x
BERT     o3v61     2,771,684    235,686   11.76x
GPT-3    minor    64,283,925  2,285,603   28.13x
GPT-3    o3       25,243,763  1,584,037   15.94x
GPT-3    o3v61    30,048,969  1,586,830   18.94x

Figure 6.2 (GPT-3)
pimony_alone      cycles   705,145
minor             cycles 2,285,603  asleep   952,950  awake 1,332,653
o3                cycles 1,584,037  asleep   959,983  awake   624,054
o3v61             cycles 1,586,830  asleep   959,683  awake   627,147
pimony_same_jobs  cycles   941,105

VLEN 256 vs 512 (BERT, O3)
VLEN 256  baseline 2,291,189  PIM 236,107  9.70x
VLEN 512  baseline 1,761,774  PIM 240,944  7.31x
512 vs 256: baseline -23.1%, PIM +2.0%
```
