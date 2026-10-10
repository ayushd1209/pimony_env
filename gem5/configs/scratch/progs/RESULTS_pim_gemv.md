# pim.gemv — measured results

Current as of 2026-10-10. Every number here is on the fixed build (O3 `pim.fence`
fix `ed427c0`, token fix `c01ad8a`) and comes with the command that reproduces it from
`gem5/m5out`. The history — how each number got here, and every number it replaced — is
in **`GAPS_pim_gemv.md`** and **`gpt3/GAPS_gpt3.md`** (top blocks first).

Cycles are always `simTicks / 333.33` (3 GHz). Never `numCycles`: on Minor and O3 it
drops the cycles the core sleeps in `pim.wait`.

## 1. PIM vs the best CPU code

Matmul sections only (stats blocks 1/3/5/7). Baseline = OpenBLAS, vectorised, on the
same machine. PIM = one `pim.gemv` per matrix-vector multiply.

```
                    TimingSimple    Minor    O3 default    O3 with Minor's vector unit
BERT layer, SEQ=1       8.43x       16.54x     12.59x            14.13x
GPT-3 2.7B decode         -         28.29x     16.24x            18.95x
```

- **Headline pair: default O3 vs default Minor** (decided 2026-10-02). They also differ in
  vector hardware (O3: 4 SIMD units, opLat 1; Minor: 1 unit, opLat 6); the last column
  removes that difference.
- **The speedup depends on the host.** The PIM run holds a fixed wait while the device
  works (GPT-3: ~950k cycles, the same on Minor and O3). A faster core shrinks the
  CPU-only baseline 2.5x but the PIM run only 1.4x, so the ratio falls.
- **A bigger ratio is not a faster machine:** O3 + PIM finishes first on both workloads
  (GPT-3: 1,589,781 vs 2,290,759 cycles).

```
cd gem5/m5out && for d in bert_blas_fair_timing bert_pim_g7_timing bert_blas_fair_minor bert_pim_g7pt_minor bert_blas_fair_o3_fix bert_pim_g7pt_o3_f2 bert_blas_fair_o3v61 bert_pim_g7pt_o3v61_f2 gpt3_blas_c_minor gpt3_pt_minor gpt3_blas_c_o3 gpt3_pt_o3_f2 gpt3_blas_c_o3v61 gpt3_pt_o3v61_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} /^system.cpu.quiesceCycles /{q[b]=$2} END{printf "%-24s %10d   asleep %8d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33, q[1]+q[3]+q[5]+q[7]}' $d/stats.txt; done
```

Divide each baseline line by the PIM line under it.

## 2. One big instruction vs many small ones (BERT and GPT-3, default O3)

Same matmuls, same weight layout, the PIM does the same 2,112 MAC commands either way.

```
                                       matmul cycles    pim.gemv faster by
pim.gemv (8 instructions)                   256,300           -
pim.dispatch, 32 at a time, then wait       333,455         1.30x
pim.dispatch, 1 at a time, then wait      2,934,992        11.45x
pim.dispatch, all 192 at once               PIMony aborts after 49 commands
```

- **Quote 1.30x** — 32-at-a-time is the fastest `pim.dispatch` schedule that runs.
- `pim.dispatch` still does **less**: no input-vector fill, no readout, no write-back,
  because no instruction exists for those steps. Its answer is never usable.
- `pim.dispatch` has no per-engine completion: the program must know there are 32 engines,
  and sending more crashes the PIM (a second command reached an engine still running its
  first). `pim.gemv`'s sequencer sends a command only when that engine is free.

```
cd gem5/m5out && for d in bert_pim_g7_o3_f2 bert_disp_w32_o3_f2 bert_disp_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} END{printf "%-22s %d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33}' $d/stats.txt; done
for d in bert_disp_w192_o3_f2 bert_disp_w32_o3_f2; do echo "$d  MACs sent: $(grep -ac 'AddTransaction(PIM).*[|] MAC [|]' $d/run.log)"; grep -a -E 'Exiting|PIM State' $d/run.log | sed 's/\x1b\[[0-9;]*m//g'; done
python3 ../configs/scratch/progs/engine_gaps.py bert_disp_w192_o3_f2/run.log
```

**GPT-3 decode, same method (added 2026-10-09)** — details in `gpt3/GAPS_gpt3.md`, top block.

```
                                       matmul cycles    pim.gemv faster by
pim.gemv (25 instructions)                1,589,781           -
pim.dispatch, 32 at a time, then wait     2,862,466         1.80x
pim.dispatch, 1 at a time, then wait     34,310,747        21.58x
```

- Both dispatch runs reached `m5_exit` and sent all 21,760 MACs; config.ini identical to
  `gpt3_pt_o3_f2` bar the binary.
- No MAC reached a busy engine: smallest gap 592 DRAM clk (w32) vs 276 busy. A MAC is busy
  4 x (64 steps + 6-stage pipeline - 1) = 276 clk, not 256; BERT's margin is 531 - 276 = 255.

```
cd gem5/m5out && for d in gpt3_pt_o3_f2 gpt3_disp_w32_o3_f2 gpt3_disp_w1_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} END{printf "%-22s %d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33}' $d/stats.txt; done
for d in gpt3_disp_w32_o3_f2 gpt3_disp_w1_o3_f2; do echo "$d  MACs sent: $(grep -ac 'AddTransaction(PIM).*[|] MAC [|]' $d/run.log)"; grep -a -E 'Exiting|PIM State' $d/run.log | sed 's/\x1b\[[0-9;]*m//g'; done
python3 ../configs/scratch/progs/engine_gaps.py gpt3_disp_w32_o3_f2/run.log
```

## 3. CPU work during the PIM's wait (GPT-3, default O3)

```
                                            block-1 asleep   ticks per L2 miss, blocks 1+2
CPU only sleeps during each job                 234,076           75,296
+ reads results back during the next job        189,022           76,653
+ attention scores during the wait              132,306           79,110
```

- **Overlap works, the prize is small:** reading back during the next job fills 45,054
  cycles of sleep. 79% of all sleep is in matmuls that need the step right before them,
  so one inference stream has nothing to put there.
- **Sharing the memory costs +5.1% per CPU memory access** (first row → last row). With
  PIMony set to PIM-first it is +8.7%, so part of it is the CPU and the PIM competing.
- **Attention in the wait does not pay:** it fills 56,716 cycles of sleep but makes the
  CPU's own work 93,556 cycles slower — blocks 1+2 end 36,840 cycles slower.
- **Open:** PIM-first also makes the PIM's own jobs ~20% slower (GPT-3 +19.8%, BERT
  +12.5%). Cause unknown; the result write-backs are ruled out (`gpt3/GAPS_gpt3.md`).

Commands: top block of `gpt3/GAPS_gpt3.md`.

## 4. Batching — where PIM's case ends (CPU only, BERT layer, O3, OpenBLAS)

PIM wins when the CPU waits for weights. Processing more tokens at once reuses every
weight fetched, so the CPU waits less per multiply.

```
tokens   cycles/token   DRAM read    GB/s    multiply-adds per byte    IPC
     1     5,588,703      28.4 MB    15.28        0.25                0.22
     4     5,773,517      29.3 MB     3.80        0.97                0.36
    16     1,969,164      32.9 MB     3.13        3.46                0.80
    64     1,077,886      39.3 MB     1.71       11.69                1.41
   128     1,027,648      52.3 MB     1.19       17.79                1.52
   256       932,491      86.1 MB     1.08       22.22                1.79
   384       977,472     152.5 MB     1.22       19.31                1.82
   512     1,039,491     227.0 MB     1.28       17.74                1.83
```

- **Math per byte rises 71x from 1 to 128 tokens and peaks at 256 (89x).** PIM's case is
  strongest at 1 token — GPT-3 decode — and gone by 64.
- **BERT's usual lengths, 128–512, are the case least favourable to PIM:** 18–22
  multiply-adds per byte, ~1.1–1.3 GB/s read, and the highest IPC of the sweep. The BERT
  rows in section 1 are a BERT-sized layer at 1 token, not BERT inference.
- **Past 256 the intensity falls back,** because DRAM traffic grows faster than the work:
  the activations stop fitting in the 2 MiB L2. Dirty write-backs, which can only be
  activations, grow 8x from 128 to 512 tokens (17.8 → 142.7 MB) for 4x the tokens — the
  superlinear part is attention's score table (tokens²). This baseline stores the whole
  table, as unfused attention does; a fused kernel would avoid most of that traffic.
  ⚠️ (2026-10-10) The score-table part is unverified: these runs have no per-step split.
  On GPT-3 (4b) the split puts the extra reads in the matrix multiplies, not attention.

```
cd gem5/m5out && for p in "l2big-s1 1" "l2big-s4 4" "l2big-s16 16" "l2big-s64 64" "o3-s128-l2big 128" "l2big-s256_oct 256" "l2big-s384_oct 384" "l2big-s512_oct 512"; do set -- $p; awk -v d=$1 -v n=$2 '/Begin Simulation Statistics/{b++} b==1&&/^system.membus.transDist::ReadSharedReq /{r=$2} b==1&&/^system.membus.transDist::WritebackDirty /{w=$2} b==1&&/^simSeconds /{s=$2} b==1&&/^system.cpu.numCycles /{c=$2} b==1&&/^simInsts /{i=$2} END{by=r*64; macs=n*7077888+2*n*n*768; printf "%-15s tokens %3d  cyc/token %9.0f  DRAM read %6.1f MB  %5.2f GB/s  MACs/byte %6.2f  IPC %.2f  written back %6.1f MB\n", d, n, c/n, by/1e6, by/s/1e9, macs/by, i/c, w*64/1e6}' $1/stats.txt; done
```

`numCycles` is fine here: no PIM, so the core never sleeps. Multiply-adds = 7,077,888
per token (BERT's six weight matrices) + 2 x tokens² x 768 (attention). DRAM read =
`membus.transDist::ReadSharedReq` x 64 B. Runs 1–128 are from August; 256–512 were
built on 2026-10-03 against the September OpenBLAS install (`bert_blas_s*_oct`), and the
same rebuild at 128 tokens (`l2big-s128_oct`) reproduces August within 0.012%.

## 4b. Prefill vs decode — the same question on GPT-3 (CPU only, O3, OpenBLAS, FP16)

Decode makes one token at a time (matrix-vector); prefill runs the whole prompt at once
(matrix-matrix). Same machine and precision as section 1's GPT-3 baseline. Prefill comes
from `gpt3/gpt3_prefill.c`, a copy of the decode program with only attention changed
(causal, empty KV cache).

```
                     tokens   cycles/token   DRAM read    GB/s   multiply-adds per byte    IPC
decode (256 cached)      1     27,401,792     163.3 MB   17.90           0.49             0.50
prefill                  4     44,959,560     200.5 MB    3.35           1.57             0.37
prefill                 16     13,563,564     211.8 MB    2.93           5.95             0.69
prefill                 64      5,913,266     295.7 MB    2.35          17.09             1.40
prefill                128      4,754,784     420.4 MB    2.07          24.14             1.73
prefill                256      4,605,617   1,073.9 MB    2.74          19.06             1.81
prefill                512      4,462,854   2,235.9 MB    2.94          18.61             1.95
```

- **Decode is where PIM's case is:** half a multiply-add per byte read, at 17.9 GB/s — the
  CPU waits on memory. It reads the weights and the KV cache once each.
- **Prefill leaves that regime fast:** 35x the work per byte at 64 tokens, ~2–3 GB/s, IPC
  1.4 and up. At PIMony's prompt length (256 tokens) it is 39x. This is the CPU side only —
  no PIM run uses more than one token.
- **The ratio peaks at 128 tokens and falls back to ~19.** The extra DRAM reads are in the
  matrix multiplies, not attention: at 512 tokens W1 reads 950 MB for 52 MB of weights,
  attention 98.5 MB. Inferred: their working data outgrows the 2 MiB L2.
- **Per token,** 4-token prefill costs 1.64x decode; from 128 tokens on, 0.16–0.17x.
- Checked: prefill is built byte-for-byte like the decode baseline; attention matches a
  double-precision reference; token 0's output is identical in all six prefill runs; the
  decode rerun is bit-identical to September's. Details, per-step table and every command:
  `gpt3/GAPS_gpt3.md`, top block.

```
cd gem5/m5out && for p in "gpt3_blas_c_o3_oct 1" "gpt3_prefill_s4_o3_oct 4" "gpt3_prefill_s16_o3_oct 16" "gpt3_prefill_s64_o3_oct 64" "gpt3_prefill_s128_o3_oct 128" "gpt3_prefill_s256_o3_oct 256" "gpt3_prefill_s512_o3_oct 512"; do set -- $p; awk -v d=$1 -v n=$2 '/Begin Simulation Statistics/{b++} b>=1&&b<=9&&/^system.membus.transDist::ReadSharedReq /{r+=$2} b>=1&&b<=9&&/^simTicks /{t+=$2} b>=1&&b<=9&&/^system.cpu.numCycles /{c+=$2} b>=1&&b<=9&&/^simInsts /{i+=$2} END{by=r*64; macs=(d~/blas_c/)? 78643200+2*257*2560 : n*78643200+2*n*n*2560; printf "%-26s tokens %3d  cyc/token %11.0f  DRAM read %7.1f MB  %5.2f GB/s  MACs/byte %6.2f  IPC %.2f\n", d, n, c/n, by/1e6, by/(t*1e-3), macs/by, i/c}' $1/stats.txt; done
```

Whole layer = stats blocks 1–9. Multiply-adds = 78,643,200 per token (GPT-3's six weight
matrices) + 2 x tokens² x 2560 (attention, full S x S as computed); decode adds
2 x 257 x 2560. DRAM read as in section 4. BERT (section 4) is FP32, this is FP16 — FP16
alone doubles multiply-adds per byte, so do not overlay the two curves unlabelled.

## 5. Validation

- **The offload does the right work** (BERT, `m5out/bert_pim_g7pt_o3_wlog`): 2,112 MAC and
  2,112 READRES commands reach the PIM, and the CPU sleeps exactly 8 times — once per
  `pim.gemv`. 110,592 MAC steps = 7,077,888 weights / 64 per step (GAPS G7).
- **The instructions behave the same on every CPU:** 9 tests x {TimingSimple, Minor, O3}
  all finish, and for every test the PIM receives the identical command list from all
  three CPUs (`m5out/fix2_*`).
- **`pim.fence.inv` really invalidates on O3 now:** the reload after the fence goes to DRAM
  (2 accesses to the line, 3 for `.flush`) on all three CPUs; before the fix O3 read the
  stale copy (1 / 2).

```
cd gem5/m5out && d=bert_pim_g7pt_o3_wlog; echo "MAC $(sed 's/\x1b\[[0-9;]*m//g' $d/run.log | grep -a -c 'AddTransaction(PIM).*| MAC |')  READRES $(sed 's/\x1b\[[0-9;]*m//g' $d/run.log | grep -a -c 'AddTransaction(PIM).*| READRES |')  sleeps $(awk '/^system.workload.inst.quiesce /{s+=$2} END{print s}' $d/stats.txt)"
for t in asid e2e fence_cbo gemv group issue order_f order_nf sweep; do printf "%-10s" $t; for c in timing minor o3; do h=$(sed 's/\x1b\[[0-9;]*m//g' fix2_${t}_$c/run.log | grep -a -o 'AddTransaction(PIM)\] cid: [0-9]* clk: [0-9]* | [A-Z0-9]* | addr: [0-9A-FX]*' | sed -E 's/clk: [0-9]+ //' | sort | md5sum | cut -c1-6); e=$(grep -a -c 'Exiting @' fix2_${t}_$c/run.log); printf "  %-6s exit=%s cmds=%s" $c $e $h; done; echo; done
for r in ver fix2; do for c in timing minor o3; do d=${r}_fence_cbo_$c; printf "%-22s inv line: %s   flush line: %s\n" $d $(grep -c "Enqueueing address $((0x81000140))$" $d/dbg.log) $(grep -c "Enqueueing address $((0x81000240))$" $d/dbg.log); done; done
```

## 6. Known limits

1. **PIMony computes no values.** Only the matmul sections are comparable; branchy CPU
   code after them diverges, so whole-layer ratios are unsound on pipelined CPUs.
2. **One layer, batch 1.** Per-layer numbers; see section 4 for why batch 1 is the case.
3. **Result width is our assumption** (FP16, 2 B per result); the paper gives none.
4. **Fences cost 22.7% of BERT PIM matmul time on O3** (upper bound for a range fence;
   design parked) — GAPS G8.1.
5. **One `pim.gemv` at a time** — no sharing between processes (GAPS G10).

## Build and run

Binaries, build lines and run lines: `README.md` in this folder. Every run's
`config.ini` is what was actually simulated — check it, not the config script.
