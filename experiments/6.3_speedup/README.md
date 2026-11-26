# 6.3 Speedup on a single-token transformer layer (Table 6.2)

Matrix-multiplication time (sections 1, 3, 5 and 7: QKV, output projection, W1, W2)
of one BERT-sized layer and one GPT-3 2.7B decode layer: OpenBLAS on the CPU against
`pim.gemv`, on three hosts. Speedup = baseline cycles / PIM cycles, with
cycles = simTicks / 333.

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

## Steps

Run from the repo root.

1. **Run** the 12 simulations. They start in the background inside `gem5-container`.
   BERT runs take minutes, GPT-3 runs 1 to 2.5 hours each. A run folder that already
   exists is skipped, never overwritten, so give a new output folder for a fresh copy:
   ```
   experiments/6.3_speedup/run.sh m5out/rerun
   ```
   `DRY=1` in front prints the commands without starting anything.

2. **Numbers**: prints the table and writes `results.csv` next to this file.
   ```
   python3 experiments/6.3_speedup/parse.py m5out/rerun
   ```
   Without an argument it reads the thesis runs in `gem5/m5out`. If that folder is
   empty, restore it first (see the top-level README).

## Expected result

What `parse.py` prints for the thesis runs, and Table 6.2 in the thesis:

```
workload host       baseline        PIM  speedup
BERT     minor     5,235,273    375,816   13.93x
BERT     o3        2,291,189    236,107    9.70x
BERT     o3v61     2,771,684    235,686   11.76x
GPT-3    minor    64,283,925  2,285,603   28.13x
GPT-3    o3       25,243,763  1,584,037   15.94x
GPT-3    o3v61    30,048,969  1,586,830   18.94x
```
