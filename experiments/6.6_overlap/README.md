# 6.6 Filling the wait: overlap and interference (Figure 6.5)

One GPT-3 decode layer on the default O3CPU, all runs from `gpt3/gpt3.c` with only the
compile flags differing (build lines: `gem5/configs/scratch/progs/gpt3/GAPS_gpt3.md`,
"attention-head split, rerun with the GELU fix", 2026-10-20):

| Run | Program (`progs/gpt3/`) | Flag | What it does |
|---|---|---|---|
| `gpt3_hs2_n0_o3` | `gpt3_hs2_n0` | `-DPIM_ATT_N=0` | reference: no overlap, all heads on the CPU |
| `gpt3_hs2_mm_o3` | `gpt3_hs2_mm` | `-DPIM_OV_MM=1` | 1st overlap: read Q's results while PIM computes K, K's while it computes V |
| `gpt3_hs2_att_o3` | `gpt3_hs2_att` | `-DPIM_OV_ATT=1` | 2nd overlap: scores and softmax while PIM computes V |
| `gpt3_hs2_n28_o3` | `gpt3_hs2_n28` | `-DPIM_ATT_N=28` | 28 attention heads on PIM |
| `gpt3_hs2_n32_o3` | `gpt3_hs2_n32` | `-DPIM_ATT_N=32` | all 32 attention heads on PIM |

Sections are the layer's eight stats blocks: 1 LN1 + QKV, 2 attention, 3 Wo,
4 residual 1 + LN2, 5 W1, 6 GELU, 7 W2, 8 residual 2. Cycles = simTicks / 333,
CPU asleep = cycles - numCycles. These runs are compared with each other, not with
the CPU-only baseline. Figure 6.6 (one attention head on PIM) is a drawing, not data,
and stays in `docs/diagrams/`.

## Steps

Run from the repo root.

1. **Run** the 5 simulations (1 to 2.5 hours each) in the background inside
   `gem5-container`. Existing run folders are skipped, so give a new output folder
   for a fresh copy (`DRY=1` prints only):
   ```
   experiments/6.6_overlap/run.sh m5out/rerun
   ```
2. **Numbers**: prints them and writes `runs.csv`, `overlap.csv` and `fig6_5.csv`.
   ```
   python3 experiments/6.6_overlap/parse.py m5out/rerun
   ```
   Without an argument it reads the thesis runs in `gem5/m5out`.
3. **Figure**: draws `fig6_5.svg`, `.pdf` and `.png` from `fig6_5.csv` (`--svg-only`
   skips the PDF and PNG, which need Microsoft Edge from Windows).
   ```
   python3 experiments/6.6_overlap/plot.py
   ```

## Expected result

What `parse.py` prints for the thesis runs. The SVG that `plot.py` draws from these
numbers is byte-identical to the thesis Figure 6.5. The L2 miss latency is the
average over sections 1 + 2 (`demandMissLatency / demandMisses`), relative to the
no-overlap run.

```
run                   layer  attention    asleep attn asleep CPU L2 misses
gpt3_hs2_n0_o3    3,594,981  1,934,445   966,387           0        11,109
gpt3_hs2_mm_o3    3,546,063  1,951,539   902,837           0        11,035
gpt3_hs2_att_o3   3,535,938    572,340   859,225           0        11,059
gpt3_hs2_n28_o3   2,875,654  1,222,930 1,043,665      83,161         4,856
gpt3_hs2_n32_o3   2,724,040  1,073,518 1,055,479      95,467         2,310

Reference run (no overlap, all heads on the CPU)
CPU asleep: 26.9% of the layer; Wo + W1 + W2 hold 75.6% of it, QKV 24.4%
Attention: 53.8% of the layer, 2.00x the CPU's total sleep (the PIM is idle in it)

Overlap (sections 1 + 2, N=0)
no overlap                    2,324,677 (+0.0% vs the row above)  L2 miss latency +0.0% (CPU loads +0.0%)
1st overlap: Q, K read-back   2,285,387 (-1.7% vs the row above)  L2 miss latency +0.5% (CPU loads +0.8%)
2nd overlap: scores early     2,263,927 (-0.9% vs the row above)  L2 miss latency +1.9% (CPU loads +3.3%)

Attention heads on PIM (vs N=0)
n28  attention -36.8%, layer -20.0%
n32  attention -44.5%, layer -24.2%
All 32 on PIM: CPU asleep 8.9% of attention; CPU L2 misses x0.21; CPU time per head x0.51 of a CPU head
```
