# Workloads

One folder per workload: its sources, its built programs, its notes.

| Folder | What |
|---|---|
| `bert/` | BERT-base encoder layer: bare-metal PIM builds and CPU-only OpenBLAS baselines (its README) |
| `gpt3/` | GPT-3 2.7B decode layer, prefill sweep, W1 comparison (lab notes: `gpt3/GAPS_gpt3.md`) |
| `llama/` | Llama 3.2-3B layer (in progress, branch `llama`) |
| `pimony_baseline/` | PIMony run alone on the same matrices (reference for Figure 6.2) |

Shared, for both BERT and GPT-3:

- `RESULTS_pim_gemv.md`: the measured pim.gemv results, with their commands.
- `GAPS_pim_gemv.md`: the full record behind them.
- `engine_gaps.py`, `writeback_wait.py`: read a run's `run.log` (MAC timing per engine, write-back wait).

The lab notes keep the paths of their time: before 2026-11-26 the BERT files were directly
in `progs/`. The thesis tables and figures are reproduced from `experiments/` at the repo root.
