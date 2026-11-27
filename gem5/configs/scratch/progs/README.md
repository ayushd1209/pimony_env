# Workloads

One folder per workload: its sources, its built programs, its notes.

| Folder | What |
|---|---|
| `bert/` | BERT-base encoder layer: bare-metal PIM builds and CPU-only OpenBLAS baselines (its README) |
| `gpt3/` | GPT-3 2.7B decode layer, prefill sweep, W1 comparison (lab notes: `gpt3/gpt3_lab_notes.md`) |
| `llama/` | Llama 3.2-3B layer (in progress, branch `llama`) |
| `pimony_baseline/` | PIMony run alone on the same matrices (reference for Figure 6.2) |

Shared, for both BERT and GPT-3:

- `pim_gemv_results.md`: the measured pim.gemv results, with their commands.
- `pim_gemv_lab_notes.md`: the full record behind them: problems G1 to G14 found in the
  results and how each was closed (G10 and G11 are open, G8.1 is parked).

The lab notes keep the paths of their time: before 2026-11-26 the BERT files were directly
in `progs/`. The thesis tables and figures are reproduced from `experiments/` at the repo root.
