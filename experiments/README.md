# Thesis experiments

One folder per thesis section. Each folder holds only the recipe; the programs and their
sources stay in `gem5/configs/scratch/progs/` (and `gem5/tests/test-progs/`), once.

| Folder | Thesis | Tables and figures |
|---|---|---|
| `5.6_validation` | 5.6 Validation of the CPU-PIM implementation | Table A.1 |
| `6.2_seqlen` | 6.2 Sequence length and PIM offload | Figure 6.1 |
| `6.3_speedup` | 6.3 Speedup on a single-token transformer layer | Table 6.2, Figure 6.2 |
| `6.4_granularity` | 6.4 Offload granularity | Table 6.3, Figure 6.3 |
| `6.5_fence` | 6.5 Cost of pim.fence | Figure 6.4, Table 6.4 |
| `6.6_overlap` | 6.6 Filling the wait | Figure 6.5 |

Every folder works the same way, run from the repo root:

```
experiments/<folder>/run.sh m5out/rerun          # start its gem5 runs (inside gem5-container)
python3 experiments/<folder>/parse.py m5out/rerun # numbers -> printed + CSV files
python3 experiments/<folder>/plot.py              # figures from the CSV files (where there are any)
```

- `run.sh` skips a run folder that already exists, so it never overwrites a result; give
  a new output folder (here `m5out/rerun`) for a fresh copy. `DRY=1` prints the commands
  without starting anything.
- `parse.py` without an argument reads the thesis runs in `gem5/m5out`. If that folder is
  empty, restore it first (see the top-level README). Its output on the thesis runs is
  listed in each folder's README and matches the thesis.
- `plot.py` draws in the thesis figure style (`docs/diagrams/figstyle.py`). Each SVG is
  byte-identical to the thesis figure. The PDF and PNG are printed by Microsoft Edge from
  Windows; `--svg-only` skips them.

Shared code: `common.sh` (starting a run: output folder, stdout filter, skip, dry run) and
`common.py` (reading `stats.txt`: matmul sections, CPU asleep, PIMony logs, CSV writing).

**Adding an experiment**, for example for a paper: copy a folder, change the runs in
`run.sh` and the numbers in `parse.py`. Cycles = simTicks / 333 (the CPU clock period in
ps); the matmul sections are stats blocks 1, 3, 5 and 7.
