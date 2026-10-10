# GPT3-2.7B decode port, fairness audit, and the first overlap result

Opened 2026-09-21. Everything here is in `configs/scratch/progs/gpt3/`.
**The BERT work is untouched** — `bert.c`, `bert_blas.c`, all `m5out/bert_*`
and `m5out/blas_*` runs are exactly as they were. This is a parallel tree.

Status: `CLOSED` · `OPEN`

---

## PREFILL vs DECODE — CPU-only token sweep · 2026-10-10 · default O3 · QUOTE FROM HERE

The BERT batching sweep (`../RESULTS_pim_gemv.md` §4) repeated on the GPT3-2.7B layer, to
show where decode (GEMV) and prefill (GEMM) sit. **CPU only, no PIM.** Same machine as the
GPT-3 decode baseline: `se_bert.py o3` (L2 2 MiB, default vector unit), OpenBLAS ZVL256B,
**FP16 weights** (`-DBLAS_FP16`, the precision of the PIM comparison).

- **Decode** = `gpt3_blas.c`, unchanged (binary `gpt3_blas_c`): 1 token, 256 cached tokens.
- **Prefill** = new `gpt3_prefill.c`: a copy of `gpt3_blas.c` with only attention changed —
  S prompt tokens in one pass, empty KV cache, causal (token i sees 0..i). QK^T and att·V
  are full S x S `cblas_sgemm`, as PyTorch's eager path does. A separate file so that no
  existing GPT-3 run can change (Ayush, 2026-10-09).

```
                     tokens   cycles/token   DRAM read    GB/s   MACs/byte   IPC    L1D miss   written back
decode (256 cached)      1     27,401,792     163.3 MB   17.90      0.49     0.497   54.1%         2.4 MB
prefill                  4     44,959,560     200.5 MB    3.35      1.57     0.367   27.6%         5.2 MB
prefill                 16     13,563,564     211.8 MB    2.93      5.95     0.694   22.9%        15.1 MB
prefill                 64      5,913,266     295.7 MB    2.35     17.09     1.404   16.1%        88.4 MB
prefill                128      4,754,784     420.4 MB    2.07     24.14     1.726   13.7%       197.6 MB
prefill                256      4,605,617   1,073.9 MB    2.74     19.06     1.806   11.6%       713.6 MB
prefill                512      4,462,854   2,235.9 MB    2.94     18.61     1.947   10.8%     1,484.4 MB
```

DRAM reads per layer step (MB). Weights alone, FP16: QKV 39.3 · Wo 13.1 · W1 52.4 · W2 52.4.

```
                     LN1+QKV   attention     Wo      W1      W2    other   attention share of time
decode (256 cached)     39.5        5.5     13.2    52.6    52.5     0.1          5.3%
prefill    4            50.1        0.1     16.6    67.3    66.1     0.2          0.1%
prefill   16            52.6        0.5     17.3    72.7    67.9     0.9          0.3%
prefill   64            66.0        2.8     21.7   122.4    78.4     4.5          1.8%
prefill  128            97.4        7.0     31.9   179.4    92.1    12.7          4.2%
prefill  256           242.2       23.5     79.0   393.6   307.7    27.9          8.4%
prefill  512           435.1       98.5    141.6   950.2   553.5    57.0         17.5%
```

**Findings**

1. **Decode is memory-bound, prefill is not.** Decode: 0.49 MACs per DRAM byte at 17.9 GB/s,
   IPC 0.50; it reads the weights and the KV cache (5.3 MB) once each. Prefill from 64
   tokens: 17–24 MACs/byte (**35–49x decode**), 2.1–2.9 GB/s, IPC 1.4–1.9. At PIMony's
   prompt length (input 256): 19.06 MACs/byte (**39x**), 2.74 GB/s. ⇒ PIM's case is decode;
   for prefill it is gone by 64 tokens. **CPU side only** — no PIM run uses more than one token.
2. **Per token,** prefill costs 0.16–0.17x decode from 128 tokens on (weight reuse) — but 4 tokens
   cost **1.64x** decode per token, 16 tokens 0.49x. BERT leaned the same way at 4 tokens
   (1.03x, §4).
   Cause (small GEMMs not paying for their setup) is inferred, not traced.
3. **MACs/byte peaks at 128 tokens (24.14) and falls back to ~19 at 256 and 512.** The extra
   DRAM reads are in the matrix multiplies, not attention: at 512, W1 reads 950 MB for 52 MB
   of weights; attention reads 98.5 MB. MEASURED: which step. INFERRED: the GEMMs' working
   data that grows with tokens (activations, FP32 partial sums) outgrows the 2 MiB L2 and is
   re-read; OpenBLAS's blocking was not traced.
   ⚠️ `../RESULTS_pim_gemv.md` §4 attributes BERT's fall-back past 256 to attention's score
   table. BERT had no per-step split, so that is unverified; on GPT-3 the split points at the
   matrix multiplies. `OPEN` — settle before the thesis text uses either.
4. **Attention's share of prefill time grows with tokens:** 0.1% (4) → 1.8% (64) → 4.2%
   (128) → 8.4% (256) → 17.5% (512). Decode with 256 cached tokens: 5.3%.

**Checks — all passed**

- **Built like decode:** `gpt3_blas.c` rebuilt with the prefill flags is `cmp`-IDENTICAL to
  `gpt3_blas_c` (GCC 16.1.0, OpenBLAS install of 2026-09-14).
- **Attention correct:** tiny build (HIDDEN 320, HEADS 4 ⇒ 80-wide heads like the real
  model, FFN 1280, SEQ 16, `-DCHECK_ATTN`) on TimingSimple: max |ctx − double-precision
  reference| = **1.50e-05 → PASS** (`m5out/gpt3_prefill_check`; stdout not kept — rerun below).
- **Causal at full size:** `out[0][0..3] = 731.944153 739.319336 738.282043 729.162842` in
  all six prefill runs — token 0's result does not depend on how many tokens follow it.
- **Same simulator:** the decode rerun on today's gem5 (`gpt3_blas_c_o3_oct`) is
  bit-identical to the 2026-09-23 run (`gpt3_blas_c_o3`): simTicks and simInsts of every
  block. All earlier GPT-3 decode numbers stand.
- **FP16 GEMM runs on O3:** first O3 run of `shgemm` (`m5out/fp16_gemm_o3`,
  `bert_blas_s16_fp16`) commits the same instructions as TimingSimple (`m5out/blas_fp16`):
  23,604,855, of them 18,279,578 vector. Its `vlseg2/4/8e16` loads are what crashed O3 in
  August (scratch-register fix `e00025b`).
- All seven runs end `exiting with last active thread context`.

**Definitions.** ROI = stats blocks 1–9 (eight `PHASE_STATS` blocks + a 1-instruction tail);
block 10 (after the ROI) is excluded. Cycles = `numCycles` (= simTicks / 333; no PIM, so the
core never sleeps). DRAM read = `membus.transDist::ReadSharedReq` x 64 B, as §4 (DRAMsim3's
own counters do not reset). MACs = tokens x 78,643,200 (six projections) + 2 x tokens² x 2560
(QK^T and att·V, full S x S as computed); decode = 78,643,200 + 2 x 257 x 2560. MB = 10⁶ B.

**Build and run** (as used):

```
docker exec -w /home/pimony/gem5/configs/scratch/progs/gpt3 gem5-container sh -c '
CC=/opt/riscv/bin/riscv64-unknown-linux-gnu-gcc
F="-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -static -DBLAS_FP16 -DPHASE_STATS -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math -I/opt/openblas-zvl256b/include"
L="/opt/openblas-zvl256b/lib/libopenblas.a -lm"
$CC $F -o /tmp/blas_c_check gpt3_blas.c $L && cmp /tmp/blas_c_check gpt3_blas_c && echo IDENTICAL
for s in 4 16 64 128 256 512; do $CC $F -DSEQ=$s -o gpt3_prefill_s$s gpt3_prefill.c $L || exit 1; done
$CC $F -DHIDDEN=320 -DHEADS=4 -DFFN=1280 -DSEQ=16 -DCHECK_ATTN -o gpt3_prefill_check gpt3_prefill.c $L'

cd ~/pimony_env/gem5
docker exec -w /home/pimony/gem5 gem5-container sh -c 'build/RISCV/gem5.opt -d m5out/gpt3_prefill_check configs/pimony/se_bert.py timing configs/scratch/progs/gpt3/gpt3_prefill_check 2>&1 | grep -a -E "attention check|Exiting"'
for chain in "prefill_s512" "prefill_s256 prefill_s64 prefill_s4" "prefill_s128 prefill_s16 blas_c"; do
  docker exec -d -w /home/pimony/gem5 gem5-container sh -c "for b in $chain; do d=m5out/gpt3_\${b}_o3_oct; mkdir \$d && build/RISCV/gem5.opt -d \$d configs/pimony/se_bert.py o3 configs/scratch/progs/gpt3/gpt3_\$b 2>&1 | grep -a -E 'workload|prefill|layer|out\[0|checksum|Exiting|panic|fatal|egmentation' > \$d/run.log; done"
done
```

⚠️ **Filter stdout.** PIMony prints every DRAM command, ~5 GB per hour per run. The first
start (2026-10-09 17:31) wrote it to run.log unfiltered — 21 GB in 77 min — and was killed;
the runs above are the restart at 18:51. Each run spends 35–65 min outside the ROI, nearly
all of it generating weights (slower while three run at once); the 512-token run took
9 h 23 min wall (18:51 → 04:14). The check run above overwrites
`m5out/gpt3_prefill_check` — use another `-d` to keep the original.

**Reproduce** (from `gem5/m5out`):

```
for p in "gpt3_blas_c_o3_oct 1" "gpt3_prefill_s4_o3_oct 4" "gpt3_prefill_s16_o3_oct 16" "gpt3_prefill_s64_o3_oct 64" "gpt3_prefill_s128_o3_oct 128" "gpt3_prefill_s256_o3_oct 256" "gpt3_prefill_s512_o3_oct 512"; do set -- $p; awk -v d=$1 -v n=$2 '/Begin Simulation Statistics/{b++} b>=1&&b<=9&&/^system.membus.transDist::ReadSharedReq /{r+=$2} b>=1&&b<=9&&/^system.membus.transDist::WritebackDirty /{w+=$2} b>=1&&b<=9&&/^simTicks /{t+=$2} b>=1&&b<=9&&/^system.cpu.numCycles /{c+=$2} b>=1&&b<=9&&/^simInsts /{i+=$2} b>=1&&b<=9&&/^system.cpu.dcache.overallMisses::total /{m+=$2} b>=1&&b<=9&&/^system.cpu.dcache.overallAccesses::total /{a+=$2} END{by=r*64; macs=(d~/blas_c/)? 78643200+2*257*2560 : n*78643200+2*n*n*2560; printf "%-26s tokens %3d  cyc/token %11.0f  DRAM read %7.1f MB  %5.2f GB/s  MACs/byte %6.2f  IPC %.3f  L1D miss %4.1f%%  written back %7.1f MB\n", d, n, c/n, by/1e6, by/(t*1e-3), macs/by, i/c, 100*m/a, w*64/1e6}' $1/stats.txt; done
for d in gpt3_blas_c_o3_oct gpt3_prefill_s{4,16,64,128,256,512}_o3_oct; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^system.membus.transDist::ReadSharedReq /{r[b]=$2*64/1e6} /^simTicks /{t[b]=$2} END{T=0; for(k=1;k<=9;k++) T+=t[k]; printf "%-26s QKV %6.1f  attn %6.1f  Wo %6.1f  W1 %6.1f  W2 %6.1f  other %5.1f   attention = %4.1f%% of time\n", d, r[1], r[2], r[3], r[5], r[7], r[4]+r[6]+r[8]+r[9], 100*t[2]/T}' $d/stats.txt; done
for d in gpt3_blas_c_o3 gpt3_blas_c_o3_oct; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} /^simInsts /{i[b]=$2} END{printf "%-20s", d; for(k=1;k<=9;k++) printf " %d/%d", t[k], i[k]; print ""}' $d/stats.txt; done   # two identical lines
for s in 4 16 64 128 256 512; do printf "%-4s" $s; grep -a 'out\[0' gpt3_prefill_s${s}_o3_oct/run.log; done   # six identical lines
grep -a -c 'exiting with last active thread context' gpt3_*_o3_oct/run.log   # 1 each
for d in blas_fp16 fp16_gemm_o3; do awk -v d=$d '/Begin Simulation Statistics/{b++} b==1 && /^simInsts /{i=$2} b==1 && /^system.cpu.commitStats0.numVecInsts /{v=$2} END{printf "%-14s ROI insts %d  vector insts %d\n", d, i, v}' $d/stats.txt; done
```

**Open / not done**

- No PIM run uses more than one token: the prefill conclusion is from the CPU side only.
- One layer, batch 1. No 384-token point (BERT has one).
- Attention computes the full S x S, masked half included, like PyTorch's eager path; a
  causal-aware or fused kernel would cut attention's MACs and its 17.5% share at 512.
- BERT §4 is FP32, this is FP16 — FP16 alone doubles MACs/byte at equal reuse. Do not put
  the two curves on one plot without saying so.
- 4-token prefill reads ~27% more than the weights in every matrix multiply (QKV 50.1 vs
  39.3 MB). Not explained.

---

## PIM.GEMV vs PIM.DISPATCH ON GPT-3 — 2026-10-08/09 · default O3 · G9 repeated

Same method as `../GAPS_pim_gemv.md` G9 (BERT). `gpt3.c` unchanged: its plain build is
byte-identical to `gpt3_pt` (cmp), and the dispatch builds add only `-DPIM_BY_DISPATCH
-DPIM_WAVE=n`. config.ini identical to `gpt3_pt_o3_f2` bar the binary. Both runs exited
with `m5_exit`, no `PIM State` abort.

```
GPT-3 decode, matmul blocks 1/3/5/7     matmul cycles   pim.gemv faster by   waits   (BERT)
pim.gemv            (gpt3_pt_o3_f2)          1,589,781          -              25
pim.dispatch, 32 then wait (gpt3_disp_w32)   2,862,466        1.80x           680     1.30x
pim.dispatch, 1 then wait  (gpt3_disp_w1)   34,310,747       21.58x        21,760    11.45x
```

- **MACs sent: 21,760 in both**, = matrix shapes. One command = 4 outputs, one dot <= 1,024:
  Wq/Wk/Wv/Wo 4 x (640 x 3 parts) = 7,680; W1 2,560 x 3 = 7,680; W2 640 x 10 = 6,400.
  32 engines x 680 each, 0 paused+resumed (BERT w32: 34).
- **No MAC reached a busy engine.** Smallest gap, previous MAC on that engine last started
  -> next arrives: w32 **592** DRAM clk, w1 12,356, vs **276** busy (BERT w32: 531).
- **Busy = 276, not 256.** 64 steps x tCCD_L 4 = 256 is only the steps going in; the 6-stage
  MAC pipeline drains 5 more: 4 x (64 + 6 - 1) = 276 (pim_controller.cc:675-676, READRES
  gate timing.cc:95). Margins: GPT-3 w32 316, BERT w32 255 (G9 says 275). `engine_gaps.py`
  label corrected 256 -> 276; the measured gaps did not change.
- 32-at-a-time is safe here **by timing, not by guarantee** (same as G9). 1-at-a-time is the
  safe schedule.

```
P=~/pimony_env/gem5/configs/scratch/progs/gpt3; cd $P
B="-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math -DFREESTANDING -DNO_IO -DBAREMETAL -DPIM_LAYOUT -DPIM_GEMV -DPHASE_STATS -nostdlib -nostartfiles -T gpt3_bm.ld"
riscv64-unknown-elf-gcc $B -o /tmp/check_pt gpt3.c sgemv_n.o sgemv_t.o -lgcc && cmp /tmp/check_pt gpt3_pt && echo IDENTICAL
riscv64-unknown-elf-gcc $B -DPIM_BY_DISPATCH -DPIM_WAVE=32 -o gpt3_disp_w32 gpt3.c sgemv_n.o sgemv_t.o -lgcc   # =1 -> gpt3_disp_w1
docker exec -w /home/pimony/gem5 gem5-container sh -c 'd=m5out/gpt3_disp_w32_o3_f2; mkdir $d && build/RISCV/gem5.opt -d $d configs/pimony/fs_bert.py o3 configs/scratch/progs/gpt3/gpt3_disp_w32 2>&1 | grep -a -E "[|] MAC [|]|Exiting|PIM State|panic|fatal" > $d/run.log'

cd ~/pimony_env/gem5/m5out && for d in gpt3_disp_w32_o3_f2 gpt3_disp_w1_o3_f2; do diff gpt3_pt_o3_f2/config.ini $d/config.ini; grep -a -E 'Exiting|PIM State|panic|fatal' $d/run.log | sed 's/\x1b\[[0-9;]*m//g'; echo "MACs sent: $(grep -ac 'AddTransaction(PIM).*[|] MAC [|]' $d/run.log)"; python3 ../configs/scratch/progs/engine_gaps.py $d/run.log; done
for x in gpt3_pt_o3_f2 gpt3_disp_w32_o3_f2 gpt3_disp_w1_o3_f2; do awk -v d=$x '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} /^system.workload.inst.quiesce /{s+=$2} END{printf "%-22s %d  waits %d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33, s}' $x/stats.txt; done
```

The `PIM_WAVE` comment in `gpt3.c` ("32 hangs") is stale: that was the token bug, fixed
2026-10-03. Not edited: `gpt3.c` was kept untouched for this measurement.

---

## THE NUMBERS — 2026-10-02 · after the O3 fence fix · QUOTE FROM HERE

⚠️ **Every O3 PIM figure below dated before 2026-10-02 is superseded.** O3's `pim.fence`
let later accesses run before its clean/invalidate (`../GAPS_pim_gemv.md` G13, fixed in
`ed427c0`). Minor and the baselines are unchanged. Headline pair (Ayush, 2026-10-02):
**default O3 vs default Minor**; they differ in vector hardware too.

```
GPT-3 decode, matmul blocks 1/3/5/7     baseline         PIM     speedup   (before fix)
Minor                                 64,814,111   2,290,760     28.29x    28.29x
O3 default                            25,823,931   1,589,782     16.24x    19.51x
O3, Minor's vector unit               30,558,375   1,612,483     18.95x    22.81x
```

O3 PIM time +20% (fences now serialize, as on Minor); instructions identical, PIM sleep
−0.5% / +0.2%. Minor's lead over O3 grows, and O3 + PIM is still fastest in absolute time.
Reproduce with the one-liner in `../GAPS_pim_gemv.md` (top block); runs
`m5out/gpt3_pt_o3{,v61}_f2`.

## OVERLAP AND INTERFERENCE — RERUN 2026-10-03 on the fixed O3 · QUOTE FROM HERE

Same binaries as 2026-09-24/30, same config.ini; only gem5 changed (`ed427c0`). All five
runs exited with `m5_exit`. Runs `m5out/gpt3_{v2_ctl,v2_ov,att}_o3_f2`,
`m5out/gpt3_{v2_ctl,att}_pimfirst_o3_f2`.

```
OVERLAP (block 1: read Q back under K's job, K under V's)    control     overlap
  block-1 asleep                                              234,076     189,022   -45,054 filled  (was 21,369)
  sleep in blocks 3+5+7 (nothing independent to run)          727,462     721,637   = 79% of all sleep (was 77%)

INTERFERENCE, blocks 1+2, avg L2 miss latency (ticks)     no work in wait   + read-back   + attention
  CPU first (MEM_FIRST, the default)                          75,296          76,653        79,110
  PIM first (PIM_FIRST)                                       75,211            -           81,754
```

- **Overlap:** it works, the prize is small. 45,054 cycles of block-1 sleep filled; the
  other matmuls each need the step right before them, so 79% of the sleep cannot be
  filled by one inference stream.
- **Cost of sharing the memory (quote this): +5.1% per L2 miss**, sleep-only control →
  attention in the wait, CPU first (was 6.54%). PIM first: **+8.7%** (was 8.46%), so ~3.6
  points are policy-sensitive contention (was ~2). Adding the attention step on top of the
  read-back alone: +3.2% (was 4.69%). Ayush, 2026-10-03: 5.1% is the number for "what
  sharing costs"; 3.2% is only the marginal step.
- **Attention in the wait does not pay:** it fills 56,716 cycles of sleep, but the CPU's own
  work gets 93,556 cycles slower, so blocks 1+2 end **36,840 cycles slower** (2,447,541 →
  2,484,381; was +25,061).
- **PIM first slows the PIM's own jobs +19.8%** (was 20.6%) — open; the write-back suspect
  is ruled out (G-INT, last section).

```
cd gem5/m5out && for d in gpt3_v2_ctl_o3_f2 gpt3_v2_ov_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^system.cpu.quiesceCycles /{q[b]=$2} END{r=q[3]+q[5]+q[7]; printf "%-18s asleep in block 1: %7d   in blocks 3+5+7: %7d of %7d (%.0f%%)\n", d, q[1], r, q[1]+r, 100*r/(q[1]+r)}' $d/stats.txt; done
for d in gpt3_v2_ctl_o3_f2 gpt3_v2_ov_o3_f2 gpt3_att_o3_f2 gpt3_v2_ctl_pimfirst_o3_f2 gpt3_att_pimfirst_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} /^system.cpu.quiesceCycles /{q[b]=$2} /^system.l2cache.demandMissLatency::total /{l[b]=$2} /^system.l2cache.demandMisses::total /{m[b]=$2} END{c=(t[1]+t[2])/333.33; printf "%-28s blocks1+2 %8d  asleep %7d  busy %8d  ticks/miss %6.0f\n", d, c, q[1], c-q[1]-q[2], (l[1]+l[2])/(m[1]+m[2])}' $d/stats.txt; done
```

⚠️ One difference from 09-24: the attention run now also makes 3.0% more L2 misses than
`v2_ov` (57,476 → 59,216; was +0.03%), so "same misses, each dearer" holds only roughly.

## THE NUMBERS — 2026-09-24, o3, cycles from simTicks (3 GHz, /333.33)

⚠️ Superseded for O3 PIM figures — see the 2026-10-02 block above. The +4.7% and
"~2 points" below are superseded by the 2026-10-03 rerun block above (+5.1% / +8.7%).

**THE HEADLINE RESULT OF THE OVERLAP WORK IS NOT A SPEEDUP — see G-INT.** Putting
host work into the PIM wait window costs **+4.7% on every host memory access**,
the first time this platform has had PIM and CPU touching DRAM at once.
2026-09-30: flipping PIMony to PIM-first moves that penalty by ~2 points, so part of
it is PIM/CPU contention (G-INT, policy split). PIM-first also livelocked one run
(a PIMony model bug) and made the PIM jobs themselves 20% slower (open).

**Quote matmul-only.** Blocks 1/3/5/7. See "whole-layer is unsound" below —
that is not the old G3 style-of-caution, it is a hard result.

### 2026-09-30 — GPT-3 ON MINOR: THE HOST ORDER FLIPS AGAINST BERT

Binary `gpt3_pt`: G12's page-table preload ported to `gpt3.c` (`GIGAPAGE`, stores
kept). Its `main` also carries the 2026-09-25 attention rework, so it is not
byte-identical to `gpt3_dec_b` (text 7692 vs 7818 B; flags verified identical).
Matmul blocks 1/3/5/7, cycles from simTicks:

```
                                 baseline        PIM     matmul    sleep / wakes
  Minor (in order)             64,814,111  2,290,760     28.29x   958,866 / 25
  O3, Minor's vector unit      30,558,375  1,339,410     22.81x   965,374 / 25   <- the comparison
  O3, gem5 default             25,823,931  1,323,866     19.51x   953,842 / 25
  O3, published (gpt3_dec_b)   25,823,931  1,349,787     19.13x   957,718 / 25   reproduces exactly
```

**On BERT the out-of-order core showed the bigger speedup (17.53x vs 16.54x). On
GPT-3 the in-order core does, by 24%.** Both halves of the ratio move, both measured:

- **The baseline gains more from out-of-order here: 2.12x** (BERT: 1.71x). Its matmul
  blocks make 3.32M L2 demand accesses on every host, through a 2 MiB L2. By Little's
  law (misses × average miss latency / time) O3 keeps **6.8** L2 misses in flight on
  average, Minor **1.5** — on BERT 7.0 vs 2.1. Minor's misses are individually
  *cheaper* (45 vs 89 cycles); it is slower because it overlaps so few of them.
- **The PIM run gains less: 1.71x** (BERT: 1.81x). 72% of the O3 PIM time is the host
  asleep while the device works, which no host core changes (BERT: 46%). O3 speeds up
  the remaining host work 3.56x, but on only 28% of the time.

2.12 / 1.71 = 1.24 = 28.29 / 22.81. ⇒ **"Out-of-order widens PIM's lead" is not a
general rule; which host shows the bigger ratio depends on the workload.** In absolute
time O3 + PIM is still the fastest system (1.34M vs 2.29M cycles). Minor's memory
parallelism is gem5's default Minor LSQ, not a measured core.

Checks, all passed: baseline matmul simInsts identical on every host; PIM matmul
blocks agree within 4 instructions (O3 = matched O3 exactly; the only asynchronous
input in `gpt3_pt` is the completion interrupt — no LR/SC/AMO, no timer CSRs);
baseline L2 demand accesses 3.32M on every host; sleep host-invariant within 1.2%;
`inst.quiesce` = 25 everywhere; all five runs exited normally (`m5_exit` / SE end).
Runs: `m5out/gpt3_{pt,blas_c}_{minor,o3v61}`, `m5out/gpt3_pt_o3`.

```
                                    matmul (1,3,5,7)      layer
  CPU baseline  (gpt3_blas_c)          25,823,672      27,374,382
  PIM           (gpt3_dec_b)            1,349,774       3,490,720
                                    ----------------
  SPEEDUP                                  19.13x           7.84x
```

For comparison, BERT-base on the same host was **15.28x**. Bigger matmuls suit
PIM better: the baseline streams 170 MB of weights per token with no reuse, and
2560 is a longer reduction per command than 768.

Per-block, PIM side, with the sleep:

```
  1 QKV       338,783   asleep 230,241  (68.0%)   busy 108,542
  2 attn    2,098,320   asleep       0             busy 2,098,320
  3 Wo        128,417   asleep  83,913  (65.3%)   busy  44,504
  4 res/ln      8,092   asleep       0             busy   8,092
  5 W1        419,978   asleep 329,255  (78.4%)   busy  90,723
  6 GELU       31,275   asleep       0             busy  31,275
  7 W2        462,596   asleep 314,309  (67.9%)   busy 148,287
  8 res         3,260   asleep       0             busy   3,260
            ---------          -------
  layer     3,490,720          957,718   = 27.4% of layer, 71.0% of matmul
```

**The sleep is 71.0% of the matmul sections**, up from 41.8% on BERT. In every
matmul block the sleep exceeds the busy time, so in principle ALL host work
could hide and the sections would cost only their sleep:

```
  1,349,774  ->  957,718     1.41x      speedup 19.13x -> ~26.9x
```

That is the ceiling the overlap work is aiming at.

---

## THE WORKLOAD — why it changed

`bert.c` at SEQ=1 was **mislabelled**. BERT is encoder-only and never runs at
one token; SEQ=1 is GPT-style autoregressive decode. The dimensions were
BERT-base's, the access pattern was GPT's.

Now: **GPT3-2.7B**, from PIMony's own `configs/model_configs/gpt3-2.7B_single_layer.json` —
`d_model 2560 · n_head 32 · d_head 80 · d_interm 10240 · FP16 · input 256 · output 128`.
Using their config makes the workload directly comparable to their paper.

Three changes to the layer, PIM side unaffected by all three:
1. **KV cache** `Kc/Vc[CTX+1][HIDDEN]`, `CTX` defaults to 256, `-DCTX=n`.
2. **Decode attention** — one query over CTX+1 cached keys. Causal masking is
   implicit (everything cached is already in the past). `att` is `[HEADS][CTX+1]`.
3. **Pre-layernorm** — GPT normalises before each sub-block, BERT after. Two
   lines move; block 8 becomes a bare residual.

**Why this matters for the thesis:** attention grows as O(S_L) while QKV/FFN stay
fixed. Context length is now the knob that sizes the overlap window, and it does
NOT touch batching — so the batch-1 case the supervisor settled stays intact.
At BERT's SEQ=1 attention was 5,917 cycles; here it is 2,098,320.

### Why not just raise SEQ

`RESULTS_pim_gemv.md:179` — PIM's case is gone by SEQ~64. Raising SEQ turns the
matmuls into GEMM, the baseline gets weight reuse, PIM gets none. Context length
is the knob that grows CPU work WITHOUT destroying the result.

---

## SIZING — three things were undersized, all found before the first run

A 7-agent audit read the code at the new dimensions (2026-09-21). Findings that
mattered, all fixed in `gpt3.c`:

| what | why | fix |
|---|---|---|
| `PIM_OUT_SLOTS` | macro had no `PIM_PARTS(HIDDEN)*FFN` term — invisible while `PIM_PARTS(768)==1`. At 2560/10240 W1 needs 3 x 10240 = 30,720 against 25,600 allocated. | max over BOTH shapes. No-op at BERT dims. |
| `PIM_NDESC` | `5 + PIM_PARTS(FFN)` assumed one descriptor per matmul. Now every matmul is multi-part: 5x3 + 10 = **25** `pim.gemv` per layer. | `5*PIM_PARTS(HIDDEN) + PIM_PARTS(FFN)` |
| `D_WQ..D_W2` | fixed bases 0..5 overlap once each matmul owns 3 slots | space them by `PIM_PARTS(HIDDEN)` |

⚠️ The `PIM_OUT_SLOTS` overrun **would not have crashed**. It lands in `.bss`
alignment padding. Adding the KV cache reorders `.bss` and can close that hole,
at which point the device silently overwrites a live array. Three independent
agents found it.

Measured after the fix: weights **170.00 MiB**, `.bss` **179.0 MiB** of a 2 GB
range, 78.6 M MACs/layer (11.1x BERT), 25 `pim.gemv`/layer.

**Runtime:** ~100 min per o3 run, and nearly all of it is `init_params()` —
78.6 M weights generated one at a time, before `m5_reset_stats()`, contributing
nothing. The checkpoint fix is designed in the audit and NOT built.

---

## THE FAIRNESS AUDIT — four differences that had nothing to do with PIM

The first GPT-3 runs gave a whole-layer number polluted by code differences
between `gpt3.c` (bare metal) and `gpt3_blas.c` (SE + libc). Found and removed:

| difference | size | fix |
|---|---|---|
| attention: hand-written C vs `cblas_sgemv` | 1.33x on block 2 | both call raw `sgemv_t`/`sgemv_n` |
| softmax: `k_expf` vs libm `expf` | **1.8x per call**, 8,224 calls/layer | `k_expf` both sides |
| GELU: `k_tanhf` vs libm `tanhf` | **19.39x on block 6** | `k_tanhf` both sides |
| Q/K/V alignment, `-mcmodel` | small | matched |

**`k_expf` is 1.8x FASTER than libm** — measured, 164,480 calls: 1,499,376 vs
2,701,117 cycles, 9.1 vs 16.4 cyc/call. The hand-written code was never the
slow one; the assumption that it was cost an afternoon.

### THE CONTROL THAT NOW EXISTS

```
  block 4 (residual + layernorm)   PIM 8,092   CPU 8,110   = 1.00x
```

Identical code, identical machine, identical result. **Any future A/B should
check block 4 first.** If it is not ~1.00x, the two binaries differ in something
other than the thing being measured.

### THE BARE-METAL OpenBLAS RESULT — a standing assumption was wrong

`README.md` says `bert_blas.c` needs libc, so the PIM build can't use OpenBLAS.
That is true of **`cblas_sgemv`**, which pulls `blas_memory_alloc` and
`__assert_fail`. It is **not** true of the kernels underneath:

```
$ nm -u sgemv_n.o sgemv_t.o
(nothing)
```

Zero undefined symbols. Proven twice, in `gpt3/blastest/`:
1. **Orientation** — `sgemv_t` for scores, `sgemv_n` for context, on a row-major
   head block with `lda = HIDDEN`: exact match against a scalar reference.
2. **Bare metal** — links with `-nostdlib`, runs under `fs_bert.py`, exits via
   `m5_exit` (the test spins forever unless the results are right).

Signature, `/opt/OpenBLAS/kernel/riscv64/gemv_n.c:41`:
```c
int sgemv_n(long m, long n, long dummy, float alpha,
            float *a, long lda, float *x, long incx,
            float *y, long incy, float *buffer);
```
Column-major with `lda = HIDDEN` makes the row-major head block
`A[i][j] = Kc[j][off+i]`, i.e. HEAD_DIM x L. Both accumulate into `y`, so zero
it first. `-nostdlib` also needs a local `memset` — GCC turns the zeroing loops
into calls to it (`no-tree-loop-distribute-patterns` stops it recursing).

**This was never tried before because it never mattered** — on BERT the PIM
build had no CPU-side matmul at all.

---

## ⚠️ WHOLE-LAYER COMPARISON IS UNSOUND — this is a hard result, not caution

**PIMony computes no values.** The PIM build's activations are essentially just
the biases; the baseline's are real numbers. Both `k_expf` and `k_tanhf` branch:

```c
if (v >  87.0f) return INFINITY;          /* k_expf   */
if (v < -87.0f) return 0.0f;
if (v > -0.05f && v < 0.05f) ...          /* k_tanhf  */
```

Different data ⇒ different branches ⇒ different instruction counts, with
identical source. Quantified exactly:

```
  attention insts   PIM 798,095   CPU 649,318   difference 148,777
  exp calls/layer   32 heads x 257 = 8,224
  148,777 / 8,224 = 18.1 instructions per call
```

That is precisely the full polynomial versus an early return. And it predicts
the rest: **block 4 is 1.00x because layernorm has no branches**; blocks 2 and 6
are the two branchy ones and are the two that still differ.

**Not fixable.** PIMony will never produce values. ⇒ only blocks 1/3/5/7 are
comparable, because they contain no data-dependent branches.

### It reaches back into the BERT numbers

The same libm-vs-hand-written split existed in `bert.c`/`bert_blas.c`:

```
  BERT, o3        PIM      baseline    ratio
  2 attention   5,917        11,793    1.99x
  6 GELU       10,790       209,205   19.39x   <- CPU code on both sides
```

Substituting PIM's own block 2 and 6 costs into the baseline:

```
  whole layer   14.72x published  ->  ~13.83x fair   (~6% inflated)
  matmul only   15.28x            ->  UNCHANGED
```

⇒ **`GAPS_pim_gemv.md` needs a correction note on the whole-layer figure.** Not
written yet. The headline 15.28x is safe — this is exactly what the "quote
matmul-only" rule was protecting against, and it worked.

---

## G-OV — HOST/PIM OVERLAP · MECHANISM CONFIRMED, PRIZE IS STRUCTURALLY SMALL

⚠️ **This section was rewritten 2026-09-25. The earlier version reported the
overlap as NET NEGATIVE (19.13x → 18.37x). That was wrong, and the error is
worth keeping because it is the same mistake in a new place: the control was
handicapped.** See "the mistake" below.

`pim.gemv` returns immediately and `pim.wait` blocks separately, precisely so the
host can work in between. Nothing had ever used it: `pim_linear` was
issue → wait → read back, per slab.

**What makes it possible:** one matmul is not one PIM job. A `pim.gemv` reduces
over at most 1024 inputs (a 2 KB DRAM row at FP16, and a MAC cannot cross a row).
`in_dim` 2560 ⇒ 3 slabs, 10240 ⇒ 10. **25 jobs and 25 windows per layer.**

**The rule:** work may move into a window only if it does not depend on the job
being waited on.

### THE RESULT — cross-matmul read-back, o3

⚠️ Broken-O3 numbers. Rerun 2026-10-03 (fixed O3): block-1 sleep 234,076 → 189,022,
**45,054 filled** — see the rerun block at the top.

Block 1 is three INDEPENDENT matmuls, so each one's read-back is legal inside the
next one's windows. `-DPIM_OVERLAP_MM`:

```c
pim_linear_issue(D_WQ, ...);                              /* Q runs alone     */
pim_linear_overlap(D_WK, ..., D_WQ, bq, Q, ...);          /* read Q under K   */
pim_linear_overlap(D_WV, ..., D_WK, bk, K, ...);          /* read K under V   */
pim_linear_read(D_WV, bv, V, ...);                        /* no window left   */
```

```
                        matmul (1,3,5,7)   block 1 asleep   instructions
  control (v2_ctl)          1,345,853          235,772        1,977,262
  OVERLAP (v2_ov)           1,317,187          214,403        1,977,259
                                               -------
                            reclaimed           21,369
```

**Clean, and isolated exactly where predicted.** Instructions moved −0.00%, so
nothing was skipped. Block 1's sleep fell by 21,369 and **every other block's
moved by under 400** — which is what you would see if the mechanism works and the
dependency analysis is right.

**19.19x → 19.61x.** ⚠️ Do not quote that to three digits: `fetch/inst` moved
0.1027 → 0.0896 and blocks 2 and 7 shifted by more than block 1 did, so the
*total* is inside layout noise. **The sleep figure is the trustworthy one.**

### WHY THE PRIZE IS SMALL — the structural finding

```
block 1 busy total          102,419 cycles
  of which read-back         ~32,000   (Q, K and V)
  of which hidden             21,369   (Q and K; V has no window after it)
  the rest                   ~70,000   layernorm, pim_store_vec, pim_flush_vec
```

That last 70,000 **cannot move**: it produces the operand vector Wq consumes, so
it must run before the first issue. Block 1's ceiling was never 235,772 — it was
~32,000, and two thirds of that is now captured.

The earlier estimate of ~24,000 cycles for one read was wrong; output-major it is
about **10,700**, against a ~26,000-cycle window. So the read fits in ONE window
with room to spare, and splitting it across windows would buy nothing. That was
checked by measurement, not assumed.

### ⚠️ THE MISTAKE, KEPT ON PURPOSE

The first attempt (`-DPIM_OVERLAP`, slab-major) reported 28.9% of the sleep
reclaimed but a NET LOSS of 19.13x → 18.37x. Both halves were artefacts:

```
  original  (dec_b)       1,349,774
  slab-major control      1,658,066     <- 23% slower, and NOT because of overlap
  slab-major overlap      1,405,532
```

To consume one slab at a time, the read-back was rewritten slab-major — a pass
over `y` per slab instead of one pass over outputs with the sum in a register.
For W2 that is 25,600 stores of `y` instead of 2,560. It cost **+23%
instructions**, and it had nothing to do with overlap. The control carried the
same penalty, so the A/B *between* them was valid while the absolute number was
not.

Proof it is now repaired: `v2_ctl` measures **1,345,853** against the original's
1,349,774 — **−0.29%**.

**The rule this gives you: a control must differ from the test build in ONE
`#define` and nothing else.** Not "an earlier run of roughly the same thing".
Same source, same day, same flags. This is the second time in this file that an
unrelated code change contaminated a comparison — the first was libm vs
hand-written `exp`/`tanh` (see the fairness audit).

### 77% OF THE SLEEP IS STRUCTURALLY UNREACHABLE — report this as a result

⚠️ Rerun 2026-10-03 (fixed O3): **79%** (721,637 of 910,659). The argument is unchanged.

```
block 1  asleep  214,403   <- the only block with independent work
block 3  asleep   82,564
block 5  asleep  332,692
block 7  asleep  315,669
                 -------
unreachable      730,925   = 77% of all sleep in the layer
```

`Wo` needs `ctx`, `W1` needs `norm1`, `W2` needs `hid` — and each is produced by
the code immediately before it. Nothing in the layer is independent of them, and
the next layer depends on this one's output, so there is no escape upward either.
Block 1 is the exception only because Q, K and V happen to be three separate
matmuls.

⇒ **A single inference stream runs out of independent work.** Filling the other
77% requires work from *outside* the stream — a second request or a second
process — which is exactly what **G10** (one `pim.gemv` at a time) forbids.
**The overlap result and the sharing hole are the same argument from two sides,
and that is the strongest thing this section says.**

---

## G-INT — THE SCORE HALF OF ATTENTION, AND THE FIRST INTERFERENCE MEASUREMENT

`-DPIM_OVERLAP_ATT`, measured 2026-09-25. **This is the most important result in
this file. It is not a speedup.**

Attention splits cleanly along the dependency:

```
1.  scores  = q_h . Kc_h^T      needs Q and K
2.  scale                        needs nothing new
3.  softmax                      needs nothing new
4.  ctx     = weights . Vc_h     needs V
```

PIM produces Q, K, V in that order, so steps 1–3 are legal during Wv's three
windows and step 4 is not. Block 1 issues Wv slab by slab and scores ~11 heads
per window; block 2 then does step 4 only.

⚠️ **THE METRIC CHANGES.** The scores move out of block 2 and into block 1, so
"matmul-only" is not a clean category for this build. **Compare blocks 1+2.**

#### The result

⚠️ Broken-O3 numbers. Rerun 2026-10-03: +3.2% per miss vs `v2_ov`, +5.1% vs the
sleep-only `v2_ctl`; blocks 1+2 36,840 cycles WORSE — see the rerun block at the top.

```
                              control (v2_ov)   + ATTENTION (att)
  blocks 1+2                     2,407,738        2,432,799     +25,061  WORSE
  block 1 asleep                   214,403          142,631     -71,772  filled
  instructions                   1,024,118        1,019,307      -0.47%
  L2 misses                         58,052           58,067      +0.03%
  L2 miss latency (ticks)           75,911           79,473      +4.69%  <--
  busy time                      2,193,335        2,290,168      +4.41%
```

**The overlap worked** — 71,772 cycles of sleep filled, and the scores really did
run inside the windows.

**But the same work became 4.4% more expensive.** Same instruction count, same
number of cache misses, and **each miss costs 4.69% more**. The busy-time
increase tracks the latency increase almost exactly, so the chain is clean:
it is not the code, it is the memory.

#### WHY THIS IS THE RESULT

The thesis task description opens with:

> *PIM operations coexist with ordinary host memory accesses*

**Until this run they never did.** The host slept through every PIM job, so there
was nothing to interfere with. This is the first time the CPU streams the KV
cache (5.02 MiB, against a 2 MiB L2) out of DRAM *while* PIM is computing inside
it. The measurement is what that costs: **+4.7% on every host memory access.**

That is the phenomenon PIMony's paper exists to address, measured on this
platform, produced by these instructions, on a workload their own setup cannot
generate — their CPU runs SPEC and never issues the PIM work (see the Fig. 9
section at the end of this file).

⇒ **The idle window is not free capacity. Using it costs memory bandwidth, and
here the cost exceeded the gain.** Report the mechanism and the price, not a
ratio.

⚠️ **One honest limit.** This shows the contention is in the memory system. It
does NOT separate *PIM's commands competing with the CPU* from *the CPU's own
access pattern becoming more fragmented* by being interleaved with descriptor
writes and fences. Both are interference; distinguishing them needs another
experiment. (Run 2026-09-30 — "THE POLICY SPLIT" below: ~2 points are
policy-sensitive contention; the rest cannot be split by this method.)

#### THE POLICY SWEEP THAT WOULD HAVE SEPARATED IT — FIRST ATTEMPT BLOCKED (done 2026-09-30, below)

Plan: rerun under PIMony's other memory configs to show how much of the 4.7% its
scheduler is already absorbing. `fs_bert.py` now takes the config as `argv[5]`
and prints it; `configs/pimony/pimony_mem_{pim_first,mem_first,balanced}.json`
were created.

**All four runs SEGFAULT**, ~2.5 h in, in
`BankState::GetReadyCommandSingle`. Cause, `channel_state.cc:197-231`:

```c
} else if (compute_mode_ == ComputeMode::ASYNC && (MAC||MACINTR||READRES)) {
    for (k = 0; k < banks_per_group; k++)  ... bank_states_[rank][bg][k] ...
} else {
    ... bank_states_[rank][bg][cmd.Bank()] ...      /* indexes BY bank */
}
```

Our sequencer emits MACs with **no bank field** — correct, the CA table gives MAC
and READRES no bank bits. `pimony.ini` runs `compute_mode = ASYNC` and takes the
first branch. The other three `.ini` files switch to `ALL_BANK`, so the same
command falls to the second branch and indexes with `bank = -1`.

**The files change THREE things at once** — `scheduling_policy`, `compute_mode`
and `bank_mode` — and we only wanted the first:

```
pimony.ini      MEM_FIRST  +  ASYNC     +  DPSA
pim_first.ini   PIM_FIRST  +  ALL_BANK  +  SINGLE
```

**If picked up:** copy `pimony.ini` and change ONLY `scheduling_policy` to
`PIM_FIRST`. Same single-variable discipline as everything else in this file.

⚠️ **This is NOT a limitation of the instructions.** `ASYNC` is PIMony's flagship
mode — the one their paper argues for, and the interrupt-preemptible model the
thesis task description says to build on. The `ALL_BANK` configs are the
*baselines PIMony beats*. State it as scope:

> *"The sequencer emits asynchronous-mode commands, which carry no bank field.
> Supporting the legacy all-bank mode would require a second command shape and
> was not attempted."*

#### THE POLICY SPLIT — RUN 2026-09-30

⚠️ Broken-O3 numbers. Rerun 2026-10-03: CPU first +5.07% (75,296 → 79,110), PIM first
+8.70% (75,211 → 81,754) ⇒ ~3.6 policy-sensitive points — see the rerun block at the top.

`configs/pimony/LPDDR5X_12Gb_x16_8533_pimony_pimfirst.ini` = `pimony.ini` with ONLY
`scheduling_policy = PIM_FIRST`; `configs/pimony/pimony_mem_pimfirst_only.json` points
at it (`fs_bert.py` argv[5]). Under ASYNC the setting is live: PIM_FIRST keeps host
reads/writes out of the command queue while their bankgroup has PIM commands pending
(`pim_command_queue.cc:276`); MEM_FIRST lets a host read preempt a MAC unless the MAC
is nearly done (`pim_controller.cc:458`). Smoke test `gpt3_tiny`: ROI blocks moved up
to 14%, instructions identical ⇒ the setting takes effect.

The overlap control `v2_ov` cannot run under PIM_FIRST (it livelocks, next section),
so the control is `v2_ctl` — no overlap, the host sleeps through every job.
Blocks 1+2, O3:

```
  avg L2 miss latency (ticks)   v2_ctl (no overlap)   att (scores in window)   att vs ctl
  MEM_FIRST  (CPU first)              74,592                 79,473               +6.54%
  PIM_FIRST  (PIM first)              76,510                 82,983               +8.46%
  policy shift                        +2.57%                 +4.42%
```

Instructions 1,024,117 / 1,024,119 (ctl), 1,019,307 / 1,019,296 (att); L2 misses within
1.3% (ctl 61,039 / 61,830, att 58,067 / 58,637). Same work under both policies.

**Reading.** The overlap penalty moves with the policy, 6.5% → 8.5%: part of it IS
PIM's commands competing with the CPU's. Give PIM the priority and the overlapped
run's misses get ~2 points slower on top. Against `v2_ov` the MEM_FIRST penalty is
the 4.69% above; the other 1.8 points come from the overlap structure itself
(`v2_ctl` → `v2_ov`: 74,592 → 75,911).

⚠️ **What this cannot split.** Even with CPU priority, a CPU read that preempts a MAC
pays to close the PIM's row and open its own (MACINTR, PRECHARGE, ACTIVATE) — a
contention cost no policy removes. So the MEM_FIRST 6.5% is not "pure access
pattern". The experiment bounds the policy-sensitive part (~2 points), not the whole.

#### PIM_FIRST LIVELOCKS THE OVERLAP CONTROL — a PIMony model bug · diagnosed 2026-09-30

`m5out/gpt3_v2_ov_pimfirst_o3` never reached its first dump: 3h27m, 62 GB of stdout
(a full run writes ~6.3 GB), 6.5 MB/s against 0.4–0.8 MB/s for a healthy run. Killed.
A 3 MB sample of its output is nothing but ACTIVATE / PRECHARGE on one bank, on both
channel halves, plus refreshes — no READ, WRITE, MAC or D2GWRITE:

```
  clk 605339225  ACTIVATE   row 8788   CPU read of bk opens its row
  clk 605339226  PRECHARGE             asked by pimvec's D2GWRITE: closes 8788
  clk 605339229  ACTIVATE   row 8215   our sequencer's vector load opens its row
  clk 605339230  PRECHARGE             asked by the CPU read: closes 8215
  ... every 8 cycles, forever. tRCD = 19 never elapses; refresh does not break it.
```

Both rows are bankgroup 0, bank 0, subarrays 16 and 17. Every step is in PIMony's
model (internals: documented, not fixed):

- **D2GWRITE is built with `for_pim = false`** (`TransToCommand`), so it shares the
  bank's normal row port with host reads.
- **`BankState::GetReadyCommandSingle` times a command by the REQUESTER's subarray,
  not the subarray of the row it closes.** A PRECHARGE asked for by a subarray-16
  request that closes a subarray-17 row is timed as "other subarray": tRAS (45) and
  tRP (19) are skipped; only tRRD_L (4) paces the ACTIVATEs.
- **`ArbitratePrecharge`:** a D2GWRITE may always precharge; the host read checks only
  its own per-bank queue, and the D2GWRITE sits in the PIM queue, invisible to it.

Real DRAM cannot do this: a row must stay open tRAS before it closes. Why only the
PIM_FIRST run hit it is NOT established — none of the three steps above reads the
policy. `v2_ctl` and `att` under PIM_FIRST both completed normally.

#### OPEN — PIM_FIRST MAKES THE PIM JOBS SLOWER · write-back suspect RULED OUT 2026-10-03

Giving PIM the priority made PIM slower, on both workloads, host asleep during every
job, instructions identical:

```
                                 PIM sleep, matmul blocks 1/3/5/7
                                  MEM_FIRST    PIM_FIRST
  GPT-3  v2_ctl  (fixed O3, _f2)    961,538    1,152,339    +19.8%   (broken O3: +20.6%)
  BERT   g7pt    (fixed O3, wlog)    95,301      107,177    +12.5%
```

**The write-back suspect is ruled out.** It was: the sequencer's result write-back is an
ordinary WRITE (`AddPIMTransaction(WRITE)`), which PIM_FIRST refuses while its bankgroup
has PIM commands queued (`pim_command_queue.cc:276`) — our own MACs — so the sequencer
would throttle itself. Measured on BERT with the write lines kept: the 2,112 write-backs
do wait longer (added → issued, mean 323 → 406 DRAM clk), but they cannot hold up the
job. PIMony acknowledges a write one cycle after accepting it
(`PIMController::AddTransaction`, `complete_cycle = clk_ + 1`), and the write buffer
peaked at 47 of 96, so the sequencer was never refused a write. **The hardware-contract
point proposed here is withdrawn.**

Cause still open. The policy enters PIMony in exactly two places:
`pim_command_queue.cc:276` (host R/W refused while the bankgroup has PIM commands) and
`pim_controller.cc:458` (MEM_FIRST: a host read may preempt a MAC). Every quoted number
uses MEM_FIRST, so none depends on this.

Runs `m5out/bert_pim_g7pt{,_pimfirst}_o3_wlog`: `fs_bert.py o3 configs/scratch/progs/bert_pim_g7pt`
(+ `1 4 configs/pimony/pimony_mem_pimfirst_only.json`), stdout through
`grep -a -E "[|] (WRITE|WRITE_PRECHARGE|MAC|MACINTR|READRES) [|]|Exiting|PIM State|panic|fatal"`.
The MEM_FIRST one reproduces `bert_pim_g7pt_o3_f2` exactly (253,794, same exit tick).

```
cd gem5/m5out && for d in bert_pim_g7pt_o3_wlog bert_pim_g7pt_pimfirst_o3_wlog gpt3_v2_ctl_o3_f2 gpt3_v2_ctl_pimfirst_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^system.cpu.quiesceCycles /{q[b]=$2} /^simInsts /{i[b]=$2} END{printf "%-32s PIM sleep %8d   insts %d\n", d, q[1]+q[3]+q[5]+q[7], i[1]+i[3]+i[5]+i[7]}' $d/stats.txt; done
for d in bert_pim_g7pt_o3_wlog bert_pim_g7pt_pimfirst_o3_wlog; do python3 ../configs/scratch/progs/writeback_wait.py $d/run.log; done
```

## CHECKPOINTING — BUILT, MEASURED, NOT USED · 2026-09-25

~90 of every 100 minutes of a run is `init_params()` generating 78 M weights
before `m5_reset_stats()`. The obvious fix is a checkpoint at that point.

**Built and it works.** `m5_checkpoint()` in `gpt3.c` behind `-DTAKE_CKPT`
(M5OP 0x43 ⇒ `.word 0x8600007B`), and `save:<dir>` / `restore:<dir>` as
`argv[6]` of `fs_bert.py`. Save exits with cause `checkpoint`; **restore takes
15 seconds.**

**But it is not transparent, so it is not used.** Tiny build (HIDDEN 256,
HEADS 4, FFN 1024, CTX 32 — dimensions are `#ifndef`-guarded for exactly this),
restored run against the same binary run straight through:

```
  blk    plain ticks   restored ticks    delta
   2       6,052,275        8,431,227   +39.3%   and 5 FEWER instructions
   4         256,077          284,382   +11.1%
   8          90,576          200,799  +121.7%
  layer   27,069,237       29,843,793   +10.3%
```

**gem5 checkpoints do not carry cache contents.** A restored run starts with
cold L1 and L2; a straight-through run inherits whatever `init_params` left
warm. The small blocks are hit hardest, which is exactly the pattern. The
5-instruction difference says something beyond timing also differs.

Using it would mean re-baselining every number in this file and the report onto
cold caches. **Decision: abandon.** The code is left in place and inert so the
next person does not rediscover this.

⚠️ Also: a checkpoint contains the binary's memory image, so it is **per-binary**.
It amortises config sweeps (policies, cache sizes, ROB), never binary sweeps.

**Cost of finding this out: 15 seconds, because the dimensions are overridable.**
Any mechanism question should be asked of a tiny build first.

---

## BINARIES AND RUNS

| binary | build | run |
|---|---|---|
| `gpt3_pim` | GPT-3 dims, BERT attention (step 1 only) | `m5out/gpt3_pim_o3` |
| `gpt3_dec` | + KV cache, decode attention, pre-LN | `m5out/gpt3_dec_o3` |
| `gpt3_dec_b` | + BLAS attention kernels | `m5out/gpt3_dec_b_o3` |
| `gpt3_dec_c` | + slab-major read-back (superseded control) | `m5out/gpt3_dec_c_o3` |
| `gpt3_dec_ov` | + `-DPIM_OVERLAP`, slab-major (superseded) | `m5out/gpt3_dec_ov_o3` |
| `gpt3_mm_ctl` / `_ov` | cross-matmul, but still slab-major read (superseded) | `m5out/gpt3_mm_*_o3` |
| `gpt3_v2_ctl` | read-back RESTORED, overlap off — **the control** | `m5out/gpt3_v2_ctl_o3` |
| `gpt3_v2_ov` | + `-DPIM_OVERLAP_MM` | `m5out/gpt3_v2_ov_o3` |
| `gpt3_att` | + `-DPIM_OVERLAP_ATT` | `m5out/gpt3_att_o3` |
| `gpt3_pt` | current `gpt3.c`, PIM recipe, page table preloaded (G12) — **the Minor vs O3 PIM build** | `m5out/gpt3_pt_{minor,o3,o3v61}` |
| (PIM_FIRST) | `v2_ctl`, `att`, `v2_ov` with argv[5] `configs/pimony/pimony_mem_pimfirst_only.json` | `m5out/gpt3_{v2_ctl,att}_pimfirst_o3`; `gpt3_v2_ov_pimfirst_o3` = the livelock, killed |
| `gpt3_blas` | baseline, `cblas_sgemv` attention | `m5out/gpt3_blas_o3` |
| `gpt3_blas_b` | baseline, raw-kernel attention | `m5out/gpt3_blas_b_o3` |
| `gpt3_blas_c` | + `k_expf`/`k_tanhf`, `-mcmodel=medany` | `m5out/gpt3_blas_c_{o3,minor,o3v61}`; rerun 2026-10-10 `m5out/gpt3_blas_c_o3_oct` (bit-identical to `_o3`) |
| `gpt3_prefill_s{4,16,64,128,256,512}` | `gpt3_prefill.c -DSEQ=n`, the `gpt3_blas_c` flags — prefill sweep (top block) | `m5out/gpt3_prefill_s*_o3_oct` |
| `gpt3_prefill_check` | `gpt3_prefill.c` tiny (320/4/1280, SEQ 16) + `-DCHECK_ATTN` | `m5out/gpt3_prefill_check` (timing) |

Host runs: `minor` as `argv[1]`; O3 with Minor's vector unit = `o3 … 6 1`
(`fs_bert.py` / `se_bert.py` argv[3]/[4]).

PIM: `-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -fno-math-errno
-fassociative-math -fno-signed-zeros -fno-trapping-math -DFREESTANDING -DNO_IO
-DBAREMETAL -DPIM_LAYOUT -DPIM_GEMV -DPHASE_STATS -nostdlib -nostartfiles
-T gpt3_bm.ld  gpt3.c sgemv_n.o sgemv_t.o -lgcc`, host toolchain. Without the four
math flags the link fails on `sqrtf` (they are what the shipped binaries used).

Baseline: container toolchain, `-static -DBLAS_FP16 -DPHASE_STATS
-I/opt/openblas-zvl256b/include ... /opt/openblas-zvl256b/lib/libopenblas.a -lm`.

Run: `docker start gem5-container` then
`docker exec -w /home/pimony/gem5 gem5-container build/RISCV/gem5.opt -d m5out/<dir>
configs/pimony/{fs,se}_bert.py o3 configs/scratch/progs/gpt3/<binary>`.
`m5out/` is root-owned — any `tee` must run inside the container.

**Always check the `workload:` line.** `fs_bert.py:115` and `se_bert.py:112`
print it; `argv[2]` selects the binary.

### Side tests kept

- `gpt3/blastest/` — orientation + bare-metal link proof for the BLAS kernels.
- `gpt3/modetest/` — SE-vs-FS on an isolated KV walk (**neutral**: 11,798,301 vs
  12,119,871, identical L2 miss latency) and `k_expf` vs libm `expf`.

The SE/FS result is worth keeping: it independently re-confirms the 0.00027%
mode-tax calibration on a memory-bound access pattern, which is where it was
most open to doubt.

---

## PIMony's own interference benchmark — for later

`main.cpp` takes `--normal_trace`; `run.sh` plays a random trace against six
memory configs. But their **paper** (Fig. 9) used SPEC CPU2017 on
`configs/example/gem5_library/x86-spec-cpu2017-benchmarks_{mobile,laptop}.py`:
`SimpleProcessor(CPUTypes.O3, isa=X86, num_cores=8)` with PIM work generated
*inside* the memory by `LLMGenerator`.

**Their CPU never issues the PIM work.** Two unrelated tenants colliding. Ours is
issued by an instruction, and the CPU-side half of the same application is what
coexists with it. Not a reproduction of Fig. 9 — a case their design cannot
express. We have 1 RISC-V core against their 8 x86; no direct comparison.

In our gem5 build `memory_system.cc:98` constructs `TraceRequestHandler` with an
empty path, so their normal-traffic input is wired up and fed by the real CPU.
Today it is silent during every PIM job, because the CPU is asleep. **That is the
gap the overlap work closes**, and it is the task description's
*"PIM operations coexist with ordinary host memory accesses"*.
