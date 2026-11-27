# 6.4 Offload granularity: pim.gemv versus pim.dispatch (Table 6.3, Figure 6.3)

The same MAC commands and weight layout on the default O3CPU, issued three ways:
one `pim.gemv` per GEMV (the sequencer sends the MACs), or `pim.dispatch` per MAC
command with a wait after every 32 commands or after every command. The dispatch
runs omit global-buffer fills, READRES and result writes, so they do not compute a
complete GEMV. Matmul cycles = simTicks / 333 over sections 1, 3, 5 and 7.

Programs, in `gem5/configs/scratch/progs/`, all built from `bert/bert_scrub64.c` /
`gpt3/gpt3_scrub64.c` (build lines: `pim_gemv_lab_notes.md`, entry of 2026-10-22):

| | pim.gemv | 32 commands, then wait | 1 command, then wait |
|---|---|---|---|
| BERT | `bert/bert_pim_g7pt_scrub64` | `bert/bert_disp_w32_scrub64` | `bert/bert_disp_scrub64` |
| GPT-3 | `gpt3/gpt3_scr64` | `gpt3/gpt3_disp_w32_scr64` | `gpt3/gpt3_disp_w1_scr64` |

`bert_disp_w192` sends all 192 MAC commands of one 768 x 768 matrix at once; PIMony
aborts when a command reaches a busy engine. That run has no cache scrub.

The dispatch runs keep PIMony's MAC lines in `run.log`, for counting the commands
and for `mac_spacing.py` (in this folder), which finds the shortest time between two
MACs reaching the same engine.

## Steps

Run from the repo root.

1. **Run** the 7 simulations in the background inside `gem5-container`. Existing run
   folders are skipped, so give a new output folder for a fresh copy (`DRY=1` prints only):
   ```
   experiments/6.4_granularity/run.sh m5out/rerun
   ```
2. **Numbers**: prints them and writes `table6_3.csv`, `fig6_3.csv` and `details.csv`.
   ```
   python3 experiments/6.4_granularity/parse.py m5out/rerun
   ```
   Without an argument it reads the thesis runs in `gem5/m5out`.
3. **Figure**: draws `fig6_3.svg`, `.pdf` and `.png` from `fig6_3.csv` (`--svg-only`
   skips the PDF and PNG, which need Microsoft Edge from Windows).
   ```
   python3 experiments/6.4_granularity/plot.py
   ```

## Expected result

What `parse.py` prints for the thesis runs. The SVG that `plot.py` draws from these
numbers is byte-identical to the thesis Figure 6.3. Waits = `pim.wait` sleeps
(quiesce count); 21,760 / 25 = 870 times fewer submissions, 680 / 25 = 27 times
fewer completions than batched `pim.dispatch`.

```
Table 6.3 (O3)
workload   pim.gemv       32 then wait          1 then wait
BERT        236,107    318,229 (1.35x)   2,912,639 (12.34x)
GPT-3     1,584,037  2,844,295 (1.80x)  34,327,451 (21.67x)

Figure 6.3 (GPT-3)
pim.gemv          cycles  1,584,037  asleep    959,983  awake    624,054
pim.dispatch      cycles  2,844,295  asleep    730,362  awake  2,113,933
pimony_same_jobs  cycles    941,105

In the text
bert_pim_g7pt_scrub64_o3   waits      8
bert_disp_w32_scrub64_o3   waits     66  MACs sent  2,112  smallest engine gap 531 DRAM cycles
bert_disp_scrub64_o3       waits  2,112  MACs sent  2,112
pim.wait asleep vs PIMony same jobs (%): 1.2  (bert_pim_g7pt_scrub64_o3)
gpt3_scr64_o3              waits     25
gpt3_disp_w32_scr64_o3     waits    680  MACs sent 21,760  smallest engine gap 599 DRAM cycles
gpt3_disp_w1_scr64_o3      waits 21,760  MACs sent 21,760
pim.wait asleep vs PIMony same jobs (%): 2.0  (gpt3_scr64_o3)
1 then wait, GPT-3: asleep / awake cycles: 20622960/13704491  (gpt3_disp_w1_scr64_o3)
192 at once: MACs PIMony received before aborting: 49  (bert_disp_w192_o3_f2)
```
