# PIMony's own simulator as a reference for pim.gemv

Opened 2026-10-10, closed 2026-10-11. Everything is in this folder:
`pimony_aligned.patch` (all code), `logs/` (output of every PIMony run).
The PIMony copies the runs were made in are untracked: `~/pimony_env/pimony_standalone/`
and `~/pimony_env/pimony_aligned/`. Recreate them with the last block of this file.

**Cycles** are CPU cycles at 3 GHz, the unit of the thesis table
(`tab:eval:granularity`). PIMony counts DRAM cycles; gem5 ticks PIMony every **937 ps**
(`src/mem/pimony.cc:172-173`: tCK 0.9376465 ns x 1000, truncated to a Tick), so
CPU cycles = DRAM cycles x 937 / 333.33. **PIM side** = the time the CPU sleeps in
`pim.wait` = simTicks/333.33 - numCycles per block (on O3 and Minor numCycles counts
active cycles only).

---

## THE ANSWERS · QUOTE FROM HERE

1. **Only the four matmuls compare** (QKV, Wo, W1, W2). PIMony's layer also runs
   attention on PIM; we keep it on the CPU.
2. **PIMony as shipped, GPT-3 matmuls, no CPU: 704,446 cycles. pim.gemv: 1,589,781 = 2.26x**
   (the thesis table's number; summed from stats below it rounds to 1,589,782).
   Same 21,760 MAC commands, same tiling, same DRAM. But PIMony's own run does *less
   work* (no result write-back, among others), so this is a floor, not like-for-like.
3. **Where pim.gemv's extra 885,336 cycles go:** CPU awake 642,658 (73%), PIM side
   slower than PIMony's 242,678 (27%).
4. **PIMony doing exactly our work** (same commands, same addresses, same write-back;
   only the scheduling is PIMony's): GPT-3 **940,174** vs our PIM side **947,124**
   = **+0.7%** (+0.7% to +2.8% across five host CPUs). BERT **91,013** vs **96,363**
   = +5.9% (+2.4% to +8.4% across hosts).
5. **The gap is a fixed cost per pim.gemv call, 275-1,035 cycles in both models**
   (per command it differs 3x between the models), and it moves with the host CPU.
   ⇒ The rigid sequencer (one issue port, MACs in order) costs little against
   PIMony's flexible driver (one port per channel, any free engine). The small rest is
   per call, partly CPU-side time inside "PIM side".

---

## 1 · PIMony as shipped, GPT-3 · 2026-10-10

`pimony_standalone` = PIMony at the repo's initial commit `0157784`, where its own
workload generator still runs (our fork switched it off in `42d0011`, `Request.cc`).
Run in its GEMV mode, one matrix per run: `getGEMVtrace`, the same function its GPT
layer uses for these four steps. Configs `gpt3_{qkv,wo,w1,w2}.json`
(`model_d_interm` = outputs, `model_d_model` = dot length). No CPU traffic trace.

```
matmul   PIMony DRAM cyc   = cycles    pim.gemv O3 (gpt3_pt_o3_f2)   pim.gemv / PIMony
QKV           63,054        177,247          391,729                     2.21
Wo            21,673         60,923          151,621                     2.49
W1            82,847        232,882          485,875                     2.09
W2            83,027        233,388          560,557                     2.40
sum          250,601        704,446        1,589,782                     2.26
```

**Why it is fair:** 21,760 MAC commands on both sides (PIMony's tiling formula; our
`gpt3_disp_w32_o3_f2` run log), the same 1,024-element chunks, 4 outputs per command,
32 engines, and the same DRAM file `LPDDR5X_12Gb_x16_8533_pimony.ini`. PIMSim, the
DRAM timing model, is byte-identical to the one gem5 uses (`diff -rq`); our fork
changed only PIMony's front end in `src/`.

**What PIMony as shipped does not do**, measured in section 3:
- no result write-back: its READRES results are read out and dropped (facts, below);
- fills the input buffer from the host (H2GWRITE), not from DRAM (D2GWRITE);
- QKV as one 7680 x 2560 matrix (3 jobs); we run Q, K, V separately (9 jobs);
- a random DRAM row per tile; our weights sit in consecutive rows;
- no descriptor read.

Each run exited 139 or 134 *after* printing its result: PIMony's `main.cpp` frees two
pointers twice at exit. Harmless; fixed in the aligned copy.

## 2 · Where pim.gemv's time goes · gpt3_pt_o3_f2

```
block        total     CPU awake    PIM side (CPU asleep)
1 QKV       391,729     163,097       228,632
3 Wo        151,621      71,314        80,307
5 W1        485,875     156,468       329,407
7 W2        560,557     251,779       308,778
sum       1,589,782     642,658       947,124
```

Awake = the CPU's work in these blocks: LN1, storing and flushing the input vector,
fences, reading the results back. PIMony has none of it.

## 3 · PIMony doing exactly our work, GPT-3 · 2026-10-11

`pimony_aligned` = `pimony_standalone` + `pimony_aligned.patch`. A new `SEQ` model
replays the pim.gemv jobs a binary issues, command for command as
`memory_system.cc`'s sequencer sends them:

- per job: the descriptor read (two 32 B halves on a channel pair, as every CPU access
  is split); 4x D2GWRITE from `v_base + ch x 32`; then per command in `GemvCmdAddr`
  order: MAC, READRES, WRITE of 8 B at `out_base + j x 2`;
- the 25 jobs and their addresses come from `gpt3_pt` itself: `make_seq_jobs.py`
  reads the symbols with `nm` and applies `gpt3.c`'s `pim_issue` formulas;
- one job at a time, as our sequencer.

Only what was needed to carry our work was changed in PIMony's driver; its
scheduling policy is untouched:
- `Request.cc`: a result WRITE is its own tile in the same slot, after its READRES
  (it can only issue once the readout is back); D2GWRITE is handled like H2GWRITE;
  a trace that ends on an empty tile no longer pushes it (old bug, harmless before);
- `main.cpp`: completion is credited to the issuing channel, since a result write
  lands on another channel; the double free at exit is gone.

```
block   PIMony DRAM cyc  = cycles   our PIM side (o3_f2)   ours / PIMony
QKV          81,489      229,068        228,632               0.998
Wo           28,082       78,939         80,307               1.017
W1          114,119      320,792        329,407               1.027
W2          110,769      311,375        308,778               0.992
sum         334,459      940,174        947,124               1.007
```

**Checks, all passed**
- Same work: requests sent = 2 + 4 + 3 x commands per job, exactly
  (17,334 / 5,778 / 23,058 / 19,260); the writes happened (`write_energy` > 0).
- PIMony itself unchanged: the as-shipped configs, rerun in the aligned build, give
  the same four numbers (63,054 / 21,673 / 82,847 / 83,027).
- The PIM side measures the memory, not the host: the same binary on five host CPUs.
  CPU awake time varies 3.6x; the PIM side stays within 2%.

```
run                    QKV        Wo        W1        W2      PIM side   vs PIMony
PIMony, our work    229,068    78,939   320,792   311,375     940,174
gpt3_pt_minor       234,138    81,051   327,146   314,260     956,596     +1.7%
gpt3_pt_o3          228,695    80,108   328,149   315,578     952,530     +1.3%
gpt3_pt_o3_f2       228,632    80,307   329,407   308,778     947,124     +0.7%
gpt3_pt_o3v61       235,499    83,026   331,748   313,774     964,047     +2.5%
gpt3_pt_o3v61_f2    234,093    83,589   334,166   314,197     966,045     +2.8%
```

**Why the in-order sequencer keeps up:** 65,430 commands over 336,931 DRAM cycles:
the one issue port is busy **19.4%** of the time, and every engine gets identical work,
so the next command in order almost always finds its engine free.

## 4 · The same check on BERT · 2026-10-11

8 jobs per layer (Q, K, V, Wo, W1 one part each, 48 steps; W2 three parts of 64),
2,112 MACs = the table. Two binaries, same weight addresses, different `pimout`:
`bert_pim_g7` (the table's 256,300) and `bert_pim_g7pt` (has runs on five hosts).
Job lists from `make_seq_jobs_bert.py`, using `bert.c` as it built them (`d415987`,
`e33578c`; `pim_issue` identical in both). Requests sent exactly as predicted:
1,746 / 582 / 2,310 / 1,746.

```
run                     QKV       Wo       W1       W2    PIM side   vs PIMony
PIMony, g7 work      22,707    7,803   32,063   28,439     91,013
bert_pim_g7_o3       27,993    8,801   32,989   26,988     96,771     +6.3%
bert_pim_g7_o3_f2    25,643    7,653   30,939   32,128     96,363     +5.9%
PIMony, g7pt work    23,036    7,874   30,770   28,560     90,240
bert_pim_g7pt_minor  25,047    8,120   31,130   28,140     92,437     +2.4%
bert_pim_g7pt_o3     25,026    8,365   30,872   29,432     93,695     +3.8%
bert_pim_g7pt_o3_f2  25,660    7,230   32,563   29,596     95,049     +5.3%
bert_pim_g7pt_o3v61  24,452    7,972   30,461   31,864     94,750     +5.0%
bert_pim_g7pt_o3v61_f2 24,780  9,152   31,273   32,582     97,787     +8.4%
```

Single BERT blocks move up to ±12% between hosts (jobs are ~10x smaller than GPT-3's),
so compare totals.

## 5 · The gap is per call, not per command

```
                     gap per pim.gemv (cycles)      gap per MAC command
GPT-3, 25 calls          278 - 1,035                    0.3 - 1.2
BERT,   8 calls          275 -   943                    1.0 - 3.6
```

Per call the two models agree; per command they differ 3x. So the gap is something
that happens once per pim.gemv, not something the in-order issue adds to every command.
It changes with the host CPU (lowest on Minor, highest on O3 with Minor's vector unit),
so part of it is CPU-side time counted inside the PIM side: the pim.gemv's trip to
memory and the wake-up after the interrupt. BERT's percentage is larger only because
its calls are smaller.

---

## Facts established on the way

- **READRES in PIMony** reads the results out to the memory controller, timed like a
  normal read (`PIMSim/src/pim_controller.cc:653-655`), and then drops them: never
  stored, never delivered (`:79-82` only counts them). This is the gap G6 closed for
  pim.gemv with `out_base` and the write-back. (Said wrongly in the thread first, as
  "PIMony sends results to the host"; corrected.)
- **PIMony's generator fills the buffer only from the host**: `LLM.cc` uses H2GWRITE 5
  times, D2GWRITE never. Why they built it so (does their host keep the results?) is
  not stated in their code or README; their paper would say.
- **PIMony writes are posted**: complete 1 DRAM cycle after acceptance
  (`pim_controller.cc` AddTransaction), on both sides alike.
- **The "PIM Tile = ... cycles" line in PIMony's log is inflated** (buffer fills are
  logged from cycle 0). Quote "GEMV done latency" only.
- Before the aligned run, two parts were estimated: loading the vector from DRAM
  instead of the host < 0.5% (PIMony's timing rules: a fill blocks the channel 128
  cycles either way, DRAM adds one activation); Q, K, V separate instead of fused
  about 3 x Wo = 65,019 vs 63,054 DRAM cycles, < 1%. Both are inside section 3 now.

## Limits / OPEN

- **PIM running alone.** The CPU sleeps during every job. With CPU memory traffic
  during a job, a flexible driver could route around busy channels. Untested.
- **4 channels only.** With 8, the one port must send twice the commands per cycle,
  about 40% busy by estimate. Untested.
- **What is in the 275-1,035 cycles per call** (CPU wake-up vs the sequencer starting a
  job) needs a gem5 run with timestamps. `OPEN`.
- No "as shipped" PIMony run for BERT; only the aligned one.

## For the thesis table (suggestion, not decided)

A row "PIMony's own driver, same commands, no CPU": GPT-3 **940,174**, BERT **91,013**
(both from the table's binaries), against pim.gemv 1,589,781 / 256,300. For the
scheduling question compare it with our PIM side, 947,124 / 96,363.

---

## Reproduce

Every number above, from the saved logs and gem5's stats:

```
cd ~/pimony_env/gem5/configs/scratch/progs/pimony_baseline
# PIMony as shipped (section 1) and all aligned runs (sections 3, 4)
grep -H "GEMV done latency" logs/standalone/gpt3_*.log | awk '{print $1, $(NF-1); s+=$(NF-1)} END{printf "sum %d DRAM cycles = %.0f cycles\n", s, s*937/333.33}'
for p in gpt3 seq seq_bert_g7 seq_bert_g7pt; do for m in qkv wo w1 w2; do grep -h "GEMV done latency" logs/aligned/${p}_$m.log; done | awk -v p=$p '{c=$(NF-1); s+=c; o=o sprintf(" %7d", c)} END{printf "%-14s DRAM%s  sum %7d = %7.0f cycles\n", p, o, s, s*937/333.33}'; done
grep -H "processed memory requests" logs/aligned/seq*.log | awk '{print $1, $NF}'
for f in logs/aligned/seq*.log; do printf "%s " $f; grep -m1 "^write_energy" $f; done

cd ~/pimony_env/gem5/m5out
# section 2: awake / PIM side per block
awk '/Begin Simulation Statistics/{k++} /^simTicks /{t[k]=$2} /^system.cpu.numCycles /{c[k]=$2} END{for(i=1;i<=7;i+=2){T+=t[i]/333.33; C+=c[i]; printf "block %d  total %8.0f  awake %7d  asleep %7.0f\n", i, t[i]/333.33, c[i], t[i]/333.33-c[i]}; printf "sum      total %8.0f  awake %7d  asleep %7.0f\n", T, C, T-C}' gpt3_pt_o3_f2/stats.txt
# sections 3-5: every host against PIMony, total and per call
for d in gpt3_pt_minor gpt3_pt_o3 gpt3_pt_o3_f2 gpt3_pt_o3v61 gpt3_pt_o3v61_f2 bert_pim_g7_o3 bert_pim_g7_o3_f2 bert_pim_g7pt_minor bert_pim_g7pt_o3 bert_pim_g7pt_o3_f2 bert_pim_g7pt_o3v61 bert_pim_g7pt_o3v61_f2; do awk -v d=$d '/Begin Simulation Statistics/{k++} /^simTicks /{t[k]=$2} /^system.cpu.numCycles /{n[k]=$2} END{s=0; printf "%-24s", d; for(i=1;i<=7;i+=2){a=t[i]/333.33-n[i]; s+=a; printf " %9.0f", a}; printf " %10.0f\n", s}' $d/stats.txt; done
L=~/pimony_env/gem5/configs/scratch/progs/pimony_baseline/logs/aligned; p() { for m in qkv wo w1 w2; do grep -h "GEMV done latency" $L/$1_$m.log; done | awk '{s+=$(NF-1)*937/333.33} END{printf "%.0f", s}'; }; for x in "seq 25 gpt3_pt_minor gpt3_pt_o3 gpt3_pt_o3_f2 gpt3_pt_o3v61 gpt3_pt_o3v61_f2" "seq_bert_g7 8 bert_pim_g7_o3 bert_pim_g7_o3_f2" "seq_bert_g7pt 8 bert_pim_g7pt_minor bert_pim_g7pt_o3 bert_pim_g7pt_o3_f2 bert_pim_g7pt_o3v61 bert_pim_g7pt_o3v61_f2"; do set -- $x; P=$(p $1); n=$2; shift 2; for d in "$@"; do awk -v d=$d -v P=$P -v n=$n '/Begin Simulation Statistics/{k++} /^simTicks /{t[k]=$2} /^system.cpu.numCycles /{c[k]=$2} END{s=0; for(i=1;i<=7;i+=2) s+=t[i]/333.33-c[i]; printf "%-24s asleep %7.0f  PIMony %7.0f  gap %+5.1f%%  per pim.gemv %5.0f\n", d, s, P, 100*(s/P-1), (s-P)/n}' $d/stats.txt; done; done
```

Recreate the copy and rerun all 16 PIMony runs (seconds each). The aligned build
reproduces the as-shipped numbers exactly, so one copy is enough:

```
cd ~/pimony_env
mkdir pimony_aligned && git archive 0157784 gem5/ext/dramsim3/PIMony | tar -x -C pimony_aligned --strip-components=4
rsync -a --exclude .git gem5/ext/dramsim3/PIMony/external/ pimony_aligned/external/
(cd pimony_aligned && patch -p1 < ../gem5/configs/scratch/progs/pimony_baseline/pimony_aligned.patch)
# job lists are in the patch; to regenerate them from the binaries:
#   cd pimony_aligned/configs/model_configs && P=~/pimony_env/gem5/configs/scratch/progs && python3 make_seq_jobs.py $P/gpt3/gpt3_pt && python3 make_seq_jobs_bert.py $P/bert_pim_g7 bert_g7 && python3 make_seq_jobs_bert.py $P/bert_pim_g7pt bert_g7pt
docker start gem5-container
docker exec -w /home/pimony/pimony_aligned gem5-container sh -c 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target pimony -j8'
docker exec -w /home/pimony/pimony_aligned gem5-container sh -c 'for p in gpt3 seq seq_bert_g7 seq_bert_g7pt; do for m in qkv wo w1 w2; do d=results/${p}_$m; mkdir -p $d && timeout 3600 build/pimony --mem_config configs/memory_configs/pimony.json --model_config configs/model_configs/${p}_$m.json --log_level off --log_dir $d > $d/simulator.log 2>&1; echo "${p}_$m exit $?"; done; done'
```
