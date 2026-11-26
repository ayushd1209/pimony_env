# 6.5 Cost of pim.fence (Figure 6.4, Table 6.4)

**Fence cost** = matmul cycles of a PIM run minus those of a control built from the same
source without fences. Matmul = sections 1, 3, 5 and 7, cycles = simTicks / 333. The
controls are not functionally correct (stale inputs and results), which does not matter
because PIMony does not compute values. Two kinds of control:

- `_nf` (`-DNO_PIM_FENCE`): every fence removed. Used on O3.
- `_nfd` (`-DNO_PIM_FENCE_DATA`): keeps the descriptor cleans. Minor needs them, otherwise
  the PIM reads a stale descriptor and the run aborts.

Fences are counted from the L1D cache-maintenance requests (CleanSharedReq for
`pim.fence.cl`, InvalidateReq for `pim.fence.inv`); descriptor cleans = one per
`pim.gemv` (its `pim.wait` count).

Programs, in `gem5/configs/scratch/progs/` (build lines: `gpt3/GAPS_gpt3.md`, fence-cost
entry of 2026-10-28, and `GAPS_pim_gemv.md`, entry of 2026-10-22):

| | fences on | `_nf` | `_nfd` |
|---|---|---|---|
| GPT-3 | `gpt3/gpt3_scr64` | `gpt3/gpt3_scr64_nf` | `gpt3/gpt3_scr64_nfd` |
| BERT | `bert_pim_g7pt_scrub64` | `bert_pim_g7pt_scrub64_nf` | `bert_pim_g7pt_scrub64_nfd` |
| BERT, no cache scrub | `bert_pim_g7pt` | | `bert_pim_g7pt_nfd` |

**Table 6.4** comes from a separate bare-metal benchmark, `gem5/tests/test-progs/fence_compare/`:
cleaning 1, 16 and 64 dirty lines with `pim.fence.cl`, or with `cbo.clean` + `fence iorw,iorw`
after every line or once at the end. Its results are committed in `validated/measurements.tsv`,
which `parse.py` reads. Its README explains how to rerun it; it needs a checkout of commit
`7fa2ee9d` with its own gem5 build (`git worktree add`, then `scons build/RISCV/gem5.opt`).

## Steps

Run from the repo root.

1. **Run** the 11 simulations in the background inside `gem5-container`. Existing run
   folders are skipped, so give a new output folder for a fresh copy (`DRY=1` prints only):
   ```
   experiments/6.5_fence/run.sh m5out/rerun
   ```
2. **Numbers**: prints them and writes `fence_cost.csv`, `fig6_4.csv` and `table6_4.csv`.
   ```
   python3 experiments/6.5_fence/parse.py m5out/rerun
   ```
   Without an argument it reads the thesis runs in `gem5/m5out`.
3. **Figure**: draws `fig6_4.svg`, `.pdf` and `.png` from `fig6_4.csv` (`--svg-only`
   skips the PDF and PNG, which need Microsoft Edge from Windows).
   ```
   python3 experiments/6.5_fence/plot.py
   ```

## Expected result

What `parse.py` prints for the thesis runs. The SVG that `plot.py` draws from these
numbers is byte-identical to the thesis Figure 6.4.

```
Fence cost (matmul blocks)
workload         cpu   ctrl  fences on        off     cost removed /fence  share
GPT-3            o3    _nf   1,584,037  1,308,908  275,129   4,985   55.2  17.4%
GPT-3            o3    _nfd  1,584,037  1,313,755  270,282   4,960   54.5  17.1%
GPT-3            minor _nfd  2,285,603  2,162,209  123,394   4,960   24.9   5.4%
BERT             o3    _nf     236,107    181,067   55,040     944   58.3  23.3%
BERT             minor _nfd    375,816    343,591   32,225     936   34.4   8.6%
BERT unscrubbed  minor _nfd    374,501    350,858   23,643     936   25.3   6.3%

Fences in the matmul blocks, and the cache operation under a fence
gpt3_scr64_o3                4,985 = 2,240 input cleans + 25 descriptor cleans + 2,720 result invalidates   cache op 53.3 cycles
gpt3_scr64_minor             4,985 = 2,240 input cleans + 25 descriptor cleans + 2,720 result invalidates   cache op 52.5 cycles
bert_pim_g7pt_scrub64_o3       944 =   672 input cleans +  8 descriptor cleans +   264 result invalidates   cache op 55.3 cycles
bert_pim_g7pt_scrub64_minor    944 =   672 input cleans +  8 descriptor cleans +   264 result invalidates   cache op 53.7 cycles

Figure 6.4 (GPT-3)
minor cycles 2,285,603  asleep 952,950  other awake 1,209,259  fences 123,394 (5.4%)
o3    cycles 1,584,037  asleep 959,983  other awake   348,925  fences 275,129 (17.4%)

Table 6.4 (cycles to clean N dirty lines)
cpu    lines  pim.fence.cl  per-line  batched
o3         1            90        96       96
o3        16         1,049     1,100      239
o3        64         4,122     4,316      546
minor      1            74        74       74
minor     16           500       961      500
minor     64         1,940     3,793    1,940
```
